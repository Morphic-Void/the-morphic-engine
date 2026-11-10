# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'Win32')] [string] $Platform = 'x64',
    [ValidateSet(1, 2)] [int] $WorkerCount = 2,
    [string] $MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    [string] $Python = 'python',
    [switch] $SkipBuild
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (!$SkipBuild) {
    foreach ($project in @('MorphicInputPlatform', 'MorphicInputExecutive', 'MorphicInputSecondary')) {
        & $MSBuild (Join-Path $repository "visual_studio/$project.vcxproj") /m:2 `
            "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:SolutionDir=$repository\" /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "$project build failed." }
    }
}
foreach ($case in @('lifecycle', 'pressure', 'transitions', 'work-pressure', 'second-consumer',
        'shutdown-blocked', 'ingress-loss', 'acquisition-cleanup', 'source-cleanup', 'load-smoke')) {
    & $Python (Join-Path $PSScriptRoot 'run_tests.py') --engine "$repository/build/bin/$Platform/$Configuration/MorphicEngine.exe" `
        --host-workers $WorkerCount --output-directory "$repository/build/input-host-$Platform-$Configuration" --case $case --rate 120
    if ($LASTEXITCODE -ne 0) { throw "Input Host $case acceptance failed." }
}
