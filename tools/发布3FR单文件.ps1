param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    # Recorder 的窗口捕获与通知组件需要 .NET 11 WinRT 投影。
    [ValidatePattern('^net11\.0-windows\d+(?:\.\d+)+$')]
    [string]$NetTFM = "net11.0-windows10.0.26100.0",
    [switch]$AllowFrameworkMismatch = $true,
    [switch]$SkipIamf,
    [switch]$SkipDependencyPreparation
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
. (Join-Path $PSScriptRoot "Resolve-NativeDependencies.ps1")
. (Join-Path $PSScriptRoot "Publish-Common.ps1")

$OutputDirectory = Get-PublishOutputDirectory -ProjectRoot $ProjectRoot -OutputDirectory $OutputDirectory -DefaultDirectory "publish\win-x64"
$Project = Join-Path $ProjectRoot "FFF.Recorder\FFF.Recorder.vbproj"
$DotNet = Get-DotNetTool
$Framework = Get-ProductTargetFramework -ProjectPath $Project -NetTFM $NetTFM -DotNet $DotNet
if ($Framework -notmatch '^net11\.0-windows\d+(?:\.\d+)+$') {
    throw "3FR requires a .NET 11 Windows target with an explicit Windows SDK version. Resolved target: $Framework"
}
if (-not $AllowFrameworkMismatch) {
    Assert-ProductFrameworkPair -OutputDirectory $OutputDirectory -Product "FFF.Recorder" `
        -Framework $Framework -CheckOnly
}

Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf -SkipFfmpegRuntime
Invoke-ProductNativeBuild -MSBuild (Get-MSBuildTool) -ProjectRoot $ProjectRoot -Configuration $Configuration -SkipIamf:$SkipIamf

$StagingDirectory = New-PublishStagingDirectory
try {
    & $DotNet publish $Project -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:DebugType=None -p:DebugSymbols=false -p:EnableMsixTooling=true "-p:NetTFM=$Framework"
    if ($LASTEXITCODE -ne 0) { throw "FFF.Recorder single-file publish failed." }

    Publish-SingleExecutable -StagingDirectory $StagingDirectory -OutputDirectory $OutputDirectory `
        -Product "FFF.Recorder" -Framework $Framework `
        -AllowFrameworkMismatch:$AllowFrameworkMismatch
}
finally {
    Remove-PublishStagingDirectory -Path $StagingDirectory
}
