# Private (Vendor) GainMap Pipeline — Design Notes

Date: 2026-11-15
Worktree: `C:\PLAN\3FCompare\_wt_gainmap` (branch `feat/private-gainmap`, base `17bcd13`)
Module: `FFF.Native/3FP/Core/GainMapProbe.{h,cpp}`
CLI: `FFF.Native/tools/gainmap_probe.cpp` → `gainmap_probe.exe`

## 1. Goal

Recognize the private HDR gain-map carriers that phone cameras embed in HEIC/AVIF
still images, decode the gain-map, and mathematically lift the SDR base to linear
HDR. Fail closed on anything untrusted. Deliver a scriptable probe that emits one
line of JSON so the host and CI can classify a file without opening the player.

## 2. Carrier detection (container walk)

A single ISO-BMFF box walk over `ftyp/meta`:

- `ftyp`: record `major_brand` and `compatible_brands`. `tmap` in compatible brands
  is a hint but **not** proof — we only commit after finding the derived item.
- `meta/iprp/ipco`: collect `hvcC`/`av1C` codecs, `ispe` dimensions, `auxC`
  URN strings, `colr`.
- `meta/iinf`: map item-id → item-type (`hvc1`, `av01`, `tmap`, `grid`, `Exif`,
  `mime`, ...).
- `meta/iloc`: locate each item's extent (extents, construction method 1 = `idat`).
- `meta/iref`: follow `dimg` from the `tmap` item to its gain-map item, and
  `auxl` from the primary image to the auxiliary gain-map item.

### `form` classification

| form             | condition |
|------------------|-----------|
| `tmap`           | ISO 21496-1 `tmap` item whose dimg points at a single coded image item |
| `tmap_grid`      | `tmap` dimg points at a `grid` derived item (multi-tile gain-map) |
| `auxc_apple`     | `auxC` URN `urn:com:apple:photo:2020:aux:hdrgainmap`, single item |
| `auxc_vendor`    | unregistered `auxC` URN that looks like a gain-map (gated by XMP), single item |
| `auxc_grid`      | Apple/vendor `auxC` whose `auxl` points at a `grid` derived item |
| `none`           | no gain-map carrier found |

`auxC` URN routing: contains `21496` → ISO; contains `hdrgainmap`/`hdr-gain-map`/
`hdr_gain_map` → Apple; `auxid:1`/`auxiliary:alpha` → alpha (ignored);
`depth/disparity/mask/segment/matte/thumbnail` → ignored; anything else →
vendor candidate (kind=4), only accepted if the Apple HDRGainMap XMP also exists.

### Grid payload

`u8 version(=0) u8 flags u8 rowsMinus1 u8 colsMinus1`.
- `flags & 1 != 0`: followed by `u32 grid_width u32 grid_height`.
- `flags & 1 == 0` (DJI / MiHE private): the tail still carries **2×u16 w/h**.
  We read them but only accept a value if it falls within
  `cols * tile_w × rows * tile_h`; otherwise we keep tile-derived dimensions and
  note the tail was ignored.

The gain-map grid tiles are decoded each as a separate Annex-B / OBU buffer, then
stitch into one image in row-major order (tile `(r,c)` occupies
`[c*tw .. (c+1)*tw) × [r*th .. (r+1)*th)`).

## 3. Metadata resolution (ordered fallback)

1. **ISO 21496-1 tmap payload** — `u8 version(=0)` then `GainMapMetadata`:
   `u16 min_version u16 writer_version u8 flags` (bit7 = 3-channel,
   bit3 = common_denominator, bit2 = backward-compatible). If common_denom:
   `u32 denom, 2×u32 headroom, channels*5×u32 (min/max per channel × {Sdr, Hdr})`.
   Otherwise per-field num/den pairs. Yields per-channel `offsetSdr`,
   `offsetHdr`, `logBoostMin`, `logBoostMax`.
2. **Exif MakerNote** (Apple): Exif item → TIFF head → IFD0 tag 0x8769 →
   ExifIFD tag 0x927C → MakerNote. MN starts with `"Apple iOS\0"`; IFD at
   MN+14, entry offsets relative to MN start. tag 0x0021 = HDRHeadroom,
   tag 0x0030 = HDRGain (type 5 RATIONAL64 or type 11 FLOAT32). Stops →
   headroom via the reference's two-piece linear fit; **0/0 is treated as
   invalid and falls through to XMP** (never as 3.48×).
3. **XMP**: scan the Exif/XMP item for `<HDRGainMap:HDRGainMapHeadroom>` (float
   in log2) and/or Apple `ns.apple.com/HDRGainMap` attributes. Vendor `auxC`
   acceptance requires both the Apple XMP namespace and `HDRGainMapHeadroom`.

A 0/0 metadata set is rejected at every step. If all sources are absent or
invalid, `meta.family = none` and the result is `fail_closed_malformed` with
reason "no credible gain metadata".

## 4. Decoding

- **Base**: FFmpeg opens the whole file and picks the first video stream. (For
  grid HEIC this picks one tile; the probe only needs a representative base to
  prove the math — the player's own `AvifTileGridComposer` assembles the real
  canvas. This is a known probe simplification, not a player regression.)
- **Gain-map**: the raw item bytes are converted from HEVC length-prefixed
  (`hvcC` lengthSize + VPS/SPS/PPS param sets prepended) to Annex-B start codes,
  then fed to FFmpeg through an in-memory `avio` context. AV1 raw OBU items are
  fed as-is. Output is converted to packed RGBA float via `sws_scale` → RGB24
  then /255 (the GBRPF32 sws path heap-corrupted on this build; RGB24 is the
  safe path).
- Channels are detected from pixels: if R/G/B planes differ meaningfully, the
  gain-map is treated as 3-channel; otherwise 1-channel (luma).

## 5. Apply math

### ISO family (`family = iso`)
Per channel, per pixel:
```
gain = gain_norm           # 0..1 from the decoded gain-map
t = pow(gain, 1/gamma)
logBoost = lerp(logBoostMin[c], logBoostMax[c], t)
hdr = (sdr_linear + offsetSdr[c]) * 2^logBoost - offsetHdr[c]
```
`sdr_linear = sRGB_to_linear(sdr)`.

### Apple 0-base family (`family = apple`)
```
g = Rec709Inverse(gain)      # v<0.081 ? v/4.5 : ((v+0.099)/1.099)^(1/0.45)
hdr = sdr_linear * (1 + (headroom-1) * g)
```

### 128-base vendor family (`family = vendor128`)
Detected when the gain-map p0.5×255 ≥ 16: min=-h, max=+h, gamma=1, and the
ISO family formula is reused with those bounds. p0.5×255 ≤ 8 → Apple 0-base.
Intermediate values are treated as 0-base but a note is recorded.

### Stats (real, computed on the applied pixels)
- `p50`: median linear luma after apply.
- `maxLinear`: max linear luma.
- `over1Pct`: percentage of pixels whose linear luma exceeds 1.0 (the SDR
  white point) — this is the empirical proof that HDR lift happened.

## 6. Fail-closed contract

| verdict                  | when |
|--------------------------|------|
| `applied`                | gain-map decoded **and** math ran on real pixels |
| `decoded_not_applied`    | gain-map decoded but apply skipped (--base-only or unsupported correction) |
| `fail_closed_unsupported`| carrier recognized but codec/feature unsupported |
| `fail_closed_malformed`  | carrier recognized but container/bytes/metadata untrusted; `reason` is human-readable |
| `not_gainmap`            | no gain-map carrier (plain SDR, alpha, JXL, PQ AVIF, ...) |

We never return "support" for a malformed carrier. A malformed gain-map does not
promote the file to HDR and does not change the existing still decode path.

## 7. 3FP integration (this round)

- `PlayerSession::DoOpen`, after the still-image size check: if
  `staticImage_` and the container is a still container, run
  `GainMapProbe::Probe(path, /*baseOnly=*/true)` and store the result in
  `gainmapInfo_`.
- `PlayerSession::RebuildMediaInfo` publishes a `gainmap` object
  (`present/form/verdict/headroom/reason`) in the media-info JSON the host
  already consumes.
- `DoClose` resets `gainmapInfo_`.
- The host (FFF.Player VB.NET) is not changed this round; it compiles against
  the unchanged ABI.

## 8. Support matrix (as shipped)

| carrier | decode channels | metadata source | apply | verdict on samples |
|---------|-----------------|-----------------|-------|--------------------|
| tmap single item (HEVC) | 1 & 3 | tmap ISO payload | ISO family | applied |
| tmap grid (flags=1, in-payload w/h) | 1 & 3 | tmap ISO payload | ISO family | applied |
| auxC Apple URN, grid | 1 & 3 | XMP (Apple headroom) | Apple 0-base | applied |
| auxC vendor (128-base), grid | 1 | XMP headroom | ISO family | applied |
| auxC Apple URN, no XMP/MakerNote/tmap | — | none | — | fail_closed_malformed |
| tmap but broken Annex-B (fakehevc) | — | — | — | fail_closed_malformed |
| plain HEIC / PQ AVIF / alpha / JXL | — | — | — | not_gainmap |

## 9. Open items / not closed this round

1. **Per-pixel AVFrame integration in the renderer.** The probe proves the math
   on its own decoded base, but the player's already-decoded `AVFrame` (from
   `AvifTileGridComposer` / the video decoder) is not yet lifted by the gain-map
   before presentation. This requires scaling the gain-map to the canvas size and
   running the math in linear space inside the present path; detection and
   metadata advertisement are wired, pixel application is not.
2. **3-channel gain-map 3×3 channel correction.** The reference implementation
   itself lists this as unfinished; we treat 3-channel maps as per-channel
   independent and do not apply the cross-channel 3×3 matrix.
3. **Real-world vendor private sample.** The remembered DJI_1_0927_D.heic was
   not on disk this round; only synthetic Apple/vendor-grid samples were tested.
   `flags=0` tail w/h parsing is implemented but untested against a real
   `flags=0` device file.
4. **AV1 gain-map decode** is wired (raw OBU → avio) but no AV1 gain-map sample
   was available in the corpus.
5. **JXL gain-map** (ISO JPEG XL gain-map) is out of scope — the probe returns
   `not_gainmap` for `.jxl` because it is not ISO-BMFF; the reference has a
   separate `JxlGainMapProbe` we did not port.
6. **Base decode in the probe** picks the first video stream (one tile for grid
   files). This is fine for proving apply stats but the player uses the assembled
   canvas; when item 1 lands, apply on the canvas, not a re-decode.

## 10. Build / reproduce

- Third-party shim lives at `_wt_gainmap/third_party/` (junctions into the shared
  `C:\PLAN\3FCompare\third_party` and `3fp\build\libraw_x3f_src`).
- DLL:
  `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
   FFF.Native\FFF.Native.vcxproj /p:Configuration=Release /p:Platform=x64 /m`
  → `FFF.Native\x64\Release\FFF.Native.dll`, 0 errors.
- Probe CLI: `FFF.Native\tools\build_probe.bat` (cl.exe direct link against the
  same FFmpeg/libav libraries), runtime DLLs copied next to the exe.
- Usage: `gainmap_probe.exe <file> [--dump <dir>] [--base-only]`.
  stdout is one JSON line; stderr is diagnostic only.
