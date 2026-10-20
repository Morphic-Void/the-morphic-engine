# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# Match deliberately exercised failures by source, text and scenario interval.
# Incident IDs order the combined event/direct logs; file order cannot do so.

function Assert-HostDiagnostics([string] $Events, [string] $Runtime) {
    $expected = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'host_expected_diagnostics.json') -Raw | ConvertFrom-Json
    foreach ($entry in $expected) {
        $entry.message = $entry.message.Replace('{runtime}', $Runtime.Replace('\', '/'))
    }
    $passes = @([regex]::Matches($Events, '(?m)^\[(\d+)\].*\[executive_thread\.cpp:\d+\] Executive: (\w+) passed\r?$') |
        ForEach-Object { [pscustomobject]@{ Id = [long] $_.Groups[1].Value; Scenario = $_.Groups[2].Value } } |
        Sort-Object Id)
    $seen = @{}
    foreach ($line in ($Events -split '\r?\n')) {
        if ($line -notmatch '\[(assert|error|critical|fatal):') { continue }
        if ($line -notmatch '^\[(\d+)\] \[[^\]]+\] \[error:event\] \[([^:\]]+):\d+\] (.*)$') {
            throw "Unexpected diagnostic: $line"
        }
        $incident = [long] $Matches[1]
        $source = $Matches[2]
        $message = $Matches[3]
        $nextPass = $passes | Where-Object Id -gt $incident | Select-Object -First 1
        $entry = @($expected | Where-Object {
            ($_.scenario -ceq $nextPass.Scenario) -and ($_.source -ceq $source) -and ($_.message -ceq $message)
        })
        if (($entry.Count -ne 1) -or $seen.ContainsKey($nextPass.Scenario)) {
            throw "Unexpected or duplicate diagnostic: $line"
        }
        $seen[$nextPass.Scenario] = $true
    }
    foreach ($entry in $expected) {
        if (!$seen.ContainsKey($entry.scenario)) { throw "Missing expected diagnostic for $($entry.scenario)" }
    }
}
