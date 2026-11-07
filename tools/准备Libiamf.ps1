# 准备 Libiamf：把 IAMF 参考解码器落到 third_party\libiamf，供 FFF.Native 链接。
#
# 为什么需要这个脚本：内核用 libiamf 渲染 IAMF 空间音频（FFmpeg 只能给出子流，混不成目标
# 布局），而 third_party\ 被 .gitignore 排除，与 FFmpeg/Libass 同一策略 —— 所以新克隆必须
# 先跑本脚本，否则 FFF.Native.vcxproj 找不到 iamf.lib/oar.lib。
#
# 关键：必须带 -DIAMF_ENABLE_BINAURALIZER=ON。不带它 libiamf 只产 iamf.lib + oar.lib，
# 而 FFF.Native 的 IAMF 路径要 obr_create/obr_add_audio_element/obr_process（双耳）与
# opus/FLAC/fdk-aac 的 multistream 解码器（iamf.lib 内部引用它们），照“只产 iamf+oar”的取库
# Upstream's own copy omits the binauralizer switch, so following its README and then applying this bucket guarantees LNK2019 (29 unresolved externals).
#
# 产物（Release 在 lib\x64，Debug 在 lib\x64\debug）：
#   third_party\libiamf\include\        IAMF_decoder.h IAMF_defines.h vlogging_tool_sr.h
#   third_party\libiamf\oar\include\    oar.h oar_base.h oar_metadata.h animation.h
#   third_party\libiamf\lib\x64\        iamf.lib oar.lib obr.lib obr_capi.lib pffft.lib FLAC.lib opus.lib fdk-aac.lib
#   third_party\libiamf\lib\x64\absl\   工程引用的 Abseil 静态库
#   third_party\libiamf\LICENSE.libiamf.txt
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\准备Libiamf.ps1
#   powershell -ExecutionPolicy Bypass -File tools\准备Libiamf.ps1 -RepoPath $env:TEMP\libiamf-src   # 已 clone 好的仓库；缺省用 $env:TEMP\libiamf-prepare\repo
#   Git/CMake 优先使用环境配置，缺失时自动查找 Visual Studio（含 Build Tools/Preview）自带工具。
#   也可用 -CmakeExe <路径> 显式指定 CMake；无需提前打开 VS 开发者命令行。
#   构建 Debug 依赖：tools\准备Libiamf.ps1 -Configuration Debug
#   -BuildDirectory 可指定持久缓存目录；-Parallel 控制并行度（默认最多 8）。
#
# -RepoPath 指向一个已经 clone 好的 libiamf（跳过联网 clone）。两条路径产出的文件集合一致，
# 已有源码仍会进行增量构建；缺少的提交、子模块和 CMake 外部依赖仍需联网获取。

param(
    [Parameter(Mandatory = $false)]
    [string]$RepoPath = "",

    [Parameter(Mandatory = $false)]
    [string]$CmakeExe = "",

    # 本机验证过的那一版；libiamf 的头与 ABI 在 main 上会动，所以钉住。
    [Parameter(Mandatory = $false)]
    [string]$PinnedCommit = "b276f4371a8ae4f370a656e914f7b07e4cd5a2fa",

    [string]$BuildDirectory = "",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateRange(1, 64)]
    [int]$Parallel = [Math]::Max(1, [Math]::Min(8, [Environment]::ProcessorCount))
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")

# 与发布脚本共用工具发现逻辑；在 clone/fetch 之前检查，避免下载后才发现缺工具。
$GitExe = Get-GitTool
if ([string]::IsNullOrWhiteSpace($CmakeExe)) {
    $CmakeExe = Get-CMakeTool
} else {
    $found = Get-Command $CmakeExe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $found) { throw "指定的 CMake 不存在：$CmakeExe" }
    $CmakeExe = $found.Source
}
Write-Host "Git: $GitExe"
Write-Host "CMake: $CmakeExe"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$StageRoot = Join-Path $ProjectRoot "third_party\libiamf"
$WorkRoot = Join-Path $env:TEMP "libiamf-prepare"
$BuildDir = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $WorkRoot "build"
} else { [IO.Path]::GetFullPath($BuildDirectory) }

if ([string]::IsNullOrWhiteSpace($RepoPath)) {
    $RepoPath = Join-Path $WorkRoot "repo"
    if (-not (Test-Path -LiteralPath (Join-Path $RepoPath ".git"))) {
        New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
        Write-Host "clone https://github.com/AOMediaCodec/libiamf -> $RepoPath"
        & $GitExe clone https://github.com/AOMediaCodec/libiamf $RepoPath
        if ($LASTEXITCODE -ne 0) { throw "git clone 失败（rc=$LASTEXITCODE）" }
    }
}
if (-not (Test-Path -LiteralPath $RepoPath)) { throw "仓库路径不存在：$RepoPath" }
$RepoPath = (Resolve-Path -LiteralPath $RepoPath).ProviderPath

Push-Location $RepoPath
try {
    $head = & $GitExe rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw "读取仓库 HEAD 失败：$RepoPath" }
    $head = $head.Trim()
    if ($head -ne $PinnedCommit) {
        Write-Host "checkout $PinnedCommit（当前 $head）"
        & $GitExe cat-file -e "$PinnedCommit^{commit}" 2>$null
        if ($LASTEXITCODE -ne 0) {
            & $GitExe fetch origin $PinnedCommit
            if ($LASTEXITCODE -ne 0) { throw "fetch 钉住的提交失败：$PinnedCommit" }
        }
        & $GitExe checkout $PinnedCommit
        if ($LASTEXITCODE -ne 0) { throw "checkout 失败：$PinnedCommit" }
    }
    & $GitExe submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "初始化 libiamf 子模块失败（rc=$LASTEXITCODE）" }
} finally { Pop-Location }

# 动态 CRT 是硬要求：Release /MD、Debug /MDd，与 FFF.Native 配置一致。用 /MT 会报
# __imp_realloc / __imp_fmax / __imp__localtime64 等未解析外部符号。
# /DNDEBUG 显式注入（2026-11-07，docs/139 §欠账2）：上游 code\CMakeLists.txt 会向
# CMAKE_C_FLAGS 追加全局旗标（`set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")`），为防未来
# 上游改写 per-config 旗标让断言代码回归成品（docs/110 §三 实测过断言串进出货件），
# 这里把 Release 的 C/C++ 旗标显式写死（与 CMake 默认值等价，仅显式化）；Debug 不动，
# 保留 /RTC1 与断言语义。产物串实测：11-6 构建（默认旗标已含 NDEBUG）断言串已为 0。
Write-Host "configure（iamf + oar + OBR 双耳链）"
& $CmakeExe -S (Join-Path $RepoPath "code") -B $BuildDir -A x64 '-DCMAKE_CONFIGURATION_TYPES=Debug;Release' `
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL' -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_TESTING=OFF -DEIGEN_BUILD_TESTING=OFF -DIAMF_TEST_TOOL=OFF `
    -DIAMF_BUILD_SHARED_LIB=OFF -DIAMF_ENABLE_BINAURALIZER=ON `
    '-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG' '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG'
if ($LASTEXITCODE -ne 0) { throw "cmake configure 失败（rc=$LASTEXITCODE）" }
& $CmakeExe --build $BuildDir --config $Configuration --target iamf --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build 失败（rc=$LASTEXITCODE）" }

function Copy-Stage {
    param([Parameter(Mandatory = $true)][string]$SourceDir,
          [Parameter(Mandatory = $true)][string]$TargetDir,
          [Parameter(Mandatory = $true)][string[]]$Names)
    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    foreach ($name in $Names) {
        $src = Join-Path $SourceDir $name
        if (-not (Test-Path -LiteralPath $src)) { throw "缺少 $src（构建产物不完整）" }
        Copy-Item -LiteralPath $src -Destination $TargetDir -Force
    }
}

$includeDir = Join-Path $RepoPath "code\include"
$oarInclude = Join-Path $RepoPath "code\dep_external\src\oar\include"
Copy-Stage -SourceDir $includeDir -TargetDir (Join-Path $StageRoot "include") `
    -Names @("IAMF_decoder.h", "IAMF_defines.h", "vlogging_tool_sr.h")
Copy-Stage -SourceDir $oarInclude -TargetDir (Join-Path $StageRoot "oar\include") `
    -Names @("oar.h", "oar_base.h", "oar_metadata.h", "animation.h")
$libraryDirectory = Join-Path $StageRoot "lib\x64"
if ($Configuration -eq 'Debug') { $libraryDirectory = Join-Path $libraryDirectory 'debug' }
New-Item -ItemType Directory -Force -Path $libraryDirectory | Out-Null

# 双耳 + 编解码传递依赖。CMake 把 dep_external 下的每个包各自输出到
# <build>\dep_external\src\<pkg>\Release 还是 <build>\Release 取决于 libiamf 那一版的
# CMakeLists，所以按配置查找唯一命中项，避免混入另一配置或陈旧的同名库；
# 找不到就响亮报错——绝大多数情况就是忘了 -DIAMF_ENABLE_BINAURALIZER=ON。
$configurationLibraries = @(Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter *.lib |
    Where-Object { $_.Directory.Name -eq $Configuration })
foreach ($spec in @(
    @{ Name = "iamf.lib";      Why = "IAMF 解码器" },
    @{ Name = "oar.lib";       Why = "OAR 元数据/布局" },
    @{ Name = "obr.lib";       Why = "双耳 HRTF 实现（BedBinauralRenderer 的直接依赖）" },
    @{ Name = "obr_capi.lib";  Why = "obr 的 C API 封装" },
    @{ Name = "pffft.lib";     Why = "obr 的 FFT" },
    @{ Name = "FLAC.lib";      Why = "iamf.lib 的 flac_multistream_decoder.obj" },
    @{ Name = "opus.lib";      Why = "iamf.lib 的 opus_multistream2_decoder.obj" },
    @{ Name = "fdk-aac.lib";   Why = "iamf.lib 的 aac_multistream_decoder.obj" })) {
    $hits = @($configurationLibraries | Where-Object { $_.Name -eq $spec.Name })
    if ($hits.Count -ne 1) {
        throw "需要唯一的 $Configuration/$($spec.Name)（$($spec.Why)），找到 $($hits.Count) 个：$($hits.FullName -join ', ')"
    }
    $hit = $hits[0]
    Copy-Item -LiteralPath $hit.FullName -Destination $libraryDirectory -Force
    Write-Host ("  staged {0} <- {1}" -f $spec.Name, $hit.FullName.Substring($BuildDir.Length + 1))
}
# OBR 的静态依赖不能只复制 obr.lib；从工程读取所需 Abseil 名称，避免清单漂移。
. (Join-Path $PSScriptRoot "Resolve-NativeDependencies.ps1")
$abslDirectory = Join-Path $libraryDirectory "absl"
New-Item -ItemType Directory -Force -Path $abslDirectory | Out-Null
foreach ($relative in @(Get-LibiamfRequiredFiles -ProjectRoot $ProjectRoot -Configuration $Configuration | Where-Object { $_ -like '*\absl\*' })) {
    $name = Split-Path -Leaf $relative
    $hits = @($configurationLibraries | Where-Object { $_.Name -eq $name })
    if ($hits.Count -ne 1) { throw "Expected one $Configuration library for $name, found $($hits.Count)." }
    Copy-Item -LiteralPath $hits[0].FullName -Destination $abslDirectory -Force
}
Copy-Item -LiteralPath (Join-Path $RepoPath "LICENSE") -Destination (Join-Path $StageRoot "LICENSE.libiamf.txt") -Force
Assert-LibiamfReady -ProjectRoot $ProjectRoot -Configuration $Configuration

Write-Host ""
Write-Host "已落地 $StageRoot"
Get-ChildItem -LiteralPath $StageRoot -Recurse -File | ForEach-Object {
    Write-Host ("  {0,10}  {1}" -f $_.Length, $_.FullName.Substring($StageRoot.Length + 1))
}
Write-Host "接着：tools\构建3FP.ps1 -Configuration $Configuration"
