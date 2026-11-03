# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'Win32')] [string] $Platform = 'x64',
    [ValidateSet(1, 2)] [int] $WorkerCount = 2,
    [string] $MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    [switch] $SkipBuild
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (!$SkipBuild) {
    foreach ($role in @('Provider', 'Executive', 'Replacement')) {
        & $MSBuild (Join-Path $repository 'visual_studio/MorphicPumpFixture.vcxproj') /m:2 `
            "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:SolutionDir=$repository\" "/p:FixtureRole=$role" /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "$role pump fixture build failed." }
    }
}
python (Join-Path $PSScriptRoot 'run_tests.py') --engine "$repository/build/bin/$Platform/$Configuration/MorphicEngine.exe" `
    --host-workers $WorkerCount --output-directory "$repository/build/pump-$Platform-$Configuration"
if ($LASTEXITCODE -ne 0) { throw 'Host pump acceptance failed.' }
