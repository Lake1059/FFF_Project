# 3FP realtime AI-subtitle ASR worker (D7 route: llama-server over HTTP).
#
# Protocol (frozen, see docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md section 6):
#   stdin  frame: [u32 LE metaLen][metaLen bytes UTF-8 JSON][PCM bytes]
#   stdout line : one JSON object per line
#       {"type":"status","state":"loading|ready|error","detail":"..."}
#       {"type":"final","generation":N,"start100ns":S,"end100ns":E,
#        "text":"...","language":"Chinese","target":"Chinese","matched":true}
#       {"type":"stats","rtf":0.31,"windows":42,"dropped":0}
#   "target"/"matched" are only meaningful when --language is set; the host
#   decides whether a mismatched window is dropped (worker always reports).
#
# The worker owns a llama-server child process, converts tap frames to
# 16 kHz mono, windows them by MEDIA time (never wall clock), and posts each
# window to /v1/audio/transcriptions. Playback never waits on it: frames are
# dropped, inference is retried, and a dead server is restarted once.

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

import numpy as np
import requests
import soundfile as sf

TARGET_RATE = 16000

# --------------------------------------------------------------------------
# 语言指定（--language）
#
# llama-server 的 /v1/audio/transcriptions 把 language 字段**原样拼进 prompt**
# （同一段音频 input_tokens 80 -> 85），因此：
#   * 必须给模型认识的完整英文名。实测 language=Chinese 能把英文音频的标签
#     改成 Chinese，而 language=zh / language=Klingon 被忽略（标签仍是 English）。
#     宿主那边因此先做 ISO -> 英文名的映射，再把英文名传进来。
#   * 它只是软提示，不是硬约束：中文音频 + language=English 仍输出
#     language Chinese（模型确信时压不住）。要"只出所选语言"必须由宿主闸门丢弃。
#   * 它不是翻译：指定与片源不同的语言只会改标签，正文仍是原语言。
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
    """ISO 代码或英文名 -> 模型认识的英文名；auto/空值返回 None（不传字段）。"""
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
    # 未知值原样透传：llama-server 会忽略它，但日志里留下用户到底写了什么。
    return v


def same_language(reported, target):
    """模型自报的语言是否可算作目标语言。任一为空都按"匹配"处理（不乱杀）。"""
    if not reported or not target:
        return True
    a = reported.strip().lower()
    b = target.strip().lower()
    return a == b or a.startswith(b) or b.startswith(a)

# The sandbox (and some corporate networks) export a system proxy that would
# otherwise answer our own localhost calls itself (404 "File Not Found"):
# every HTTP call to llama-server -- health polls and transcriptions alike --
# must bypass it.
_HTTP = requests.Session()
_HTTP.trust_env = False


def log(msg):
    print(f"[asr-worker] {msg}", file=sys.stderr, flush=True)


# --------------------------------------------------------------------------
# 推理后端 / 设备选择
#
# llama.cpp 的一个二进制里只编进了它被编译时勾选的那几个后端（本机的 CUDA 构建
# --list-devices 只报 CUDA0），所以"换后端"本质是"换二进制"。至于这台机器上真
# 有什么设备、设备名叫什么，一律问二进制自己（--list-devices），不在这里猜名字。
# --------------------------------------------------------------------------

# 各后端在 --list-devices 里可能打印的前缀。一个后端给多个别名是为了不吃版本差异
# （ROCm 后端历史上既报过 ROCm 也报过 HIP），匹配时统一小写前缀比较。
BACKEND_ALIASES = {
    "cuda": ("cuda", "nvidia"),
    "vulkan": ("vulkan",),
    "rocm": ("rocm", "hip", "amd"),
    "sycl": ("sycl", "intel"),
    "cpu": ("cpu",),
}

_DEVICE_LINE = re.compile(r"^\s*([^\s:]+):\s*(.+?)\s*$")


def list_devices(exe):
    """问 llama-server 这台机器上有哪些设备。失败/超时返回空列表（不致命）。"""
    try:
        out = subprocess.run(
            [str(exe), "--list-devices"],
            capture_output=True, text=True, timeout=30,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except Exception as e:  # noqa: BLE001 - 探测失败只影响日志，不该拖垮启动
        log(f"list-devices failed: {e}")
        return []
    devices = []
    for line in (out.stdout or "").splitlines():
        m = _DEVICE_LINE.match(line)
        if m and not line.lower().startswith("available"):
            devices.append((m.group(1), m.group(2)))
    return devices


def _is_cpu_device(name):
    return name.lower().startswith(BACKEND_ALIASES["cpu"])


def resolve_device(exe, backend, index):
    """决定 --device 与 --n-gpu-layers。
    返回 (device_arg or None, ngl, note)；note 为空表示一切按预期。"""
    devices = list_devices(exe)
    shown = ", ".join(n for n, _ in devices) or "(二进制没报出任何设备)"

    if backend == "cpu":
        # -ngl 0 只表示"层不 offload"，官方文档明确说 GPU 仍可能参与部分算子；
        # 彻底关掉要用 --device none（本机二进制实测接受该值并正常加载模型）。
        # 这样"选 CPU"才是真的不碰显卡，显存占用归零。
        return "none", 0, f"设备=CPU（-ngl 0 --device none）；该二进制可用设备: {shown}"

    if backend == "auto":
        gpu = [n for n, _ in devices if not _is_cpu_device(n)]
        if devices and not gpu:
            return "none", 0, f"自动：这台机器上没报出 GPU 设备（{shown}），走 CPU"
        return None, 999, f"自动：交给 llama-server 自选；可用设备: {shown}"

    aliases = BACKEND_ALIASES.get(backend, (backend,))
    hits = [(n, d) for n, d in devices
            if n.lower().startswith(tuple(a.lower() for a in aliases))]
    if not hits:
        # 用户明确要了一个这个二进制没有的后端：说清楚再回落，绝不静默跑错设备。
        return (None, 999,
                f"WARNING 请求后端 {backend}，但这个 llama-server 只有: {shown}；"
                f"已回落到它的默认设备")

    if index >= 0 and index < len(hits):
        name, desc = hits[index]
    else:
        name, desc = hits[0]
        if index >= len(hits):
            return (name, 999,
                    f"WARNING 只有 {len(hits)} 块 {backend} 设备，序号 {index} 越界；"
                    f"用第一块 {name} ({desc})")
    return name, 999, f"设备={name} ({desc})"


def auto_threads(backend):
    """GPU 后端给少量线程做采样/回退即可；纯 CPU 时线程数直接决定速度。"""
    n = os.cpu_count() or 8
    if backend == "cpu":
        return max(4, min(n, 16))
    return max(4, min(n, 8))


def free_port():
    """Pick an unused localhost port.

    The host no longer hands us a fixed port: two 3FP windows (or a dual-pane
    comparison) each start their own worker, and a hardcoded 8199 meant the
    second llama-server could never bind. Ask the OS instead -- it knows which
    ports are actually free, and the window between close() and llama-server's
    bind() is small enough to be worth not managing ourselves.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def pick_tmpdir(preferred=""):
    """挑一个可写的临时目录，返回本 worker 独立的子目录（退出时整棵删掉）。

    preferred 是宿主验证过可写的根目录（--tmp-dir）；为空或不可写时依次回退：
    系统临时目录（tempfile 自己挑）→ worker 脚本旁边的 tmp → 当前目录的 tmp。
    schtasks 派发环境里连用户 Temp 都可能整个不可用
    （FileNotFoundError: No usable temporary directory found in [...]），
    所以这条链必须存在——2026-10-23 完整跑就死在这一步，字幕全白。

    每个 worker 用 mkdtemp 拿独立子目录：两个播放器窗口各起一份 worker 时
    互不干扰，退出时也只删自己那棵（wav 名字还带 uuid，双保险）。
    """
    candidates = []
    if preferred:
        candidates.append(Path(preferred))
    candidates.append(None)  # None = 系统默认
    try:
        candidates.append(Path(__file__).resolve().parent / "tmp")
    except Exception:
        pass
    candidates.append(Path.cwd() / "tmp")
    for root in candidates:
        try:
            if root is None:
                return tempfile.mkdtemp(prefix="asr_worker_")
            root.mkdir(parents=True, exist_ok=True)
            return tempfile.mkdtemp(prefix="asr_worker_", dir=str(root))
        except Exception as exc:
            log(f"tmpdir candidate {root or 'system default'} unusable "
                f"({type(exc).__name__}: {exc})")
    raise RuntimeError("no writable temporary directory "
                       "(tried --tmp-dir, system temp, script dir, cwd)")


class LlamaServer:
    """Child llama-server process with health polling and one restart."""

    def __init__(self, exe, model, mmproj, port, device=None, ngl=999, threads=8):
        self.exe = exe
        self.model = model
        self.mmproj = mmproj
        self.port = port
        # device=None 表示不传 --device，交给 llama-server 按自己的后端默认挑。
        self.device = device
        self.ngl = ngl
        self.threads = threads
        self.proc = None
        self.restarts_left = 1

    def start(self, timeout_s=90.0):
        cmd = [
            str(self.exe),
            "-m", str(self.model),
            "--mmproj", str(self.mmproj),
            "--port", str(self.port),
            "--host", "127.0.0.1",
            "--ctx-size", "4096",
            # 0 = 全部留在内存里跑 CPU 后端；999 = 能 offload 的层全给设备。
            "--n-gpu-layers", str(self.ngl),
            "--threads", str(self.threads),
            "--temp", "0.6",
            # Every subtitle window is unique audio, so prefix reuse is pure
            # waste: the default prompt cache (cache-ram 8192) retains ~12 MiB
            # of KV per unique window on the host side (VRAM stays flat) --
            # measured +2.1 GB over a 15-minute run. Both flags together keep
            # RSS flat (p14_continuous_q8_fixed.json).
            "--no-cache-idle-slots",
            "--cache-ram", "0",
        ]
        if self.device:
            cmd += ["--device", str(self.device)]
        log("starting llama-server: " + " ".join(cmd))
        self.proc = subprocess.Popen(
            cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"llama-server exited early rc={self.proc.returncode}")
            try:
                r = _HTTP.get(f"http://127.0.0.1:{self.port}/health", timeout=3)
                if r.status_code == 200:
                    log("llama-server ready")
                    return
            except requests.RequestException:
                pass
            time.sleep(0.5)
        raise RuntimeError("llama-server health timeout")

    def warm_audio(self, seconds=0.5, tmpdir=None):
        """用一小段静音替音频路径付掉一次性开销，别让第一个真窗口承担。

        实测（CUDA + Q4_K_M，2026-10-23）：
          * 刚量化出来的新权重**第一次**加载：首个音频请求 16.3 s（窗口只有 5 s），
            第二个 1.3 s —— 大头是权重首次从磁盘搬进显存，预热消不掉；
          * 同一份权重第二次启动（文件已在页缓存）：预热 0.77 s，随后 10 s 音频
            1.50 s（rtf 0.150）、4.2 s 音频 0.59 s（rtf 0.140）。
        所以这里付掉的是"音频图首次构建"那一层（约 0.7 s），顺带把
        "第一句字幕比后面慢一截"的抖动压平。权重首次上传属于换档后的正常现象，
        见 MODEL_TIERS.md。

        tmpdir 传 main() 选好的目录；不传就自己回退选一个（绝不因此崩）。

        失败一律忽略：warmup 只是优化，不承担正确性。
        """
        try:
            t0 = time.perf_counter()
            silence = np.zeros(int(TARGET_RATE * seconds), dtype=np.float32)
            if not tmpdir:
                tmpdir = pick_tmpdir("")
            transcribe(self, silence, tmpdir, _HTTP, None)
            log(f"audio path warmup {time.perf_counter() - t0:.1f}s")
        except Exception as exc:
            log(f"audio path warmup skipped ({type(exc).__name__}: {exc})")

    def ensure(self):
        """Restart once if the process died; raise if out of restarts."""
        if self.proc is not None and self.proc.poll() is None:
            return
        if self.restarts_left <= 0:
            raise RuntimeError("llama-server keeps dying")
        self.restarts_left -= 1
        log("llama-server died; restarting (last restart)")
        self.start()

    def stop(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None


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


class WindowBuilder:
    """Media-time windowing over tap frames (section 6.2-6.4)."""

    def __init__(self, window_s, step_s, target_rate=TARGET_RATE):
        self.window_s = window_s
        self.step_s = step_s
        self.target_rate = target_rate
        self.buf = []            # list of (position100ns, float32 mono @ src rate)
        self.window_start = None  # position100ns or None
        self.last_rate = None
        self.last_channels = None
        self.last_positions = None

    def reset_generation(self):
        self.buf.clear()
        self.window_start = None

    def add(self, position100ns, samples, rate, channels, positions=None):
        """samples: float32 [frames, channels], de-interleaved (row = frame)."""
        if self.window_start is None:
            self.window_start = position100ns
        self.last_rate = rate
        self.last_channels = channels
        self.last_positions = positions
        self.buf.append((position100ns, samples))
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
        keep = int(round((last_end - first_pos) * rate / 1e7)) - skip
        mono = mono[skip:skip + max(0, keep)]
        # Resample to 16 kHz for the model.
        if rate != self.target_rate:
            try:
                import soxr
                mono = soxr.resample(mono, rate, self.target_rate)
            except ImportError:
                n = int(round(len(mono) * self.target_rate / rate))
                mono = np.interp(
                    np.linspace(0, len(mono) - 1, n),
                    np.arange(len(mono)), mono).astype(np.float32)
        return mono.astype(np.float32)

    def poll(self, now_end100ns):
        """Return (start, end, samples) when a window is due, else None."""
        if self.window_start is None or not self.buf:
            return None
        if now_end100ns - self.window_start < int(self.step_s * 1e7):
            return None
        # Media-time jump (pause/seek residue) larger than 2x window: cut,
        # never bridge across the gap (section 6.3).
        if now_end100ns - self.window_start > int(2 * self.window_s * 1e7):
            log(f"media time jump {now_end100ns - self.window_start} 100ns; cutting window")
            self.reset_generation()
            self.window_start = now_end100ns
            return None
        end = now_end100ns
        start = end - int(self.window_s * 1e7)
        if start < self.window_start:
            start = self.window_start
        samples = self._slice(start, end)
        if len(samples) < int(0.3 * self.target_rate):
            return None
        # Advance: next window overlaps by (window - step).
        self.window_start = end - int((self.window_s - self.step_s) * 1e7)
        return (start, end, samples)


def transcribe(server, samples, tmpdir, session, language=None):
    """Write the window as 16k mono PCM16, POST it, return (language, text).

    language is the model-facing English name ("Chinese"), never an ISO code:
    llama-server pastes the string straight into the prompt and the model only
    recognises the names it was trained on. None = let the model decide.
    """
    import re
    path = Path(tmpdir) / f"win_{uuid.uuid4().hex}.wav"
    try:
        sf.write(str(path), samples, TARGET_RATE, subtype="PCM_16")
        data = {"model": "qwen3-asr", "response_format": "json"}
        if language:
            data["language"] = language
        with open(path, "rb") as fh:
            r = session.post(
                f"http://127.0.0.1:{server.port}/v1/audio/transcriptions",
                files={"file": (path.name, fh, "audio/wav")},
                data=data,
                timeout=120)
        if r.status_code != 200:
            raise RuntimeError(f"HTTP {r.status_code}: {r.text[:200]}")
        text = r.json().get("text", "")
        m = re.match(r"^language\s+([A-Za-z\-]+)<asr_text>\s*(.*)$", text, re.S)
        if m:
            return m.group(1).lower(), m.group(2).strip()
        return "", text.strip()
    finally:
        path.unlink(missing_ok=True)


def read_exact(stream, n):
    data = b""
    while len(data) < n:
        chunk = stream.read(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model-dir", required=True)
    ap.add_argument("--quant", default="Q8_0")
    ap.add_argument("--mmproj-quant", default="Q8_0")
    # 扩展包可以把两个权重文件的路径直接写进清单（--model / --mmproj），
    # 优先于 --model-dir + --quant 的文件名拼接 —— 这样清单能自描述任意量化档。
    ap.add_argument("--model", default="")
    ap.add_argument("--mmproj", default="")
    ap.add_argument("--llama-server", default="llama-server.exe")
    # 0 = 自己挑一个空闲端口。宿主总是传 0：两个播放器窗口（或双路对比）各起
    # 一份 worker，固定端口会让第二份 llama-server 绑不上。
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--window", type=float, default=5.0)
    ap.add_argument("--step", type=float, default=5.0)
    # 推理设备。--llama-servers 是 {后端: 二进制} 的 JSON：换后端在 llama.cpp
    # 这边等于换二进制，所以由扩展包清单提供映射，本体只负责"用户选了哪一类"。
    ap.add_argument("--device-backend", default="auto")
    ap.add_argument("--device-index", type=int, default=-1)
    ap.add_argument("--llama-servers", default="")
    ap.add_argument("--threads", type=int, default=0)
    # 卸载到显卡的层数（llama-server --n-gpu-layers）。默认 -1 = 不覆盖：
    # auto/指定后端由 resolve_device 定（GPU 全卸 999，CPU 0）；>=0 时用户显式
    # 指定 —— 显存吃紧的机器填小数（本模型 28 层，如 16 = 一半权重在显存），
    # 0 = 一层都不卸（与选 CPU 不同：device 仍是 GPU，部分算子还在卡上）。
    ap.add_argument("--ngl", type=int, default=-1)
    # 字幕语言：auto = 不传 language 字段，让模型自判；其余按 ISO 代码或英文名
    # 给（"zh" 或 "Chinese" 都收）。注意这只是软提示，不翻译、也不保证标签。
    ap.add_argument("--language", default="auto")
    # 临时目录根：宿主把它亲眼验证过可写的目录传进来（扩展包目录或程序目录下的
    # tmp）。不传或不可写时 worker 自己回退（见 pick_tmpdir）—— schtasks 派发等
    # 环境下系统临时目录可能整个不可用，Python 会在 mkdtemp 直接
    # FileNotFoundError 退出，字幕全白（2026-10-23 完整跑实测）。
    ap.add_argument("--tmp-dir", default="")
    args = ap.parse_args()

    sys.stdout.reconfigure(encoding="utf-8")
    model_dir = Path(args.model_dir)
    if args.model:
        model = Path(args.model)
        if args.mmproj:
            mmproj = Path(args.mmproj)
        else:
            # 只给了主模型：在它旁边找投影（mmproj-*.gguf）；找不到就落到
            # 一个必然不存在的名字上，让下面的存在性检查统一报错。
            cands = sorted(model.parent.glob("mmproj-*.gguf"))
            mmproj = cands[0] if cands else model.parent / "__missing_mmproj__.gguf"
    else:
        model = model_dir / f"Qwen3-ASR-0.6B-{args.quant}.gguf"
        mmproj = model_dir / f"mmproj-Qwen3-ASR-0.6B-{args.mmproj_quant}.gguf"
    for p in (model, mmproj):
        if not p.exists():
            emit({"type": "status", "state": "error", "detail": f"missing {p}"})
            return 2

    backend = (args.device_backend or "auto").strip().lower()
    if backend != "auto" and backend not in BACKEND_ALIASES:
        log(f"unknown backend {backend!r}; falling back to auto")
        backend = "auto"

    servers = {}
    if args.llama_servers:
        try:
            servers = {str(k).strip().lower(): str(v)
                       for k, v in json.loads(args.llama_servers).items() if v}
        except Exception as e:  # noqa: BLE001 - 坏 JSON 不该让字幕整个停摆
            log(f"bad --llama-servers json: {e}")
    exe = servers.get(backend) or args.llama_server
    threads = args.threads if args.threads > 0 else auto_threads(backend)
    port = args.port if args.port > 0 else free_port()

    builder = WindowBuilder(args.window, args.step)
    session = _HTTP
    target_language = normalize_language(args.language)
    if target_language:
        log(f"language hint: {target_language} (soft prompt, not a hard constraint)")

    emit({"type": "status", "state": "loading", "detail": "starting llama-server"})
    device, ngl, note = resolve_device(exe, backend, args.device_index)
    # 用户显式指定层数时覆盖自动决策（-1 = 不覆盖）。999 是"全部"的约定写法，
    # llama-server 自己会按模型层数 clamp；填 0 在 GPU 后端下表示不卸层。
    if args.ngl >= 0:
        if args.ngl != ngl:
            note += f"；用户指定层数 {args.ngl}（原自动值 {ngl}）"
        ngl = args.ngl
    log(note)
    if note.startswith("WARNING"):
        emit({"type": "status", "state": "warning", "detail": note})
    emit({"type": "status", "state": "loading",
          "detail": f"backend={backend} device={device or 'default'} ngl={ngl} "
                    f"threads={threads} port={port} exe={exe}"})
    server = LlamaServer(exe, model, mmproj, port,
                         device=device, ngl=ngl, threads=threads)
    # 临时目录必须在起 llama-server 之前定好：选不出来的话走协议报 fatal，
    # 让宿主记一行日志，而不是一句 traceback 崩掉（2026-10-23 完整跑的教训）。
    try:
        tmpdir = pick_tmpdir(args.tmp_dir)
    except Exception as e:  # noqa: BLE001 - report through the protocol
        emit({"type": "status", "state": "error", "fatal": True,
              "detail": f"no writable temp dir: {e}"})
        return 5
    log(f"tmpdir: {tmpdir}")
    windows = dropped = failures = 0
    infer_seconds = []
    last_stats = time.time()
    generation = 0

    try:
        server.start()
        # 先把音频路径预热掉：否则第一个真窗口要额外付十几秒构图代价，用户看到
        # 的第一句字幕会明显迟到（详见 warm_audio 的注释）。
        server.warm_audio(tmpdir=tmpdir)
    except Exception as e:  # noqa: BLE001 - report through the protocol
        emit({"type": "status", "state": "error", "detail": str(e)})
        return 3

    emit({"type": "status", "state": "ready",
          "detail": f"window={args.window}s step={args.step}s" +
                    (f" lang={target_language}" if target_language else " lang=auto")})
    log("ready; reading frames from stdin")

    stdin = sys.stdin.buffer
    try:
        while True:
            head = read_exact(stdin, 4)
            if head is None:
                log("stdin EOF; shutting down")
                break
            meta_len = int.from_bytes(head, "little")
            meta_bytes = read_exact(stdin, meta_len)
            if meta_bytes is None:
                break
            try:
                meta = json.loads(meta_bytes.decode("utf-8"))
            except Exception:
                # Resync is impossible on a length-prefixed stream; drop PCM.
                pcm = read_exact(stdin, meta.get("bytes", 0) if isinstance(meta, dict) else 0)
                continue
            pcm = read_exact(stdin, meta.get("bytes", 0))
            if pcm is None:
                break

            if meta.get("generation") != generation:
                generation = meta.get("generation", 0)
                builder.reset_generation()
                log(f"generation -> {generation}; buffer cleared")

            rate = int(meta.get("sampleRate", 48000))
            channels = int(meta.get("channels", 2))
            fmt = int(meta.get("sampleFormat", 8))  # 8 = AV_SAMPLE_FMT_FLTP
            frames = int(meta.get("frames", len(pcm) // (4 * max(1, channels))))
            if frames <= 0 or channels <= 0:
                dropped += 1
                continue
            try:
                if fmt == AV_SAMPLE_FMT_FLTP:
                    # Planar: kernel writes one plane per channel back to back, so
                    # the flat array is [ch0 frames][ch1 frames]... -- reshaping it
                    # as interleaved would pair up unrelated samples. The old
                    # mean(axis=1) hid this because an average does not care about
                    # the grouping; picking a specific channel does.
                    arr = np.frombuffer(pcm, dtype="<f4").reshape(channels, -1).T
                else:  # packed float32 (AV_SAMPLE_FMT_FLT)
                    arr = np.frombuffer(pcm, dtype="<f4").reshape(-1, channels)
            except ValueError:
                dropped += 1
                continue

            positions = meta.get("positions") or ()
            builder.add(int(meta.get("position100ns", 0)), arr, rate, channels, positions)
            frame_end = int(meta.get("position100ns", 0)) + int(round(frames * 1e7 / rate))

            due = builder.poll(frame_end)
            if due is None:
                continue
            start100ns, end100ns, samples = due

            t0 = time.perf_counter()
            try:
                server.ensure()
                language, text = transcribe(server, samples, tmpdir, session,
                                            target_language)
                failures = 0
            except Exception as e:  # noqa: BLE001 - retry, never block playback
                failures += 1
                # fatal=False: this window failed, the worker keeps running and
                # retries. The host must not treat it as "worker died" -- doing so
                # blanked subtitles for the rest of the session on a single hiccup.
                emit({"type": "status", "state": "error", "fatal": False,
                      "detail": f"inference failed ({failures}): {e}"})
                if failures >= 5:
                    log("too many consecutive failures; exiting for host restart")
                    emit({"type": "status", "state": "error", "fatal": True,
                          "detail": "consecutive inference failures"})
                    return 4
                continue
            dt = time.perf_counter() - t0
            infer_seconds.append(dt)
            windows += 1
            # matched 只是"报给宿主看"：worker 从不自己丢结果（宿主可选择丢弃，
            # 而且那个开关不该逼 worker 重启）。
            matched = same_language(language, target_language)
            emit({"type": "final", "generation": generation,
                  "start100ns": start100ns, "end100ns": end100ns,
                  "text": text, "language": language,
                  "target": target_language or "", "matched": matched})
            log(f"final [{start100ns/1e7:.2f}s..{end100ns/1e7:.2f}s] "
                f"lang={language or '?'}"
                f"{'' if matched else f' (target={target_language}, mismatched)'}"
                f" rtf={dt/max(0.001, len(samples)/TARGET_RATE):.3f} :: {text[:60]}")

            if time.time() - last_stats >= 30.0:
                rtf = (sum(infer_seconds) /
                       max(0.001, windows * args.step)) if windows else 0.0
                emit({"type": "stats", "rtf": round(rtf, 3),
                      "windows": windows, "dropped": dropped})
                last_stats = time.time()
    finally:
        server.stop()
        try:
            import shutil
            shutil.rmtree(tmpdir, ignore_errors=True)
        except Exception:
            pass
    # Final stats carry the real cumulative rtf (this used to hardcode 0.0, which
    # made every shutdown look like "no inference ever ran").
    final_rtf = (sum(infer_seconds) / max(0.001, windows * args.step)) if windows else 0.0
    emit({"type": "stats", "rtf": round(final_rtf, 3),
          "windows": windows, "dropped": dropped})
    return 0


if __name__ == "__main__":
    sys.exit(main())
