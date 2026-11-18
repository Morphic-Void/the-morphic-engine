Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    reference-extension-validation.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    17 Nov 26

# Text and span prerequisite implementation record

Implementation and applicable qualification are complete. The focused stage-1
design coordinator accepted the schema prerequisite on 18 November 2026; see the
[acceptance record](../modules/pso_creation_stage1.md#schema-prerequisite-implementation-review-and-acceptance).
This records the original implementation and traversal qualification; the later
[consolidation record](consolidation-validation.md) holds final qualification and
the resolved coordinator finding. Acceptance does not accept Platform reports or
authorise Rendering work.
See the [approved contract and gates](../modules/pso_creation_stage1.md#agreed-validation-and-acceptance-gates-for-prerequisites)
and [delivered public contract](text-and-spans.md).

## Delivered surface

- Inline text/span declarations, defaults, nullability, fixed-array containment
  and atomic selections; four-byte text and twelve-byte span descriptors, both
  aligned to four bytes, with unsigned 32-bit span count.
- Atomic optional generic-bake string map, effective text retention, current
  collection directory, same-block typed reference validation and cycle rejection.
- Checked complete-block readers and counted UTF-8 extraction; independent
  subset/dependency copy and repack; logical inspection/export and reconstruction.
  Copies preserve padding, selections and raw non-reference encodings. Literal
  export rejects encodings without faithful authoring representations.
- Private edits with stable handles, explicit commands/commit/cancel, rollback,
  coordinated target replacement and both descendant policies.
- Shared finite source/storage/result/node/record/work/depth allowances, bounded
  admission/sorting, durable diagnostics and failure-atomic publication.
- Explicit rejection by unbound capture/writable/remap/role-materialisation APIs
  and prepared-schema role extraction for reference-bearing layouts. Existing
  reference-free consumers retain their boundary and are rebuilt and tested.
- A Foundation-only `MorphicSchemaFixture`, with no Platform native types,
  Host/module execution, window, GPU or Vulkan runtime.

Public headers are `data_block_view.hpp`, `data_copy.hpp`, `data_block_edit.hpp`,
`data_operation.hpp` and `reference_types.hpp` under `foundation/schema/`.
Visual Studio shared items and Linux CMake source extraction include the new
implementation/tests. The optional fixture project is built directly; the
already modified solution was not edited to add it.

## Current format and maintained artifacts

The schema block marker is **1**, with mandatory `retainedText` and the current
typed collection directory. There is no version-2 reader or transitional writer.
Negative tests construct unsupported markers and malformed historical layouts
using marker 1; a matching marker does not establish valid current structure.
Generic baked-document format **5** remains unchanged.

The canonical `schema-example.json` retains its earlier information and adds
Unicode/logical NUL, absent/empty text and typed vertex ranges. Current consumer
format references, documentation, generators and test expectations use the final
contract. No tracked `.bsd` files exist; none were found outside ignored build
and development output, excluding external dependencies.

Current replacements live beneath `build/schema-reference-qualification/`:

| Artifact | Contents |
| --- | --- |
| `portable-x64-debug/`, `portable-win32-debug/`, `portable-linux-debug/`, `portable-x64-release/` | Fresh 64/1024-mode `.bsd`, authored JSON, logical inspection JSON, generated C++ header and independent layout assertion TU. |
| `tests-debug64/`, `tests-debug32/`, `tests-release64/` | Isolated ordinary-suite outputs, including regenerated canonical schema binaries and layout assertions. |
| `solution-completion-*.log`, `tests-*-complete.log`, `linux-completion-*.log` | Final rebuild and regression evidence. |
| `fixture-*.csv`, `layout-msvc.log` | Standalone round trips, phase measurements and compiler checks. |

Fixture invocations overwrite their maintained report outputs with the rebuilt
current writer. Earlier diagnostic logs and unrelated historical test output are
not runtime inputs or maintained fixtures and were not broadly deleted. Negative
legacy-layout cases are constructed in memory, not retained as a legacy dataset.

Pre-existing edits to AGENTS.md, MorphicEngine.sln, general documentation, shader
backlogs/designs and project milestones are preserved. Narrow current-format
corrections were necessary in some already edited shader documents/backlogs and
milestones. No commit, push, branch or worktree was created.

## Regression evidence

The focused reference suite passed **1,898 checks** (`final-audit-focused.log`).
Those cases also run inside the final ordinary schema suite. They cover strict
grammar/defaults/array markers; exact Unicode and logical NUL; distinct live/baked
and cross-source IDs; explicit conflict renames; whole shared/overlapping/nested
dependencies; malformed descriptors/targets; targeted-zero validation; raw
independently assembled span bytes and zero-byte 2^31-count boundaries; typed
edits, replacement/repair, erase/recreate, stale/foreign handles, both descendant
policies and explicit cancellation/publication; padding/noncanonical scalar
fidelity; reference-free boundaries and present-empty section preservation.

Small configured allowances exercise all seven limits, including eight-record
success/seven-record rejection, non-resetting edit work and diagnostic lifetime.
Allocation-failure sweeps cover repack, export, bake, rename and commit, checking
empty publication, source/candidate preservation, retry where budget permits and
balanced attribution.

| Configuration | Final result |
| --- | --- |
| Windows x64 Debug | Ordinary `-t1` passed: SchemaModel 1,172, Schema 82,769, BakedDocumentTransfer 628, ShaderReflection 5,870, ShaderConditioning 1,649, plus existing document, ownership, Host, service and container suites. |
| Linux GCC Debug | All consumers rebuilt; ordinary CTest passed in 57.68 seconds. |
| Windows Win32 Debug | Solution rebuild and ordinary `-t1` passed; SchemaModel 1,172 and Schema 82,769 checks. |
| Windows x64 Release | Solution rebuild and ordinary `-t1` passed; SchemaModel 1,172 and Schema 82,769 checks. |

Regressions exposed parser singleton-marker handling and recursive stack growth.
The Windows ordinary depth-256 test caught an overflow absent from the shallower
isolated runner. Moving leaf temporaries into helpers initially reduced MSVC
Debug's recursive declaration frame from 0xF40 to 0xD10 bytes, passing on the
unchanged default stack. That headroom mitigation is superseded by the iterative
traversal follow-up below. Temporary tracing was removed. A stale SchemaModel
version-2 assertion was corrected to current version 1.

Source-policy validation passed with zero errors/warnings and one existing
negative-test identity suppression. CMake extraction tests passed nine cases;
`MorphicPolicyIncludes` passed. Tracked line endings and new-file LF/final-newline/
leading-blank conventions passed; VS metadata CRLF/BOM state was preserved.
The scoped whitespace check passed.

## Independent reader and layout checks

MSVC x86/x64 and GCC 13.3 independently compile generated 64/1024-mode headers and
assertion TUs: text 4 bytes; span 12 bytes/alignment 4/u32 count; Mode 20, Display 16,
Adapter 16 and Report 20 bytes, with the adapter span at offset 8. Canonical sample
layout assertions also passed MSVC x86/x64.

Win32 artifacts are read by Windows x64 and Linux; Linux artifacts are read by
Win32 and Windows x64 Debug/Release. Inspection JSON matches byte-for-byte across
writers/readers for both sizes. Round trips dispose the original schema/block/view
before inspecting the independent copy, then export, import, rebake and inspect
again. Engine allocation attribution is empty on exit.

This is portable schema evidence. Native display capture, Platform report
publication, Host report acquisitions and Rendering were not exercised.

## Reproduction

Windows environment: Windows 11 Pro for Workstations 10.0.26200 x64, AMD Ryzen
Threadripper 7980X (64 cores/128 logical processors), Visual Studio Community 18,
MSVC v143, SDK 10.0.26100.0. Linux: Ubuntu 24.04 under WSL2, GCC 13.3.0,
kernel 6.6.87.2-microsoft-standard-WSL2.

Run the installed MSBuild against `MorphicEngine.sln` with `/m:4`,
`/p:TrackFileAccess=true`, `/p:PreferredToolArchitecture=x64`,
`/p:_LatestWindowsTargetPlatformVersion=10.0.26100.0`,
`/p:TargetPlatformSdkPath="C:\Program Files (x86)\Windows Kits\10\"`,
`/p:TargetPlatformDisplayName="Windows SDK 10.0.26100.0"` and `/v:minimal`.
Configurations/platforms are Debug/x64, Debug/x86 (solution mapping to Win32),
and Release/x64. The fixture project is `visual_studio/MorphicSchemaFixture.vcxproj`
with `/p:SolutionDir=D:\TheMorphicEngine\` and project platforms Win32/x64;
exact completion compile/link arguments are retained in `fixture-completion-*.rsp`.

```powershell
.\build\bin\x64\Debug\MorphicTests.exe -t1 --log-tag=schema-reference-complete-dbg64 --output-directory=D:\TheMorphicEngine\build\schema-reference-qualification\tests-debug64
.\build\bin\Win32\Debug\MorphicTests.exe -t1 --log-tag=schema-reference-complete-dbg32 --output-directory=D:\TheMorphicEngine\build\schema-reference-qualification\tests-debug32
.\build\bin\x64\Release\MorphicTests.exe -t1 --log-tag=schema-reference-complete-rel64 --output-directory=D:\TheMorphicEngine\build\schema-reference-qualification\tests-release64
.\build\bin\x64\Debug\MorphicTests.exe --schema-references --log-tag=schema-reference-focused
.\build\bin\x64\Debug\MorphicTests.exe --schema --log-tag=schema-regression
.\build\bin\x64\Release\MorphicSchemaFixture.exe build/schema-reference-qualification/portable-x64-release 64
.\build\bin\x64\Release\MorphicSchemaFixture.exe build/schema-reference-qualification/portable-x64-release 1024
.\build\bin\Win32\Debug\MorphicSchemaFixture.exe read build/schema-reference-qualification/portable-linux-debug/report-64.bsd build/schema-reference-qualification/portable-linux-debug/read-win32-64.json
.\tools\validate_schema_layout.ps1 -Source build/schema-reference-qualification/portable-win32-debug/report-64.layout.cpp
.\tools\check_line_endings.ps1
```

Linux, from `/mnt/d/TheMorphicEngine`:

```sh
cmake --build build/linux-identity -j8
ctest --test-dir build/linux-identity -R MorphicOrdinary --output-on-failure
ctest --test-dir build/linux-identity -R MorphicPolicyIncludes --output-on-failure
python3 -B tools/tests/test_cmake_sources.py
build/linux-identity/bin/MorphicSchemaFixture build/schema-reference-qualification/portable-linux-debug
build/linux-identity/bin/MorphicSchemaFixture read build/schema-reference-qualification/portable-win32-debug/report-64.bsd build/schema-reference-qualification/portable-win32-debug/read-linux-64.json
g++ -std=c++17 -I foundation -I . -c build/schema-reference-qualification/portable-linux-debug/report-64.layout.cpp -o build/schema-reference-qualification/portable-linux-debug/report-64.layout.o
```

Repeat reader/layout commands for the corresponding `report-1024` paths.

## Iterative traversal follow-up

The subsequent stack-risk fix removes depth-dependent native recursion from
declaration-value validation and type resolution, including named physical
dependencies, association/interface dependencies and nested array defaults.
Explicit continuation vectors use the engine allocator; existing logical depth
limits, dependency visitation/cycle detection, ordering and diagnostic locations
remain enforced. Allocation failure rejects the private candidate without
publishing a partially resolved owner. No public API, schema layout or format
marker changed.

Compile-time caps on continuation records are 80 bytes for declaration validation,
160 bytes for physical resolution, 128 bytes for non-storage declarations and
64 bytes for defaults. These records occupy allocator-owned storage, with bounded
capacity growing with depth; native traversal frames no longer accumulate per
level. Resolver buffers are reused during one resolution and released afterwards.
Validation and default traversal allocate continuation storage only when entering
a child that itself has children. This targets the identified validation/resolver
call chains; other schema operations retain recursive walkers and are not covered
by a general claim of constant stack use.

`SchemaTraversal_test_cases.cpp`, included in the ordinary Schema suite and
available as `--schema-traversal`, covers live/baked forward dependency chains,
all five dependency kinds' cycles, accepted/rejected depth boundaries, 180-level
array defaults, ordered sibling resumption, diagnostic precedence, explicit
operation depth limits and successive workspace-allocation failures. Sweeps check
failure diagnostics, unpublished results, unchanged sources and balanced memory
attribution through a successful uninjected run. The three resolver cases
exercise 31, 19 and 15 failing allocation positions.

| Configuration | Follow-up result |
| --- | --- |
| Windows x64 Debug, Win32 Debug, x64 Release | Engine/consumers rebuilt; ordinary `-t1` passed, including SchemaModel 1,172 and Schema 85,056 checks. |
| Linux GCC Debug | All targets rebuilt; ordinary CTest passed in 59.96 seconds; include-policy check and nine CMake source-extraction tests passed. |
| All four configurations | Focused traversal runner passed 2,195 checks with a **128 KiB native stack**. |

Windows evidence uses a copy of each test executable with PE stack reserve
131,072 bytes and commit 4,096 bytes, verified with `dumpbin /headers`; the normal
executable/linker settings are unchanged. Linux uses `ulimit -s 128`. This proves
the exercised boundaries on those stacks, not a universal minimum stack size.
From a Visual Studio developer shell, for each configuration's executable:

```powershell
Copy-Item .\build\bin\x64\Debug\MorphicTests.exe .\build\bin\x64\Debug\MorphicTests-stack128.exe
editbin /STACK:131072,4096 .\build\bin\x64\Debug\MorphicTests-stack128.exe
dumpbin /headers .\build\bin\x64\Debug\MorphicTests-stack128.exe
.\build\bin\x64\Debug\MorphicTests-stack128.exe --schema-traversal
```

```sh
(ulimit -s 128 && build/linux-identity/bin/MorphicTests --schema-traversal)
```

Evidence is under `build/schema-stack-qualification/`: `build-*.log`,
`tests-final-*.log`, `stack-header-*.log`, `stack128-final-*.log`,
`linux-final2-tests.log`, `cmake-source-tests.log` and fixture outputs.
Fresh 64/1024-mode fixture round trips pass on all four configurations; logical
inspection output matches the pre-rewrite fixtures byte-for-byte. Source policy,
line endings and scoped whitespace checks also pass. No commit or push was made.

## Timing and placement

The measurements in this section predate the iterative traversal follow-up and
are retained as the prerequisite's placement evidence, not fresh measurements of
the revised traversals.

Final Release measurements were collected after compilation finished (the Win32
ordinary regression was still running). The fixture records parse/adopt,
resolve, bake, typed admission/inspection, dependency copy, source retirement,
export, import/bake/read and final retirement separately. Each size starts in a
fresh process with three iterations; iteration zero is process-cold, not an
OS-cache flush. Peak engine-requested bytes include engine owners/scratch but
exclude standard-library and OS allocations.

Reports contain 64 or 1024 complete modes, two displays, two adapters, shared and
overlapping ranges, rational refresh values and Unicode/NUL text. These are
schema cost fixtures, not the production Platform report schema.

Microseconds, from `fixture-release64-normal-final.csv` and
`fixture-release64-large-final.csv`; warm columns show the two observed iterations:

| Phase | 64 cold | 64 warm range | 1024 cold | 1024 warm range |
| --- | ---: | ---: | ---: | ---: |
| Generate input, parse and adopt | 990 | 865–892 | 11,842 | 11,834–12,107 |
| Resolve | 111 | 79 | 120 | 91–92 |
| Bake and validate | 1,552 | 1,454–1,484 | 17,278 | 17,117–17,605 |
| Typed admission and inspection | 113 | 107–110 | 726 | 733–748 |
| Complete dependency repack | 1,058 | 974–1,138 | 8,105 | 8,003–8,025 |
| Source retirement | 17 | 18–20 | 71 | 59–69 |
| Logical export | 1,094 | 1,065–1,073 | 15,468 | 15,301–15,398 |
| Import, bake and inspect | 1,931 | 1,891–1,933 | 23,156 | 22,731–22,928 |
| Final retirement | 27 | 16–17 | 97 | 88–97 |

Runtime blocks are 8,640 and 74,816 bytes. Peak engine-requested storage is
221,096 bytes for the normal case; the large case is 1,413,232 process-cold and
1,375,400 warm. Debug timing CSVs are retained for diagnostic comparison and also
exceed the pump allowance; Release is the placement baseline. These are three
observations per size on this machine, not a statistical latency bound.

Finite synchronous limits are not latency guarantees. The default Host pump
allowance is 2 ms (`host_pump.hpp`, `time_budget_ns`). Total construction or a
large copy exceeding that allowance requires a concrete placement/timing decision
in Platform-report work. Normal parse/adopt + resolve + bake takes 2.425–2.653 ms;
normal bake and repack individually fit the allowance in these samples, while
the large bake/repack phases exceed it substantially. Unqualified placement of
the whole construction sequence or the large phases on that pump is therefore
not accepted by this evidence. Review the concrete stages or explicitly accept
the measured tradeoff at the Platform gate; this does not require a general
offloading/resumable-schema subsystem. No native-call or actual owner-pump stall
measurement is claimed by this standalone fixture.
