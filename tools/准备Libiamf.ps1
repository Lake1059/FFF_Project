# 准备 Libiamf：把 IAMF 参考解码器落到 third_party\libiamf，供 FFF.Native 链接。
#
# 为什么需要这个脚本：内核用 libiamf 渲染 IAMF 空间音频（FFmpeg 只能给出子流，混不成目标
# 布局），而 third_party\ 被 .gitignore 排除，与 FFmpeg/Libass 同一策略 —— 所以新克隆必须
# 先跑本脚本，否则 FFF.Native.vcxproj 找不到 iamf.lib/oar.lib。
#
# 产物：
#   third_party\libiamf\include\        IAMF_decoder.h IAMF_defines.h vlogging_tool_sr.h
#   third_party\libiamf\oar\include\    oar.h oar_base.h oar_metadata.h animation.h
#   third_party\libiamf\lib\x64\        iamf.lib oar.lib
#   third_party\libiamf\LICENSE.libiamf.txt
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\准备Libiamf.ps1
#   powershell -ExecutionPolicy Bypass -File tools\准备Libiamf.ps1 -RepoPath D:\cache\libiamf
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
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $found) {
        $fallback = "C:\Program Files\CMake\bin\cmake.exe"
        if (-not (Test-Path -LiteralPath $fallback)) { throw "找不到 cmake，请传 -CmakeExe" }
        $CmakeExe = $fallback
    } else { $CmakeExe = $found.Source }
}

# /MD 是硬要求：这两个静态库按动态 CRT 构建，用 /MT 会在链接 FFF.Native 时报
# __imp_realloc / __imp_fmax / __imp__localtime64 等未解析外部符号。
Write-Host "configure（iamf 静态库 + oar）"
& $CmakeExe -S $RepoPath -B $BuildDir -A x64 -DCMAKE_CONFIGURATION_TYPES=Release `
    -DIAMF_TEST_TOOL=ON -DIAMF_BUILD_SHARED_LIB=OFF
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
Copy-Stage -SourceDir $RepoPath -TargetDir $StageRoot -Names @("LICENSE")
Rename-Item -LiteralPath (Join-Path $StageRoot "LICENSE") -NewName "LICENSE.libiamf.txt" -Force

Write-Host ""
Write-Host "已落地 $StageRoot"
Get-ChildItem -LiteralPath $StageRoot -Recurse -File | ForEach-Object {
    Write-Host ("  {0,10}  {1}" -f $_.Length, $_.FullName.Substring($StageRoot.Length + 1))
}
Write-Host "接着：msbuild FFF.Native\FFF.Native.vcxproj -p:Configuration=Release -p:Platform=x64"
