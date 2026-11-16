<#
.SYNOPSIS
    Run the GainMap probe over the deterministic test corpus and diff against expected values.

.DESCRIPTION
    Supports TWO manifest schemas out of the box:
      * Internal schema: <corpus>\manifest.json is an object with a "files" array; each entry
        carries its expected values under "expected" (gainmapPresent/form/family/metaSource/
        expectedVerdict).
      * External schema: <corpus>\manifest.json is a bare ARRAY; each entry carries the v1
        probe's own verdict/form/reason as "probeVerdict"/"probeForm"/"probeReason".

    A versioned, in-repo expected table can be supplied with -Expected <json> (see
    docs\gainmap-corpus-expectations.json). When given, per-file expected values are looked up
    by relative path from that table, overriding whatever the manifest embeds.

    Compared fields (when present on either side): verdict, form, family, metaSource,
    gainmapPresent. The probe JSON must keep these exact key names (gainmap.present/form,
    meta.family/source, verdict).

    Output: a machine-readable table (written to -OutFile) plus a TOTAL/MATCHED/DIFF summary.

.PARAMETER Root
    FFF repository root. Default: parent of this script's directory. Env: FFF_ROOT.

.PARAMETER Corpus
    GainMap corpus root (must contain manifest.json). Default: the first existing of
    <root>\..\..\testmedia\gainmap-corpus or <root>\..\testmedia\gainmap-corpus. Env: FFF_CORPUS_ROOT.

.PARAMETER Expected
    Optional path to a versioned expected table JSON: { "entries": [ { file, verdict, form,
    family, metaSource, gainmapPresent, rationale } ] }. When set, per-file expectations come
    from here (matched by relative file path) instead of the manifest's embedded "expected".

.PARAMETER ProbeExe
    Path to gainmap_probe.exe. Default: <root>\FFF.Native\tools\build_out\gainmap_probe.exe.

.PARAMETER OutFile
    Where to write the machine-readable result table. Default: <root>\docs\corpus_matrix.json.

.PARAMETER TargetHeadroom
    Optional; appended as "--target-headroom <v>".

.PARAMETER Filter
    Optional substring; only manifest entries whose file contains it are run.

.PARAMETER DryRun
    Resolve and print paths + manifest count, but do not invoke the probe.

.PARAMETER FailOnDiff
    Switch: exit non-zero when any row differs (for CI / gating). Off by default so local
    exploratory runs still report counts.
#>
[CmdletBinding()]
param(
    [string]$Root,
    [string]$Corpus,
    [string]$Expected,
    [string]$ProbeExe,
    [string]$FfmpegDir,
    [string]$OutFile,
    [double]$TargetHeadroom,
    [string]$Filter,
    [switch]$DryRun,
    [switch]$FailOnDiff
)

$ErrorActionPreference = 'Continue'

# --- 1. Resolve Root (script dir = <root>\tools) ---
if (-not $Root)        { $Root = $env:FFF_ROOT }
if (-not $Root)        { $Root = (Split-Path -Parent $PSScriptRoot) }
$Root = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path

# --- 2. Resolve Corpus (sibling testmedia dir by default; try 1- and 2-up layouts) ---
if (-not $Corpus)      { $Corpus = $env:FFF_CORPUS_ROOT }
if (-not $Corpus) {
    $candidates = @(
        (Join-Path $Root '..\..\testmedia\gainmap-corpus'),
        (Join-Path $Root '..\testmedia\gainmap-corpus')
    )
    $chosen = $null
    foreach ($c in $candidates) {
        $cp = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($c)
        if (Test-Path -LiteralPath (Join-Path $cp 'manifest.json')) { $chosen = $cp; break }
    }
    if ($chosen) { $Corpus = $chosen } else { $Corpus = $candidates[0] }
}
$Corpus = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Corpus)

# --- 3. Resolve probe exe ---
if (-not $ProbeExe)    { $ProbeExe = $env:FFF_PROBE_EXE }
if (-not $ProbeExe)    { $ProbeExe = (Join-Path $Root 'FFF.Native\tools\build_out\gainmap_probe.exe') }
$ProbeExe = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ProbeExe)

# --- 4. Ffmpeg dir (informational; probe DLLs live next to the exe) ---
if (-not $FfmpegDir)   { $FfmpegDir = $env:FFF_FFMPEG_DIR }
if (-not $FfmpegDir)   { $FfmpegDir = (Join-Path $Root 'third_party\ffmpeg') }
$FfmpegDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($FfmpegDir)

# --- 5. Out file ---
if (-not $OutFile)     { $OutFile = $env:FFF_CORPUS_OUT }
if (-not $OutFile)     { $OutFile = (Join-Path $Root 'docs\corpus_matrix.json') }
$OutFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)

Write-Host "Root      = $Root"
Write-Host "Corpus    = $Corpus"
Write-Host "Expected  = $(if ($Expected) { $Expected } else { '(manifest-embedded)' })"
Write-Host "ProbeExe  = $ProbeExe"
Write-Host "FfmpegDir = $FfmpegDir"
Write-Host "OutFile   = $OutFile"

$manifestPath = Join-Path $Corpus 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Error "Corpus manifest not found: $manifestPath`nPlace the corpus per docs\gainmap-testing.md, or pass -Corpus."
    exit 2
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

# --- 6. Load the optional in-repo expected table (keyed by relative file path) ---
$expectedMap = @{}
if ($Expected) {
    $Expected = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Expected)
    if (-not (Test-Path -LiteralPath $Expected)) {
        Write-Error "Expected table not found: $Expected"
        exit 2
    }
    $expDoc = Get-Content -LiteralPath $Expected -Raw | ConvertFrom-Json
    foreach ($e in @($expDoc.entries)) {
        if ($e.file) { $expectedMap[$e.file] = $e }
    }
    Write-Host ("Expected  = {0} entries loaded" -f $expectedMap.Count)
}

# --- 7. Normalize manifest entries into a common shape ---
# Internal: object with .files[].expected{...}. External: bare array of probeVerdict/probeForm.
$isExternalArray = ($manifest -is [System.Array]) -or ($null -eq $manifest.files)
if ($isExternalArray) {
    Write-Host "Manifest  = external array schema"
    $entries = @($manifest)
} else {
    Write-Host ("Manifest  = internal object schema ({0} entries)" -f @($manifest.files).Count)
    $entries = @($manifest.files)
}

if ($DryRun) {
    Write-Host ("DryRun: {0} manifest entries; probe exists = {1}" -f $entries.Count, (Test-Path -LiteralPath $ProbeExe))
    exit 0
}

if (-not (Test-Path -LiteralPath $ProbeExe)) {
    Write-Error "Probe exe not found: $ProbeExe`nBuild it first via FFF.Native\tools\build_probe.bat, or pass -ProbeExe."
    exit 2
}

$probeArgs = @()
if ($PSBoundParameters.ContainsKey('TargetHeadroom')) {
    $probeArgs += @('--target-headroom', $TargetHeadroom)
}

# Resolve the expected verdict/form/family/metaSource/gainmapPresent for a manifest entry.
# Priority: -Expected table > internal embedded expected > external array probeVerdict/probeForm.
function Resolve-Expected($e, $isExt, $map, $fileKey) {
    $row = [pscustomobject]@{
        verdict       = $null
        form          = $null
        family        = $null
        metaSource    = $null
        gainmapPresent = $null
        maxLinear     = $null
        maxLinearTolerance = $null
        p50           = $null
        p50Tolerance  = $null
    }
    if ($map.ContainsKey($fileKey)) {
        $m = $map[$fileKey]
        $row.verdict = $m.verdict
        $row.form = $m.form
        $row.family = $m.family
        $row.metaSource = $m.metaSource
        $row.gainmapPresent = $m.gainmapPresent
        $row.maxLinear = $m.maxLinear
        $row.maxLinearTolerance = $m.maxLinearTolerance
        $row.p50 = $m.p50
        $row.p50Tolerance = $m.p50Tolerance
        return $row
    }
    if ($isExt) {
        # External array: probeVerdict/probeForm are the v1 expectations; family/metaSource/gainmapPresent unknown.
        $row.verdict = $e.probeVerdict
        $row.form = $e.probeForm
        return $row
    }
    $x = $e.expected
    if ($null -eq $x) { return $row }
    $row.verdict = $x.expectedVerdict
    $row.form = $x.form
    $row.family = $x.family
    $row.metaSource = $x.metaSource
    $row.gainmapPresent = $x.gainmapPresent
    $row.maxLinear = $x.maxLinear
    $row.maxLinearTolerance = $x.maxLinearTolerance
    $row.p50 = $x.p50
    $row.p50Tolerance = $x.p50Tolerance
    return $row
}

$rows = @()
foreach ($e in $entries) {
    $fileKey = $e.file
    if ($Filter -and ($fileKey -notlike "*$Filter*")) { continue }
    $path = Join-Path $Corpus $fileKey
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Warning "missing corpus file: $fileKey"
        continue
    }

    $want = Resolve-Expected $e $isExternalArray $expectedMap $fileKey

    $raw = & $ProbeExe $path @probeArgs 2>$null
    $a = $raw | ConvertFrom-Json

    $diffs = @()
    # verdict
    if ($null -ne $want.verdict -and $a.verdict -ne $want.verdict) {
        $diffs += "verdict:exp=$($want.verdict)/got=$($a.verdict)"
    }
    # form (none = wildcard)
    if ($null -ne $want.form -and $want.form -ne 'none' -and $a.gainmap.form -ne $want.form) {
        $diffs += "form:exp=$($want.form)/got=$($a.gainmap.form)"
    }
    # family
    if ($null -ne $want.family -and $a.meta.family -ne $want.family) {
        $diffs += "family:exp=$($want.family)/got=$($a.meta.family)"
    }
    # metaSource
    if ($null -ne $want.metaSource -and $a.meta.source -ne $want.metaSource) {
        $diffs += "metaSource:exp=$($want.metaSource)/got=$($a.meta.source)"
    }
    # gainmapPresent
    if ($null -ne $want.gainmapPresent -and [bool]$a.gainmap.present -ne [bool]$want.gainmapPresent) {
        $diffs += "gainmapPresent:exp=$($want.gainmapPresent)/got=$($a.gainmap.present)"
    }
    # Optional numeric oracle: catches an implementation that keeps the right
    # verdict/form but copies the base buffer through the apply path.
    if ($null -ne $want.maxLinear -and $null -ne $a.apply.stats.maxLinear) {
        $tol = if ($null -ne $want.maxLinearTolerance) { [double]$want.maxLinearTolerance } else { 1e-4 }
        if ([math]::Abs([double]$a.apply.stats.maxLinear - [double]$want.maxLinear) -gt $tol) {
            $diffs += "maxLinear:exp=$($want.maxLinear)/got=$($a.apply.stats.maxLinear)"
        }
    }
    if ($null -ne $want.p50 -and $null -ne $a.apply.stats.p50) {
        $tol = if ($null -ne $want.p50Tolerance) { [double]$want.p50Tolerance } else { 1e-4 }
        if ([math]::Abs([double]$a.apply.stats.p50 - [double]$want.p50) -gt $tol) {
            $diffs += "p50:exp=$($want.p50)/got=$($a.apply.stats.p50)"
        }
    }

    $rows += [pscustomobject]@{
        file           = $fileKey
        verdict        = $a.verdict
        expVerdict     = $want.verdict
        form           = $a.gainmap.form
        expForm        = $want.form
        family         = $a.meta.family
        expFamily      = $want.family
        source         = $a.meta.source
        expSource      = $want.metaSource
        gainmapPresent = $a.gainmap.present
        expPresent     = $want.gainmapPresent
        diff           = ($diffs -join '; ')
    }
}

$rows | Format-Table -AutoSize | Out-String -Width 220
Write-Host "=== MISMATCHES ==="
$rows | Where-Object { $_.diff -ne '' } | ForEach-Object { Write-Host ("* " + $_.file + " :: " + $_.diff) }
Write-Host ""
$total = @($rows).Count
$matched = @($rows | Where-Object { $_.diff -eq '' }).Count
$diff = @($rows | Where-Object { $_.diff -ne '' }).Count
Write-Host ("TOTAL=" + $total + " MATCHED=" + $matched + " DIFF=" + $diff)

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
$rows | ConvertTo-Json -Depth 5 | Out-File -LiteralPath $OutFile -Encoding utf8
Write-Host "Wrote $OutFile"

if ($FailOnDiff -and $diff -gt 0) {
    Write-Host "run_corpus: $diff mismatches (FailOnDiff)."
    exit 1
}
exit 0
