param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

# 传统散装发布：产出「FFF.Player.exe + 全部依赖 DLL 同目录」的目录形态。
# 与 发布3FP单文件.ps1 的区别：
#   - 不做 PublishSingleFile ⇒ exe 只有 ~0.3 MB，其余是同目录的 DLL（可增量替换、便于排查）
#   - 不内置 .NET 运行时（--self-contained false，需本机装 .NET 10 桌面运行时）
#   - **自带 FFmpeg**：非单文件发布时 CopyProductNativeToPublish 会连 DevelopmentRuntime 一起拷
#     （单文件模式不拷，见 FFF.Player.vbproj 里的 Condition）
# 仍然启用 PublishReadyToRun（项目属性）：实测冷启动 −13.0%。

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
    throw "The release destination contains the private Dolby Vision test DLL. Use a clean release directory."
}
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")
$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool
$LibassReadyMarker = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share\3f-project\libass-ready.txt"
if (-not (Test-Path -LiteralPath $LibassReadyMarker -PathType Leaf)) {
    throw "libass is not prepared. Run the *Libass.ps1 preparation script in tools first."
}
foreach ($library in @("bluray.lib")) {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\lib\$library"))) {
        throw "Disc libraries are not prepared. Run tools/准备光盘库.ps1 first."
    }
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-loose-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:DebugType=None -p:DebugSymbols=false
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player loose publish failed." }

    # 发布完整性校验：散装形态必须自带这些，否则宿主会走「核心文件缺失」早退路径。
    $Required = @(
        'FFF.Player.exe', 'FFF.Native.dll',
        'avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll',
        'avfilter-12.dll', 'swresample-7.dll', 'swscale-10.dll',
        'libc++.dll', 'libomp.dll', 'libunwind.dll',
        'libwinpthread-1.dll', 'libshaderc_shared.dll', 'libSPIRV-Tools-shared.dll'
    )
    $Missing = @($Required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $StagingDirectory $_) -PathType Leaf) })
    if ($Missing) { throw "Publish output is missing required files: $($Missing -join ', ')" }

    if (Test-Path -LiteralPath $OutputDirectory) {
        Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $StagingDirectory '*') -Destination $OutputDirectory -Recurse -Force

    $Files = @(Get-ChildItem -LiteralPath $OutputDirectory -File)
    $Bytes = ($Files | Measure-Object -Property Length -Sum).Sum
    Write-Host ("Loose publish completed: {0} ({1} files, {2:N1} MB)" -f $OutputDirectory, $Files.Count, ($Bytes / 1MB))
}
finally {
    if (Test-Path -LiteralPath $StagingDirectory) { Remove-Item -LiteralPath $StagingDirectory -Recurse -Force }
}
