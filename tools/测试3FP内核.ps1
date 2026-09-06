param(
    [ValidateSet("Release", "Debug")][string]$Configuration = "Release",
    [string]$MediaPath = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$FixtureRoot = Join-Path $ProjectRoot "artifacts\kernel-tests"
$LogRoot = Join-Path $FixtureRoot "logs"
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$Ffmpeg = (Get-Command ffmpeg.exe -ErrorAction Stop).Source
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "构建3FP.ps1") -Configuration $Configuration -SkipDependencyPreparation
}
function Invoke-Fixture([string]$Name, [string[]]$Arguments) {
    & $Ffmpeg -v error -y @Arguments (Join-Path $FixtureRoot $Name)
    if ($LASTEXITCODE -ne 0) { throw "Fixture generation failed: $Name" }
}
$Video = @("-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=1")
$Audio = @("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=1")
$Codec = @("-c:v", "libx264", "-threads", "2", "-bf", "3", "-c:a", "flac")
Invoke-Fixture "short.mkv" ($Video + $Audio + $Codec)
Invoke-Fixture "silent.mkv" @("-i", (Join-Path $FixtureRoot "short.mkv"), "-map", "0:v", "-c", "copy")
Invoke-Fixture "tiny.mkv" (@("-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=0.066667",
    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=0.066667") + $Codec + @("-frames:v", "2"))
Invoke-Fixture "early-audio.mkv" ($Video + @("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=0.2") + $Codec)
Invoke-Fixture "late-video.mkv" (@("-itsoffset", "0.5", "-f", "lavfi", "-i",
    "testsrc2=size=320x180:rate=30:duration=0.5") + $Audio + $Codec)
Invoke-Fixture "audio.flac" (@("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=0.4", "-c:a", "flac"))
Invoke-Fixture "long.mkv" (@("-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=4",
    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=4") + $Codec)
Invoke-Fixture "hdr-white.mkv" @("-f", "lavfi", "-i", "color=white:size=64x64:rate=30:duration=0.1",
    "-pix_fmt", "yuv420p10le", "-c:v", "libx265", "-x265-params", "log-level=error:pools=2:colorprim=9:transfer=16:colormatrix=9")
$Test = Join-Path $ProjectRoot "FFF.Player.Tests\bin\$Configuration\net10.0-windows10.0.26100.0\FFF.Player.Tests.exe"
& $Test --kernel-timeline-regression $FixtureRoot *> (Join-Path $LogRoot "timeline.log")
Get-Content (Join-Path $LogRoot "timeline.log") | Where-Object { $_ -notmatch "libpng warning" }
if ($LASTEXITCODE -ne 0) { throw "Kernel timeline regression failed." }
if (-not [string]::IsNullOrWhiteSpace($MediaPath)) {
    & $Test --startup-regression ([IO.Path]::GetFullPath($MediaPath)) *> (Join-Path $LogRoot "startup.log")
    Get-Content (Join-Path $LogRoot "startup.log") | Where-Object { $_ -match "STARTUP|测试失败" }
    if ($LASTEXITCODE -ne 0) { throw "Real-media startup regression failed." }
}
