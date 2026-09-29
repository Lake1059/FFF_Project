#!/usr/bin/env python3
"""Regenerate 3FP/Render/ShaderBytecode.h from the HLSL sources embedded in
VideoRenderer.cpp.

Why this exists
---------------
The playback pixel shaders are consumed as **precompiled DXBC** (ShaderBytecode.h)
so nothing compiles HLSL at runtime. That also means editing the HLSL string in
VideoRenderer.cpp has no effect until this script is run. In particular the P3
gamut switch (ResolveSourceGamut -> Gamut == 2 -> P3To709) only becomes live
after a regeneration: an old array paired with a tri-state resolver silently
routes P3 through the Rec.2020 matrix.

Usage
-----
    python tools/generate_shader_bytecode.py                  # auto-detect fxc
    python tools/generate_shader_bytecode.py --fxc <path>     # explicit compiler
    python tools/generate_shader_bytecode.py --dry-run        # list what it would do

Requirements: the Windows SDK HLSL compiler (fxc.exe). It ships with
"Windows Kits\\10\\bin\\<version>\\x64\\fxc.exe"; the script probes the newest
installed version if --fxc is omitted.

The generated file is written next to the source header and must be reviewed
like any other generated artefact: the kernel links ShaderBytecode.h directly,
so a wrong array silently changes what every frame looks like.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

# Paths are resolved relative to this file so the generator lives inside the kernel
# repository: ShaderBytecode.h is a 1.1 MB generated artefact that the kernel links
# directly, so "how was this produced" has to be answerable from a clean checkout.
TREE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_CPP = os.path.join(
    TREE_ROOT, "FFF.Native", "3FP", "Render", "VideoRenderer.cpp"
)
OUTPUT_HEADER = os.path.join(
    TREE_ROOT, "FFF.Native", "3FP", "Render", "ShaderBytecode.h"
)

# (HLSL constant in VideoRenderer.cpp, generated array name, shader profile)
SHADERS = [
    ("VertexShaderSource", "FFFVertexShaderBytecode", "vs_5_0"),
    ("PixelShaderSource", "FFFPixelShaderBytecode", "ps_5_0"),
    ("ScalePixelShaderSource", "FFFScalePixelShaderBytecode", "ps_5_0"),
    ("CoverBackdropPixelShaderSource", "FFFCoverBackdropPixelShaderBytecode", "ps_5_0"),
    ("TimedTextPixelShaderSource", "FFFTimedTextPixelShaderBytecode", "ps_5_0"),
    ("TimedTextSpriteVertexShaderSource", "FFFTimedTextSpriteVertexShaderBytecode", "vs_5_0"),
    ("TimedTextSpritePixelShaderSource", "FFFTimedTextSpritePixelShaderBytecode", "ps_5_0"),
]

DECLARATION = re.compile(
    r'constexpr\s+const\s+char\*\s+' + r'(\w+)' + r'\s*=\s*R"\((.*?)\)";',
    re.DOTALL,
)


def extract_sources(cpp_text: str) -> dict[str, str]:
    found = {name: body for name, body in DECLARATION.findall(cpp_text)}
    missing = [name for name, _, _ in SHADERS if name not in found]
    if missing:
        raise SystemExit(
            "VideoRenderer.cpp does not declare: " + ", ".join(missing)
            + "\n(the raw-string declarations changed; update SHADERS/DECLARATION)"
        )
    return found


def find_fxc() -> str:
    pattern = r"C:\Program Files (x86)\Windows Kits\10\bin\*\x64\fxc.exe"
    candidates = sorted(glob.glob(pattern))
    if not candidates:
        raise SystemExit(
            "fxc.exe not found under Windows Kits\\10\\bin. Pass --fxc <path> "
            "(the Windows SDK HLSL compiler is required)."
        )
    return candidates[-1]  # newest SDK version sorts last


def compile_shader(fxc: str, hlsl: str, profile: str, workdir: str) -> bytes:
    source_path = os.path.join(workdir, "shader.hlsl")
    object_path = os.path.join(workdir, "shader.cso")
    with open(source_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(hlsl)
    command = [
        fxc,
        "/nologo",
        "/T", profile,
        "/O3",
        "/Fo", object_path,
        source_path,
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(object_path):
        raise SystemExit(
            f"fxc failed for {profile}:\n{result.stdout}\n{result.stderr}"
        )
    with open(object_path, "rb") as handle:
        return handle.read()


def format_array(name: str, blob: bytes) -> str:
    # Reproduce the committed layout exactly: six values per line, the first column
    # right-aligned in width 7, the rest in width 4, a comma after every value except
    # the array's last one, and one trailing space on every line that is followed by
    # another. The ragged widths are why this is spelled out instead of a single
    # format string. Getting it wrong rewrites all 24k lines of the header for a
    # one-shader change, which makes the generated blob unreviewable in a patch.
    lines = [f"const BYTE {name}[] =", "{"]
    last = len(blob) - 1
    for offset in range(0, len(blob), 6):
        parts = []
        for index, byte in enumerate(blob[offset:offset + 6]):
            position = offset + index
            parts.append(f"{byte:{7 if position % 6 == 0 else 4}d}")
            if position != last:
                parts.append(",")
        line = "".join(parts)
        if position != last:
            line += " "
        lines.append(line)
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fxc", help="path to fxc.exe (auto-detected by default)")
    parser.add_argument("--dry-run", action="store_true",
                        help="only report what would be regenerated")
    parser.add_argument("--source", default=SOURCE_CPP)
    parser.add_argument("--output", default=OUTPUT_HEADER)
    args = parser.parse_args()

    with open(args.source, "r", encoding="utf-8") as handle:
        sources = extract_sources(handle.read())

    if args.dry_run:
        for name, array, profile in SHADERS:
            print(f"{name} -> {array} ({profile}), {len(sources[name])} bytes of HLSL")
        return 0

    fxc = args.fxc or find_fxc()
    print(f"fxc: {fxc}")

    blocks = []
    with tempfile.TemporaryDirectory() as workdir:
        for name, array, profile in SHADERS:
            blob = compile_shader(fxc, sources[name], profile, workdir)
            print(f"{array}: {len(blob)} bytes ({profile})")
            blocks.append(format_array(array, blob))

    header = "#pragma once\n\n#include <d3d11.h>\n\n"
    header += (
        "// Generated at build preparation time with the Windows SDK HLSL compiler.\n"
        "// Keep these arrays in the Native binary so playback never compiles HLSL at runtime.\n"
        "// Regenerate with tools/generate_shader_bytecode.py after editing the HLSL in\n"
        "// VideoRenderer.cpp.\n\n"
    )
    header += "\n\n".join(blocks) + "\n"

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
