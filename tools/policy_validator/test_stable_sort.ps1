
# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# File:   test_stable_sort.ps1
# Author: OpenAI Codex
# Date:   31 Aug 26

param(
    [string] $ValidatorPath = (Join-Path $PSScriptRoot '../../build/bin/x64/Debug/MorphicPolicyValidator.exe')
)

$ErrorActionPreference = 'Stop'
$validator = (Resolve-Path -LiteralPath $ValidatorPath).Path
$temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureName = 'morphic-policy-stable-sort-' + [Guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path $temporaryParent $fixtureName
$encoding = [Text.UTF8Encoding]::new($false)

try {
    [void][IO.Directory]::CreateDirectory((Join-Path $fixtureRoot 'policy'))
    [void][IO.Directory]::CreateDirectory((Join-Path $fixtureRoot 'core'))
    [void][IO.Directory]::CreateDirectory((Join-Path $fixtureRoot 'tools'))
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'MorphicEngine.sln'), '', $encoding)
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'policy/morphic_policy.cfg'), @'
version|1
scan|core|engine
include|algorithm|all|any|*|engine-wide
project|Fixture.vcxproj|engine
'@, $encoding)
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'Fixture.vcxproj'), @'
<Project>
  <ItemGroup><ProjectConfiguration Include="Debug|x64" /></ItemGroup>
  <ItemDefinitionGroup><ClCompile>
    <LanguageStandard>stdcpp17</LanguageStandard>
    <ExceptionHandling>false</ExceptionHandling>
  </ClCompile></ItemDefinitionGroup>
</Project>
'@, $encoding)
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'tools/exempt.cpp'), 'std::stable_sort(first, last);', $encoding)

    # These are lexical fixtures, not compilable translation units.
    $cases = @(
        @{ Name = 'qualified'; Source = 'std::stable_sort(first, last);'; Exit = 1; Match = 'fixture.cpp\(1,6\): error LIB001' },
        @{ Name = 'unqualified'; Source = 'stable_sort(first, last);'; Exit = 1; Match = 'error LIB001' },
        @{ Name = 'using'; Source = 'using std::stable_sort;'; Exit = 1; Match = 'error LIB001' },
        @{ Name = 'function-reference'; Source = 'auto fn = &std::stable_sort<int*>;'; Exit = 1; Match = 'error LIB001' },
        @{ Name = 'split-lines'; Source = "std ::`n /* comment */ stable_sort(first, last);"; Exit = 1; Match = 'error LIB001' },
        @{ Name = 'macro'; Source = "`n  #define SORT std::stable_sort"; Exit = 1; Match = 'fixture.cpp\(2,21\): error LIB001' },
        @{ Name = 'continued-macro'; Source = "#define SORT \`n std::stable_sort"; Exit = 1; Match = 'fixture.cpp\(2,7\): error LIB001' },
        @{ Name = 'inactive'; Source = "#if 0`nstd::stable_sort(first, last);`n#endif"; Exit = 1; Match = 'error LIB001' },
        @{ Name = 'same-name'; Source = 'void stable_sort();'; Exit = 1; Match = 'error LIB001' },
        @{ Name = 'allowed'; Source = @'
#include <algorithm>
// stable_sort
/* std::stable_sort */
const char* text = "stable_sort";
const char* raw = R"tag(std::stable_sort)tag";
const char* wide = L"stable_sort";
int character = 'stable_sort';
#define TEXT "stable_sort"
#define RAW R"tag(stable_sort)tag"
#define OTHER 1 /* stable_sort */
#define MORE 2 // stable_sort
int stable_sort_count;
void my_stable_sort();
std::sort(first, last);
'@; Exit = 0; Match = '0 error\(s\), 0 warning\(s\)' },
        @{ Name = 'suppressed'; Source = @'
// morphic-policy: suppress-next-line LIB001 reason="fixture verifies exceptional local classification"
void stable_sort();
'@; Exit = 0; Match = 'suppressed LIB001' },
        @{ Name = 'stale-suppression'; Source = @'
// morphic-policy: suppress-next-line LIB001 reason="fixture verifies stale decoration"
void ordinary_sort();
'@; Exit = 1; Match = 'error SUP001' }
    )

    foreach ($case in $cases) {
        [IO.File]::WriteAllText((Join-Path $fixtureRoot 'core/fixture.cpp'), $case.Source, $encoding)
        $output = & $validator --root $fixtureRoot --no-report 2>&1
        $exitCode = $LASTEXITCODE
        $text = $output -join "`n"
        if ($exitCode -ne $case.Exit -or $text -notmatch $case.Match) {
            throw "Case '$($case.Name)' failed (exit $exitCode):`n$text"
        }
        Write-Host "PASS $($case.Name)"
    }
    Write-Host "All $($cases.Count) stable_sort policy cases passed."
}
finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $expectedFixture = [IO.Path]::GetFullPath((Join-Path $temporaryParent $fixtureName))
    if ($resolvedFixture -ne $expectedFixture -or
        (Split-Path -Leaf $resolvedFixture) -ne $fixtureName) {
        throw 'Refusing cleanup outside the generated fixture directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixture) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
