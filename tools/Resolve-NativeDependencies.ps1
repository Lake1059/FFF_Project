# Shared preflight for build and publish entry points. Inspect actual files rather
# than a marker left by an earlier (possibly incomplete) preparation run.
$ErrorActionPreference = 'Stop'

function Get-LibiamfRequiredFiles {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot, [string]$Configuration = 'Release')

    @('include\IAMF_decoder.h', 'include\IAMF_defines.h', 'include\vlogging_tool_sr.h',
      'oar\include\oar.h', 'oar\include\oar_base.h', 'oar\include\oar_metadata.h',
      'oar\include\animation.h')
    [xml]$project = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'FFF.Native\FFF.Native.vcxproj'))
    foreach ($group in $project.Project.ItemDefinitionGroup) {
        if ($group.Condition -notlike '*HaveLibiamf*') { continue }
        foreach ($library in ([string]$group.Link.AdditionalDependencies).Split(';')) {
            if ($library.EndsWith('.lib')) {
                $prefix = if ($Configuration -eq 'Debug') { 'lib\x64\debug\' } else { 'lib\x64\' }
                $prefix + $library
            }
        }
    }
}

function Get-MissingDependencyFiles {
    param([string]$Root, [string[]]$RelativePaths)
    foreach ($relative in $RelativePaths) {
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $path }
    }
}

function Assert-LibiamfReady {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot, [string]$Configuration = 'Release')
    $missing = @(Get-MissingDependencyFiles -Root (Join-Path $ProjectRoot 'third_party\libiamf') `
        -RelativePaths @(Get-LibiamfRequiredFiles -ProjectRoot $ProjectRoot -Configuration $Configuration))
    if ($missing.Count) { throw "Incomplete IAMF dependencies. Run tools/准备Libiamf.ps1. Missing: $($missing -join ', ')" }
}

function Initialize-NativeDependencies {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
        [switch]$SkipPreparation,
        [switch]$SkipIamf
    )

    $toolsDirectory = Join-Path $ProjectRoot 'tools'
    $ffmpegFiles = @()
    foreach ($component in @('avcodec-63', 'avformat-63', 'avutil-61', 'avfilter-12', 'swresample-7', 'swscale-10')) {
        $prefix = $component.Split('-')[0]
        $ffmpegFiles += "third_party\ffmpeg\include\lib$prefix\$prefix.h"
        $ffmpegFiles += "third_party\ffmpeg\lib\x64\$component.lib"
        $ffmpegFiles += "runtime\$component.dll"
    }
    $missing = @(Get-MissingDependencyFiles -Root $ProjectRoot -RelativePaths $ffmpegFiles)
    if ($missing.Count -and -not $SkipPreparation) {
        # Reuse an existing runtime so repairing headers/libs does not replace the
        # user's FFmpeg variant with a fresh download.
        $runtime = Join-Path $ProjectRoot 'runtime'
        $runtimeArgs = @{}
        if (Test-Path -LiteralPath (Join-Path $runtime 'avcodec-63.dll')) { $runtimeArgs.RuntimeSource = $runtime }
        & (Join-Path $toolsDirectory '准备FFmpeg.ps1') @runtimeArgs
        $missing = @(Get-MissingDependencyFiles -Root $ProjectRoot -RelativePaths $ffmpegFiles)
    }
    if ($missing.Count) { throw "Incomplete FFmpeg dependencies. Run tools/准备FFmpeg.ps1. Missing: $($missing -join ', ')" }

    $vcpkgRoot = Join-Path $ProjectRoot 'third_party\vcpkg_installed\x64-windows'
    $libDirectory = if ($Configuration -eq 'Debug') { 'debug\lib' } else { 'lib' }
    $binDirectory = if ($Configuration -eq 'Debug') { 'debug\bin' } else { 'bin' }
    $vcpkgFiles = @('include\ass\ass.h', 'include\libbluray\bluray.h', 'include\udfread\udfread.h',
        "$libDirectory\ass.lib", "$libDirectory\bluray.lib")
    $runtimeNames = @('ass-9.dll', 'bluray-3.dll', 'udfread-3.dll', 'libxml2.dll', 'iconv-2.dll', 'charset-1.dll',
        'brotlicommon.dll', 'brotlidec.dll', 'fribidi-0.dll', 'harfbuzz.dll')
    $runtimeNames += if ($Configuration -eq 'Debug') { @('bz2d.dll', 'freetyped.dll', 'libpng16d.dll', 'zd.dll') } `
        else { @('bz2.dll', 'freetype.dll', 'libpng16.dll', 'z.dll') }
    $vcpkgFiles += @($runtimeNames | ForEach-Object { "$binDirectory\$_" })
    $missing = @(Get-MissingDependencyFiles -Root $vcpkgRoot -RelativePaths $vcpkgFiles)
    if ($missing.Count -and -not $SkipPreparation) {
        & (Join-Path $toolsDirectory '准备光盘库.ps1')
        $missing = @(Get-MissingDependencyFiles -Root $vcpkgRoot -RelativePaths $vcpkgFiles)
    }
    if ($missing.Count) { throw "Incomplete libass/disc dependencies. Run tools/准备光盘库.ps1. Missing: $($missing -join ', ')" }

    if (-not $SkipIamf) {
        $missing = @(Get-MissingDependencyFiles -Root (Join-Path $ProjectRoot 'third_party\libiamf') `
            -RelativePaths @(Get-LibiamfRequiredFiles -ProjectRoot $ProjectRoot -Configuration $Configuration))
        if ($missing.Count -and -not $SkipPreparation) { & (Join-Path $toolsDirectory '准备Libiamf.ps1') -Configuration $Configuration }
        Assert-LibiamfReady -ProjectRoot $ProjectRoot -Configuration $Configuration
    }
}
