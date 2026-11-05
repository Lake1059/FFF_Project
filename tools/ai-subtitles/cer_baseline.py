# CER baseline: bf16 (transformers) vs Q8_0 (llama-server) on the same samples.
#
# Three phases, deliberately separate processes so the GPU never hosts both
# routes at once:
#   --route bf16 : load the HF safetensors model on CUDA, transcribe, save refs
#   --route q8   : POST each file to the running llama-server, save hyps
#   --compare    : character/word-level CER table between the two JSON files
#
# Text normalization strips the model's own "language XX<asr_text>" prefix,
# punctuation and case, so the comparison measures recognition, not formatting.

import argparse
import json
import re
import subprocess
import sys
import unicodedata
from pathlib import Path

PREFIX = re.compile(r"^language\s+[A-Za-z\-]+<asr_text>\s*")
# batch_decode(skip_special_tokens=True) leaves the chat scaffolding in front
# of the ASR payload ("system\n\nuser\n\nassistant\n"); drop it too.
SCAFFOLD = re.compile(r"^\s*(system\s*\n\s*user\s*\n\s*assistant\s*\n)+", re.IGNORECASE)


def normalize(text, is_zh):
    text = SCAFFOLD.sub("", text or "")
    text = PREFIX.sub("", text)
    text = unicodedata.normalize("NFKC", text)
    text = text.lower()
    # Drop punctuation and symbols; keep letters/digits/CJK.
    kept = []
    for ch in text:
        if ch.isspace():
            kept.append(" ")
        elif unicodedata.category(ch)[0] in ("L", "N"):
            kept.append(ch)
    text = "".join(kept)
    text = re.sub(r"\s+", " ", text).strip()
    if is_zh:
        return text.replace(" ", "")
    return text


def edit_distance(a, b):
    # Classic DP; sample texts are short so O(n*m) is fine.
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
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def load_audio(path, target_rate=16000):
    import numpy as np
    import soundfile as sf
    data, rate = sf.read(str(path), dtype="float32", always_2d=True)
    if data.shape[1] > 1:
        data = data.mean(axis=1)
    else:
        data = data[:, 0]
    if rate != target_rate:
        # Linear resample: CER comparison does not need a high-quality path,
        # and this avoids an ffmpeg dependency for the wav samples.
        n = int(round(len(data) * target_rate / rate))
        if n <= 0:
            return np.zeros(0, dtype=np.float32)
        idx = np.linspace(0, len(data) - 1, n)
        data = np.interp(idx, np.arange(len(data)), data).astype(np.float32)
    return data


def route_bf16(model_dir, files, out_json, language=None):
    import torch
    from transformers import AutoModelForMultimodalLM, AutoProcessor

    processor = AutoProcessor.from_pretrained(model_dir)
    model = AutoModelForMultimodalLM.from_pretrained(
        model_dir, torch_dtype=torch.bfloat16, device_map="cuda")
    model.eval()
    results = []
    for f in files:
        audio = load_audio(f)
        kwargs = {"audio": audio}
        if language:
            kwargs["language"] = language
        inputs = processor.apply_transcription_request(**kwargs)
        # The audio encoder's convs run in the model dtype; the processor hands
        # back float32 features, so cast floating tensors to bf16 explicitly
        # (integer ids must stay integer).
        inputs = {
            k: (v.to(device="cuda", dtype=model.dtype)
                if hasattr(v, "is_floating_point") and v.is_floating_point()
                else (v.to("cuda") if hasattr(v, "to") else v))
            for k, v in inputs.items()
        }
        with torch.inference_mode():
            out = model.generate(**inputs, max_new_tokens=256, do_sample=False)
        text = processor.batch_decode(out, skip_special_tokens=True)[0]
        results.append({"file": str(f), "route": "bf16", "text": text})
        print(f"[bf16] {Path(f).name}: {text[:80]}", flush=True)
    Path(out_json).write_text(json.dumps({"results": results}, ensure_ascii=False, indent=2),
        encoding="utf-8")
    print(f"[bf16] wrote {out_json}", flush=True)


def route_q8(url, files, out_json):
    results = []
    for f in files:
        curl = subprocess.run(
            ["curl", "-s", "--max-time", "300", "-X", "POST",
             url.rstrip("/") + "/v1/audio/transcriptions",
             "-F", f"file=@{f}", "-F", "model=qwen3-asr",
             "-F", "response_format=json"],
            capture_output=True, text=True)
        try:
            payload = json.loads(curl.stdout)
            text = payload.get("text", "")
        except Exception:
            text = ""
            print(f"[q8] FAILED to parse response for {f}: {curl.stdout[:200]}", flush=True)
        results.append({"file": str(f), "route": "q8_0", "text": text})
        print(f"[q8 ] {Path(f).name}: {text[:80]}", flush=True)
    Path(out_json).write_text(json.dumps({"results": results}, ensure_ascii=False, indent=2),
        encoding="utf-8")
    print(f"[q8 ] wrote {out_json}", flush=True)


def compare(refs_json, hyps_json):
    refs = {r["file"]: r["text"] for r in json.loads(Path(refs_json).read_text(encoding="utf-8"))["results"]}
    hyps = {r["file"]: r["text"] for r in json.loads(Path(hyps_json).read_text(encoding="utf-8"))["results"]}
    print(f"{'sample':<28} {'CER/WER':>9}  dist/len")
    worst = 0.0
    for f, ref_text in refs.items():
        hyp_text = hyps.get(f, "")
        is_zh = any("\u4e00" <= ch <= "\u9fff" for ch in ref_text) or "_zh" in Path(f).name
        a, b = normalize(ref_text, is_zh), normalize(hyp_text, is_zh)
        dist = edit_distance(a, b)
        cer = dist / max(1, len(a))
        worst = max(worst, cer)
        print(f"{Path(f).name:<28} {cer*100:>8.2f}%  {dist}/{len(a)}")
        if cer > 0.02:
            print(f"    ref: {a[:120]}")
            print(f"    hyp: {b[:120]}")
    print(f"worst = {worst*100:.2f}%  (gate: <=0.5% abs for the Q8_0 baseline)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--route", choices=["bf16", "q8", "compare"], required=True)
    ap.add_argument("--model", default="E:/WorkBuddy/3FP-AI-Subtitles/Qwen3-ASR-0.6B-hf")
    ap.add_argument("--url", default="http://127.0.0.1:8199")
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--language", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--refs", default=None)
    ap.add_argument("--hyps", default=None)
    args = ap.parse_args()

    if args.route == "bf16":
        if not args.files or not args.out:
            sys.exit("bf16 route needs --files and --out")
        route_bf16(args.model, args.files, args.out, args.language)
    elif args.route == "q8":
        if not args.files or not args.out:
            sys.exit("q8 route needs --files and --out")
        route_q8(args.url, args.files, args.out)
    else:
        if not args.refs or not args.hyps:
            sys.exit("compare needs --refs and --hyps")
        compare(args.refs, args.hyps)


if __name__ == "__main__":
    main()
