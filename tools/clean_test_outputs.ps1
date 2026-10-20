#requires -Version 7.0
# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$development = Join-Path $repository 'development'
$logicalRoots = Join-Path $development 'logical-roots'
$targets = @(
    [IO.Path]::GetFullPath((Join-Path $logicalRoots 'test-logs')),
    [IO.Path]::GetFullPath((Join-Path $logicalRoots 'test-output'))
)

# Validate both destinations before deleting anything. Reject redirected parent
# directories so these fixed paths cannot lead outside this checkout.
foreach ($path in @($repository, $development, $logicalRoots) + $targets) {
    $directory = Get-Item -LiteralPath $path -Force
    if (!$directory.PSIsContainer -or
        (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "Expected an ordinary directory: $path"
    }
}
foreach ($target in $targets) {
    if (!$target.StartsWith($repository + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target is outside the checkout: $target"
    }
    $keep = Get-Item -LiteralPath (Join-Path $target '.gitkeep') -Force
    if ($keep.PSIsContainer -or
        (($keep.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "Expected an ordinary .gitkeep file in: $target"
    }
}

foreach ($target in $targets) {
    if ($PSCmdlet.ShouldProcess($target, 'Remove all contents except the root .gitkeep file')) {
        # PowerShell 7 removes nested links themselves without following them.
        foreach ($entry in Get-ChildItem -LiteralPath $target -Force) {
            if ($entry.Name -ne '.gitkeep') {
                Remove-Item -LiteralPath $entry.FullName -Recurse -Force -ErrorAction Stop
            }
        }
        Write-Output "Cleaned: $target"
    }
}
