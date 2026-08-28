param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [ValidateSet("Standard", "RTX")]
    [string]$Variant = "Standard",
    [Parameter(Mandatory = $true)]
    [string]$ExecutableName,
    [string]$RtxVideoSdkRoot = "",
    [UInt64]$RtxVideoApplicationId = 0
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

$ResolvedRtxVideoSdkRoot = ""
$RtxVideoFeatureDirectory = ""
if ($Variant -eq "RTX") {
    if ($RtxVideoApplicationId -eq 0) {
        throw "An NVIDIA-issued RtxVideoApplicationId is required for an RTX build."
    }

    $libraryName = if ($Configuration -eq "Debug") {
        "nvsdk_ngx_d_dbg.lib"
    } else {
        "nvsdk_ngx_d.lib"
    }
    $localSdkRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory "RTX"))
    $localSdkLibrary = Join-Path $localSdkRoot "lib\Windows\x64\$libraryName"
    $localSdkReady =
        (Test-Path -LiteralPath (Join-Path $localSdkRoot "include\nvsdk_ngx.h") -PathType Leaf) -and
        (Test-Path -LiteralPath $localSdkLibrary -PathType Leaf)
    $environmentSdkRoot = [Environment]::GetEnvironmentVariable("NV_RTX_VIDEO_SDK")
    $candidateSdkRoot = if (-not [string]::IsNullOrWhiteSpace($RtxVideoSdkRoot)) {
        $RtxVideoSdkRoot
    } elseif ($localSdkReady) {
        $localSdkRoot
    } else {
        $environmentSdkRoot
    }
    if ([string]::IsNullOrWhiteSpace($candidateSdkRoot)) {
        throw "RTX Video SDK was not found. Pass -RtxVideoSdkRoot or set NV_RTX_VIDEO_SDK."
    }
    $ResolvedRtxVideoSdkRoot = [IO.Path]::GetFullPath(
        [Environment]::ExpandEnvironmentVariables($candidateSdkRoot.Trim().Trim('"')))
    $sdkLibrary = Join-Path $ResolvedRtxVideoSdkRoot "lib\Windows\x64\$libraryName"
    if (-not (Test-Path -LiteralPath (Join-Path $ResolvedRtxVideoSdkRoot "include\nvsdk_ngx.h") -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sdkLibrary -PathType Leaf)) {
        throw "RTX Video SDK headers or $libraryName are missing under $ResolvedRtxVideoSdkRoot."
    }

    $RtxVideoFeatureDirectory = $localSdkRoot
    if (-not [string]::Equals(
            $ResolvedRtxVideoSdkRoot.TrimEnd('\'),
            $localSdkRoot.TrimEnd('\'),
            [StringComparison]::OrdinalIgnoreCase)) {
        $runtimeFlavor = if ($Configuration -eq "Debug") { "dev" } else { "rel" }
        $RtxVideoFeatureDirectory = Join-Path $ResolvedRtxVideoSdkRoot "bin\Windows\x64\$runtimeFlavor"
    }
    $featureDlls = @(
        (Join-Path $RtxVideoFeatureDirectory "nvngx_vsr.dll"),
        (Join-Path $RtxVideoFeatureDirectory "nvngx_truehdr.dll")
    )
    $missingFeatureDlls = @($featureDlls | Where-Object {
        -not (Test-Path -LiteralPath $_ -PathType Leaf)
    })
    if ($missingFeatureDlls.Count -ne 0) {
        throw "RTX feature DLLs are missing: $($missingFeatureDlls -join ', ')"
    }
}

function Invoke-NativeBuild {
    $arguments = @(
        (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj"),
        "/p:Configuration=$Configuration",
        "/p:Platform=x64",
        "/p:RtxVideoEnabled=$(if ($Variant -eq 'RTX') { '1' } else { '0' })"
    )
    if ($Variant -eq "RTX") {
        $arguments += "/p:RtxVideoSdkRoot=$ResolvedRtxVideoSdkRoot"
        $arguments += "/p:RtxVideoApplicationId=$RtxVideoApplicationId"
    }
    $arguments += @("/m", "/v:minimal")
    & $MSBuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "FFF.Native x64 $Configuration $Variant build failed."
    }
}

function Test-SingleFileContainsText {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string]$Text
    )
    $stream = [IO.File]::OpenRead($Path)
    try {
        $buffer = New-Object byte[] 65536
        $tail = ""
        $tailLength = [Math]::Max(0, $Text.Length - 1)
        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $chunk = [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            $combined = $tail + $chunk
            if ($combined.IndexOf($Text, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                return $true
            }
            $tail = if ($combined.Length -le $tailLength) {
                $combined
            } else {
                $combined.Substring($combined.Length - $tailLength)
            }
        }
        return $false
    } finally {
        $stream.Dispose()
    }
}

function Publish-PlayerSingleFile {
    $stagingDirectory = Join-Path ([IO.Path]::GetTempPath()) (
        "3FP-publish-" + $Variant.ToLowerInvariant() + "-" + [Guid]::NewGuid().ToString("N"))
    $pendingExecutable = $null
    try {
        New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
        $publishArguments = @(
            "publish",
            (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj"),
            "-c", $Configuration,
            "-r", "win-x64",
            "--self-contained", "false",
            "-o", $stagingDirectory,
            "-p:PublishSingleFile=true",
            "-p:IncludeNativeLibrariesForSelfExtract=true",
            "-p:DebugType=None",
            "-p:DebugSymbols=false",
            "-p:RtxVideoEnabled=$(if ($Variant -eq 'RTX') { '1' } else { '0' })"
        )
        if ($Variant -eq "RTX") {
            $publishArguments += @(
                "-p:RtxVideoSdkRoot=$ResolvedRtxVideoSdkRoot",
                "-p:RtxVideoFeatureDirectory=$RtxVideoFeatureDirectory",
                "-p:RtxVideoApplicationId=$RtxVideoApplicationId"
            )
        }
        & $DotNet @publishArguments
        if ($LASTEXITCODE -ne 0) {
            throw "FFF.Player $Variant single-file publish failed."
        }

        $unexpectedFiles = @(Get-ChildItem -LiteralPath $stagingDirectory -File |
            Where-Object { $_.Name -ne "FFF.Player.exe" })
        if ($unexpectedFiles.Count -ne 0) {
            throw "Unexpected files in single-file output: $($unexpectedFiles.Name -join ', ')"
        }
        $publishedExecutable = Join-Path $stagingDirectory "FFF.Player.exe"
        if (-not (Test-Path -LiteralPath $publishedExecutable -PathType Leaf)) {
            throw "FFF.Player.exe is missing from the publish output."
        }
        if ($Variant -eq "RTX") {
            foreach ($payload in @("FFF.Native.dll", "nvngx_vsr.dll", "nvngx_truehdr.dll")) {
                if (-not (Test-SingleFileContainsText -Path $publishedExecutable -Text $payload)) {
                    throw "The RTX single-file bundle does not contain required payload: $payload"
                }
            }
        }

        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
        $targetExecutable = Join-Path $OutputDirectory $ExecutableName
        $pendingExecutable = Join-Path $OutputDirectory (
            $ExecutableName + ".new-" + [Guid]::NewGuid().ToString("N"))
        Copy-Item -LiteralPath $publishedExecutable -Destination $pendingExecutable
        if (Test-Path -LiteralPath $targetExecutable -PathType Leaf) {
            Remove-Item -LiteralPath $targetExecutable -Force
        }
        Move-Item -LiteralPath $pendingExecutable -Destination $targetExecutable
        $pendingExecutable = $null
        $publishedFile = Get-Item -LiteralPath $targetExecutable
        Write-Host "$Variant single-file publish completed: $($publishedFile.FullName) ($($publishedFile.Length) bytes)"
    } finally {
        if ($pendingExecutable -and (Test-Path -LiteralPath $pendingExecutable)) {
            Remove-Item -LiteralPath $pendingExecutable -Force
        }
        if (Test-Path -LiteralPath $stagingDirectory) {
            Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
        }
    }
}

Invoke-NativeBuild
Publish-PlayerSingleFile
