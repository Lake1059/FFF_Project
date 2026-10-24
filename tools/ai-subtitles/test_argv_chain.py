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
DEFAULTS = ["--device", "Vulkan0", "--ctx-size", "4096", "--n-gpu-layers", "999",
            "--threads", "8", "--temp", "0.6", "--no-cache-idle-slots",
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
                             "--no-cache-idle-slots", "--cache-ram", "--device"]), cmd)
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

# S7 老 manifest（无 llamaArgs）与改动前逐 token 等价（向后兼容）
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

print(f"\n{ok} passed / {fail} failed")
sys.exit(1 if fail else 0)
