param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ($PSScriptRoot) { $PSScriptRoot } else {
    Split-Path -Parent $MyInvocation.MyCommand.Path
}
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")

$MSBuild = Get-MSBuildTool
$Project = Join-Path $ProjectRoot "FFF.Player.Tests\假模块\假外部模块.vcxproj"
if (-not (Test-Path -LiteralPath $Project -PathType Leaf)) {
    throw "假模块工程不存在：$Project"
}

& $MSBuild $Project /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "假模块 x64 $Configuration 构建失败。" }

$Built = Join-Path $ProjectRoot "FFF.Player.Tests\假模块\bin\$Configuration\FFF.HDRdp.Test.dll"
if (-not (Test-Path -LiteralPath $Built -PathType Leaf)) {
    throw "假模块构建产物不存在：$Built"
}

# 测试临时目录固定在 %TEMP%\FFF3FP-FakeModule；测试只从这里取，不污染 exe 目录，
# 也不把任何外部模块 DLL 放进仓库的发布 staging。
$TempDirectory = Join-Path ([IO.Path]::GetTempPath()) "FFF3FP-FakeModule"
New-Item -ItemType Directory -Path $TempDirectory -Force | Out-Null
$ModuleName = Join-Path $TempDirectory "FFF.HDRdp.Test.dll"
Copy-Item -LiteralPath $Built -Destination $ModuleName -Force

Write-Host "假模块已输出："
Write-Host "  $ModuleName"
