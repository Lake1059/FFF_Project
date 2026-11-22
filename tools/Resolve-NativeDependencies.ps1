# Shared preflight for build and publish entry points. Inspect actual files rather
# than a marker left by an earlier (possibly incomplete) preparation run.
$ErrorActionPreference = 'Stop'

function Get-FFmpegComponentNames {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)
    [xml]$manifest = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'build-config\FFmpegRuntime.props'))
    $manifest.Project.ItemGroup.FFmpegComponent.Include.Split(';')
}

function Invoke-ProductNativeBuild {
    param([string]$MSBuild, [string]$ProjectRoot, [string]$Configuration, [switch]$SkipIamf)
    $iamfProperty = if ($SkipIamf) { '/p:HaveLibiamf=false' } else { '/p:FFF_FORCE_LIBIAMF=true' }
    & $MSBuild (Join-Path $ProjectRoot 'FFF.Native\FFF.Native.vcxproj') `
        /p:Configuration=$Configuration /p:Platform=x64 $iamfProperty /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }
}

function Get-LibiamfRequiredFiles {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot,
        [string]$Configuration = 'Release', [switch]$IncludeDebugSymbols)

    @('include\IAMF_decoder.h', 'include\IAMF_defines.h', 'include\vlogging_tool_sr.h',
      'oar\include\oar.h', 'oar\include\oar_base.h', 'oar\include\oar_metadata.h',
      'oar\include\animation.h')
    $libPrefix = if ($Configuration -eq 'Debug') { 'lib\x64\debug\' } else { 'lib\x64\' }
    [xml]$project = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'FFF.Native\FFF.Native.vcxproj'))
    foreach ($group in $project.Project.ItemDefinitionGroup) {
        if ($group.Condition -notlike '*HaveLibiamf*') { continue }
        foreach ($library in ([string]$group.Link.AdditionalDependencies).Split(';')) {
            if ($library.EndsWith('.lib')) {
                $relative = $libPrefix + $library
                $relative
                if ($Configuration -eq 'Debug' -and $IncludeDebugSymbols) {
                    [IO.Path]::ChangeExtension($relative, '.pdb')
                }
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
    param([Parameter(Mandatory = $true)][string]$ProjectRoot,
        [string]$Configuration = 'Release', [switch]$IncludeDebugSymbols)
    $missing = @(Get-MissingDependencyFiles -Root (Join-Path $ProjectRoot 'third_party\libiamf') `
        -RelativePaths @(Get-LibiamfRequiredFiles -ProjectRoot $ProjectRoot -Configuration $Configuration `
            -IncludeDebugSymbols:$IncludeDebugSymbols))
    if ($missing.Count) { throw "Incomplete IAMF dependencies. Run tools/准备Libiamf.ps1. Missing: $($missing -join ', ')" }
}

function Initialize-NativeDependencies {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
        [switch]$SkipPreparation,
        [switch]$SkipIamf,
        [switch]$SkipFfmpegRuntime
    )

    $toolsDirectory = Join-Path $ProjectRoot 'tools'
    $ffmpegRoot = Join-Path $ProjectRoot 'third_party\ffmpeg'
    $components = @(Get-FFmpegComponentNames -ProjectRoot $ProjectRoot)
    $ffmpegFiles = @(foreach ($component in $components) {
        $prefix = $component.Split('-')[0]
        "include\lib$prefix\$prefix.h"
        "lib\x64\$component.lib"
        if (-not $SkipFfmpegRuntime) { "bin\x64\$component.dll" }
    })
    $missing = @(Get-MissingDependencyFiles -Root $ffmpegRoot -RelativePaths $ffmpegFiles)
    if ($missing.Count -and -not $SkipPreparation) {
        & (Join-Path $toolsDirectory '准备FFmpeg.ps1')
        $missing = @(Get-MissingDependencyFiles -Root $ffmpegRoot -RelativePaths $ffmpegFiles)
    }
    if ($missing.Count) { throw "Incomplete FFmpeg dependencies. Run tools/准备FFmpeg.ps1. Missing: $($missing -join ', ')" }

    $librawRoot = Join-Path $ProjectRoot 'third_party\libraw'
    $librawFiles = @('include\libraw\libraw.h', 'include\libraw\libraw_alloc.h',
        'include\libraw\libraw_const.h', 'include\libraw\libraw_datastream.h',
        'include\libraw\libraw_internal.h', 'include\libraw\libraw_types.h',
        'include\libraw\libraw_version.h', 'lib\x64\libraw.lib', 'bin\x64\libraw.dll')
    $missing = @(Get-MissingDependencyFiles -Root $librawRoot -RelativePaths $librawFiles)
    if ($missing.Count -and -not $SkipPreparation) {
        & (Join-Path $toolsDirectory '准备LibRaw.ps1')
        $missing = @(Get-MissingDependencyFiles -Root $librawRoot -RelativePaths $librawFiles)
    }
    if ($missing.Count) { throw "Incomplete LibRaw dependencies. Run tools/准备LibRaw.ps1. Missing: $($missing -join ', ')" }

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
        & (Join-Path $toolsDirectory '准备Libass.ps1')
        $missing = @(Get-MissingDependencyFiles -Root $vcpkgRoot -RelativePaths $vcpkgFiles)
    }
    if ($missing.Count) { throw "Incomplete libass/disc dependencies. Run tools/准备Libass.ps1. Missing: $($missing -join ', ')" }

    if (-not $SkipIamf) {
        $missing = @(Get-MissingDependencyFiles -Root (Join-Path $ProjectRoot 'third_party\libiamf') `
            -RelativePaths @(Get-LibiamfRequiredFiles -ProjectRoot $ProjectRoot -Configuration $Configuration `
                -IncludeDebugSymbols:(-not $SkipPreparation)))
        if ($missing.Count -and -not $SkipPreparation) { & (Join-Path $toolsDirectory '准备Libiamf.ps1') -Configuration $Configuration }
        Assert-LibiamfReady -ProjectRoot $ProjectRoot -Configuration $Configuration `
            -IncludeDebugSymbols:(-not $SkipPreparation)
    }
}
