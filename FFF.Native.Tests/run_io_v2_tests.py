#!/usr/bin/env python3
"""Compile and run the I/O 2.0 contract tests without the Windows runtime.

The headers under test are deliberately Windows-free; this runner only needs
vcvars64 for cl.exe and a temp directory, and writes nothing into the repo.
"""
import argparse
import glob
import subprocess
import sys
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent / "FFF.Native"
    include_dir = root
    source = root.parent / "FFF.Native.Tests" / "IoV2Tests.cpp"
    log: list[str] = []
    with tempfile.TemporaryDirectory(prefix="fff_io_v2_") as tmp:
        work = Path(tmp)
        exe = work / "IoV2Tests.exe"
        batch = work / "build.bat"
        batch.write_text(
            "@echo off\r\n"
            'set "PATH=C:\\Windows\\System32;C:\\Windows"\r\n'
            f'call "{find_vcvars()}" >nul\r\n'
            f'cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I"{include_dir}" '
            f'"{source}" /Fe:"{exe}"\r\n'
            "exit /b %ERRORLEVEL%\r\n",
            encoding="ascii")
        build = subprocess.run(["cmd", "/c", str(batch)], cwd=work,
                               capture_output=True, text=True)
        log.append("$ compile\n" + build.stdout + build.stderr)
        if build.returncode != 0:
            Path(args.log).write_text("\n".join(log), encoding="utf-8")
            print(build.stdout + build.stderr)
            return build.returncode
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        log.append("$ run\n" + run.stdout + run.stderr)
        Path(args.log).write_text("\n".join(log), encoding="utf-8")
        print(run.stdout)
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
