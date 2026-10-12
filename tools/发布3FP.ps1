param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

# 发布 EXE 及依赖 DLL，包含 FFmpeg；运行时需要 .NET 10 桌面运行时。

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { Split-Path -Parent $MyInvocation.MyCommand.Path } else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64-散装"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath (Join-Path $OutputDirectory "FFF.DolbyVision.Test.dll")) {
    throw "发布目录含私有 Dolby Vision 测试 DLL，请使用干净的发布目录。"
}
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")
$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool
$LibassReadyMarker = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share\3f-project\libass-ready.txt"
if (-not (Test-Path -LiteralPath $LibassReadyMarker -PathType Leaf)) {
    throw "缺少 libass，请先运行 tools\准备Libass.ps1。"
}
if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\lib\bluray.lib") -PathType Leaf)) {
    throw "缺少光盘库，请先运行 tools\准备光盘库.ps1。"
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration 构建失败。" }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-loose-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:DebugType=None -p:DebugSymbols=false
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player 散装发布失败。" }

    # 确认运行依赖齐全后，再替换发布目录。
    $Required = @(
        'FFF.Player.exe', 'FFF.Native.dll',
        'avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll',
        'avfilter-12.dll', 'swresample-7.dll', 'swscale-10.dll',
        'ass-9.dll', 'bluray-3.dll', 'udfread-3.dll',
        'libxml2.dll', 'iconv-2.dll', 'charset-1.dll'
    )
    $Missing = @($Required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $StagingDirectory $_) -PathType Leaf) })
    if ($Missing) { throw "发布产物缺少文件：$($Missing -join ', ')" }

    if (Test-Path -LiteralPath $OutputDirectory) {
        Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $StagingDirectory -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory -Recurse -Force
    }

    $Files = @(Get-ChildItem -LiteralPath $OutputDirectory -File)
    $Bytes = ($Files | Measure-Object -Property Length -Sum).Sum
    Write-Host ("散装发布完成：{0}（{1} 个文件，{2:N1} MB）" -f $OutputDirectory, $Files.Count, ($Bytes / 1MB))
}
finally {
    if (Test-Path -LiteralPath $StagingDirectory) { Remove-Item -LiteralPath $StagingDirectory -Recurse -Force }
}
