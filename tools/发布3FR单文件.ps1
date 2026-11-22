param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [string]$NetTFM = "",
    [switch]$AllowFrameworkMismatch,
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
Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf -SkipFfmpegRuntime
Invoke-ProductNativeBuild -MSBuild (Get-MSBuildTool) -ProjectRoot $ProjectRoot -Configuration $Configuration -SkipIamf:$SkipIamf

$PublishArgs = @()
if ($NetTFM) { $PublishArgs += "-p:NetTFM=$NetTFM" }
$PublishArgs += '-p:EnableMsixTooling=true'

$StagingDirectory = New-PublishStagingDirectory
try {
    & $DotNet publish $Project -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:DebugType=None -p:DebugSymbols=false @PublishArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Recorder single-file publish failed." }

    Publish-SingleExecutable -StagingDirectory $StagingDirectory -OutputDirectory $OutputDirectory `
        -Product "FFF.Recorder" -Framework (Get-ProductTargetFramework -ProjectPath $Project -NetTFM $NetTFM -DotNet $DotNet) `
        -AllowFrameworkMismatch:$AllowFrameworkMismatch
}
finally {
    Remove-PublishStagingDirectory -Path $StagingDirectory
}
