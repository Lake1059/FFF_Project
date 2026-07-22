param(
    [Parameter(Mandatory = $true)]
    [string]$ImagePath,
    [string]$OutputDirectory = "",
    [string]$FfmpegDirectory = "",
    [ValidateRange(10, 300)]
    [int]$FrameCount = 24,
    [string]$Encoder = "",
    [ValidateSet("", "sdr8", "sdr10", "hdr10")]
    [string]$Mode = "",
    [ValidateSet("", "420", "422", "444")]
    [string]$Sampling = "",
    [string]$Preset = "",
    [ValidateRange(0, 64)]
    [int]$LookaheadFrames = 0
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "color-test-output"
}
if ([string]::IsNullOrWhiteSpace($FfmpegDirectory)) {
    $FfmpegDirectory = Join-Path $env:USERPROFILE "Desktop\FFmpegShared"
}

$ImagePath = [IO.Path]::GetFullPath($ImagePath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$Ffmpeg = Join-Path ([IO.Path]::GetFullPath($FfmpegDirectory)) "ffmpeg.exe"
$Ffprobe = Join-Path ([IO.Path]::GetFullPath($FfmpegDirectory)) "ffprobe.exe"
if (-not (Test-Path -LiteralPath $ImagePath -PathType Leaf)) { throw "Test image not found: $ImagePath" }
if (-not (Test-Path -LiteralPath $Ffmpeg -PathType Leaf)) { throw "ffmpeg.exe not found: $Ffmpeg" }
if (-not (Test-Path -LiteralPath $Ffprobe -PathType Leaf)) { throw "ffprobe.exe not found: $Ffprobe" }

$NativeDirectory = Join-Path $ProjectRoot "FFF.Native\x64\Release"
$env:PATH = "$ProjectRoot\runtime;$NativeDirectory;$env:PATH"
Add-Type -Path (Join-Path $PSScriptRoot "ColorPipelineProbe.cs") -ReferencedAssemblies System.Drawing

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Results = [ColorPipelineProbe]::Run($ImagePath, $OutputDirectory, $FrameCount,
    $Encoder, $Mode, $Sampling, $Preset, [uint32]$LookaheadFrames)
$LastFrame = $FrameCount - 1
foreach ($Result in $Results | Where-Object Success) {
    $Decoded = [IO.Path]::ChangeExtension($Result.Output, ".png")
    & $Ffmpeg -hide_banner -loglevel error -y -i $Result.Output `
        -vf "select=eq(n\,$LastFrame)" -fps_mode passthrough -frames:v 1 -pix_fmt rgb24 $Decoded
    if ($LASTEXITCODE -ne 0) {
        $Result.Success = $false
        $Result.Error = "ffmpeg decode failed"
    }
}

$Metadata = foreach ($Result in $Results | Where-Object Success) {
    $ProbeJson = & $Ffprobe -v error -count_frames -select_streams v:0 `
        -show_entries stream=pix_fmt,color_range,color_space,color_transfer,color_primaries,chroma_location,nb_read_frames `
        -of json $Result.Output
    $Stream = ($ProbeJson | Out-String | ConvertFrom-Json).streams[0]
    $ExpectedColorRange = "pc"
    if ($Stream.color_range -ne $ExpectedColorRange) {
        $Result.Success = $false
        $Result.Error = "encoded color range is $($Stream.color_range), expected $ExpectedColorRange"
    }
    $BitstreamChromaLocation = ""
    $IsHevc = $Result.Encoder -eq "libx265" -or $Result.Encoder.StartsWith("hevc_")
    if ($IsHevc) {
        $ErrorActionPreference = "Continue"
        $HeaderTrace = & $Ffmpeg -hide_banner -loglevel trace -i $Result.Output `
            -map 0:v:0 -c copy -bsf:v trace_headers -frames:v 1 -f null NUL 2>&1 | Out-String
        $ErrorActionPreference = "Stop"
        if ($HeaderTrace -match 'chroma_loc_info_present_flag\s+1\s+=\s+1' -and
            $HeaderTrace -match 'chroma_sample_loc_type_top_field\s+1\s+=\s+0' -and
            $HeaderTrace -match 'chroma_sample_loc_type_bottom_field\s+1\s+=\s+0') {
            $BitstreamChromaLocation = "left"
        } elseif ($HeaderTrace -match 'chroma_loc_info_present_flag\s+0\s+=\s+0') {
            $BitstreamChromaLocation = "unspecified"
        } else {
            $BitstreamChromaLocation = "unknown"
        }
        if ($Result.Sampling -eq "420" -and $BitstreamChromaLocation -ne "left") {
            $Result.Success = $false
            $Result.Error = "HEVC SPS does not explicitly signal left-sited 4:2:0 chroma"
        }
    }
    [PSCustomObject]@{
        Encoder = $Result.Encoder; Mode = $Result.Mode; Sampling = $Result.Sampling; Range = $Result.Range
        ExpectedColorRange = $ExpectedColorRange
        PixelFormat = $Stream.pix_fmt; ColorRange = $Stream.color_range
        ColorSpace = $Stream.color_space; ColorTransfer = $Stream.color_transfer
        ColorPrimaries = $Stream.color_primaries; ChromaLocation = $Stream.chroma_location
        BitstreamChromaLocation = $BitstreamChromaLocation
        FrameCount = $Stream.nb_read_frames
    }
}

$Results | Select-Object Encoder,Mode,Sampling,Range,FrameCount,Success,Error,
    SetupMilliseconds,SubmitMilliseconds,StopMilliseconds,DestroyMilliseconds,Output |
    Export-Csv -LiteralPath (Join-Path $OutputDirectory "results.csv") -NoTypeInformation -Encoding UTF8
$Metadata | Export-Csv -LiteralPath (Join-Path $OutputDirectory "metadata.csv") -NoTypeInformation -Encoding UTF8
$PixelResults = [ColorPipelineProbe]::AnalyzePixels($ImagePath, $Results)
$PixelResults | Select-Object Encoder,Mode,Sampling,Range,Status,BiasR,BiasG,BiasB,
    MaeR,MaeG,MaeB,P95R,P95G,P95B,MaxR,MaxG,MaxB,GreenBias,BiasSpread,GreenDominantPercent,
    NeutralPixelCount,NeutralBiasR,NeutralBiasG,NeutralBiasB,NeutralGreenBias,Decoded |
    Export-Csv -LiteralPath (Join-Path $OutputDirectory "pixel-report.csv") -NoTypeInformation -Encoding UTF8
$Results | Group-Object Encoder,Mode | ForEach-Object {
    [PSCustomObject]@{
        EncoderMode = $_.Name
        Passed = ($_.Group | Where-Object Success).Sampling -join ","
        Rejected = ($_.Group | Where-Object { -not $_.Success }).Sampling -join ","
    }
} | Format-Table -AutoSize

$PixelResults | Select-Object Encoder,Mode,Sampling,Status,
    @{Name="GreenBias";Expression={"{0:F3}" -f $_.GreenBias}},
    @{Name="NeutralGreen";Expression={"{0:F3}" -f $_.NeutralGreenBias}},
    @{Name="BiasSpread";Expression={"{0:F3}" -f $_.BiasSpread}},
    @{Name="GreenPixels%";Expression={"{0:F2}" -f $_.GreenDominantPercent}},
    @{Name="MAE(R/G/B)";Expression={"{0:F2}/{1:F2}/{2:F2}" -f $_.MaeR,$_.MaeG,$_.MaeB}} |
    Format-Table -AutoSize

$Results | Select-Object Encoder,Mode,Sampling,Success,
    @{Name="SetupMs";Expression={"{0:F1}" -f $_.SetupMilliseconds}},
    @{Name="SubmitMs";Expression={"{0:F1}" -f $_.SubmitMilliseconds}},
    @{Name="StopMs";Expression={"{0:F1}" -f $_.StopMilliseconds}},
    @{Name="DestroyMs";Expression={"{0:F1}" -f $_.DestroyMilliseconds}} |
    Format-Table -AutoSize

Write-Host "Color test outputs: $OutputDirectory"
