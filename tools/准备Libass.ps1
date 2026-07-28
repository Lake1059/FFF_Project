param(
    [Parameter(Mandatory = $false)]
    [string]$VisualStudioDirectory = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64-windows")]
    [string]$Triplet = "x64-windows"
)

$parameters = @{ Triplet = $Triplet }
if (-not [string]::IsNullOrWhiteSpace($VisualStudioDirectory)) {
    $parameters.VisualStudioDirectory = $VisualStudioDirectory
}
& (Join-Path $PSScriptRoot "Prepare-Libass.ps1") @parameters
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
