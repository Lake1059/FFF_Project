param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    # 目标框架。留空 = 用播放器默认值（当前 net10.0-windows）。
    # .NET 11 正式版发布后改为：发布3FP单文件.ps1 -NetTFM net11.0-windows10.0.26100.0
    [string]$NetTFM = "",
    # 外部商业兄弟仓 LakeUI 的目标框架，可与宿主不同（混血状态会出警告但可工作）。
    [string]$LakeUITfm = "",
    # 跨产品框架配对：默认拒绝不同 .NET 运行时版本，见 Resolve-Toolchain.ps1。
    [switch]$AllowFrameworkMismatch,
    # IAMF 空间音频：默认强制携带（缺料即报错，而非静默编译成无 IAMF 内核）。
    # 体积敏感/无准备环境用 -SkipIamf 显式放弃该能力（docs/105 §十三/§十四）。
    [switch]$SkipIamf
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { Split-Path -Parent $MyInvocation.MyCommand.Path } else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
# Publishing uses a clean staging directory. Existing sidecar extensions in the
# destination are independent of the bundle and must not prevent updating the host.
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

# IAMF 空间音频：官方交付默认携带。HaveLibiamf 的自动探测在缺料时会静默降级成
# 无 IAMF 内核（docs/105 §十三/§十四：上游发布即此形态），故这里前置校验 + 强制门禁；
# -SkipIamf 显式放弃时不校验、并显式传 HaveLibiamf=false。
if (-not $SkipIamf) {
    $LibiamfHeader = Join-Path $ProjectRoot "third_party\libiamf\include\IAMF_decoder.h"
    $LibiamfImportLib = Join-Path $ProjectRoot "third_party\libiamf\lib\x64\iamf.lib"
    if (-not (Test-Path -LiteralPath $LibiamfHeader -PathType Leaf) -or
        -not (Test-Path -LiteralPath $LibiamfImportLib -PathType Leaf)) {
        throw "IAMF 空间音频依赖未就绪：缺少 $LibiamfHeader 或 $LibiamfImportLib。请先运行 tools/准备Libiamf.ps1，或用 -SkipIamf 显式放弃该能力。"
    }
}

$NativeArgs = @()
if ($SkipIamf) { $NativeArgs += "/p:HaveLibiamf=false" } else { $NativeArgs += "/p:FFF_FORCE_LIBIAMF=true" }

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 @NativeArgs /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-" + [Guid]::NewGuid().ToString("N"))
$PendingExecutable = $null
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    # TFM 参数包：留空时展开为 0 个参数，命令行与改动前逐字符一致。
    $TfmArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $TfmArgs += "-p:NetTFM=$NetTFM" }
    if (-not [string]::IsNullOrWhiteSpace($LakeUITfm)) { $TfmArgs += "-p:LakeUITfm=$LakeUITfm" }
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:PublishDocumentationFiles=false `
        -p:DebugType=None -p:DebugSymbols=false @TfmArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player single-file publish failed." }
    $UnexpectedFiles = @(Get-ChildItem -LiteralPath $StagingDirectory -File | Where-Object { $_.Name -ne "FFF.Player.exe" })
    if ($UnexpectedFiles) { throw "Unexpected files in single-file output: $($UnexpectedFiles.Name -join ', ')" }
    # 侵权护栏（tripwire）：单文件 bundle 由 GenerateSingleFileBundle 生成，vbproj 的
    # RejectPrivateColorExtensionPublish（BeforeTargets=GenerateSingleFileBundle）已拦一道；
    # 这里再查一次 staging 做双保险。旧商标名 FFF.DolbyVision.Test.dll 永不入包；
    # 中性模块 FFF.HDRdp.Test.dll 按设计放行（条件精确匹配旧名，不误伤新名）。
    $ForbiddenLegacy = @(Get-ChildItem -LiteralPath $StagingDirectory -Filter "FFF.DolbyVision.Test.dll" -File -ErrorAction SilentlyContinue)
    if ($ForbiddenLegacy.Count -gt 0) {
        throw "侵权护栏：staging 目录出现旧商标私有 DLL FFF.DolbyVision.Test.dll（$($ForbiddenLegacy.FullName -join ', ')）——必须移除；中性模块请使用 FFF.HDRdp.Test.dll。"
    }
    $PublishedExecutable = Join-Path $StagingDirectory "FFF.Player.exe"
    if (-not (Test-Path -LiteralPath $PublishedExecutable -PathType Leaf)) { throw "FFF.Player.exe is missing from publish output." }
    Assert-ProductFrameworkPair -OutputDirectory $OutputDirectory -Product "FFF.Player" `
        -Framework (Get-ProductTargetFramework -ProjectPath (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
            -NetTFM $NetTFM -DotNet $DotNet) `
        -AllowMismatch:$AllowFrameworkMismatch
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $Executable = Join-Path $OutputDirectory "FFF.Player.exe"
    $PendingExecutable = Join-Path $OutputDirectory ("FFF.Player.exe.new-" + [Guid]::NewGuid().ToString("N"))
    Copy-Item -LiteralPath $PublishedExecutable -Destination $PendingExecutable
    if (Test-Path -LiteralPath $Executable -PathType Leaf) {
        [IO.File]::Replace($PendingExecutable, $Executable, [NullString]::Value)
    } else {
        Move-Item -LiteralPath $PendingExecutable -Destination $Executable
    }
    $PendingExecutable = $null
    $PublishedFile = Get-Item -LiteralPath $Executable
    Write-Host "Single-file publish completed: $($PublishedFile.FullName) ($($PublishedFile.Length) bytes)"
}
finally {
    if ($PendingExecutable -and (Test-Path -LiteralPath $PendingExecutable)) { Remove-Item -LiteralPath $PendingExecutable -Force }
    if (Test-Path -LiteralPath $StagingDirectory) { Remove-Item -LiteralPath $StagingDirectory -Recurse -Force }
}
