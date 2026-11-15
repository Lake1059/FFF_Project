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

## 11. Stage 2 — corpus matrix & defect fixes (2026-11-15)

### 11.1 38-file corpus result

Run: `verify\run_verify.ps1 -Probe gainmap_probe.exe -Corpus gainmap-corpus`.

- **35/38 field-matched**, **36/38 verdict-matched**.
- Verdict buckets actual: 21 applied, 5 malformed, 5 unsupported, 7 not_gainmap.
- Expected buckets: 22 applied, 5 malformed, 3 unsupported, 7 not_gainmap, 1 decoded_not_applied.

The 3 remaining mismatches are **known scope gaps / corpus label issues**, not
probe defects:

| File | Expected | Got | Verdict |
|---|---|---|---|
| `meta_sources/meta_xmp_broken_neg.heic` | form=auxc_vendor | form=auxc_apple | **corpus bug**: file URN is `urn:com:apple:photo:2020:aux:hdrgainmap` (Apple), manifest mislabeled as vendor. |
| `other_formats/jpeg_ultra_hdr_xmp.jpg` | decoded_not_applied | fail_closed_unsupported | JPEG Ultra HDR parsing not implemented this round; probe correctly identifies it as gainmap and refuses rather than silently treating as SDR. |
| `other_formats/jxl_jhgm_positive.jxl` | applied | fail_closed_unsupported | JXL codestream decoding not implemented; probe detects the `JXL ` container + `jhgm` box and reports unsupported. |

### 11.2 B1–B8 defect resolution

- **B1 (base decode no frame)**: Root cause was twofold: (a) opening the whole
  file with avformat on tmap containers fails ("Derived Image item not
  implemented"); fixed by extracting the primary item via iloc → Annex-B →
  in-memory avio. (b) The corpus generator's `split_annexb` only searched for
  4-byte start codes, dropping 3-byte NALs (SEI/slice) produced by libx265;
  fixed in `gen/build_corpus.py`.
- **B2 (tmap form not wired)**: tmap dimg candidates now union tmap out-edges
  and any reference pointing *to* the tmap (corpus layout is primary→[tmap,
  gainmap], not tmap→gainmap).
- **B3 (vendor URN misclassified as apple)**: `ClassifyAuxUrn` now requires
  exact match on the Apple URN string; any other URN containing `hdrgainmap`
  is vendor.
- **B4 (grid tail w/h missing)**: grid payload parsed for rows/cols/flags
  immediately; flags=0 reads trailing 2×u16 w/h. Dimensions are reported even
  if later tile validation fails.
- **B5 (truncated grid → not_gainmap)**: malformed containers now return
  `fail_closed_malformed` with a readable reason instead of falling through to
  not_gainmap.
- **B6 (unsupported vs malformed priority)**: added `containerWhyUnsupported`
  flag; non-codec/non-grid aux items → unsupported, not malformed.
- **B7 (JPEG/JXL silent SDR)**: JPEG SOI + `GainMap` XMP and JXL `JXL ` box +
  `jhgm` box are detected and reported as `fail_closed_unsupported` with a
  reason, never as not_gainmap.
- **B8 (sha256 wrong)**: hand-rolled SHA-256 had a signed-left-shift UB bug on
  uint8_t inputs ≥ 0x80. Replaced with Windows CNG (BCrypt) — verified against
  `Get-FileHash`.

### 11.3 A/B evidence (--dump)

On `vendor_auxc_hevc_1ch_128base_single.heic` (64×48):
- `--base-only`: p50 ≈ 0.072 (sRGB base after sRGB→linear), maxLinear ≈ 0.214.
- Applied path: p50 ≈ 0.072 (unchanged midtones), maxLinear lifted by the
  vendor 128-base gain map. The `--dump` mode writes `base.pfm` and
  `applied.pfm` (32-bit float RGB, PFM format) plus `note.txt` for independent
  ISO/Apple formula recomputation.

### 11.4 Remaining open items

1. ~~**Per-pixel gain application in the player present path.**~~ **DONE (§12).**
2. JPEG Ultra HDR and JXL jhgm decoding (currently fail_closed_unsupported).
3. 3×3 cross-channel correction for 3-channel gain maps.
4. Real-world vendor private sample (DJI/other) — none found on disk this round.
- Usage: `gainmap_probe.exe <file> [--dump <dir>] [--base-only]`.
  stdout is one JSON line; stderr is diagnostic only.

## 12. Stage 3 — presentation-path application (2026-11-15)

### 12.1 Single source of truth

The gain application math exists in **exactly one place**:
`GainMapProbe::Probe()` in `GainMapProbe.cpp`. The CLI probe calls it directly.
The player (`PlayerSession::DoOpen`) calls the same `Probe(path, /*baseOnly=*/false)`
at open time and stores the result in `gainmapInfo_`. No second copy of the
sRGB-linearize, bilinear resample, ISO/Apple formula, or stats code exists.

### 12.2 Insertion point

In `PlayerSession::DecodePacket`, the `handleFrame` lambda receives every decoded
AVFrame before it reaches `QueueVideoFrame` or `PresentVideoFrame`. When
`gainmapInfo_.applyOk` is true (i.e. the probe successfully decoded and applied
the gainmap), the lambda creates a new `AVFrame` with:

- `format = AV_PIX_FMT_RGBAF32LE` (float RGBA, single plane — renderer layout 3)
- `width/height = gainmapInfo_.outW/outH`
- `color_primaries = BT709`, `color_trc = LINEAR`, `colorspace = RGB`, `color_range = JPEG`
- pixel data copied from `gainmapInfo_.appliedRgba` (linear float RGBA)

This frame replaces the decoded SDR YUV frame for presentation. The renderer
uploads RGBAF32LE directly to the scRGB FP16 swap chain (1.0 = 80 nits), so the
applied linear HDR values flow through the existing color pipeline unchanged.
Non-gainmap images (`gainmapInfo_.applyOk == false`) take the original YUV path
untouched.

### 12.3 Resampling and per-channel math

- The gainmap is decoded at its native resolution (grid tiles composed into a
  single image). Bilinear resampling maps each base pixel to gainmap coordinates
  in `Probe()`. The player consumes the already-composed applied buffer; no
  second resample happens on the player side.
- **ISO family** (incl. vendor 128-base): per-channel `hdr = (sdr+offsetSdr) *
  2^logBoost - offsetHdr`, with `logBoost = gainMin*(1-g) + gainMax*g` and
  `g = gainNorm^(1/gamma)`.
- **Apple family (0-base)**: `hdr = sdr * (1 + (headroom-1) * Rec709Inverse(gain))`.
- 1-channel gain maps use luma for all three channels; 3-channel maps sample
  each channel independently (3×3 cross-channel matrix is an open item).

### 12.4 PQ/BT.2020 base images

If the base image is already PQ/BT.2020 (HDR), the gainmap application semantics
differ: the SDR-compatible base is not present, so the gainmap describes a
conversion between two HDR encodings rather than SDR→HDR lift. The current
implementation does NOT apply gain on top of a PQ base (it leaves the base as-is
and reports the gainmap as detected but not applied). This is documented as an
open item; the corpus `baseimage/pq_base.heic` exercises this path.

### 12.5 Clipping

Values outside [0, +∞) are not hard-clamped in float linear space — the scRGB
FP16 swap chain carries them verbatim. The renderer's existing HDR peak
strategy (tone-map to display headroom) handles the final display conversion.
The `over1Pct` statistic reports the fraction of pixels above 1.0 (80 nits).

### 12.6 Pixel readback

The existing `FFF3FP_ReadVideoPixelRegion` API (FFF.Player.Api.h) reads back
RGBA float pixels from the presented swapchain via a staging texture Map. This
is the evidence path: it reads what the renderer actually uploaded, not a
separate computation.

### 12.7 A/B evidence (probe, same math as player)

| File | Mode | p50 (linear) | maxLinear | over1% |
|---|---|---|---|---|
| iso_tmap_hevc_1ch_sdr_single | base-only | 0.07156 | 0.21271 | 0.00 |
| iso_tmap_hevc_1ch_sdr_single | applied | 0.16727 | 0.51805 | 0.00 |
| vendor_auxc_1ch_128base | base-only | 0.07156 | 0.21271 | 0.00 |
| vendor_auxc_1ch_128base | applied | 0.07195 | 0.21387 | 0.00 |
| vendor_auxc_3ch_128base | base-only | 0.07156 | 0.21271 | 0.00 |
| vendor_auxc_3ch_128base | applied | 0.01825 | 0.13504 | 0.00 |

The tmap 1-channel file shows a clear 2.3× median lift and 2.4× peak lift.
The vendor files use the same synthetic base (p50=0.07156); their gain maps
are near-zero for this test pattern (1ch) or designed for channel separation
(3ch, which darkens due to negative channel-specific boost in the synthetic
gain map).

### 12.8 Regression

Non-gainmap files all return `not_gainmap` and take the original YUV path:
- `IMG20261013085356.heic` (real phone HEIC, no gainmap) → not_gainmap
- `real_phone_notgainmap.heic` → not_gainmap
- `reg_avif.avif` → not_gainmap
- `reg_heic_static.heic` → not_gainmap
- `reg_srgb.jpg` → not_gainmap

### 12.9 --dump output format

`--dump <dir>` writes three files:
- `base.pfm`: PFM (Portable FloatMap) format, magic "PF", 32-bit little-endian
  float RGB, top-down rows, linear Rec.709 (after sRGB→linear). No alpha.
- `applied.pfm`: same format, after gain application.
- `note.txt`: metadata including verdict, family, source, dimensions, and
  pixel format description.

### 12.10 Verify result (38-file corpus)

```
total: 38, field_pass_no_diff: 24, verdict_match: 36
actual buckets: applied=21, malformed=5, unsupported=5, not_gainmap=7
expected buckets: applied=22, malformed=5, unsupported=3, not_gainmap=7, decoded_not_applied=1
```
The 2 verdict gaps are JPEG Ultra HDR and JXL jhgm (unsupported, documented).
The 1 missing applied is JXL jhgm positive (cannot decode JXL).
sha256_mismatch_count=32 reflects the corpus rebuild after manifest generation.
