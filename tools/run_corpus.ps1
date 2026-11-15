$ErrorActionPreference = 'Continue'
$root = 'C:\PLAN\3FCompare\testmedia\gainmap-corpus'
$exe  = 'C:\PLAN\3FCompare\_wt_gainmap\FFF.Native\tools\build_out\gainmap_probe.exe'
$manifest = Get-Content (Join-Path $root 'manifest.json') -Raw | ConvertFrom-Json

$rows = @()
foreach ($e in $manifest.files) {
    $path = Join-Path $root $e.file
    $raw = & $exe $path 2>$null
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
        file = $e.file
        verdict = $a.verdict
        expVerdict = $x.expectedVerdict
        form = $a.gainmap.form
        expForm = $x.form
        family = $a.meta.family
        expFamily = $x.family
        source = $a.meta.source
        expSource = $x.metaSource
        diff = ($diffs -join '; ')
    }
}
$rows | Format-Table -AutoSize | Out-String -Width 200
Write-Host "=== MISMATCHES ==="
$rows | Where-Object { $_.diff -ne '' } | ForEach-Object { Write-Host ("* " + $_.file + " :: " + $_.diff) }
Write-Host ""
Write-Host ("TOTAL=" + $rows.Count + " MATCHED=" + ($rows | Where-Object {$_.diff -eq ''}).Count + " DIFF=" + ($rows | Where-Object {$_.diff -ne ''}).Count)
# save machine-readable
$rows | ConvertTo-Json -Depth 4 | Out-File 'C:\PLAN\3FCompare\_wt_gainmap\docs\corpus_matrix.json' -Encoding utf8
