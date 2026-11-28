#!/usr/bin/env python3
"""Build and run the seek-skip regression against the release DLL.

The runner creates its own small media corpus with ffmpeg, compiles
SeekSkipTests.cpp in a temp directory, and runs the public API. It writes
nothing into the repository.
"""
import argparse
import glob
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def find_vcvars() -> Path:
    patterns = [
        r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files (x86)\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat",
    ]
    for pattern in patterns:
        hits = sorted(glob.glob(pattern))
        if hits:
            return Path(hits[0])
    raise SystemExit("vcvars64.bat not found under Program Files")


def run(cmd: list[str], cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, capture_output=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--d3d12", action="store_true",
                        help="also exercise the real D3D12VA decoder and renderer")
    args = parser.parse_args()
    if not args.ffmpeg:
        raise SystemExit("--ffmpeg is required")

    root = Path(__file__).resolve().parent.parent
    include_dir = root / "FFF.Native"
    source = Path(__file__).resolve().parent / "SeekSkipTests.cpp"
    release_dir = root / "FFF.Native" / "x64" / "Release"
    dll = args.dll or release_dir / "FFF.Native.dll"
    runtime_dirs = [
        release_dir,
        root / "third_party" / "ffmpeg" / "bin" / "x64",
        root / "third_party" / "vcpkg_installed" / "x64-windows" / "bin",
    ]
    log: list[str] = []

    with tempfile.TemporaryDirectory(prefix="3fp_seek_skip_") as folder:
        work = Path(folder)
        media = work / "media"
        media.mkdir()
        ffmpeg = Path(args.ffmpeg)

        def make(name: str, *extra: str) -> None:
            cmd = [str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y", *extra, str(media / name)]
            result = run(cmd, work)
            log.append("$ " + " ".join(cmd) + "\n" + result.stdout + result.stderr)
            if result.returncode != 0:
                raise SystemExit(f"fixture {name} failed")

        make("h264.mp4", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=30,format=yuv420p",
             "-t", "60", "-c:v", "libx264", "-preset", "ultrafast", "-crf", "32",
             "-g", "60", "-bf", "2", "-x264-params", "scenecut=0")
        make("interlaced.mp4", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=30,format=yuv420p",
             "-t", "10", "-flags", "+ildct+ilme", "-c:v", "libx264", "-preset", "ultrafast",
             "-crf", "32", "-g", "60", "-bf", "2", "-x264-params", "tff=1")
        make("hevc_open.mp4", "-f", "lavfi", "-i", "testsrc2=size=3840x2160:rate=30,format=yuv420p",
             "-t", "10", "-c:v", "libx265", "-preset", "ultrafast", "-crf", "32",
             "-x265-params", "open-gop=1:keyint=60:min-keyint=60:scenecut=0")

        exe = work / "SeekSkipTests.exe"
        batch = work / "build.bat"
        batch.write_text(
            "@echo off\r\n"
            'set "PATH=C:\\Windows\\System32;C:\\Windows"\r\n'
            f'call "{find_vcvars()}" >nul\r\n'
            f'cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I"{include_dir}" '
            f'"{source}" /Fe:"{exe}" /link /LIBPATH:"{release_dir}" FFF.Native.lib\r\n'
            "exit /b %ERRORLEVEL%\r\n",
            encoding="ascii")
        build = run(["cmd", "/c", str(batch)], work)
        log.append("$ compile\n" + build.stdout + build.stderr)
        if build.returncode != 0:
            (work / "seek_skip_tests.log").write_text("\n".join(log), encoding="utf-8")
            print(build.stdout + build.stderr)
            return build.returncode

        env = os.environ.copy()
        env["PATH"] = ";".join(str(path) for path in runtime_dirs) + ";" + env.get("PATH", "")
        run_args = [str(exe), str(media)]
        if args.d3d12:
            run_args.append("--d3d12")
        run_result = subprocess.run(run_args, cwd=work, env=env,
                                    text=True, capture_output=True)
        log.append("$ run\n" + run_result.stdout + run_result.stderr)
        (work / "seek_skip_tests.log").write_text("\n".join(log), encoding="utf-8")
        print(run_result.stdout)
        if run_result.returncode != 0:
            print(run_result.stderr)
        return run_result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
