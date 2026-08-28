param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [string]$RtxVideoSdkRoot = "",
    [UInt64]$RtxVideoApplicationId = 0
)

$ErrorActionPreference = "Stop"
if ($RtxVideoApplicationId -eq 0) {
    throw "An NVIDIA-issued RtxVideoApplicationId is required for the RTX publish script."
}

$Publisher = Join-Path $PSScriptRoot "3FP-Publish-Internal.ps1"
& $Publisher `
    -Configuration $Configuration `
    -OutputDirectory $OutputDirectory `
    -Variant "RTX" `
    -ExecutableName "FFF.Player.RTX.exe" `
    -RtxVideoSdkRoot $RtxVideoSdkRoot `
    -RtxVideoApplicationId $RtxVideoApplicationId
