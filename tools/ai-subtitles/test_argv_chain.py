# argv 全链路回归测试：manifest llamaArgs -> VB 等号形式 argv -> argparse -> _user_first -> llama-server cmd
#
# 为什么有这份测试：M1（--llama-arg 拆两个 token 传被 argparse 判
# "expected one argument"，worker 启动即 exit 2，而文档标准示例
# ["--ctx-size","8192"] 每个值都以 -- 开头、必然触发）当初漏测，因为单测
# 直接构造 LlamaServer(extra=[...]) 绕过了 argv 解析层。本测试从 argv 层
# 进入，复刻宿主 Ai字幕管道.vb 的等号形态（"--llama-arg=" & token），最终
# 落到 LlamaServer._build_cmd() 与 build_arg_parser() 的真实输出——不启
# 进程、不碰 GPU、不写文件，纯逻辑随时可跑。
#
# 运行：用装了 numpy/requests/soundfile 的解释器（扩展包 venv 即可）：
#   <扩展包>/venv/Scripts/python.exe test_argv_chain.py
import importlib.util
import sys
from pathlib import Path

WORKER = Path(__file__).resolve().parent / "asr_worker.py"
spec = importlib.util.spec_from_file_location("asr_worker", WORKER)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)  # 顶层只有 import 和 requests.Session()，main() 有 __main__ 护栏

ok = fail = 0


def check(name, cond, detail=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"PASS {name}")
    else:
        fail += 1
        print(f"FAIL {name} :: {detail}")


def vb_argv(tokens):
    """复刻宿主 Ai字幕管道.vb 的等号形态：每个 token 一个 argv 元素
    "--llama-arg=<token>"（拆分形态 "--llama-arg" "<token>" 会被 argparse
    拒，见 S2）。"""
    out = ["--model-dir", "X"]
    for t in tokens:
        out.append("--llama-arg=" + t)
    return out


def chain(tokens, mmproj=None):
    """manifest llamaArgs -> 等号 argv -> 真解析器 -> 真 LlamaServer -> 真命令行。"""
    args = mod.build_arg_parser().parse_args(vb_argv(tokens))
    srv = mod.LlamaServer("llama-server.exe", "M.gguf", mmproj, 8199,
                          device="Vulkan0", ngl=999, threads=8,
                          extra=args.llama_arg)
    return args.llama_arg, srv._build_cmd()


FIXED = ["llama-server.exe", "-m", "M.gguf", "--port", "8199", "--host", "127.0.0.1"]
# 2026-10-25 两处变更，都写在 S7/S13 的断言里：
#   1. 新增输出护栏 --n-predict 512（无语音段会让模型重复到撞满 ctx）。
#   2. 移除 --no-cache-idle-slots：实测它是性能元凶（rtf 恶化 1~2 个数量级）。
DEFAULTS = ["--device", "Vulkan0", "--ctx-size", "4096", "--n-gpu-layers", "999",
            "--threads", "8", "--temp", "0.6", "--n-predict", "512",
            "--cache-ram", "0"]

# S1 文档标准示例（PACK_FORMAT §2.5）：["--ctx-size","8192","--temp","0.4"]
extra, cmd = chain(["--ctx-size", "8192", "--temp", "0.4"])
check("S1 equals-form collects values",
      extra == ["--ctx-size", "8192", "--temp", "0.4"], extra)
check("S1 ctx-size overridden to 8192",
      cmd.count("--ctx-size") == 1 and cmd[cmd.index("--ctx-size") + 1] == "8192", cmd)
check("S1 temp overridden to 0.4",
      cmd.count("--temp") == 1 and cmd[cmd.index("--temp") + 1] == "0.4", cmd)
check("S1 worker defaults kept",
      all(f in cmd for f in ["--n-gpu-layers", "--threads",
                             "--cache-ram", "--device"]), cmd)
# S1b 防回归：--no-cache-idle-slots 不能再回来。它看着是省内存的开关，实测
# （2026-10-25，CUDA0）却让长输出请求的 rtf 从 0.08 恶化到 1.5~6.7（极端 34）。
# 字幕实时性优先：宁可多占一点内存，也不能让字幕追不上画面。
check("S1b no-cache-idle-slots absent", "--no-cache-idle-slots" not in cmd, cmd)
check("S1 fixed segment intact", cmd[:7] == FIXED, cmd[:7])

# S2 拆分形态必须失败（防回归：M1 缺陷形态原样复现）
try:
    mod.build_arg_parser().parse_args(
        ["--model-dir", "X", "--llama-arg", "--ctx-size", "--llama-arg", "8192"])
    check("S2 split form rejected", False, "unexpectedly passed")
except SystemExit:
    check("S2 split form rejected", True)

# S3 等号值形态 --ctx-size=2048（既非默认也非让位，原样进命令行）
extra, cmd = chain(["--ctx-size=2048"])
check("S3 equals-value single occurrence",
      cmd.count("--ctx-size") == 0 and "--ctx-size=2048" in cmd, cmd)

# S4 短选项不让位（文档警告场景：-c 与默认 --ctx-size 并存，重复传参）
extra, cmd = chain(["-c", "2048"])
check("S4 short option keeps default",
      "--ctx-size" in cmd and cmd[cmd.index("--ctx-size") + 1] == "4096" and "-c" in cmd, cmd)

# S5 固定段 flag（--port）不在让位范围 -> 重复传参（专家自伤场景，失败不静默）
extra, cmd = chain(["--port", "1234"])
check("S5 protected flag duplicated", cmd.count("--port") == 2, cmd)

# S6 奇数 token（悬空 flag）：原样落在命令行末尾
extra, cmd = chain(["--ctx-size"])
tail = cmd[cmd.index("--ctx-size"):]
check("S6 dangling flag at tail", tail == ["--ctx-size"], tail)

# S7 老 manifest（无 llamaArgs）命令行 = 固定段 + 默认段（别无其他变化）。
# 默认段自 2026-10-25 起含输出护栏 --n-predict 512（见 DEFAULTS 注释）。
extra, cmd = chain([])
check("S7 legacy manifest identical", cmd == FIXED + DEFAULTS, cmd)

# S8 含空格路径值 token 原样通过（llamaArgs 是 flag/值不是路径，写绝对路径）
extra, cmd = chain(["--grammar", "C:\\models\\my grammars\\g.gbnf"])
check("S8 spaced path token intact",
      cmd[-2:] == ["--grammar", "C:\\models\\my grammars\\g.gbnf"], cmd[-2:])

# S9 --device 可让位（在 defaults 里，用户点名以用户为准）
extra, cmd = chain(["--device", "none"])
check("S9 device override",
      cmd.count("--device") == 1 and cmd[cmd.index("--device") + 1] == "none", cmd)

# S10 双文件形态：mmproj 段在固定位
extra, cmd = chain(["--ctx-size", "8192"], mmproj="mmproj.gguf")
check("S10 mmproj segment present",
      cmd[:6] == ["llama-server.exe", "-m", "M.gguf", "--mmproj",
                  "mmproj.gguf", "--port"], cmd[:7])

# S11 无值开关（--no-cache-idle-slots）同名让位：它后面跟的是下一个 flag
# （--cache-ram），_user_first 只删这 1 个 token；用户的开关落在命令行末尾
extra, cmd = chain(["--no-cache-idle-slots"])
check("S11 valueless flag override",
      cmd.count("--no-cache-idle-slots") == 1
      and cmd[-1] == "--no-cache-idle-slots"
      and "--cache-ram" in cmd and cmd[cmd.index("--cache-ram") + 1] == "0", cmd)

# S12 _probe_model_id 四分支（多模型核心机制：服务端自报身份 + 三档失败回落）
class _Resp:
    def __init__(self, status, payload):
        self.status_code = status
        self._payload = payload

    def json(self):
        return self._payload


class _Probe:
    """替身 requests.Session：按 script 依次返回预定响应或抛异常。"""

    def __init__(self, script):
        self.script = list(script)
        self.calls = []

    def get(self, url, timeout=0):
        self.calls.append(url)
        kind = self.script.pop(0)
        if kind == "raise":
            raise OSError("connection refused")
        status, payload = kind
        return _Resp(status, payload)


_orig_get = mod._HTTP.get
try:
    srv = mod.LlamaServer("llama-server.exe", "M.gguf", None, 8199)
    probe = _Probe([(200, {"data": [{"id": "C:/models/qwen3-asr.gguf"}]})])
    mod._HTTP.get = probe.get
    mid = srv._probe_model_id()
    check("S12 probe returns server-reported id",
          mid == "C:/models/qwen3-asr.gguf", mid)
    check("S12 probe hits /v1/models",
          bool(probe.calls) and probe.calls[0].endswith("/v1/models"), probe.calls)

    for name, kind in [("S12a empty list -> fallback", (200, {"data": []})),
                       ("S12b non-200 -> fallback", (404, {})),
                       ("S12c exception -> fallback", "raise")]:
        mod._HTTP.get = _Probe([kind]).get
        check(name, srv._probe_model_id() == "qwen3-asr")
    mod._HTTP.get = _Probe([(404, {})]).get
    check("S12d custom fallback honored", srv._probe_model_id(fallback="x") == "x")
finally:
    mod._HTTP.get = _orig_get

# S13 输出护栏可被用户覆盖（--n-predict 走 _user_first，与 --ctx-size 同语义）
extra, cmd = chain(["--n-predict", "2048"])
check("S13a n-predict override single",
      cmd.count("--n-predict") == 1 and cmd[cmd.index("--n-predict") + 1] == "2048", cmd)
extra, cmd = chain([])
check("S13b guard present by default",
      cmd.count("--n-predict") == 1 and cmd[cmd.index("--n-predict") + 1] == "512", cmd)

# S14 失控输出判定（纯逻辑，2026-10-25 实测缺陷的护栏）
cases = [
    ("S14a runaway single char", "啊" * 3808, True),
    ("S14b runaway with spaces", "啊 " * 100, True),
    ("S14c normal chinese line", "今天天气很好我们出去散步吧顺便买点水果回来", False),
    ("S14d short interjection", "嗯。", False),
    ("S14e empty", "", False),
    ("S14f repeated but short", "哈哈哈", False),
]
for name, text, want in cases:
    check(name, mod.looks_degenerate(text) is want,
          f"got {mod.looks_degenerate(text)} want {want}")

# S15 指令泄漏判定（2026-10-25 实测缺陷的护栏：模型把 instruction 当内容输出）
leak_cases = [
    ("S15a bare template", "Transcribe audio to text.", True),
    ("S15b template with the", "Transcribe the audio to text", True),
    ("S15c template + stray spaces", "  transcribe   AUDIO to TEXT  ", True),
    ("S15d chinese-ish template", "Transcribe audio to text", True),
    ("S15e chat marker asr_text", "language Chinese<asr_text> Transcribe audio to text", True),
    ("S15f chat marker im_start", "<|im_start|>assistant", True),
    ("S15g empty is not leak", "", False),
    ("S15h normal english line", "Good morning everyone and welcome back to the show", False),
    ("S15i normal chinese line", "今天我们先看一下这个方案的总体设计", False),
    # 长段对白里提到 transcribe 不该被杀（只判"短得像一句指令"的文本）
    ("S15j long tutorial mentioning transcribe",
     "今天我们要讲的是如何 transcribe audio to text，也就是把一段音频里的语音"
     "转成文字，整个流程分成三步，第一步是采样率归一化，第二步是特征提取，"
     "第三步才是解码器输出，每一步都有各自的代价和注意事项需要权衡。", False),
]
for name, text, want in leak_cases:
    check(name, mod.looks_like_prompt_leak(text) is want,
          f"got {mod.looks_like_prompt_leak(text)} want {want}")

print(f"\n{ok} passed / {fail} failed")
sys.exit(1 if fail else 0)
