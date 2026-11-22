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

# 修补开发文件时优先复用本地的整套 DLL，避免更换构建变体。
# 首次准备或 -ForceDownload 使用 LGPL shared URL；替换 DLL 可能移除 GPL 编码器。
# 更换 FFmpeg 变体前先比较实际能力和分发许可，勿当作常规依赖更新。
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
. (Join-Path $PSScriptRoot "Resolve-NativeDependencies.ps1")
$ThirdPartyDirectory = Join-Path $ProjectRoot "third_party\ffmpeg"
$IncludeDirectory = Join-Path $ThirdPartyDirectory "include"
$LibraryDirectory = Join-Path $ThirdPartyDirectory "lib\x64"
$RuntimeDirectory = Join-Path $ThirdPartyDirectory "bin\x64"
$PinnedCommit = "7f6b35d6c804ed8d1bc517c33044f075d72852ba"
$Components = @(Get-FFmpegComponentNames -ProjectRoot $ProjectRoot)
$RuntimeFiles = @($Components | ForEach-Object { "$_.dll" })

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)] [string]$Executable,
        [Parameter(Mandatory = $true)] [string[]]$Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: $Executable $($Arguments -join ' ')"
    }
}

$Git = Get-GitTool

if ([string]::IsNullOrWhiteSpace($RuntimeSource) -and -not $ForceDownload -and
    -not (Get-MissingDependencyFiles -Root $RuntimeDirectory -RelativePaths $RuntimeFiles)) {
    $RuntimeSource = $RuntimeDirectory
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
        $downloadRoot = (Resolve-Path -LiteralPath $DownloadDirectory).ProviderPath
        $extractDirectory = (Resolve-Path -LiteralPath $extractDirectory).ProviderPath
        if ($extractDirectory -ne (Join-Path $downloadRoot 'extracted') -or
            (Get-Item -LiteralPath $extractDirectory -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
            throw "Unexpected FFmpeg extraction path: $extractDirectory"
        }
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

$SourceDlls = foreach ($name in $RuntimeFiles) {
    $files = @(Get-ChildItem -LiteralPath $RuntimeSource -Filter $name -File -Recurse)
    if ($files.Count -ne 1) { throw "Expected one $name under $RuntimeSource, found $($files.Count)." }
    $files[0]
}

if (-not (Test-Path -LiteralPath $SourceDirectory)) {
    Invoke-Git -Executable $Git -Arguments @("clone", "https://github.com/FFmpeg/FFmpeg.git", $SourceDirectory)
}
$currentCommit = (& $Git -C $SourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the FFmpeg source revision in $SourceDirectory"
}
if ($currentCommit -ne $PinnedCommit) {
    Invoke-Git -Executable $Git -Arguments @("-C", $SourceDirectory, "fetch", "origin", $PinnedCommit)
    Invoke-Git -Executable $Git -Arguments @("-C", $SourceDirectory, "checkout", "--detach", $PinnedCommit)
}

New-Item -ItemType Directory -Force -Path $IncludeDirectory, $LibraryDirectory, $RuntimeDirectory | Out-Null
foreach ($component in $Components) {
    $headers = 'lib' + $component.Split('-')[0]
    $Destination = Join-Path $IncludeDirectory $headers
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $SourceDirectory $headers) -Filter "*.h" -File |
        Copy-Item -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $SourceDirectory "COPYING.LGPLv2.1") -Destination $ThirdPartyDirectory -Force
Copy-Item -LiteralPath (Join-Path $SourceDirectory "COPYING.LGPLv3") -Destination $ThirdPartyDirectory -Force

$Dumpbin = Get-VisualCppTool -Name "dumpbin.exe"
$Lib = Get-VisualCppTool -Name "lib.exe"

foreach ($sourceDll in $SourceDlls) {
    $destination = Join-Path $RuntimeDirectory $sourceDll.Name
    if ($sourceDll.FullName -ne $destination) {
        Copy-Item -LiteralPath $sourceDll.FullName -Destination $destination -Force
    }
    $Dump = & $Dumpbin /nologo /exports $sourceDll.FullName
    if ($LASTEXITCODE -ne 0) { throw "Failed to read exports from $($sourceDll.FullName)" }
    $Symbols = $Dump | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)\s*$') { $Matches[1] }
    }
    if (-not $Symbols) {
        throw "No exported symbols were found in $($SourceDll.FullName)"
    }

    $DefFile = Join-Path $LibraryDirectory ($sourceDll.BaseName + ".def")
    @("LIBRARY `"$($sourceDll.BaseName)`"", "EXPORTS") + $Symbols |
        Set-Content -LiteralPath $DefFile -Encoding ASCII
    & $Lib /nologo /machine:x64 "/def:$DefFile" "/out:$(Join-Path $LibraryDirectory ($sourceDll.BaseName + '.lib'))"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate $($sourceDll.BaseName).lib"
    }
}

Write-Host "FFmpeg headers, import libraries, and runtime DLLs are ready."
Write-Host "FFmpeg runtime: $RuntimeDirectory"
