# Converts an AsrTapProbe dump (asr_tap_dump.bin) into a plain wav, so the same
# real playback audio can be fed to both ASR routes for the CER baseline.
# The dump's blocks are already contiguous media-time PCM in the decoded bed's
# own format (planar float), so this is a header rewrite plus plane interleave
# for soundfile.

import struct
import sys
from pathlib import Path

HEADER = struct.Struct("<QqIIIiI" + "i" * 24)


def main(dump_path, wav_path):
    data = Path(dump_path).read_bytes()
    offset = 0
    rate = channels = fmt = None
    chunks = []
    while offset + HEADER.size <= len(data):
        f = HEADER.unpack_from(data, offset)
        (gen, pos, frames, rate, channels, fmt, nbytes) = f[:7]
        pcm = data[offset + HEADER.size: offset + HEADER.size + nbytes]
        chunks.append((frames, pcm))
        offset += HEADER.size + nbytes
    if not chunks:
        print("no blocks")
        return 1
    if fmt != 8:  # AV_SAMPLE_FMT_FLTP only; extend when another bed shows up
        print(f"unsupported sample format {fmt} (only FLTP=8 handled)")
        return 1
    import numpy as np
    import soundfile as sf
    total_frames = sum(c[0] for c in chunks)
    out = np.zeros((total_frames, channels), dtype=np.float32)
    written = 0
    plane_bytes = None
    for frames, pcm in chunks:
        plane_bytes = frames * 4
        for ch in range(channels):
            plane = np.frombuffer(pcm[ch * plane_bytes:(ch + 1) * plane_bytes],
                                  dtype="<f4")
            out[written:written + frames, ch] = plane
        written += frames
    sf.write(wav_path, out, rate)
    print(f"wrote {wav_path}: {total_frames} frames, {channels}ch, {rate} Hz, "
          f"{total_frames / rate:.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "tap_audio.wav"))
