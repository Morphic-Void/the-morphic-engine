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

function New-BundleTestSources([string] $Repository, [string] $Runtime) {
    $source = Join-Path $Runtime 'development/logical-roots/dev-source/BundleInput'
    foreach ($directory in @($source, "$source/Empty", "$source/MiXeD", "$source/../BundleMissing")) {
        $null = New-Item -ItemType Directory -Path $directory -Force
    }
    foreach ($item in @(@('Before', 127), @('Aligned', 128), @('After', 129))) {
        $bytes = [byte[]]::new($item[1])
        [Array]::Fill[byte]($bytes, 0x5a)
        [IO.File]::WriteAllBytes((Join-Path $source $item[0]), $bytes)
    }
    foreach ($name in @('offset', 'content', 'SIZE', 'MiXeD/Child', '../BundleMissing/Vanished')) {
        [IO.File]::WriteAllText((Join-Path $source $name), 'fixture', [Text.UTF8Encoding]::new($false))
    }
    [IO.File]::WriteAllText((Join-Path $source 'Data.json'), '{"answer":42}', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllBytes((Join-Path $source 'Zero'), [byte[]]::new(0))
    Copy-Item -LiteralPath (Join-Path $Repository 'tests/shader/fixtures/poc/hlsl/shaders/simple.hlsl') -Destination (Join-Path $source 'Shader.hlsl')
    Copy-Item -LiteralPath (Join-Path $Repository 'development/logical-roots/dev-source/test_input.tga') -Destination (Join-Path $source 'Image.tga')
    #  A junction requires no symbolic-link privilege and is inventoried as an unsupported reparse entry.
    $null = New-Item -ItemType Junction -Path (Join-Path $source 'Unsupported') -Target (Join-Path $Runtime 'development/logical-roots/dev-workspace')
}
