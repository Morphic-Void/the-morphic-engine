
# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# File:    run_tests.ps1
# Authors: Ritchie Brannan / OpenAI Codex
# Date:    20 Sep 26
#
# Build and exercise real DLL lifecycle fixtures against the Host process.

param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'Win32')] [string] $Platform = 'x64',
    [string] $MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$binaryDirectory = Join-Path $repository "build/bin/$Platform/$Configuration"
$suffix = [Guid]::NewGuid().ToString('N').Substring(0, 8)

if (!$SkipBuild) {
    $solutionPlatform = if ($Platform -eq 'Win32') { 'x86' } else { $Platform }
    & $MSBuild (Join-Path $repository 'MorphicEngine.sln') /m "/p:Configuration=$Configuration" "/p:Platform=$solutionPlatform" /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Solution build failed.' }
    foreach ($role in @('Service', 'Executive')) {
        & $MSBuild (Join-Path $repository 'MorphicLifecycleFixture.vcxproj') /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:SolutionDir=$repository\" "/p:FixtureRole=$role" /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "$role fixture build failed." }
    }
}

foreach ($case in @('ordinary', 'dependency', 'disposal', 'disposal-during-save', 'shutdown', 'shutdown-dependency', 'replace', 'replace-dependency', 'replace-missing', 'bootstrap-missing', 'startup-failure', 'replace-startup-failure')) {
    $tag = "lifecycle-$suffix-$case"
    $executive = if ($case -eq 'bootstrap-missing') { 'MorphicMissingExecutive.dll' } else { 'MorphicLifecycleExecutive.dll' }
    $expectedExit = if ($case.EndsWith('-missing') -or $case.EndsWith('-failure')) { 1 } else { 0 }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = Join-Path $binaryDirectory 'MorphicEngine.exe'
    $process.StartInfo.WorkingDirectory = $repository
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.ArgumentList.Add("--executive=$executive")
    $process.StartInfo.ArgumentList.Add("--log-tag=$tag")
    $process.StartInfo.Environment['MORPHIC_LIFECYCLE_CASE'] = $case
    try {
        if (!$process.Start()) { throw "Could not start $case." }
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            throw "$case timed out."
        }
        if ($process.ExitCode -ne $expectedExit) { throw "$case returned $($process.ExitCode), expected $expectedExit." }
        $log = Join-Path $repository "logs/morphic_debug.$tag.p$($process.Id).log"
        $events = Get-Content -LiteralPath $log -Raw
        if ($events -match 'Lifecycle fixture failed|notification failed|\[(error|critical|fatal):') { throw "$case has unexpected diagnostics: $log" }
        $assertions = [regex]::Matches($events, '\[assert:').Count
        $dependencyCleanup = $case -in @('dependency', 'shutdown-dependency', 'replace-dependency')
        $expectedAssertions = if ($dependencyCleanup) { 1 } else { $expectedExit }
        if ($assertions -ne $expectedAssertions) { throw "$case has $assertions assertion logs, expected $expectedAssertions." }
        if ($dependencyCleanup) {
            $cleanup = $events.IndexOf('Host: disposing retained assets dependent on module mount')
            if ($cleanup -lt 0) { throw "$case has no dependency cleanup diagnostic." }
            $unload = $events.IndexOf('Module unloaded', $cleanup)
            if ($unload -lt $cleanup) { throw "$case did not dispose dependencies before unload." }
            if (($case -ne 'dependency') -and ($events.IndexOf("Lifecycle fixture: $case passed") -gt $cleanup)) { throw "$case disposed assets before the outgoing Executive finished." }
        }
        if (($expectedExit -eq 1) -and !$events.Contains('Executive module lifecycle failed')) { throw "$case has no lifecycle failure diagnostic." }
        if (($case -ne 'bootstrap-missing') -and ($case -ne 'startup-failure')) {
            if (!$events.Contains("Lifecycle fixture: $case passed") -or
                !$events.Contains('Host requested Executive exit without notifications')) { throw "$case fixture did not complete." }
        }
        if (($case -eq 'ordinary') -and ([regex]::Matches($events, 'Lifecycle fixture: operation \d+ passed').Count -ne 9)) { throw 'Ordinary notification checks incomplete.' }
        if (($case -in @('dependency', 'disposal', 'disposal-during-save')) -and !$events.Contains('Lifecycle fixture: operation 2 passed')) { throw 'Asset disposal check incomplete.' }
        if (($case -in @('replace', 'replace-dependency')) -and !$events.Contains('Asset acceptance: 48 sequential and 32 concurrent operations passed')) { throw 'Replacement Executive did not complete its acceptance flow.' }
        if ($case -eq 'disposal-during-save') {
            $saved = [IO.File]::ReadAllBytes((Join-Path $repository 'build/lifecycle-disposal.bin'))
            if (($saved.Length -ne 16) -or @($saved | Where-Object { $_ -ne 0x5a }).Count) { throw 'Disposal corrupted the saved asset.' }
        }
        foreach ($line in ($events -split "`n")) {
            if (($line -match 'Module (loaded|unloaded)') -and ($line -notmatch '\[executable:bg_file_io\]')) { throw "Module operation ran outside the I/O worker: $line" }
        }
        Write-Output "$Configuration/$Platform $case passed: $log"
    }
    finally {
        $process.Dispose()
    }
}
