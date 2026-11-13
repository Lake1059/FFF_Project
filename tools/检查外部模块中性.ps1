param(
    [string]$PublishDirectory = ""
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ($PSScriptRoot) { $PSScriptRoot } else {
    Split-Path -Parent $MyInvocation.MyCommand.Path
}
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))

# 发布 staging 不允许出现任何外部模块 DLL；只查文件名，不猜内容。
if (-not [string]::IsNullOrWhiteSpace($PublishDirectory)) {
    if (-not [IO.Path]::IsPathRooted($PublishDirectory)) {
        $PublishDirectory = Join-Path $ProjectRoot $PublishDirectory
    }
    $PublishDirectory = [IO.Path]::GetFullPath($PublishDirectory)
    if (-not (Test-Path -LiteralPath $PublishDirectory -PathType Container)) {
        throw "发布目录不存在：$PublishDirectory"
    }
    $Forbidden = @(Get-ChildItem -LiteralPath $PublishDirectory -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*ExternalModule*.dll" -or $_.Name -like "*.Test.dll" })
    if ($Forbidden.Count -gt 0) {
        Write-Host "发布 staging 含外部模块 DLL：$($Forbidden.Count) 个" -ForegroundColor Red
        $Forbidden | ForEach-Object { Write-Host "  - $($_.FullName)" }
        exit 1
    }
}

Write-Host "外部模块发布扫描通过。" -ForegroundColor Green
exit 0
