# 构建 Fmp4SeekProbe.ps1 — build the native test entry point.
#
# Toolchain discovery uses tools\Resolve-Toolchain.ps1 (environment -> stable/preview ->
# latest MSVC toolset), but this script configures INCLUDE/LIB itself and invokes
# cl.exe directly inside the current pwsh process: no vcvars64.bat, no cmd.exe
# quoting traps. cl must be invoked via pwsh (pwsh -File this script); invoking it
# from a git-bash pipe is known to hang silently.
#
# Run:  pwsh -NoProfile -File <repo>\tools\构建Fmp4SeekProbe.ps1

$ErrorActionPreference = "Stop"

$KernelRoot = Split-Path -Parent $PSScriptRoot                            # 3fp\kernel
$NativeRoot = Join-Path $KernelRoot "FFF.Native"
$ProbeDir   = Join-Path $KernelRoot "tests\Fmp4SeekProbe"
$BuildDir   = Join-Path (Split-Path -Parent $KernelRoot) "build"          # 3fp\build
$ObjDir     = Join-Path $ProbeDir "obj"

. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
$source = Join-Path $ProbeDir "main.cpp"
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Probe source is missing: $source. This checkout does not include the standalone Fmp4SeekProbe sources."
}

function Get-LatestChildDirectory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return Get-ChildItem -LiteralPath $Path -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]"0.0" } } -Descending |
        Select-Object -First 1
}

$vsRoot  = Get-VisualStudioInstallation
$msvcDir = Get-LatestChildDirectory (Join-Path $vsRoot "VC\Tools\MSVC")
if (-not $msvcDir) { throw "MSVC toolset not found under $vsRoot\VC\Tools\MSVC." }
$msvcRoot = $msvcDir.FullName
$clExe = Join-Path $msvcRoot "bin\Hostx64\x64\cl.exe"
if (-not (Test-Path -LiteralPath $clExe)) { throw "cl.exe not found: $clExe" }

$kitsRoot = "C:\Program Files (x86)\Windows Kits\10"
$sdkDir = Get-LatestChildDirectory (Join-Path $kitsRoot "Include")
if (-not $sdkDir) { throw "Windows SDK not found under $kitsRoot\Include." }
$sdkVer = $sdkDir.Name

$env:PATH = "$msvcRoot\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$msvcRoot\include;$kitsRoot\Include\$sdkVer\ucrt;$kitsRoot\Include\$sdkVer\um;$kitsRoot\Include\$sdkVer\shared;$kitsRoot\Include\$sdkVer\winrt;$kitsRoot\Include\$sdkVer\cppwinrt"
$env:LIB = "$msvcRoot\lib\x64;$kitsRoot\Lib\$sdkVer\ucrt\x64;$kitsRoot\Lib\$sdkVer\um\x64"

$importLib = Join-Path $NativeRoot "x64\Release\FFF.Native.lib"
if (-not (Test-Path -LiteralPath $importLib)) {
    throw "FFF.Native.lib not found: $importLib (build FFF.Native x64|Release first)."
}

New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

# /I covers the include root of FFF.Native so both "3FP/Api/FFF.Player.Api.h" and its
# nested "3FR/Api/FFF.Native.Api.h" resolve, matching the vcxproj AdditionalIncludeDirectories.
& $clExe /nologo /utf-8 /EHsc /O2 /std:c++20 /MD /W3 `
    "/I$NativeRoot" `
    $source `
    "/Fe:$ProbeDir\Fmp4SeekProbe.exe" `
    "/Fo:$ObjDir\" `
    /link $importLib user32.lib
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with exit code $LASTEXITCODE" }

$exePath = Join-Path $ProbeDir "Fmp4SeekProbe.exe"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Copy-Item -LiteralPath $exePath -Destination (Join-Path $BuildDir "Fmp4SeekProbe.exe") -Force
Write-Host "BUILD OK: $BuildDir\Fmp4SeekProbe.exe (obj: $ObjDir)"
