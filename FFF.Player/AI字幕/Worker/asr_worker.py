# 3FP realtime AI-subtitle ASR worker —— sherpa-onnx 路线（进程内推理）。
#
# 协议（冻结，见 docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md §6）：
#   stdin  逐帧：[u32 LE metaLen][metaLen 字节 UTF-8 JSON][PCM 字节]
#   stdout 逐行：一行一个 JSON 对象
#       {"type":"status","state":"loading|ready|warning|error","detail":"...","fatal":bool}
#       {"type":"final","generation":N,"start100ns":S,"end100ns":E,
#        "text":"...","language":"Chinese","target":"Chinese","matched":true}
#       {"type":"stats","rtf":0.31,"windows":42,"dropped":0}
#   "target"/"matched" 只在 --language 有值时有意义；worker 从不自己丢结果，
#   丢不丢由宿主决定（那个开关不该逼 worker 重启）。
#
# 两条运行时，一份 worker：
#   * onnx   —— sherpa-onnx 进程内推理，权重是官方 int8 三件套
#                （DynamicQuantizeLinear -> MatMulInteger，W8A8）。
#   * llama  —— llama.cpp 子进程，权重是 GGUF Q8_0（block-wise weight-only int8）。
#
# 为什么会有两条（2026-10-29 定，依据见交接文档 §26–§31）：
#   官方 int8 那份用的是 QOperator 动态量化，激活量化成 uint8，而 CUDA 的
#   MatMulInteger 只注册 int8 版、且要求权重 zero_point 是标量 —— 本模型是
#   per-channel，于是整段静默回落 CPU（profiling 里 220 个 MatMulInteger 全在
#   CPU EP）。改写成 s8s8 后能在 CUDA 上算，但在 x86-64 CPU 上慢 29 倍，
#   统一不了。GGUF 走的是"只量化权重、反量化写进 kernel"，同一份 Qwen3-ASR
#   权重的 int8 封装，CPU 上比 sherpa 快 2.2–2.6 倍（同一段 5 s 音频
#   1.24 s vs 2.75 s），三家 GPU 用一份 Vulkan 运行时就能吃。
#   ⇒ 默认走 llama；onnx 留给 NPU 与老设备（NPU 只能吃静态 QDQ，是另一条支线）。
#
# llama 那条腿的固有成本（不是失误，是进程边界本身）：端口分配、健康轮询、
# 每个窗口落一次临时 wav、进程回收。上一版删掉这些是因为当时只有 sherpa 一条
# 腿；现在两条腿并存，它们必须回来。两条硬约束写在 LlamaBackend 里。
#
# 保留下来的东西都是有独立价值的：窗口按**媒体时间**切（不是墙钟，暂停/跳转不
# 会串窗）、对白优先下混、三条文本污染过滤、语言归一。它们的实测依据留在原处。

import argparse
from bisect import bisect_left, bisect_right
from collections import deque
from difflib import SequenceMatcher
import io
from itertools import accumulate
import json
import os
import re
import socket
import subprocess
import sys
import time
import traceback
import unicodedata
import urllib.request
import uuid
import wave
from pathlib import Path
from typing import NamedTuple

try:
    import numpy as np
except ImportError:
    if __name__ != "__main__":
        raise
    sys.stdout.reconfigure(encoding="utf-8")
    print(json.dumps({"type": "status", "state": "error", "fatal": True,
                      "detail": "识别进程无法加载 NumPy。请在 AI 字幕设置中补全官方扩展包。"},
                     ensure_ascii=False), flush=True)
    sys.exit(2)

# sherpa_onnx 是可选的安装项：缺了它这个 worker 一个设备也跑不了，但"缺依赖"
# 必须走协议报 fatal（宿主能显示原因），而不是在 import 期甩一串 traceback 让
# 用户对着白屏猜。所以这里兜住导入异常，由 SherpaBackend.load() 转成 RuntimeError。
try:
    import sherpa_onnx
except Exception as _exc:  # noqa: BLE001 - 缺依赖要在协议里说清楚
    sherpa_onnx = None
    _SHERPA_IMPORT_ERROR = _exc
else:
    _SHERPA_IMPORT_ERROR = None

TARGET_RATE = 16000

# 模型目录的必备件。少一个都加载不了，与其让 sherpa_onnx 抛一句 C++ 断言，不如
# 在 main() 里逐个点名报错 —— 用户看到"缺 encoder"才知道该补哪个文件。
# 只钉主干名、不钉全名，理由见 _pick_onnx。
_MODEL_STEMS = ("conv_frontend", "encoder", "decoder")


def _pick_onnx(model_dir, stem):
    """在模型目录里挑 <stem>.onnx；没有就接受唯一的 <stem>.<后缀>.onnx。

    k2-fsa 的官方包是 encoder.int8.onnx / decoder.int8.onnx（量化标在文件名里），
    换 int4 后又变成 encoder.int4.onnx。硬钉 encoder.onnx 等于要求每个打包的人
    先重命名两个文件 —— 而"解压即用"正是便携包的全部意义。所以按主干名解析。
    """
    exact = model_dir / f"{stem}.onnx"
    if exact.is_file():
        return exact
    cands = sorted(p for p in model_dir.glob(f"{stem}.*.onnx") if p.is_file())
    if len(cands) == 1:
        return cands[0]
    if len(cands) > 1:
        raise RuntimeError(
            f"{stem} 有多个候选：{'、'.join(p.name for p in cands)}；"
            f"一个模型目录里只能留一份")
    raise RuntimeError(f"缺 {stem}.onnx（也接受 {stem}.int8.onnx 这类带后缀的）")


# 引擎判定：模型目录里放的是什么容器，就决定了走哪条运行时。不靠清单里的
# engine 字段做主判据 —— 文件本身是唯一真相，清单写错了也不该把包判死。
# gguf 里嵌了 tokenizer 与词表，所以 llama 引擎不需要 tokenizer/ 目录。
_ENGINE_LLAMA = "llama"
_ENGINE_ONNX = "onnx"


def _detect_engine(model_dir):
    model_dir = Path(model_dir)
    if any(model_dir.glob("*.gguf")):
        return _ENGINE_LLAMA
    return _ENGINE_ONNX


def _llama_weights(model_dir):
    """返回 (主权重, mmproj)。两者都在模型目录根的 *.gguf 里。"""
    model_dir = Path(model_dir)
    allg = sorted(p for p in model_dir.glob("*.gguf") if p.is_file())
    mm = [p for p in allg if "mmproj" in p.name.lower()]
    main = [p for p in allg if "mmproj" not in p.name.lower()]
    if not main:
        raise RuntimeError("模型目录里没有主权重 *.gguf")
    if not mm:
        raise RuntimeError("模型目录里没有 mmproj*.gguf（音频编码器，缺了只能转文字模型不认音频）")
    # 分片权重（xxx-00001-of-00002.gguf）会命中多个，交给 llama.cpp 自己按第一片拼。
    return main[0], mm[0]


def _missing_items(model_dir, engine):
    """清点缺什么。只报不抛，给 main() 拼一句人能看懂的错。"""
    out = []
    if engine == _ENGINE_LLAMA:
        try:
            _llama_weights(model_dir)
        except RuntimeError as exc:
            out.append(str(exc))
        return out
    for stem in _MODEL_STEMS:
        try:
            _pick_onnx(model_dir, stem)
        except RuntimeError as exc:
            out.append(str(exc))
    if not (model_dir / "tokenizer").is_dir():
        out.append("缺 tokenizer/ 目录（内含 vocab.json、merges.txt 等）")
    return out


# 失控输出的判据：去空白后长度 ≥ 该值，且最高频单字占比 ≥ 该比例 → 判为模型在
# 空转（重复同一个字直到撞上限），丢弃不上屏。
_DEGENERATE_MIN_LEN = 24
_DEGENERATE_TOP_RATIO = 0.6

# 指令泄漏的判据（2026-10-25 实测）：语音极稀疏时模型会把拼进 prompt 的
# instruction 原样当内容吐出来（"Transcribe audio to text"），有时连 chat 模板的
# 标记（<asr_text> / <|im_start|>）一起。它字面是"识别结果"，但和失控编字是同一
# 类污染——糊到屏幕上比没字幕更像"功能坏了"。处置同失控：丢弃 + 提示一次。
#
# 只判"短得像一句指令"的文本：长度超过下面的上限就放行，免得把真在讲 ASR 教程的
# 长段对白误杀（那种场景里这句话只是长句里的一小段）。
_PROMPT_LEAK_PHRASES = (
    "transcribe audio to text",
    "transcribe the audio to text",
    "transcribe audio into text",
    "transcribe the following audio",
    "transcribe this audio",
    "transcribe the speech",
    "please transcribe",
    "transcribe audio",
)
# 标记残留不受长度限制：正常转写结果里不可能出现 chat 模板标记，出现即泄漏。
_PROMPT_LEAK_MARKERS = (
    "<asr_text>", "</asr_text>", "asr_text",
    "<|audio|>", "<|im_start|>", "<|im_end|>", "<|endoftext|>",
)
_PROMPT_LEAK_MAX_LEN = 160
# 正文里 CJK 占比超过这个值就不判短语泄漏：模板串是纯英文指令，中文长句里夹
# 一句英文指令是正常内容（讲 ASR 教程的片段实测就长这样），杀了就是误伤。
_CJK_RE = re.compile(r"[\u3000-\u303f\u4e00-\u9fff\u3040-\u30ff\uac00-\ud7af]")
_PROMPT_LEAK_MAX_CJK_RATIO = 0.2

# 模型偶尔把语言标签留在正文里（"language Chinese<asr_text>正文"）。这是模型自己
# 的输出习惯，与运行时无关，所以照旧拆；拆不出就整句当正文。
_ASR_TEXT_RE = re.compile(r"^language\s+([A-Za-z\-]+)<asr_text>\s*(.*)$", re.S)

# 自检的容忍度：字符级编辑距离 / max(1, 期望长度)。0.30 是"能听懂大意但个别字
# 不同"的量级，足以区分"模型加载对了"和"权重/分词器配错了"（后者 CER 接近 1）。
_SELFTEST_MAX_CER = 0.30

# --------------------------------------------------------------------------
# 设备表（唯一真值表）
#
# id -> (provider 字符串, 显示名)。provider 的合法取值由运行时决定：
#   onnx  走 sherpa-onnx 的 EP 名（cpu / cuda，后者要 GPU 版 wheel）；
#   llama 的 provider 是 --list-devices 现选的设备名（Vulkan0 / CUDA0 / cpu），
#   加载时才定得下来，所以表里留空串，由 LlamaBackend.load() 填真值。
# 新增设备 = 往这张表加一行 + 在 _CANDIDATES 里给它一个候选序列，别处不用改。
# --------------------------------------------------------------------------
_BACKENDS = {
    "cpu": ("cpu", "CPU"),
    "cuda": ("cuda", "NVIDIA CUDA"),
    "gpu": ("", "GPU"),
}

# 请求 -> 候选序列（第一个是首选，后面是唯一的降档路径）。按引擎分开：
#   onnx  —— 一份 int8 权重，能上加速器的只有 conv（§20/§26），cuda 是 EP 名；
#            npu / ov-gpu / ov-cpu 走 OpenVINO 原生（OvBackend），三段各挂各的。
#            npu 的降档落到 cpu（sherpa）：OpenVINO CPU 比 sherpa 慢 2.3 倍，
#            兜底要挑快的，而不是"同族所以顺延"。
#   llama —— 一份 GGUF，同一份二进制既跑 CPU 也跑 GPU，档位只剩"要不要上显卡"。
#            cuda 是旧宿主会传的值，按 gpu 处理：它是别名，不是第二档。
#            npu 这条腿没有实现（weight-only 量化进不了 NPU 的执行单元），
#            NpuBackend.load() 必失败 → 落 cpu，notes 里说清"该装 NPU 包"。
_CANDIDATES = {
    _ENGINE_ONNX: {
        # auto / gpu 的顺序与 qwen3_ort.AUTO_ORDER 一致，也与设置页那句
        # 「按 CUDA → DirectML → OpenVINO GPU → CPU 试」一致。以前这张表漏了
        # dml，UI 却在承诺它 —— 传进来被 _normalize_request 静默归 auto，
        # 等于「选了 AMD 显卡，实际去试 CUDA」。dml 在 qwen3_ort 里早已实现
        # （DmlExecutionProvider），这里补的只是登记。
        "auto": ("cuda", "dml", "ov-gpu", "cpu"),
        # gpu 不是 cuda 的别名：它按"这台机器能用的显卡"依次试 —— NVIDIA 走 CUDA EP，
        # Intel 走 OpenVINO 原生（NPU 包里没有 CUDA 版 ORT，不这么排就会直接掉 CPU，
        # 等于把用户点的"显卡"静默作废）。没有 openvino 的老 ONNX 包会在
        # _capability 里跳过 ov-gpu，行为与从前一致。
        "gpu": ("cuda", "dml", "ov-gpu", "cpu"),
        "cuda": ("cuda", "cpu"),
        # AMD 在 Windows 上的唯一显卡路径（ROCm / MIGraphX 没有 Windows 构件）。
        "dml": ("dml", "cpu"),
        "cpu": ("cpu",),
        "ov-npu": ("ov-npu", "cpu"),
        "ov-gpu": ("ov-gpu", "cpu"),
        "ov-cpu": ("ov-cpu",),
    },
    _ENGINE_LLAMA: {
        "auto": ("gpu", "cpu"),
        "gpu": ("gpu", "cpu"),
        "cuda": ("gpu", "cpu"),
        "cpu": ("cpu",),
        "ov-npu": ("ov-npu", "cpu"),
    },
}

# 同一个意思、两种叫法。用户设置里存的值是跨包复用的：从 GGUF 包换回 ONNX 包时
# 还留着 "gpu" 就直接掉回 auto，那等于把"我明确选了显卡"静默作废。
# onnx 侧的 gpu 已经是正式键（会依次试 CUDA / OpenVINO），不再是 cuda 的别名。
# npu -> ov-npu：宿主与 qwen3_ort.BACKENDS 用的都是 ov-npu，worker 内部统一到它，
# 旧设置里存的裸 "npu" 靠这条别名接住（这条曾经断过：宿主传 ov-npu、worker 只认
# npu，选 NPU 会静默落到 auto，连"降档"都不报）。
_DEVICE_ALIAS = {
    _ENGINE_ONNX: {"npu": "ov-npu"},
    _ENGINE_LLAMA: {"npu": "ov-npu"},   # llama 侧 cuda 已经是正式键
}


def _normalize_request(request, engine):
    """宿主传的是自由文本，拼错不该让字幕整个起不来：不认识就按 auto。

    两个引擎对"显卡"的叫法不同（onnx 叫 cuda，llama 叫 gpu）。用户设置里存的值
    是跨包复用的 —— 从 ONNX 包换到 GGUF 包时还留着 "cuda"，不认就直接掉回 auto，
    等于把"我明确选了显卡"静默作废。所以这里补一层同义映射，映射不出去才 auto。
    """
    cand = _CANDIDATES[engine]
    key = (request or "").strip().lower()
    if key not in cand:
        alias = _DEVICE_ALIAS.get(engine, {}).get(key)
        if alias in cand:
            return alias
    return key if key in cand else "auto"

# --------------------------------------------------------------------------
# 语言指定（--language）
#
# 上一版（llama-server）把 language 字段**原样拼进 prompt**，所以必须给模型认识
# 的完整英文名（实测 language=Chinese 能把英文音频的标签改成 Chinese，而
# language=zh / language=Klingon 被忽略）。宿主因此先做 ISO -> 英文名映射，
# 再把英文名传进来。现在两个引擎都不接受语言输入（sherpa 没这个口子，
# llama-server 侧也没把 language 拼进 prompt），这份映射只剩两个用途：
# 日志里打得好看，以及给宿主算 matched。契约照旧保留。
#
# 语言闸门能不能真判，取决于**最终选中的后端**（不是引擎）：
#   - llama：输出恒带 "language X<asr_text>" 模板，X 就是模型自报的语言，
#     所以 reported 基本都有值 ⇒ matched 有意义（实测粤语回 cantonese）。
#   - OpenVINO（qwen3_ort.Engine，即 ov-npu / ov-gpu / ov-cpu）：同一个模板，
#     同一个抠法，实测同样回 cantonese ⇒ 闸门有效。
#   - sherpa（cpu / cuda）：result 只有 text，没有独立 language 字段，只能从正文
#     残留的前缀里抠，多数时候是空串 ⇒ matched 恒 true，闸门退化为不拦。
# 所以 ready 行的 lang-gate 由 backend.id 决定，见 main()。
# --------------------------------------------------------------------------
LANGUAGE_NAMES = {
    "zh": "Chinese", "en": "English", "yue": "Cantonese", "ja": "Japanese",
    "ko": "Korean", "fr": "French", "de": "German", "es": "Spanish",
    "ru": "Russian", "pt": "Portuguese", "it": "Italian", "ar": "Arabic",
    "th": "Thai", "vi": "Vietnamese", "id": "Indonesian", "ms": "Malay",
    "tr": "Turkish", "hi": "Hindi", "nl": "Dutch", "pl": "Polish",
    "sv": "Swedish", "da": "Danish", "fi": "Finnish", "cs": "Czech",
    "el": "Greek", "hu": "Hungarian", "ro": "Romanian", "fil": "Filipino",
    "fa": "Persian", "mk": "Macedonian",
}


def normalize_language(value):
    """ISO 代码或英文名 -> 英文名；auto/空值返回 None（表示不指定）。"""
    if not value:
        return None
    v = value.strip()
    if not v or v.lower() == "auto":
        return None
    low = v.lower()
    if low in LANGUAGE_NAMES:
        return LANGUAGE_NAMES[low]
    for name in LANGUAGE_NAMES.values():
        if name.lower() == low:
            return name
    # 未知值原样透传：日志里留下用户到底写了什么。
    return v


def same_language(reported, target):
    """模型自报的语言是否可算作目标语言。任一为空都按"匹配"处理（不乱杀）。"""
    if not reported or not target:
        return True
    a = reported.strip().lower()
    b = target.strip().lower()
    return a == b or a.startswith(b) or b.startswith(a)




def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def read_exact(stream, n):
    data = b""
    while len(data) < n:
        chunk = stream.read(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


# AVSampleFormat 枚举值（libavutil/samplefmt.h）：tap 的 meta["sampleFormat"]
# 原样携带解码器输出格式，这里必须认识全部十种（见 decode_pcm_frame）。
_AV_FMT_U8, _AV_FMT_S16, _AV_FMT_S32, _AV_FMT_FLT, _AV_FMT_DBL = 0, 1, 2, 3, 4
_AV_FMT_U8P, _AV_FMT_S16P, _AV_FMT_S32P, _AV_FMT_FLTP, _AV_FMT_DBLP = 5, 6, 7, 8, 9
# 宿主单帧元数据约 500 B；超过 64 KiB 只可能是流已错位，不再尝试读取。
_META_BYTES_MAX = 64 * 1024

_PLANAR_DTYPES = {_AV_FMT_U8P: np.uint8, _AV_FMT_S16P: np.dtype("<i2"),
                  _AV_FMT_S32P: np.dtype("<i4"), _AV_FMT_FLTP: np.dtype("<f4"),
                  _AV_FMT_DBLP: np.dtype("<f8")}
_PACKED_DTYPES = {_AV_FMT_U8: np.uint8, _AV_FMT_S16: np.dtype("<i2"),
                  _AV_FMT_S32: np.dtype("<i4"), _AV_FMT_FLT: np.dtype("<f4"),
                  _AV_FMT_DBL: np.dtype("<f8")}


def decode_pcm_frame(pcm, fmt, channels):
    """内核 tap 的一帧原始 PCM -> (帧数, 声道) 的 float32 交错矩阵。

    tap 有意送**未经任何转换的解码床**（FFF.Player.AudioTap.h §1），所以这里
    必须认识解码器可能吐出的每一种 AVSampleFormat。旧实现只认 FLTP、其余一律
    按打包 float32 重解释 —— FLAC(s32p)/PCM WAV(s16)/ALAC 那类源会整段产出
    乱码识别文本（P1，2026-11-03 审查定案）。定标判据与 C++ 侧
    UpdateInputAudioPeakLevels 是同一套，两处必须同步改。

    布局无法解释（未知格式/长度不齐/空块）时返回 None，调用方计 dropped。
    """
    dtype = _PLANAR_DTYPES.get(fmt) or _PACKED_DTYPES.get(fmt)
    if dtype is None or channels <= 0 or not pcm:
        return None
    planar = fmt >= _AV_FMT_U8P
    try:
        if planar:
            # 平面：内核按 [ch0 整平面][ch1 整平面]…背靠背写出（AsrAudioTap
            # Produce 的 planar 分支），reshape 成 (声道, 帧) 再转置成交错。
            arr = np.frombuffer(pcm, dtype=dtype).reshape(channels, -1).T
        else:
            arr = np.frombuffer(pcm, dtype=dtype).reshape(-1, channels)
    except ValueError:
        return None
    if arr.size == 0:
        return None
    if fmt in (_AV_FMT_S16, _AV_FMT_S16P):
        arr = arr.astype(np.float32) / 32768.0
    elif fmt in (_AV_FMT_S32, _AV_FMT_S32P):
        arr = arr.astype(np.float32) / 2147483648.0
    elif fmt in (_AV_FMT_U8, _AV_FMT_U8P):
        # u8 无符号、静音在中点 128。
        arr = (arr.astype(np.float32) - 128.0) / 128.0
    elif fmt in (_AV_FMT_DBL, _AV_FMT_DBLP):
        arr = arr.astype(np.float32)
    return np.ascontiguousarray(arr, dtype=np.float32)


# --------------------------------------------------------------------------
# 推理后端
#
# 三个方法就是全部接口：load() 失败一律抛 RuntimeError（带可读原因），选设备的
# 代码只认这个约定，不认具体是哪家运行时。换实现（onnxruntime 直调、TensorRT、
# OpenVINO）时改的是这里，主循环一行都不用动。
# --------------------------------------------------------------------------


class InferenceBackend:
    """唯一接口。"""

    def __init__(self, backend_id, provider, display):
        self.id = backend_id
        self.provider = provider
        self.display = display

    def load(self):
        """加载模型。失败抛 RuntimeError（原因要能直接给用户看）。"""
        raise NotImplementedError

    def transcribe(self, samples):
        """samples: float32 mono@16k -> (language, text)。"""
        raise NotImplementedError

    def close(self):
        raise NotImplementedError


class SherpaBackend(InferenceBackend):
    """sherpa-onnx 的 Qwen3-ASR 离线识别器。cpu / cuda 共用，只差 provider。"""

    def __init__(self, backend_id, provider, display, model_dir, threads):
        super().__init__(backend_id, provider, display)
        self.model_dir = Path(model_dir)
        self.threads = threads
        self._rec = None

    def load(self):
        if sherpa_onnx is None:
            raise RuntimeError(f"sherpa_onnx 不可用：{_SHERPA_IMPORT_ERROR}")
        md = self.model_dir
        try:
            conv = _pick_onnx(md, "conv_frontend")
            enc = _pick_onnx(md, "encoder")
            dec = _pick_onnx(md, "decoder")
            # 参数名以本机 2.0.1 的签名为准（已实测），别照网上别的版本改。
            self._rec = sherpa_onnx.OfflineRecognizer.from_qwen3_asr(
                conv_frontend=str(conv),
                encoder=str(enc),
                decoder=str(dec),
                tokenizer=str(md / "tokenizer"),   # 目录，内含 vocab.json / merges.txt
                num_threads=self.threads,
                sample_rate=TARGET_RATE,
                feature_dim=128,
                provider=self.provider,
                max_total_len=512,
                # 单次生成的硬上限，兜的是和当年 --n-predict 512 同一件事：没有
                # 上限时一段没语音的音频能让模型一直编到撞 ctx（实测 3808 个
                # 「啊」、27s，把整条字幕管道堵死）。128 个 token 对 5s 窗口
                # （中文 ≤ 40 字）绰绰有余。
                max_new_tokens=128,
            )
        except Exception as exc:  # noqa: BLE001 - 统一转成可读原因
            raise RuntimeError(f"加载 {self.display} 失败：{exc}") from exc
        if self._rec is None:
            raise RuntimeError(f"加载 {self.display} 失败：运行时返回空识别器")

    def transcribe(self, samples):
        if self._rec is None:
            raise RuntimeError("识别器尚未加载")
        # accept_waveform 要一维 float32 连续数组：传 list 或 float64 会被 pybind
        # 的 Sequence[SupportsFloat] 逐个转换，几万个元素慢一个量级；dtype 不对还
        # 可能静默截断。窗口本身已经是 float32，这里的拷贝主要防的是切片不连续。
        mono = np.ascontiguousarray(samples, dtype=np.float32).reshape(-1)
        stream = self._rec.create_stream()
        stream.accept_waveform(TARGET_RATE, mono)
        self._rec.decode_stream(stream)
        raw = (stream.result.text or "").strip()
        m = _ASR_TEXT_RE.match(raw)
        if m:
            return m.group(1).lower(), m.group(2).strip()
        return "", raw

    def close(self):
        # 识别器没有显式释放接口，丢掉引用等 GC 归还 onnxruntime 的 session。
        self._rec = None


# --------------------------------------------------------------------------
# llama.cpp 后端（GGUF Q8_0）
#
# 两条硬约束，违反任意一条都表现为"起了但永远没结果"，而不是报错：
#   1. 子进程输出不得混入 worker 的标准输出协议。
#   2. 子进程 stdin 必须 DEVNULL。否则父子抢同一个管道句柄，PCM 帧被
#      llama-server 吃掉一部分，worker 侧表现为"窗口一直凑不齐"。
#
# 超时为什么要 180 s：Vulkan 冷启实测 4.6–5.2 s，但 CUDA 那份预构建缺
# Ada/Blackwell 的 SASS（§29.3），5080 要靠 compute_90 的 PTX 现场 JIT，
# 冷启到能服务要 58.9 s、首句再等 76 s。这个后端的任务是"别误判成挂了"，
# 不是"快点挂"。
# --------------------------------------------------------------------------

_LLAMA_LOAD_TIMEOUT = 180.0
_LLAMA_CTX = "4096"
_NO_WINDOW = 0x08000000 if os.name == "nt" else 0

# 弱 GPU：Intel 核显 / 软件实现。核显上 Vulkan 实测比 CPU 慢 2.5 倍
# （rtf 0.458 vs 0.183），所以自动档要主动跳过它，别把"有设备"当成"有收益"。
# Arc 是独显（Xe-HPG，有 XMX），不能连坐。
_WEAK_GPU = re.compile(r"intel|uhd|iris|\bhd graphics\b|swiftshader|microsoft basic|llvmpipe",
                       re.I)


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _http_json(port, path, timeout=10.0):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


# ---- 双语对照（上行 / 下行）----------------------------------------------
# Qwen3-ASR 是纯转写权重：language 与 prompt 都改不动它的输出（实测 A/B/C 三组
# 逐字相同，D 组只把个别中文词替换成英文词 —— 那是提示词污染，不是翻译）。
# 但它的底座文本能力还在：**绕开 ASR 的 chat 模板**、用原始 /completion 续写，
# 就能做中英互译（实测 zh->en 0.13 s/句，与 ASR 交替调用互不污染）。
# 所以双语不引入第二个模型、不碰任何权重，同一个 llama-server 进程多打一个请求。
_ZH_FAMILY = {"zh", "zho", "chi", "chinese", "cantonese", "mandarin", "yue", "cmn", "中文", "粤语", "普通话"}
_EN_FAMILY = {"en", "english", "英文", "英语"}


def language_family(value):
    """把模型自报的语种名归到 zh / en；不认识的按原样返回（第三种语言）。"""
    v = (value or "").strip().lower()
    if v in _ZH_FAMILY or v.startswith(("zh-", "zh_", "yue-", "yue_")):
        return "zh"
    if v in _EN_FAMILY:
        return "en"
    return v


def bilingual_enabled(value):
    """兼容旧档位和直接调用 worker 时使用的语言简写。"""
    value = (value or "").strip().lower()
    return value in ("zh-en", "en-zh") or language_family(value) in ("zh", "en")


_QUOTE_STRIP = "\"'\u201c\u201d\u2018\u2019\u300c\u300d\u300e\u300f \t"
_TRANSLATION_MARKERS = ("translation:", "english:", "\u4e2d\u6587:", "\u8bd1\u6587:",
                        "translated:")


_HAN = r"\u3400-\u9fff\uf900-\ufaff\U00020000-\U000323af"
_EAST_ASIAN = r"\u3040-\u30ff" + _HAN
_EAST_ASIAN_WORD = re.compile(fr"[{_EAST_ASIAN}]")
_HAN_WORD = re.compile(fr"[{_HAN}]")
_KANA = re.compile(r"[\u3041-\u3096\u30a1-\u30fa\uff66-\uff9d]")
_CAPTION_WORD = re.compile(
    fr"[{_EAST_ASIAN}]|\d{{1,3}}(?:[,，]\d{{3}})+(?!\d)(?:[.．]\d+)?|\d+(?:[.．:/]\d+)+|"
    fr"[^\W_{_EAST_ASIAN}]+(?:['’\-‐‑][^\W_{_EAST_ASIAN}]+)*")
_SENTENCE_END = re.compile(r"[.!?。！？][\s\"'”’」』）)\]】]*$")
_CLAUSE_BREAK = re.compile(r"[,，、،､;；؛:：…—‒–]|\.{2,}")
_ABBREVIATIONS = {"dr", "jr", "mr", "mrs", "ms", "prof", "sr", "st", "vs"}
_TICKS_PER_SECOND = 10_000_000
_ACTIVITY_BLOCK_TICKS = _TICKS_PER_SECOND // 50
_SPEECH_PAUSE_TICKS = 2_400_000
_DIALOGUE_PAUSE_TICKS = _TICKS_PER_SECOND // 2
_MAX_SENTENCE_TICKS = 16 * _TICKS_PER_SECOND
_MAX_SENTENCE_CHARS = 320
_ALIGNMENT_WORDS = 64
_CONTEXT_TICKS = 30 * _TICKS_PER_SECOND


class _SubtitleWord(NamedTuple):
    key: str
    text: str
    start: int
    end: int
    sentence_end: bool
    clause_end: bool


class SubtitleCue(NamedTuple):
    start: int
    end: int
    language: str
    text: str
    words: tuple[_SubtitleWord, ...] = ()


def subtitle_audio_spans(samples, start, end):
    """Estimate voiced spans, keeping sustained quiet out of the caption clock."""
    block = TARGET_RATE // 50  # 20 ms
    count = len(samples) // block
    if count < 15:
        return ((start, end),)
    audio = samples[:count * block].reshape(count, block)
    rms = np.sqrt(np.mean(audio * audio, axis=1))
    peak = float(np.max(rms))
    # Adapt to a quiet noise bed, but don't turn a continuous soundtrack into
    # silence merely because parts of it are louder than others.
    floor = float(np.quantile(rms, 0.2))
    threshold = max(0.001, min(0.005, peak * 0.06), min(0.012, peak * 0.12, floor * 2.0))
    edges = np.flatnonzero(np.diff(rms >= threshold, prepend=False, append=False))
    runs = []
    for left, right in zip(edges[::2], edges[1::2]):
        if right - left < 4:  # an isolated click is not a speech onset
            continue
        if runs and left - runs[-1][1] < _SPEECH_PAUSE_TICKS // _ACTIVITY_BLOCK_TICKS:
            runs[-1] = (runs[-1][0], int(right))
        else:
            runs.append((int(left), int(right)))
    if not runs:
        return ()

    # No lead or release padding across silence. Partial blocks have at most
    # 20 ms of quantization error.
    def tick(index):
        return start + (end - start) * block * index // len(samples)

    return tuple((tick(a), end if b == count else tick(b)) for a, b in runs)


def _subtitle_words(text, start, end, spans=None):
    """The bundled recognizer supplies window times without word alignment."""
    matches = list(_CAPTION_WORD.finditer(text))
    if not matches:
        return []
    words = []
    for i, match in enumerate(matches):
        next_match = matches[i + 1] if i + 1 < len(matches) else None
        right = next_match.start() if next_match else len(text)
        punctuation = text[match.end():right]
        key = unicodedata.normalize("NFKC", match.group()).casefold().replace("’", "'")
        terminal = bool(_SENTENCE_END.search(punctuation))
        if terminal and punctuation.strip() == "." and next_match is not None:
            initial = len(key) == 1 and match.group().isupper() and key not in ("a", "i")
            terminal = not (initial or key in _ABBREVIATIONS)
        words.append(_SubtitleWord(key, text[match.start() if i else 0:right], start, end,
                                   terminal, terminal or bool(_CLAUSE_BREAK.search(punctuation))))
    if end <= start:  # translated text has no clock of its own
        return words
    if spans is None:
        spans = ((start, end),)
    spans = [(max(start, a), min(end, b)) for a, b in spans if b > start and a < end]
    if not spans:
        return []
    voiced = list(accumulate((b - a for a, b in spans), initial=0))
    weights = list(accumulate((min(8, len(word.key)) + 2 for word in words), initial=0))
    estimates = [voiced[-1] * w // weights[-1] for w in weights]

    # Fit breaks in the audio to word boundaries, preferring a nearby comma
    # or sentence end. No timestamp is allocated to the quiet between spans.
    gaps = sorted(sorted(range(1, len(spans)), key=lambda i: spans[i][0] - spans[i - 1][1],
                         reverse=True)[:len(words) - 1])
    anchors = [(0, 0)]
    for j, gap in enumerate(gaps):
        lo = anchors[-1][0] + 1
        hi = len(words) - (len(gaps) - j)
        mark = voiced[gap]
        near = [i for i in range(lo, hi + 1)
                if words[i - 1].clause_end and abs(estimates[i] - mark) <= 2_200_000]
        index = min(near or range(lo, hi + 1), key=lambda i: abs(estimates[i] - mark))
        anchors.append((index, mark))
    anchors.append((len(words), voiced[-1]))

    def media_time(offset, opening):
        # A word beginning on the boundary belongs to the *next* span; a
        # word ending there must clear at the end of the preceding span.
        i = bisect_right(voiced, offset) if opening else bisect_left(voiced, offset)
        i = min(len(spans) - 1, max(0, i - 1))
        return spans[i][0] + offset - voiced[i]

    for (first, a), (last, b) in zip(anchors, anchors[1:]):
        duration = b - a
        weight = weights[last] - weights[first]
        for i in range(first, last):
            word_start = media_time(a + duration * (weights[i] - weights[first]) // weight, True)
            word_end = media_time(a + duration * (weights[i + 1] - weights[first]) // weight, False)
            words[i] = words[i]._replace(start=word_start, end=word_end)
    return words


class SubtitleAssembler:
    """Join repeated observations before assigning a start and end to a sentence."""

    def __init__(self, overlap_ticks):
        self.overlap = overlap_ticks
        self.reset()

    def reset(self):
        self.words = []
        self.committed = 0
        self.last_end = None
        self.previous = None
        self.language = ""

    def _overlap_match(self, incoming, start, end):
        # Limit text matching to the shared portion of adjacent audio, allowing
        # for the fact that the ASR output has only estimated word times.
        slack = min(12_000_000, (end - start) // 3)
        left = len(self.words)
        while left > max(0, len(self.words) - _ALIGNMENT_WORDS) and self.words[left - 1].end >= start - slack:
            left -= 1
        previous_end = self.previous[1]
        head = [word for word in incoming[:_ALIGNMENT_WORDS] if word.start <= previous_end + slack]
        old = self.words[left:]
        matcher = SequenceMatcher(None, [word.key for word in old], [word.key for word in head], autojunk=False)
        best = None
        for i, j, length in matcher.get_matching_blocks():
            if not length:
                continue
            old_index = left + i
            if old_index + length < self.committed:
                continue
            delta = abs(old[i].start - head[j].start)
            key = old[i].key
            if length == 1 and (len(key) < 5 or j > 1 or delta > slack):
                continue
            if length == 2 and sum(len(word.key) for word in old[i:i + length]) < 3 and not (
                    all(_EAST_ASIAN_WORD.fullmatch(word.key) for word in old[i:i + length]) and delta <= slack):
                continue
            if delta > previous_end - start + slack:
                continue
            # Earlier anchors let a new observation correct more of the old
            # trailing words while retaining the established sentence start.
            score = (length, -delta, -j)
            if best is None or score > best[0]:
                best = (score, old_index, j)
        return best[1:] if best else None

    def _drain(self, cutoff, force=False):
        cues = []
        i = self.committed
        while i < len(self.words):
            length = 0
            boundary = None
            for j in range(i, len(self.words)):
                word = self.words[j]
                if not force and word.end > cutoff:
                    break
                length += len(word.text)
                reached_limit = word.end - self.words[i].start >= _MAX_SENTENCE_TICKS or length >= _MAX_SENTENCE_CHARS
                if word.sentence_end or reached_limit:
                    boundary = j + 1
                    break
            else:
                if force:
                    boundary = len(self.words)
            if boundary is None:
                break
            words = tuple(self.words[i:boundary])
            text = "".join(word.text for word in words).strip()
            start = words[0].start
            if self.last_end is not None:
                start = max(start, self.last_end)
            end = words[-1].end
            if text and start < end:
                cues.append(SubtitleCue(start, end, self.language, text, words))
                self.last_end = end
            i = boundary
        # Keep a small tail of committed words to recognise duplicate input.
        drop = max(0, i - _ALIGNMENT_WORDS)
        if drop:
            del self.words[:drop]
        self.committed = i - drop
        return cues

    def finish(self):
        return self._drain(0, force=True)

    def _join(self, incoming, old_index, new_index):
        # Closed cues are immutable. Move an anchor within a closed cue to
        # the next uncommitted word in both observations.
        committed_words = max(0, self.committed - old_index)
        old_index += committed_words
        new_index += committed_words
        if new_index >= len(incoming):
            return
        first = incoming[new_index].start
        if old_index < len(self.words):
            original = self.words[old_index]
            current = incoming[new_index]
            if current.key == original.key and old_index > self.committed:
                # Preserve case at the seam when the model capitalized the
                # beginning of a window inside an existing sentence.
                original_word = _CAPTION_WORD.search(original.text)
                current_word = _CAPTION_WORD.search(current.text)
                if original_word and current_word:
                    text = original_word.group() + current.text[current_word.end():]
                    incoming[new_index] = current._replace(text=text)
        if old_index:
            first = max(first, self.words[old_index - 1].end)
        if self.last_end is not None:
            first = max(first, self.last_end)
        original_start = incoming[new_index].start
        end = incoming[-1].end
        span = end - original_start
        if first >= end or span <= 0:
            return
        # Keep the incoming audio clock whenever it fits. In particular, don't
        # stretch words backwards into audio preceding this observation.
        self.words[old_index:] = [word._replace(
            start=first + (word.start - original_start) * (end - first) // span,
            end=first + (word.end - original_start) * (end - first) // span,
        ) for word in incoming[new_index:]]

    def accept(self, start, end, language, text, speech_spans=None):
        cutoff = end - self.overlap
        text = unicodedata.normalize("NFC", " ".join(text.split()))
        incoming = _subtitle_words(text, start, end, speech_spans)
        if not incoming:
            # Don't close an unfinished sentence on a single filtered window.
            # One more observation can still have a usable overlap or show a pause.
            if self.previous and start >= self.previous[1]:
                cues = self.finish()
                self.previous = None
                return cues
            return self._drain(cutoff)
        overlapping = self.previous is not None and self.previous[0] < start < self.previous[1]
        same_language = not language or not self.language or language_family(language) == language_family(self.language)
        anchor = self._overlap_match(incoming, start, end) if overlapping and same_language else None
        if anchor:
            self._join(incoming, *anchor)
            self.previous = (start, end)
            self.language = language or self.language
            return self._drain(cutoff)

        cues = self.finish()
        first = max(incoming[0].start, self.last_end or 0)
        if first < incoming[-1].end:
            self.words = _subtitle_words(text, first, end, speech_spans) if first > incoming[0].start else incoming
            self.committed = 0
            self.previous = (start, end)
            self.language = language
            cues.extend(self._drain(cutoff))
        return cues


def _caption_text(text, chinese):
    """Strip display punctuation only; analysis and translation keep it."""
    if chinese:
        # Don't turn a spoken decimal into a different number when removing its point.
        text = re.sub(r"(?<=\d)[.．](?=\d)", "点", text)
        text = re.sub(r"(\d+(?:[,，]\d{3})*(?:点\d+)?)[%％]", r"百分之\1", text)
    out = []
    for i, ch in enumerate(text):
        if not (unicodedata.category(ch).startswith("P") or ch in "~～<>"):
            out.append(ch)
            continue
        before, after = text[i - 1:i], text[i + 1:i + 2]
        if chinese:
            # Joined clauses still need a visual separator, without punctuation.
            if ch in "，、,；;：:。！？.!?…—" and not (before.isdecimal() and after.isdecimal()):
                out.append(" ")
        else:
            # Preserve punctuation that changes a word/number, not typography.
            if ((ch in "'’-‐‑" and before.isalnum() and after.isalnum()) or
                    (ch in ".．,，:/" and before.isdecimal() and after.isdecimal()) or
                    (ch in "%％" and before.isdecimal())):
                out.append(ch)
            elif before.isalnum() and after.isalnum():
                out.append(" ")
    return " ".join("".join(out).split())


def _caption_clauses(words, *, include_soft=False):
    """Keep a dialogue cue through brief pauses; separate sentences or long gaps."""
    first = 0
    for i in range(1, len(words)):
        before, after = words[i - 1], words[i]
        if (before.sentence_end or after.start - before.end >= _DIALOGUE_PAUSE_TICKS or
                (include_soft and before.clause_end)):
            yield words[first:i]
            first = i
    if first < len(words):
        yield words[first:]


def _caption_width(text):
    return sum(2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
               for ch in text if not unicodedata.category(ch).startswith("M"))


def _short_phrases(clause, chinese):
    """Fit a readable dialogue line; prefer commas only when the line needs a split."""
    limit = 40 if chinese else 56
    words = []
    for word in clause:
        if _caption_width(_caption_text(word.text, chinese)) <= limit:
            words.append(word)
            continue
        # Fallback for scripts with no spaces, or unusually long Latin tokens.
        text = _caption_text(word.text, chinese)
        first = width = 0
        for i, ch in enumerate(text):
            cw = _caption_width(ch)
            if cw > 0 and width + cw > limit:
                words.append(word._replace(text=text[first:i],
                    start=word.start + (word.end - word.start) * first // len(text),
                    end=word.start + (word.end - word.start) * i // len(text)))
                first, width = i, 0
            width += cw
        words.append(word._replace(text=text[first:], start=word.start + (word.end - word.start) * first // len(text)))

    first = 0
    while first < len(words):
        end, soft_end = first, None
        while end < len(words):
            text = _caption_text("".join(word.text for word in words[first:end + 1]), chinese)
            width = _caption_width(text)
            if end > first and width > limit:
                break
            end += 1
            if words[end - 1].clause_end and width >= limit // 2:
                soft_end = end
        if end < len(words) and soft_end is not None:
            end = soft_end
        yield words[first:end]
        first = end


def _caption_pieces(clauses, chinese):
    return [(words, _caption_text("".join(word.text for word in words), chinese))
            for clause in clauses for words in _short_phrases(clause, chinese)]


def _pair_caption_pieces(source, translated):
    if not translated:
        yield from ((words[0].start, words[-1].end, text, "") for words, text in source)
        return
    for i, (words, original) in enumerate(source):
        # Keep the ordering of both sides. A shorter side can remain on screen
        # for multiple short phrases, but cannot move their onset times.
        first = i * len(translated) // len(source)
        last = max(first + 1, (i + 1) * len(translated) // len(source))
        glosses = translated[first:last]
        if len(glosses) <= len(words):
            groups = [words[k * len(words) // len(glosses):(k + 1) * len(words) // len(glosses)]
                      for k in range(len(glosses))]
            intervals = [(group[0].start, group[-1].end) for group in groups]
        else:
            # A single short source word can translate into several words.
            a, b = words[0].start, words[-1].end
            edges = [a + (b - a) * k // len(glosses) for k in range(len(glosses) + 1)]
            intervals = zip(edges, edges[1:])
        for (a, b), (_, translation) in zip(intervals, glosses):
            yield a, b, translation, original


def looks_like_isolated_character(text):
    # Music and sound effects often make the ASR model emit one isolated Han
    # character followed by punctuation. Do not strip it from real sentences.
    content = [ch for ch in text if ch.isalnum()]
    return len(content) == 1 and unicodedata.name(content[0], "").startswith("CJK ")


def clean_translation(text):
    """译文常带引号、译名标签或复述指令。只做保守清洗，不猜内容。"""
    s = (text or "").strip()
    low = s.lower()
    for marker in _TRANSLATION_MARKERS:
        if low.startswith(marker):
            s = s[len(marker):].strip()
            break
    s = s.split("\n")[0].strip(_QUOTE_STRIP)
    return s


def translation_prompt(text, target, history=()):
    language, label = ("简体中文", "中文") if target == "zh" else ("英文", "English")
    request = f"把下面这句译成{language}，保留自然的分句标点，只输出译文："
    if not history:
        return f"{request}\n{text}\n{label}:"
    lines = [f"前文只用于理解名字、词义和指代。{request}", "前文对白（参考）："]
    for source, translated in history:
        lines.append(f"原文：{source}")
        if translated:
            lines.append(f"{label}：{translated}")
    return "\n".join([*lines, f"当前原文：{text}", f"{label}:"])


def is_context_echo(translation, text, history):
    def letters(value):
        return "".join(ch for ch in value.casefold() if ch.isalnum())

    current = letters(text)
    result = letters(translation)
    for source, gloss in history:
        old = letters(gloss)
        if not old or not current or current == letters(source):
            continue
        if old == result:
            return True
        # Allow shared names and terms, but reject a substantial passage copied
        # from another line, even when the model has changed its first words.
        if len(old) < 12 or len(result) < 12:
            continue
        reused = SequenceMatcher(None, old, result, autojunk=False).find_longest_match().size
        if reused >= max(12, int(len(result) * 0.6)):
            return True
    return False


class SubtitleTranslator:
    """Translate complete sentences, then schedule dialogue cues for display."""

    def __init__(self, backend, chinese=False, bilingual=False):
        self.backend = backend
        self.chinese = chinese
        self.bilingual = bilingual
        self.history = deque(maxlen=3)

    def reset(self):
        self.history.clear()

    def phrases(self, cue):
        source = cue.text
        translated = ""
        target = ""
        family = language_family(cue.language)
        if family in ("", "und", "unknown") and not _KANA.search(source):
            # Some backends don't return a language tag. Avoid translating Han
            # text to itself, without treating mixed Kana/Han Japanese as Chinese.
            han_count = len(_HAN_WORD.findall(source))
            if han_count and han_count * 2 >= sum(ch.isalnum() for ch in source):
                family = "zh"
        if self.chinese:
            if family != "zh":
                target = "zh"
        elif self.bilingual:
            target = "en" if family == "zh" else "zh"
        while self.history and cue.start - self.history[0][0] > _CONTEXT_TICKS:
            self.history.popleft()
        if target:
            context = tuple((line, gloss if language == target else "") for _, line, gloss, language in self.history)
            translated = self.backend.translate(source, target, context)
            if translated and translated.casefold() == source.casefold():
                translated = ""
        if self.chinese or self.bilingual:
            self.history.append((cue.end, source[-160:], translated[-120:], target))
        originals = list(_caption_clauses(cue.words or _subtitle_words(source, cue.start, cue.end)))
        translated_words = _subtitle_words(translated, 0, 0)
        groups = [(originals, [translated_words] if translated_words else [])]
        if translated_words and len(originals) > 1:
            # Translation punctuation can match a pause already found in the
            # source, but should not create additional display transitions.
            glosses = list(_caption_clauses(translated_words, include_soft=True))
            if len(glosses) == len(originals):
                groups = [([a], [b]) for a, b in zip(originals, glosses)]
        for original, gloss in groups:
            pieces = _caption_pieces(original, family == "zh")
            translations = _caption_pieces(gloss, target == "zh")
            for a, b, text, text2 in _pair_caption_pieces(pieces, translations):
                a, b = max(a, cue.start), min(b, cue.end)
                if a < b and text:
                    yield {"start100ns": a, "end100ns": b, "text": text, "text2": text2}


def _post_wav(port, wav_bytes, model_id, timeout=600.0):
    """multipart 手拼字节。不为一个 POST 引入 requests —— 包里只有 numpy。"""
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"
            f"{model_id}\r\n").encode("utf-8")
    body += (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
             f"filename=\"a.wav\"\r\nContent-Type: audio/wav\r\n\r\n").encode("utf-8")
    body += wav_bytes + b"\r\n" + f"--{boundary}--\r\n".encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/audio/transcriptions", data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def _post_json(port, path, payload, timeout=180.0):
    """llama-server 的 JSON 端点。翻译走 /completion（裸续写），不走 chat 模板。"""
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}", data=body,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def _wav_bytes(samples):
    """float32 mono@16k -> 16 bit PCM 的 wav 字节。llama-server 只吃容器文件。"""
    pcm = np.clip(np.asarray(samples, dtype=np.float32).reshape(-1), -1.0, 1.0)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(TARGET_RATE)
        w.writeframes((pcm * 32767.0).astype("<i2").tobytes())
    return buf.getvalue()


class LlamaBackend(InferenceBackend):
    """llama.cpp 的 llama-server 子进程（GGUF Q8_0）。cpu / gpu 共用一份二进制。"""

    def __init__(self, backend_id, display, model_dir, runtime_dir, threads, want_gpu):
        super().__init__(backend_id, "", display)
        self.model_dir = Path(model_dir)
        self.runtime_dir = Path(runtime_dir) if runtime_dir else None
        self.threads = threads
        self.want_gpu = want_gpu
        self._proc = None
        self._port = 0
        self._model_id = ""

    # ---- 启动前 -----------------------------------------------------------
    def _exe(self):
        if self.runtime_dir is None:
            raise RuntimeError("清单里没有写 runtime 目录（llama 引擎需要 runtime/vulkan）")
        exe = self.runtime_dir / "llama-server.exe"
        if not exe.is_file():
            raise RuntimeError(f"runtime 目录里没有 llama-server.exe：{self.runtime_dir}")
        return exe

    @staticmethod
    def _list_devices(exe):
        try:
            r = subprocess.run([exe, "--list-devices"], capture_output=True, text=True,
                               timeout=90, cwd=str(Path(exe).parent),
                               creationflags=_NO_WINDOW)
        except Exception:
            return []
        out = []
        for ln in (r.stdout or "").splitlines():
            m = re.match(r"\s*([A-Za-z]+\d+)\s*:\s*(.+?)\s*\((\d+)\s*MiB", ln)
            if m:
                out.append({"idx": m.group(1), "name": m.group(2),
                            "vram_mb": int(m.group(3))})
        return out

    def _pick_device(self, exe):
        """返回 (device_idx, 说明)。自动档跳过核显；没有可用的就回 CPU。"""
        devs = self._list_devices(exe)
        if not devs:
            return "", "没有可用的 GPU 设备，退回 CPU"
        strong = [d for d in devs if not (_WEAK_GPU.search(d["name"])
                                          and "arc" not in d["name"].lower())]
        if not strong:
            names = "、".join(d["name"] for d in devs)
            return "", f"只看到核显/软件设备（{names}），退回 CPU（核显实测比 CPU 慢 2.5 倍）"
        best = max(strong, key=lambda d: d["vram_mb"])
        return best["idx"], f"{best['idx']} {best['name']}"

    # ---- 生命周期 ---------------------------------------------------------
    def load(self):
        exe = self._exe()
        main, mmproj = _llama_weights(self.model_dir)

        device = ""
        if self.want_gpu:
            device, _ = self._pick_device(exe)

        self._port = _free_port()
        cmd = [str(exe), "-m", str(main), "--mmproj", str(mmproj),
               "--host", "127.0.0.1", "--port", str(self._port),
               "--ctx-size", _LLAMA_CTX, "--threads", str(self.threads),
               "--n-gpu-layers", "999" if device else "0",
               # 贪心解码：温度 > 0 时中文会退化成复述指令
               # （实测 --temp 0.6 输出 "Transcribe audio to text."）。
               "--temp", "0", "--n-predict", "512", "--cache-ram", "0"]
        if device:
            cmd += ["--device", device]
        try:
            self._proc = subprocess.Popen(
                cmd, cwd=str(exe.parent), stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=_NO_WINDOW)
        except Exception as exc:  # noqa: BLE001 - 起不来就是这一档不可用
            self.close()
            raise RuntimeError(f"启动 llama-server 失败：{exc}") from exc

        self._wait_ready()
        self.provider = device or "cpu"
        self.display = "GPU（Vulkan）" if device else "CPU"

    def _wait_ready(self):
        t0 = time.time()
        while time.time() - t0 < _LLAMA_LOAD_TIMEOUT:
            if self._proc.poll() is not None:
                raise RuntimeError(
                    f"llama-server 启动即退出（rc={self._proc.returncode}）")
            try:
                if _http_json(self._port, "/health", timeout=5).get("status") == "ok":
                    self._model_id = _http_json(self._port, "/v1/models")["data"][0]["id"]
                    return
            except Exception:
                pass
            time.sleep(0.3)
        raise RuntimeError(
            f"llama-server 在 {_LLAMA_LOAD_TIMEOUT:.0f}s 内没有就绪")

    def transcribe(self, samples):
        if self._proc is None:
            raise RuntimeError("llama-server 尚未启动")
        if self._proc.poll() is not None:
            raise RuntimeError(
                f"llama-server 已退出（rc={self._proc.returncode}）")
        try:
            out = _post_wav(self._port, _wav_bytes(samples), self._model_id)
        except Exception as exc:  # noqa: BLE001 - 这一窗失败，别拖垮整条管道
            raise RuntimeError(f"转写请求失败：{exc}") from exc
        raw = (out.get("text") or "").strip()
        # 输出形如 "language Chinese<asr_text>正文"。模板必须剥掉 ——
        # 原样上屏就是把 "language Chinese<asr_text>" 打进字幕。
        # 没有模板时把整串交回去，交给上面的泄漏过滤判定，而不是在这里猜。
        m = _ASR_TEXT_RE.match(raw)
        if m:
            return m.group(1).lower(), m.group(2).strip()
        return "", raw

    # ---- 双语 ----
    def translate(self, text, target, history=()):
        """把 text 译成 target（zh / en）。

        必须走原始 /completion：/v1/chat/completions 与 /v1/audio/transcriptions
        都会被 ASR 的 chat 模板包住，模型只会往回吐 `language X<asr_text>` 加原文
        （实测三种 chat 写法全部如此）。裸续写才摸得到底座的文本能力。

        失败返回空串：副行留白比错译好，更不能因为翻译挂了把这一窗字幕整条丢掉。
        """
        text = (text or "").strip()
        if not text or self._proc is None or self._proc.poll() is not None:
            return ""
        if target not in ("en", "zh"):
            return ""
        contexts = (history, ()) if history else ((),)
        for context in contexts:
            prompt = translation_prompt(text, target, context)
            try:
                out = _post_json(self._port, "/completion", {
                    "prompt": prompt, "temperature": 0, "n_predict": min(192, max(24, len(text) * 2)),
                    "stream": False, "cache_prompt": False,
                    "stop": ["\n", "<|im_end|>"]})
            except Exception:  # Translation failure leaves the source line usable.
                return ""
            translation = clean_translation(out.get("content") or "")
            if (not translation or out.get("stop_type") == "limit" or
                    looks_like_prompt_leak(translation) or looks_degenerate(translation)):
                continue
            if context and translation.startswith(("前文", "当前原文", "待翻译", "请将", "请把")):
                # The completion echoed the instruction instead of clearing the cue.
                continue
            if context and len(translation) > max(24, len(text) * (1.25 if target == "zh" else 5) + 12):
                continue
            if target == "zh":
                if not any(unicodedata.name(ch, "").startswith("CJK ") for ch in translation):
                    continue
            if is_context_echo(translation, text, context):
                continue
            return translation
        return ""

    def close(self):
        if self._proc is not None:
            try:
                subprocess.run(["taskkill", "/PID", str(self._proc.pid), "/T", "/F"],
                               capture_output=True, timeout=30, creationflags=_NO_WINDOW)
            except Exception:
                try:
                    self._proc.kill()
                except Exception:
                    pass
            try:
                self._proc.wait(timeout=10)
            except Exception:
                pass
            self._proc = None


# --------------------------------------------------------------------------
# OpenVINO 原生后端（NPU / Intel GPU / Intel CPU）
#
# 为什么 NPU 只能走这条：encoder/decoder 是 QOperator 动态量化
# （DynamicQuantizeLinear + MatMulInteger），GGUF 那条腿是 block-wise weight-only，
# 数学上不等价，到不了 NPU；onnxruntime 的 OpenVINO EP 又和 2025.x/2026.x 报
# Error 127（符号缺失），只有 openvino 原生的 read_model -> compile_model 通。
#
# 三段各挂各的设备这件事复用同目录的 qwen3_ort.py（**模型一个字节都不改**）：
#   npu    : conv -> NPU，      int8 两段 -> CPU
#   ov-gpu : conv -> Intel GPU，int8 两段 -> CPU
#   ov-cpu : 三段都在 OpenVINO CPU
# int8 两段必须留 CPU 不是保守，是实测：OpenVINO GPU 跑 encoder.int8 出 NaN 且
# **不抛异常**，只有数值探针能挡；NPU 的 op 集比 GPU 更窄，更没有理由放上去。
#
# 与 sherpa 路线的取舍：sherpa 一个 recognizer 只吃一个 provider，做不到"conv 上
# 加速器、int8 留 CPU"，而且它自带的是纯 CPU 版 onnxruntime。所以 NPU 包用这条，
# 代价是 CPU 侧解码比 sherpa 慢（每 token 一次 session.run），换来的是真能用 NPU。
# --------------------------------------------------------------------------

_OV_TARGETS = {
    "ov-npu": ("NPU", "NPU（Intel Core Ultra）"),
    "ov-gpu": ("GPU", "Intel GPU（OpenVINO）"),
    "ov-cpu": ("CPU", "CPU（OpenVINO）"),
}


def _ov_device_name(core, dev):
    """读设备的真实名字，读不到返回空串（OpenVINO 允许这个属性查不出来）。"""
    try:
        return str(core.get_property(dev, "FULL_DEVICE_NAME") or "")
    except Exception:  # noqa: BLE001
        return ""


def _pick_intel_gpu(core, devices):
    """在 GPU.x 里挑 Intel 那一块，返回具体设备名；判定不了返回 None。

    为什么要挑：OpenVINO 会把 N 卡 / A 卡也列进 GPU.x（第三方实测那台机器就是
    GPU.0=Intel Arc 140T、GPU.1=RTX 5060 Laptop），而设备名 "GPU" 默认取第一块。
    不钉的话"选 Intel 显卡"可能编到 NVIDIA 上，而且**不报错** —— 编译照样成功、
    conv 照样算出数，正是要防的那种假绿。
    """
    gpus = [d for d in devices if d.upper().startswith("GPU")]
    if not gpus:
        return None
    names = {d: _ov_device_name(core, d) for d in gpus}
    for d in gpus:
        if "intel" in names[d].lower():
            return d
    if any(names.values()):
        # 名字都读得到，却没有一块是 Intel → 明确失败，别去编别家的卡。
        raise RuntimeError(
            "本机没有 Intel 显卡（OpenVINO 看到 "
            + "、".join(f"{d}={names[d]}" for d in gpus)
            + "）；选 Intel 显卡会编到别家设备上，拒绝继续")
    return None


class OvBackend(InferenceBackend):
    """OpenVINO 原生三段推理。目标设备不存在时**明确失败**，不假装跑在加速器上。"""

    def __init__(self, backend_id, model_dir, threads):
        target, display = _OV_TARGETS[backend_id]
        super().__init__(backend_id, target, display)
        self.model_dir = str(model_dir)
        self.threads = threads
        self._engine = None
        self._qv = None
        self.stages = {}

    def load(self):
        try:
            import qwen3_ort as qv
        except Exception as exc:  # noqa: BLE001 - 缺包就是不能用，说清缺什么
            raise RuntimeError(
                f"NPU 包需要同目录的 qwen3_ort.py 与 openvino（{exc}）") from exc
        self._qv = qv

        try:
            _, core = qv.openvino_core()
            devices = list(core.available_devices)
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(f"OpenVINO 不可用：{exc}") from exc

        # 目标设备必须真的在枚举里。compile_model("NPU") 在没有 NPU 的机器上会抛，
        # 但抛出来的是一串插件路径错误，不如在这里给出"本机有什么、缺什么"。
        want = self.provider
        have = [d for d in devices if d == want or d.startswith(want + ".")]
        if not have:
            raise RuntimeError(
                f"本机没有 {want} 设备（OpenVINO 只看到 {'、'.join(devices) or '无'}）")
        # 只有 GPU 家族需要挑：NPU / CPU 不存在"同族不同厂商"的问题。
        ov_dev = _pick_intel_gpu(core, have) if want.upper() == "GPU" else None

        # NPU 编译器只吃"有上界"的形状，conv_frontend 的帧维是 INT64_MAX，直接编
        # 会报 Upper bounds are not specified。给上界就能过（离线实测 NPU4000：
        # 500 / 3000 / [1,1..3000] 三种都成功），而 reshape 只改内存元数据，
        # onnx 文件一个字节不动。上界取 32 s：宿主窗口上限就是 30 s，留两成余量。
        bounded = 3200 if self.provider.upper() == "NPU" else None
        # 编译缓存：NPU 首次编译几十秒（实测离线 20 s，真机带编译器初始化更久），
        # 第二次必须命中，否则每次开字幕都卡一次。只给 NPU 设，免得 OpenVINO
        # 在 GPU 路径上多打一行 CACHE_DIR 警告。
        if bounded:
            cache = Path(__file__).resolve().parent / "runtime" / "npu-cache"
            try:
                cache.parent.mkdir(parents=True, exist_ok=True)
                os.environ.setdefault("OV_CACHE_DIR", str(cache))
            except OSError:
                pass
        try:
            self._engine = qv.Engine(self.model_dir, device=self.id,
                                     threads=self.threads,
                                     conv_bounded_frames=bounded,
                                     ov_device=ov_dev)
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(f"OpenVINO 三段编译失败：{exc}") from exc

        self.stages = {k: v["bound"]
                       for k, v in self._engine.device_report().items()
                       if isinstance(v, dict)}
        # 只验上加速器的那一段：int8 两段本来就在 CPU，与参考同设备，比值恒 0，
        # 白编译一遍 decoder.int8（720 MB）要几十秒。
        accel = [n for n, b in (("conv", self._engine.conv_backend),
                                ("encoder", self._engine.enc_backend),
                                ("decoder", self._engine.dec_backend))
                 if b.kind == "ov" and b.target != "CPU"]
        if accel:
            try:
                rep = self._engine.verify(stages=accel)
            except Exception as exc:  # noqa: BLE001
                raise RuntimeError(f"数值门禁跑不起来：{exc}") from exc
            bad = [n for n, r in rep.items() if not r["ok"]]
            if bad:
                detail = "，".join(
                    f"{n} 偏差 {rep[n]['rel_err']}（上限 {rep[n]['limit']}）"
                    f"{'/含 NaN' if rep[n]['has_nan'] else ''}" for n in bad)
                raise RuntimeError(
                    f"{self.provider} 上算错了：{detail}。这是「不报错但算错」的"
                    f"那类假绿，拒绝继续（int8 段请留 CPU）")

    def transcribe(self, samples):
        if self._engine is None:
            raise RuntimeError("OvBackend 尚未加载")
        # pad30=False 是实测出来的，不是省事：
        #   pad30=True  390 audio token、rtf 1.81、文本「去香」
        #   pad30=False  78 audio token、rtf 0.96、文本「喺香」，CER 0.029 vs 0.059
        # 补到 30 s 的代价是 decoder 要多跑 5 倍 token，而尾部那点信息照样被 5 s
        # 窗口切掉 —— 花的钱没买到字。宿主是滑动窗口，每窗本来就短，按真实长度算
        # 才是对的。（qwen3_ort.py 的 CLI 默认 True，是给整段离线转写用的。）
        r = self._engine.transcribe(np.ascontiguousarray(samples, dtype=np.float32),
                                    pad30=False)
        lang = (r.get("language") or "").strip().lower()
        return lang, (r.get("text") or "").strip()

    def close(self):
        # OpenVINO 的 CompiledModel 没有显式释放接口，丢引用等 GC。
        self._engine = None


class NpuBackend(InferenceBackend):
    """GGUF 包里没有 NPU 路径（block-wise weight-only 进不了 NPU 的执行单元）。

    真正能跑 NPU 的后端是 OvBackend，它只在 NPU 包里（ONNX 三件套 + OpenVINO）。
    这个类留着是为了在**默认包**里选中 NPU 时给出可执行的下一步，而不是笼统的
    "设备不可用"。
    """

    def __init__(self):
        super().__init__("ov-npu", "", "NPU")

    def load(self):
        raise RuntimeError(
            "这个包是 GGUF 引擎，没有 NPU 路径；NPU 要装独立的 NPU 包"
            "（ONNX 三件套 + OpenVINO）")

    def transcribe(self, samples):
        raise RuntimeError("NPU 后端不可用")

    def close(self):
        pass


# sherpa-onnx 的 provider 是 pip 包形态决定的，不是运行时选项：
#   纯 CPU 版 wheel 只带一个 17 MB 的 onnxruntime.dll；
#   `sherpa-onnx==<版本>+cuda` 才额外带 onnxruntime_providers_cuda.dll。
_CUDA_EP_DLL = "onnxruntime_providers_cuda.dll"
_NV_DRIVER_DLL = "nvcuda.dll"

# onnxruntime_providers_cuda.dll 的静态导入（从 PE 导入表读出，不是猜的）。
# 缺任何一个都会让 LoadLibrary 失败，而 sherpa 只打一行日志就静默回落 CPU；
# 金样本自检照样通过（CPU 算得对），所以必须逐项挡。实测本机只查前两项时，
# 缺 cufft64_11.dll 就出现过"报 cuda、实跑 CPU、自检绿"的情况。
_CUDA_RUNTIME_DLLS = (
    "cublas64_12.dll",
    "cublasLt64_12.dll",
    "cudart64_12.dll",
    "cudnn64_9.dll",
    "cufft64_11.dll",
)


def _find_dll(name, pkg):
    """按 Windows 的搜索顺序找运行库：包内 -> System32 -> PATH -> CUDA_PATH。"""
    root = os.environ.get("SystemRoot") or r"C:\Windows"
    for d in (Path(pkg), Path(root) / "System32"):
        if (d / name).exists():
            return str(d / name)
    for part in os.environ.get("PATH", "").split(os.pathsep):
        if part and (Path(part) / name).exists():
            return str(Path(part) / name)
    cuda = os.environ.get("CUDA_PATH")
    if cuda and (Path(cuda) / "bin" / name).exists():
        return str(Path(cuda) / "bin" / name)
    return ""


def _has_nv_driver():
    root = os.environ.get("SystemRoot") or r"C:\Windows"
    return (Path(root) / "System32" / _NV_DRIVER_DLL).exists()


def _cuda_ready():
    """cuda 档能不能真的用上，返回 (ok, 原因)。

    **为什么必须静态查**：sherpa-onnx 在 provider 不可用时不抛异常，只打一行
    C++ 日志然后照常返回。那句日志走 Windows 控制台句柄而不是 fd 2（Python
    重定向实测抓到 0 字节），识别器对象也不暴露 provider。所以不提前挡，
    就会出现"报 cuda、实跑 CPU、金样本还照样通过"——CPU 算得对，自检挡不住
    降档。

    三道检查对应 pip 包的形态与系统前提：
      * 包内 lib/ 下有没有 onnxruntime_providers_cuda.dll —— 没有就是纯 CPU 版；
      * 系统有没有 NVIDIA 驱动（System32 下的 nvcuda.dll）—— 有 CUDA 版 ORT 但没驱动
        时 ORT 一样回落；
      * 该 EP DLL 的全部静态依赖（cublas/cublasLt/cudart/cudnn/cufft 12.x）是否就位
        —— LoadLibrary 少一个就整体失败，而 sherpa 只打一行日志。
    两道都过之后仍可能因驱动/显卡算力过低回落，这点查不出来；调用方据此把
    结论标成"静态确认"而不是"运行时确认"。
    """
    try:
        import sherpa_onnx
    except Exception as exc:  # noqa: BLE001 - 装不上就是不能用
        return False, f"import sherpa_onnx 失败：{exc}"
    pkg = Path(sherpa_onnx.__file__).parent
    dlls = list(pkg.rglob("*.dll"))
    names = {f.name.lower() for f in dlls}
    if _CUDA_EP_DLL.lower() not in names:
        cpu_ort = next((f.stat().st_size for f in dlls
                        if f.name.lower() == "onnxruntime.dll"), 0)
        return False, (
            f"sherpa-onnx 是纯 CPU 版（包内无 {_CUDA_EP_DLL}，"
            f"随包 onnxruntime.dll 仅 {cpu_ort / 1048576:.1f} MB）；"
            f"要 CUDA 请装 sherpa-onnx==<版本>+cuda")
    if not _has_nv_driver():
        return False, (f"包内有 {_CUDA_EP_DLL} 但系统没有 NVIDIA 驱动"
                       f"（System32 缺 {_NV_DRIVER_DLL}），跑不起来")
    missing = [n for n in _CUDA_RUNTIME_DLLS if not _find_dll(n, pkg)]
    if missing:
        return False, (
            f"包内有 {_CUDA_EP_DLL}，但缺 CUDA 12 运行库：{', '.join(missing)}；"
            f"缺任意一个 ORT 都加载不了 CUDA EP（会静默回落 CPU）。"
            f"pip install nvidia-cublas-cu12 nvidia-cudnn-cu12 nvidia-cufft-cu12 "
            f"nvidia-cuda-runtime-cu12，或装对应 CUDA 12 版本运行时")
    return True, ""


def _capability(backend_id, engine, runtime_dir):
    """该档在当前环境能不能用。cpu 恒可用；npu / ov-* 在这里挡"包不完整"。

    llama 的 gpu 档只静态确认「运行时在不在」——有没有显卡、是不是核显，由
    LlamaBackend.load() 里 --list-devices 现查，查不到就退回 CPU（那是设计行为，
    不是降档失败）。所以这里不重复判显卡，只挡"包不完整"。

    OpenVINO 三档同理：这里只确认运行库和推理实现都在，具体设备枚举留给
    OvBackend.load()——它拿得到 available_devices，能报出"本机有哪些、缺哪个"。
    """
    if engine == _ENGINE_LLAMA and backend_id == "gpu":
        if not runtime_dir:
            return False, "清单里没有 runtime 目录"
        if not (Path(runtime_dir) / "llama-server.exe").is_file():
            return False, f"runtime 目录里没有 llama-server.exe：{runtime_dir}"
        return True, ""
    if backend_id in _OV_TARGETS:
        if engine != _ENGINE_ONNX:
            # GGUF 包没有 OpenVINO 路径，这里放行让 NpuBackend.load() 去说
            # "该装 NPU 包"——报"缺 qwen3_ort.py"会让用户以为是文件丢了。
            return True, ""
        try:
            import openvino  # noqa: F401
        except Exception as exc:  # noqa: BLE001
            return False, (f"没有 openvino（{exc}）；"
                           f"NPU / Intel GPU 只有 OpenVINO 原生这条路")
        if not Path(__file__).with_name("qwen3_ort.py").is_file():
            return False, "worker 同级目录缺 qwen3_ort.py（三段 OpenVINO 推理实现）"
        return True, ""
    if backend_id == "cuda":
        return _cuda_ready()
    if backend_id == "dml":
        # DirectML 走 qwen3_ort 的 DmlExecutionProvider。装的是 onnxruntime 的
        # 哪个变体（cpu / gpu / directml）要 import 之后才知道，所以模块缺失不
        # 在这里挡 —— 交给 load() 抛异常，由 _select_backend 记成一条明确的
        # 降档说明，而不是悄悄退化成 auto。
        if not Path(__file__).with_name("qwen3_ort.py").is_file():
            return False, "worker 同级目录缺 qwen3_ort.py（DirectML 推理实现）"
        return True, ""
    return True, ""

def _provider_note(backend_id, engine):
    """provider 值的确信程度，跟着 JSON 一起给宿主，别让人以为查过运行时。"""
    if engine == _ENGINE_LLAMA and backend_id == "gpu":
        return "provider 由 llama-server --list-devices 现选"
    if backend_id in _OV_TARGETS:
        return "provider 由 OpenVINO 编译后读 FULL_DEVICE_NAME 回读"
    if backend_id == "cuda":
        return "provider 静态确认，无运行时校验接口"
    return "cpu 无需确认"

def _make_backend(backend_id, engine, model_dir, runtime_dir, threads):
    if backend_id == "ov-npu":
        # onnx 引擎的 NPU 是真后端（OpenVINO），llama 引擎没有这条路。
        if engine == _ENGINE_ONNX:
            return OvBackend(backend_id, model_dir, threads)
        return NpuBackend()
    if backend_id in _OV_TARGETS:
        return OvBackend(backend_id, model_dir, threads)
    provider, display = _BACKENDS[backend_id]
    if engine == _ENGINE_LLAMA:
        return LlamaBackend(backend_id, display, model_dir, runtime_dir, threads,
                            want_gpu=(backend_id == "gpu"))
    return SherpaBackend(backend_id, provider, display, model_dir, threads)


# --------------------------------------------------------------------------
# 声道下混
#
# v1 是"所有声道平均"，问题在于 5.1/7.1 影视的对白单独放在 FRONT_CENTER 上，
# 平均之后被配乐和环境声稀释，LFE 更是纯低频污染。块头已经带了每个槽位的
# AVChannel 值，所以这里按声道位置挑，而不是盲目平均：
#   * 有 FC  -> FC 主导 + 少量 L/R（保留一点空间感），归一化防削波
#   * 无 FC  -> L/R 各半（立体声的常规做法，结果与旧的平均等价）
#   * 什么都没有（布局 UNSPEC）-> 除 LFE 外逐列平均
# --------------------------------------------------------------------------

AV_FRONT_LEFT = 0
AV_FRONT_RIGHT = 1
AV_FRONT_CENTER = 2
AV_LOW_FREQUENCY = 3

# planarity 只用于解交织：内核按 size 逐 plane 写，交错格式才按帧排。
AV_SAMPLE_FMT_FLT = 3
AV_SAMPLE_FMT_FLTP = 8


def _channel_index(channels, positions):
    """{AVChannel: column}。位置表缺失/-1 的槽位不入表（也就不会被挑中）。"""
    index = {}
    for column, channel in enumerate(positions or ()):
        if column >= channels:
            break
        if channel is not None and channel >= 0 and channel not in index:
            index[int(channel)] = column
    return index


def _downmix(raw, channels, positions):
    """raw: float32 [frames, channels] -> float32 [frames]。"""
    if channels <= 1:
        return raw[:, 0]
    index = _channel_index(channels, positions)
    if not index:
        return raw.mean(axis=1)

    centre = index.get(AV_FRONT_CENTER)
    left = index.get(AV_FRONT_LEFT)
    right = index.get(AV_FRONT_RIGHT)

    if centre is not None and left is not None and right is not None:
        # 1.8 = 1.0 + 0.4 + 0.4，保证与原始电平同量级（PCM16 前不削波）。
        return (raw[:, centre] + 0.4 * raw[:, left] + 0.4 * raw[:, right]) / 1.8
    if centre is not None:
        return raw[:, centre]
    if left is not None and right is not None:
        return 0.5 * raw[:, left] + 0.5 * raw[:, right]

    keep = [c for ch, c in index.items() if ch != AV_LOW_FREQUENCY]
    return raw[:, keep or list(index.values())].mean(axis=1)


def _resample_to_16k(mono, rate):
    """任意采样率 -> 16 kHz。soxr 在时装（快且带抗混叠），否则 np.interp 退化。"""
    if rate == TARGET_RATE or len(mono) == 0:
        return np.asarray(mono, dtype=np.float32)
    try:
        # 函数内 import：soxr 是可选加速项，装在不在都要能跑，且不该让本模块
        # 的导入因为缺它而失败。
        import soxr
    except ImportError:
        n = int(round(len(mono) * TARGET_RATE / rate))
        if n <= 0:
            return np.zeros(0, dtype=np.float32)
        return np.interp(
            np.linspace(0, len(mono) - 1, n),
            np.arange(len(mono)), mono).astype(np.float32)
    return np.asarray(soxr.resample(mono, rate, TARGET_RATE), dtype=np.float32)


class WindowBuilder:
    """Media-time windowing over tap frames (section 6.2-6.4)."""

    MAX_BUFFER_BYTES = 256 * 1024 * 1024
    MAX_BUFFER_BLOCKS = 4096

    def __init__(self, window_s, step_s, target_rate=TARGET_RATE):
        self.window_s = window_s
        self.step_s = step_s
        self.target_rate = target_rate
        self.buf = []            # list of (position100ns, float32 mono @ src rate)
        self.window_start = None  # position100ns or None
        self.last_rate = None
        self.last_channels = None
        self.last_positions = None
        self.buffered_bytes = 0
        self.covered_until = None
        self.revision = 0

    def reset_generation(self):
        self.revision += 1
        self.buf.clear()
        self.window_start = None
        self.buffered_bytes = 0
        self.covered_until = None

    def add(self, position100ns, samples, rate, channels, positions=None):
        """samples: float32 [frames, channels], de-interleaved (row = frame)."""
        size = samples.nbytes
        if size > self.MAX_BUFFER_BYTES:
            self.reset_generation()
            return
        if self.buf:
            last_position, last_samples = self.buf[-1]
            last_end = last_position + round(last_samples.shape[0] * _TICKS_PER_SECOND / self.last_rate)
            if (position100ns < last_position or position100ns - last_end > _TICKS_PER_SECOND // 2 or
                    self.last_rate != rate or self.last_channels != channels or self.last_positions != positions):
                self.reset_generation()
        if (len(self.buf) >= self.MAX_BUFFER_BLOCKS or
                size > self.MAX_BUFFER_BYTES - self.buffered_bytes):
            self.reset_generation()
        if self.window_start is None:
            self.window_start = position100ns
        self.last_rate = rate
        self.last_channels = channels
        self.last_positions = positions
        self.buf.append((position100ns, samples))
        self.buffered_bytes += size
        # Prune anything older than the current window start (bounded memory).
        if self.buf:
            cutoff = self.window_start - int(0.5 * 1e7)  # 0.5s margin
            drop = 0
            for pos, _ in self.buf:
                if pos < cutoff:
                    drop += 1
                else:
                    break
            if drop:
                self.buffered_bytes -= sum(block.nbytes for _, block in self.buf[:drop])
                del self.buf[:drop]

    def _slice(self, start100ns, end100ns):
        """Concatenate samples whose media position falls in [start, end)."""
        rate = self.last_rate
        channels = self.last_channels
        pieces = []
        first_pos = None
        last_end = None
        for pos, samples in self.buf:
            frames = samples.shape[0]
            frame_end = pos + int(round(frames * 1e7 / rate))
            if frame_end <= start100ns or pos >= end100ns:
                continue
            pieces.append(samples)
            if first_pos is None:
                first_pos = pos
            last_end = frame_end
        if not pieces or first_pos is None or last_end is None:
            return np.zeros(0, dtype=np.float32)
        raw = np.concatenate(pieces, axis=0)
        # 对白优先的下混：5.1 的对白在 FRONT_CENTER 上，平均会把它稀释掉。
        mono = _downmix(raw, channels, self.last_positions)
        # The slice may start/end mid-frame; trim by media position.
        skip = max(0, int(round((start100ns - first_pos) * rate / 1e7)))
        keep = int(round((min(last_end, end100ns) - first_pos) * rate / 1e7)) - skip
        mono = mono[skip:skip + max(0, keep)]
        # Resample to 16 kHz for the model.
        return _resample_to_16k(mono, rate)

    def poll(self, now_end100ns):
        """Return (start, end, samples) when a window is due, else None."""
        if self.window_start is None or not self.buf:
            return None
        # 攒够 window 才出窗，不是过了 step 就出。用 step 当阈值会让窗长退化成
        # Δ + (window - step)（Δ = poll 间隔，宿主按采集块投喂只有几十毫秒），
        # 默认 5/3 实测每窗只有 ~2 秒 —— 窗口秒这一格因此名不副实。
        # advance 仍用 (window - step)，窗长恒为 window 时它等价于 window_start
        # += step，步进不受影响。代价是首窗要等满 window（本来就该等）。
        if now_end100ns - self.window_start < int(self.window_s * 1e7):
            return None
        # Media-time jump (pause/seek residue) larger than 2x window: cut,
        # never bridge across the gap (section 6.3).
        if now_end100ns - self.window_start > int(2 * self.window_s * 1e7):
            self.reset_generation()
            self.window_start = now_end100ns
            return None
        start = self.window_start
        end = start + int(self.window_s * 1e7)
        samples = self._slice(start, end)
        if len(samples) < int(0.3 * self.target_rate):
            return None
        # Advance: next window overlaps by (window - step).
        self.window_start = start + int(self.step_s * 1e7)
        self.covered_until = end
        return (start, end, samples)

    def finish(self, end):
        """Keep overlap so an EOF window can complete the preceding sentence."""
        if end is None or self.window_start is None or not self.buf:
            return None
        uncovered_start = max(self.window_start, self.covered_until or self.window_start)
        if end - uncovered_start < 3_000_000:
            return None
        start = self.window_start
        samples = self._slice(start, end)
        self.reset_generation()
        if len(samples) >= int(0.3 * self.target_rate):
            return start, end, samples
        return None


def looks_degenerate(text):
    """模型是不是在空转（把同一个字重复到撞上限）？

    实测（2026-10-25，CUDA0）：一段没有语音的音频换来 3808 个「啊」，占满 4096
    的 ctx、耗时 27s。它在字面上是"识别结果"，糊到屏幕上是纯粹的信息污染，而且
    比没有字幕更糟——用户会以为字幕功能坏了或者模型在胡说。判出来的窗直接丢掉，
    并把原因告诉用户（见调用处的 warning）。

    只判"长且高度重复"：短文本（"嗯。"）与正常句子都不可能让单字占到 60%，
    所以既不会误伤笑声（"哈哈哈"本来也不是字幕），也不会误伤正常对白。
    """
    s = "".join((text or "").split())
    if len(s) < _DEGENERATE_MIN_LEN:
        return False
    if not s:
        return False
    counts = {}
    for ch in s:
        counts[ch] = counts.get(ch, 0) + 1
    return max(counts.values()) / len(s) >= _DEGENERATE_TOP_RATIO


def looks_like_prompt_leak(text):
    """模型是不是把 instruction 当内容输出了？

    实测（2026-10-25，`zh-wiki` 语音稀疏段）：转写结果整句就是拼进 prompt 的
    模板串 `Transcribe audio to text`。它不是识别结果，也不是噪声——上屏等于告诉
    用户"模型在胡说"。判出来的窗直接丢掉，并和失控编字共用一次提示。

    两类命中：
      * 模板短语（大小写/标点不敏感），仅在文本短得像一句指令时才算——长段对白里
        偶尔出现 transcribe 一词不该被杀。
      * chat 模板标记残留（`asr_text` / `<|im_start|>` 等），一旦出现即泄漏，不看长度。

    正常的 {"language X<asr_text> 正文"} 前缀已在 SherpaBackend.transcribe() 里被
    剥离，这里只看剩下的正文，所以不会误伤带语言头的正常结果。
    """
    s = " ".join((text or "").split())
    if not s:
        return False
    low = s.lower()
    for marker in _PROMPT_LEAK_MARKERS:
        if marker in low:
            return True
    # 长度按原文算（不是按去掉非 ASCII 后的残串）：中文长句里夹一句英文短语，
    # 折完只剩那句短语，按残串判长度会把它误杀。
    if len(s) > _PROMPT_LEAK_MAX_LEN:
        return False
    # 模板串是英文指令，整句就是它。中文（或日韩文）正文里夹一句英文指令不是
    # 泄漏——讲 ASR 教程的长句正好长这样，别杀。
    cjk = len(_CJK_RE.findall(s))
    if cjk > _PROMPT_LEAK_MAX_CJK_RATIO * len(s):
        return False
    folded = re.sub(r"[^a-z0-9]+", " ", low).strip()
    return any(p in folded for p in _PROMPT_LEAK_PHRASES)


# --------------------------------------------------------------------------
# 金样本自检
#
# 为什么必须有：provider 写错、CUDA 版 onnxruntime 没装、tokenizer 目录指错，
# 这三种情况的共同表现都是"加载成功、转写出来是乱码或空"。用户只会看到一句
# "字幕全白"，而日志里一切正常。拿一段已知内容的音频跑一遍，用编辑距离卡
# 0.30，就能在 ready 之前把"加载了但不对"的设备拦下来并降档。
#
# 样本是可选的：没有 selftest.wav / selftest.txt 就跳过（打一行日志），不阻塞
# 正常使用 —— 自检是保险，不是前置条件。
# --------------------------------------------------------------------------


def _read_wav_mono(path):
    """读 16-bit PCM wav -> (float32 mono [-1,1], rate)。

    自检样本是我们自己给的，只需要支持 16-bit（录音笔/ffmpeg 的默认输出）。
    多声道按等权平均：样本里没有 AVChannel 位置表，_downmix 无从下手。
    """
    with wave.open(str(path), "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    if width != 2:
        raise ValueError(f"只支持 16-bit PCM 样本，这份是 {width * 8}-bit")
    data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    if channels > 1:
        data = data.reshape(-1, channels).mean(axis=1)
    return data, rate


def _normalize_for_compare(text):
    """比对前归一：去空白、去标点、转小写。

    编辑距离对全角/半角标点、句末句号极其敏感，不归一的话同一句话能差出两三个
    字符的"错"——那和"权重配错了"没法区分。用 Unicode 类别判，比列一张标点表
    少漏（中文的「，。、」都是 P 类）。
    """
    out = []
    for ch in str(text or "").lower():
        if unicodedata.category(ch)[0] in ("P", "Z", "C"):
            continue
        out.append(ch)
    return "".join(out)


def _edit_distance(a, b):
    """字符级 Levenshtein 距离（两行滚动，样本短，不必上库）。"""
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1,          # 删除
                           cur[j - 1] + 1,       # 插入
                           prev[j - 1] + (ca != cb)))  # 替换
        prev = cur
    return prev[-1]


def _run_selftest(backend, model_dir):
    """跑金样本自检，返回 (ok, detail)。没有样本时 ok=True 并说明跳过了。"""
    model_dir = Path(model_dir)
    wav_path = model_dir / "selftest.wav"
    txt_path = model_dir / "selftest.txt"
    if not wav_path.exists() or not txt_path.exists():
        return True, "未提供自检样本，跳过"
    try:
        audio, rate = _read_wav_mono(wav_path)
        # utf-8-sig：Windows 记事本存的中文样本常带 BOM，不剥掉会多出一个字符。
        expected = _normalize_for_compare(
            txt_path.read_text(encoding="utf-8-sig"))
    except Exception as exc:  # noqa: BLE001 - 样本坏了不算设备坏了
        return True, f"自检样本不可用，跳过：{exc}"
    if not audio.size:
        return True, "自检样本是空音频，跳过"
    audio = _resample_to_16k(audio, rate)
    try:
        _, text = backend.transcribe(audio)
    except Exception as exc:  # noqa: BLE001 - 转写失败就是自检失败
        return False, f"自检转写失败：{exc}"
    got = _normalize_for_compare(text)
    cer = _edit_distance(got, expected) / max(1, len(expected))
    ok = cer <= _SELFTEST_MAX_CER
    detail = (f"CER={cer:.2f}{'（通过）' if ok else '（超阈值）'} "
              f"期望={expected[:24]!r} 实得={got[:24]!r}")
    return ok, detail


def _select_backend(request, engine, model_dir, runtime_dir, threads):
    """按 _CANDIDATES[engine] 逐个试，第一个"加载成功且自检通过"的胜出。

    这是**唯一**一处回落。CUDA 没装、卡不在、NPU 没实现，全都表现为 load() 抛
    异常 —— 不需要为每种情况各写一条分支，也不该有第二层重试。
    返回 (backend|None, 每一步的说明, 采用者的自检说明)。
    """
    notes = []
    for backend_id in _CANDIDATES[engine][request]:
        # 先在设备表外挡一道：cuda 的 provider 是包形态决定的，等 load() 报错
        # 就晚了——它根本不报错，只打一行日志然后静默跑 CPU。
        ready, why = _capability(backend_id, engine, runtime_dir)
        if not ready:
            notes.append(f"{backend_id} 跳过（{why}）")
            continue
        backend = _make_backend(backend_id, engine, model_dir, runtime_dir, threads)
        try:
            backend.load()
        except Exception as exc:  # noqa: BLE001 - 设备不可用是常态，不是崩溃
            notes.append(f"{backend_id} 不可用（{exc}）")
            backend.close()
            continue
        ok, detail = _run_selftest(backend, model_dir)
        if ok:
            return backend, notes, detail
        notes.append(f"{backend_id} 自检不通过（{detail}）")
        backend.close()
    return None, notes, ""


def build_arg_parser():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model-dir", required=True)
    # 设备只有一个旋钮：auto 先试 cuda 再退 cpu；cpu / cuda 仍保留降档（指定 cuda
    # 但机器没 N 卡时不该直接罢工）；npu 是占位，见 NpuBackend。未知值一律当 auto。
    ap.add_argument("--device", default="auto")
    ap.add_argument("--window", type=float, default=5.0)
    ap.add_argument("--step", type=float, default=5.0)
    # 字幕语言：auto = 不指定；其余按 ISO 代码或英文名给（"zh" 或 "Chinese" 都收）。
    # 注意该运行时不给语言标签，所以它只影响日志与 matched，不拦结果。
    ap.add_argument("--language", default="auto")
    # 0 = 自动（min(6, 核数)）。CPU 上线程数直接决定速度；超过 6 在 P+E 混合
    # 架构上会退化（见 main() 里的实测数字）。GPU 上够采样即可。
    ap.add_argument("--threads", type=int, default=0)
    # 引擎：auto = 按模型目录里的容器自己判（有 *.gguf 就是 llama，否则 onnx）。
    # 清单里写了 engine 时宿主会显式传进来；传错也不致命，因为模型文件是唯一真相，
    # 这里只做一次交叉校验，不一致就按文件判并记一行日志。
    ap.add_argument("--engine", default="auto")
    # llama 引擎的原生运行时目录（含 llama-server.exe 与 ggml-*.dll）。
    # 缺省取 模型目录的兄弟目录 runtime/vulkan，也就是便携包的标准布局。
    ap.add_argument("--runtime-dir", default="")
    # zh-en 和旧档 en-zh 均启用双语：译文在上、原文在下。
    # 只有 GGUF（llama）引擎做得到 —— 它靠同一进程的原始 /completion 续写，
    # ONNX 那条腿是纯 ASR，没有文本生成能力，见 main() 里的降级告警。
    ap.add_argument("--bilingual", default="")
    return ap


def _resolve_runtime_dir(explicit, model_dir):
    """返回绝对的运行时目录；显式值也绝对化（理由同 main 里的 model_dir）。"""
    if explicit:
        return str(Path(explicit).expanduser().resolve())
    guess = Path(model_dir).parent / "runtime" / "vulkan"
    return str(guess) if guess.is_dir() else ""


def main():
    args = build_arg_parser().parse_args()

    sys.stdout.reconfigure(encoding="utf-8")

    model_dir = Path(args.model_dir)
    # 🔴 必须绝对化：llama-server 是以 cwd=runtime 目录起的（为了让它找得到同目录的
    # ggml-*.dll），子进程里的相对路径会被解析到 runtime/vulkan/model 去 —— 表现是
    # 「failed to load model」而不是路径错误。宿主传的相对路径（便携包的 model）就
    # 正好踩这个坑。绝对化之后 cwd 设成哪里都无所谓。
    model_dir = model_dir.expanduser().resolve()
    engine = _detect_engine(model_dir)
    declared = (args.engine or "auto").strip().lower()
    if declared in (_ENGINE_LLAMA, _ENGINE_ONNX) and declared != engine:
        pass
    missing = _missing_items(model_dir, engine)
    if missing:
        emit({"type": "status", "state": "error", "fatal": True,
              "detail": f"模型目录 {model_dir} 有问题：{'；'.join(missing)}"})
        return 2

    runtime_dir = _resolve_runtime_dir(args.runtime_dir, model_dir)
    if engine == _ENGINE_LLAMA and not runtime_dir:
        emit({"type": "status", "state": "error", "fatal": True,
              "detail": "这是 GGUF 包，但找不到 llama 运行时：清单里要写 runtime 目录"
                        "（便携包标准布局是 runtime/vulkan）"})
        return 2

    request = _normalize_request(args.device, engine)
    if request != (args.device or "").strip().lower():
        # 设备值是宿主的自由文本，拼错不该让字幕整个起不来：当成 auto 继续跑，
        # 只在日志里留下原值。
        pass

    # 上限 6 是实测卡出来的，不是拍的：这台 6P+8E 的机器上超过 6 个线程会有
    # 线程被排到 E-core，慢核拖住同步点，rtf 从 0.91 一路涨到 1.05(8) / 1.64(10)
    # / 4.70(20)。5 s 窗口 rtf 0.907 才勉强跟上实时，8 线程就 1.05 追不上了。
    # 物理核拓扑 Python 拿不到，取保守上限比取满安全 —— 何况播放器还要解码视频。
    threads = args.threads if args.threads > 0 else min(6, os.cpu_count() or 4)
    target_language = normalize_language(args.language)
    if target_language:
        # 闸门强不强取决于**最终选中的后端**，不是引擎：llama 自报语言；
        # OpenVINO 那条腿（qwen3_ort.Engine）也从 `language X<asr_text>` 模板里抠，
        # 实测能回 cantonese；只有 sherpa 的 result 没有独立 language 字段。
        # 选后端是下一步的事，所以这里只说目标，结论留给 ready 行。
        pass

    emit({"type": "status", "state": "loading",
          "detail": f"engine={engine} device={request} "
                    f"候选={'、'.join(_CANDIDATES[engine][request])} "
                    f"threads={threads}"})

    backend, notes, _ = _select_backend(request, engine, model_dir,
                                                runtime_dir, threads)
    if backend is None:
        emit({"type": "status", "state": "error", "fatal": True,
              "detail": "没有可用的推理设备：" + ("；".join(notes) or "无候选")})
        return 3

    # 译文依赖后端的文本生成能力。
    translation_mode = args.bilingual.strip().lower()
    chinese_requested = translation_mode in ("zh-above", "zh-below")
    bilingual_requested = bilingual_enabled(args.bilingual)
    bilingual_ok = bilingual_requested and hasattr(backend, "translate")
    chinese_ok = chinese_requested and hasattr(backend, "translate")
    if (chinese_requested and not chinese_ok) or (bilingual_requested and not bilingual_ok):
        emit({"type": "status", "state": "warning",
              "detail": "翻译需要支持文本生成的推理包，已按原语种输出"})

    # 请求的档没拿到（指定了 cuda / npu 却降了档）：说一句，别让用户以为自己
    # 选错了。auto 不报 —— 它本来就是"有什么用什么"，落到 cpu 是预期行为，
    # 每次启动都弹一条 warning 只会把真正的告警淹掉。只报一次：进程整个生命周期
    # 里设备不会再变。
    if request != "auto" and backend.id != _CANDIDATES[engine][request][0]:
        emit({"type": "status", "state": "warning",
              "detail": f"请求 {request}，实际用 {backend.id}"
                        f"（{backend.display}）：" + "；".join(notes)})

    # 语言闸门的强度按**后端**分，不是按引擎：sherpa 的 result 只有 text，报 on 等于
    # 骗人；llama 与 OpenVINO（qwen3_ort.Engine）都从 `language X<asr_text>` 模板里
    # 拿到自报语言，闸门真的生效。
    自报语言 = engine == _ENGINE_LLAMA or backend.id in _OV_TARGETS
    if not target_language:
        lang_detail = "lang=auto"
    elif 自报语言:
        lang_detail = f"lang={target_language} lang-gate=on"
    else:
        lang_detail = f"lang={target_language} lang-gate=off"
    device_kind = "NPU" if backend.id == "ov-npu" else (
        "CPU" if backend.id in ("cpu", "ov-cpu") else "GPU")
    # Leave enough shared audio to recognise the words around a window edge.
    overlap = min(2.0, args.window * 0.4)
    step = round(min(args.step, args.window - overlap), 6)
    emit({"type": "status", "state": "ready", "device": device_kind,
          "chineseTranslation": chinese_ok, "detail": f"window={args.window}s step={step}s device={backend.id} "
                    f"provider={backend.provider} threads={threads} {lang_detail} "
                    f"[{_provider_note(backend.id, engine)}]"})

    builder = WindowBuilder(args.window, step)
    assembler = SubtitleAssembler(round((args.window - step) * _TICKS_PER_SECOND))
    translator = SubtitleTranslator(backend, chinese_ok, bilingual_ok)
    windows = cues_sent = dropped = failures = 0
    infer_seconds = 0.0
    attempts = 0
    filtered = dict(empty=0, isolated=0, degenerate=0, leak=0)
    last_frame_end = None
    last_stats = time.time()
    generation = 0
    stdin = sys.stdin.buffer

    def publish_cues(cues, t0, audio_through):
        nonlocal cues_sent
        for cue in cues:
            for phrase in translator.phrases(cue):
                cues_sent += 1
                # An independent media-time interval per phrase, not a list
                # of equal-length pages for the host to guess timestamps for.
                emit({"type": "final", "generation": generation, **phrase,
                      "audioThrough100ns": audio_through if audio_through is not None else cue.end,
                      "bilingual": bilingual_ok, "inferenceMs": round((time.perf_counter() - t0) * 1000, 1),
                      "translationFirst": True, "language": cue.language,
                      "target": target_language or "", "matched": same_language(cue.language, target_language)})

    try:
        while True:
            head = read_exact(stdin, 4)
            if head is None:
                break
            meta_len = int.from_bytes(head, "little")
            if meta_len <= 0 or meta_len > _META_BYTES_MAX:
                # 长度前缀被写坏 ⇒ 帧边界已丢，长度前缀流无法重同步——继续读
                # 只会把 PCM 当元数据，错误滚雪球。停 worker：宿主收到 EOF 后
                # 关字幕，播放不受影响（P2，2026-11-03 审查定案）。
                break
            meta_bytes = read_exact(stdin, meta_len)
            if meta_bytes is None:
                break
            try:
                meta = json.loads(meta_bytes.decode("utf-8"))
            except Exception:
                # 长度前缀流一旦错位就无法重同步。旧代码在这里按"上一帧的
                # bytes"盲读 PCM（首帧时 meta 未定义，直接 NameError）——两种
                # 结果都是把后面每一帧全部读歪。帧边界已丢，唯一正确的动作
                # 是停 worker（P2，2026-11-03 审查定案）。
                break
            pcm = read_exact(stdin, meta.get("bytes", 0))
            if pcm is None:
                break

            if meta.get("generation") != generation:
                generation = meta.get("generation", 0)
                builder.reset_generation()
                assembler.reset()
                translator.reset()
                last_frame_end = None

            eof = meta.get("kind") == "eof"
            if eof:
                due = builder.finish(last_frame_end)
                audio_through = last_frame_end
                last_frame_end = None
            else:
                rate = int(meta.get("sampleRate", 48000))
                channels = int(meta.get("channels", 2))
                fmt = int(meta.get("sampleFormat", 8))
                frames = int(meta.get("frames", 0))
                if frames <= 0 or channels <= 0 or rate <= 0:
                    dropped += 1
                    continue
                arr = decode_pcm_frame(pcm, fmt, channels)
                if arr is None:
                    dropped += 1
                    continue
                position = int(meta.get("position100ns", 0))
                revision = builder.revision
                builder.add(position, arr, rate, channels, tuple(meta.get("positions") or ()))
                last_frame_end = position + int(round(frames * 1e7 / rate))
                audio_through = last_frame_end
                due = builder.poll(last_frame_end)
                if builder.revision != revision:
                    assembler.reset()
                    translator.reset()
            if due is None:
                if eof:
                    tail = assembler.finish()
                    if tail:
                        t0 = time.perf_counter()
                        emit({"type": "inference", "busy": True, "generation": generation})
                        publish_cues(tail, t0, audio_through)
                        infer_seconds += time.perf_counter() - t0
                        emit({"type": "inference", "busy": False, "generation": generation})
                continue
            start100ns, end100ns, samples = due

            emit({"type": "inference", "busy": True, "generation": generation})
            attempts += 1
            t0 = time.perf_counter()
            try:
                language, text = backend.transcribe(samples)
                failures = 0
            except Exception as e:  # noqa: BLE001 - 丢这一窗，绝不让播放等它
                failures += 1
                infer_seconds += time.perf_counter() - t0
                emit({"type": "inference", "busy": False, "generation": generation})
                # fatal=False: this window failed, the worker keeps running and
                # retries. The host must not treat it as "worker died" -- doing so
                # blanked subtitles for the rest of the session on a single hiccup.
                emit({"type": "status", "state": "error", "fatal": False,
                      "detail": f"inference failed ({failures}): {e}"})
                if failures >= 5:
                    emit({"type": "status", "state": "error", "fatal": True,
                          "detail": "consecutive inference failures"})
                    return 4
                continue

            if not any(ch.isalnum() for ch in text):
                reason = "empty"
            elif looks_like_isolated_character(text):
                reason = "isolated"
            elif looks_degenerate(text):
                reason = "degenerate"
            elif looks_like_prompt_leak(text):
                reason = "leak"
            else:
                reason = ""

            if reason:
                dropped += 1
                filtered[reason] += 1
            else:
                windows += 1
            speech_spans = None if reason else subtitle_audio_spans(samples, start100ns, end100ns)
            cues = assembler.accept(start100ns, end100ns, language or target_language or "",
                                    "" if reason else text, speech_spans)
            if eof:
                cues.extend(assembler.finish())
            publish_cues(cues, t0, audio_through)
            infer_seconds += time.perf_counter() - t0
            emit({"type": "inference", "busy": False, "generation": generation})

            if time.time() - last_stats >= 30.0:
                emit({"type": "stats", "rtf": round(infer_seconds / max(0.001, attempts * step), 3),
                      "windows": windows, "cues": cues_sent, "attempts": attempts, "dropped": dropped, "filtered": filtered})
                last_stats = time.time()
    except Exception as e:  # noqa: BLE001 - 兜底上报后按失败退出
        # 循环里任何未捕获异常过去会直接打死 worker 且一个字不吐：宿主只见
        # EOF，用户面对空白字幕且无解释。兜住并上报一次 fatal（P2，2026-11-03）。
        traceback.print_exc()
        emit({"type": "status", "state": "error", "fatal": True,
              "detail": f"worker 主循环异常退出：{type(e).__name__}: {e}"})
        return 1
    finally:
        backend.close()

    # Final stats carry the real cumulative rtf (this used to hardcode 0.0, which
    # made every shutdown look like "no inference ever ran").
    final_rtf = infer_seconds / max(0.001, attempts * step)
    emit({"type": "stats", "rtf": round(final_rtf, 3), "windows": windows,
          "cues": cues_sent, "attempts": attempts, "dropped": dropped, "filtered": filtered})
    return 0


if __name__ == "__main__":
    sys.exit(main())
