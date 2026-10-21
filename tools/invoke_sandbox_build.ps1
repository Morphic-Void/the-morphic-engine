param(
    [ValidateSet("Debug", "Development", "Release")]
    [string] $Configuration = "Debug",

    [ValidateSet("x64", "x86")]
    [string] $Platform = "x64",

    [ValidateSet("Build", "Rebuild")]
    [string] $Target = "Build",

    [string] $WindowsSdkVersion,
    [string] $MSBuildPath,

    [switch] $RunTests,

    [ValidateRange(0, 3)]
    [int] $TestMode = 1,

    [ValidatePattern('^[A-Za-z0-9._-]{1,48}$')]
    [string] $LogTag = "sandbox"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot "MorphicEngine.sln"
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$lineEndingCheck = Join-Path $PSScriptRoot "check_line_endings.ps1"

& $lineEndingCheck -RepositoryRoot $repositoryRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ([string]::IsNullOrWhiteSpace($WindowsSdkVersion)) {
    $installedSdks = @()
    $includeRoot = Join-Path $sdkRoot "Include"
    foreach ($directory in Get-ChildItem -LiteralPath $includeRoot -Directory) {
        $parsedVersion = $null
        if ([Version]::TryParse($directory.Name, [ref] $parsedVersion) -and
            (Test-Path -LiteralPath (Join-Path $directory.FullName "um\Windows.h"))) {
            $installedSdks += [pscustomobject]@{
                Name = $directory.Name
                Version = $parsedVersion
            }
        }
    }
    $WindowsSdkVersion = $installedSdks |
        Sort-Object Version -Descending |
        Select-Object -First 1 -ExpandProperty Name
}

if ([string]::IsNullOrWhiteSpace($WindowsSdkVersion)) {
    throw "No Windows 10/11 SDK was found beneath '$sdkRoot'."
}

if ([string]::IsNullOrWhiteSpace($MSBuildPath)) {
    $msbuildCandidates = @(
        Get-ChildItem -Path "$env:ProgramFiles\Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe" -File -ErrorAction SilentlyContinue
        Get-ChildItem -Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe" -File -ErrorAction SilentlyContinue
    ) | Sort-Object -Property @{ Expression = {
        $version = $_.VersionInfo
        [version]::new($version.FileMajorPart, $version.FileMinorPart,
            $version.FileBuildPart, $version.FilePrivatePart)
    }; Descending = $true }, FullName
    $MSBuildPath = $msbuildCandidates |
        Select-Object -First 1 -ExpandProperty FullName
}

if ([string]::IsNullOrWhiteSpace($MSBuildPath) -or
    ![System.IO.File]::Exists($MSBuildPath)) {
    throw "MSBuild.exe was not found. Supply its absolute path with -MSBuildPath."
}

# The sandbox can provide both PATH and Path. .NET Framework ToolTask copies
# environment variables into a case-insensitive dictionary and rejects that pair.
$processPath = $env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:PATH = $processPath

# Use the native 64-bit compiler/linker on a 64-bit host. The x86-hosted PDB
# tools can fail inside the sandbox even when their DLL versions match.
# This selects the build tools, not the architecture of the generated binaries.
$toolArchitecture = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }

$msbuildArguments = @(
    $solutionPath
    "/t:$Target"
    "/m:1"
    "/p:Configuration=$Configuration"
    "/p:Platform=$Platform"
    "/p:PreferredToolArchitecture=$toolArchitecture"
    "/p:_LatestWindowsTargetPlatformVersion=$WindowsSdkVersion"
    "/p:TargetPlatformSdkPath=$sdkRoot\"
    "/p:TargetPlatformDisplayName=Windows SDK $WindowsSdkVersion"
    "/p:TrackFileAccess=false"
    "/p:ForceRebuild=true"
    "/v:minimal"
)

Write-Host "Sandbox build: $Configuration|$Platform, Windows SDK $WindowsSdkVersion"
Write-Host "MSBuild: $MSBuildPath ($((Get-Item -LiteralPath $MSBuildPath).VersionInfo.FileVersion)), tool host: $toolArchitecture"
& $MSBuildPath @msbuildArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($RunTests) {
    $outputPlatform = if ($Platform -eq "x86") { "Win32" } else { $Platform }
    $testExecutable = Join-Path $repositoryRoot "build\bin\$outputPlatform\$Configuration\MorphicTests.exe"
    if (![System.IO.File]::Exists($testExecutable)) {
        throw "The test executable was not produced at '$testExecutable'."
    }

    & $testExecutable "-t$TestMode" "--log-tag=$LogTag"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
