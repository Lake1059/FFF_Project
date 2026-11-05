#!/usr/bin/env python3
"""Reproducible local benchmark for Qwen3-ASR native Transformers.

This program is intentionally independent from FFF.Player and FFF.Native. It measures
only the model runtime. Live player integration must be evaluated separately.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np
import psutil
import soundfile as sf
import torch
from scipy.signal import resample_poly
from transformers import AutoModelForMultimodalLM, AutoProcessor

TARGET_SAMPLE_RATE = 16_000


@dataclass
class ResourceSample:
    elapsed_seconds: float
    process_cpu_percent: float
    process_rss_bytes: int
    gpu_util_percent: int | None
    gpu_memory_used_mib: int | None


class ResourceSampler:
    def __init__(self, interval_seconds: float = 0.25) -> None:
        self.interval_seconds = interval_seconds
        self.samples: list[ResourceSample] = []
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._process = psutil.Process()
        self._start = 0.0

    def __enter__(self) -> "ResourceSampler":
        self._start = time.perf_counter()
        self._process.cpu_percent(None)
        self._thread = threading.Thread(target=self._run, name="resource-sampler", daemon=True)
        self._thread.start()
        return self

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def _read_gpu(self) -> tuple[int | None, int | None]:
        try:
            completed = subprocess.run(
                [
                    "nvidia-smi",
                    "--query-gpu=utilization.gpu,memory.used",
                    "--format=csv,noheader,nounits",
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=2.0,
            )
            if completed.returncode != 0:
                return None, None
            first_line = completed.stdout.strip().splitlines()[0]
            utilization, memory = (int(part.strip()) for part in first_line.split(",", 1))
            return utilization, memory
        except (OSError, ValueError, IndexError, subprocess.SubprocessError):
            return None, None

    def _run(self) -> None:
        while not self._stop.is_set():
            gpu_util, gpu_memory = self._read_gpu()
            try:
                rss = self._process.memory_info().rss
                cpu = self._process.cpu_percent(None)
            except psutil.Error:
                break
            self.samples.append(
                ResourceSample(
                    elapsed_seconds=time.perf_counter() - self._start,
                    process_cpu_percent=cpu,
                    process_rss_bytes=rss,
                    gpu_util_percent=gpu_util,
                    gpu_memory_used_mib=gpu_memory,
                )
            )
            self._stop.wait(self.interval_seconds)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, help="Local model directory")
    parser.add_argument("--audio", required=True, help="Input WAV/FLAC supported by soundfile")
    parser.add_argument("--output", required=True, help="JSON result path")
    parser.add_argument("--durations", nargs="+", type=float, default=[2.0, 5.0, 10.0])
    parser.add_argument("--language", default=None, help="Force language name/code; omit for auto")
    parser.add_argument("--device", choices=["cuda", "cpu"], default="cuda")
    parser.add_argument("--dtype", choices=["bfloat16", "float16", "float32"], default="bfloat16")
    parser.add_argument("--max-new-tokens", type=int, default=256)
    parser.add_argument("--warmup-seconds", type=float, default=2.0)
    parser.add_argument("--continuous-minutes", type=float, default=0.0)
    parser.add_argument("--continuous-window-seconds", type=float, default=5.0)
    parser.add_argument("--continuous-step-seconds", type=float, default=2.0)
    return parser.parse_args()


def load_audio(path: Path) -> tuple[np.ndarray, int, float]:
    audio, sample_rate = sf.read(path, dtype="float32", always_2d=True)
    mono = audio.mean(axis=1, dtype=np.float32)
    if sample_rate != TARGET_SAMPLE_RATE:
        divisor = math.gcd(sample_rate, TARGET_SAMPLE_RATE)
        mono = resample_poly(
            mono,
            TARGET_SAMPLE_RATE // divisor,
            sample_rate // divisor,
        ).astype(np.float32, copy=False)
    duration = len(mono) / TARGET_SAMPLE_RATE
    return mono, sample_rate, duration


def audio_of_duration(source: np.ndarray, duration_seconds: float) -> np.ndarray:
    requested = max(1, int(round(duration_seconds * TARGET_SAMPLE_RATE)))
    if len(source) >= requested:
        return np.ascontiguousarray(source[:requested])
    repeat_count = math.ceil(requested / len(source))
    return np.ascontiguousarray(np.tile(source, repeat_count)[:requested])


def synchronize(device: str) -> None:
    if device == "cuda":
        torch.cuda.synchronize()


def summarize_resources(samples: list[ResourceSample]) -> dict[str, Any]:
    if not samples:
        return {"sample_count": 0}

    def maximum(attribute: str) -> Any:
        values = [getattr(sample, attribute) for sample in samples]
        concrete = [value for value in values if value is not None]
        return max(concrete) if concrete else None

    return {
        "sample_count": len(samples),
        "peak_process_cpu_percent": maximum("process_cpu_percent"),
        "peak_process_rss_bytes": maximum("process_rss_bytes"),
        "peak_gpu_util_percent": maximum("gpu_util_percent"),
        "peak_gpu_memory_used_mib": maximum("gpu_memory_used_mib"),
        "samples": [asdict(sample) for sample in samples],
    }


def transcribe(
    processor: Any,
    model: Any,
    audio: np.ndarray,
    language: str | None,
    max_new_tokens: int,
    device: str,
) -> dict[str, Any]:
    if device == "cuda":
        torch.cuda.reset_peak_memory_stats()
    inputs = processor.apply_transcription_request(audio=audio, language=language)
    inputs = inputs.to(model.device, model.dtype)
    synchronize(device)
    started = time.perf_counter()
    with ResourceSampler() as sampler, torch.inference_mode():
        output_ids = model.generate(
            **inputs,
            max_new_tokens=max_new_tokens,
            do_sample=False,
        )
        synchronize(device)
    elapsed = time.perf_counter() - started
    generated_ids = output_ids[:, inputs["input_ids"].shape[1] :]
    parsed = processor.decode(generated_ids, return_format="parsed")[0]
    raw = processor.decode(generated_ids)[0]
    result = {
        "processing_seconds": elapsed,
        "first_effective_text_seconds": elapsed,
        "raw": raw,
        "language": parsed.get("language"),
        "text": parsed.get("transcription", ""),
        "resources": summarize_resources(sampler.samples),
    }
    if device == "cuda":
        result["torch_peak_allocated_bytes"] = torch.cuda.max_memory_allocated()
        result["torch_peak_reserved_bytes"] = torch.cuda.max_memory_reserved()
    return result


def run_continuous(
    processor: Any,
    model: Any,
    source: np.ndarray,
    minutes: float,
    window_seconds: float,
    step_seconds: float,
    language: str | None,
    max_new_tokens: int,
    device: str,
) -> dict[str, Any] | None:
    if minutes <= 0:
        return None
    total_audio_seconds = minutes * 60.0
    windows = max(1, math.ceil(max(0.0, total_audio_seconds - window_seconds) / step_seconds) + 1)
    audio_window = audio_of_duration(source, window_seconds)
    backlog = 0.0
    peak_backlog = 0.0
    processing_times: list[float] = []
    errors: list[str] = []
    started = time.perf_counter()
    for index in range(windows):
        try:
            result = transcribe(
                processor,
                model,
                audio_window,
                language,
                max_new_tokens,
                device,
            )
            processing = float(result["processing_seconds"])
            processing_times.append(processing)
            backlog = max(0.0, backlog + processing - step_seconds)
            peak_backlog = max(peak_backlog, backlog)
        except Exception as error:  # noqa: BLE001 - benchmark must record runtime failures
            errors.append(f"window {index}: {type(error).__name__}: {error}")
            break
    elapsed = time.perf_counter() - started
    processed_audio = len(processing_times) * step_seconds
    return {
        "requested_minutes": minutes,
        "window_seconds": window_seconds,
        "step_seconds": step_seconds,
        "requested_windows": windows,
        "completed_windows": len(processing_times),
        "wall_seconds": elapsed,
        "processed_audio_seconds_by_step": processed_audio,
        "aggregate_rtf": (sum(processing_times) / processed_audio) if processed_audio else None,
        "mean_processing_seconds": float(np.mean(processing_times)) if processing_times else None,
        "p95_processing_seconds": float(np.percentile(processing_times, 95)) if processing_times else None,
        "peak_simulated_backlog_seconds": peak_backlog,
        "errors": errors,
    }


def main() -> int:
    args = parse_args()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    model_path = Path(args.model).resolve()
    audio_path = Path(args.audio).resolve()
    dtype = {
        "bfloat16": torch.bfloat16,
        "float16": torch.float16,
        "float32": torch.float32,
    }[args.dtype]
    if args.device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
    if args.device == "cpu" and dtype != torch.float32:
        dtype = torch.float32

    audio, original_sample_rate, source_duration = load_audio(audio_path)
    runtime: dict[str, Any] = {
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "python": sys.version,
        "platform": platform.platform(),
        "torch": torch.__version__,
        "transformers": __import__("transformers").__version__,
        "device_requested": args.device,
        "dtype_requested": args.dtype,
        "cuda_runtime": torch.version.cuda,
        "cuda_device": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
        "model_path": str(model_path),
        "audio_path": str(audio_path),
        "audio_original_sample_rate": original_sample_rate,
        "audio_source_duration_seconds": source_duration,
        "audio_runtime_sample_rate": TARGET_SAMPLE_RATE,
    }

    load_started = time.perf_counter()
    with ResourceSampler() as load_sampler:
        processor = AutoProcessor.from_pretrained(model_path, local_files_only=True)
        model = AutoModelForMultimodalLM.from_pretrained(
            model_path,
            dtype=dtype,
            device_map=("cuda:0" if args.device == "cuda" else "cpu"),
            local_files_only=True,
            low_cpu_mem_usage=True,
        ).eval()
        synchronize(args.device)
    runtime["model_load_seconds"] = time.perf_counter() - load_started
    runtime["model_device"] = str(model.device)
    runtime["model_dtype"] = str(model.dtype)
    runtime["load_resources"] = summarize_resources(load_sampler.samples)

    warmup_audio = audio_of_duration(audio, args.warmup_seconds)
    runtime["warmup"] = transcribe(
        processor,
        model,
        warmup_audio,
        args.language,
        args.max_new_tokens,
        args.device,
    )

    cases: list[dict[str, Any]] = []
    for duration in args.durations:
        case_audio = audio_of_duration(audio, duration)
        result = transcribe(
            processor,
            model,
            case_audio,
            args.language,
            args.max_new_tokens,
            args.device,
        )
        result["audio_seconds"] = duration
        result["rtf"] = result["processing_seconds"] / duration
        result["source_was_repeated"] = duration > source_duration
        cases.append(result)
    runtime["cases"] = cases
    runtime["continuous"] = run_continuous(
        processor,
        model,
        audio,
        args.continuous_minutes,
        args.continuous_window_seconds,
        args.continuous_step_seconds,
        args.language,
        args.max_new_tokens,
        args.device,
    )

    temporary = output_path.with_suffix(output_path.suffix + ".tmp")
    temporary.write_text(json.dumps(runtime, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(temporary, output_path)
    print(json.dumps({"output": str(output_path), "cases": len(cases)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
