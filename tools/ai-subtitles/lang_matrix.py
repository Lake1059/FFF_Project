"""L3 模型级矩阵：30 个语言英文名喂给 llama-server，看模型认不认、我们的解析会不会漏。

为什么不在宿主里测：宿主这条链路（设置 → 管道 → worker → HTTP → 解析 → 闸门）里
唯一"不可控"的一环就是模型对 language 字段的反应。把它单独钉死，
以后换模型/换 llama.cpp 版本时只要重跑这一个脚本就知道还成不成立。

关键点：脚本 import 的是 **worker 自己的** transcribe() / same_language()，
不是复制的解析逻辑 —— 测的就是生产代码，正则失配（把 "language Chinese"
漏进字幕正文）这类问题只有在真跑时才会暴露。

用法（必须用装了 requests/soundfile 的 venv python）：
    python lang_matrix.py E:\\...\\samples\\asr_en.wav --seconds 10 \
        --llama E:\\...\\llama-server.exe --model E:\\...\\Q8_0.gguf \
        --mmproj E:\\...\\mmproj-Q8_0.gguf --out _3fc_tmp/lang_matrix.md
退出码 = 未接受的语言个数（0 = 全接受）。

⚠️ 这是**按需工具**，不是常规回归：一次整表扫描 = 31 次真实推理，会把 GPU 占满几分钟。
   日常只跑 --only <一个语言> 单点复核；整表留给"换模型/换 llama.cpp 版本"这种节点。
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import soundfile as sf

try:
    import soxr  # worker 里是函数内导入，这里单独备一份
except ImportError:  # pragma: no cover
    soxr = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asr_worker as W  # noqa: E402  （只有 __main__ 才解析参数，import 安全）


class _TimedSession:
    """把单请求超时压到测试用的小值：transcribe() 里把 timeout 写死成 120s，
    这里不改生产代码，只替换 session 对象把 timeout 顶上。"""

    def __init__(self, timeout):
        self.timeout = timeout

    def post(self, *a, **kw):
        kw["timeout"] = self.timeout
        return W._HTTP.post(*a, **kw)

    def get(self, *a, **kw):
        return W._HTTP.get(*a, **kw)


def load_16k(path, seconds):
    data, rate = sf.read(path, dtype="float32")
    if data.ndim > 1:
        data = W._downmix(data, data.shape[1], None)
    if rate != W.TARGET_RATE:
        if soxr is not None:
            data = soxr.resample(np.ascontiguousarray(data), rate, W.TARGET_RATE)
        else:
            n = int(round(len(data) * W.TARGET_RATE / rate))
            data = np.interp(np.linspace(0, len(data) - 1, n), np.arange(len(data)), data).astype(np.float32)
    return np.ascontiguousarray(data[: int(seconds * W.TARGET_RATE)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--only", default="",
                    help="只跑这些目标（逗号分隔的 id 或英文名），空 = 全部。\n"
                         "按需单点用，别整表扫 —— 31 次请求要占满 GPU 几分钟。")
    ap.add_argument("--port", type=int, default=18077)
    ap.add_argument("--timeout", type=float, default=45.0,
                    help="单窗请求超时（秒）。生产 worker 是 120s；这里收窄是为了"
                         "让'模型跑飞'表现为一条 TIMEOUT 记录而不是整轮卡死。")
    ap.add_argument("--llama", default=r"E:\WorkBuddy\3FP-AI-Subtitles\llama-b11669\cuda\llama-server.exe")
    ap.add_argument("--model", default=r"E:\WorkBuddy\3FP-AI-Subtitles\Qwen3-ASR-0.6B-GGUF\Qwen3-ASR-0.6B-Q8_0.gguf")
    ap.add_argument("--mmproj", default=r"E:\WorkBuddy\3FP-AI-Subtitles\Qwen3-ASR-0.6B-GGUF\mmproj-Qwen3-ASR-0.6B-Q8_0.gguf")
    ap.add_argument("--out", default="_3fc_tmp/lang_matrix.md")
    args = ap.parse_args()

    samples = load_16k(args.wav, args.seconds)
    print(f"audio {args.wav} -> {len(samples)/W.TARGET_RATE:.2f}s @16k", flush=True)

    # 启动参数刻意与 asr_worker.LlamaServer.start() 的固定默认保持一致
    # （--temp 0.6、--n-predict 512、--cache-ram 0；--no-cache-idle-slots 在
    # 生产里已随失控修复移除——保留它会把长输出请求拖到 171s，矩阵测的就
    # 不再是生产行为了）：矩阵要回答的是"生产里会发生什么"，不是"理想条件
    # 下模型能不能认"。
    outdir = Path(args.out).parent
    outdir.mkdir(parents=True, exist_ok=True)
    log = open(outdir / "lang_matrix.server.log", "w", encoding="utf-8")
    proc = subprocess.Popen(
        [args.llama, "-m", args.model, "--mmproj", args.mmproj,
         "--port", str(args.port), "--host", "127.0.0.1",
         "--ctx-size", "4096", "--n-gpu-layers", "999", "--threads", "8",
         "--temp", "0.6", "--n-predict", "512", "--cache-ram", "0"],
        stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(240):
            try:
                if W._HTTP.get(f"http://127.0.0.1:{args.port}/health", timeout=2).status_code == 200:
                    break
            except Exception:
                pass
            time.sleep(0.5)
        else:
            print("server 未能就绪", file=sys.stderr)
            return 99

        session = _TimedSession(args.timeout)
        # 替身必须带 model_id：asr_worker.transcribe 直接读 server.model_id
        # （多模型适配起引入），缺属性的替身会让每次转写都 AttributeError、
        # 整张矩阵 31 行全 error——工具自多模型提交后一直处于这个失效状态。
        # 空串走 transcribe 里的 "qwen3-asr" 回退，与生产探测失败时的行为一致。
        server = type("S", (), {"port": args.port, "model_id": ""})()
        tmpdir = Path("_3fc_tmp/lang_matrix_windows")
        tmpdir.mkdir(parents=True, exist_ok=True)

        targets = [(None, "auto")] + [(name, ident) for ident, name in W.LANGUAGE_NAMES.items()]
        if args.only.strip():
            要的 = {x.strip().lower() for x in args.only.split(",") if x.strip()}
            targets = [t for t in targets
                       if (t[1] or "").lower() in 要的 or (t[0] or "auto").lower() in 要的]
        rows = []
        jsonl = open(Path(args.out).with_suffix(".jsonl"), "w", encoding="utf-8")
        for i, (name, ident) in enumerate(targets, 1):
            t0 = time.perf_counter()
            err = ""
            try:
                reported, text = W.transcribe(server, samples, tmpdir, session, name)
            except Exception as exc:  # 跑飞/超时/HTTP 错误都要记成一行，不能整轮丢
                reported, text = "", ""
                err = type(exc).__name__
            dt = time.perf_counter() - t0
            matched = W.same_language(reported, name) and not err
            leak = text.strip().lower().startswith("language")
            row = {"id": ident, "target": name or "", "reported": reported,
                   "accepted": bool(name) and matched and not err, "matched": matched,
                   "prefix_leak": leak, "seconds": round(dt, 1), "error": err,
                   "text": text[:120]}
            rows.append(row)
            jsonl.write(json.dumps(row, ensure_ascii=False) + "\n")
            jsonl.flush()
            flag = "OK " if (not name or matched) else "?? "
            print(f"{flag}{i:2d}/{len(targets)} target={name or 'auto':11s} "
                  f"reported={reported or '-':11s} {dt:5.1f}s "
                  f"{err or ''} :: {text[:40]}", flush=True)
        jsonl.close()

        # auto 臂不给语言字段，matched 应恒为 True（R7 默认路径）
        auto = rows[0]
        named = rows[1:]
        accepted = [r for r in named if r["accepted"]]
        rejected = [r for r in named if not r["accepted"] and not r["error"]]
        errored = [r for r in rows if r["error"]]
        leaks = [r for r in rows if r["prefix_leak"]]
        empty = [r for r in rows if not r["text"].strip()]

        md = ["# 语言名接受矩阵（L3）", "",
              f"- 素材：`{args.wav}` 前 {args.seconds:g}s",
              f"- 目标：30 个语言英文名 + auto 基线",
              f"- 判据：模型自报标签 == 目标名（`same_language`）", "",
              f"**接受 {len(accepted)}/30，标签不符 {len(rejected)}，超时/异常 {len(errored)}，"
              f"解析漏前缀 {len(leaks)}，空文本 {len(empty)}**", "",
              "| # | id | 目标 | 模型自报 | 接受 | 耗时 | 备注 | 正文（前 40） |",
              "| --- | --- | --- | --- | --- | --- | --- | --- |"]
        for i, r in enumerate(rows):
            md.append(f"| {i} | {r['id']} | {r['target'] or 'auto'} | {r['reported'] or '—'} | "
                      f"{'✅' if r['accepted'] else ('—' if not r['target'] else ('⏱' if r['error'] else '❌'))} | "
                      f"{r['seconds']}s | {r['error'] or ''} | {(r['text'] or '').replace('|', '/')[:40]} |")
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text("\n".join(md) + "\n", encoding="utf-8")

        data = {"accepted": len(accepted), "rejected": [r["id"] for r in rejected],
                "errored": [r["id"] for r in errored],
                "leaks": [r["id"] for r in leaks], "empty": [r["id"] for r in empty],
                "auto_matched": auto["matched"], "rows": rows}
        Path(args.out).with_suffix(".json").write_text(
            json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")

        print(f"\n接受 {len(accepted)}/30  标签不符 {[r['id'] for r in rejected]}  "
              f"超时 {[r['id'] for r in errored]}  漏前缀 {[r['id'] for r in leaks]}  "
              f"空文本 {[r['id'] for r in empty]}")
        print(f"auto 基线 matched={auto['matched']}")
        return len(rejected) + len(leaks) + len(errored) + (0 if auto["matched"] else 1)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


if __name__ == "__main__":
    sys.exit(main())
