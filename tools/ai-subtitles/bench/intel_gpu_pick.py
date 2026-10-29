"""_pick_intel_gpu 的逻辑验证（stub core，不需要 OpenVINO）。

要挡的假绿：OpenVINO 把 N 卡 / A 卡也列进 GPU.x，而 "GPU" 取第一块。
第三方实测那台机器就是 GPU.0=Intel Arc / GPU.1=RTX 5060，顺序反了就会编到 N 卡上。
"""
import sys

REPO = r"C:\PLAN\3FCompare\third_party\fff_project\tools\ai-subtitles"
sys.path.insert(0, REPO)
import asr_worker as aw  # noqa: E402


class C:
    def __init__(self, names):
        self.names = names

    def get_property(self, dev, key):
        if dev not in self.names:
            raise RuntimeError("no such property")
        return self.names[dev]


CASES = [
    ("Intel 排第一", {"GPU.0": "Intel(R) Arc(TM) 140T GPU (16GB)",
                      "GPU.1": "NVIDIA GeForce RTX 5060 Laptop GPU"},
     ["GPU.0", "GPU.1"], "GPU.0"),
    ("Intel 排第二（第三方那台机器的反序情形）",
     {"GPU.0": "NVIDIA GeForce RTX 5060 Laptop GPU",
      "GPU.1": "Intel(R) Arc(TM) 140T GPU (16GB)"},
     ["GPU.0", "GPU.1"], "GPU.1"),
    ("只有 N 卡", {"GPU.0": "NVIDIA GeForce RTX 4060 Laptop GPU"},
     ["GPU.0"], "RAISE"),
    ("只有 A 卡", {"GPU.0": "AMD Radeon(TM) Graphics"}, ["GPU.0"], "RAISE"),
    ("名字读不到（不敢判，退回默认）", {}, ["GPU.0"], None),
    ("没有 GPU 设备", {}, ["CPU", "NPU"], None),
]

bad = 0
for name, names, devs, want in CASES:
    try:
        got = aw._pick_intel_gpu(C(names), devs)
    except RuntimeError as e:
        got = "RAISE"
        detail = str(e).split("；")[0]
    else:
        detail = ""
    ok = (got == want)
    bad += 0 if ok else 1
    print(f"  [{'OK ' if ok else 'FAIL'}] {name}: 期望 {want} 实得 {got}  {detail}")

print("\nFAIL" if bad else "\n全通过")
sys.exit(1 if bad else 0)
