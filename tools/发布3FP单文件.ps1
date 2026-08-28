param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$Publisher = Join-Path $PSScriptRoot "3FP-Publish-Internal.ps1"
& $Publisher `
    -Configuration $Configuration `
    -OutputDirectory $OutputDirectory `
    -Variant "Standard" `
    -ExecutableName "FFF.Player.exe"
