param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    # 目标框架。留空 = 用各工程自身默认值（当前 net10.0-windows10.0.26100.0）。
    # .NET 11 正式版发布后改为：发布3FR单文件.ps1 -NetTFM net11.0-windows10.0.26100.0
    [string]$NetTFM = "",
    # 外部商业兄弟仓 LakeUI 的目标框架，可与宿主不同（混血状态会出警告但可工作）。
    [string]$LakeUITfm = "",
    # 跨产品框架配对：默认拒绝与输出目录里已发布的对家产品框架不一致，见 Resolve-Toolchain.ps1。
    [switch]$AllowFrameworkMismatch
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $MyInvocation.MyCommand.Path
} else {
    $PSScriptRoot
}
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")
$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool
$LibassReadyMarker = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share\3f-project\libass-ready.txt"
if (-not (Test-Path -LiteralPath $LibassReadyMarker -PathType Leaf)) {
    throw "libass is not prepared. Run the *Libass.ps1 preparation script in tools first."
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FR-publish-" + [Guid]::NewGuid().ToString("N"))
$PendingExecutable = $null
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null

    # TFM 参数包：留空时展开为 0 个参数，命令行与改动前逐字符一致。
    $TfmArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $TfmArgs += "-p:NetTFM=$NetTFM" }
    if (-not [string]::IsNullOrWhiteSpace($LakeUITfm)) { $TfmArgs += "-p:LakeUITfm=$LakeUITfm" }
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Recorder\FFF.Recorder.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:PublishSingleFile=true `
        -p:EnableMsixTooling=true `
        -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:DebugType=None -p:DebugSymbols=false @TfmArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Recorder single-file publish failed." }

    $OutputFiles = @(Get-ChildItem -LiteralPath $StagingDirectory -File)
    $UnexpectedFiles = $OutputFiles | Where-Object { $_.Name -ne "FFF.Recorder.exe" }
    if ($UnexpectedFiles) {
        throw "Unexpected files in single-file output: $($UnexpectedFiles.Name -join ', ')"
    }

    $PublishedExecutable = Join-Path $StagingDirectory "FFF.Recorder.exe"
    if (-not (Test-Path -LiteralPath $PublishedExecutable -PathType Leaf)) {
        throw "FFF.Recorder.exe is missing from the publish output."
    }

    Assert-ProductFrameworkPair -OutputDirectory $OutputDirectory -Product "FFF.Recorder" `
        -Framework (Get-ProductTargetFramework -ProjectPath (Join-Path $ProjectRoot "FFF.Recorder\FFF.Recorder.vbproj") `
            -NetTFM $NetTFM -DotNet $DotNet) `
        -AllowMismatch:$AllowFrameworkMismatch
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $Executable = Join-Path $OutputDirectory "FFF.Recorder.exe"
    $PendingExecutable = Join-Path $OutputDirectory ("FFF.Recorder.exe.new-" + [Guid]::NewGuid().ToString("N"))
    Copy-Item -LiteralPath $PublishedExecutable -Destination $PendingExecutable
    if (Test-Path -LiteralPath $Executable -PathType Leaf) {
        Remove-Item -LiteralPath $Executable -Force
    }
    Move-Item -LiteralPath $PendingExecutable -Destination $Executable
    $PendingExecutable = $null
    $PublishedFile = Get-Item -LiteralPath $Executable
    Write-Host "Single-file publish completed: $($PublishedFile.FullName) ($($PublishedFile.Length) bytes)"
}
finally {
    if ($PendingExecutable -and (Test-Path -LiteralPath $PendingExecutable)) {
        Remove-Item -LiteralPath $PendingExecutable -Force
    }
    if (Test-Path -LiteralPath $StagingDirectory) {
        Remove-Item -LiteralPath $StagingDirectory -Recurse -Force
    }
}
