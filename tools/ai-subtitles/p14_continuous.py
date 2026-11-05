# P1.4: 15-minute continuous llama-server run (5s window / 5s step).
#
# Drives the production route (D7): llama-server /v1/audio/transcriptions.
# Records per-window latency, simulated backlog, server RSS and GPU VRAM,
# and any HTTP-level failure. A crash or a sustained backlog is a gate fail.

import argparse
import json
import subprocess
import time
from pathlib import Path

import numpy as np
import requests
import soundfile as sf


def load_loop_source(paths, window_seconds, minutes):
    data = []
    rate = None
    for p in paths:
        d, r = sf.read(str(p), dtype="float32", always_2d=True)
        if d.shape[1] > 1:
            d = d.mean(axis=1)
        else:
            d = d[:, 0]
        rate = r
        data.append(d)
    src = np.concatenate(data)
    need = int((minutes * 60.0 + window_seconds) * rate)
    reps = int(np.ceil(need / len(src)))
    return np.tile(src, reps)[:need], rate


def gpu_used_mib():
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=10)
        return float(out.stdout.strip().splitlines()[0])
    except Exception:
        return None


def server_rss_mib(pid):
    try:
        out = subprocess.run(
            ["tasklist", "/fi", f"PID eq {pid}", "/nh", "/fo", "csv"],
            capture_output=True, text=True, timeout=10)
        line = out.stdout.strip().splitlines()[0] if out.stdout.strip() else ""
        # "Image Name","PID","Session Name","Session#","Mem Usage"
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 5 and parts[1] == str(pid):
            return float(parts[4].replace(",", "").replace(" K", "")) / 1024.0
    except Exception:
        pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8199")
    ap.add_argument("--minutes", type=float, default=15.0)
    ap.add_argument("--window", type=float, default=5.0)
    ap.add_argument("--step", type=float, default=5.0)
    ap.add_argument("--audio", nargs="+", required=True)
    ap.add_argument("--pid", type=int, required=True, help="llama-server.exe process id")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    src, rate = load_loop_source(args.audio, args.window, args.minutes)
    win = int(args.window * rate)
    step = int(args.step * rate)
    windows = max(1, (len(src) - win) // step + 1)
    print(f"source {len(src)/rate:.0f}s @ {rate}Hz -> {windows} windows "
          f"({args.window}s/{args.step}s) for {args.minutes}min", flush=True)

    session = requests.Session()
    # The sandbox exports a system proxy; localhost must bypass it or the
    # proxy itself answers (404 "File Not Found") instead of llama-server.
    session.trust_env = False
    latencies, vram, rss, errors = [], [], [], []
    backlog = peak_backlog = 0.0
    tmp = Path(args.out).with_suffix(".window.wav")

    started = time.perf_counter()
    for i in range(windows):
        seg = src[i * step: i * step + win]
        sf.write(str(tmp), seg, rate)
        t0 = time.perf_counter()
        try:
            with open(tmp, "rb") as fh:
                r = session.post(
                    args.url.rstrip("/") + "/v1/audio/transcriptions",
                    files={"file": (tmp.name, fh, "audio/wav")},
                    data={"model": "qwen3-asr", "response_format": "json"},
                    timeout=120)
            dt = time.perf_counter() - t0
            if r.status_code != 200:
                errors.append(f"window {i}: HTTP {r.status_code}: {r.text[:200]}")
                break
            payload = r.json()
            if not payload.get("text"):
                errors.append(f"window {i}: empty text")
            latencies.append(dt)
            backlog = max(0.0, backlog + dt - args.step)
            peak_backlog = max(peak_backlog, backlog)
        except Exception as e:  # noqa: BLE001 - the gate is about runtime failures
            errors.append(f"window {i}: {type(e).__name__}: {e}")
            break
        v = gpu_used_mib()
        m = server_rss_mib(args.pid)
        if v is not None:
            vram.append(v)
        if m is not None:
            rss.append(m)
        if i % 10 == 0 or i == windows - 1:
            print(f"[{i+1}/{windows}] latency={dt:.2f}s backlog={backlog:.2f}s "
                  f"vram={v}MiB rss={m if m is None else round(m)}MiB", flush=True)
    elapsed = time.perf_counter() - started

    tmp.unlink(missing_ok=True)
    processed = len(latencies) * args.step
    result = {
        "requested_minutes": args.minutes,
        "window_seconds": args.window,
        "step_seconds": args.step,
        "requested_windows": windows,
        "completed_windows": len(latencies),
        "wall_seconds": elapsed,
        "processed_audio_seconds": processed,
        "mean_latency_seconds": float(np.mean(latencies)) if latencies else None,
        "p95_latency_seconds": float(np.percentile(latencies, 95)) if latencies else None,
        "max_latency_seconds": float(np.max(latencies)) if latencies else None,
        "peak_simulated_backlog_seconds": peak_backlog,
        "vram_mib": {"start": vram[0] if vram else None,
                     "end": vram[-1] if vram else None,
                     "max": max(vram) if vram else None},
        "server_rss_mib": {"start": rss[0] if rss else None,
                           "end": rss[-1] if rss else None,
                           "max": max(rss) if rss else None},
        "errors": errors,
    }
    Path(args.out).write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2), flush=True)


if __name__ == "__main__":
    main()
