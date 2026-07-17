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

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

if (Test-Path -LiteralPath $OutputDirectory) {
    $AllowedRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "publish"))
    if (-not $OutputDirectory.StartsWith($AllowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Automatic cleanup is restricted to the project publish directory: $OutputDirectory"
    }
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}

dotnet publish (Join-Path $ProjectRoot "FFF.Recorder\FFF.Recorder.vbproj") `
    -c $Configuration -r win-x64 --self-contained false -o $OutputDirectory `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:DebugType=None -p:DebugSymbols=false
if ($LASTEXITCODE -ne 0) { throw "FFF.Recorder single-file publish failed." }

$ForbiddenRuntime = Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object {
    $_.Name -match '^(avcodec|avformat|avutil|swresample)(?:-\d+)?\.dll$'
}
if ($ForbiddenRuntime) {
    throw "FFmpeg runtime DLLs were unexpectedly published: $($ForbiddenRuntime.Name -join ', ')"
}

$Executable = Join-Path $OutputDirectory "FFF.Recorder.exe"
if (-not (Test-Path -LiteralPath $Executable)) { throw "FFF.Recorder.exe is missing from the publish directory." }
$ExtraFiles = Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object { $_.Name -ne "FFF.Recorder.exe" }
$UnexpectedFiles = $ExtraFiles | Where-Object Name -ne "FFF.Native.dll"
if ($UnexpectedFiles) {
    throw "Unexpected files in single-file output: $($UnexpectedFiles.Name -join ', ')"
}

Write-Host "Single-file publish completed: $Executable"
if (Test-Path -LiteralPath (Join-Path $OutputDirectory "FFF.Native.dll")) {
    Write-Host "FFF.Native.dll remains a separate product file; FFmpeg runtime DLLs are excluded."
}
