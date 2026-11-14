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
    # ReadyToRun：默认关闭（JIT 交付），参数保留作为体积/启动权衡的余量开关：
    # -ReadyToRun 实测冷启动 T2 中位 1717 ms（JIT 1893，−176 ms，docs/105 §十二同窗对照），
    # 代价 +7.84 MB 托管体积。勿同时开 PublishReadyToRunEmitSymbols
    #（与 -p:DebugType=None 冲突，docs/17 §4.1）。
    [switch]$ReadyToRun,
    # IAMF 空间音频：默认强制携带（缺料即报错，而非静默编译成无 IAMF 内核）。
    # 体积敏感/无准备环境用 -SkipIamf 显式放弃该能力（docs/105 §十三/§十四）。
    [switch]$SkipIamf,
    [switch]$SkipDependencyPreparation
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
. (Join-Path $ScriptDirectory "Resolve-NativeDependencies.ps1")
Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf

$NativeArgs = @()
if ($SkipIamf) { $NativeArgs += "/p:HaveLibiamf=false" } else { $NativeArgs += "/p:FFF_FORCE_LIBIAMF=true" }

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 @NativeArgs /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

# 单文件形态按项目分发策略：只输出 FFF.Player.exe，不复制 licenses/ 或其他
# 许可文件；许可入口由“关于”页的项目主页链接和仓库主页提供。
# 单文件模式最容易出的坑是“内核更新了、libraw.dll 没进 bundle”：内核自解压到临时目录后
# 从自身目录找 libraw.dll，而发布目录只有一个 exe。这里先卡内核 RAW 标识；bundling 由
# FFF.Player.vbproj 的 PublishSingleFile Content 项负责（libraw.dll 与内核同批自解压）。
$KernelDllBuilt = Join-Path $ProjectRoot "FFF.Native\x64\$Configuration\FFF.Native.dll"
$KernelAscii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($KernelDllBuilt))
if (-not ($KernelAscii.Contains('libraw.dll') -and $KernelAscii.Contains('FFF3FP_GetRawInfo'))) {
    throw "发布中止：$KernelDllBuilt 不含 RAW 标识（libraw.dll / FFF3FP_GetRawInfo）—— 单文件包会带着旧内核发布。"
}

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-" + [Guid]::NewGuid().ToString("N"))
$PendingExecutable = $null
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    # TFM 参数包：留空时展开为 0 个参数，命令行与改动前逐字符一致。
    $TfmArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $TfmArgs += "-p:NetTFM=$NetTFM" }
    if (-not [string]::IsNullOrWhiteSpace($LakeUITfm)) { $TfmArgs += "-p:LakeUITfm=$LakeUITfm" }
    # ReadyToRun 参数包：默认关闭（JIT 交付），-ReadyToRun 时展开（余量开关，见参数说明）。
    $R2rArgs = @()
    if ($ReadyToRun) { $R2rArgs += "-p:PublishReadyToRun=true" }
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:PublishDocumentationFiles=false `
        -p:DebugType=None -p:DebugSymbols=false @TfmArgs @R2rArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player single-file publish failed." }
    # 单文件本体包只交付 exe：发布输出里可能被 None 项带出的许可文本、许可目录、
    # AI 包声明与 ICO sidecar 在这里清掉；UI 图标回退到 exe 内嵌图标。
    foreach ($Name in @("LICENSE.txt", "LICENSING.md", "NOTICE", "THIRD-PARTY-NOTICES.md", "ICO.ico")) {
        $Path = Join-Path $StagingDirectory $Name
        if (Test-Path -LiteralPath $Path -PathType Leaf) { Remove-Item -LiteralPath $Path -Force }
    }
    $LicenseDirectory = Join-Path $StagingDirectory "licenses"
    if (Test-Path -LiteralPath $LicenseDirectory -PathType Container) { Remove-Item -LiteralPath $LicenseDirectory -Recurse -Force }
    $AiNotice = Join-Path $StagingDirectory "ai-subtitles\AI字幕包声明.txt"
    if (Test-Path -LiteralPath $AiNotice -PathType Leaf) { Remove-Item -LiteralPath $AiNotice -Force }
    $UnexpectedFiles = @(Get-ChildItem -LiteralPath $StagingDirectory -File | Where-Object { $_.Name -ne "FFF.Player.exe" })
    if ($UnexpectedFiles) { throw "Unexpected files in single-file output: $($UnexpectedFiles.Name -join ', ')" }
    # staging 双保险：本体包不随包发布任何外部模块 DLL。
    $ForbiddenModules = @(Get-ChildItem -LiteralPath $StagingDirectory -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*ExternalModule*.dll" -or $_.Name -like "*.Test.dll" })
    if ($ForbiddenModules.Count -gt 0) {
        throw "staging 目录出现外部模块 DLL（$($ForbiddenModules.FullName -join ', ')）——必须移除；本体包不随包发布任何外部模块。"
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
