param([Parameter(Mandatory = $true)][string]$PublishDirectory)

$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $ProjectRoot 'tools\Publish-Common.ps1')

$directory = Get-PublishOutputDirectory -ProjectRoot $ProjectRoot -OutputDirectory $PublishDirectory
if (-not (Test-Path -LiteralPath $directory -PathType Container)) { throw "发布目录不存在：$directory" }
Assert-NoExternalPublishModules -Directory $directory
Write-Host '外部模块发布扫描通过。'
