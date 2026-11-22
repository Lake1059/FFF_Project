param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [string]$NetTFM = "",
    [switch]$AllowFrameworkMismatch = $true,
    [switch]$ReadyToRun,
    [switch]$SkipIamf,
    [switch]$SkipDependencyPreparation
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
. (Join-Path $PSScriptRoot "Resolve-NativeDependencies.ps1")
. (Join-Path $PSScriptRoot "Publish-Common.ps1")

$OutputDirectory = Get-PublishOutputDirectory -ProjectRoot $ProjectRoot -OutputDirectory $OutputDirectory -DefaultDirectory "publish\win-x64"
$Project = Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj"
$DotNet = Get-DotNetTool
$Framework = Get-ProductTargetFramework -ProjectPath $Project -NetTFM $NetTFM -DotNet $DotNet
if (-not $AllowFrameworkMismatch) {
    Assert-ProductFrameworkPair -OutputDirectory $OutputDirectory -Product "FFF.Player" `
        -Framework $Framework -CheckOnly
}

Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf -SkipFfmpegRuntime
Invoke-ProductNativeBuild -MSBuild (Get-MSBuildTool) -ProjectRoot $ProjectRoot -Configuration $Configuration -SkipIamf:$SkipIamf
Assert-PlayerRawKernel -ProjectRoot $ProjectRoot -Configuration $Configuration

$PublishArgs = @()
if ($NetTFM) { $PublishArgs += "-p:NetTFM=$NetTFM" }
$PublishArgs += '-p:BodyOnly=true', '-p:PublishDocumentationFiles=false'
if ($ReadyToRun) { $PublishArgs += '-p:PublishReadyToRun=true' }

$StagingDirectory = New-PublishStagingDirectory
try {
    & $DotNet publish $Project -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:DebugType=None -p:DebugSymbols=false @PublishArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player single-file publish failed." }

    Publish-SingleExecutable -StagingDirectory $StagingDirectory -OutputDirectory $OutputDirectory `
        -Product "FFF.Player" -Framework $Framework `
        -AllowFrameworkMismatch:$AllowFrameworkMismatch
}
finally {
    Remove-PublishStagingDirectory -Path $StagingDirectory
}
