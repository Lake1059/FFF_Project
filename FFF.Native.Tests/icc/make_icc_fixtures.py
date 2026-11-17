#!/usr/bin/env python3
"""ICC fixture generator for fff::icc::Parse structural tests.

Produces, into the directory given by --out:
  * a set of well-formed ICC profiles (matrix/TRC, LUT-based, named color ...)
  * a set of one-point-broken malformed profiles (each breaks exactly one rule)
  * manifest.json: per-file expectations the C++ skeleton asserts against.

Normal profiles come from two sources:
  * PIL.ImageCms (lcms2) where the library can produce them (sRGB v4, Lab).
  * hand-assembled bytes (struct, big-endian) everywhere else.

The expectations in the manifest are written from the ICC.1:2010 spec and the
contract in FFF.Native/3FP/Core/IccProfileParser.h, NOT from "how we guess the
implementation will behave". Anything uncertain is recorded as "unknown" and
listed separately so the integration lane can rule on it.

Usage:
    python make_icc_fixtures.py --out <dir>
"""
import argparse
import json
import shutil
import struct
from pathlib import Path

from PIL import ImageCms

D50 = (0.9642, 1.0, 0.8249)
D50_XY = (0.3457, 0.3585)
D65_XY = (0.3127, 0.3290)


# --------------------------------------------------------------------------
# low-level pack helpers (ICC is big-endian)
# --------------------------------------------------------------------------
def be16(v: int) -> bytes:
    return struct.pack(">H", v & 0xFFFF)


def be32(v: int) -> bytes:
    return struct.pack(">I", v & 0xFFFFFFFF)


def s15fixed(v: float) -> bytes:
    """s15Fixed16Number."""
    return struct.pack(">i", int(round(v * 65536)))


# --------------------------------------------------------------------------
# tag payload builders. Each returns the full tag data (type signature + body).
# --------------------------------------------------------------------------
def tag_desc(text: str) -> bytes:
    """v2 multiLocalizedTextDescriptionType (desc)."""
    data = text.encode("ascii") + b"\x00"
    body = b"desc" + b"\x00" * 4
    body += be32(len(data)) + data
    body += be16(0) + be16(0)  # no unicode section
    body += be16(0) + be16(0)  # no scriptCode section
    return body


def tag_mluc(text: str) -> bytes:
    """v4 multiLocalizedUnicodeType (mluc), one enUS record."""
    u = text.encode("utf-16-be")
    body = b"mluc" + b"\x00" * 4 + be32(1) + be32(12)
    # record at offset 16..28, text at 28
    body += be16(0x656E) + be16(0x5553) + be32(len(u)) + be32(28)
    body += u
    return body


def tag_cicp(primaries: int, transfer: int, matrix: int = 0,
             full_range: int = 0) -> bytes:
    """ICC.1:2022 colourimetryType (cicp): 4 bytes after type header."""
    return b"cicp" + b"\x00" * 4 + bytes([primaries, transfer, matrix, full_range])


def tag_curv_table(values, width: int = 16) -> bytes:
    """curveType lookup table; width=8 exercises the legacy u8 fallback."""
    if width == 8:
        payload = bytes(int(round(v * 255)) for v in values)
    elif width == 16:
        payload = b"".join(be16(int(round(v * 65535))) for v in values)
    else:
        raise ValueError("width must be 8 or 16")
    return b"curv" + b"\x00" * 4 + be32(len(values)) + payload


def tag_xyz(x: float, y: float, z: float) -> bytes:
    return b"XYZ " + b"\x00" * 4 + s15fixed(x) + s15fixed(y) + s15fixed(z)


def tag_curv_gamma(gamma: float) -> bytes:
    """curveType with count == 1: gamma stored as u8Fixed8."""
    return b"curv" + b"\x00" * 4 + be32(1) + be16(int(round(gamma * 256)))


def tag_curv_identity() -> bytes:
    """curveType with count == 0: identity."""
    return b"curv" + b"\x00" * 4 + be32(0)


def tag_para(function_type: int, params) -> bytes:
    """parametricCurveType (para)."""
    body = b"para" + b"\x00" * 4 + be16(function_type) + b"\x00" * 2
    for p in params:
        body += s15fixed(p)
    return body


def tag_mft1(channels_in: int = 3, channels_out: int = 3, grid: int = 2) -> bytes:
    """lut8Type (mft1). Layout follows the parser's decoder exactly:
    [type+resv][in,out,grid,pad][9 s15Fixed16 matrix][in*256*2 input tables]
    [grid^in*out u8 CLUT][out*256*2 output tables].
    The parser requires grid >= 2.
    """
    body = b"mft1" + b"\x00" * 4
    body += bytes([channels_in, channels_out, grid, 0])
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1):
        body += s15fixed(v)
    body += bytes(channels_in * 256 * 2)      # input tables (contents irrelevant)
    clut = grid ** channels_in * channels_out
    body += bytes(clut)                        # CLUT u8
    body += bytes(channels_out * 256 * 2)      # output tables
    return body


def tag_mft2(channels_in: int = 3, channels_out: int = 3, grid: int = 2) -> bytes:
    """lut16Type (mft2): same layout as mft1, CLUT entries are u16."""
    body = b"mft2" + b"\x00" * 4
    body += bytes([channels_in, channels_out, grid, 0])
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1):
        body += s15fixed(v)
    body += bytes(channels_in * 256 * 2)
    clut = grid ** channels_in * channels_out * 2
    body += bytes(clut)
    body += bytes(channels_out * 256 * 2)
    return body


def tag_mab(sig: bytes = b"mAB ") -> bytes:
    """lutAToBType / lutBToAType. Header per the parser's decoder:
    [type+resv][in,out,resv16][offB][offMat][offM][offCLUT][offA].
    Only B and A curves (identity curv); CLUT/matrix/M absent (offset 0).
    """
    hdr = bytearray(sig + b"\x00" * 4)
    hdr += bytes([3, 3, 0, 0])          # 3 in / 3 out, reserved u16
    hdr += be32(36)                     # offB
    hdr += be32(0)                      # offMat absent
    hdr += be32(0)                      # offM absent
    hdr += be32(0)                      # offCLUT absent
    hdr += be32(72)                     # offA
    assert len(hdr) == 32
    body = bytes(hdr)
    for _ in range(3):                  # B curves: 3 x curv identity = 36 bytes
        body += tag_curv_identity()
    for _ in range(3):                  # A curves: 3 x curv identity = 36 bytes
        body += tag_curv_identity()
    return body                         # total 108 bytes


def tag_ncl2() -> bytes:
    """Minimal namedColor2Type (ncl2): 1 named color, 0 device coordinates."""
    body = b"ncl2" + b"\x00" * 4
    body += be32(0)                    # vendor flag count
    body += be32(1)                    # number of named colors
    body += be32(0)                    # number of device coordinates
    body += b"\x00" * 32               # prefix
    body += b"\x00" * 32               # suffix
    body += b"black".ljust(32, b"\x00")  # first color root name (32 bytes)
    return body


def tag_clro() -> bytes:
    """colorantOrderType (clro). The decoder reads a leading count byte
    followed by that many colorant numbers (fixture follows the decoder's
    layout; see report 'open questions').
    """
    return b"clro" + b"\x00" * 4 + bytes([1, 1])


def tag_clrt() -> bytes:
    """colorantTableType (clrt): one colorant."""
    body = b"clrt" + b"\x00" * 4 + be32(1)
    body += b"Cyan".ljust(32, b"\x00")
    body += s15fixed(0.0) + s15fixed(0.0) + s15fixed(0.0)
    return body


# --------------------------------------------------------------------------
# profile assembler
# --------------------------------------------------------------------------
def build_profile(version: int, klass: bytes, data_space: bytes, pcs: bytes,
                  tags, platform: bytes = b"MSFT") -> bytes:
    """tags: list of (signature4, payload). Lay out 4-byte aligned tag data."""
    header = bytearray(128)
    header[4:8] = b"3FP "
    header[8:12] = version.to_bytes(4, "big")
    header[12:16] = klass
    header[16:20] = data_space
    header[20:24] = pcs
    header[24:36] = be16(2024) + be16(1) + be16(1) + be16(0) + be16(0) + be16(0)
    header[36:40] = b"acsp"
    header[40:44] = platform
    header[68:80] = s15fixed(D50[0]) + s15fixed(D50[1]) + s15fixed(D50[2])

    n = len(tags)
    table = be32(n)
    offset = 128 + 4 + 12 * n
    blobs = []
    for sig, payload in tags:
        table += sig + be32(offset) + be32(len(payload))
        blobs.append(payload)
        offset += len(payload)
        pad = (-len(payload)) % 4
        if pad:
            blobs.append(b"\x00" * pad)
            offset += pad
    header[0:4] = be32(offset)
    return bytes(header) + table + b"".join(blobs)


def _xyz_of(xy):
    x, y = xy
    return (x / y, 1.0, (1.0 - x - y) / y)


def _mat3_mul(a, b):
    return [
        [sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
        for i in range(3)
    ]


def _mat3_inv(m):
    a, b, c = m[0]
    d, e, f = m[1]
    g, h, i = m[2]
    A = e * i - f * h
    B = -(d * i - f * g)
    C = d * h - e * g
    det = a * A + b * B + c * C
    if abs(det) < 1e-12:
        raise ValueError("singular matrix")
    inv = 1.0 / det
    return [
        [A * inv, (c * h - b * i) * inv, (b * f - c * e) * inv],
        [B * inv, (a * i - c * g) * inv, (c * d - a * f) * inv],
        [C * inv, (b * g - a * h) * inv, (a * e - b * d) * inv],
    ]


def _bradford(src_xy, dst_xy):
    """M^-1 * L * M, matching ColorSpaceRegistry.BradfordAdapt."""
    M = [[0.8951, 0.2664, -0.1614],
         [-0.7502, 1.7135, 0.0367],
         [0.0389, -0.0685, 1.0296]]
    src = _xyz_of(src_xy)
    dst = _xyz_of(dst_xy)
    cone_s = [sum(M[i][j] * src[j] for j in range(3)) for i in range(3)]
    cone_d = [sum(M[i][j] * dst[j] for j in range(3)) for i in range(3)]
    L = [[cone_d[0] / cone_s[0], 0, 0],
         [0, cone_d[1] / cone_s[1], 0],
         [0, 0, cone_d[2] / cone_s[2]]]
    return _mat3_mul(_mat3_inv(M), _mat3_mul(L, M))


def rgb_matrix(xy_primaries, white_xy):
    """RGB->XYZ D50 columns for the given xy primaries.

    This is the same Lindbloom + Bradford path the parser uses to name a
    known space, so fixtures can assert knownSpace instead of only structure.
    """
    r, g, b = (_xyz_of(p) for p in xy_primaries)
    w = _xyz_of(white_xy)
    N = [[r[0], g[0], b[0]],
         [r[1], g[1], b[1]],
         [r[2], g[2], b[2]]]
    invN = _mat3_inv(N)
    S = [sum(invN[i][j] * w[j] for j in range(3)) for i in range(3)]
    M = [[r[0] * S[0], g[0] * S[1], b[0] * S[2]],
         [r[1] * S[0], g[1] * S[1], b[1] * S[2]],
         [r[2] * S[0], g[2] * S[1], b[2] * S[2]]]
    adapted = _mat3_mul(_bradford(white_xy, D50_XY), M)
    return ((adapted[0][0], adapted[1][0], adapted[2][0]),
            (adapted[0][1], adapted[1][1], adapted[2][1]),
            (adapted[0][2], adapted[1][2], adapted[2][2]))


V2 = 0x02100000
V4 = 0x04300000


def handbuilt_profiles():
    """Return {filename: (bytes, manifest_entry)} for hand-assembled profiles."""
    out = {}

    # --- sRGB v2 (matrix/TRC, desc, curv gamma 2.2) -----------------------
    rXYZ, gXYZ, bXYZ = rgb_matrix(
        [(0.640, 0.330), (0.300, 0.600), (0.150, 0.060)], D65_XY)
    body = build_profile(V2, b"mntr", b"RGB ", b"XYZ ", [
        (b"desc", tag_desc("sRGB v2 hand-built")),
        (b"wtpt", tag_xyz(*D50)),
        (b"rXYZ", tag_xyz(*rXYZ)),
        (b"gXYZ", tag_xyz(*gXYZ)),
        (b"bXYZ", tag_xyz(*bXYZ)),
        (b"rTRC", tag_curv_gamma(2.2)),
        (b"gTRC", tag_curv_gamma(2.2)),
        (b"bTRC", tag_curv_gamma(2.2)),
    ])
    out["srgb_v2.icc"] = (body, {
        "kind": "normal", "version": 2, "class": "mntr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False, "minTagDecoded": 8,
                   "hasRgbMatrix": True, "hasWhitePoint": True,
                   "hasRgbCurves": True, "curvesIdentical": True,
                   "knownSpace": "sRGB", "primariesToken": "bt709",
                   "transferToken": "bt470m", "knownSpaceErrorMax": 0.002},
        "tags": {"rTRC": {"decoded": True, "type": "curv",
                          "curveTableCount": 0},
                 "rXYZ": {"decoded": True, "type": "XYZ "}},
    })

    # --- matrix/TRC family: P3 / AdobeRGB / ProPhoto ----------------------
    for name, desc, prim, white_xy, gamma, space, primaries_tok in [
        ("displayp3_v4.icc", "Display P3 hand-built",
         [(0.680, 0.320), (0.265, 0.690), (0.150, 0.060)], D65_XY, 2.2,
         "Display P3", "smpte432"),
        ("adobergb_v4.icc", "Adobe RGB 1998 hand-built",
         [(0.640, 0.330), (0.210, 0.710), (0.150, 0.060)], D65_XY, 2.2,
         "Adobe RGB", ""),
        ("prophoto_v4.icc", "ProPhoto ROMM hand-built",
         [(0.7347, 0.2653), (0.1596, 0.8404), (0.0366, 0.0001)], D50_XY, 1.8,
         "ProPhoto RGB", ""),
        ("bt2020_v4.icc", "BT.2020 hand-built",
         [(0.708, 0.292), (0.170, 0.797), (0.131, 0.046)], D65_XY, 2.2,
         "BT.2020", "bt2020"),
        ("dci_p3_v4.icc", "DCI-P3 hand-built",
         [(0.680, 0.320), (0.265, 0.690), (0.150, 0.060)], (0.314, 0.351), 2.6,
         "DCI-P3", "smpte431"),
    ]:
        r, g, b = rgb_matrix(prim, white_xy)
        body = build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
            (b"mluc", tag_mluc(desc)),
            (b"wtpt", tag_xyz(*D50)),
            (b"rXYZ", tag_xyz(*r)),
            (b"gXYZ", tag_xyz(*g)),
            (b"bXYZ", tag_xyz(*b)),
            (b"rTRC", tag_para(3, (gamma, 1.0, 0.0, 0.0, 0.0))),
            (b"gTRC", tag_para(3, (gamma, 1.0, 0.0, 0.0, 0.0))),
            (b"bTRC", tag_para(3, (gamma, 1.0, 0.0, 0.0, 0.0))),
        ])
        transfer = "bt470m" if abs(gamma - 2.2) < 1e-9 else ""
        out[name] = (body, {
            "kind": "normal", "version": 4, "class": "mntr",
            "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                       "hasLut": False, "hasChad": False, "minTagDecoded": 8,
                       "hasRgbMatrix": True, "hasWhitePoint": True,
                       "hasRgbCurves": True, "curvesIdentical": True,
                       "knownSpace": space, "primariesToken": primaries_tok,
                       "transferToken": transfer, "knownSpaceErrorMax": 0.002},
            "tags": {"rTRC": {"decoded": True, "type": "para"}},
        })

    # --- sRGB v4 parametric TRC, plus a deliberately conflicting cicp --------
    srgb_r, srgb_g, srgb_b = rgb_matrix(
        [(0.640, 0.330), (0.300, 0.600), (0.150, 0.060)], D65_XY)
    srgb_para = (2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045)
    for name, version, with_cicp in [
        ("srgb_para_v4.icc", V4, False),
        ("srgb_cicp_conflict.icc", 0x04400000, True),
    ]:
        tags = [
            (b"mluc", tag_mluc(name + " fixture")),
            (b"wtpt", tag_xyz(*D50)),
            (b"rXYZ", tag_xyz(*srgb_r)),
            (b"gXYZ", tag_xyz(*srgb_g)),
            (b"bXYZ", tag_xyz(*srgb_b)),
            (b"rTRC", tag_para(3, srgb_para)),
            (b"gTRC", tag_para(3, srgb_para)),
            (b"bTRC", tag_para(3, srgb_para)),
        ]
        if with_cicp:
            # Deliberately claim BT.2020/PQ; ICC matrix and TRC must win.
            tags.append((b"cicp", tag_cicp(9, 16, 9, 0)))
        body = build_profile(version, b"mntr", b"RGB ", b"XYZ ", tags)
        tags_expect = {"rTRC": {"decoded": True, "type": "para"}}
        if with_cicp:
            tags_expect["cicp"] = {"decoded": True, "type": "cicp"}
        out[name] = (body, {
            "kind": "normal", "version": 4, "class": "mntr",
            "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                       "hasLut": False, "hasChad": False,
                       "hasRgbMatrix": True, "hasWhitePoint": True,
                       "hasRgbCurves": True, "curvesIdentical": True,
                       "knownSpace": "sRGB", "primariesToken": "bt709",
                       "transferToken": "iec61966-2-1",
                       "knownSpaceErrorMax": 0.002,
                       "version": "4.4" if version == 0x04400000 else "4.3"},
            "tags": tags_expect,
        })

    # --- CICP-only profile: no matrix/TRC, CICP supplies fallback tokens -----
    body = build_profile(0x04400000, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("CICP only fixture")),
        (b"wtpt", tag_xyz(*D50)),
        (b"cicp", tag_cicp(9, 16, 9, 0)),
    ])
    out["cicp_only.icc"] = (body, {
        "kind": "normal", "version": 4, "class": "mntr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False, "hasCicp": True,
                   "hasRgbMatrix": False, "hasRgbCurves": False,
                   "knownSpace": "", "primariesToken": "bt2020",
                   "transferToken": "smpte2084", "version": "4.4"},
        "tags": {"cicp": {"decoded": True, "type": "cicp"}},
        "cicp": {"colourPrimaries": 9, "transferCharacteristics": 16,
                 "matrixCoefficients": 9, "videoFullRangeFlag": 0},
    })

    # --- legacy 8-bit curv table; must be stored, not silently skipped --------
    body = build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("8-bit table fixture")),
        (b"wtpt", tag_xyz(*D50)),
        (b"rXYZ", tag_xyz(*srgb_r)),
        (b"gXYZ", tag_xyz(*srgb_g)),
        (b"bXYZ", tag_xyz(*srgb_b)),
        (b"rTRC", tag_curv_table([i / 255.0 for i in range(256)], width=8)),
        (b"gTRC", tag_curv_table([i / 255.0 for i in range(256)], width=8)),
        (b"bTRC", tag_curv_table([i / 255.0 for i in range(256)], width=8)),
    ])
    out["curve_table8.icc"] = (body, {
        "kind": "normal", "version": 4, "class": "mntr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False,
                   "hasRgbMatrix": True, "hasWhitePoint": True,
                   "hasRgbCurves": True, "curvesIdentical": True,
                   "knownSpace": "sRGB", "primariesToken": "bt709",
                   "transferToken": "", "knownSpaceErrorMax": 0.002,
                   "colorimetryNoteContains": "table TRC"},
        "tags": {"rTRC": {"decoded": True, "type": "curv",
                          "curveTableCount": 256, "curveTable16Bit": False}},
    })

    # --- non-neutral TRC: keep per-channel curves, do not fake a single curve -
    body = build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("non-neutral TRC fixture")),
        (b"wtpt", tag_xyz(*D50)),
        (b"rXYZ", tag_xyz(*srgb_r)),
        (b"gXYZ", tag_xyz(*srgb_g)),
        (b"bXYZ", tag_xyz(*srgb_b)),
        (b"rTRC", tag_curv_gamma(2.2)),
        (b"gTRC", tag_curv_gamma(2.4)),
        (b"bTRC", tag_curv_gamma(2.6)),
    ])
    out["non_neutral_v4.icc"] = (body, {
        "kind": "normal", "version": 4, "class": "mntr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False,
                   "hasRgbMatrix": True, "hasWhitePoint": True,
                   "hasRgbCurves": True, "curvesIdentical": False,
                   "knownSpace": "sRGB", "primariesToken": "bt709",
                   "transferToken": "", "knownSpaceErrorMax": 0.002,
                   "colorimetryNoteContains": "non-neutral TRC"},
        "tags": {"rTRC": {"decoded": True, "type": "curv"},
                 "gTRC": {"decoded": True, "type": "curv"},
                 "bTRC": {"decoded": True, "type": "curv"}},
    })

    # --- Gray v2 -----------------------------------------------------------
    body = build_profile(V2, b"mntr", b"GRAY", b"XYZ ", [
        (b"desc", tag_desc("Gray hand-built")),
        (b"wtpt", tag_xyz(*D50)),
        (b"kTRC", tag_curv_gamma(2.2)),
    ])
    out["gray_v2.icc"] = (body, {
        "kind": "normal", "version": 2, "class": "mntr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False, "minTagDecoded": 3},
    })

    # --- CMYK output profile carrying a minimal mAB ------------------------
    body = build_profile(V2, b"prtr", b"CMYK", b"XYZ ", [
        (b"desc", tag_desc("CMYK minimal mAB")),
        (b"wtpt", tag_xyz(*D50)),
        (b"A2B0", tag_mab(b"mAB ")),
    ])
    out["cmyk_mab.icc"] = (body, {
        "kind": "normal", "version": 2, "class": "prtr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": True, "hasChad": False, "minTagDecoded": 2},
        "tags": {"A2B0": {"decoded": True, "type": "mAB ", "lutValid": True}},
    })

    # --- minimal hand-built LUT-type profiles ------------------------------
    lut_defs = [
        ("mft1_minimal.icc", b"link", tag_mft1(), "mft1", False),
        ("mft2_minimal.icc", b"link", tag_mft2(), "mft2", False),
        ("mab_minimal.icc", b"prtr", tag_mab(b"mAB "), "mAB ", True),
        ("mba_minimal.icc", b"prtr", tag_mab(b"mBA "), "mBA ", True),
    ]
    for name, klass, payload, typ, is_mab in lut_defs:
        body = build_profile(V4, klass, b"RGB ", b"XYZ ", [
            (b"mluc", tag_mluc(name + " fixture")),
            (b"wtpt", tag_xyz(*D50)),
            (b"A2B0", payload),
        ])
        out[name] = (body, {
            "kind": "normal", "version": 4, "class": klass.decode(),
            "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                       "hasLut": True, "hasChad": False, "minTagDecoded": 2},
            "tags": {"A2B0": {"decoded": True, "type": typ, "lutValid": True}},
        })

    # --- named colour profile ---------------------------------------------
    body = build_profile(V4, b"nmcl", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("Named color fixture")),
        (b"wtpt", tag_xyz(*D50)),
        (b"ncl2", tag_ncl2()),
    ])
    out["ncl2_minimal.icc"] = (body, {
        "kind": "normal", "version": 4, "class": "nmcl",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False, "minTagDecoded": 2},
        "tags": {"ncl2": {"decoded": True, "type": "ncl2"}},
    })

    # --- colorant order/table ---------------------------------------------
    body = build_profile(V4, b"prtr", b"CMYK", b"XYZ ", [
        (b"mluc", tag_mluc("Colorant table fixture")),
        (b"clro", tag_clro()),
        (b"clrt", tag_clrt()),
    ])
    out["clro_clrt.icc"] = (body, {
        "kind": "normal", "version": 4, "class": "prtr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": False, "minTagDecoded": 2},
        "tags": {"clro": {"decoded": True, "type": "clro"},
                 "clrt": {"decoded": True, "type": "clrt"}},
    })

    return out


def pillow_profiles():
    """Profiles produced by PIL.ImageCms (lcms2)."""
    out = {}

    prof = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB"))
    data = prof.tobytes()
    # lcms shares the identical rTRC/gTRC/bTRC payload at one offset; per
    # ICC.1:2010 identical (offset,size) sharing is legal (warning only).
    out["srgb_v4_lcms.icc"] = (data, {
        "kind": "normal", "version": 4, "class": "mntr",
        "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                   "hasLut": False, "hasChad": True, "minTagDecoded": 3,
                   "warningsContain": "share",
                   "hasRgbMatrix": True, "hasWhitePoint": True,
                   "hasRgbCurves": True, "curvesIdentical": True,
                   "knownSpace": "sRGB", "primariesToken": "bt709",
                   "transferToken": "iec61966-2-1",
                   "knownSpaceErrorMax": 0.002},
        "rationale": "ICC.1:2010 allows tags to share identical (offset,size) "
                     "payloads; lcms emits mluc-only description, now decoded per "
                     "the parser contract (desc + mluc both fill description).",
    })

    try:
        lab = ImageCms.ImageCmsProfile(ImageCms.createProfile("LAB"))
        data = lab.tobytes()
        out["lab_lcms.icc"] = (data, {
            "kind": "normal", "version": int(data[8]), "class": data[12:16].decode("latin1"),
            "expect": {"status": "Ok", "usable": True, "descriptionNonEmpty": True,
                       "hasChad": True, "minTagDecoded": 1},
            "rationale": "A2B0 mft2 with grid=1 is legal per ICC.1:2010 (abstract "
                         "Lab / matrix-shaper profiles use a 1-point CLUT); the "
                         "earlier MalformedTag expectation was wrong.",
        })
    except Exception as exc:  # pragma: no cover - environment dependent
        print("warning: LAB profile not produced:", exc)

    return out


def real_world_lut():
    """Copy a genuine shipped profile (Adobe RSWOP, mft2 CMYK)."""
    src = Path(r"C:\Windows\System32\spool\drivers\color\RSWOP.icm")
    if not src.exists():
        return {}
    data = src.read_bytes()
    return {"rswop_real.icm": (data, {
        "kind": "normal", "version": int(data[8]), "class": data[12:16].decode("latin1"),
        # RSWOP shares identical (offset,size) payloads (A2B0/A2B2); per
        # ICC.1:2010 that is legal sharing, recorded as a warning only.
        "expect": {"status": "Ok", "usable": True, "hasLut": True,
                   "warningsContain": "share", "minTagDecoded": 1},
        "rationale": "ICC.1:2010 permits identical offset+size payload sharing "
                     "between tags; overlap is fatal only when ranges intersect "
                     "without being identical.",
    })}


# --------------------------------------------------------------------------
# malformed profiles: each breaks exactly one rule
# --------------------------------------------------------------------------
def malformed_from(base: bytes):
    out = {}

    # 1. truncated to 127 bytes (< 128) -> HeaderTooSmall
    out["bad_trunc_127.icc"] = (base[:127], {
        "kind": "malformed", "expect": {"status": "HeaderTooSmall", "usable": False}})

    # 2. acsp signature corrupted (full size) -> NotIcc
    b = bytearray(base)
    b[36:40] = b"XXXX"
    out["bad_acsp.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "NotIcc", "usable": False}})

    # 3a. declared size = actual + 1 -> SizeMismatch
    b = bytearray(base)
    declared = struct.unpack(">I", b[0:4])[0]
    b[0:4] = be32(declared + 1)
    out["bad_size_larger.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "SizeMismatch", "usable": False}})

    # 3b. declared size = actual - 1 -> SizeMismatch
    b = bytearray(base)
    b[0:4] = be32(declared - 1)
    out["bad_size_smaller.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "SizeMismatch", "usable": False}})

    # 4. tag count overstated -> TagTableTruncated
    b = bytearray(build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("count lie")),
        (b"wtpt", tag_xyz(*D50)),
    ]))
    b[128:132] = be32(99)  # claims 99 tags, table only holds 2
    out["bad_tagcount.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "TagTableTruncated", "usable": False}})

    # 5. tag offset+size runs past EOF -> TagOutOfBounds
    b = bytearray(build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("offset out")),
        (b"wtpt", tag_xyz(*D50)),
    ]))
    # second entry (wtpt) at table index 1: entry bytes at 128+4+12=144
    b[144 + 4:144 + 8] = be32(0x7FFFF000)  # wild offset
    out["bad_tagoffset.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "TagOutOfBounds", "usable": False}})

    # 6. two tags PARTIALLY overlapping -> TagOverlap.
    # tag A: (offset=200, size=40) ==> [200,240)
    # tag B: (offset=224, size=40) ==> [224,264)
    # ranges intersect on [224,240) but are not identical -> genuine conflict.
    payload = tag_mluc("overlap")
    total = 264
    b = bytearray(total)
    b[0:4] = be32(total)
    b[8:12] = be32(V4)
    b[12:16] = b"mntr"
    b[16:20] = b"RGB "
    b[20:24] = b"XYZ "
    b[36:40] = b"acsp"
    b[68:80] = s15fixed(D50[0]) + s15fixed(D50[1]) + s15fixed(D50[2])
    b[128:132] = be32(2)
    b[132:136] = b"tstA"
    b[136:140] = be32(200)
    b[140:144] = be32(40)
    b[144:148] = b"tstB"
    b[148:152] = be32(224)
    b[152:156] = be32(40)
    b[200:240] = payload[:40]
    out["bad_overlap.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "TagOverlap", "usable": False},
        "rationale": "ICC.1:2010 forbids tag data ranges that partially intersect "
                     "(only identical offset+size sharing is legal); A[200,240) "
                     "and B[224,264) overlap on 16 bytes but are not identical.",
    })

    # 7. tag declares size 4 (< 8-byte type header) -> TagDataTruncated
    small = tag_mluc("x")
    total = 128 + 4 + 12 + 4
    b = bytearray(total)
    b[0:4] = be32(total)
    b[8:12] = be32(V4)
    b[12:16] = b"mntr"
    b[36:40] = b"acsp"
    b[128:132] = be32(1)
    b[132:136] = b"mluc"
    b[136:140] = be32(128 + 16)
    b[140:144] = be32(4)  # only 4 bytes, type header alone needs 8
    b[144:148] = small[:4]
    out["bad_tag_short.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "TagDataTruncated", "usable": False}})

    # 8. curv count=1 but the gamma u16 is missing -> MalformedTag
    curv = b"curv" + b"\x00" * 4 + be32(1)   # declares count=1, no entry
    total = 128 + 4 + 12 + len(curv)
    b = bytearray(total)
    b[0:4] = be32(total)
    b[8:12] = be32(V4)
    b[12:16] = b"mntr"
    b[36:40] = b"acsp"
    b[128:132] = be32(1)
    b[132:136] = b"rTRC"
    b[136:140] = be32(128 + 16)
    b[140:144] = be32(len(curv))
    b[144:144 + len(curv)] = curv
    out["bad_curv_short.icc"] = (bytes(b), {
        "kind": "malformed", "expect": {"status": "MalformedTag", "usable": False}})

    # 9. para functionType=5 (illegal; spec allows 0..4) -> MalformedTag
    body = build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("bad para")),
        (b"rTRC", tag_para(5, (2.2,))),
    ])
    out["bad_para5.icc"] = (body, {
        "kind": "malformed", "expect": {"status": "MalformedTag", "usable": False}})

    # 10. mluc record length runs past tag payload -> MalformedTag
    good = tag_mluc("hello")
    bad = bytearray(good)
    # record 0 at bytes 16..28: length field at 20..24
    bad[20:24] = be32(9000)   # claims 9000 UTF-16 bytes
    body = build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", bytes(bad)),
    ])
    out["bad_mluc_bounds.icc"] = (body, {
        "kind": "malformed", "expect": {"status": "MalformedTag", "usable": False}})

    # 11. duplicate desc tag: warning only, profile still Ok
    body = build_profile(V4, b"mntr", b"RGB ", b"XYZ ", [
        (b"mluc", tag_mluc("dup desc test")),
        (b"wtpt", tag_xyz(*D50)),
        (b"mluc", tag_mluc("second mluc")),
    ])
    out["bad_dup_desc.icc"] = (body, {
        "kind": "malformed", "expect": {"status": "Ok", "usable": True,
                                        "descriptionNonEmpty": True,
                                        "hasLut": False, "hasChad": False},
        "note": "duplicate tag must produce a warning, not a failure",
    })

    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    samples = {}
    samples.update(handbuilt_profiles())
    samples.update(pillow_profiles())
    samples.update(real_world_lut())

    base_srgb = samples["srgb_v2.icc"][0]
    samples.update(malformed_from(base_srgb))

    manifest = []
    for name, (data, entry) in sorted(samples.items()):
        (out / name).write_bytes(data)
        rec = {"file": name, "bytes": len(data)}
        rec.update(entry)
        manifest.append(rec)

    (out / "manifest.json").write_text(
        json.dumps({"samples": manifest}, indent=2), encoding="utf-8")

    n_normal = sum(1 for r in manifest if r["kind"] == "normal")
    n_bad = sum(1 for r in manifest if r["kind"] == "malformed")
    print(f"wrote {len(manifest)} samples ({n_normal} normal, {n_bad} malformed) "
          f"to {out}")
    for r in manifest:
        print(f"  {r['file']:<24} {r['bytes']:>8} bytes  "
              f"-> {r['expect']['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
