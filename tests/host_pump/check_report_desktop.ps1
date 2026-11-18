# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

# Compare a reconstructed report with the current, unchanged interactive desktop.
param([Parameter(Mandatory)] [string] $ReportJson)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$report = Get-Content -LiteralPath $ReportJson -Raw | ConvertFrom-Json
$sources = @($report.data.PlatformDisplaySource.sources.data | Where-Object desktop_attached -eq 'yes')
$identities = $report.data.PlatformNativeIdentity.native_identities.data
$screens = [System.Windows.Forms.Screen]::AllScreens
if ($sources.Count -ne $screens.Count) { throw "Attached source count differs from desktop screen count." }
foreach ($screen in $screens) {
    $matchedSources = @(foreach ($source in $sources) {
        $span = $source.identities
        for ($i = $span.first; $i -lt ($span.first + $span.count); ++$i) {
            $id = $identities[$i]
            if ($id.kind -eq 'windows_gdi_source_name' -and $id.text -ieq $screen.DeviceName) {
                $source
                break
            }
        }
    })
    if ($matchedSources.Count -ne 1) { throw "Source identity mismatch for $($screen.DeviceName)." }
    $row = $matchedSources[0]
    if (($row.primary -eq 'yes') -ne $screen.Primary) { throw "Primary source mismatch for $($screen.DeviceName)." }
    $r = $row.gdi_current.rectangle
    $b = $screen.Bounds
    if (!$row.gdi_current.known_fields.rectangle -or $r.x -ne $b.X -or $r.y -ne $b.Y -or
        $r.width -ne $b.Width -or $r.height -ne $b.Height) {
        throw "Current GDI desktop rectangle mismatch for $($screen.DeviceName)."
    }
    # CCD supplies its source-mode extent separately from the oriented GDI desktop.
    $ccd = $row.desktop.rectangle
    $rotations = @($report.data.PlatformDisplayPath.paths.data | Where-Object source_id -eq $row.id |
        ForEach-Object { $_.signal.rotation.value })
    Write-Output "$($screen.DeviceName) $($screen.Bounds) primary=$($screen.Primary) GDI refresh=$($row.gdi_current.refresh.numerator)/$($row.gdi_current.refresh.denominator) CCD source=$($ccd.width)x$($ccd.height) path rotation=$($rotations -join ',')"
}
Get-CimInstance Win32_VideoController | Select-Object Name, CurrentRefreshRate
