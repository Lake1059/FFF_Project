"""P5 worker smoke test: drive asr_worker.py over its stdin/stdout protocol
without the player, so llama-server / windowing / transcription are proven
before we spend a real playback run on them.

Frame wire format (must match FFF.Player/AI字幕/Ai字幕管道.vb 写帧()):
    [u32 LE metaLen][meta JSON UTF-8][PCM float32 interleaved]

Usage:
    python p5_worker_smoke.py <wav> [--repeat N] [--window 5] [--step 5]
"""
import argparse
import json
import struct
import subprocess
import sys
import threading
import time

import numpy as np
import soundfile as sf

VENV_PY = r"E:\WorkBuddy\3FP-AI-Subtitles\venv\Scripts\python.exe"
WORKER = r"C:\PLAN\3FCompare\_3fp-ai-subtitles\tools\ai-subtitles\asr_worker.py"
MODEL_DIR = r"E:\WorkBuddy\3FP-AI-Subtitles\Qwen3-ASR-0.6B-GGUF"
LLAMA = r"E:\WorkBuddy\3FP-AI-Subtitles\llama-b11669\cuda\llama-server.exe"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--repeat", type=int, default=2)
    ap.add_argument("--window", type=float, default=5.0)
    ap.add_argument("--step", type=float, default=5.0)
    ap.add_argument("--quant", default="Q8_0")
    ap.add_argument("--frame-ms", type=float, default=100.0)
    ap.add_argument("--language", default="auto")   # ISO 代码或英文名，验证提示是否生效
    args = ap.parse_args()

    data, rate = sf.read(args.wav, dtype="float32")
    if data.ndim > 1:
        data = data.mean(axis=1)
    data = np.tile(data, args.repeat)
    # Present as stereo interleaved: exercises the downmix path (D1).
    stereo = np.repeat(data[:, None], 2, axis=1).reshape(-1)

    frames_per_chunk = max(1, int(rate * args.frame_ms / 1000.0))
    nch = 2

    cmd = [VENV_PY, WORKER,
           "--model-dir", MODEL_DIR, "--quant", args.quant,
           "--mmproj-quant", "Q8_0",
           "--llama-server", LLAMA, "--port", "8211",
           "--window", str(args.window), "--step", str(args.step),
           "--language", args.language]
    print(" ".join(cmd), flush=True)

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    results = []

    def reader():
        for line in proc.stdout:
            line = line.decode("utf-8", "replace").strip()
            if not line:
                continue
            print(f"[out] {line}", flush=True)
            try:
                results.append(json.loads(line))
            except Exception:
                pass
        print("[out] <eof>", flush=True)

    err_lines = []

    def err_reader():
        for line in proc.stderr:
            err_lines.append(line.decode("utf-8", "replace").rstrip())

    threading.Thread(target=reader, daemon=True).start()
    threading.Thread(target=err_reader, daemon=True).start()

    total = len(stereo) // nch
    t0 = time.time()
    pos = 0
    written = 0
    try:
        for off in range(0, total, frames_per_chunk):
            frames = min(frames_per_chunk, total - off)
            pcm = stereo[off * nch:(off + frames) * nch]
            meta = {
                "generation": 0,
                "position100ns": pos,
                "sampleRate": rate,
                "channels": nch,
                "sampleFormat": 8,
                "frames": frames,
                "bytes": int(pcm.nbytes),
            }
            mb = json.dumps(meta).encode("utf-8")
            try:
                proc.stdin.write(struct.pack("<I", len(mb)) + mb + pcm.tobytes())
            except (BrokenPipeError, ValueError):
                print("stdin closed early", flush=True)
                break
            written += frames
            pos += int(round(frames * 1e7 / rate))
            # Feed slightly slower than realtime so the worker keeps up.
            target = written / rate
            elapsed = time.time() - t0
            if target > elapsed:
                time.sleep(min(0.5, target - elapsed))
    finally:
        try:
            proc.stdin.flush()
            proc.stdin.close()
        except Exception:
            pass

    rc = proc.wait(timeout=120)
    print(f"\n=== exit={rc} fed={written/rate:.2f}s audio in {time.time()-t0:.1f}s", flush=True)
    finals = [r for r in results if r.get("type") == "final"]
    print(f"=== final windows: {len(finals)}", flush=True)
    for f in finals:
        print(f"    [{f['start100ns']/1e7:6.2f}..{f['end100ns']/1e7:6.2f}] "
              f"({f.get('language')} target={f.get('target')} matched={f.get('matched')}) "
              f"{f.get('text','')[:80]}", flush=True)
    if err_lines:
        print("=== stderr (tail):", flush=True)
        for l in err_lines[-20:]:
            print("    " + l, flush=True)
    return 0 if finals else 1


if __name__ == "__main__":
    sys.exit(main())
