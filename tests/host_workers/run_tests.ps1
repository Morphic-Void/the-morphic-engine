# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# File:    run_tests.ps1
# Author:  OpenAI Codex
# Date:    7 Oct 26
#
# Exercise worker startup options, asset processing and shutdown in a built Host.

param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'Win32')] [string] $Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$executable = Join-Path $repository "build/bin/$Platform/$Configuration/MorphicEngine.exe"
$suffix = [Guid]::NewGuid().ToString('N').Substring(0, 8)
. (Join-Path $PSScriptRoot '../support/host_environment.ps1')
$runtime = New-HostTestEnvironment -Repository $repository -Name "host-workers-$suffix"

function Invoke-Host([string[]] $Arguments, [int] $ExpectedExit) {
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $executable
    $process.StartInfo.WorkingDirectory = $runtime
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $process.StartInfo.ArgumentList.Add($argument) }
    try {
        if (!$process.Start()) { throw 'Host did not start.' }
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Host timed out: $Arguments $($stderr.GetAwaiter().GetResult())"
        }
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne $ExpectedExit) { throw "Host returned $($process.ExitCode), expected $ExpectedExit`: $Arguments $errorText" }
        if (($ExpectedExit -eq 2) -and ($errorText -notmatch '--host-workers')) { throw 'Missing invalid-option diagnostic.' }
        return $process.Id
    }
    finally { $process.Dispose() }
}

foreach ($arguments in @(
    @('--host-workers'), @('--host-workers='), @('--host-workers=0'),
    @('--host-workers=-1'), @('--host-workers=+2'), @('--host-workers=abc'),
    @('--host-workers=2x'), @('--host-workers=4294967296'),
    @('--host-workers=1', '--host-workers=2')
)) {
    $null = Invoke-Host -Arguments $arguments -ExpectedExit 2
}
Write-Output "$Configuration/$Platform invalid worker options passed"

foreach ($requested in @(0, 1, 2, 9, 128)) {
    $tag = "workers-$suffix-$requested"
    $arguments = @("--log-tag=$tag", '--log-directory=development/logical-roots/test-logs')
    if ($requested -ne 0) { $arguments += "--host-workers=$requested" }
    $processId = Invoke-Host -Arguments $arguments -ExpectedExit 0
    $log = Join-Path $runtime "development/logical-roots/test-logs/morphic_debug.$tag.p$processId.log"
    $events = Get-Content -LiteralPath $log -Raw
    if ($events -match '\[(assert|error|critical|fatal):') { throw "Unexpected diagnostics: $log" }
    if (!$events.Contains('Asset acceptance: 48 sequential and 32 concurrent operations passed') -or
        !$events.Contains('Filesystem acceptance: 12 queued refresh, concurrent access and cache operations passed')) {
        throw "Acceptance flow incomplete: $log"
    }
    if ($events -notmatch 'Host: Background workers (\d+), dedicated conditioning workers (\d+), reported hardware threads (\d+)') {
        throw "Missing worker configuration: $log"
    }
    $count = [int] $Matches[1]
    $dedicated = [int] $Matches[2]
    $hardware = [long] $Matches[3]
    $requestCount = if ($requested -eq 0) { 2 } else { $requested }
    $budget = [Math]::Min($hardware, 64)
    $floor = if ($budget -ge 8) { 2 } else { 1 }
    $expected = [Math]::Min($requestCount, [Math]::Min(9, [Math]::Max($floor, $budget - 8)))
    if (($count -ne $expected) -or ($dedicated -ne $(if ($count -gt 1) { $count - 1 } else { 0 }))) { throw "Unexpected worker sizing: $log" }
    if (($count -lt $requestCount) -and !$events.Contains("Host: Worker count reduced from $requestCount to $count")) {
        throw "Missing worker reduction diagnostic: $log"
    }
    $identity = '\[executable:(bg_file_io|bg_conditioning_0[0-7])\]'
    $starts = [regex]::Matches($events, "$identity.*Worker starting")
    $exits = [regex]::Matches($events, "$identity.*Worker exited")
    if (($starts.Count -ne $count) -or ($exits.Count -ne $count) -or
        @($starts | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique).Count -ne $count) {
        throw "Workers did not start and exit exactly once: $log"
    }
    if ($Configuration -eq 'Debug') {
        $conditioning = [regex]::Matches($events, "$identity.*Worker (document conditioning|TGA encode|TGA decode) request")
        $used = @($conditioning | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        $expectedUsed = if ($count -eq 1) { 1 } else { $count - 1 }
        if (($used.Count -ne $expectedUsed) -or (($count -gt 1) -and ($used -contains 'bg_file_io'))) {
            throw "Conditioning did not use the configured workers: $log"
        }
        foreach ($io in [regex]::Matches($events, "$identity.*Worker (file load|file save|filesystem scan|module lifecycle) request")) {
            if ($io.Groups[1].Value -ne 'bg_file_io') { throw "I/O work ran on a conditioning worker: $log" }
        }
    }
    $reference = $null
    for ($index = 0; $index -lt 32; ++$index) {
        $saved = [IO.File]::ReadAllText((Join-Path $runtime ('development/logical-roots/test-output/asset-concurrent-{0:D2}.json' -f $index)))
        if ([string]::IsNullOrWhiteSpace($saved)) { throw 'Concurrent conditioning saved an empty document.' }
        if ($index -eq 0) {
            $reference = $saved
            $null = ConvertFrom-Json -InputObject $reference
        }
        elseif ($saved -cne $reference) { throw 'Concurrent conditioning produced inconsistent documents.' }
    }
    Write-Output "$Configuration/$Platform requested $requestCount, effective $count passed: $log"
}
