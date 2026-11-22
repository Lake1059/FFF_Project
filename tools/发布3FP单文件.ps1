param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    # 目标框架。留空 = 用播放器默认值（当前 net11.0-windows，不带 Windows SDK 版本后缀）。
    # 旧口径带 SDK 版本是为了 SMTC 的 WinRT 投影；SMTC 迁到内核 COM ABI 后宿主不再需要。
    # 需要老口径/别的大版本时显式覆盖：发布3FP单文件.ps1 -NetTFM net10.0-windows10.0.26100.0
    [string]$NetTFM = "",
    # 外部商业兄弟仓 LakeUI 的目标框架，可与宿主不同（混血状态会出警告但可工作）。
    [string]$LakeUITfm = "",
    # 跨产品框架配对：默认拒绝不同 .NET 运行时版本，见 Resolve-Toolchain.ps1。
    [switch]$AllowFrameworkMismatch,
    # ReadyToRun：默认关闭（JIT 交付），参数保留作为体积/启动权衡的余量开关：
    # -ReadyToRun 实测冷启动 T2 中位 1717 ms（JIT 1893，−176 ms，发布实测 §十二同窗对照），
    # 代价 +7.84 MB 托管体积。勿同时开 PublishReadyToRunEmitSymbols
    #（与 -p:DebugType=None 冲突，单文件发布记录 §4.1）。
    [switch]$ReadyToRun,
    # IAMF 空间音频：默认强制携带（缺料即报错，而非静默编译成无 IAMF 内核）。
    # 体积敏感/无准备环境用 -SkipIamf 显式放弃该能力（发布实测 §十三/§十四）。
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
# 原生件不再作为 bundle 条目：.NET 的 bundle 压缩只对自包含发布成立（框架依赖发布被
# SDK 直接拒绝，运行时的 FDD apphost 也拒绝压缩条目，见 FFF.Player.vbproj 注释）。
# 这里改用 tools\native_payload.cs 把它们压成 Brotli 载荷，由 vbproj 内嵌进宿主程序集，
# 宿主 Core\原生载荷.vb 在启动早期解压到缓存后再加载内核。
# 清单只从工程的 WriteSingleFileNativePayloadList target 取，脚本不另写一份文件列表——
# 两处各维护一份，漂移的结果就是发出去的单文件包缺件。
$KernelDllBuilt = Join-Path $ProjectRoot "FFF.Native\x64\$Configuration\FFF.Native.dll"
$KernelAscii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($KernelDllBuilt))
if (-not ($KernelAscii.Contains('libraw.dll') -and $KernelAscii.Contains('FFF3FP_GetRawInfo'))) {
    throw "发布中止：$KernelDllBuilt 不含 RAW 标识（libraw.dll / FFF3FP_GetRawInfo）—— 单文件包会带着旧内核发布。"
}

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-" + [Guid]::NewGuid().ToString("N"))
$PayloadDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-native-payload-" + [Guid]::NewGuid().ToString("N"))
$PendingExecutable = $null
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    # 本体单文件不携带外部 FFmpeg。上一次非本体构建可能把 av*.dll/sw*.dll 留在 host bin，
    # IncludeNativeLibrariesForSelfExtract 会把它们一起吞进 bundle；先清掉这些构建残留。
    $HostBinRoot = Join-Path $ProjectRoot "FFF.Player\bin\$Configuration"
    if (Test-Path -LiteralPath $HostBinRoot) {
        Get-ChildItem -Path $HostBinRoot -Recurse -File -Include 'av*.dll','sw*.dll' -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }
    # ── 单文件原生载荷（Brotli）────────────────────────────────────────────
    # 清单口径由 vbproj 的 WriteSingleFileNativePayloadList 决定（内核 + LibRaw +
    # libass 栈 + 光盘栈，16 件）；清单为空说明依赖没准备齐，vbproj 会 fail-closed。
    New-Item -ItemType Directory -Path $PayloadDirectory -Force | Out-Null
    $PayloadList = Join-Path $PayloadDirectory "payload-list.txt"
    $global:LASTEXITCODE = $null
    & $MSBuild (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -t:WriteSingleFileNativePayloadList -nologo `
        "-p:Configuration=$Configuration" "-p:PublishSingleFile=true" "-p:NativePayloadListFile=$PayloadList"
    if ($LASTEXITCODE -ne 0) { throw "生成单文件原生载荷清单失败（$LASTEXITCODE）。" }
    if (-not (Test-Path -LiteralPath $PayloadList -PathType Leaf)) {
        throw "单文件原生载荷清单没有生成：$PayloadList"
    }
    $PayloadFile = Join-Path $PayloadDirectory "native-payload.bin"
    # 打包器是 .NET 10+ 文件式应用，必须在没有工程文件的目录里跑，否则 dotnet run 会把
    # 当前目录的工程当成入口。
    Push-Location (Join-Path $ProjectRoot "tools")
    try {
        $global:LASTEXITCODE = $null
        & $DotNet run native_payload.cs -- $PayloadFile $PayloadList
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $PayloadFile -PathType Leaf)) {
        throw "打包单文件原生载荷失败（$LASTEXITCODE）。"
    }
    $PayloadBytes = (Get-Item -LiteralPath $PayloadFile).Length
    $PayloadCount = @(Get-Content -LiteralPath $PayloadList | Where-Object { $_.Trim() }).Count
    Write-Host ("  原生载荷：{0} 件 → {1:N0} 字节（Brotli）" -f $PayloadCount, $PayloadBytes)

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
        -p:NativePayloadFile=$PayloadFile `
        -p:BodyOnly=true `
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
    if ($PayloadDirectory -and (Test-Path -LiteralPath $PayloadDirectory)) { Remove-Item -LiteralPath $PayloadDirectory -Recurse -Force }
    if (Test-Path -LiteralPath $StagingDirectory) { Remove-Item -LiteralPath $StagingDirectory -Recurse -Force }
}
