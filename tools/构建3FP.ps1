param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Debug",

    [switch]$SkipDependencyPreparation,

    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $MyInvocation.MyCommand.Path
} else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")

$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool

if (-not $SkipDependencyPreparation) {
    $PrepareLibassScript = Get-ChildItem -LiteralPath $ScriptDirectory -Filter "*Libass.ps1" -File |
        Select-Object -First 1
    if ($null -eq $PrepareLibassScript) { throw "The libass preparation script was not found." }
    & $PrepareLibassScript.FullName
}

$RequiredFfmpegFiles = @(
    (Join-Path $ProjectRoot "third_party\ffmpeg\include\libavcodec\avcodec.h"),
    (Join-Path $ProjectRoot "third_party\ffmpeg\lib\x64\avcodec-63.lib"),
    (Join-Path $ProjectRoot "runtime\avcodec-63.dll")
)
$MissingFfmpegFiles = @($RequiredFfmpegFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($MissingFfmpegFiles.Count -ne 0) {
    $PrepareFfmpegScript = Get-ChildItem -LiteralPath $ScriptDirectory -Filter "*FFmpeg.ps1" -File |
        Select-Object -First 1
    if ($null -eq $PrepareFfmpegScript) { throw "The FFmpeg preparation script was not found." }
    & $PrepareFfmpegScript.FullName
    $MissingFfmpegFiles = @($RequiredFfmpegFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($MissingFfmpegFiles.Count -ne 0) {
        throw "FFmpeg preparation completed without all required development files."
    }
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

& $DotNet build (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
    -c $Configuration --nologo
if ($LASTEXITCODE -ne 0) { throw "FFF.Player x64 $Configuration build failed." }

if (-not $SkipTests) {
    & $DotNet build (Join-Path $ProjectRoot "FFF.Player.Tests\FFF.Player.Tests.vbproj") `
        -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player.Tests x64 $Configuration build failed." }
}

Write-Host "3FP $Configuration build completed with MSBuild: $MSBuild"
