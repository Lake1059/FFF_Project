#!/usr/bin/env python3
"""Drive the ICC parser fixture tests.

  1. run icc/make_icc_fixtures.py to (re)generate fixtures into a temp dir
  2. vcvars64 -> cl compile IccParserTests.cpp + IccProfileParser.cpp there
  3. run the exe against the fixtures, tee stdout to --log
  4. exit with the exe's exit code (or the compiler's, if the build fails)

Nothing is written into the repository: build artifacts and fixtures live in a
temp directory. Usage:

    python run_icc_parser_tests.py --log <path> [--fixtures <dir>]
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


def sdk_env() -> tuple[str, str]:
    """Return extra INCLUDE/LIB fragments for the newest Windows SDK.

    vcvars64 on this box leaves the SDK paths out of INCLUDE/LIB, so append
    them explicitly.
    """
    root = Path(r"C:\Program Files (x86)\Windows Kits\10\Include")
    versions = sorted(p for p in root.glob("10.*") if p.is_dir()) if root.exists() else []
    if not versions:
        return "", ""
    ver = versions[-1]
    sdk = ver.parent.parent / "Lib" / ver.name
    inc = ";".join(str(ver / sub) for sub in ("ucrt", "shared", "um", "winrt"))
    lib = ";".join(str(sdk / sub / "x64") for sub in ("ucrt", "um"))
    return inc, lib


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", type=Path, required=True)
    ap.add_argument("--fixtures", type=Path, default=None,
                    help="reuse an existing fixtures dir (else a temp one is generated)")
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    repo = here.parent
    generator = here / "icc" / "make_icc_fixtures.py"
    test_cpp = here / "IccParserTests.cpp"
    parser_cpp = repo / "FFF.Native" / "3FP" / "Core" / "IccProfileParser.cpp"
    include_dir = repo / "FFF.Native"

    work = Path(tempfile.mkdtemp(prefix="icc-parser-tests-"))
    fixtures = args.fixtures or (work / "fixtures")
    fixtures.mkdir(parents=True, exist_ok=True)

    log: list[str] = []

    # --- 1. generate fixtures -------------------------------------------------
    gen = subprocess.run(
        [sys.executable, str(generator), "--out", str(fixtures)],
        capture_output=True, text=True)
    log.append("$ generate fixtures\n" + gen.stdout + gen.stderr)
    if gen.returncode != 0:
        args.log.write_text("\n".join(log), encoding="utf-8")
        print("fixture generation failed; see", args.log)
        return gen.returncode

    # --- 2. compile with MSVC -------------------------------------------------
    vcvars = find_vcvars()
    sdk_inc, sdk_lib = sdk_env()
    exe = work / "icc_parser_tests.exe"
    batch = work / "build.bat"
    batch.write_text(
        "@echo off\r\n"
        f'call "{vcvars}" >nul\r\n'
        f'if defined INCLUDE set "INCLUDE=%INCLUDE%;{sdk_inc}"\r\n'
        f'if defined LIB set "LIB=%LIB%;{sdk_lib}"\r\n'
        f'cl /nologo /std:c++17 /EHsc /W3 /I"{include_dir}" '
        f'"{test_cpp}" "{parser_cpp}" /Fe:"{exe}"\r\n'
        "exit /b %ERRORLEVEL%\r\n",
        encoding="ascii")
    build = subprocess.run(["cmd", "/c", str(batch)], cwd=work,
                           capture_output=True, text=True)
    log.append("$ compile\n" + build.stdout + build.stderr)
    if build.returncode != 0:
        args.log.write_text("\n".join(log), encoding="utf-8")
        print("compile failed (expected until IccProfileParser.cpp lands); see",
              args.log)
        return build.returncode

    # --- 3. run the test exe ---------------------------------------------------
    run = subprocess.run([str(exe), str(fixtures)], capture_output=True, text=True)
    log.append("$ run tests\n" + run.stdout + run.stderr)
    args.log.write_text("\n".join(log), encoding="utf-8")
    print(run.stdout)
    return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
