function Get-PublishOutputDirectory {
    param([string]$ProjectRoot, [string]$OutputDirectory, [string]$DefaultDirectory)
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = $DefaultDirectory }
    if (-not [IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory }
    $fullPath = [IO.Path]::GetFullPath($OutputDirectory)
    if ($fullPath -eq [IO.Path]::GetPathRoot($fullPath)) { throw "Publish output cannot be a drive root: $fullPath" }
    $fullPath.TrimEnd('\', '/')
}

function New-PublishStagingDirectory {
    $path = Join-Path ([IO.Path]::GetTempPath()) ('3F-publish-' + [Guid]::NewGuid().ToString('N'))
    (New-Item -ItemType Directory -Path $path).FullName
}

function Remove-PublishStagingDirectory {
    param([string]$Path)
    $directory = Get-Item -LiteralPath $Path -Force
    $tempRoot = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).ProviderPath.TrimEnd('\')
    if ($directory.Parent.FullName -ne $tempRoot -or $directory.Name -notmatch '^3F-publish-[0-9a-f]{32}$' -or
        $directory.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw "Unexpected publish staging path: $Path"
    }
    Remove-Item -LiteralPath $directory.FullName -Recurse -Force
}

function Assert-NoExternalPublishModules {
    param([string]$Directory)
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { return }
    $forbidden = @(Get-ChildItem -LiteralPath $Directory -File -Recurse -Force |
        Where-Object { $_.Name -like '*ExternalModule*.dll' -or $_.Name -like '*.Test.dll' })
    if ($forbidden.Count) { throw "External module DLLs found in ${Directory}: $($forbidden.FullName -join ', ')" }
}

function Publish-SingleExecutable {
    param([string]$StagingDirectory, [string]$OutputDirectory, [string]$Product,
        [string]$Framework, [switch]$AllowFrameworkMismatch)

    $entries = @(Get-ChildItem -LiteralPath $StagingDirectory -Force)
    $fileName = "$Product.exe"
    if ($entries.Count -ne 1 -or $entries[0].PSIsContainer -or $entries[0].Name -ne $fileName) {
        throw "Expected only $fileName in ${StagingDirectory}; found: $($entries.Name -join ', ')"
    }
    $pair = @{ OutputDirectory = $OutputDirectory; Product = $Product; Framework = $Framework; AllowMismatch = $AllowFrameworkMismatch }
    if (-not $AllowFrameworkMismatch) { Assert-ProductFrameworkPair @pair -CheckOnly }

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $executable = Join-Path $OutputDirectory $fileName
    $pending = Join-Path $OutputDirectory ("$fileName.new-" + [Guid]::NewGuid().ToString('N'))
    try {
        Copy-Item -LiteralPath $entries[0].FullName -Destination $pending
        if (Test-Path -LiteralPath $executable -PathType Leaf) {
            [IO.File]::Replace($pending, $executable, [NullString]::Value)
        } else {
            Move-Item -LiteralPath $pending -Destination $executable
        }
    }
    finally {
        if (Test-Path -LiteralPath $pending -PathType Leaf) { Remove-Item -LiteralPath $pending -Force }
    }
    Assert-ProductFrameworkPair @pair
    Write-Host "Single-file publish completed: $executable ($($entries[0].Length) bytes)"
}

function Assert-PlayerRawKernel {
    param([string]$ProjectRoot, [string]$Configuration)
    $kernel = Join-Path $ProjectRoot "FFF.Native\x64\$Configuration\FFF.Native.dll"
    $image = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($kernel))
    if (-not ($image.Contains('libraw.dll') -and $image.Contains('FFF3FP_GetRawInfo'))) {
        throw "发布中止：内核缺少 RAW 标识（libraw.dll / FFF3FP_GetRawInfo）：$kernel"
    }
}
