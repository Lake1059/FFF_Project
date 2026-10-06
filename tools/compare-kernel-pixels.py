"""Bound compiler-rounding differences to one output quantization step."""
import array
import math
import sys

left, right = array.array("f"), array.array("f")
with open(sys.argv[1], "rb") as handle:
    left.frombytes(handle.read())
with open(sys.argv[2], "rb") as handle:
    right.frombytes(handle.read())
if not left or len(left) != len(right):
    raise SystemExit("Pixel buffer sizes differ or are empty")
bits = int(sys.argv[3])
tolerance = 1.0 / ((1 << bits) - 1) + 1e-7 if bits in (8, 10) else 0.0
differences = [abs(a - b) for a, b in zip(left, right)]
maximum = max(differences)
changed = sum(value != 0 for value in differences)
print(f"channels={len(left)} changed={changed} max_diff={maximum:.10g} tolerance={tolerance:.10g}")
if any(not math.isfinite(value) for value in differences) or maximum > tolerance:
    raise SystemExit(1)
