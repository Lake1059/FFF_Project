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
# 产物：
#   third_party\libiamf\include\        IAMF_decoder.h IAMF_defines.h vlogging_tool_sr.h
#   third_party\libiamf\oar\include\    oar.h oar_base.h oar_metadata.h animation.h
#   third_party\libiamf\lib\x64\        iamf.lib oar.lib obr.lib obr_capi.lib pffft.lib FLAC.lib opus.lib fdk-aac.lib
#   third_party\libiamf\LICENSE.libiamf.txt
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\准备Libiamf.ps1
#   powershell -ExecutionPolicy Bypass -File tools\准备Libiamf.ps1 -RepoPath $env:TEMP\libiamf-src   # 已 clone 好的仓库；缺省用 $env:TEMP\libiamf-prepare\repo
#
# -RepoPath 指向一个已经 clone 好的 libiamf（跳过联网 clone）。两条路径产出的文件集合一致，
# 差别只是是否重新 clone/build。

param(
    [Parameter(Mandatory = $false)]
    [string]$RepoPath = "",

    [Parameter(Mandatory = $false)]
    [string]$CmakeExe = "",

    # 本机验证过的那一版；libiamf 的头与 ABI 在 main 上会动，所以钉住。
    [Parameter(Mandatory = $false)]
    [string]$PinnedCommit = "b276f4371a8ae4f370a656e914f7b07e4cd5a2fa"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$StageRoot = Join-Path $ProjectRoot "third_party\libiamf"
$WorkRoot = Join-Path $env:TEMP "libiamf-prepare"
$BuildDir = Join-Path $WorkRoot "build"

if ([string]::IsNullOrWhiteSpace($RepoPath)) {
    $RepoPath = Join-Path $WorkRoot "repo"
    if (-not (Test-Path -LiteralPath (Join-Path $RepoPath ".git"))) {
        New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
        Write-Host "clone https://github.com/AOMediaCodec/libiamf -> $RepoPath"
        git clone https://github.com/AOMediaCodec/libiamf $RepoPath
        if ($LASTEXITCODE -ne 0) { throw "git clone 失败（rc=$LASTEXITCODE）" }
    }
}
if (-not (Test-Path -LiteralPath $RepoPath)) { throw "仓库路径不存在：$RepoPath" }

Push-Location $RepoPath
try {
    $head = (git rev-parse HEAD).Trim()
    if ($head -ne $PinnedCommit) {
        Write-Host "checkout $PinnedCommit（当前 $head）"
        git fetch origin $PinnedCommit
        if ($LASTEXITCODE -ne 0) { throw "fetch 钉住的提交失败：$PinnedCommit" }
        git checkout $PinnedCommit
        if ($LASTEXITCODE -ne 0) { throw "checkout 失败：$PinnedCommit" }
    }
} finally { Pop-Location }

if ([string]::IsNullOrWhiteSpace($CmakeExe)) {
    # 不在这里硬编码某个机器的 cmake 安装路径：PATH 里找，找不到就要求显式传参。
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $found) { throw "PATH 里没有 cmake，请用 -CmakeExe <绝对路径> 指定（本脚本不猜安装位置）" }
    $CmakeExe = $found.Source
}


# /MD 是硬要求：这两个静态库按动态 CRT 构建，用 /MT 会在链接 FFF.Native 时报
# __imp_realloc / __imp_fmax / __imp__localtime64 等未解析外部符号。
Write-Host "configure（iamf + oar + OBR 双耳链）"
& $CmakeExe -S $RepoPath -B $BuildDir -A x64 -DCMAKE_CONFIGURATION_TYPES=Release `
    -DIAMF_TEST_TOOL=ON -DIAMF_BUILD_SHARED_LIB=OFF `
    -DIAMF_ENABLE_BINAURALIZER=ON
if ($LASTEXITCODE -ne 0) { throw "cmake configure 失败（rc=$LASTEXITCODE）" }
& $CmakeExe --build $BuildDir --config Release
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
Copy-Stage -SourceDir (Join-Path $BuildDir "Release") -TargetDir (Join-Path $StageRoot "lib\x64") `
    -Names @("iamf.lib")
Copy-Stage -SourceDir (Join-Path $BuildDir "dep_external\src\oar\Release") `
    -TargetDir (Join-Path $StageRoot "lib\x64") -Names @("oar.lib")

# 双耳 + 编解码传递依赖。CMake 把 dep_external 下的每个包各自输出到
# <build>\dep_external\src\<pkg>\Release 还是 <build>\Release 取决于 libiamf 那一版的
# CMakeLists，所以这里按文件名在整棵 build 树里找第一个命中项，不硬编码子目录；
# 找不到就响亮报错——绝大多数情况就是忘了 -DIAMF_ENABLE_BINAURALIZER=ON。
foreach ($spec in @(
    @{ Name = "oar.lib";       Why = "OAR 元数据/布局" },
    @{ Name = "obr.lib";       Why = "双耳 HRTF 实现（BedBinauralRenderer 的直接依赖）" },
    @{ Name = "obr_capi.lib";  Why = "obr 的 C API 封装" },
    @{ Name = "pffft.lib";     Why = "obr 的 FFT" },
    @{ Name = "FLAC.lib";      Why = "iamf.lib 的 flac_multistream_decoder.obj" },
    @{ Name = "opus.lib";      Why = "iamf.lib 的 opus_multistream2_decoder.obj" },
    @{ Name = "fdk-aac.lib";   Why = "iamf.lib 的 aac_multistream_decoder.obj" })) {
    $hit = Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter $spec.Name |
        Select-Object -First 1
    if ($null -eq $hit) {
        throw "构建产物里没有 $($spec.Name)（$($spec.Why)）。检查 configure 是否带 -DIAMF_ENABLE_BINAURALIZER=ON。"
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $StageRoot "lib\x64") | Out-Null
    Copy-Item -LiteralPath $hit.FullName -Destination (Join-Path $StageRoot "lib\x64") -Force
    Write-Host ("  staged {0} <- {1}" -f $spec.Name, $hit.FullName.Substring($BuildDir.Length + 1))
}
Copy-Stage -SourceDir $RepoPath -TargetDir $StageRoot -Names @("LICENSE")
Rename-Item -LiteralPath (Join-Path $StageRoot "LICENSE") -NewName "LICENSE.libiamf.txt" -Force

Write-Host ""
Write-Host "已落地 $StageRoot"
Get-ChildItem -LiteralPath $StageRoot -Recurse -File | ForEach-Object {
    Write-Host ("  {0,10}  {1}" -f $_.Length, $_.FullName.Substring($StageRoot.Length + 1))
}
Write-Host "接着：msbuild FFF.Native\FFF.Native.vcxproj -p:Configuration=Release -p:Platform=x64"
