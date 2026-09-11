param([Parameter(Mandatory = $true)][string]$BlurayPath)

$ErrorActionPreference = "Stop"
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$Probe = Join-Path $ProjectRoot "FFF.Player.Tests\Native\x64\Release\DiscNavigationProbe.exe"
if (-not (Test-Path -LiteralPath $Probe -PathType Leaf)) { throw "Build the Blu-ray navigation probe first: $Probe" }
$ArtifactDirectory = Join-Path $ProjectRoot "artifacts\disc-probe"
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
& $Probe bluray $BlurayPath 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactDirectory "bluray.log")
if ($LASTEXITCODE -ne 0) { throw "Blu-ray navigation verification failed. See artifacts/disc-probe/bluray.log." }
