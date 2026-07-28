param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $MyInvocation.MyCommand.Path
} else {
    $PSScriptRoot
}
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw "vswhere.exe was not found." }
$installations = @(& $vswhere -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
$VisualStudioDirectory = ($installations | Where-Object { $_.isPrerelease } |
    Sort-Object installationVersion -Descending | Select-Object -First 1).installationPath
if ([string]::IsNullOrWhiteSpace($VisualStudioDirectory)) { throw "Visual Studio Preview with C++ tools was not found." }
$MSBuild = Join-Path $VisualStudioDirectory "MSBuild\Current\Bin\amd64\MSBuild.exe"
if (-not (Test-Path -LiteralPath $MSBuild)) { throw "Visual Studio Preview x64 MSBuild was not found: $MSBuild" }

# FFF.Native contains the optional 3FP ASS entry points, so libass is a link-time
# dependency even though 3FR never loads its delayed runtime DLL.
& (Join-Path $ScriptDirectory "Prepare-Libass.ps1") -VisualStudioDirectory $VisualStudioDirectory

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FR-publish-" + [Guid]::NewGuid().ToString("N"))
$PendingExecutable = $null
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null

    dotnet publish (Join-Path $ProjectRoot "FFF.Recorder\FFF.Recorder.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true `
        -p:EnableMsixTooling=true `
        -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:DebugType=None -p:DebugSymbols=false
    if ($LASTEXITCODE -ne 0) { throw "FFF.Recorder single-file publish failed." }

    $OutputFiles = @(Get-ChildItem -LiteralPath $StagingDirectory -File)
    $UnexpectedFiles = $OutputFiles | Where-Object { $_.Name -ne "FFF.Recorder.exe" }
    if ($UnexpectedFiles) {
        throw "Unexpected files in single-file output: $($UnexpectedFiles.Name -join ', ')"
    }

    $PublishedExecutable = Join-Path $StagingDirectory "FFF.Recorder.exe"
    if (-not (Test-Path -LiteralPath $PublishedExecutable -PathType Leaf)) {
        throw "FFF.Recorder.exe is missing from the publish output."
    }

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $Executable = Join-Path $OutputDirectory "FFF.Recorder.exe"
    $PendingExecutable = Join-Path $OutputDirectory ("FFF.Recorder.exe.new-" + [Guid]::NewGuid().ToString("N"))
    Copy-Item -LiteralPath $PublishedExecutable -Destination $PendingExecutable
    if (Test-Path -LiteralPath $Executable -PathType Leaf) {
        Remove-Item -LiteralPath $Executable -Force
    }
    Move-Item -LiteralPath $PendingExecutable -Destination $Executable
    $PendingExecutable = $null
    $PublishedFile = Get-Item -LiteralPath $Executable
    Write-Host "Single-file publish completed: $($PublishedFile.FullName) ($($PublishedFile.Length) bytes)"
}
finally {
    if ($PendingExecutable -and (Test-Path -LiteralPath $PendingExecutable)) {
        Remove-Item -LiteralPath $PendingExecutable -Force
    }
    if (Test-Path -LiteralPath $StagingDirectory) {
        Remove-Item -LiteralPath $StagingDirectory -Recurse -Force
    }
}
