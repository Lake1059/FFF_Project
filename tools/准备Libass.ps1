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
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")

function Add-ProcessPath {
    param([Parameter(Mandatory = $true)] [string]$Directory)

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { return }
    $fullDirectory = [IO.Path]::GetFullPath($Directory).TrimEnd('\')
    $present = @($env:PATH -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        [string]::Equals($_.Trim().Trim('"').TrimEnd('\'), $fullDirectory,
            [StringComparison]::OrdinalIgnoreCase)
    }).Count -ne 0
    if (-not $present) { $env:PATH = "$fullDirectory;$env:PATH" }
}

if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) {
    throw "The vcpkg manifest was not found: $Manifest"
}

$preferredVsDirectory = if ([string]::IsNullOrWhiteSpace($VisualStudioDirectory)) {
    ""
} else {
    Get-VisualStudioInstallation -PreferredDirectory $VisualStudioDirectory
}
$vcpkg = Get-VcpkgTool -PreferredVisualStudioDirectory $preferredVsDirectory
$git = Get-GitTool -PreferredVisualStudioDirectory $preferredVsDirectory
$cmake = Get-CMakeTool -PreferredVisualStudioDirectory $preferredVsDirectory -AllowMissing
$ninja = Get-NinjaTool -PreferredVisualStudioDirectory $preferredVsDirectory -AllowMissing
$originalPath = $env:PATH
$originalForceSystemBinaries = $env:VCPKG_FORCE_SYSTEM_BINARIES
try {
    Add-ProcessPath -Directory (Split-Path -Parent $git)
    if ($null -ne $cmake) { Add-ProcessPath -Directory (Split-Path -Parent $cmake) }
    if ($null -ne $ninja) { Add-ProcessPath -Directory (Split-Path -Parent $ninja) }
    $env:VCPKG_FORCE_SYSTEM_BINARIES = "1"

    $gitVersionOutput = @(& $git --version)
    $gitExitCode = $LASTEXITCODE
    $gitVersion = $gitVersionOutput | Select-Object -First 1
    if ($gitExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($gitVersion)) {
        throw "The selected Git executable could not run: $git"
    }
    $vcpkgGitOutput = @(& $vcpkg fetch git --x-stderr-status)
    $vcpkgGitExitCode = $LASTEXITCODE
    $vcpkgGit = $vcpkgGitOutput | Select-Object -Last 1
    if ($vcpkgGitExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($vcpkgGit)) {
        throw "vcpkg could not resolve Git from the process environment. Git download is disabled."
    }
    $resolvedVcpkgGit = [IO.Path]::GetFullPath($vcpkgGit.Trim())
    if (-not [string]::Equals($resolvedVcpkgGit, [IO.Path]::GetFullPath($git),
        [StringComparison]::OrdinalIgnoreCase)) {
        throw "vcpkg resolved an unexpected Git executable: $resolvedVcpkgGit"
    }

    Write-Host "Using existing Git: $resolvedVcpkgGit ($gitVersion)"
    if ($null -eq $originalForceSystemBinaries) {
        Remove-Item Env:VCPKG_FORCE_SYSTEM_BINARIES -ErrorAction SilentlyContinue
    } else {
        $env:VCPKG_FORCE_SYSTEM_BINARIES = $originalForceSystemBinaries
    }
    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
    & $vcpkg install "--triplet=$Triplet" "--x-manifest-root=$ProjectRoot" "--x-install-root=$InstallRoot"
    if ($LASTEXITCODE -ne 0) { throw "vcpkg could not prepare libass (exit code $LASTEXITCODE)." }
}
finally {
    $env:PATH = $originalPath
    if ($null -eq $originalForceSystemBinaries) {
        Remove-Item Env:VCPKG_FORCE_SYSTEM_BINARIES -ErrorAction SilentlyContinue
    } else {
        $env:VCPKG_FORCE_SYSTEM_BINARIES = $originalForceSystemBinaries
    }
}

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

$ReadyDirectory = Join-Path $TripletRoot "share\3f-project"
$ReadyMarker = Join-Path $ReadyDirectory "libass-ready.txt"
New-Item -ItemType Directory -Force -Path $ReadyDirectory | Out-Null
@(
    "triplet=$Triplet",
    "git=$resolvedVcpkgGit",
    "git-version=$gitVersion"
) | Set-Content -LiteralPath $ReadyMarker -Encoding ASCII

Write-Host "libass Debug and Release files are ready under $TripletRoot"
