# GainMap External Corpus — Reproducibility Record

This document pins down **where the 41 external GainMap test files come from, which
commit/version they were taken at, their per-file SHA-256, how to re-download them,
and how to re-derive the expected values**. It is the reproducibility contract for
the GainMap probe (PR #63).

- Corpus on disk (machine-local, **not in git**): `testmedia/gainmap-corpus/external/`
- Companion files in that directory: `manifest.json` (size + sha256 + source + probe verdict),
  `expected.json` (v2.0 spec-derived truth), `SOURCES.md` (origin notes).
- All 41 SHA-256 values below were recomputed against the on-disk files on 2026-11-16
  and match `external/manifest.json` 1:1 (41/41, 0 missing).

---

## 1. Why the external binaries are NOT committed to the repo

- **Licensing.** The files come from three upstream projects with different licenses
  (BSD-2-Clause, MIT) plus local reference fixtures. Vendoring them into this repo
  would force a re-license review and redistribution obligations on every downstream
  consumer. We instead record the upstream source + pinned commit + sha256, so a
  reviewer can independently fetch the byte-identical files.
- **Size.** The corpus is ~8.5 MB on disk, dominated by `spruit_sunrise_4k.*`
  (~4.8 MB) and `02.jpg`/`03.jpg` (~2.9 MB). Keeping them out of git keeps clone
  times and PR diffs small; the probe smoke test in CI does **not** require them.
- **Alternative.** Reproduce locally: fetch from the pinned raw URLs in §3 into
  `testmedia/gainmap-corpus/external/`, verify SHA-256 (§4), then run
  `tools/run_corpus.ps1` (§5). CI (`.github/workflows/gainmap-probe.yml`) builds the
  probe and runs `--help` only; it never downloads this corpus.

---

## 2. Source groups

| # | Upstream | Path within repo | Pinned commit / ref | License |
|---|---|---|---|---|
| 1 | [AOMediaCodec/libavif](https://github.com/AOMediaCodec/libavif) | `tests/data/` | `37a5461042aa3b1eb105193724ee41bfd2478d0b` | BSD-2-Clause (per-file provenance in that dir's `README.md`) |
| 2 | [imazen/heic](https://github.com/imazen/heic) | `testdata/apple-hdr/` | `main` (pinned by size+sha256; file is `hdr-sample.heic`, 238982 B) | MIT (`testdata/apple-hdr/LICENSE`; originally `johncf/apple-hdr-heic`) |
| 3 | [MONOGRID/gainmap-js](https://github.com/MONOGRID/gainmap-js) | `tests/files/` | `2529e4eb4b9a57b90caa310fd74fe13f46600fb2` | MIT (repo `LICENSE`) |
| 4 | Local reference fixtures (`ffmpegPictureUI/tests/data/`) | n/a (local) | n/a — see note | Local reference fixtures; `gainmap_hevc_16x16.heic` traces to [google/libultrahdr](https://github.com/google/libultrahdr) `tests/data` (Apache-2.0 OR MIT) |

Group 4 files are synthesized HEIC/HEVC/JXL fixtures from the local reference project;
they are not served from a public pinned URL. They are listed here with sha256 so the
corpus contents are fully named; to re-obtain them you must rebuild the reference
project's fixture generator or copy them from that project. `gainmap_hevc_16x16.heic`
is the one fixture that also exists upstream in `libultrahdr/tests/data`.

---

## 3. Per-file manifest (41 files)

SHA-256 lower-case, verified 2026-11-16.

### Group 1 — libavif `tests/data/` (BSD-2-Clause)

| File | Size (B) | SHA-256 |
|---|---:|---|
| apple_gainmap_v1.jpg | 50742 | `2e4310a0dd37a98678e057e25d936bbc4f936bb6fe17380c0ce99e58ecaa603b` |
| apple_gainmap_v2.jpg | 50824 | `492a94bd0636bcf15b3f560c68142a7783936c1597e78b0d0461d8be7fddc078` |
| apple_gainmap_v3.jpg | 51479 | `071203c2a0ba754852e480a1cb70a1d45ea7476a25a79fc38e4433ea1aabb871` |
| color_grid_alpha_grid_gainmap_nogrid.avif | 2870 | `c424c43fe4bab3b8ef37b86c0bab3851b850b94e5d46b9fae979586dae45de0a` |
| color_grid_gainmap_different_grid.avif | 3101 | `73a68c3d6daad7b8298db975a00f02bca46b6c3f292eac09d3c1443d2006fab2` |
| color_nogrid_alpha_nogrid_gainmap_grid.avif | 1875 | `d783e0d9ce778f972e88586b6b1b9eb062f54d38f28521721a8b9cbbda3b7fb0` |
| paris_exif_xmp_gainmap_bigendian.jpg | 47579 | `d22fd05e210df1067ebd6a1f63d0e5d94c5fa9042fe2ebd7067a10a301402271` |
| paris_exif_xmp_gainmap_littleendian.jpg | 47579 | `cea42a04bb4a6a264ee6a972e4c5dad3f0e7eff4f110445af2f46d918fd57923` |
| paris_exif_xmp_icc_gainmap_bigendian.jpg | 48117 | `9736405ab8541bdd749af2ef62a5c48aee067f73c9e248dd4b57550e7faa0cdd` |
| seine_hdr_gainmap_small_srgb.avif | 102004 | `573a67fdd581f6e634da198a819cc92a071539dfb720c5d7dfdf02e4e87a0346` |
| seine_hdr_gainmap_srgb.avif | 122961 | `9bf9c6a7606951de07e4079cd63c2cfe379d95139cd99ab9142d8a6ee22d28c7` |
| seine_hdr_gainmap_wrongaltr.avif | 122961 | `23990f6493467f13a313c629dec3c98a6560b85dbfbb11bfb2ab8a6bc9850bbf` |
| seine_hdr_srgb.avif | 168028 | `b4f440392ad8cdfd398aea0c8817c93d98cfd8cde381bbe430aceae0207e93c8` |
| seine_sdr_different_gainmap_srgb.jpg | 168934 | `4c11ca314630430d3bda8a5bf9648b36f7bc75c62695bc1ab22913887bc11499` |
| seine_sdr_gainmap_big_srgb.avif | 169759 | `b672cfd5ac792ae0a70f82b68ef7bab06c117e5d297624ec8afdf56af847c6b7` |
| seine_sdr_gainmap_gammazero.avif | 129766 | `faf73bb88eb6ed048aefb864db41fb69fa16952d6a1c1246b563465ae020edda` |
| seine_sdr_gainmap_notmapbrand.avif | 129769 | `63da24747562724235e3fc803d2230d25bf5a4f91112aa87a3b804a333f12d7f` |
| seine_sdr_gainmap_srgb_icc.avif | 129863 | `63fa6580a4cc215debc1229e35450c8144207218cc7ce6f5bfe5596efa357d55` |
| seine_sdr_gainmap_srgb.avif | 129773 | `e0ebdb2f1f44c7d901e6b5f817eb2520eace344624cab0d57aaa929d05d6d971` |
| seine_sdr_gainmap_srgb.jpg | 142972 | `1aaf0a1afa91a485c13e4ad696a3da1fe1ddd2675417e3812a23a9d13f765262` |
| supported_gainmap_writer_version_with_extra_bytes.avif | 1375 | `189398ea72d391c75c8679942c539ae7d4324152fa2f948d742b73f3bd6cc8f1` |
| unsupported_gainmap_minimum_version.avif | 1371 | `d675f46519029ce3da98fac587cb25fa2eb33c7b77d7ae5c04903b2825367331` |
| unsupported_gainmap_version.avif | 1371 | `f67ef979ee9df50c7893eafea591030b3897dba1285ea8331dc0687e063dda9b` |
| weld_sato_12B_8B_q0.avif | 38100 | `fa41d615d244d50fc99d71c1fea14561e0814382a748d1b8b672c3fd5a595dbe` |

### Group 2 — imazen/heic `testdata/apple-hdr/` (MIT)

| File | Size (B) | SHA-256 |
|---|---:|---|
| apple_hdr_sample_imazen.heic | 238982 | `c690fa4ecc6ae71ee9a926de869a95835a5d41e764ccbc200647e2fb9d0800cc` |

### Group 3 — MONOGRID/gainmap-js `tests/files/` (MIT)

| File | Size (B) | SHA-256 |
|---|---:|---|
| 02.jpg | 1466329 | `7390369df01a52560b1d4840b64d77500accc8434ca97a48f86440304ecd7829` |
| 03.jpg | 1438257 | `0c7538850bf3b5dbdfeb6bfdbb7c639d867d07bd7d8bf286537cfd23cca8a513` |
| plain-jpeg.jpg | 27661 | `8c712905b799905357b8202d0cb7a244cefeeccf7aa5eb79896645ac50158ffa` |
| skia-gainmap-iso21496-1-adobe-gcontainer.jpg | 3758 | `22a607c29dd1897c152be4b65e93ef08e8625dc7a3704e8368a42813e2bc5636` |
| skia-gainmap-iso21496-1-hdr-base.jpg | 1996 | `5a59ad3e36e3876d95b5bb6c76c0df2134e04cc06bd42092c84b83d4610d791f` |
| spruit_sunrise_4k-gainmap.webp | 24642 | `21ccf40cc3ad4b699d86d9fa32ad56684395cb0abc55acd4378fa97f5ae19d81` |
| spruit_sunrise_4k.jpg | 1786249 | `49372dff4f9db4aab995f0a22462df4c25d31d5ac16ce9620b7e69d2dac69ddd` |
| spruit_sunrise_4k.webp | 1525718 | `5e8952e80ff17cd3726ed2591d89975862d4757682173482f095b3a7adf62fe7` |

### Group 4 — local reference fixtures

| File | Size (B) | SHA-256 |
|---|---:|---|
| gainmap_hevc_16x16.heic | 1523 | `6dd810ee6d118d3ce876936d9b265338b94d44cde187bce2ba384ce93edb1b8e` |
| synth_heic_gainmap.heic | 6373 | `5d615404c7403b0b232e3a2d1c184e65bdf691d634f00299fe337a6a66520bf0` |
| synth_heic_grid_aux_gainmap_b.heic | 13274 | `4e4a99cc087005a9a8dbbde8819d2c53ea8aa5480a6e1f0eb271748d662e578a` |
| synth_heic_grid_aux_gainmap.heic | 15874 | `0ef2306409838448c2c57935c9e1b5dc263801d7f756a57b45cc796630315533` |
| synth_heic_grid_aux_noxmp.heic | 15469 | `e34f760869b906fd7d937218ea3ca8b47c5eba59689f683011f8fa75c9f5f4e7` |
| synth_heic_grid_gainmap.heic | 15569 | `07f4fbc3edd50c7279d25f9113d6836e8b0362e0c48aad8221d4f731d52fcfa3` |
| synth_jxl_gainmap_base.jxl | 90 | `af0a836e7c164edc4c03531c10d6e73ef5fd3cf78ed006b50bcc8f3e0deea0cc` |
| synth_jxl_gainmap.jxl | 232 | `009f22befa1def75a42c0436409ad7e9100c4519b79f061d3b699d2ada3a53a0` |

---

## 4. How to download and verify

Put the files in `testmedia/gainmap-corpus/external/` (sibling of the repo checkout,
i.e. `<repo>\..\testmedia\gainmap-corpus\external\`). Raw-URL fetch at the pinned
commits:

```powershell
$ext = "<repo>\..\testmedia\gainmap-corpus\external"
New-Item -ItemType Directory -Force $ext | Out-Null

$LIBVAVIF = 'https://raw.githubusercontent.com/AOMediaCodec/libavif/37a5461042aa3b1eb105193724ee41bfd2478d0b/tests/data'
$GAINMAPJS = 'https://raw.githubusercontent.com/MONOGRID/gainmap-js/2529e4eb4b9a57b90caa310fd74fe13f46600fb2/tests/files'

# Group 1 (libavif)
foreach ($f in @(
  'apple_gainmap_v1.jpg','apple_gainmap_v2.jpg','apple_gainmap_v3.jpg',
  'color_grid_alpha_grid_gainmap_nogrid.avif','color_grid_gainmap_different_grid.avif',
  'color_nogrid_alpha_nogrid_gainmap_grid.avif',
  'paris_exif_xmp_gainmap_bigendian.jpg','paris_exif_xmp_gainmap_littleendian.jpg',
  'paris_exif_xmp_icc_gainmap_bigendian.jpg',
  'seine_hdr_gainmap_small_srgb.avif','seine_hdr_gainmap_srgb.avif','seine_hdr_gainmap_wrongaltr.avif',
  'seine_hdr_srgb.avif','seine_sdr_different_gainmap_srgb.jpg','seine_sdr_gainmap_big_srgb.avif',
  'seine_sdr_gainmap_gammazero.avif','seine_sdr_gainmap_notmapbrand.avif','seine_sdr_gainmap_srgb_icc.avif',
  'seine_sdr_gainmap_srgb.avif','seine_sdr_gainmap_srgb.jpg',
  'supported_gainmap_writer_version_with_extra_bytes.avif',
  'unsupported_gainmap_minimum_version.avif','unsupported_gainmap_version.avif','weld_sato_12B_8B_q0.avif')) {
  Invoke-WebRequest "$LIBVAVIF/$f" -OutFile (Join-Path $ext $f)
}

# Group 2 (imazen/heic, main)
Invoke-WebRequest 'https://raw.githubusercontent.com/imazen/heic/main/testdata/apple-hdr/hdr-sample.heic' `
  -OutFile (Join-Path $ext 'apple_hdr_sample_imazen.heic')

# Group 3 (gainmap-js)
foreach ($f in @('02.jpg','03.jpg','plain-jpeg.jpg',
  'skia-gainmap-iso21496-1-adobe-gcontainer.jpg','skia-gainmap-iso21496-1-hdr-base.jpg',
  'spruit_sunrise_4k-gainmap.webp','spruit_sunrise_4k.jpg','spruit_sunrise_4k.webp')) {
  Invoke-WebRequest "$GAINMAPJS/$f" -OutFile (Join-Path $ext $f)
}

# Group 4: local reference fixtures — copy from the ffmpegPictureUI/tests/data tree,
# or fetch gainmap_hevc_16x16.heic from google/libultrahdr tests/data.
```

Verify every byte matches:

```powershell
$man = Get-Content (Join-Path $ext 'manifest.json') -Raw | ConvertFrom-Json
foreach ($m in $man) {
  $h = (Get-FileHash -LiteralPath (Join-Path $ext $m.file) -Algorithm SHA256).Hash.ToLower()
  if ($h -ne $m.sha256.ToLower()) { Write-Host "MISMATCH: $($m.file)" }
}
```

Expected result: 41/41 match, no `MISMATCH` lines.

---

## 5. How to build the probe and run the corpus

### 5.1 Build

From a Visual Studio 2022 (VS18) x64 environment:

```bat
:: Release probe (links ffmpeg)
FFF.Native\tools\build_probe.bat
:: AddressSanitizer build (Debug)
FFF.Native\tools\build_probe_asan.bat
:: JPEG-only probe (no ffmpeg linkage)
FFF.Native\tools\build_jpeg_probe.bat
:: Apple metadata probe
FFF.Native\tools\build_apple_meta.bat
```

All four derive the repo root from `%~dp0` and honor these environment overrides
(no absolute paths hardcoded): `FFF_ROOT`, `FFF_FFMPEG_DIR`, `VCROOT`, `SDK_VERSION`,
`FFF_LIBUNWIND_DLL`. On machines where `vcvars64.bat` does not register the Windows
SDK `Include`/`Lib`, the batch files explicitly append
`Include\10.0.26100.0\{ucrt,um,shared,winrt}` and `Lib\10.0.26100.0\{ucrt,um}\x64`
**after** the `vcvars` call (override `SDK_VERSION` if your SDK differs).

Output: `FFF.Native\tools\build_out\gainmap_probe.exe` (plus runtime DLLs copied next
to it).

### 5.2 Run

```powershell
# Dry run — print resolved paths and manifest count, do not invoke the probe
.\tools\run_corpus.ps1 -DryRun

# Full run over the internal corpus (38 entries in manifest.json)
.\tools\run_corpus.ps1

# Run a single file subset
.\tools\run_corpus.ps1 -Filter 'negative/not_gainmap_plain'
```

Parameters / environment variables (first non-empty wins: explicit param → env → default):

| Param | Env var | Default | Meaning |
|---|---|---|---|
| `-Root` | `FFF_ROOT` | parent of `tools\` | FFF repo root |
| `-Corpus` | `FFF_CORPUS_ROOT` | `<Root>\..\testmedia\gainmap-corpus` | corpus root containing `manifest.json` |
| `-ProbeExe` | `FFF_PROBE_EXE` | `<Root>\FFF.Native\tools\build_out\gainmap_probe.exe` | probe binary |
| `-FfmpegDir` | `FFF_FFMPEG_DIR` | `<Root>\third_party\ffmpeg` | prebuilt ffmpeg (informational; DLLs sit next to the exe) |
| `-OutFile` | `FFF_CORPUS_OUT` | `<Root>\docs\corpus_matrix.json` | machine-readable result table |
| `-TargetHeadroom` | — | (probe default 4.9) | appended as `--target-headroom <v>` |
| `-Filter` | — | (none) | substring on manifest file path |
| `-DryRun` | — | off | resolve + print only |

The probe contract: `gainmap_probe.exe <file> [--target-headroom <stops>]` prints a
single-line JSON object (`gainmap.present/form/auxCUrn/grid`, `meta.family/source/...`,
`apply.stats`, `verdict`). `run_corpus.ps1` diffs that against `expected.*` and prints
`TOTAL / MATCHED / DIFF`.

---

## 6. `expected.json` field semantics and derivation rules

`external/expected.json` (v2.0) is **not** hand-rolled. Every expected value was
re-derived from the raw container bytes (the ISO BMFF `tmap` box, the JPEG APP2 box,
and the XMP / Exif MakerNote blocks) per the rules below. Fields:

| Field | Meaning |
|---|---|
| `expectedVerdict` | One of `applied`, `decoded_not_applied`, `fail_closed_unsupported`, `fail_closed_malformed`, `not_gainmap`. |
| `expectedForm` | Container form: `tmap`, `tmap_grid`, `auxc_apple`, `auxc_vendor`, `auxc_grid`, `jpeg_hdr`, `jpeg_apple_v1/v2/v3`, `jpeg_adobe_gcontainer`, `webp_gainmap`, `jhgm`, or `none`. |
| `gainmapPresent` | Whether any gain-map metadata box / APP2 / XMP was located at all. |
| `family` | `iso` (ISO 21496-1), `apple` (Apple 0-base family), `vendor128` (128-base), or `none`. |
| `channels` | Gain-map channel count from the tmap flags byte (`0x80` = multichannel ⇒ 3, else 1). |
| `itemType` | Primary image codec (`hvc1`, `av01`, `mjpeg`, `jxl`, …). |
| `byteFacts` | Raw-byte evidence (XMP scalars, MPF dimensions, URN, headroom source). |
| `note` | Why the verdict is what it is, and what would make the probe flag a regression. |

Derivation rules (the only source of truth; do not infer from old probe output):

1. **tmap flags byte (ISO 21496-1).** Bits are only `0x80` = `is_multichannel`
   (channels 3/1) and `0x40` = `use_base_colour_space`. The low 6 bits (`0x3F`) are
   reserved — **non-zero ⇒ `fail_closed_unsupported`**. There is **no** HDR-base bit.
2. **HDR base decision.** Let `hB = baseHdrHeadroomLog2`, `hA = altHdrHeadroomLog2`
   (both in log2 stops, linear multiplier = `2^`).
   - `hB > hA` ⇒ the base image is already HDR ⇒ `verdict = decoded_not_applied`
     (display base only, no gain map applied).
   - `hB <= hA` ⇒ apply with weight `w = clamp((target - hB) / (hA - hB), 0, 1)`,
     `target` default 4.9 stops (`hA == hB` ⇒ `w = 1` with a note).
3. **Apply math.** `g = pow(gainNormalized, 1/gamma)` (gamma > 0 checked);
   `log2Gain = gainMin[c] + (gainMax[c]-gainMin[c]) * g`;
   `mult = pow(2, w * log2Gain)`;
   `out = (base + w*offSdr[c]) * mult - w*offHdr[c]` (clamped ≥ 0; `w == 0` ⇒
   identity). `gainMapMax` is **never** used directly as a multiplier. Apple 0-base
   family uses `out = base * (1 + (2^(w*headroomLog2)-1) * gApple)`.
4. **JPEG / Ultra HDR.** `hdrgm:HDRCapacityMin = hB`, `hdrgm:HDRCapacityMax = hA`,
   plus `BaseRenditionIsHDR`. These scalars are mandatory and must feed the hB/hA
   decision (they are not `(void)`-discarded). Apple JPEG v1 reads headroom from the
   Exif MakerNote; v2/v3 from XMP `HDRGainMapHeadroom`. **Missing headroom metadata
   ⇒ `fail_closed_unsupported`** (no silent 3.0 default). An Adobe `gcontainer`
   secondary JPEG carries its own APP2 ISO box + hdrgm XMP.
5. **Validation (all fail-closed).** `gainMin[c] > gainMax[c]`, `gamma[c] <= 0`, or
   any rational denominator `== 0` ⇒ `fail_closed_malformed`. tmap v0 fixed layout
   with trailing bytes ⇒ `fail_closed_unsupported`. Unsupported
   version/minVersion ⇒ `not_gainmap` (display base, ignore gain map).

---

## 7. CI

`.github/workflows/gainmap-probe.yml` runs on `workflow_dispatch` only (no push/PR
triggers, to avoid noise). On `windows-latest` it builds `FFF.Native`
(`Release|x64` via MSBuild) and, because the repo vendors **no** GainMap sample file,
runs a build-only smoke check (`gainmap_probe.exe --help` / exe existence) and uploads
logs. It intentionally does **not** download this external corpus — reproducing the
41-file matrix is a local, manual step documented above.
