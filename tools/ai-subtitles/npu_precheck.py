"""NPU 预检：一眼看清这台机器有没有 NPU、包里的编译器能不能吃下我们的模型。

为什么需要它
------------
NPU 这条路径和 CPU/GPU 不一样：模型能编译不等于这台机器能跑，反过来也一样。
三个问题必须分开回答，混在一起就只能靠猜：

  1. 这台机器有 NPU 吗？          -> available_devices / FULL_DEVICE_NAME
  2. 包里的 NPU 编译器完整吗？    -> 没有硬件时用 NPU_PLATFORM 离线编译一遍
  3. 我们的 conv_frontend 编得过吗？ -> 静态 / 带上界的动态 shape 各试一次

第 2 步是关键：OpenVINO 的 NPU 编译器（openvino_intel_npu_compiler.dll，96 MB）
可以脱离硬件做离线编译，所以**没有 NPU 的机器也能验证包里的编译器是不是完整的**。
到真有 NPU 的机器上，第 3 步换成在线编译，多的只是执行。

用法
----
    python npu_precheck.py --model-dir model
    python npu_precheck.py --model-dir model --platform NPU3720
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# conv_frontend 的帧维上界。宿主窗口上限 30 s（3000 帧 @10 ms），留两成余量。
BOUNDED_FRAMES = 3200
# OpenVINO 2025+ 的 NPU 平台代号；离线编译要显式指定。
PLATFORMS = ("NPU4000", "NPU3720")


def _line(tag, text):
    print(f"  {tag:<10}{text}")


def device_table(core):
    print("== OpenVINO 设备 ==")
    try:
        import openvino as ov
        print(f"  版本      {ov.__version__}")
    except Exception:  # noqa: BLE001
        pass
    for d in core.available_devices:
        try:
            name = core.get_property(d, "FULL_DEVICE_NAME")
        except Exception:  # noqa: BLE001
            name = "（读不到名称）"
        print(f"  {d:<10}{name}")
    return list(core.available_devices)


def try_compile(core, model_path, target, bounded, cfg_extra=None):
    """返回 (ok, 耗时, 说明)。bounded=True 时给帧维加上界。"""
    import openvino as ov
    cfg = {"PERFORMANCE_HINT": "LATENCY"}
    if cfg_extra:
        cfg.update(cfg_extra)
    try:
        model = core.read_model(str(model_path))
    except Exception as exc:  # noqa: BLE001
        return False, 0.0, f"读模型失败：{exc}"
    shape_note = "原样（无上界）"
    if bounded:
        ps = model.input(0).partial_shape
        if ps.rank.is_static and len(ps) == 3:
            n_mels = ps[2].get_length() if ps[2].is_static else 128
            model.reshape({0: ov.PartialShape(
                [(1, 1), (1, BOUNDED_FRAMES), (n_mels, n_mels)])})
            shape_note = f"帧维上界 {BOUNDED_FRAMES}"
    t0 = time.perf_counter()
    try:
        cm = core.compile_model(model, target, cfg)
    except Exception as exc:  # noqa: BLE001
        msg = str(exc)
        hint = ""
        for key in ("Unsupported platform", "Upper bounds are not specified",
                    "Unsupported operation", "No available devices"):
            if key in msg:
                hint = key
                break
        if not hint:
            lines = [l.strip() for l in msg.splitlines() if l.strip()]
            hint = lines[-1][:90] if lines else type(exc).__name__
        return False, time.perf_counter() - t0, f"{shape_note} → 失败（{hint}）"
    dt = time.perf_counter() - t0
    try:
        out = str(cm.outputs[0].get_partial_shape())
    except Exception:  # noqa: BLE001
        out = "?"
    return True, dt, f"{shape_note} → 成功 {dt:.1f} s  输出 {out}"


def main(argv=None):
    ap = argparse.ArgumentParser(description="NPU 可用性预检")
    ap.add_argument("--model-dir", default="model", help="含 conv_frontend.onnx 的目录")
    ap.add_argument("--platform", default="NPU4000",
                    help=f"离线编译用的平台代号（{', '.join(PLATFORMS)}）")
    args = ap.parse_args(argv)

    model_dir = Path(args.model_dir).expanduser().resolve()
    conv = model_dir / "conv_frontend.onnx"
    print(f"模型目录  {model_dir}")
    if not conv.is_file():
        print(f"  ✗ 缺 {conv.name} —— NPU 只跑这一段（int8 两段留 CPU），缺它无从谈起")
        return 2

    try:
        import openvino as ov
        core = ov.Core()
        # 不要在这里设 LOG_LEVEL：OpenVINO 2026.4.1 的 core.set_property({"LOG_LEVEL":
        # "NONE"}) 会让进程直接段错误（实测 exit 139）。要少打日志就别做注定失败的
        # 试探 —— 无上界那次编译会刷一串 IE::FrontEnd ERROR，本脚本已去掉。
    except Exception as exc:  # noqa: BLE001
        print(f"  ✗ OpenVINO 不可用：{exc}")
        return 2
    devices = device_table(core)

    npu_devs = [d for d in devices if d.upper().startswith("NPU")]
    print()
    print("== NPU ==")
    if npu_devs:
        for d in npu_devs:
            try:
                print(f"  发现      {d} {core.get_property(d, 'FULL_DEVICE_NAME')}")
            except Exception:  # noqa: BLE001
                print(f"  发现      {d}")
        print("  在线编译 conv_frontend …")
        ok, dt, note = try_compile(core, conv, npu_devs[0], bounded=True)
        _line("结果", note)
        if not ok:
            print("  结论：有 NPU，但这段模型编不过 —— 看上面的失败原因，"
                  "多半是算子集，不是硬件问题。")
            return 1
        print(f"  结论：NPU 可用，conv_frontend 编译通过（{dt:.1f} s）。"
              f"首次启动会等这么久，之后走 runtime/npu-cache。")
        return 0

    print("  未发现    本机没有 NPU 设备（正常的：只有 Core Ultra 系列才有）")
    print(f"  离线编译  用 {args.platform} 验证包里的编译器是否完整 …")
    ok, dt, note = try_compile(core, conv, "NPU", bounded=True,
                               cfg_extra={"NPU_PLATFORM": args.platform})
    _line("结果", note)
    print()
    if ok:
        print("  结论：包里的 NPU 编译链完整，conv_frontend 能编到 NPU 架构。")
        print("        插到有 NPU 的机器上把设备选成 NPU 即可；这里只是没硬件可执行。")
        return 0
    print("  结论：离线编译失败 —— 包里的 NPU 编译器不完整或与 OpenVINO 版本不匹配。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
