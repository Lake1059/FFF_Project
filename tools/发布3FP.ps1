param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [string]$NetTFM = "",
    [switch]$AllowFrameworkMismatch,
    [switch]$SkipIamf,
    [switch]$SkipDependencyPreparation,
    [switch]$IncludeExternalRuntime
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
. (Join-Path $PSScriptRoot "Resolve-NativeDependencies.ps1")
. (Join-Path $PSScriptRoot "Publish-Common.ps1")

$OutputDirectory = Get-PublishOutputDirectory -ProjectRoot $ProjectRoot -OutputDirectory $OutputDirectory -DefaultDirectory "publish\win-x64-散装"
Assert-NoExternalPublishModules -Directory $OutputDirectory
$Project = Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj"
$DotNet = Get-DotNetTool
$MSBuild = Get-MSBuildTool
Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf -SkipFfmpegRuntime:(-not $IncludeExternalRuntime)
Invoke-ProductNativeBuild -MSBuild $MSBuild -ProjectRoot $ProjectRoot -Configuration $Configuration -SkipIamf:$SkipIamf
Assert-PlayerRawKernel -ProjectRoot $ProjectRoot -Configuration $Configuration
& $MSBuild (Join-Path $ProjectRoot "FFF.PrefetchHost\FFF.PrefetchHost.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "FFF.PrefetchHost x64 $Configuration 构建失败。" }

$PublishArgs = @("-p:BodyOnly=$((-not $IncludeExternalRuntime).ToString().ToLowerInvariant())")
if ($NetTFM) { $PublishArgs += "-p:NetTFM=$NetTFM" }
$StagingDirectory = New-PublishStagingDirectory
try {
    & $DotNet publish $Project -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:DebugType=None -p:DebugSymbols=false @PublishArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player 散装发布失败。" }

    $files = @(Get-ChildItem -LiteralPath $StagingDirectory -File)
    $ffmpegDlls = @(Get-FFmpegComponentNames -ProjectRoot $ProjectRoot | ForEach-Object { "$_.dll" })
    $required = @('FFF.Player.exe', 'FFF.Player.dll', 'FFF.Native.dll', 'libraw.dll', 'FFF.PrefetchHost.exe',
        'ass-9.dll', 'bluray-3.dll', 'udfread-3.dll', 'libxml2.dll', 'iconv-2.dll', 'charset-1.dll')
    if ($IncludeExternalRuntime) { $required += $ffmpegDlls }
    $missing = @($required | Where-Object { $_ -notin $files.Name })
    if ($missing.Count) { throw "发布产物缺少文件：$($missing -join ', ')" }
    Assert-NoExternalPublishModules -Directory $StagingDirectory

    $runtimeFiles = @($files | Where-Object { $_.Name -match '^(av(codec|filter|format|util)|sw(resample|scale))(-\d+)?\.dll$' })
    if ($IncludeExternalRuntime) {
        foreach ($file in $runtimeFiles) {
            $image = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($file.FullName))
            if ($image.Contains('--enable-nonfree')) {
                if ($env:FFF_ALLOW_NONFREE -ne '1') { throw "FFmpeg 变体含 --enable-nonfree：$($file.Name)。" }
                Write-Warning "FFF_ALLOW_NONFREE=1 已放行 $($file.Name)。"
            } elseif (-not ($image.Contains('--enable-shared') -or $image.Contains('--disable-static'))) {
                Write-Warning "未找到 $($file.Name) 的 FFmpeg 构建配置。"
            }
        }
    } elseif ($runtimeFiles.Count) {
        throw "本体包含外部 FFmpeg DLL：$($runtimeFiles.Name -join ', ')"
    }

    $localLicenses = Join-Path $ProjectRoot 'licenses'
    foreach ($name in @('LICENSE.txt', 'NOTICE', 'LICENSING.md', 'THIRD-PARTY-NOTICES.md', 'ASSETS.md')) {
        $source = if ($name -eq 'LICENSE.txt') { Join-Path $ProjectRoot $name } else { Join-Path $localLicenses $name }
        if ((Get-Item -LiteralPath $source).Length -eq 0) { throw "许可文件为空：$source" }
        Copy-Item -LiteralPath $source -Destination $StagingDirectory -Force
    }
    $licenseStage = Join-Path $StagingDirectory 'licenses'
    Copy-Item -LiteralPath $localLicenses -Destination $licenseStage -Recurse -Force
    $missingLicenses = @()
    $extraLicenses = [ordered]@{
        'libraw-LICENSE.LGPL.txt' = 'third_party\libraw\LICENSE.LGPL'
        'libraw-LICENSE.CDDL.txt' = 'third_party\libraw\LICENSE.CDDL'
        'libraw-COPYRIGHT.txt' = 'third_party\libraw\COPYRIGHT'
    }
    if ($IncludeExternalRuntime) {
        $extraLicenses['ffmpeg-COPYING.LGPLv2.1.txt'] = 'third_party\ffmpeg\COPYING.LGPLv2.1'
        $extraLicenses['ffmpeg-COPYING.LGPLv3.txt'] = 'third_party\ffmpeg\COPYING.LGPLv3'
    }
    foreach ($port in @('libass', 'libbluray', 'libudfread', 'libiconv', 'fribidi', 'freetype',
                       'harfbuzz', 'brotli', 'bzip2', 'libpng', 'zlib', 'libxml2')) {
        $extraLicenses["vcpkg-$port.txt"] = "third_party\vcpkg_installed\x64-windows\share\$port\copyright"
    }
    foreach ($entry in $extraLicenses.GetEnumerator()) {
        $source = Join-Path $ProjectRoot $entry.Value
        $destination = Join-Path $licenseStage $entry.Key
        if ((Test-Path -LiteralPath $source -PathType Leaf) -and (Get-Item -LiteralPath $source).Length -gt 0) {
            Copy-Item -LiteralPath $source -Destination $destination -Force
        } elseif (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            $missingLicenses += $entry.Value
        }
    }
    if ($missingLicenses.Count) {
        $message = "缺少许可文本：$($missingLicenses -join ', ')"
        if ($env:FFF_ALLOW_MISSING_LICENSES -ne '1') { throw $message }
        Write-Warning $message
    }
    $emptyLicenses = @(Get-ChildItem -LiteralPath $licenseStage -File -Recurse | Where-Object Length -eq 0)
    if ($emptyLicenses.Count) { throw "许可文件为空：$($emptyLicenses.FullName -join ', ')" }

    $pair = @{ OutputDirectory = $OutputDirectory; Product = 'FFF.Player'; AllowMismatch = $AllowFrameworkMismatch
        Framework = Get-ProductTargetFramework -ProjectPath $Project -NetTFM $NetTFM -DotNet $DotNet }
    Assert-ProductFrameworkPair @pair -CheckOnly
    if (Test-Path -LiteralPath $OutputDirectory) {
        $existing = Get-Item -LiteralPath $OutputDirectory -Force
        $resolved = (Resolve-Path -LiteralPath $OutputDirectory).ProviderPath
        if (-not $existing.PSIsContainer -or $resolved -ne $OutputDirectory -or
            $existing.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint) -or
            ((Get-ChildItem -LiteralPath $resolved -Force | Select-Object -First 1) -and
             -not (Test-Path -LiteralPath (Join-Path $resolved 'FFF.Player.dll') -PathType Leaf))) {
            throw "散装发布只能替换既有 3FP 散装目录或空目录：$OutputDirectory"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $StagingDirectory -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory -Recurse -Force
    }
    Assert-ProductFrameworkPair @pair
    Write-Host "3FP 散装发布完成：$OutputDirectory"
}
finally {
    Remove-PublishStagingDirectory -Path $StagingDirectory
}
