param([string]$VisualStudioDirectory = "")

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot 'Resolve-Toolchain.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$installRoot = Join-Path $projectRoot 'third_party\libraw'
$vsDirectory = Get-VisualStudioInstallation -PreferredDirectory $VisualStudioDirectory
$vsDevCmd = Join-Path $vsDirectory 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
    throw "Visual Studio developer command prompt was not found: $vsDevCmd"
}

# The upstream Win64 binary lacks X3F support. Build the DLL and import library
# together from the pinned source so the native and managed RAW paths agree.
$version = '0.22.3'
$archiveName = "LibRaw-$version.zip"
$archiveSha256 = '114CA5CF9F774114CB99CE7ACBB521AF5F6895E76372098EA4BEF5741BD089AC'
$tempRoot = (Get-Item -LiteralPath ([IO.Path]::GetTempPath())).FullName.TrimEnd('\')
$tempDirectory = Join-Path $tempRoot ('fff-libraw-' + [Guid]::NewGuid().ToString('N'))
$workDirectory = (New-Item -ItemType Directory -Path $tempDirectory).FullName
try {
    $archivePath = Join-Path $workDirectory $archiveName
    Invoke-WebRequest -Uri "https://www.libraw.org/data/$archiveName" -OutFile $archivePath -UseBasicParsing
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $archiveSha256) {
        throw "Unexpected SHA-256 for $archiveName."
    }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $workDirectory
    $sourceDirectory = Join-Path $workDirectory "LibRaw-$version"

    $previousVsDevCmd = $env:FFF_LIBRAW_VS_DEVCMD
    Push-Location -LiteralPath $sourceDirectory
    try {
        $env:FFF_LIBRAW_VS_DEVCMD = $vsDevCmd
        & $env:ComSpec /d /c 'call "%FFF_LIBRAW_VS_DEVCMD%" -no_logo -arch=x64 -host_arch=x64 && nmake /nologo /f Makefile.msvc "COPT_OPT=/DUSE_X3FTOOLS" bin\libraw.dll'
        if ($LASTEXITCODE -ne 0) { throw "LibRaw x64 build failed (exit $LASTEXITCODE)." }
    }
    finally {
        $env:FFF_LIBRAW_VS_DEVCMD = $previousVsDevCmd
        Pop-Location
    }

    $files = [ordered]@{
        'LICENSE.LGPL' = 'LICENSE.LGPL'
        'LICENSE.CDDL' = 'LICENSE.CDDL'
        'COPYRIGHT' = 'COPYRIGHT'
        'lib\libraw.lib' = 'lib\x64\libraw.lib'
        'bin\libraw.dll' = 'bin\x64\libraw.dll'
    }
    foreach ($file in $files.Keys) {
        if (-not (Test-Path -LiteralPath (Join-Path $sourceDirectory $file) -PathType Leaf)) {
            throw "Missing LibRaw output: $file"
        }
    }

    $includeDirectory = Join-Path $installRoot 'include\libraw'
    New-Item -ItemType Directory -Path $includeDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $sourceDirectory 'libraw') -Filter '*.h' -File |
        Copy-Item -Destination $includeDirectory -Force
    foreach ($file in $files.Keys) {
        $destination = Join-Path $installRoot $files[$file]
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $sourceDirectory $file) -Destination $destination -Force
    }
    Write-Host "LibRaw $version x64 (X3F) is ready under $installRoot"
}
finally {
    $resolvedWorkDirectory = (Resolve-Path -LiteralPath $workDirectory).ProviderPath
    if ([string]::Equals($resolvedWorkDirectory, $tempDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedWorkDirectory -Recurse -Force
    }
}
