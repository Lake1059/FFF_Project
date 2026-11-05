"""llama.cpp CPU 臂：GGUF Q8_0，-ngl 0 --device CPU，温度 0（贪心）。

温度是关键：最初用 --temp 0.6，中文会退化成复述指令（"Transcribe audio to text."）。
ASR 必须是贪心解码，否则小模型的分布尾部会自己走飞。
"""
import json
import os
import statistics
import sys
import time

sys.path.insert(0, r"C:\PLAN\3FCompare\_3fc_tmp\igpu-test")
import rerun_gpu as R

EXE = os.path.join(R.BASE, "cuda", "llama-server.exe")
THREADS = 6
OUT = os.path.join(r"C:\PLAN\3FCompare\_3fc_tmp\pkg-ab", "llama-cpu.json")

import argparse as _ap

_argv = _ap.ArgumentParser()
_argv.add_argument("--wavs", default=None)
_argv.add_argument("--out", default=None)
_A = _argv.parse_args()
if _A.out:
    OUT = _A.out


def strip_template(t):
    return t.split("<asr_text>", 1)[1].strip() if "<asr_text>" in t else t.strip()


def main():
    port = R.free_port()
    cmd = [EXE, "-m", R.MODEL, "--mmproj", R.MMPROJ,
           "--port", str(port), "--host", "127.0.0.1",
           "--ctx-size", "4096", "--n-gpu-layers", "0",
           "--threads", str(THREADS), "--temp", "0",
           "--n-predict", "512", "--cache-ram", "0"]
    log = open(os.path.join(r"C:\PLAN\3FCompare\_3fc_tmp\pkg-ab", "llama-cpu.log"), "wb")
    import subprocess
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(EXE), stdout=log, stderr=log)
    ok, why = R.wait_health(port, proc, timeout=300)
    print("ready:", ok, why, "boot %.1fs" % (time.time() - t0), flush=True)
    if not ok:
        R.kill(proc)
        print(open(log.name, "rb").read()[-1500:].decode("utf-8", "replace"))
        return 1

    mid = R.http_get(port, "/v1/models")["data"][0]["id"]
    rows = []
    wavs = _A.wavs.split(",") if _A.wavs else [R.W5_EN, R.W5_ZH]
    for wav in wavs:
        dur = R.wav_dur(wav)
        for _ in range(2):
            R.post_audio(port, wav, mid)
        ts, txt = [], ""
        for _ in range(3):
            el, out, err = R.post_audio(port, wav, mid)
            ts.append(round(el, 3))
            try:
                txt = json.loads(out).get("text", "")
            except Exception:
                txt = out[:120]
            if err:
                print("  err", err, out[:200])
        med = statistics.median(ts)
        rows.append({"wav": os.path.basename(wav), "dur_s": round(dur, 2),
                     "runs_s": ts, "median_s": med, "min_s": min(ts),
                     "rtf": round(med / dur, 3), "text": strip_template(txt)[:120]})
        print("%-24s med %.3fs min %.3fs rtf %.3f | %s"
              % (rows[-1]["wav"], med, min(ts), med / dur,
                 strip_template(txt)[:60]), flush=True)

    R.kill(proc)
    log.close()
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"engine": "llama", "pad30": None, "threads": THREADS,
                   "boot_s": round(time.time() - t0, 1), "rows": rows},
                  f, ensure_ascii=False, indent=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
