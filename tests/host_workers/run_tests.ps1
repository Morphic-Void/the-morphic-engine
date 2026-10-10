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

function Invoke-Host([string[]] $Lines, [int] $ExpectedExit, [switch] $WithBom) {
    $configFile = Join-Path $runtime ("bootstrap config-$([Guid]::NewGuid().ToString('N')).cfg")
    [IO.File]::WriteAllLines($configFile, $Lines, [Text.UTF8Encoding]::new($WithBom.IsPresent))
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $executable
    $process.StartInfo.WorkingDirectory = $runtime
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardError = $true
    $process.StartInfo.ArgumentList.Add($configFile)
    try {
        if (!$process.Start()) { throw 'Host did not start.' }
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Host timed out: $Lines $($stderr.GetAwaiter().GetResult())"
        }
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne $ExpectedExit) { throw "Host returned $($process.ExitCode), expected $ExpectedExit`: $Lines $errorText" }
        if (($ExpectedExit -eq 2) -and ($errorText -notmatch 'Invalid bootstrap configuration')) { throw 'Missing invalid-configuration diagnostic.' }
        return $process.Id
    }
    finally {
        $process.Dispose()
        Remove-Item -LiteralPath $configFile
    }
}

function Invoke-LaunchFailure([string[]] $Arguments, [string] $ExpectedDiagnostic) {
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
            throw "Host timed out: $Arguments"
        }
        $errorText = $stderr.GetAwaiter().GetResult()
        if (($process.ExitCode -ne 2) -or !$errorText.Contains($ExpectedDiagnostic)) {
            throw "Unexpected launch result $($process.ExitCode)`: $Arguments $errorText"
        }
    }
    finally { $process.Dispose() }
}

Invoke-LaunchFailure -Arguments @() -ExpectedDiagnostic 'Usage: MorphicEngine'
Invoke-LaunchFailure -Arguments @('unused.cfg', 'extra') -ExpectedDiagnostic 'Usage: MorphicEngine'
Invoke-LaunchFailure -Arguments @('missing.cfg') -ExpectedDiagnostic 'cannot open file'
Write-Output "$Configuration/$Platform bootstrap argument validation passed"

foreach ($lines in @(
    @('host-workers'), @('host-workers='), @('host-workers=0'),
    @('host-workers=-1'), @('host-workers=+2'), @('host-workers=abc'),
    @('host-workers=2x'), @('host-workers=4294967296'),
    @('host-workers=1', 'host-workers=2')
)) {
    $null = Invoke-Host -Lines (@('executive=package:/bin/MorphicExecutive.dll') + $lines) -ExpectedExit 2
}
Write-Output "$Configuration/$Platform invalid worker settings passed"

foreach ($lines in @(
    @('batch-runners'), @('batch-runners='), @('batch-runners=-1'),
    @('batch-runners=+2'), @('batch-runners=abc'), @('batch-runners=2x'),
    @('batch-runners=4294967296'), @('batch-runners=1', 'batch-runners=2')
)) {
    $null = Invoke-Host -Lines (@('executive=package:/bin/MorphicExecutive.dll') + $lines) -ExpectedExit 2
}
Write-Output "$Configuration/$Platform invalid batch settings passed"

foreach ($lines in @(
    @('# no Executive selected'), @('executive='),
    @('executive=package:/bin/MorphicExecutive.dll', 'executive=package:/bin/MorphicExecutive.dll'),
    @('executive=package:/bin/MorphicExecutive.dll', 'unknown=1'),
    @('executive=package:/bin/MorphicExecutive.dll', 'log-tag=bad tag'),
    @('executive=package:/bin/MorphicExecutive.dll', ('#' + ('x' * 4096)))
)) {
    $null = Invoke-Host -Lines $lines -ExpectedExit 2
}
Write-Output "$Configuration/$Platform invalid bootstrap structure passed"

foreach ($requested in @(0, 1, 2, 3, 9, 128)) {
    $tag = "workers-$suffix-$requested"
    $lines = @('executive=package:/bin/MorphicExecutive.dll', "log-tag=$tag", 'log-directory=development/logical-roots/test-logs')
    if ($requested -ne 0) { $lines += "host-workers=$requested" }
    $batchRequested = if ($requested -eq 1) { 0 } elseif ($requested -eq 2) { 32 } else { 8 }
    if ($batchRequested -ne 8) { $lines += "batch-runners=$batchRequested" }
    $processId = Invoke-Host -Lines $lines -ExpectedExit 0 -WithBom:($requested -eq 0)
    $log = Join-Path $runtime "development/logical-roots/test-logs/morphic_debug.$tag.p$processId.log"
    $events = Get-Content -LiteralPath $log -Raw
    if ($events -match '\[(assert|error|critical|fatal):') { throw "Unexpected diagnostics: $log" }
    if (!$events.Contains('Asset acceptance: 71 sequential and 32 concurrent operations passed') -or
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
    $limit = if ($hardware -ge 8) { 2 } else { 1 }
    $expected = [Math]::Min($requestCount, $limit)
    if (($count -ne $expected) -or ($dedicated -ne ($count - 1))) { throw "Unexpected worker sizing: $log" }
    if (($count -lt $requestCount) -and !$events.Contains("Host: Worker count reduced from $requestCount to $count")) {
        throw "Missing worker reduction diagnostic: $log"
    }
    $batchLimit = [Math]::Min(32, [Math]::Max(0, [Math]::Min($hardware, 64) - 6 - $count))
    $batchExpected = [Math]::Min($batchRequested, $batchLimit)
    if (!$events.Contains("Host: Batch runners $batchExpected")) { throw "Unexpected batch runner sizing: $log" }
    $directLog = Join-Path $runtime "development/logical-roots/test-logs/morphic_debug_direct.$tag.p$processId.log"
    $directEvents = Get-Content -LiteralPath $directLog -Raw
    $textureRoute = if ($batchExpected -eq 0) { 'completed inline' } else { 'queued' }
    $textureThread = if ($batchExpected -eq 0) { 'rendering' } else { 'batch_runner_\d+' }
    if (!$events.Contains("Rendering: Texture batch $textureRoute") -or
        ($directEvents -notmatch "\[render_vulkan:$textureThread\].*Rendering Basis: encode begin") -or
        ($directEvents -notmatch "\[render_vulkan:$textureThread\].*Rendering Basis: transcode begin")) {
        throw "Texture work did not use the expected batch route and module context: $directLog"
    }
    if (($batchExpected -lt $batchRequested) -and
        !$events.Contains("Host: Batch runners reduced from $batchRequested to $batchExpected")) {
        throw "Missing batch runner reduction diagnostic: $log"
    }
    $identity = '\[executable:(bg_file_io|bg_conditioning)\]'
    $starts = [regex]::Matches($events, "$identity.*Worker starting")
    $exits = [regex]::Matches($events, "$identity.*Worker exited")
    if (($starts.Count -ne $count) -or ($exits.Count -ne $count) -or
        @($starts | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique).Count -ne $count) {
        throw "Workers did not start and exit exactly once: $log"
    }
    if ($Configuration -eq 'Debug') {
        $conditioning = [regex]::Matches($events, "$identity.*Worker (document conditioning|TGA encode|TGA decode) request")
        $used = @($conditioning | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        $expectedConditioning = if ($count -eq 1) { 'bg_file_io' } else { 'bg_conditioning' }
        if (($used.Count -ne 1) -or ($used[0] -ne $expectedConditioning)) {
            throw "Conditioning did not exclusively use $expectedConditioning`: $log"
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
