param(
    [Parameter(Mandatory = $false)]
    [string]$RuntimeSource = "",

    [Parameter(Mandatory = $false)]
    [string]$SourceDirectory = "$env:LOCALAPPDATA\Temp\fff-ffmpeg-source",

    [Parameter(Mandatory = $false)]
    [string]$DownloadUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip",

    [Parameter(Mandatory = $false)]
    [string]$DownloadDirectory = "$env:LOCALAPPDATA\fff-ffmpeg-download",

    [switch]$ForceDownload
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ThirdPartyDirectory = Join-Path $ProjectRoot "third_party\ffmpeg"
$IncludeDirectory = Join-Path $ThirdPartyDirectory "include"
$LibraryDirectory = Join-Path $ThirdPartyDirectory "lib\x64"
$RuntimeDirectory = Join-Path $ProjectRoot "runtime"
$PinnedCommit = "7f6b35d6c804ed8d1bc517c33044f075d72852ba"
$Components = @("libavcodec", "libavformat", "libavutil", "libswresample")
$RuntimeFiles = @(
    @{ ImportName = "avcodec-63"; Prefix = "avcodec" },
    @{ ImportName = "avformat-63"; Prefix = "avformat" },
    @{ ImportName = "avutil-61"; Prefix = "avutil" },
    @{ ImportName = "swresample-7"; Prefix = "swresample" }
)

function Get-VisualCppTool {
    $installRoot = "C:\Program Files\Microsoft Visual Studio\18"
    $tool = Get-ChildItem -LiteralPath $installRoot -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $_.FullName "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue } |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($null -eq $tool) {
        throw "Visual C++ v145 x64 build tools were not found. Install the Desktop development with C++ workload."
    }
    return $tool
}

function Get-RuntimeDll {
    param(
        [Parameter(Mandatory = $true)] [string]$Directory,
        [Parameter(Mandatory = $true)] [string]$Prefix
    )

    $pattern = "^" + [regex]::Escape($Prefix) + "(?:-\d+)?\.dll$"
    $matches = @(Get-ChildItem -LiteralPath $Directory -File -Recurse |
        Where-Object { $_.Name -match $pattern })
    if ($matches.Count -ne 1) {
        $names = if ($matches.Count -eq 0) { "none" } else { $matches.FullName -join ", " }
        throw "Expected exactly one $Prefix DLL under $Directory, found: $names"
    }
    return $matches[0]
}

function Invoke-Git {
    param([Parameter(Mandatory = $true)] [string[]]$Arguments)

    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git $($Arguments -join ' ')"
    }
}

if ([string]::IsNullOrWhiteSpace($RuntimeSource)) {
    New-Item -ItemType Directory -Force -Path $DownloadDirectory | Out-Null
    $archiveName = Split-Path -Leaf ([Uri]$DownloadUrl).AbsolutePath
    if ([string]::IsNullOrWhiteSpace($archiveName)) { $archiveName = "ffmpeg-win64-shared.zip" }
    $archivePath = Join-Path $DownloadDirectory $archiveName
    $extractDirectory = Join-Path $DownloadDirectory "extracted"

    if ($ForceDownload -or -not (Test-Path -LiteralPath $archivePath)) {
        Invoke-WebRequest -Uri $DownloadUrl -OutFile $archivePath
    }
    if ($ForceDownload -and (Test-Path -LiteralPath $extractDirectory)) {
        Remove-Item -LiteralPath $extractDirectory -Recurse -Force
    }
    if (-not (Test-Path -LiteralPath $extractDirectory)) {
        Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDirectory -Force
    }
    $RuntimeSource = $extractDirectory
}

$RuntimeSource = [IO.Path]::GetFullPath($RuntimeSource)
if (-not (Test-Path -LiteralPath $RuntimeSource -PathType Container)) {
    throw "FFmpeg runtime directory was not found: $RuntimeSource"
}

if (-not (Test-Path -LiteralPath $SourceDirectory)) {
    Invoke-Git -Arguments @("clone", "https://github.com/FFmpeg/FFmpeg.git", $SourceDirectory)
}
$currentCommit = (& git -C $SourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the FFmpeg source revision in $SourceDirectory"
}
if ($currentCommit -ne $PinnedCommit) {
    Invoke-Git -Arguments @("-C", $SourceDirectory, "fetch", "origin", $PinnedCommit)
    Invoke-Git -Arguments @("-C", $SourceDirectory, "checkout", "--detach", $PinnedCommit)
}

New-Item -ItemType Directory -Force -Path $IncludeDirectory, $LibraryDirectory, $RuntimeDirectory | Out-Null
foreach ($Component in $Components) {
    $Destination = Join-Path $IncludeDirectory $Component
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $SourceDirectory $Component) -Filter "*.h" -File |
        Copy-Item -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $SourceDirectory "COPYING.LGPLv2.1") -Destination $ThirdPartyDirectory -Force
Copy-Item -LiteralPath (Join-Path $SourceDirectory "COPYING.LGPLv3") -Destination $ThirdPartyDirectory -Force

$tool = Get-VisualCppTool
$Dumpbin = Join-Path $tool.FullName "bin\Hostx64\x64\dumpbin.exe"
$Lib = Join-Path $tool.FullName "bin\Hostx64\x64\lib.exe"
if (-not (Test-Path -LiteralPath $Dumpbin) -or -not (Test-Path -LiteralPath $Lib)) {
    throw "Visual C++ x64 linker tools are missing from $($tool.FullName)"
}

foreach ($item in $RuntimeFiles) {
    $SourceDll = Get-RuntimeDll -Directory $RuntimeSource -Prefix $item.Prefix
    $sourceIsRuntimeDirectory = [string]::Equals(
        ([IO.Path]::GetFullPath($SourceDll.DirectoryName)).TrimEnd('\'),
        ([IO.Path]::GetFullPath($RuntimeDirectory)).TrimEnd('\'),
        [StringComparison]::OrdinalIgnoreCase)
    $namePattern = "^" + [regex]::Escape($item.Prefix) + "(?:-\d+)?\.dll$"
    if (-not $sourceIsRuntimeDirectory) {
        Get-ChildItem -LiteralPath $RuntimeDirectory -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match $namePattern } |
            Remove-Item -Force
        Copy-Item -LiteralPath $SourceDll.FullName -Destination $RuntimeDirectory -Force
    }
    $Dump = & $Dumpbin /nologo /exports $SourceDll.FullName
    $Symbols = $Dump | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)\s*$') { $Matches[1] }
    }
    if (-not $Symbols) {
        throw "No exported symbols were found in $($SourceDll.FullName)"
    }

    $DefFile = Join-Path $LibraryDirectory ($item.ImportName + ".def")
    @("LIBRARY `"$($item.ImportName)`"", "EXPORTS") + $Symbols |
        Set-Content -LiteralPath $DefFile -Encoding ASCII
    & $Lib /nologo /machine:x64 "/def:$DefFile" "/out:$(Join-Path $LibraryDirectory ($item.ImportName + '.lib'))"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate $($item.ImportName).lib"
    }
}

Write-Host "FFmpeg headers, import libraries, and runtime DLLs are ready."
Write-Host "Native accepts avcodec, avformat, avutil, and swresample DLL names with an optional numeric suffix."
