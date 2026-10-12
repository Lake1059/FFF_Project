param([ValidateSet("x64-windows")][string]$Triplet = "x64-windows")

$ErrorActionPreference = "Stop"
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
# The shared manifest installs libass and disc libraries with one pinned baseline.
& (Join-Path $PSScriptRoot "准备Libass.ps1") -Triplet $Triplet
$TripletRoot = Join-Path $ProjectRoot "third_party\vcpkg_installed\$Triplet"
foreach ($relative in @("include\libbluray\bluray.h", "include\udfread\udfread.h", "lib\bluray.lib", "debug\lib\bluray.lib")) {
    if (-not (Test-Path -LiteralPath (Join-Path $TripletRoot $relative) -PathType Leaf)) {
        throw "Disc dependency is missing: $relative"
    }
}
Write-Host "Disc navigation Debug and Release libraries are ready under $TripletRoot"
