#!/usr/bin/env python3
"""Drive the memory budget arithmetic tests (no Windows runtime needed for tests).

  1. vcvars64 -> cl compile MembudgetMathTests.cpp (header-only, no .cpp to link)
     AND PlacementPolicyTests.cpp (self-contained header-only dedup checks)
  2. run both exes, tee stdout to --log
  3. exit non-zero if either exe fails (or the compiler does)

Nothing is written into the repository: build artifacts live in a temp directory.

    python FFF.Native.Tests/run_membudget_math_tests.py --log <path>
    python FFF.Native.Tests/run_membudget_math_tests.py --log <path> --table
        (build the same exe, then run it in --table mode: the 5-tier x 3-occupancy
         comparison table, with this host's real physical RAM appended)
"""
import argparse
import ctypes
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


def host_phys_mib() -> int:
    """Total physical RAM of THIS machine, in MiB (same source as the product)."""
    class MEMORYSTATUSEX(ctypes.Structure):
        _fields_ = [
            ("dwLength", ctypes.c_ulong),
            ("dwMemoryLoad", ctypes.c_ulong),
            ("ullTotalPhys", ctypes.c_ulonglong),
            ("ullAvailPhys", ctypes.c_ulonglong),
            ("ullTotalPageFile", ctypes.c_ulonglong),
            ("ullAvailPageFile", ctypes.c_ulonglong),
            ("ullTotalVirtual", ctypes.c_ulonglong),
            ("ullAvailVirtual", ctypes.c_ulonglong),
            ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
        ]
    status = MEMORYSTATUSEX()
    status.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
    return int(status.ullTotalPhys // (1024 * 1024))


def build_one(work: Path, vcvars: Path, include_dir: Path,
              src: Path, exe: Path, log: list[str]) -> int:
    batch = work / (exe.stem + "_build.bat")
    batch.write_text(
        "@echo off\r\n"
        f'call "{vcvars}" >nul\r\n'
        f'cl /nologo /std:c++17 /EHsc /W4 /I"{include_dir}" '
        f'"{src}" /Fe:"{exe}"\r\n'
        "exit /b %ERRORLEVEL%\r\n",
        encoding="ascii")
    build = subprocess.run(["cmd", "/c", str(batch)], cwd=work,
                           capture_output=True, text=True)
    log.append(f"$ compile {src.name}\n" + build.stdout + build.stderr)
    return build.returncode


def run_one(exe: Path, argv: list[str], title: str, log: list[str]) -> int:
    run = subprocess.run([str(exe)] + argv, capture_output=True, text=True)
    log.append(f"$ run {title}\n" + run.stdout + run.stderr)
    print(run.stdout)
    return run.returncode


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", type=Path, required=True)
    ap.add_argument("--table", action="store_true",
                    help="run the comparison-table mode instead of the math test suite")
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    repo = here.parent
    include_dir = repo / "FFF.Native"
    vcvars = find_vcvars()

    work = Path(tempfile.mkdtemp(prefix="membudget-math-tests-"))
    log: list[str] = []
    rc = 0

    # 1) MembudgetMathTests (header-only math + cap slot).
    math_exe = work / "membudget_math_tests.exe"
    rc = build_one(work, vcvars, include_dir,
                   here / "MembudgetMathTests.cpp", math_exe, log)
    if rc != 0:
        args.log.write_text("\n".join(log), encoding="utf-8")
        print("compile failed; see", args.log)
        return rc
    if args.table:
        host_mib = host_phys_mib()
        rc = run_one(math_exe, ["--table", str(host_mib)],
                     f"--table hostPhysMiB={host_mib}", log)
    else:
        rc = run_one(math_exe, [], "tests", log)
    if rc != 0:
        args.log.write_text("\n".join(log), encoding="utf-8")
        return rc

    # 2) PlacementPolicyTests: the SMT-brother dedup checks (self-contained
    # header-only; used to be a hand-run cl command, so CI never saw it).
    place_exe = work / "placement_policy_tests.exe"
    rc = build_one(work, vcvars, include_dir,
                   here / "PlacementPolicyTests.cpp", place_exe, log)
    if rc != 0:
        args.log.write_text("\n".join(log), encoding="utf-8")
        print("compile failed; see", args.log)
        return rc
    rc = run_one(place_exe, [], "placement dedup", log)
    args.log.write_text("\n".join(log), encoding="utf-8")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
