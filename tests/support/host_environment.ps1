# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# Stage an isolated development filesystem for Host process acceptance tests.

function New-HostTestEnvironment([string] $Repository, [string] $Name) {
    $runtime = Join-Path $Repository "build/$Name"
    $development = Join-Path $runtime 'development'
    $null = New-Item -ItemType Directory -Path $development
    $manifest = Join-Path $Repository 'development/root-manifest.json'
    Copy-Item -LiteralPath $manifest -Destination (Join-Path $development 'root-manifest.json')
    $roots = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).roots
    foreach ($root in $roots.PSObject.Properties) {
        $null = New-Item -ItemType Directory -Path (Join-Path $development $root.Value.source) -Force
    }
    Copy-Item -LiteralPath (Join-Path $Repository 'development/logical-roots/dev-source/test_input.tga') -Destination (Join-Path $development 'logical-roots/dev-source/test_input.tga')
    return $runtime
}
