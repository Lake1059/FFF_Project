# tap PCM 解码回归测试：asr_worker.decode_pcm_frame 对全部 10 种 AVSampleFormat
# 的处理必须与 C++ 侧 UpdateInputAudioPeakLevels 的定标判据一致。
#
# 为什么有这份测试：tap 有意送"未经任何转换的解码床"（AudioTap.h §1），而旧实现
# 只认 FLTP、其余一律按打包 float32 重解释 —— FLAC(s32p)/PCM WAV(s16)/ALAC 这类
# 源会整段产出乱码识别文本，且无任何日志提示（P1，2026-11-03 审查定案）。
#
# 运行：python test_frame_decode.py（纯逻辑：不碰 GPU、不起进程、不读文件），
# 需要的解释器装了 numpy（扩展包 venv 即可）。
import importlib.util
import sys
from pathlib import Path

import numpy as np

spec = importlib.util.spec_from_file_location(
    "asr_worker", Path(__file__).resolve().parent / "asr_worker.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)  # 顶层只有 import 与常量表，main() 有 __main__ 护栏

ok = fail = 0


def check(name, cond, detail=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"PASS {name}")
    else:
        fail += 1
        print(f"FAIL {name} :: {detail}")


FRAMES, CHANNELS = 3, 2
# 每声道参考值（float 域）：[0.0, +0.5, -1.0]——覆盖零、正、负满幅三档。
REF = np.array([[0.0, 0.5, -1.0],
                [0.0, 0.5, -1.0]], dtype=np.float32).T  # -> (3, 2)


def planar_bytes(dtype, columns):
    """列表 -> 平面布局字节串：[ch0 整列][ch1 整列]…（与内核 Produce 一致）。"""
    arr = np.asarray(columns, dtype=dtype)
    return b"".join(arr[:, c].tobytes() for c in range(arr.shape[1]))


def packed_bytes(dtype, columns):
    arr = np.asarray(columns, dtype=dtype)
    return arr.tobytes()  # (帧, 声道) C 序 = 交错


def decoded_ok(name, fmt, pcm):
    arr = mod.decode_pcm_frame(pcm, fmt, CHANNELS)
    check(name, arr is not None and arr.shape == (FRAMES, CHANNELS)
          and arr.dtype == np.float32 and np.allclose(arr, REF, atol=1e-6),
          f"got {None if arr is None else (arr.shape, arr.dtype, arr[:2].tolist())}")


# ---- float 家族：数值原样 ----
decoded_ok("FLTP 平面（AAC/MP3/Opus 解码输出）", 8,
           planar_bytes(np.dtype("<f4"), REF))
decoded_ok("FLT 打包", 3, packed_bytes(np.dtype("<f4"), REF))
decoded_ok("DBL 打包（降精度到 f32）", 4, packed_bytes(np.dtype("<f8"), REF))
decoded_ok("DBLP 平面（降精度到 f32）", 9, planar_bytes(np.dtype("<f8"), REF))

# ---- 有符号整型：定标到 [-1, 1)（输入按 (帧, 声道) 构造，与 REF 同形）----
decoded_ok("S16 打包（PCM WAV 常见）", 1,
           packed_bytes(np.dtype("<i2"), np.round(REF * 32768.0).astype("<i2")))
decoded_ok("S16P 平面（FLAC s16 变体）", 6,
           planar_bytes(np.dtype("<i2"), np.round(REF * 32768.0).astype("<i2")))
decoded_ok("S32 打包", 2,
           packed_bytes(np.dtype("<i4"), np.clip(np.round(REF * 2147483648.0),
                                                 -2147483648, 2147483647).astype("<i4")))
decoded_ok("S32P 平面（FLAC/ALAC 解码输出）", 7,
           planar_bytes(np.dtype("<i4"), np.clip(np.round(REF * 2147483648.0),
                                                 -2147483648, 2147483647).astype("<i4")))

# ---- u8：无符号、静音在中点 128 ----
u8_pcm = np.clip(np.round(REF * 128.0 + 128.0), 0, 255).astype(np.uint8)
arr = mod.decode_pcm_frame(packed_bytes(np.uint8, u8_pcm), 0, CHANNELS)
check("U8 打包（中点定标）", arr is not None and np.allclose(arr, REF, atol=1e-6),
      f"got {None if arr is None else arr[:2].tolist()}")
arr = mod.decode_pcm_frame(planar_bytes(np.uint8, u8_pcm), 5, CHANNELS)
check("U8P 平面（中点定标）", arr is not None and np.allclose(arr, REF, atol=1e-6),
      f"got {None if arr is None else arr[:2].tolist()}")

# ---- 必须拒绝的输入：一律 None（调用方计 dropped，不产出乱码窗）----
check("未知格式拒绝", mod.decode_pcm_frame(b"\x00" * 24, 99, CHANNELS) is None)
check("负格式拒绝", mod.decode_pcm_frame(b"\x00" * 24, -1, CHANNELS) is None)
check("长度不齐拒绝", mod.decode_pcm_frame(b"\x00" * 7, 8, CHANNELS) is None)
check("空 PCM 拒绝", mod.decode_pcm_frame(b"", 8, CHANNELS) is None)
check("零声道拒绝", mod.decode_pcm_frame(b"\x00" * 24, 8, 0) is None)

# ---- 帧长与声道数一致性：helper 输出的帧数必须等于输入帧数 ----
arr = mod.decode_pcm_frame(packed_bytes(np.dtype("<f4"), REF), 3, CHANNELS)
check("帧数守恒", arr is not None and arr.shape[0] == FRAMES)

print(f"\n通过 {ok} / 失败 {fail}")
sys.exit(1 if fail else 0)
