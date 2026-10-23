# Analyzes AsrTapProbe's asr_tap_dump.bin: verifies the bed description, media
# position continuity and generation stamping of every drained block.
#
# Dump record layout (written by AsrTapProbe.DrainTap, all little-endian):
#   u64 generation, i64 position100ns, u32 frames, u32 sampleRate, u32 channels,
#   i32 sampleFormat, u32 bytesWritten, i32 positions[24], then PCM bytes.

import struct
import sys
from pathlib import Path

HEADER = struct.Struct("<QqIIIiI" + "i" * 24)   # 8+8+4+4+4+4+4+96 = 132 bytes
AV_CHANNEL_NAMES = {
    -1: "none",
    0: "FL", 1: "FR", 2: "FC", 3: "LFE", 4: "BL", 5: "BR",
    6: "FLC", 7: "FRC", 8: "BC", 9: "SL", 10: "SR",
    11: "TC", 12: "TFL", 13: "TFC", 14: "TFR", 15: "TBL",
    16: "TBC", 17: "TBR", 18: "DL", 19: "DR",
    20: "WL", 21: "WR", 22: "SDL", 23: "SDR",
    24: "TSL", 25: "TSR", 26: "BDL", 27: "BDR",
    # AV_CHAN_UNKNOWN (this FFmpeg: sequential enum, not the legacy bitmask).
    28: "UNK",
}
AV_SAMPLE_FMT_NAMES = {0: "U8", 1: "S16", 2: "S32", 3: "FLT", 4: "DBL",
                       5: "U8P", 6: "S16P", 7: "S32P", 8: "FLTP", 9: "DBLP"}


def main(path):
    data = Path(path).read_bytes()
    offset = 0
    blocks = []
    while offset + HEADER.size <= len(data):
        fields = HEADER.unpack_from(data, offset)
        (gen, pos, frames, rate, channels, fmt, nbytes) = fields[:7]
        positions = fields[7:]
        pcm = data[offset + HEADER.size: offset + HEADER.size + nbytes]
        blocks.append(dict(gen=gen, pos=pos, frames=frames, rate=rate,
                           channels=channels, fmt=fmt, nbytes=nbytes,
                           positions=positions, pcm=pcm))
        offset += HEADER.size + nbytes
    if not blocks:
        print("NO BLOCKS in dump")
        return 1

    rates = {b["rate"] for b in blocks}
    chans = {b["channels"] for b in blocks}
    fmts = {b["fmt"] for b in blocks}
    gens = {b["gen"] for b in blocks}
    print(f"blocks={len(blocks)} rates={sorted(rates)} channels={sorted(chans)} "
          f"fmts={[AV_SAMPLE_FMT_NAMES.get(f, f) for f in fmts]} generations={sorted(gens)}")

    first = blocks[0]
    pos_names = [AV_CHANNEL_NAMES.get(p, str(p)) for p in first["positions"][:first["channels"]]]
    print(f"positions: {pos_names}")

    # Position continuity: timestamps must be non-decreasing, and the implied
    # duration (frames/rate) must roughly cover the gap to the next block.
    bad_order = 0
    gaps = []
    prev = None
    for b in blocks:
        if b["pos"] != -(2**63):
            if prev is not None and b["pos"] < prev:
                bad_order += 1
            prev = b["pos"]
    for i in range(1, len(blocks)):
        a, b = blocks[i - 1], blocks[i]
        if a["pos"] == -(2**63) or b["pos"] == -(2**63):
            continue
        implied_end = a["pos"] + round(a["frames"] * 10_000_000 / a["rate"])
        gaps.append(b["pos"] - implied_end)
    gaps.sort()
    if gaps:
        p50 = gaps[len(gaps) // 2]
        print(f"position order violations = {bad_order}; gap-to-next-block(100ns) "
              f"min={gaps[0]} p50={p50} max={gaps[-1]}")

    # PCM sanity: float32 audio must not be all-zero (silence would mean the tap
    # is reading the wrong planes or a dead stream).
    nonzero = 0
    checked = 0
    for b in blocks[:: max(1, len(blocks) // 20)]:
        if b["fmt"] in (3, 8):  # FLT / FLTP
            vals = struct.unpack_from("<" + "f" * (len(b["pcm"]) // 4), b["pcm"])
            checked += 1
            if any(v != 0.0 for v in vals[:256]):
                nonzero += 1
    if checked:
        print(f"non-silent float blocks: {nonzero}/{checked}")

    total_seconds = sum(b["frames"] for b in blocks) / max(1, first["rate"])
    total_bytes = sum(b["nbytes"] for b in blocks)
    print(f"total audio: {total_seconds:.2f}s, {total_bytes} bytes "
          f"({total_bytes / max(1, total_seconds) / 1024:.0f} KiB/s)")
    ok = (bad_order == 0 and len(rates) == 1 and len(chans) == 1 and checked == 0 or nonzero == checked)
    print("DUMP-OK" if ok else "DUMP-SUSPECT")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "asr_tap_dump.bin"))
