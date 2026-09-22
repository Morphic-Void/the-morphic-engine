
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
    foreach ($role in @('MissingThread', 'RenderingFailure', 'RenderingDisposal', 'Executive')) {
        & $MSBuild (Join-Path $repository 'MorphicLifecycleFixture.vcxproj') /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:SolutionDir=$repository\" "/p:FixtureRole=$role" /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "$role fixture build failed." }
    }
}

foreach ($case in @('ordinary', 'dependency', 'disposal', 'disposal-during-save', 'shutdown', 'shutdown-dependency', 'replace', 'replace-dependency', 'replace-missing', 'bootstrap-missing', 'startup-failure', 'replace-startup-failure', 'thread-failures', 'render-shutdown', 'render-shutdown-dependency', 'render-replace-dependency', 'render-executive-replace', 'render-drain', 'render-exit-disposal', 'normal-startup', 'normal-unavailable', 'normal-bad-start', 'normal-missing-thread')) {
    $tag = "lifecycle-$suffix-$case"
    $normalExecutive = $case.StartsWith('normal-')
    $serviceFailure = $case -in @('normal-unavailable', 'normal-bad-start', 'normal-missing-thread')
    $executive = if ($case -eq 'bootstrap-missing') { 'MorphicMissingExecutive.dll' } elseif ($normalExecutive) { 'MorphicExecutive.dll' } else { 'MorphicLifecycleExecutive.dll' }
    $launchDirectory = $binaryDirectory
    if ($serviceFailure) {
        # Isolate missing/failing required services without modifying the built DLLs.
        $launchDirectory = Join-Path $repository "build/lifecycle-$suffix-$case"
        New-Item -ItemType Directory -Path $launchDirectory | Out-Null
        foreach ($file in @('MorphicEngine.exe', 'MorphicExecutive.dll')) {
            Copy-Item -LiteralPath (Join-Path $binaryDirectory $file) -Destination (Join-Path $launchDirectory $file)
        }
        $fixture = if ($case -eq 'normal-bad-start') { 'MorphicLifecycleRenderingFailure.dll' } elseif ($case -eq 'normal-missing-thread') { 'MorphicLifecycleMissingThread.dll' }
        if ($fixture) {
            Copy-Item -LiteralPath (Join-Path $binaryDirectory $fixture) -Destination (Join-Path $launchDirectory 'MorphicRendering.dll')
        }
    }
    $expectedExit = if ($case.EndsWith('-missing') -or $case.EndsWith('-failure')) { 1 } else { 0 }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = Join-Path $launchDirectory 'MorphicEngine.exe'
    $process.StartInfo.WorkingDirectory = $repository
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.ArgumentList.Add("--executive=$executive")
    $process.StartInfo.ArgumentList.Add("--log-tag=$tag")
    $process.StartInfo.ArgumentList.Add('--log-directory=development/logical-roots/test-logs')
    $process.StartInfo.Environment['MORPHIC_LIFECYCLE_CASE'] = $case
    try {
        if (!$process.Start()) { throw "Could not start $case." }
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            throw "$case timed out."
        }
        if ($process.ExitCode -ne $expectedExit) { throw "$case returned $($process.ExitCode), expected $expectedExit." }
        $log = Join-Path $repository "development/logical-roots/test-logs/morphic_debug.$tag.p$($process.Id).log"
        $events = Get-Content -LiteralPath $log -Raw
        if ($events -match 'Lifecycle fixture failed|notification failed|\[(error|critical|fatal):') { throw "$case has unexpected diagnostics: $log" }
        $assertions = [regex]::Matches($events, '\[assert:').Count
        $dependencyCleanup = $case -in @('dependency', 'shutdown-dependency', 'replace-dependency', 'render-shutdown-dependency', 'render-replace-dependency', 'render-drain')
        $expectedAssertions = if ($dependencyCleanup) { 1 } else { $expectedExit }
        if ($assertions -ne $expectedAssertions) { throw "$case has $assertions assertion logs, expected $expectedAssertions." }
        if ($dependencyCleanup) {
            $cleanup = $events.IndexOf('Host: disposing retained assets dependent on module mount')
            if ($cleanup -lt 0) { throw "$case has no dependency cleanup diagnostic." }
            $unload = $events.IndexOf('Module unloaded', $cleanup)
            if ($unload -lt $cleanup) { throw "$case did not dispose dependencies before unload." }
            if (($case -notin @('dependency', 'render-replace-dependency', 'render-drain')) -and ($events.IndexOf("Lifecycle fixture: $case passed") -gt $cleanup)) { throw "$case disposed assets before the outgoing Executive finished." }
        }
        if (($expectedExit -eq 1) -and !$events.Contains('Executive module lifecycle failed')) { throw "$case has no lifecycle failure diagnostic." }
        if (!$normalExecutive -and ($case -ne 'bootstrap-missing') -and ($case -ne 'startup-failure')) {
            if (!$events.Contains("Lifecycle fixture: $case passed") -or
                !$events.Contains('Host requested Executive exit without notifications')) { throw "$case fixture did not complete." }
        }
        if (($case -eq 'ordinary') -and ([regex]::Matches($events, 'Lifecycle fixture: operation \d+ passed').Count -ne 9)) { throw 'Ordinary notification checks incomplete.' }
        if (($case -in @('dependency', 'disposal', 'disposal-during-save')) -and !$events.Contains('Lifecycle fixture: operation 2 passed')) { throw 'Asset disposal check incomplete.' }
        if (($case -in @('replace', 'replace-dependency', 'render-executive-replace')) -and !$events.Contains('Asset acceptance: 48 sequential and 32 concurrent operations passed')) { throw 'Replacement Executive did not complete its acceptance flow.' }
        if ($case -in @('disposal-during-save', 'render-drain')) {
            $saved = [IO.File]::ReadAllBytes((Join-Path $repository 'development/logical-roots/test-output/lifecycle-disposal.bin'))
            if (($saved.Length -ne 16) -or @($saved | Where-Object { $_ -ne 0x5a }).Count) { throw 'Disposal corrupted the saved asset.' }
        }
        if ($normalExecutive) {
            $requested = $events.IndexOf('Executive: Rendering load requested')
            $acknowledged = $events.IndexOf('Executive: Rendering load acknowledged')
            $ready = $events.IndexOf('Executive: Rendering ready')
            if (($requested -lt 0) -or ($acknowledged -lt $requested)) { throw "$case did not acknowledge the Executive's rendering request." }
            if ($serviceFailure) {
                if (($ready -ge 0) -or $events.Contains('Asset acceptance:') -or $events.Contains('Executive: retain_raw passed') -or
                    !$events.Contains('Executive: Rendering unavailable or invalid reply; requesting system shutdown') -or
                    !$events.Contains('Executive: Exited')) { throw "$case did not stop cleanly before asset acceptance." }
            }
            elseif (($ready -lt $acknowledged) -or ($events.IndexOf('Executive: retain_raw passed') -lt $ready) -or
                !$events.Contains('Asset acceptance: 48 sequential and 32 concurrent operations passed')) {
                throw "$case did not wait for rendering readiness before asset acceptance."
            }
        }
        $starts = [regex]::Matches($events, 'Host: Rendering started after asynchronous binding').Count
        $joins = [regex]::Matches($events, 'Host: Rendering thread joined').Count
        if (($case -eq 'normal-startup') -and ($starts -ne 1)) { throw 'Normal Executive did not start exactly one renderer.' }
        if (($serviceFailure -or ($case -in @('shutdown', 'shutdown-dependency'))) -and ($starts -ne 0)) { throw "$case unexpectedly started a renderer." }
        if (($case -eq 'render-executive-replace') -and !$events.Contains('Executive: Rendering ready (already loaded)')) { throw 'Replacement Executive did not reuse the available renderer.' }
        if ($starts -ne $joins) { throw "$case left a rendering thread unjoined." }
        if (($case -eq 'render-executive-replace') -and (($starts -ne 1) -or
            ($events.IndexOf('Host: Rendering thread joined') -lt $events.IndexOf('Asset acceptance: 48 sequential and 32 concurrent operations passed')))) {
            throw 'Rendering did not survive the Executive replacement and acceptance flow.'
        }
        if (($case -in @('ordinary', 'dependency', 'disposal', 'disposal-during-save', 'thread-failures', 'render-shutdown', 'render-shutdown-dependency', 'render-replace-dependency', 'render-executive-replace', 'render-drain', 'render-exit-disposal')) -and ($starts -eq 0)) {
            throw "$case did not start the real rendering thread."
        }
        if (($case -eq 'thread-failures') -and ([regex]::Matches($events, 'Lifecycle fixture: operation \d+ passed').Count -ne 6)) {
            throw 'Rendering failure checks incomplete.'
        }
        if ($case -eq 'render-exit-disposal') {
            if (!$events.Contains('Lifecycle rendering: final disposals posted')) { throw 'Rendering exit requests were not posted.' }
            $saved = [IO.File]::ReadAllBytes((Join-Path $repository 'development/logical-roots/test-output/lifecycle-disposal.bin'))
            if (($saved.Length -ne 1048576) -or @($saved | Where-Object { $_ -ne 0x5a }).Count) { throw 'Rendering exit disposal corrupted an accepted save.' }
            if ($Configuration -eq 'Debug') {
                $deferred = $events.IndexOf('Host asset disposal deferred for accepted borrowers at client slot 600')
                $completed = $events.IndexOf('Host asset disposal completed at client slot 600')
                $finalCompleted = $events.IndexOf('Host asset disposal completed at client slot 601')
                $joined = $events.IndexOf('Host: Rendering thread joined')
                if (($deferred -lt 0) -or ($completed -lt $deferred) -or ($finalCompleted -lt 0) -or
                    ($joined -lt $completed) -or ($joined -lt $finalCompleted)) {
                    throw 'Rendering disposal did not exercise deferred completion and final request drain before package destruction.'
                }
            }
        }
        $running = $false
        $exited = $false
        foreach ($line in ($events -split "`n")) {
            if ($line.Contains('Rendering: Running')) {
                if ($line -notmatch '\[render_vulkan_windows:rendering\]') { throw "$case used an unexpected rendering identity." }
                if ($running) { throw "$case started overlapping rendering threads." }
                $running = $true
                $exited = $false
            }
            if ($line.Contains('Rendering: Exited')) { $exited = $true }
            if ($line.Contains('Host: Rendering thread joined')) {
                if (!$running -or !$exited) { throw "$case joined before the rendering exit path completed." }
                $running = $false
            }
            if ($line.Contains('Module unloaded on Host worker, mount render') -and $running) {
                throw "$case unloaded a live rendering DLL."
            }
            if (($case -in @('dependency', 'render-shutdown-dependency', 'render-replace-dependency', 'render-drain')) -and $line.Contains('Host: disposing retained assets dependent on module mount') -and $running) {
                throw "$case disposed dependencies before joining rendering."
            }
            if (($line -match 'Module (loaded|unloaded)') -and ($line -notmatch '\[executable:bg_file_io\]')) { throw "Module operation ran outside the I/O worker: $line" }
        }
        Write-Output "$Configuration/$Platform $case passed: $log"
    }
    finally {
        $process.Dispose()
    }
}
