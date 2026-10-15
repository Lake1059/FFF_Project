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

# ⚠ 变体不符警示（2026-10-14 实盘查证，勿删）：
#   盘上 runtime\*.dll 经 avutil_configuration() 实测是 **nonfree-shared** 变体
#   （--enable-gpl --enable-nonfree --enable-libx264 --enable-libx265
#    --enable-libfdk-aac --enable-libplacebo …，BtbN 构建农场 /home/dominic/ffmpeg/bin），
#   与上面 $DownloadUrl 默认的 lgpl-shared **不是同一件**：
#     · lgpl-shared 明确不含 x264/x265 与 fdk-aac（BtbN README 变体定义）；
#     · 按默认 URL 重新下载会**静默丢掉** libx264/libx265 编码器与 libfdk_aac，
#       以及 libplacebo 滤镜链（HDR 色调映射的承接线）——能力矩阵整体降级。
#   换变体（lgpl ⇄ nonfree）属**团队 license 决策**，不要顺手改 URL：
#   nonfree 变体含 GPL(x264/x265)+nonfree(fdk-aac) 组件，分发合规性需另行评估。
#   刷新 runtime 前先读 3fp/docs/98（FFmpeg 能力矩阵与变体查证）。
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
$ThirdPartyDirectory = Join-Path $ProjectRoot "third_party\ffmpeg"
$IncludeDirectory = Join-Path $ThirdPartyDirectory "include"
$LibraryDirectory = Join-Path $ThirdPartyDirectory "lib\x64"
$RuntimeDirectory = Join-Path $ProjectRoot "runtime"
$PinnedCommit = "7f6b35d6c804ed8d1bc517c33044f075d72852ba"
$Components = @("libavcodec", "libavformat", "libavutil", "libswresample", "libswscale", "libavfilter")
$RuntimeFiles = @(
    @{ ImportName = "avcodec-63"; Prefix = "avcodec" },
    @{ ImportName = "avformat-63"; Prefix = "avformat" },
    @{ ImportName = "avutil-61"; Prefix = "avutil" },
    @{ ImportName = "swresample-7"; Prefix = "swresample" },
    @{ ImportName = "swscale-10"; Prefix = "swscale" },
    @{ ImportName = "avfilter-12"; Prefix = "avfilter" }
)

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
foreach ($Component in $Components) {
    $Destination = Join-Path $IncludeDirectory $Component
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $SourceDirectory $Component) -Filter "*.h" -File |
        Copy-Item -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $SourceDirectory "COPYING.LGPLv2.1") -Destination $ThirdPartyDirectory -Force
Copy-Item -LiteralPath (Join-Path $SourceDirectory "COPYING.LGPLv3") -Destination $ThirdPartyDirectory -Force

$Dumpbin = Get-VisualCppTool -Name "dumpbin.exe"
$Lib = Get-VisualCppTool -Name "lib.exe"

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
Write-Host "Native accepts all prepared FFmpeg DLL names with an optional numeric suffix."
