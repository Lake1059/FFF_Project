param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$MSBuild = "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe"
if (-not (Test-Path -LiteralPath $MSBuild)) { throw "Visual Studio x64 MSBuild was not found: $MSBuild" }

$PublishRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "publish"))
$IsDefaultOutput = [String]::Equals($OutputDirectory, [IO.Path]::GetFullPath((Join-Path $PublishRoot "win-x64")), [StringComparison]::OrdinalIgnoreCase)
if ($IsDefaultOutput -and (Test-Path -LiteralPath $OutputDirectory)) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FR-publish-" + [Guid]::NewGuid().ToString("N"))
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

    if (-not (Test-Path -LiteralPath $OutputDirectory)) {
        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    }
    $Executable = Join-Path $OutputDirectory "FFF.Recorder.exe"
    Copy-Item -LiteralPath $PublishedExecutable -Destination $Executable -Force
    Write-Host "Single-file publish completed: $Executable"
}
finally {
    if (Test-Path -LiteralPath $StagingDirectory) {
        Remove-Item -LiteralPath $StagingDirectory -Recurse -Force
    }
}
