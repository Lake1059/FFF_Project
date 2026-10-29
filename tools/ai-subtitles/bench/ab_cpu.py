"""包的形态取决于一件事：GGUF 路线在 CPU 上能不能顶替现有 ONNX/sherpa 路线。

本脚本用**同一段音频、同一 6 线程**测四条 CPU 臂：
  sherpa   —— 生产现状（asr_worker.py 的 SherpaBackend，直接复用其代码）
  ort-pad30 —— qwen3_ort，pad30=True（按 30 s 补齐，字形繁体、token 数膨胀）
  ort-nopad —— qwen3_ort，pad30=False（不补，字形简体、丢字）
  llama     —— llama.cpp GGUF Q8_0，-ngl 0 --device CPU，temp 0（见 ab_llama.py）

用法: python ab_cpu.py --engine sherpa|ort --pad30 0|1 --out result.json
"""
import argparse
import json
import os
import statistics
import sys
import time
import wave

import numpy as np

MODEL_DIR = r"C:\PLAN\3FCompare\_3fc_tmp\ai-pack\model"
AUDIO = r"C:\PLAN\3FCompare\_3fc_tmp\asr_probe"
WAVS = ["A-perf-en-int-w5.wav", "A-perf-zh-wiki-w5.wav"]
THREADS = 6
SYS_ASR = r"C:\PLAN\3FCompare\third_party\fff_project\tools\ai-subtitles"
ORT_DIR = r"C:\PLAN\3FCompare\_3fc_tmp\ort-cuda"
ORT_SRC = r"C:\PLAN\3FCompare\_3fc_tmp\int8-recheck"


def read_wav(path):
    with wave.open(path) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        sr = w.getframerate()
        raw = w.readframes(w.getnframes())
    x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    assert sr == 16000, sr
    return x


def strip_template(t):
    """llama 输出形如 'language English<asr_text>...'，取模板之后的部分。"""
    if "<asr_text>" in t:
        return t.split("<asr_text>", 1)[1].strip()
    return t.strip()


def build_sherpa():
    sys.path.insert(0, SYS_ASR)
    import asr_worker as W
    be = W.SherpaBackend("cpu", "cpu", "CPU", MODEL_DIR, THREADS)
    be.load()
    return be


def build_ort():
    sys.path.insert(0, ORT_DIR)
    sys.path.insert(0, ORT_SRC)
    import qwen3_ort as Q
    return Q.Engine(MODEL_DIR, device="cpu", threads=THREADS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", choices=["sherpa", "ort"], required=True)
    ap.add_argument("--pad30", type=int, default=1)
    ap.add_argument("--wavs", default=",".join(WAVS))
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    wavs = [w.strip() for w in args.wavs.split(",") if w.strip()]

    t0 = time.time()
    if args.engine == "sherpa":
        be = build_sherpa()
        infer = lambda a: be.transcribe(a)
    else:
        eng = build_ort()
        infer = lambda a: eng.transcribe(a, pad30=bool(args.pad30))
    build_s = round(time.time() - t0, 2)

    res = {"engine": args.engine, "pad30": bool(args.pad30), "threads": THREADS,
           "build_s": build_s, "rows": []}

    for name in wavs:
        audio = read_wav(os.path.join(AUDIO, name))
        dur = len(audio) / 16000.0
        for _ in range(2):                      # 预热：冷态睿频会把首测拉偏
            infer(audio)
        ts, txt = [], ""
        for _ in range(args.repeat):
            t = time.time()
            r = infer(audio)
            ts.append(round(time.time() - t, 3))
            txt = r[1] if isinstance(r, tuple) else r.get("text", "")
        med = statistics.median(ts)
        res["rows"].append({"wav": name, "dur_s": round(dur, 2), "runs_s": ts,
                            "median_s": med, "min_s": min(ts),
                            "rtf": round(med / dur, 3), "text": strip_template(txt)[:120]})
        print("%-24s med %.3fs min %.3fs rtf %.3f | %s"
              % (name, med, min(ts), med / dur, strip_template(txt)[:60]), flush=True)

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
