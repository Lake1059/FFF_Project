"""Build first with /p:SecurityRegressionTests=true; runs only synthetic media."""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def bmp(path, width, height):
    stride = (width * 3 + 3) & ~3
    size = stride * height
    with path.open("wb") as stream:
        stream.write(struct.pack("<2sIHHI", b"BM", 54 + size, 0, 0, 54))
        stream.write(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, size, 2835, 2835, 0, 0))
        row = bytes(stride)
        for _ in range(height):
            stream.write(row)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--dll", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    dll = args.dll or root / "FFF.Native/x64/Release/FFF.Native.dll"
    directories = [root / "runtime", root / "third_party/vcpkg_installed/x64-windows/bin", dll.parent]
    handles = [os.add_dll_directory(str(path)) for path in directories]
    os.environ["FFF3FP_PREFETCH_HOST"] = "0"
    with tempfile.TemporaryDirectory(prefix="3fp-security-media-") as folder:
        media = Path(folder)
        for name, w, h in [("small.bmp", 64, 64), ("4k.bmp", 3840, 2160), ("8k.bmp", 7680, 4320)]:
            bmp(media / name, w, h)
        for name, codec in [("small.png", "png"), ("small.jpg", "mjpeg"), ("small.webp", "libwebp"),
                            ("small.tiff", "tiff"), ("small.avif", "libaom-av1")]:
            subprocess.run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-i", str(media / "small.bmp"),
                            "-frames:v", "1", "-c:v", codec, str(media / name)], check=True, timeout=30)
        subprocess.run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=64x64:rate=25", "-t", "1", "-c:v", "libx264", "-g", "25",
                        "-pix_fmt", "yuv420p", str(media / "seek.mp4")], check=True, timeout=30)
        native = ctypes.CDLL(str(dll))
        run = native.FFF3FP_RunSecurityRegression
        run.argtypes = [ctypes.c_char_p]
        run.restype = ctypes.c_int
        result = run(str(media).encode("utf-8"))
    for handle in handles:
        handle.close()
    return result


if __name__ == "__main__":
    raise SystemExit(main())
