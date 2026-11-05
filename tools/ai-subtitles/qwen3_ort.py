"""Qwen3-ASR 多后端引擎：同一份模型（三个 onnx 原样，不做任何转换）跑在
CPU / NVIDIA GPU / AMD GPU / Intel GPU（XMX/Level Zero）/ Intel NPU 上。

为什么要自己管 session
----------------------
sherpa 的 OfflineRecognizer 一个实例只吃一个 provider（单一字符串），做不到
「conv/encoder 上 GPU、decoder 留 CPU」；它自带的还是纯 CPU 版 onnxruntime.dll
（1.28.2，17 MB，只编进了 CPUExecutionProvider），传 cuda/directml/openvino
只会打一行日志回落 CPU。所以这里直接用运行时，自己管三段 session。

三个 onnx 本来就是独立文件，天然可以各挂各的设备，**权重一个字节都不改**。

两条运行时
----------
1. **onnxruntime**（cpu / cuda / dml）
   三个变体 onnxruntime / onnxruntime-gpu / onnxruntime-directml 提供**同名模块**，
   一个进程只能装一个。EP 通过 `session.get_providers()` 校验，绝不静默回落。

2. **OpenVINO 原生**（ov-cpu / ov-gpu / ov-npu）
   via `openvino` pip 包。OpenVINO 能直接 read_model 读 ONNX，所以既不需要
   onnxruntime，也不需要那个版本对不上的 onnxruntime-openvino provider
   （那个 provider 静态依赖 openvino.dll，且 ORT 1.24.1 配 2025.x/2026.x 都报
   Error 127 符号缺失）。原生路径也是 Intel 的厂商加速路径：
   oneDNN（CPU）/ Level Zero + XMX（GPU）/ NPU 编译插件。
   **Intel NPU 只有这条能到。**

设备名
------
    cpu       CPUExecutionProvider        onnxruntime            任何机器
    cuda      CUDAExecutionProvider       onnxruntime-gpu        NVIDIA（需 CUDA 12 库 + cuDNN 9）
    dml       DmlExecutionProvider        onnxruntime-directml   NV/AMD/Intel 任一 DX12 显卡
    ov-cpu    OpenVINO CPU                openvino               Intel CPU，oneDNN
    ov-gpu    OpenVINO GPU                openvino               Intel iGPU / Arc（XMX）
    ov-npu    OpenVINO NPU                openvino               Intel Core Ultra NPU
    auto      按 cuda -> dml -> ov-gpu -> cpu 探测

**三段可以分别指定设备**：`--conv-device` / `--encoder-device` / `--decoder-device`，
没给就用 `--device`。这不是为了好玩——实测（本机 i7-13650HX + RTX 5080 + Intel iGPU）：

  * `conv_frontend` 是纯 fp32，在任何设备上都算得对，且 GPU 上确实更快
    （Intel iGPU 43 ms vs CPU 129 ms；CUDA 6.5 ms vs CPU 56.8 ms）。
  * `encoder.int8` / `decoder.int8` 是 QOperator 动态量化
    （`DynamicQuantizeLinear` + `MatMulInteger`），GPU 上要么被切碎变慢
    （CUDA 480 个 Memcpy 节点，encoder 698 ms vs CPU 59 ms），要么**直接算错**
    （OpenVINO GPU 出 NaN，不抛异常）。
  * 自回归 decoder 每 token 只有十几层小 GEMM，GPU 启动/拷贝开销必然压过计算。

所以实用配置是「fp32 那段上加速器，int8 两段留 CPU」。`ov-*` 还可以用
`--ov-device GPU.0` 钉到具体设备（多显卡时默认的 "GPU" 未必是你想要的）。

注意：onnxruntime 与 openvino 是两个不同模块，**可以同时装、同进程共存**，
所以「Intel GPU 跑 conv + CPU 跑 encoder」这种混搭成立；但 gpu/directml
两个 onnxruntime 变体互斥。

三道假绿防护
------------
1. **EP 绑定**（ORT）：只传目标 EP，没注册直接抛；注册了但初始化失败会回落，
   此时 `get_providers()` 首个不是目标 EP，立刻报错。
2. **设备回读**（OpenVINO）：编译后读 `FULL_DEVICE_NAME` 打印真实设备名，
   目标设备不存在时 `compile_model` 直接抛。
3. **数值门禁**（`--verify`）：逐段与同运行时族的 CPU 参考值比对。
   **这是唯一能挡「不报错但算错」的手段** —— OpenVINO GPU 跑 encoder.int8
   出 NaN 就是这样，CER 自检能抓到但要跑完整段音频，这个探针一次前向就够，
   而且能定位到哪一段坏了。

上线前每个后端组合都必须过 `--verify` 和 `--selftest`（金样本 CER ≤ 0.30）。

CLI
---
    python qwen3_ort.py --list-devices
    python qwen3_ort.py --model-dir <dir> --verify
    python qwen3_ort.py --model-dir <dir> --selftest
    python qwen3_ort.py --model-dir <dir> --wav a.wav --device cpu
    python qwen3_ort.py --model-dir <dir> --wav a.wav --device cuda
    python qwen3_ort.py --model-dir <dir> --wav a.wav --device ov-npu --npu-frames 6000

`--device` 一个值就够：加速后端（cuda / dml / ov-gpu / ov-npu）缺省只把 fp32 的
conv 送上去，int8 两段留 CPU（STAGE_DEFAULTS，依据见 Engine 文档注释）。
需要越过分默认时用 --conv-device / --encoder-device / --decoder-device 三段各给一个。

`--model-dir` 是含 conv_frontend.onnx / encoder.int8.onnx / decoder.int8.onnx
和 tokenizer/ 的目录。
"""

from __future__ import annotations

import json
import os
import statistics
import sys
import time
import wave

import numpy as np

# ---------------------------------------------------------------- 常量

SR = 16000
N_FFT = 400
HOP = 160
N_MELS = 128
CHUNK = 480000          # 30 s

IM_START, IM_END = 151644, 151645
AUDIO_START, AUDIO_END, AUDIO_PAD = 151669, 151670, 151676
NLAYER, NHEAD, HDIM = 28, 8, 128
EOS = (IM_END, 151643)

CONV_FILE = "conv_frontend.onnx"
ENC_FILE = "encoder.int8.onnx"
DEC_FILE = "decoder.int8.onnx"

# onnxruntime_providers_cuda.dll 的非系统依赖（PE 导入表实测）。缺任何一个，
# EP 会静默回落 CPU —— 所以这里既用于报错提示，也用于 preflight。
CUDA_REQUIRED_DLLS = (
    "cublas64_12.dll",
    "cublasLt64_12.dll",
    "cudart64_12.dll",
    "cudnn64_9.dll",
    "cufft64_11.dll",
)


class BackendError(RuntimeError):
    """后端不可用，或请求的后端没有真正生效。"""


# ---------------------------------------------------------------- 后端定义

class Backend:
    __slots__ = ("name", "kind", "target", "options", "pkg", "note")

    def __init__(self, name, kind, target, options, pkg, note):
        self.name = name
        self.kind = kind          # 'ort' | 'ov'
        self.target = target      # EP 名 或 OpenVINO 设备名
        self.options = options
        self.pkg = pkg
        self.note = note

    def __repr__(self):
        return f"<Backend {self.name} {self.kind}:{self.target}>"


BACKENDS = {
    "cpu": Backend("cpu", "ort", "CPUExecutionProvider", {},
                   "onnxruntime", "任何机器，无需显卡"),
    "cuda": Backend("cuda", "ort", "CUDAExecutionProvider",
                    {"device_id": 0,
                     "arena_extend_strategy": "kSameAsRequested",
                     "cudnn_conv_algo_search": "HEURISTIC",
                     "do_copy_in_default_stream": "1"},
                    "onnxruntime-gpu", "NVIDIA，需 CUDA 12 运行库 + cuDNN 9"),
    "dml": Backend("dml", "ort", "DmlExecutionProvider", {"device_id": 0},
                   "onnxruntime-directml",
                   "NV/AMD/Intel 任一 DX12 显卡（Windows 上 AMD 的唯一路径）"),
    "ov-cpu": Backend("ov-cpu", "ov", "CPU", {},
                      "openvino", "Intel CPU / oneDNN"),
    "ov-gpu": Backend("ov-gpu", "ov", "GPU", {},
                      "openvino", "Intel iGPU / Arc，Level Zero + XMX"),
    "ov-npu": Backend("ov-npu", "ov", "NPU", {},
                      "openvino", "Intel Core Ultra NPU"),
}

AUTO_ORDER = ("cuda", "dml", "ov-gpu", "cpu")

# 段分配默认表：--device 只给一个值时，各段按它分。依据 §20.4 的定量结论：
# fp32 的 conv 在哪都算得对且更快，放心上加速器；int8 两段（DynamicQuantizeLinear
# + MatMulInteger）在 GPU/NPU 上要么被 Memcpy 切碎变慢、要么直接算错
# （OpenVINO GPU 出 NaN 且不抛异常），一律留 CPU。cpu / ov-cpu 没有这个问题，
# 三段都留在本后端。显式给 --conv-device / --encoder-device / --decoder-device
# 时这里的默认让位；auto / 未知值不查表，维持「三段同设备、resolve 校验」。
STAGE_DEFAULTS = {
    "cpu":    ("cpu", "cpu", "cpu"),
    "cuda":   ("cuda", "cpu", "cpu"),
    "dml":    ("dml", "cpu", "cpu"),
    "ov-cpu": ("ov-cpu", "ov-cpu", "ov-cpu"),
    "ov-gpu": ("ov-gpu", "cpu", "cpu"),
    "ov-npu": ("ov-npu", "cpu", "cpu"),
}


def import_ort():
    try:
        import onnxruntime as ort
    except ImportError as e:  # pragma: no cover
        raise BackendError(
            "本进程没有可用的 onnxruntime。按目标设备装一个：\n"
            "  CPU            pip install onnxruntime\n"
            "  NVIDIA         pip install onnxruntime-gpu\n"
            "  AMD/Intel GPU  pip install onnxruntime-directml\n"
            "  Intel GPU/NPU  改用 --device ov-gpu / ov-npu（pip install openvino）\n"
            f"（原始错误：{e}）"
        ) from e
    return ort


_OV_CORE = None


def openvino_core():
    """导入 openvino 并建 Core。导入动作本身会把 openvino/libs 加进 DLL 搜索路径。"""
    global _OV_CORE
    try:
        import openvino as ov
    except ImportError as e:  # pragma: no cover
        raise BackendError(
            "本进程没有 openvino。Intel CPU/GPU/NPU 需要：pip install openvino\n"
            f"（原始错误：{e}）"
        ) from e
    if _OV_CORE is None:
        _OV_CORE = (ov, ov.Core())
    return _OV_CORE


def ort_available_eps():
    try:
        return set(import_ort().get_available_providers())
    except BackendError:
        return set()


def _find_dll(name):
    """按 Windows 搜索顺序粗查：已加载 -> System32 -> PATH。"""
    import ctypes
    try:
        ctypes.WinDLL(name)
        return True
    except OSError:
        pass
    for r in [os.environ.get("SystemRoot", r"C:\Windows") + r"\System32"] + \
             os.environ.get("PATH", "").split(os.pathsep):
        if r and os.path.isfile(os.path.join(r, name)):
            return True
    return False


def describe(backend):
    """返回 (可用?, 说明)。只做能静态判定的部分，真正的证明在建 session 之后。"""
    if backend.kind == "ov":
        try:
            ov, core = openvino_core()
        except BackendError as e:
            return False, str(e)
        try:
            devs = list(core.available_devices)
        except Exception as e:  # noqa: BLE001
            return True, f"openvino 已就绪，但设备枚举失败（{type(e).__name__}: {e}）"
        fams = {d.split(".")[0] for d in devs}
        if backend.target not in devs and backend.target not in fams:
            return False, f"本机 OpenVINO 只看到 {devs}，没有 {backend.target}"
        return True, f"openvino {ov.__version__}，可用设备 {devs}"

    eps = ort_available_eps()
    if not eps:
        return False, "本进程没有 onnxruntime"
    if backend.target not in eps:
        return False, (f"当前 onnxruntime 未提供 {backend.target}"
                       f"（可用：{', '.join(sorted(eps))}）→ 需要 {backend.pkg}")
    if backend.name == "cuda":
        missing = [d for d in CUDA_REQUIRED_DLLS if not _find_dll(d)]
        if missing:
            return False, ("CUDA EP 缺运行库：" + ", ".join(missing)
                           + " → pip install nvidia-cublas-cu12 nvidia-cudnn-cu12 "
                             "nvidia-cufft-cu12 nvidia-cuda-runtime-cu12（或装 CUDA 12 Toolkit）")
        return True, "EP 已注册且运行库齐全"
    return True, "EP 已注册"


def resolve(device, ov_device=None):
    device = (device or "auto").strip().lower()
    if device == "auto":
        tried = []
        for name in AUTO_ORDER:
            ok, why = describe(BACKENDS[name])
            tried.append(f"  {name}: {why}")
            if ok:
                return BACKENDS[name]
        raise BackendError("auto 没有可用后端：\n" + "\n".join(tried))
    if device not in BACKENDS:
        raise BackendError(f"未知设备 {device!r}，可选：{', '.join(BACKENDS)}、auto")
    b = BACKENDS[device]
    if ov_device and b.kind == "ov":
        b = Backend(b.name, b.kind, ov_device, dict(b.options), b.pkg,
                    f"{b.note}（钉到 {ov_device}）")
    ok, why = describe(b)
    if not ok:
        raise BackendError(f"设备 {device} 不可用：{why}")
    return b


# ---------------------------------------------------------------- 音频

def read_wav(path):
    with wave.open(path) as w:
        sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if sw != 2:
        raise ValueError(f"只支持 16bit PCM，当前 {sw * 8}bit：{path}")
    a = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return resample(a, sr, SR)


def resample(x, sr_in, sr_out):
    """线性插值。上游播放器已给到 16k，这里只兜底。"""
    if sr_in == sr_out:
        return x
    n_out = int(round(len(x) * sr_out / sr_in))
    xi = np.arange(n_out) * (sr_in / sr_out)
    return np.interp(xi, np.arange(len(x)), x).astype(np.float32)


# ---------------------------------------------------------------- log-mel（Whisper 口径）

def _hann(n):
    return 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / n)


def _hz2mel(f):
    f = np.asarray(f, dtype=np.float64)
    lin = f / (200.0 / 3.0)
    log = 15.0 + np.log(np.maximum(f, 1e-10) / 1000.0) / (np.log(6.4) / 27.0)
    return np.where(f < 1000.0, lin, log)


def _mel2hz(m):
    m = np.asarray(m, dtype=np.float64)
    lin = m * (200.0 / 3.0)
    log = 1000.0 * np.exp((np.log(6.4) / 27.0) * (m - 15.0))
    return np.where(m < 15.0, lin, log)


_FB = None


def _filterbank():
    global _FB
    if _FB is None:
        mel_f = _mel2hz(np.linspace(_hz2mel(0.0), _hz2mel(SR / 2.0), N_MELS + 2))
        freqs = np.linspace(0.0, SR / 2.0, 1 + N_FFT // 2)
        fdiff = np.diff(mel_f)
        ramps = np.subtract.outer(mel_f, freqs)
        w = np.zeros((N_MELS, 1 + N_FFT // 2))
        for i in range(N_MELS):
            w[i] = np.maximum(0.0, np.minimum(-ramps[i] / fdiff[i], ramps[i + 2] / fdiff[i + 1]))
        w *= (2.0 / (mel_f[2:N_MELS + 2] - mel_f[0:N_MELS]))[:, None]
        _FB = w.astype(np.float32)
    return _FB


def log_mel(audio, pad30=True):
    """WhisperFeatureExtractor 等价：n_fft400 / hop160 / 128 mel / reflect pad / slaney。

    pad30=True 补到 30 s 并丢掉最后一帧 —— 模型按 30 s 切块训练，补齐后输出才符合
    训练分布（实测 pad30 给繁体「嘅/個/裏」，不补给简体「个/里」，且会丢字）。
    代价：audio token 从 52 涨到 390，encoder 与 decoder 的 cross-attention 都变贵。
    """
    a = np.asarray(audio, dtype=np.float32).reshape(-1)
    if pad30:
        a = np.pad(a, (0, max(0, CHUNK - len(a))))[:CHUNK]
    p = np.pad(a, (N_FFT // 2, N_FFT // 2), mode="reflect")
    n = 1 + (len(p) - N_FFT) // HOP
    idx = np.arange(N_FFT)[None, :] + HOP * np.arange(n)[:, None]
    frames = p[idx] * _hann(N_FFT)[None, :]
    sp = np.fft.rfft(frames, n=N_FFT, axis=1)
    mag = (sp.real ** 2 + sp.imag ** 2).astype(np.float32)
    mel = mag @ _filterbank().T
    ls = np.log10(np.maximum(mel, 1e-10))
    ls = np.maximum(ls, ls.max() - 8.0)
    mel = (ls + 4.0) / 4.0
    if pad30:
        mel = mel[:-1]
    return mel.astype(np.float32)


# ---------------------------------------------------------------- tokenizer（只解码）

class Tokenizer:
    """Qwen BPE 的**解码**侧。编码要 merges.txt，这里用不到。"""

    def __init__(self, tokdir):
        self.vocab = json.load(open(os.path.join(tokdir, "vocab.json"), encoding="utf-8"))
        cfg = json.load(open(os.path.join(tokdir, "tokenizer_config.json"), encoding="utf-8"))
        self.special = {int(k): v["content"]
                        for k, v in cfg.get("added_tokens_decoder", {}).items()}
        self.id2tok = {v: k for k, v in self.vocab.items()}
        bs = (list(range(ord("!"), ord("~") + 1)) + list(range(ord("¡"), ord("¬") + 1))
              + list(range(ord("®"), ord("ÿ") + 1)))
        cs = bs[:]
        n = 0
        for b in range(256):
            if b not in bs:
                bs.append(b)
                cs.append(256 + n)
                n += 1
        self.u2b = {chr(c): b for b, c in zip(bs, cs)}

    def decode(self, ids):
        """逐 token 攒原始字节，最后一次 utf-8 解码 —— 汉字会被切到多个 token 上。"""
        buf = bytearray()
        for i in ids:
            if i in self.special:
                s = self.special[i]
                if s.startswith("<|"):
                    continue
                buf += s.encode("utf-8")
            else:
                for c in self.id2tok.get(i, ""):
                    b = self.u2b.get(c)
                    if b is not None:
                        buf.append(b)
                    else:
                        buf += c.encode("utf-8")
        return buf.decode("utf-8", "replace")


# ---------------------------------------------------------------- Stage（一段推理）

class OrtStage:
    """一个 onnxruntime session。run(feed) -> {输出名: 数组}。"""

    kind = "ort"

    def __init__(self, path, backend, threads):
        ort = import_ort()
        so = ort.SessionOptions()
        so.intra_op_num_threads = threads
        so.inter_op_num_threads = 1
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        providers = ([(backend.target, backend.options)] if backend.options
                     else [backend.target])
        try:
            self.sess = ort.InferenceSession(path, so, providers=providers)
        except Exception as e:  # noqa: BLE001
            raise BackendError(
                f"{os.path.basename(path)} 建 session 失败（请求 {backend.target}）："
                f"{type(e).__name__}: {e}\n  需要：{backend.pkg}") from e
        got = self.sess.get_providers()
        if not got or got[0] != backend.target:
            raise BackendError(
                f"{os.path.basename(path)} 的 {backend.target} 没有真正生效"
                f"（实际绑到 {got}）—— 这是回落，拒绝继续。检查 {backend.pkg} 与运行库。")
        self.names = [o.name for o in self.sess.get_outputs()]
        self.device = f"{backend.target} -> {got}"

    def run(self, feed):
        outs = self.sess.run(None, feed)
        return dict(zip(self.names, outs))


def _reshape_bounded(model, ov, bounded_frames, index=0):
    """给第 index 个输入的帧维加有限上界（NPU 唯一需要的模型改动）。

    reshape 只改内存里的 shape 元数据，**onnx 文件一个字节都不动**。
    """
    ps = model.input(index).partial_shape
    # rank 在这个版本是属性（Dimension），不是方法 —— 写成 rank() 只有
    # NPU 分支会执行到，CPU/GPU 永远测不出来。
    if not (ps.rank.is_static and len(ps) == 3):
        raise BackendError(
            f"输入 {index} 的 shape 是 {ps}（rank 不是 3），没法按 [1, 1..N, mel] "
            "给帧维加上界；NPU 需要这个上界，拒绝在没有它的情况下编译。")
    n_mels = ps[2].get_length() if ps[2].is_static else 128
    model.reshape({index: ov.PartialShape(
        [(1, 1), (1, bounded_frames), (n_mels, n_mels)])})


# NPU 帧维上界的默认值：宿主窗口上限 30 s（3000 帧 @10 ms），留两成余量。
# worker 显式传 3200；CLI 直接 --device ov-npu 时用它，更长的音频用 --npu-frames 调大
# （超过上界是 OpenVINO 报错，不是崩进程 —— 两者代价完全不同）。
NPU_DEFAULT_FRAMES = 3200

# 上界大到这个量级就当"没给上界"：真有上界的是几千，没给的是 INT64_MAX 级。
_UNBOUNDED_MAX = 1 << 31


def _assert_bounded(model, label):
    """编译前兜底：任一输入维没有有限上界就明确抛错。

    这条不是洁癖。第三方在 Ultra 9 285H 上实测：无上界的动态维会让 vpux 编译器
    在 Pad/ShapeOf 处 `LLVM ERROR: Failed to infer result type(s)`，进程以退出码
    127 直接死掉 —— 那是 abort()，Python 的 try/except **抓不到**，连"降档"的
    机会都没有。所以在 compile_model 之前把它变成一次普通的 BackendError。
    """
    for i, inp in enumerate(model.inputs):
        ps = inp.partial_shape
        if not ps.rank.is_static:
            raise BackendError(
                f"{label} 输入 {i} 的 rank 不静态（{ps}），无法确认上界；"
                "NPU 不接受这种模型。")
        for j in range(len(ps)):
            d = ps[j]
            if d.is_static:
                continue
            try:
                hi = d.get_max_length()
            except Exception:  # noqa: BLE001 - 取不到就是没上界
                hi = -1
            if hi < 0 or hi > _UNBOUNDED_MAX:
                raise BackendError(
                    f"{label} 输入 {i} 第 {j} 维没有有限上界（{ps}）——NPU 编译器会在"
                    "Pad/ShapeOf 处 LLVM ERROR 硬崩（退出码 127，Python 抓不到）。"
                    "先 reshape 给上界（conv_bounded_frames）。")


class OvStage:
    """一个 OpenVINO CompiledModel。动态形状原生支持，每步不用重编译。

    `bounded_frames` 只给 NPU 用：NPU 编译器拒绝"上界为 INT64_MAX"的动态维，
    报的是一串看着像算子不支持的错（`Upper bounds are not specified`）。给
    conv 的帧维一个上界就能编过（实测 NPU4000 离线编译：500 帧、3000 帧、以及
    `[1, 1..3000]` 带界动态形状三种都成功；第三方在 Ultra 9 285H 上真机执行
    同一段，rel_err 0.001、比 CPU 快 1.5~1.9 倍）。`reshape` 只改内存里的 shape
    元数据，**onnx 文件一个字节都不动**。

    注意"带界动态"与"静态"是两件事：带上界之后**一次编译覆盖 1..N 任意帧数**，
    不需要为每种音频长度各编一份，也不需要把音频 padding/trimming 到离散档
    （那会真丢音频、改 mel 内容）。第三方报告把它写成"NPU 不支持动态形状"，
    照那个结论去做"预定义帧数集合 + 形状路由"是白伤精度。
    """

    kind = "ov"

    def __init__(self, path, backend, threads, bounded_frames=None):
        ov, core = openvino_core()
        self.model = core.read_model(path)
        if backend.target.upper().startswith("NPU"):
            # 无上界的动态维会让 NPU 编译器 abort（退出码 127，Python 抓不到），
            # 所以这里做两件事：先 reshape 给上界，再在编译前断言上界确实有限。
            # 以前写的是 "bounded_frames and ..."：没传上界就悄悄跳过 reshape，
            # 直接进 compile_model 硬崩 —— 这正是第三方报告里那次崩溃的形态。
            # 没显式给就用默认值（CLI 走的是这条路），断言仍然兜底。
            _reshape_bounded(self.model, ov, bounded_frames or NPU_DEFAULT_FRAMES)
            _assert_bounded(self.model, os.path.basename(path))
        cfg = {"PERFORMANCE_HINT": "LATENCY"}
        if backend.target.upper().startswith("CPU"):
            cfg["INFERENCE_NUM_THREADS"] = str(threads)
        if backend.target.upper().startswith("NPU"):
            # 诊断口：没有 NPU 硬件时用 NPU_PLATFORM 做离线编译，验证包里的编译器
            # 能处理这段模型（npu_precheck.py 用的就是它）。有硬件时留空走自动探测。
            plat = os.environ.get("OV_NPU_PLATFORM")
            if plat:
                cfg["NPU_PLATFORM"] = plat
        # NPU 编译一次要几十秒（96 MB 的 vcl 编译器），不缓存的话每次启动都重来。
        cache = os.environ.get("OV_CACHE_DIR")
        if cache:
            os.makedirs(cache, exist_ok=True)
            cfg["CACHE_DIR"] = cache
        try:
            self.cm = core.compile_model(self.model, backend.target, cfg)
        except Exception:
            try:
                self.cm = core.compile_model(self.model, backend.target)
            except Exception as e:  # noqa: BLE001
                raise BackendError(
                    f"{os.path.basename(path)} 编译到 OpenVINO:{backend.target} 失败："
                    f"{type(e).__name__}: {e}\n  本机设备见 --list-devices") from e
        self.names = [o.get_any_name() for o in self.model.outputs]
        self._ports = {n: self.cm.output(n) for n in self.names}
        try:
            full = self.cm.get_property("FULL_DEVICE_NAME")
        except Exception:  # noqa: BLE001
            full = backend.target
        self.device = f"{backend.target} -> {full}"

    def run(self, feed):
        r = self.cm(feed)
        return {n: np.asarray(r[self._ports[n]]) for n in self.names}


def make_stage(path, backend, threads, bounded_frames=None):
    if backend.kind == "ov":
        return OvStage(path, backend, threads, bounded_frames)
    return OrtStage(path, backend, threads)


# ---------------------------------------------------------------- 引擎

class Engine:
    """三个 onnx 各一个 Stage，**三段可以分属不同后端**。

    为什么要分三段：实测定量结果表明 int8 的 encoder/decoder 在部分设备上不仅慢，
    还会静默算错（OpenVINO GPU 跑 encoder.int8 直接出 NaN，不抛异常）。而纯 fp32 的
    conv_frontend 在任何设备上都正确且更快。所以典型配置是：

        加速后端（cuda/dml/ov-gpu/ov-npu）: 只 conv 上加速器，int8 两段留 CPU
        cpu / ov-cpu  : 三段都 CPU
        （缺省分配在 STAGE_DEFAULTS；--conv-device 等显式指定时让位）
    """

    def __init__(self, model_dir, device="cpu", conv_device=None, encoder_device=None,
                 decoder_device=None, threads=None, max_len=512, ov_device=None,
                 conv_bounded_frames=None):
        self.model_dir = model_dir
        if device in STAGE_DEFAULTS:
            _c, _e, _d = STAGE_DEFAULTS[device]
        else:
            _c = _e = _d = device  # auto 等：三段同值，交给 resolve 校验
        pick = lambda d, fb: resolve(d or fb, ov_device=ov_device)  # noqa: E731
        self.conv_backend = pick(conv_device, _c)
        self.enc_backend = pick(encoder_device, _e)
        self.dec_backend = pick(decoder_device, _d)
        self.ov_device = ov_device
        self.threads = threads or min(6, os.cpu_count() or 6)
        self.max_len = max_len
        self.conv_bounded_frames = conv_bounded_frames

        t0 = time.perf_counter()
        self.conv = self._stage(CONV_FILE, self.conv_backend, conv_bounded_frames)
        self.enc = self._stage(ENC_FILE, self.enc_backend)
        self.dec = self._stage(DEC_FILE, self.dec_backend)
        self.t_compile_s = round(time.perf_counter() - t0, 2)

        self.tok = Tokenizer(os.path.join(model_dir, "tokenizer"))
        V = self.tok.vocab
        nl = V["Ċ"]
        self.prefix = [IM_START, V["system"], nl, IM_END, nl,
                       IM_START, V["user"], nl, AUDIO_START]
        self.suffix = [AUDIO_END, IM_END, nl, IM_START, V["assistant"], nl]

    def _stage(self, fname, backend, bounded_frames=None):
        path = os.path.join(self.model_dir, fname)
        if not os.path.isfile(path):
            raise BackendError(f"缺模型文件：{path}")
        return make_stage(path, backend, self.threads, bounded_frames)

    def device_report(self):
        def one(b, s):
            return {"device": b.name, "kind": b.kind, "bound": s.device}
        return {"conv": one(self.conv_backend, self.conv),
                "encoder": one(self.enc_backend, self.enc),
                "decoder": one(self.dec_backend, self.dec),
                "compile_s": self.t_compile_s}

    # -- 数值门禁 ------------------------------------------------------

    # 门禁阈值按段区分：它抓的是「算坏」（NaN / 垃圾），不是位级一致。
    # 实测本机：fp32 的 conv 在各设备上偏差 8e-05 ~ 4e-03；而 int8 的 encoder 在
    # GPU 上因为 dequant+重排，相对偏差稳定在 0.12 左右（CUDA 0.126 / DML 0.120），
    # 文本可能出现单字差异（嘢→野）。要严格一致就把 int8 段留 CPU。
    VERIFY_LIMITS = {"conv": 0.02, "encoder": 0.20, "decoder": 0.20}

    def verify(self, limits=None, stages=None):
        """逐段比对 CPU 参考值。

        这不是可选装饰：OpenVINO GPU 跑 encoder.int8 会输出 NaN 而**不抛异常**，
        CER 自检能抓到但要跑完整段音频。这个探针一次前向就够，且能定位到哪一段坏。
        参考实现取「同一运行时族的 CPU」，只衡量设备差异，不掺框架差异。

        `stages` 只挑要验的段（如 `("conv",)`）：留在 CPU 的段与参考同设备，比值
        恒为 0，而给 720 MB 的 decoder.int8 编一份 CPU 参考要几十秒，白花。
        """
        limits = limits or self.VERIFY_LIMITS
        rng = np.random.default_rng(12345)
        mel = (rng.standard_normal((1, 100, 128)) * 0.5).astype(np.float32)
        conv_feat = (rng.standard_normal((1, 13, 896)) * 0.5).astype(np.float32)
        dec_feed = {
            "input_ids": np.array([[IM_END]], np.int64),
            "audio_features": (rng.standard_normal((1, 13, 1024)) * 0.5).astype(np.float32),
            "attention_mask": np.ones((1, 2), np.int64),
            "cache_position": np.array([0], np.int64),
        }
        for i in range(NLAYER):
            dec_feed[f"cache_key_{i}"] = np.zeros((1, 16, NHEAD, HDIM), np.float32)
            dec_feed[f"cache_value_{i}"] = np.zeros((1, 16, NHEAD, HDIM), np.float32)

        cases = (
            ("conv", self.conv, self.conv_backend, CONV_FILE,
             {"input_features": mel}),
            ("encoder", self.enc, self.enc_backend, ENC_FILE,
             {"input_features": conv_feat,
              "feature_attention_mask": np.ones((1, 13), np.bool_)}),
            ("decoder", self.dec, self.dec_backend, DEC_FILE, dec_feed),
        )
        report = {}
        for name, stage, backend, fname, feed in cases:
            if stages is not None and name not in stages:
                continue
            cpu_twin = Backend("cpu-ref", backend.kind,
                               "CPU" if backend.kind == "ov" else "CPUExecutionProvider",
                               {}, backend.pkg, "数值参考")
            ref_stage = make_stage(os.path.join(self.model_dir, fname), cpu_twin, self.threads)
            ref = ref_stage.run(feed)[ref_stage.names[0]]
            got = stage.run(feed)[stage.names[0]]
            denom = float(np.linalg.norm(ref)) or 1.0
            rel = float(np.linalg.norm(got - ref) / denom)
            bad_nan = bool(np.isnan(got).any())
            lim = limits.get(name, 0.02)
            report[name] = {
                "device": backend.name,
                "rel_err": None if bad_nan else round(rel, 5),
                "limit": lim,
                "has_nan": bad_nan,
                "ok": (not bad_nan) and rel <= lim,
            }
        return report

    # -- 推理 -----------------------------------------------------------

    def _encode(self, audio, pad30):
        mel = log_mel(audio, pad30=pad30)
        # conv_frontend 把帧数补到 100 的整数倍，下采样率恰为 13/100
        conv_out = self.conv.run({"input_features": mel[None]})[self.conv.names[0]]
        n_tok = conv_out.shape[1]
        feat = self.enc.run({"input_features": conv_out,
                             "feature_attention_mask": np.ones((1, n_tok), np.bool_)})
        return feat[self.enc.names[0]], n_tok, mel.shape[0]

    def transcribe(self, audio, max_new=96, pad30=True):
        t0 = time.perf_counter()
        audio_feat, n_tok, n_mel = self._encode(audio, pad30)
        t_enc = time.perf_counter() - t0

        # audio_pad 必须放 n_tok 个。放 1 个（chat_template 里的占位写法）时模型恒输出
        # 「language None<asr_text>」，看着跑通了其实完全没听到音频。
        prompt = self.prefix + [AUDIO_PAD] * n_tok + self.suffix
        cap = len(prompt) + max_new + 4
        if cap > self.max_len:
            raise ValueError(f"prompt {len(prompt)} + max_new {max_new} 超过 max_len {self.max_len}")

        caches = {}
        for i in range(NLAYER):
            caches[f"cache_key_{i}"] = np.zeros((1, cap, NHEAD, HDIM), np.float32)
            caches[f"cache_value_{i}"] = np.zeros((1, cap, NHEAD, HDIM), np.float32)

        out_ids, pos, step = [], 0, np.array([prompt], dtype=np.int64)
        t1 = time.perf_counter()
        while True:
            feed = {
                "input_ids": step,
                "audio_features": audio_feat,
                "attention_mask": np.ones((1, pos + step.shape[1]), np.int64),
                "cache_position": np.arange(pos, pos + step.shape[1], dtype=np.int64),
            }
            feed.update(caches)
            outs = self.dec.run(feed)
            for name in self.dec.names[1:]:
                which = "key" if name.startswith("key") else "value"
                d = outs[name]
                caches[f"cache_{which}_{name.split('_')[-1]}"][:, pos:pos + d.shape[1]] = d
            pos += step.shape[1]
            nxt = int(np.argmax(outs[self.dec.names[0]][0, -1]))
            if nxt in EOS or len(out_ids) >= max_new:
                break
            out_ids.append(nxt)
            step = np.array([[nxt]], dtype=np.int64)
        t_dec = time.perf_counter() - t1

        raw = self.tok.decode(out_ids)
        lang, _, body = raw.partition("<asr_text>")
        return {
            "text": (body or raw),
            "language": lang.replace("language", "").strip() or "?",
            "audio_tokens": n_tok,
            "mel_frames": n_mel,
            "n_tokens": len(out_ids),
            "t_encode_s": round(t_enc, 3),
            "t_decode_s": round(t_dec, 3),
        }


# ---------------------------------------------------------------- CLI

def _list_devices():
    print("== onnxruntime ==")
    try:
        ort = import_ort()
        print(f"  {ort.__version__}  from {os.path.dirname(ort.__file__)}")
        print(f"  EP：{', '.join(ort.get_available_providers())}")
    except BackendError as e:
        print(f"  不可用（{str(e).splitlines()[0]}）")

    print("== openvino ==")
    try:
        ov, core = openvino_core()
        devs = list(core.available_devices)
        print(f"  {ov.__version__}")
        print(f"  设备：{devs if devs else '无'}")
        for d in devs:
            try:
                print(f"    {d:<8} {core.get_property(d, 'FULL_DEVICE_NAME')}")
            except Exception:  # noqa: BLE001
                pass
    except BackendError as e:
        print(f"  不可用（{str(e).splitlines()[0]}）")

    print("\n== 后端判定 ==")
    width = max(len(n) for n in list(BACKENDS) + ["auto"])
    for name, b in BACKENDS.items():
        ok, why = describe(b)
        print(f"  [{'OK ' if ok else ' --'}] {name:<{width}} {why}")
    print(f"\n  auto 顺序：{' -> '.join(AUTO_ORDER)}")


def _edit_distance(a, b):
    if len(a) < len(b):
        a, b = b, a
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def _selftest(engine, model_dir, max_cer=0.30):
    """金样本自检：模型目录里放 selftest.wav + selftest.txt。

    pad30 会翻转输出字形（繁/简），而 selftest.txt 只可能写其中一种。字形差异能把
    CER 顶到 0.28、把 0.30 的阈值变成摆设，所以两种模式各跑一次取更好的那个 ——
    要检的是「这个设备听没听到音频」，不是用哪种字体。
    """
    wav = os.path.join(model_dir, "selftest.wav")
    ref = os.path.join(model_dir, "selftest.txt")
    if not (os.path.isfile(wav) and os.path.isfile(ref)):
        raise SystemExit(f"模型目录缺 selftest.wav / selftest.txt：{model_dir}")
    expect = open(ref, encoding="utf-8").read().strip()
    audio = read_wav(wav)
    best = None
    t0 = time.perf_counter()
    for pad in (True, False):
        r = engine.transcribe(audio, pad30=pad)
        cer = _edit_distance(r["text"].strip(), expect) / max(1, len(expect))
        if best is None or cer < best[0]:
            best = (cer, r, pad)
    el = time.perf_counter() - t0
    cer, r, pad = best
    out = {**r, "pad30_used": pad, "selftest_s": round(el, 2), "cer": round(cer, 4),
           "pass": cer <= max_cer, "devices": engine.device_report()}
    print(json.dumps(out, ensure_ascii=False, indent=2))
    return cer <= max_cer


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)

    def opt(name, default=None):
        if name in argv:
            i = argv.index(name)
            if i + 1 < len(argv):
                return argv[i + 1]
            raise SystemExit(f"{name} 需要一个值")
        return default

    if "--list-devices" in argv:
        _list_devices()
        return 0

    model_dir = opt("--model-dir")
    if not model_dir:
        raise SystemExit("需要 --model-dir（含三个 onnx 和 tokenizer/ 的目录）")
    device = opt("--device", "cpu")
    wav = opt("--wav")
    pad30 = "--no-pad30" not in argv
    threads = opt("--threads")

    try:
        engine = Engine(model_dir, device=device,
                        conv_device=opt("--conv-device"),
                        encoder_device=opt("--encoder-device"),
                        decoder_device=opt("--decoder-device"),
                        threads=int(threads) if threads else None,
                        ov_device=opt("--ov-device"),
                        conv_bounded_frames=(int(opt("--npu-frames"))
                                             if opt("--npu-frames") else None))
    except BackendError as e:
        print(f"[后端不可用] {e}", file=sys.stderr)
        return 2

    if "--verify" in argv:
        rep = engine.verify()
        print(json.dumps(rep, ensure_ascii=False, indent=2))
        ok = all(v["ok"] for v in rep.values())
        if not ok:
            print("[数值门禁未通过] 上面标 ok=false 的段算错了，别拿它出字幕。", file=sys.stderr)
        return 0 if ok else 1

    if "--selftest" in argv:
        return 0 if _selftest(engine, model_dir) else 1
    if not wav:
        raise SystemExit("需要 --wav，或改用 --selftest / --verify / --list-devices")

    audio = read_wav(wav)
    dur = len(audio) / SR
    # 冷态首跑跑在睿频（实测同一段十连测 0.107 -> 1.048），必须预热后取中位
    for _ in range(int(opt("--warmup", "0"))):
        engine.transcribe(audio, pad30=pad30)
    runs = [engine.transcribe(audio, pad30=pad30)
            for _ in range(max(1, int(opt("--repeat", "1"))))]
    r = dict(runs[-1])
    r["t_encode_s"] = round(statistics.median([x["t_encode_s"] for x in runs]), 3)
    r["t_decode_s"] = round(statistics.median([x["t_decode_s"] for x in runs]), 3)
    r.update(wav=os.path.basename(wav), dur_s=round(dur, 2),
             rtf=round((r["t_encode_s"] + r["t_decode_s"]) / dur, 3),
             pad30=pad30, runs=len(runs), devices=engine.device_report())

    if "--json" in argv:
        print(json.dumps(r, ensure_ascii=False, indent=2))
        return 0
    d = r["devices"]
    print(f"{r['wav']}  {dur:.2f}s  pad30={pad30}")
    for stage in ("conv", "encoder", "decoder"):
        print(f"  {stage:<8} {d[stage]['device']:<8} {d[stage]['bound']}")
    print(f"  audio_tokens={r['audio_tokens']}  out_tokens={r['n_tokens']}  "
          f"lang={r['language']}  建会话 {d['compile_s']}s")
    print(f"  encode {r['t_encode_s']:.2f}s  decode {r['t_decode_s']:.2f}s  rtf={r['rtf']}")
    print(f"  TEXT: {r['text']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
