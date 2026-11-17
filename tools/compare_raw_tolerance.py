#!/usr/bin/env python3
"""Compare two BGRA32 raw still frames with an explicit pixel tolerance.

This is the WebP/JPEG still-frame acceptance gate requested by the 2026-11-18
audit (finding 3FP-AUDIT-2026-11-18-013). It intentionally works on raw
frames, not encoded files: the point is to catch a colour-pipeline change that
is small per channel but visible in aggregate.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def read_bgra(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) % 4:
        raise ValueError(f"{path}: size {len(data)} is not a multiple of 4")
    return data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument(
        "--profile", choices=("ab", "external"), default="ab",
        help="ab = two 3FP outputs; external = 3FP vs FFmpeg/libjpeg reference")
    parser.add_argument("--max-mean-delta", type=float, default=None)
    parser.add_argument("--max-pixel-delta", type=int, default=None)
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    args = parser.parse_args()

    if args.max_mean_delta is None:
        args.max_mean_delta = 1.0 if args.profile == "ab" else 2.0
    if args.max_pixel_delta is None:
        args.max_pixel_delta = 64 if args.profile == "ab" else 80

    actual = read_bgra(args.actual)
    reference = read_bgra(args.reference)
    if len(actual) != len(reference):
        print(f"FAIL: size mismatch actual={len(actual)} reference={len(reference)}")
        return 2
    if args.width and args.height:
        expected = args.width * args.height * 4
        if len(actual) != expected:
            print(f"FAIL: {args.width}x{args.height} expects {expected} bytes, got {len(actual)}")
            return 2

    n = len(actual) // 4
    channel_sum = [0, 0, 0]
    channel_max = [0, 0, 0]
    pixels_different = 0
    for i in range(0, len(actual), 4):
        different = False
        for c in range(3):
            delta = abs(actual[i + c] - reference[i + c])
            channel_sum[c] += delta
            if delta > channel_max[c]:
                channel_max[c] = delta
            if delta:
                different = True
        if different:
            pixels_different += 1

    mean = [s / n for s in channel_sum]
    print(f"pixels={n} different={pixels_different} ({100.0 * pixels_different / n:.2f}%)")
    print(f"mean_abs={mean[0]:.3f}/{mean[1]:.3f}/{mean[2]:.3f} max={channel_max[0]}/{channel_max[1]}/{channel_max[2]}")

    # ab compares two 3FP outputs and is the regression gate (mean 1.0 LSB,
    # single pixel 64 LSB). external compares a 3FP output against an FFmpeg/
    # libjpeg decode, where chroma-site and IDCT differences are expected; its
    # defaults (2.0 / 80) keep that reference useful without misclassifying a
    # known codec difference as a colour-pipeline regression.
    failed = False
    for c in range(3):
        if mean[c] > args.max_mean_delta:
            print(f"FAIL: mean channel {c} {mean[c]:.3f} > {args.max_mean_delta}")
            failed = True
        if channel_max[c] > args.max_pixel_delta:
            print(f"FAIL: max channel {c} {channel_max[c]} > {args.max_pixel_delta}")
            failed = True
    if failed:
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
