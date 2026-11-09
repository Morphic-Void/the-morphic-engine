# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

param([string] $RepositoryRoot = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
$version = '4.0.306'
$expectedHash = 'B7FACB296FD0EAED20B3D60DCA5345A5FCFA674398B34D6F0DBC2793D502E3C1'
$destination = Join-Path $RepositoryRoot "build/dependencies/gameinput/$version"
$package = Join-Path $destination 'package'
$archive = Join-Path $destination 'package.zip'
$marker = Join-Path $destination 'verified.txt'
New-Item -ItemType Directory -Force -Path $destination | Out-Null

# Projects may restore concurrently. A named mutex coordinates only this pinned
# package; extraction stays in ignored dependency storage.
$mutex = [Threading.Mutex]::new($false, 'Local\MorphicGameInput40306')
$locked = $false
try {
    try { $locked = $mutex.WaitOne(120000) }
    catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Timed out waiting for GameInput package restore.' }
    $requiredFiles = @('native/include/GameInput.h', 'native/include/v4/GameInput.h',
        'native/lib/x86/GameInput.lib', 'native/lib/x64/GameInput.lib',
        'native/bin/x86/GameInputRedist.dll', 'native/bin/x86/GameInputRawInputProxy.exe',
        'native/bin/x64/GameInputRedist.dll', 'native/bin/x64/GameInputRawInputProxy.exe', 'LICENSE.txt', 'NOTICE.txt')
    $complete = $true
    foreach ($relative in $requiredFiles) { $complete = $complete -and (Test-Path -LiteralPath (Join-Path $package $relative)) }
    if ($complete -and (Test-Path -LiteralPath $marker) -and
        ((Get-Content -LiteralPath $marker -Raw).Trim() -eq $expectedHash)) { return }
    if (!(Test-Path -LiteralPath $archive)) {
        $download = Join-Path $destination 'package.download'
        Invoke-WebRequest -UseBasicParsing -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.gameinput/$version/microsoft.gameinput.$version.nupkg" -OutFile $download
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'GameInput download hash mismatch. The download was left for inspection.'
        }
        Move-Item -LiteralPath $download -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'GameInput package hash mismatch. The archive was left for inspection.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $package -Force
    [IO.File]::WriteAllText($marker, ($expectedHash + "`n"), [Text.UTF8Encoding]::new($false))
}
finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
