# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# File:    validate_schema_layout.ps1
# Authors: Ritchie Brannan / OpenAI Codex
# Date:    26 Sep 26
#
# Compile the separate assertion TU emitted by Schema_test_suite, for both ABIs.
param(
    [Parameter(Mandatory = $true)]
    [string] $Source,
    [ValidateSet('x86', 'x64')]
    [string[]] $Platforms = @('x86', 'x64')
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$compilerRoot = Get-ChildItem -Path "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*" -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$compilerRoot) {
    throw 'MSVC was not found.'
}
$sdkInclude = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\*" -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt') } |
    Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$sdkInclude) {
    throw 'Windows SDK headers were not found.'
}
$outputRoot = Join-Path $repositoryRoot 'build\schema-validation'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
foreach ($platform in $Platforms) {
    $compiler = Join-Path $compilerRoot "bin\Hostx64\$platform\cl.exe"
    $object = Join-Path $outputRoot (([IO.Path]::GetFileNameWithoutExtension($sourcePath)) + ".$platform.obj")
    # Match the engine projects: C++ exceptions are disabled; STL warning C4530
    # is suppressed there when headers are compiled without exception handling.
    $arguments = @('/nologo', '/c', '/std:c++17', '/permissive-', '/wd4530', '/W4', '/WX',
        "/I$repositoryRoot\core", "/I$compilerRoot\include", "/I$sdkInclude\ucrt", "/Fo$object", $sourcePath)
    Write-Host "Schema C++17 layout validation: $platform"
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Generated schema layout is unsupported or failed validation on $platform (compiler exit $LASTEXITCODE)."
    }
}
