"""协议级驱动：像宿主那样喂 PCM 帧，读 worker 的 JSON 行。

用法:
  python proto_run.py <pack根> [--device auto|cpu|gpu] [--wav 绝对路径] [--selftest]

判据只看三样：
  * status ready 里的 engine / device / provider
  * final 的文本（模板有没有被剥掉）
  * 首窗延迟
"""
import argparse
import json
import os
import queue
import struct
import subprocess
import sys
import threading
import time
import wave

PY = r"C:\Users\20210\AppData\Local\Programs\Python\Python312\python.exe"
WORKER = r"C:\PLAN\3FCompare\third_party\fff_project\tools\ai-subtitles\asr_worker.py"


def read_wav(path):
    with wave.open(path) as w:
        assert w.getsampwidth() == 2, "只接受 16 bit PCM"
        sr, ch = w.getframerate(), w.getnchannels()
        raw = w.readframes(w.getnframes())
    # 协议里的 sampleFormat 是 AVSampleFormat：3 = FLT（打包 float32），
    # 8 = FLTP（平面 float32）。送 s16 字节却声明成 float32，worker 会解析出
    # NaN —— 实测表现是"输出一串重复单字，然后被 degenerate 过滤丢掉"。
    import numpy as np
    return (np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0).tobytes(), sr, ch


class Reader(threading.Thread):
    """Windows 的 select 不支持管道，用一条线程把 stdout 行灌进队列。"""

    def __init__(self, stream):
        super().__init__(daemon=True)
        self.stream = stream
        self.q = queue.Queue()

    def run(self):
        for line in iter(self.stream.readline, b""):
            line = line.decode("utf-8", "replace").strip()
            if not line:
                continue
            try:
                self.q.put(json.loads(line))
            except Exception:
                self.q.put({"type": "raw", "text": line[:160]})
        self.q.put({"type": "eof"})

    def drain(self, timeout=0.0):
        out = []
        try:
            out.append(self.q.get(timeout=max(0.001, timeout)) if timeout
                       else self.q.get_nowait())
        except queue.Empty:
            return out
        while True:
            try:
                out.append(self.q.get_nowait())
            except queue.Empty:
                return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("--device", default="auto")
    ap.add_argument("--wav", default=r"C:\PLAN\3FCompare\_3fc_tmp\ai-pack\model\selftest.wav")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--seconds", type=float, default=0.0, help="只喂前面 N 秒（0=全喂）")
    args = ap.parse_args()

    pack = os.path.abspath(args.pack)
    model_dir = os.path.join(pack, "model")
    cmd = [PY, WORKER, "--model-dir", model_dir, "--device", args.device,
           "--threads", "6"]
    if args.selftest:
        cmd.append("--selftest")
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            bufsize=0)
    reader = Reader(proc.stdout)
    reader.start()

    raw, sr, ch = read_wav(args.wav)
    # float32 => 每帧 4 字节
    dur = len(raw) / (4 * ch * sr)
    if args.seconds and args.seconds < dur:
        dur = args.seconds
        raw = raw[: int(args.seconds * sr) * 4 * ch]

    block = int(0.1 * sr) * 4 * ch
    ready = False
    finals = []
    deadline = time.time() + 200

    def show(evs):
        nonlocal ready
        for ev in evs:
            print(f"[{time.time()-t0:6.2f}s] {json.dumps(ev, ensure_ascii=False)[:240]}",
                  flush=True)
            if ev.get("type") == "status" and ev.get("state") == "ready":
                ready = True
            if ev.get("type") == "final":
                finals.append(ev)
            if ev.get("type") == "eof" or (ev.get("type") == "status" and ev.get("fatal")):
                return False
        return True

    while not ready:
        if not show(reader.drain(0.3)):
            proc.kill()
            return 1
        if time.time() > deadline:
            print("!! 等 ready 超时")
            proc.kill()
            return 1

    print(f"== ready 用时 {time.time()-t0:.1f}s，开始喂 {dur:.1f}s 音频 ==", flush=True)
    t_feed = time.time()
    t_first = None
    pos = 0
    while pos < len(raw):
        chunk = raw[pos: pos + block]
        pos += len(chunk)
        meta = json.dumps({"generation": 0, "sampleRate": sr, "channels": ch,
                           "sampleFormat": 3, "frames": len(chunk) // (4 * ch),
                           "position100ns": int(round((pos // (4 * ch)) * 1e7 / sr)),
                           "bytes": len(chunk), "positions": []}).encode()
        proc.stdin.write(struct.pack("<I", len(meta)) + meta + chunk)
        proc.stdin.flush()
        n_before = len(finals)
        show(reader.drain(0.0))
        if len(finals) > n_before and t_first is None:
            t_first = time.time() - t_feed

    for _ in range(100):
        show(reader.drain(0.2))
        if finals:
            break

    try:
        proc.stdin.close()
    except Exception:
        pass
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()

    print("\n== 结果 ==", flush=True)
    print(f"音频 {dur:.2f}s  首窗延迟 {t_first if t_first is None else round(t_first, 2)}s")
    for f in finals:
        print(f"  final [{f['start100ns']/1e7:.2f}..{f['end100ns']/1e7:.2f}] "
              f"lang={f['language']} :: {f['text']}")
    if not finals:
        print("  （没有 final —— 看上面的 status）")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
