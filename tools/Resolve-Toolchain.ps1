$ErrorActionPreference = "Stop"

function ConvertTo-ExistingFilePath {
    param([AllowNull()] [string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $expanded = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"'))
    if (Test-Path -LiteralPath $expanded -PathType Leaf) {
        return [IO.Path]::GetFullPath($expanded)
    }
    return $null
}

function ConvertTo-ExistingDirectoryPath {
    param([AllowNull()] [string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $expanded = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"'))
    if (Test-Path -LiteralPath $expanded -PathType Container) {
        return [IO.Path]::GetFullPath($expanded)
    }
    return $null
}

function Get-EnvironmentApplication {
    param(
        [Parameter(Mandatory = $true)] [string[]]$Name,
        [string[]]$EnvironmentVariable = @()
    )

    # Explicit executable variables and the current process PATH are the
    # environment-provided toolchain. They always win over Visual Studio.
    foreach ($variableName in $EnvironmentVariable) {
        $environmentPath = ConvertTo-ExistingFilePath ([Environment]::GetEnvironmentVariable($variableName))
        if ($null -ne $environmentPath) { return $environmentPath }
    }
    foreach ($candidateName in $Name) {
        $command = Get-Command $candidateName -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $command) {
            $resolved = ConvertTo-ExistingFilePath $command.Source
            if ($null -ne $resolved) { return $resolved }
        }
    }
    return $null
}

function Get-VsWhereTool {
    $vswhere = Get-EnvironmentApplication -Name @("vswhere.exe") -EnvironmentVariable @("VSWHERE_EXE_PATH")
    if ($null -ne $vswhere) { return $vswhere }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        return ConvertTo-ExistingFilePath (Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe")
    }
    return $null
}

function Find-VisualStudioInstallation {
    param([Parameter(Mandatory = $true)] [bool]$Prerelease)

    $vswhere = Get-VsWhereTool
    if ($null -eq $vswhere) { return $null }

    $arguments = @("-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json")
    if ($Prerelease) { $arguments = @("-prerelease") + $arguments }
    $json = @(& $vswhere @arguments)
    if ($LASTEXITCODE -ne 0 -or $json.Count -eq 0) { return $null }

    try { $installations = @($json | ConvertFrom-Json) }
    catch { return $null }
    $found = $installations |
        Where-Object { $_.isComplete -and [bool]$_.isPrerelease -eq $Prerelease } |
        Sort-Object installationVersion -Descending |
        Select-Object -First 1
    if ($null -eq $found) { return $null }
    return ConvertTo-ExistingDirectoryPath $found.installationPath
}

function Get-VisualStudioCandidateDirectories {
    param([string]$PreferredDirectory = "")

    $candidates = New-Object 'System.Collections.Generic.List[string]'
    $addCandidate = {
        param([string]$Candidate)
        if (-not [string]::IsNullOrWhiteSpace($Candidate) -and -not $candidates.Contains($Candidate)) {
            [void]$candidates.Add($Candidate)
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($PreferredDirectory)) {
        $preferred = ConvertTo-ExistingDirectoryPath $PreferredDirectory
        if ($null -eq $preferred) { throw "The specified Visual Studio directory does not exist: $PreferredDirectory" }
        & $addCandidate $preferred
        return @($candidates)
    }

    # A Developer Command Prompt exposes VSINSTALLDIR. Treat it as the first
    # choice, regardless of whether it belongs to the stable or preview channel.
    foreach ($variableName in @("VSINSTALLDIR", "VSINSTALLATIONPATH")) {
        $environmentDirectory = ConvertTo-ExistingDirectoryPath ([Environment]::GetEnvironmentVariable($variableName))
        & $addCandidate $environmentDirectory
    }

    # The order here is intentional: stable Visual Studio precedes Preview.
    & $addCandidate (Find-VisualStudioInstallation -Prerelease:$false)
    & $addCandidate (Find-VisualStudioInstallation -Prerelease:$true)

    # Keep a useful fallback for preview installations when vswhere is not on
    # disk (for example, a copied portable build).
    & $addCandidate (ConvertTo-ExistingDirectoryPath "C:\Program Files\Microsoft Visual Studio\18\Insiders")
    return @($candidates)
}

function Get-VisualStudioInstallation {
    param(
        [string]$PreferredDirectory = "",
        [switch]$AllowMissing
    )

    $candidate = @(Get-VisualStudioCandidateDirectories -PreferredDirectory $PreferredDirectory) |
        Select-Object -First 1
    if ($null -ne $candidate) { return $candidate }
    if ($AllowMissing) { return $null }
    throw "Visual Studio with Desktop development with C++ was not found (checked environment, stable, then preview)."
}

function Get-MSBuildTool {
    param([string]$PreferredVisualStudioDirectory = "")

    $environmentTool = Get-EnvironmentApplication -Name @("MSBuild.exe") -EnvironmentVariable @("MSBUILD_EXE_PATH")
    if ($null -ne $environmentTool) { return $environmentTool }

    foreach ($vsDirectory in @(Get-VisualStudioCandidateDirectories -PreferredDirectory $PreferredVisualStudioDirectory)) {
        foreach ($relativePath in @("MSBuild\Current\Bin\amd64\MSBuild.exe", "MSBuild\Current\Bin\MSBuild.exe")) {
            $candidate = ConvertTo-ExistingFilePath (Join-Path $vsDirectory $relativePath)
            if ($null -ne $candidate) { return $candidate }
        }
    }
    throw "MSBuild was not found in the environment, stable Visual Studio, or Visual Studio Preview."
}

function Get-DotNetTool {
    $environmentTool = Get-EnvironmentApplication -Name @("dotnet.exe") -EnvironmentVariable @("DOTNET_EXE_PATH")
    if ($null -ne $environmentTool) { return $environmentTool }
    $dotNetRoot = ConvertTo-ExistingDirectoryPath $env:DOTNET_ROOT
    if ($null -ne $dotNetRoot) {
        $environmentTool = ConvertTo-ExistingFilePath (Join-Path $dotNetRoot "dotnet.exe")
        if ($null -ne $environmentTool) { return $environmentTool }
    }
    throw "dotnet.exe was not found in the environment. Install the .NET SDK or add it to PATH."
}

function Get-CMakeTool {
    param(
        [string]$PreferredVisualStudioDirectory = "",
        [switch]$AllowMissing
    )

    $environmentTool = Get-EnvironmentApplication -Name @("cmake.exe") -EnvironmentVariable @("CMAKE_EXE_PATH")
    if ($null -ne $environmentTool) { return $environmentTool }

    foreach ($vsDirectory in @(Get-VisualStudioCandidateDirectories -PreferredDirectory $PreferredVisualStudioDirectory)) {
        $candidate = ConvertTo-ExistingFilePath (Join-Path $vsDirectory "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
        if ($null -ne $candidate) { return $candidate }
    }
    if ($AllowMissing) { return $null }
    throw "cmake.exe was not found in the environment, stable Visual Studio, or Visual Studio Preview."
}

function Get-NinjaTool {
    param(
        [string]$PreferredVisualStudioDirectory = "",
        [switch]$AllowMissing
    )

    $environmentTool = Get-EnvironmentApplication -Name @("ninja.exe") -EnvironmentVariable @("NINJA_EXE_PATH")
    if ($null -ne $environmentTool) { return $environmentTool }

    foreach ($vsDirectory in @(Get-VisualStudioCandidateDirectories -PreferredDirectory $PreferredVisualStudioDirectory)) {
        $candidate = ConvertTo-ExistingFilePath (Join-Path $vsDirectory "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
        if ($null -ne $candidate) { return $candidate }
    }
    if ($AllowMissing) { return $null }
    throw "ninja.exe was not found in the environment, stable Visual Studio, or Visual Studio Preview."
}

function Get-VcpkgTool {
    param([string]$PreferredVisualStudioDirectory = "")

    $environmentTool = Get-EnvironmentApplication -Name @("vcpkg.exe") -EnvironmentVariable @("VCPKG_EXE_PATH")
    if ($null -ne $environmentTool) { return $environmentTool }
    foreach ($rootVariable in @("VCPKG_ROOT", "VCPKG_INSTALLATION_ROOT")) {
        $root = ConvertTo-ExistingDirectoryPath ([Environment]::GetEnvironmentVariable($rootVariable))
        if ($null -ne $root) {
            $candidate = ConvertTo-ExistingFilePath (Join-Path $root "vcpkg.exe")
            if ($null -ne $candidate) { return $candidate }
        }
    }

    foreach ($vsDirectory in @(Get-VisualStudioCandidateDirectories -PreferredDirectory $PreferredVisualStudioDirectory)) {
        $candidate = ConvertTo-ExistingFilePath (Join-Path $vsDirectory "VC\vcpkg\vcpkg.exe")
        if ($null -ne $candidate) { return $candidate }
    }
    throw "vcpkg was not found in the environment, stable Visual Studio, or Visual Studio Preview."
}

function Get-GitTool {
    param([string]$PreferredVisualStudioDirectory = "")

    $environmentTool = Get-EnvironmentApplication -Name @("git.exe") -EnvironmentVariable @("GIT_EXE_PATH")
    if ($null -ne $environmentTool) { return $environmentTool }

    foreach ($vsDirectory in @(Get-VisualStudioCandidateDirectories -PreferredDirectory $PreferredVisualStudioDirectory)) {
        foreach ($relativePath in @(
            "Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe",
            "Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin\git.exe")) {
            $candidate = ConvertTo-ExistingFilePath (Join-Path $vsDirectory $relativePath)
            if ($null -ne $candidate) { return $candidate }
        }
    }
    throw "git.exe was not found in the environment, stable Visual Studio, or Visual Studio Preview. Git will not be downloaded."
}

function Get-VisualCppTool {
    param([Parameter(Mandatory = $true)] [string]$Name)

    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsInstallDir)) {
        $environmentCandidate = ConvertTo-ExistingFilePath (Join-Path $env:VCToolsInstallDir "bin\Hostx64\x64\$Name")
        if ($null -ne $environmentCandidate) { return $environmentCandidate }
    }

    $environmentTool = Get-EnvironmentApplication -Name @($Name)
    if ($null -ne $environmentTool) { return $environmentTool }

    foreach ($vsDirectory in @(Get-VisualStudioCandidateDirectories)) {
        $versionFile = Join-Path $vsDirectory "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
        if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
            $version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
            $candidate = ConvertTo-ExistingFilePath (Join-Path $vsDirectory "VC\Tools\MSVC\$version\bin\Hostx64\x64\$Name")
            if ($null -ne $candidate) { return $candidate }
        }
    }
    throw "$Name was not found in the environment, stable Visual Studio, or Visual Studio Preview."
}

function Get-ProductTargetFramework {
    param(
        [Parameter(Mandatory = $true)] [string]$ProjectPath,
        [string]$NetTFM = "",
        [string]$DotNet = "dotnet"
    )

    # 取值而不是解析：TargetFramework 在工程里是 $(NetTFM) 表达式，正则去啃 XML
    # 会在下次改工程时悄悄给出旧值。dotnet msbuild -getProperty 是权威求值。
    $arguments = @("msbuild", $ProjectPath, "-nologo", "-v:q", "-getProperty:TargetFramework")
    if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $arguments += "-p:NetTFM=$NetTFM" }
    $output = @(& $DotNet @arguments)
    if ($LASTEXITCODE -ne 0) { throw "Could not evaluate TargetFramework for $ProjectPath." }
    $framework = @($output | Where-Object { $_ -match '^net\d' }) | Select-Object -Last 1
    if ($null -eq $framework) { throw "TargetFramework evaluated empty for $ProjectPath." }
    return $framework.Trim()
}

function Assert-ProductFrameworkPair {
    param(
        [Parameter(Mandatory = $true)] [string]$OutputDirectory,
        [Parameter(Mandatory = $true)] [string]$Product,
        [Parameter(Mandatory = $true)] [string]$Framework,
        [switch]$AllowMismatch
    )

    # Record the full TFM, but compare the .NET runtime version: a Windows SDK
    # target version affects API projections, not the shared .NET runtime version.
    $manifest = Join-Path $OutputDirectory ".product-frameworks.txt"
    $entries = @{}
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        foreach ($line in @(Get-Content -LiteralPath $manifest -ErrorAction SilentlyContinue)) {
            $parts = $line.Split("=", 2)
            if ($parts.Count -eq 2 -and -not [string]::IsNullOrWhiteSpace($parts[0])) {
                $entries[$parts[0].Trim()] = $parts[1].Trim()
            }
        }
    }
    foreach ($name in @($entries.Keys)) {
        if ($name -eq $Product) { continue }
        if ($entries[$name] -eq $Framework) { continue }
        $otherMatch = [regex]::Match($entries[$name], '^(net\d+\.\d+)-windows(?:\d+(?:\.\d+)*)?$', 'IgnoreCase')
        $currentMatch = [regex]::Match($Framework, '^(net\d+\.\d+)-windows(?:\d+(?:\.\d+)*)?$', 'IgnoreCase')
        if ($otherMatch.Success -and $currentMatch.Success -and
            $otherMatch.Groups[1].Value -eq $currentMatch.Groups[1].Value) { continue }
        $message = ("$name in $OutputDirectory was published for $($entries[$name]) but " +
            "$Product resolves to $Framework. Re-publish both for the same .NET runtime version, " +
            "or pass -AllowFrameworkMismatch on purpose.")
        if (-not $AllowMismatch) { throw $message }
        Write-Warning $message
    }
    $entries[$Product] = $Framework
    $lines = @($entries.Keys | Sort-Object | ForEach-Object { "$_=$($entries[$_])" })
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    [IO.File]::WriteAllLines($manifest, $lines, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Product framework recorded: $Product=$Framework ($manifest)"
}
