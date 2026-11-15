<#
.SYNOPSIS
    Run the GainMap probe over the deterministic test corpus and diff against expected.json.

.DESCRIPTION
    Paths are derived from this script's own location, then overridable by explicit
    parameters, then by environment variables, then by defaults. No absolute machine
    paths are hardcoded.

    Resolution order for each setting (first non-empty wins):
        1. Explicit -Parameter
        2. Environment variable
        3. Default derived from $PSScriptRoot

.PARAMETER Root
    FFF repository root. Default: parent of this script's directory (this file lives
    at <root>\tools\run_corpus.ps1). Env: FFF_ROOT.

.PARAMETER Corpus
    GainMap corpus root (must contain manifest.json). Default:
    <root>\..\testmedia\gainmap-corpus (the corpus lives outside the repo on purpose;
    see docs\gainmap-testing.md). Env: FFF_CORPUS_ROOT.

.PARAMETER ProbeExe
    Path to gainmap_probe.exe. Default: <root>\FFF.Native\tools\build_out\gainmap_probe.exe.
    Env: FFF_PROBE_EXE.

.PARAMETER FfmpegDir
    Prebuilt ffmpeg dir (documented; the probe exe already carries its runtime DLLs
    next to itself). Default: <root>\third_party\ffmpeg. Env: FFF_FFMPEG_DIR.

.PARAMETER OutFile
    Where to write the machine-readable result table. Default: <root>\docs\corpus_matrix.json.
    Env: FFF_CORPUS_OUT.

.PARAMETER TargetHeadroom
    Optional. If set, appended to every probe invocation as "--target-headroom <v>".
    The probe uses its own default when omitted.

.PARAMETER Filter
    Optional substring; only manifest entries whose file contains it are run.

.PARAMETER DryRun
    Resolve and print paths + manifest count, but do not invoke the probe.
#>
[CmdletBinding()]
param(
    [string]$Root,
    [string]$Corpus,
    [string]$ProbeExe,
    [string]$FfmpegDir,
    [string]$OutFile,
    [double]$TargetHeadroom,
    [string]$Filter,
    [switch]$DryRun
)

$ErrorActionPreference = 'Continue'

# --- 1. Resolve Root (script dir = <root>\tools) ---
if (-not $Root)        { $Root = $env:FFF_ROOT }
if (-not $Root)        { $Root = (Split-Path -Parent $PSScriptRoot) }
$Root = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path

# --- 2. Resolve Corpus (sibling testmedia dir by default) ---
if (-not $Corpus)      { $Corpus = $env:FFF_CORPUS_ROOT }
if (-not $Corpus)      { $Corpus = (Join-Path $Root '..\testmedia\gainmap-corpus') }
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
Write-Host "ProbeExe  = $ProbeExe"
Write-Host "FfmpegDir = $FfmpegDir"
Write-Host "OutFile   = $OutFile"

$manifestPath = Join-Path $Corpus 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Error "Corpus manifest not found: $manifestPath`nDownload/place the corpus per docs\gainmap-testing.md, or pass -Corpus."
    exit 2
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Write-Host ("Manifest  = {0} entries" -f @($manifest.files).Count)

if ($DryRun) {
    Write-Host "DryRun: not invoking probe. Exe exists = $(Test-Path -LiteralPath $ProbeExe)"
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

$rows = @()
foreach ($e in $manifest.files) {
    if ($Filter -and ($e.file -notlike "*$Filter*")) { continue }
    $path = Join-Path $Corpus $e.file
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Warning "missing corpus file: $($e.file)"
        continue
    }
    $raw = & $ProbeExe $path @probeArgs 2>$null
    $a = $raw | ConvertFrom-Json
    $x = $e.expected
    $diffs = @()
    if ($a.gainmap.present -ne $x.gainmapPresent) { $diffs += "present:exp=$($x.gainmapPresent)/got=$($a.gainmap.present)" }
    if ($x.form -ne 'none' -and $a.gainmap.form -ne $x.form) { $diffs += "form:exp=$($x.form)/got=$($a.gainmap.form)" }
    if ($null -ne $x.auxCUrn -and $a.gainmap.auxCUrn -ne $x.auxCUrn) { $diffs += "urn:exp=$($x.auxCUrn)/got=$($a.gainmap.auxCUrn)" }
    if ($null -ne $x.grid) {
        if ($a.gainmap.grid.rows -ne $x.grid.rows) { $diffs += "grid.rows:exp=$($x.grid.rows)/got=$($a.gainmap.grid.rows)" }
        if ($a.gainmap.grid.cols -ne $x.grid.cols) { $diffs += "grid.cols:exp=$($x.grid.cols)/got=$($a.gainmap.grid.cols)" }
        if ($a.gainmap.grid.flags -ne $x.grid.flags) { $diffs += "grid.flags:exp=$($x.grid.flags)/got=$($a.gainmap.grid.flags)" }
        if ($a.gainmap.grid.tailPresent -ne $x.grid.tailPresent) { $diffs += "tailPresent:exp=$($x.grid.tailPresent)/got=$($a.gainmap.grid.tailPresent)" }
        if ($x.grid.tailW -ne $null -and $a.gainmap.grid.tailW -ne $x.grid.tailW) { $diffs += "tailW:exp=$($x.grid.tailW)/got=$($a.gainmap.grid.tailW)" }
        if ($x.grid.tailH -ne $null -and $a.gainmap.grid.tailH -ne $x.grid.tailH) { $diffs += "tailH:exp=$($x.grid.tailH)/got=$($a.gainmap.grid.tailH)" }
    }
    if ($a.meta.family -ne $x.family) { $diffs += "family:exp=$($x.family)/got=$($a.meta.family)" }
    if ($a.meta.source -ne $x.metaSource) { $diffs += "metaSource:exp=$($x.metaSource)/got=$($a.meta.source)" }
    if ($a.verdict -ne $x.expectedVerdict) { $diffs += "verdict:exp=$($x.expectedVerdict)/got=$($a.verdict)" }
    $rows += [pscustomobject]@{
        file       = $e.file
        verdict    = $a.verdict
        expVerdict = $x.expectedVerdict
        form       = $a.gainmap.form
        expForm    = $x.form
        family     = $a.meta.family
        expFamily  = $x.family
        source     = $a.meta.source
        expSource  = $x.metaSource
        diff       = ($diffs -join '; ')
    }
}
$rows | Format-Table -AutoSize | Out-String -Width 200
Write-Host "=== MISMATCHES ==="
$rows | Where-Object { $_.diff -ne '' } | ForEach-Object { Write-Host ("* " + $_.file + " :: " + $_.diff) }
Write-Host ""
Write-Host ("TOTAL=" + $rows.Count + " MATCHED=" + (@($rows | Where-Object {$_.diff -eq ''}).Count) + " DIFF=" + (@($rows | Where-Object {$_.diff -ne ''}).Count))

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
$rows | ConvertTo-Json -Depth 4 | Out-File -LiteralPath $OutFile -Encoding utf8
Write-Host "Wrote $OutFile"
