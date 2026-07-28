param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { Split-Path -Parent $MyInvocation.MyCommand.Path } else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")
$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool
$LibassReadyMarker = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share\3f-project\libass-ready.txt"
if (-not (Test-Path -LiteralPath $LibassReadyMarker -PathType Leaf)) {
    throw "libass is not prepared. Run the *Libass.ps1 preparation script in tools first."
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-" + [Guid]::NewGuid().ToString("N"))
$PendingExecutable = $null
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:DebugType=None -p:DebugSymbols=false
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player single-file publish failed." }
    $UnexpectedFiles = @(Get-ChildItem -LiteralPath $StagingDirectory -File | Where-Object { $_.Name -ne "FFF.Player.exe" })
    if ($UnexpectedFiles) { throw "Unexpected files in single-file output: $($UnexpectedFiles.Name -join ', ')" }
    $PublishedExecutable = Join-Path $StagingDirectory "FFF.Player.exe"
    if (-not (Test-Path -LiteralPath $PublishedExecutable -PathType Leaf)) { throw "FFF.Player.exe is missing from publish output." }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $Executable = Join-Path $OutputDirectory "FFF.Player.exe"
    $PendingExecutable = Join-Path $OutputDirectory ("FFF.Player.exe.new-" + [Guid]::NewGuid().ToString("N"))
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
    if ($PendingExecutable -and (Test-Path -LiteralPath $PendingExecutable)) { Remove-Item -LiteralPath $PendingExecutable -Force }
    if (Test-Path -LiteralPath $StagingDirectory) { Remove-Item -LiteralPath $StagingDirectory -Recurse -Force }
}
