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

function Find-VisualStudioPreview {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe was not found. Install Visual Studio Preview with Desktop development with C++."
    }
    $installations = @(& $vswhere -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
    $preview = $installations | Where-Object { $_.isPrerelease } |
        Sort-Object installationVersion -Descending | Select-Object -First 1
    if ($null -eq $preview) {
        throw "Visual Studio Preview with Desktop development with C++ was not found."
    }
    return $preview.installationPath
}

$VisualStudioDirectory = Find-VisualStudioPreview
$MSBuild = Join-Path $VisualStudioDirectory "MSBuild\Current\Bin\amd64\MSBuild.exe"
if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
    throw "Visual Studio Preview x64 MSBuild was not found: $MSBuild"
}

if (-not $SkipDependencyPreparation) {
    & (Join-Path $ScriptDirectory "Prepare-Libass.ps1") -VisualStudioDirectory $VisualStudioDirectory
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

dotnet build (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
    -c $Configuration --nologo
if ($LASTEXITCODE -ne 0) { throw "FFF.Player x64 $Configuration build failed." }

if (-not $SkipTests) {
    dotnet build (Join-Path $ProjectRoot "FFF.Player.Tests\FFF.Player.Tests.vbproj") `
        -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player.Tests x64 $Configuration build failed." }
}

Write-Host "3FP $Configuration build completed with Visual Studio Preview: $VisualStudioDirectory"
