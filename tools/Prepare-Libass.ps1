param(
    [Parameter(Mandatory = $false)]
    [string]$VisualStudioDirectory = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64-windows")]
    [string]$Triplet = "x64-windows"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Manifest = Join-Path $ProjectRoot "vcpkg.json"
$InstallRoot = Join-Path $ProjectRoot "third_party\vcpkg_installed"

function Find-VisualStudioDirectory {
    if (-not [string]::IsNullOrWhiteSpace($VisualStudioDirectory)) {
        return [IO.Path]::GetFullPath($VisualStudioDirectory)
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installations = @(& $vswhere -prerelease -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json |
            ConvertFrom-Json)
        $found = $installations | Where-Object { $_.isPrerelease } |
            Sort-Object installationVersion -Descending | Select-Object -First 1
        if ($null -ne $found) { return $found.installationPath }
    }

    $preview = "C:\Program Files\Microsoft Visual Studio\18\Insiders"
    if (Test-Path -LiteralPath $preview -PathType Container) { return $preview }
    throw "Visual Studio Preview with the C++ desktop workload was not found."
}

if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) {
    throw "The vcpkg manifest was not found: $Manifest"
}

$vsDirectory = Find-VisualStudioDirectory
$vcpkg = Join-Path $vsDirectory "VC\vcpkg\vcpkg.exe"
if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
    throw "The Visual Studio bundled vcpkg was not found: $vcpkg"
}

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
& $vcpkg install "--triplet=$Triplet" "--x-manifest-root=$ProjectRoot" "--x-install-root=$InstallRoot"
if ($LASTEXITCODE -ne 0) { throw "vcpkg could not prepare libass (exit code $LASTEXITCODE)." }

$TripletRoot = Join-Path $InstallRoot $Triplet
$RequiredFiles = @(
    (Join-Path $TripletRoot "include\ass\ass.h"),
    (Join-Path $TripletRoot "lib\ass.lib"),
    (Join-Path $TripletRoot "debug\lib\ass.lib")
)
foreach ($file in $RequiredFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "The libass package is incomplete: $file"
    }
}

$RuntimeSets = @(
    @{
        Directory = (Join-Path $TripletRoot "bin")
        Files = @("brotlicommon.dll", "brotlidec.dll", "bz2.dll", "freetype.dll",
            "fribidi-0.dll", "harfbuzz.dll", "libpng16.dll", "z.dll")
    },
    @{
        Directory = (Join-Path $TripletRoot "debug\bin")
        Files = @("brotlicommon.dll", "brotlidec.dll", "bz2d.dll", "freetyped.dll",
            "fribidi-0.dll", "harfbuzz.dll", "libpng16d.dll", "zd.dll")
    }
)
foreach ($runtimeSet in $RuntimeSets) {
    $assRuntime = @(Get-ChildItem -LiteralPath $runtimeSet.Directory -Filter "ass-*.dll" -File)
    if ($assRuntime.Count -ne 1) {
        throw "Expected one versioned libass DLL under $($runtimeSet.Directory), found $($assRuntime.Count)."
    }
    foreach ($runtimeFile in $runtimeSet.Files) {
        $runtimePath = Join-Path $runtimeSet.Directory $runtimeFile
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
            throw "The libass runtime dependency is missing: $runtimePath"
        }
    }
}

Write-Host "libass Debug and Release files are ready under $TripletRoot"
