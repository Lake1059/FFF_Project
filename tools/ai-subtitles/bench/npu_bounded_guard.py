"""用 NPU 包自带 python + openvino，在真实 conv_frontend.onnx 上验证上界门禁。

本机没有 NPU 硬件，但 read_model / reshape / 读 Dimension 上下界都**不需要硬件**，
所以「无上界会被挡住、加了上界能通过」这条判据可以真验证，不必上真机。
"""
import os
import sys

REPO = r"C:\PLAN\3FCompare\third_party\fff_project\tools\ai-subtitles"
MODEL = r"C:\PLAN\3FCompare\dist\AI字幕包-NPU\model\conv_frontend.onnx"
sys.path.insert(0, REPO)

import qwen3_ort as qv  # noqa: E402

print("qwen3_ort:", qv.__file__)
ov, core = qv.openvino_core()
m = core.read_model(MODEL)
ps = m.input(0).partial_shape
print("原始输入 shape:", ps)
for j in range(len(ps)):
    d = ps[j]
    if d.is_static:
        print(f"  dim{j} 静态 {d.get_length()}")
        continue
    try:
        lo, hi = d.get_min_length(), d.get_max_length()
    except Exception as exc:
        lo = hi = f"取不到（{type(exc).__name__}）"
    print(f"  dim{j} 动态 [{lo}, {hi}]")

print("\n[1] 无上界时是否挡住：")
try:
    qv._assert_bounded(m, "conv_frontend.onnx")
    print("  ✗ 没挡住 —— 判据失效，会用 abort 的方式崩")
    sys.exit(1)
except qv.BackendError as e:
    print("  ✓ 挡住了：", str(e).split("——")[0])

print("\n[2] reshape 加界后：")
qv._reshape_bounded(m, ov, 3200)
ps2 = m.input(0).partial_shape
print("  shape:", ps2)
for j in range(len(ps2)):
    d = ps2[j]
    print(f"  dim{j}", "静态" if d.is_static else f"[{d.get_min_length()}, {d.get_max_length()}]")
try:
    qv._assert_bounded(m, "conv_frontend.onnx")
    print("  ✓ 通过（一次编译覆盖 1..3200 帧，不需要为每种长度各编一份）")
except qv.BackendError as e:
    print("  ✗ 反而被挡：", e)
    sys.exit(1)

print("\n[3] rank 不为 3 时（防御未来模型改版）：")
try:
    qv._reshape_bounded(m, ov, 3200, index=99)
except qv.BackendError as e:
    print("  ✗ 取了不存在的输入却没报错")
    sys.exit(1)
except Exception as e:
    print("  ✓ 由 OpenVINO 抛出（", type(e).__name__, "）——不是静默跳过")
print("\nOK")
