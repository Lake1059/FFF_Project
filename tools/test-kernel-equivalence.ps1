param([Parameter(Mandatory)][string]$PackageDirectory, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$pkg = [IO.Path]::GetFullPath($PackageDirectory)
$probe = Join-Path $pkg 'kernel-probe.exe'
$results = Join-Path $pkg 'regression'
New-Item -ItemType Directory -Path $results -Force | Out-Null
# fixture, width, height, decode, quality, color, projection
$cases = @(
    @('sdr420',322,182,1,1,0,0),
    @('sdr420',160,90,1,0,0,0),
    @('sdr420',644,364,1,1,0,0),
    @('sdr44410',322,182,1,1,0,0),
    @('sdr44410',160,90,1,1,0,0),
    @('sdr44410',644,364,1,0,0,0),
    @('yuv422p10le',322,182,1,1,0,0),
    @('yuv444p16le',322,182,1,1,0,0),
    @('yuv444p16le',160,90,1,0,0,0),
    @('bgra',322,182,1,1,0,0),
    @('bgra',644,364,1,1,0,0),
    @('smpte2084',322,182,1,1,0,0),
    @('smpte2084',160,90,1,0,1,0),
    @('arib-std-b67',644,364,1,1,0,0),
    @('sdr420',644,364,1,1,0,1),
    @('gpu420',322,182,2,1,0,0),
    @('gpu420',160,90,2,0,0,0),
    @('gpu42010',322,182,2,1,0,0)
)
$rows = @()
for ($index = 0; $index -lt $cases.Count; ++$index) {
    $case = $cases[$index]
    $paths = @()
    foreach ($variant in @('baseline','optimized')) {
        $pixels = Join-Path $results "$index-$variant.f32"
        $arguments = @((Join-Path $pkg "$variant/FFF.Native.dll"),
            (Join-Path $pkg "fixtures/$($case[0]).mkv")) + $case[1..6] + @(0, $pixels)
        $log = @(& $probe @arguments 2>&1)
        $code = $LASTEXITCODE
        $log | Set-Content -LiteralPath (Join-Path $results "$index-$variant.log") -Encoding utf8
        if ($code -ne 0) { throw "Case $index $variant failed ($code): $log" }
        $paths += $pixels
    }
    $hashes = @(Get-FileHash -LiteralPath $paths)
    $same = $hashes[0].Hash -ceq $hashes[1].Hash
    $comparison = 'byte-identical'
    if (-not $same) {
        $bits = [regex]::Match(($log -join ' '), 'bits=(\d+)').Groups[1].Value
        $comparison = & $Python (Join-Path $PSScriptRoot 'compare-kernel-pixels.py') $paths[0] $paths[1] $bits
        if ($LASTEXITCODE -ne 0) { throw "Case $index changed pixels beyond output precision: $comparison" }
    }
    $rows += [pscustomobject]@{case=$index;fixture=$case[0];width=$case[1];height=$case[2];decode=$case[3];quality=$case[4];color=$case[5];projection=$case[6];identical=$same;comparison=$comparison;sha256=$hashes[0].Hash}
    Write-Host "Case $index $comparison : $($case -join ',')"
}
$rows | Export-Csv -LiteralPath (Join-Path $results 'results.csv') -NoTypeInformation -Encoding utf8
Write-Host "All $($cases.Count) cases passed; consult results.csv for exact equality or quantization differences."
