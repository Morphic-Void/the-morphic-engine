Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    consolidation-validation.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    17 Nov 26

# Schema consolidation validation

Stages 1–6 are implemented and reviewed. Independent coordinator acceptance and
initial commit clearance were completed on 18 November 2026 after the diagnostic
follow-up recorded below. See the [coordinator acceptance](../modules/pso_creation_stage1.md#schema-prerequisite-implementation-review-and-acceptance).
The completed implementation plan is retired. This record preserves its delivery
and review evidence. Current contracts are maintained in the [runtime API](runtime-api.md),
[text/span contract](text-and-spans.md) and [validation guide](validation.md).

The consolidation retains both generic bake overloads, including no-remap baking,
and the direct reference-free APIs and their bounded/unbounded behaviour. The
schema marker remains 1 and the generic baked-document marker remains 5; this
work changes no stored layout. Historical generated evidence under `build/` is
not a maintained legacy runtime format.

## Final qualification

This matrix qualifies the stage 6 checkout before the coordinator follow-up below.
The final audit found no superseded planner, view or operation implementation
remaining in scope. Role adapters and their indexes, reference-free validation,
source-accounting options and rebinding checks still serve distinct contracts.
The only production cleanup in stage 6 replaces unused `<cstring>` with the direct
`<utility>` dependency in `data_block_view.cpp`. No functionality or format was
removed. The extracted headers compile independently; manifests and header maps
agree with the implemented boundaries.

The shared checkout is based on `2e2a8b4922914c0897cfb0380c960ece4f550f38`, with
the existing uncommitted schema work and unrelated user changes preserved.
`build/schema-consolidation-stage6/checkout-before.txt`, `base-commit.txt`,
`source-hashes.csv` and `final-source-hashes.csv` identify the tested checkout.
The hash manifests cover 559 source, test, tool and build/policy files; only the
view implementation's include list changes between those two manifests.

| Configuration | Completion result |
| --- | --- |
| Windows x64 Debug | Solution and standalone fixture rebuilt; ordinary `-t1` passed, including BakedDocumentTransfer 629/0, SchemaModel 1,172/0 and Schema 87,146/0. |
| Windows Win32 Debug | Solution and standalone fixture rebuilt; ordinary `-t1` passed with the same three suite counts. |
| Windows x64 Release | Solution and standalone fixture rebuilt; ordinary `-t1` passed with the same three suite counts. |
| Linux GCC Debug | All CMake targets rebuilt; ordinary and include-policy CTests passed. The ordinary run has the same three suite counts. |
| All four configurations | Focused traversal passed 2,195 checks on a 128 KiB native stack. Windows copies have verified 131,072-byte reserve / 4,096-byte commit; production linker/executable stack settings are unchanged. |
| Portable fixtures | Three iterations for each 64/1024-record report per configuration passed round trips and cleanup. Binaries, headers, assertion sources and inspection JSON match stage 5 and each other byte-for-byte. Cross-platform readers reproduce the same inspection JSON. |
| Header/layout checks | 23 isolated headers passed GCC C++17 checks. Generated 64/1024-record layout assertions passed MSVC x86/x64 and GCC. |
| Policy and manifests | Final builds report no source-policy errors/warnings, with the existing identity negative-test suppression only. Nine CMake source-extraction tests and Linux include-policy CTest passed. |

The small-stack result covers the exercised iterative declaration/resolver paths,
including logical depth boundaries, cycle detection and allocation failures. Other
schema walkers retain recursion; it is not a universal constant-stack or minimum
stack-size claim. Public limits, allocator ownership and the direct reference-free
paths remain unchanged.

### Resource and timing observations

Windows x64 Debug/Release and Linux agree on these requested-storage measurements.
The before values are the recorded pre-stage-3 view measurements; the final values
include stage 4's explicit ownership wrapper:

| View-bind measure | 64 records, before → final | 1024 records, before → final |
| --- | --- | --- |
| Allocations | 30 → 26 | 38 → 34 |
| Retained requested bytes | 11,800 → 9,872 | 17,560 → 15,632 |
| Peak requested bytes | 15,952 → 14,096 | 21,712 → 19,856 |

Both fixture sizes contain one instance and three collections. These observations
demonstrate removal of duplicate retained records and unused role indexes in those
fixtures, not arbitrary collection-count scaling. Win32 has the same allocation
counts and final retained/peak bytes of 9,172/13,396 and 14,932/19,156 respectively.

The focused reference fixture still uses 85 allocations / 89,776 requested bytes
for validation versus baking's 88 / 93,920 on x64 and Linux. The measured difference
is the omitted independent output allocation plus two publication-validation
scratch allocations. Generic no-remap/remap tests pass, and the reference-free
allocation observations remain 89 / 471,008 for direct bake versus 174 / 973,984
through live owners, with 61,440-byte largest requests on those configurations.
Win32 records 85 / 85,908 for reference validation and 88 / 90,028 for baking.
Its reference-free figures remain 89 / 464,800 for direct bake and 174 / 938,400
through live owners, matching the earlier Win32 qualification.

Final x64/Linux whole-round-trip peak requested storage is 215,552 bytes for 64
records, 1,409,376 for the first 1024-record iteration and 1,369,856 thereafter.
The corresponding stage 1 values were 221,096, 1,413,232 and 1,375,400 bytes.
Win32 final peaks are 202,732, 1,376,296 and 1,270,636 bytes respectively.

Release timings below are microseconds, recorded after compilation finished while
the Win32 ordinary suite was still running. They are three observations, not
latency guarantees or a revised placement budget:

| Records | Bake, first / subsequent two | Typed admission, first / subsequent two | Copy, first / subsequent two |
| --- | --- | --- | --- |
| 64 | 1,554 / 1,522 / 1,489 | 107 / 104 / 105 | 1,065 / 1,034 / 1,015 |
| 1024 | 19,052 / 17,810 / 17,931 | 724 / 729 / 734 | 8,384 / 8,261 / 8,270 |

### Reproduction and evidence

Windows solution builds used the installed Visual Studio 18 MSBuild with
`/m:1 /p:PreferredToolArchitecture=x64 /p:TrackFileAccess=false /v:minimal`, and
configuration/platform pairs `Debug/x64`, `Debug/x86` (solution mapping to Win32)
and `Release/x64`. The initial multi-worker invocations made no progress beyond
startup and were stopped; the successful single-worker logs are the final evidence.
The standalone `visual_studio/MorphicSchemaFixture.vcxproj` uses project platforms
`x64`/`Win32` and `/p:SolutionDir=D:\TheMorphicEngine\`.

Each rebuilt `MorphicTests.exe` ran `-t1` with a unique log tag and output directory
under `build/schema-consolidation-stage6/tests-{debug64,debug32,release64}`. A copy
beside each executable, `MorphicTests-consolidation-stack128.exe`, was edited with
`editbin /STACK:131072,4096`, verified with `dumpbin /headers`, and run with
`--schema-traversal`. These copies, not the production executables, carry the
reduced stack setting.

Linux commands, from the repository root, were:

```sh
cmake --build build/linux-identity -j8
ctest --test-dir build/linux-identity -R '^(MorphicOrdinary|MorphicPolicyIncludes)$' --output-on-failure
(ulimit -s 128 && build/linux-identity/bin/MorphicTests --schema-traversal)
python3 -B tools/tests/test_cmake_sources.py
python3 -B build/schema-consolidation-stage6/header-checks.py
build/linux-identity/bin/MorphicSchemaFixture build/schema-consolidation-stage6/portable-linux-debug
```

Each Windows fixture executable used its corresponding `portable-*` output
directory. Windows x64 Debug reads Win32 output; Windows Win32 and x64 Release
read Linux output; Linux reads Windows Release output. Each reader runs
`MorphicSchemaFixture read <report-N.bsd> <inspection.json>` for N=64 and N=1024.
The generated assertion sources are compiled by `tools/validate_schema_layout.ps1`
for x86/x64 and by GCC with `-std=c++17 -I foundation -I . -fsyntax-only`.

Evidence is under `build/schema-consolidation-stage6/`: `build-*.log`,
`fixture-build-*.log`, `tests-*.log`, `linux-ordinary.log`, `stack-header-*.log`,
`stack128-*.log`, `header-checks.log`, `cmake-source-tests.log`, `layout-*.log`,
`fixture-*.csv`, `fixture-comparison.log` and `cross-read/*.json`. Local build
artifacts are evidence, not distributed runtime inputs. Changed-file whitespace,
required newlines and repository line endings passed, as did local documentation
link checks. The final audit confirms that no public headers, manifests, format
markers or generic bake overloads changed in stage 6.

The implementation plan's delivery/review sections are preserved verbatim below,
including the stage 4 follow-up and stage 5 review. Permanent contracts and header
maps remain in the runtime documentation; deferred allocator-lifetime/meter ideas
remain explicitly unselected in the follow-on notes. The plan is retired only
after this migration. The subsequent stage 6 review and coordinator clearance are
recorded below.

## Coordinator review follow-up — 18 Nov 26

The coordinator found that invalid limits could make `CDataBlockEdit::begin` and
`begin_empty` fail without an operation reason. The session had already been
allocated, but `CDataOperation::valid()` is a predicate and does not set a failure
diagnostic. The existing validity gate now records `invalid_input` before the
normal diagnose/cancel path. The analogous bounded `CSchema::bake` gate had the
same gap; it and the reference-validation gate now record the same reason before
using the existing diagnostic finalizer. View, copy/repack, export and text-copy
paths already report invalid limits explicitly and required no changes.

Checks remain in their original order. Earlier destination, owner-storage-limit
and allocation failures retain precedence; zero quotas remain valid restrictive
limits. Valid operations, both generic bake overloads and reference-free paths
are unchanged. No public API, stored layout, format marker or manifest changed.

The new regression independently invalidates each of the seven limit fields for
both session starts and bounded bake. A populated valid source/type fixture
ensures edit rejection occurs after the session allocation rather than at the
outer storage-size gate. Assertions cover durable failure reasons, non-readiness,
immediate allocator cleanup, ambient-context/meter restoration, unchanged source
bytes and usable valid retries on the same objects. Existing allocation-failure
sweeps continue to cover actual setup failures.

| Focused validation | Result |
| --- | --- |
| Linux GCC Debug before the production fix | New tests reproduced 21 diagnostic failures: seven fields across three APIs; 4,106 checks passed. |
| Windows x64 Debug affected test target | Rebuilt; `--schema-references` passed 4,127 checks, 0 failed. |
| Linux GCC Debug affected test target and dependencies | Rebuilt; `--schema-references` passed 4,127 checks, 0 failed. |
| Existing reference allocation probe | Unchanged on both platforms: validation 85 / 89,776 requested bytes; bake 88 / 93,920. |
| Policy and formatting | Both builds report 0 policy errors/warnings and the existing identity negative-test suppression only; scoped whitespace/newline and repository line-ending checks pass. |

Commands, baseline snapshots, build/regression logs and changed-source hashes are
under `build/schema-coordinator-review/`. `commands.txt` records the exact build
and focused-run commands. `linux-regression.log` is the intentional failing run;
`windows-references.log` and `linux-references.log` are the final passing runs.
`final-source-hashes.csv` covers the same 559 paths as the stage 6 manifest;
`changed-source-hashes.csv` identifies the two production files and one test file
changed by this follow-up. The passing broad matrix was not repeated for this
diagnostic correction. Independent review found no issue in the fix or tests.
The coordinator re-reviewed the correction, both passing focused logs and the
updated 559-file manifest, accepted the schema prerequisite and cleared the initial
commit on 18 November 2026. The user authorised including all outstanding
non-ignored changes; pushes remain with the user. Unspecified post-commit follow-ups
and later Platform/Rendering implementation remain separate work.

## Staged delivery and reviews

Statements about later stages below describe the checkpoint at which each entry
was recorded; the final qualification above is the completion record.

### Stage 1 delivery — 17 Nov 26

Implemented the header and access boundary changes in the shared checkout:

- Added `data_model/string_value_remap.hpp`, preserving the public move initializer
  and checked raw-index translation. Generic translation and reference-only users
  now forward-declare the remap; implementation users include its definition.
- Split public operation limits/diagnostics from `data_operation_internal.hpp`,
  retaining operation accounting, scope behaviour and peak-storage reporting.
- Extracted `instance_edit_types.hpp` for selection paths and descendant policy.
  The complete-block edit header no longer includes the live-instance owner.
  Narrowed reference-context includes and added direct implementation dependencies.
- Added `CDataBlockView::entry_identity`, which checks owner, generation and ordinal
  before returning kind/ordinal, preserving both outputs on failure. Copy uses this
  query; the two copy friendships and three unused edit/view-state friendships
  are removed. Legitimate handle-owner and baked-role planning friendships remain.
- Updated Visual Studio manifests and maintained header maps. Both generic bake
  overloads and reference-free construction paths remain. No stored layout,
  format marker, record planner or operation ownership behaviour changed.

Validation evidence is in `build/schema-consolidation-stage1/`. The starting
checkout already contained the text/span, traversal and remap work plus unrelated
user edits; this stage preserves those changes. Fixture comparison uses the
pre-stage results in `build/string-remap-qualification/`.

Implementation qualification before the include-only review cleanup:

| Check | Result |
| --- | --- |
| Windows x64 Debug solution and standalone `MorphicSchemaFixture` builds | Passed; final incremental solution build includes the completed source and test changes. |
| Linux GCC Debug, all CMake targets | Passed, including the final test rebuild. |
| Isolated header compilation | All 16 changed or directly affected headers passed GCC C++17 syntax checks with the repository build defines and include paths. |
| Windows ordinary `MorphicTests -t1`; Linux `MorphicOrdinary` | Passed: `BakedDocumentTransfer` 629/0, `SchemaModel` 1,172/0 and `Schema` 85,092/0 on both platforms. |
| Final `MorphicTests --schema-references`, Windows and Linux | 1,975 passed, 0 failed on each platform after adding the successful cross-view alias-copy case. Covers invalid, foreign, cleared/rebound and stale handles, failure outputs, alias naming conflicts and successful copy/rename across aliased views. |
| Rebuilt 64/1024-record fixtures, three iterations each on both platforms | Round trips and memory cleanup passed. Binaries, generated headers and logical inspection JSON match the baseline and each other byte-for-byte. Cross-platform readers reproduce the same JSON; generated layout assertions compile with GCC. |
| Resource observations | Fixture peak requested storage matches the baseline: 221,096 bytes for 64 records; 1,413,232 on the first 1024-record iteration and 1,375,400 thereafter. Concurrent-build timing is not treated as a performance comparison. |
| Policy and manifests | Source policy: 0 errors, 0 warnings and the existing identity negative-test suppression. Include-policy CTest and all nine CMake source-extraction tests passed. |
| Formatting | Scoped whitespace and repository line-ending checks passed; new source headers retain leading/final newlines and LF, and Visual Studio manifests retain their CRLF/BOM state. |

The final focused reference runs cover the additional test added after the ordinary
suites passed; no production behaviour changed after those suites. The wider
Win32/Release and small-stack completion matrix remains a later-stage gate. At this
checkpoint stage 2 had not begun, and no commit had been created.

### Stage 1 review — 17 Nov 26

The read-only review found no stage 1 correctness regression. It confirmed plan
adherence, checked entry ownership/generation and alias handling, retained both
generic bake overloads and their failure guarantees, and verified the direct
reference-free paths and unchanged recorded allocation counts. Header boundaries,
friendships, manifests and the existing validation evidence were also reviewed.

The sole finding was a low-priority redundant `reference_types.hpp` include in
`reference_construction.cpp`. That include has been removed; the translation unit
uses neither descriptor type. A focused GCC C++17 syntax compile with the existing
repository defines/include paths passed, as did whitespace and line-ending checks.
The compile log is `build/schema-consolidation-stage1/review-cleanup-compile.log`.
The passing runtime, header and fixture matrices were not repeated for this
include-only cleanup. This review did not itself authorise stage 2 or a commit;
the later stage 2 instruction supplied separate authorisation.

### Stage 2 delivery — 17 Nov 26

Extracted two concrete structural planners, `plan_baked_instances` and
`plan_baked_bulk_data`, with common records, plans and neutral diagnostics in
`baked_record_plan.hpp`. The header does not include either owning role or the
complete-block view. Each planner reads a baked document, a ready resolved
catalogue and the supplied payload extent. It builds private scratch and replaces
the output only on success, including successful absent/empty sections. It does
not inspect payload bytes, bind an owner or create an operation context.

The baked roles now retain the shared record types. Their thin private adapters
map diagnostic reasons explicitly and preserve exact source occurrences. Existing
attachment/rebinding, base-pointer alignment, reference-context rejection,
inheritance validation, index construction and readiness publication remain with
the roles. Live promotion uses the shared record types without changing its
behaviour. Authoring construction plans remain separate.

Traversal order, locator/selection check precedence, zero-byte ranges and allocator
association are preserved. The complete-block view still uses the role adapters,
embedded roles, copied records and indexes; its implementation is unchanged from
the stage 2 baseline. Removing that duplication and its friendships belongs to
stage 3. Both generic bake overloads and the direct reference-free paths remain.

Added focused tests to the existing schema suite for record contents and ancestry,
required extent, successful empty replacement, unready inputs, preservation of a
populated output on late errors, allocation-failure sweeps through uninjected
success, precise diagnostic precedence and role mapping, and the distinction
between structural planning and payload admission. Updated the manifests and
runtime header map. Validation evidence and pre-stage file snapshots are held in
`build/schema-consolidation-stage2/`.

| Check | Result |
| --- | --- |
| Windows x64 Debug solution and standalone fixture consumer | Passed; the final test rebuild includes the strengthened allocation-sweep termination check. |
| Linux GCC Debug, all CMake targets | Passed, including the final test rebuild. |
| Focused compilation | The isolated shared header, both planners and all four migrated role/promotion translation units passed GCC C++17 syntax checks with repository build defines. |
| Windows ordinary `-t1` and Linux `MorphicOrdinary` | Passed: `BakedDocumentTransfer` 629/0, `SchemaModel` 1,172/0 and `Schema` 85,207/0 on each platform. Includes the new planner cases and existing load/rebind, locator, selection, reference and allocation-failure coverage. |
| Rebuilt 64/1024-record fixtures, three iterations each per platform | Round trips and memory cleanup passed. Binaries, generated headers and inspection JSON match stage 1 and both platforms byte-for-byte. Cross-platform readers reproduce the same JSON; generated layout assertions compile with GCC. |
| Reference-free allocation observations | Unchanged: direct bake 89 allocations / 471,008 requested bytes; via live owners 174 / 973,984. Largest request remains 61,440 bytes. |
| Fixture peak requested storage | Unchanged: 221,096 bytes for 64 records; 1,413,232 for the first 1024-record iteration and 1,375,400 thereafter. No timing guarantee is inferred. |
| Policy, manifests and formatting | Source policy: 0 errors, 0 warnings, existing identity negative-test suppression only. Include-policy CTest, all nine CMake source-extraction tests, scoped whitespace and line-ending checks passed. New files retain leading/final newlines and LF; Visual Studio metadata retains its encoding/BOM and line-ending state. |

Independent implementation review checked each extraction against the saved
baseline and found no production correctness issue. Its allocation-test finding
was addressed before the ordinary suites: a sweep stops only after an uninjected
success. The wider Win32/Release and small-stack completion matrix remains a
later-stage gate. Stage 3 has not begun, and no commit has been created.

### Stage 2 review — 17 Nov 26

The read-only review against the saved pre-stage baseline found no correctness
or plan-adherence issues. It confirmed transactional plan replacement, including
empty success and preservation of populated outputs on failure; allocator
association after moves; and retained diagnostic precedence, source occurrences,
traversal order and zero-byte range behaviour. The baked roles still own their
binding, payload admission, inheritance, indexing and readiness checks.

Both generic bake overloads and reference-free direct construction remain, with
unchanged recorded allocation measurements. The view implementation was unchanged;
its embedded roles, duplicate records, unused indexes and associated friendships
were correctly deferred to stage 3. Allocation sweeps require an uninjected
success. Existing Windows/Linux build, test and fixture evidence was reviewed
without repeating the passing matrix. The wider Win32/Release and small-stack
checks remain later-stage gates. The review made no changes or commits; the later
instruction separately authorised recording it and implementing stage 3.

### Stage 3 delivery — 17 Nov 26

The complete-block view now calls both common planners directly and moves their
record vectors into its private checked state. Removed the embedded
`CBakedInstances`/`CBakedBulkData` owners, duplicate view record types and arrays,
the instance-copy loop, and both unused role entry indexes. Removed the associated
view-state friendships and forward declarations from the baked-role headers.

One sorted collection-ID-to-ordinal table supplies the view's required reference
lookup while the common records retain document order. View, copy and export
consumers use those common records. Export reads the validated source document's
collection ID when encoding a targeted span. Reference, cycle, inheritance,
record-count and resource checks remain with the view; diagnostic mapping and
instance-before-bulk failure precedence are preserved. Operation ownership,
public/private bind modes and publication behaviour are unchanged for stage 4.
Both generic bake overloads and direct reference-free paths remain unchanged.

Added standalone view-bind allocation-failure coverage through uninjected success,
including retry on the same view, repeated clear, source preservation and empty
allocator attribution. An independently assembled sparse-ID fixture checks IDs in
a different order from their records, retained metadata order, reference values,
repack/export round trips, duplicate/invalid IDs and missing targets. Its initial
array-selection mask was corrected to positional markers before final validation.
Existing affected sweeps retain their cleanup and failure assertions without
arbitrary minimum counts of allocation failures.

The standalone fixture now reports successful allocation requests and additional
retained/peak requested bytes for the initial view bind, excluding pre-existing
source storage and subsequent inspection. Whole-round-trip peak reporting remains.
The focused pre-stage measurement compiled only the instrumented fixture against
88 unchanged Linux production objects from the passing stage 2 build, before any
Linux production rebuild. The capture script, object hashes, pre-stage file
snapshots and validation logs are in `build/schema-consolidation-stage3/`.

| View-bind measure | 64 records, before → after | 1024 records, before → after |
| --- | --- | --- |
| Successful allocations | 30 → 26 | 38 → 34 |
| Retained requested bytes | 11,800 → 9,840 | 17,560 → 15,600 |
| Peak requested bytes | 15,952 → 14,064 | 21,712 → 19,824 |

The post-change measurements agree across Windows x64 Debug and Linux GCC Debug,
over three iterations per size. Both reports contain one instance and three
collections, so the removed directory storage is constant across these two payload
sizes. The four removed allocations correspond to the duplicate instance/bulk
vectors and the two role indexes; their copy and index-building work is gone.
Requested-byte measurements include engine allocation headers/conditioning, not
the fixture allocator's own prefix or general process allocations. Timing samples
collected alongside builds are not a latency comparison.

Whole-round-trip peak requested storage also decreases: 221,096 → 215,520 bytes
for 64 records; 1,413,232 → 1,409,312 for the first 1024-record iteration and
1,375,400 → 1,369,824 thereafter. Reference-free direct construction measurements
remain 89 allocations / 471,008 requested bytes versus 174 / 973,984 via live
owners, with a 61,440-byte largest request in both paths.

| Stage 3 validation | Result |
| --- | --- |
| Windows x64 Debug solution and standalone fixture builds | Passed; final test rebuild includes the corrected independent sparse-ID fixture. |
| Linux GCC Debug, all CMake targets | Passed, including the final test rebuild. |
| Windows ordinary `-t1` and Linux `MorphicOrdinary` | Passed: `BakedDocumentTransfer` 629/0, `SchemaModel` 1,172/0 and `Schema` 85,290/0 on each platform. Covers existing view/reference/inheritance/cycle behaviour and retained reference-free APIs. |
| Focused Windows `--schema-references` | 2,058 passed, 0 failed after the fixture correction; the final ordinary suites include these cases. |
| Portable fixtures | Three iterations per 64/1024-record report per platform passed round trips and memory cleanup. Binaries, generated headers and inspection JSON match stage 2 and each other byte-for-byte. Cross-platform readers match, and all four generated layout assertion files compile with GCC. |
| Header, policy and formatting | The private view-state header compiles in isolation with repository build settings. Source policy reports 0 errors, 0 warnings and the existing identity negative-test suppression; include-policy CTest passes. Scoped whitespace, explicit changed-file newline checks and repository line-ending checks pass. No build manifest changes were needed. |

Independent implementation review found no actionable issue in direct-plan
adoption, diagnostics, allocator lifetimes, lookup ordinals or removal of role
setup. Tests and measurement isolation were also reviewed. The wider Win32/Release
and small-stack completion matrix remains a later-stage gate. Stage 4 has not
begun, and no commit has been created. This checkpoint awaits the requested
stage 3 review before any later-stage implementation.

### Stage 3 review — 17 Nov 26

The read-only review against the saved stage 3 baseline found no actionable
correctness or plan-adherence issues. Direct plan consumption preserves record
order, parent relationships, allocator lifetimes and diagnostic mapping. Collection
lookup retains original ordinals; reference, inheritance, cycle and resource
checks complete before public binding succeeds. Both generic bake overloads,
their failure guarantees and reference-free construction/loading behaviour remain.
Operation ownership was correctly left unchanged for stage 4.

The review checked the recorded Windows x64 Debug and Linux Debug results,
including 85,290 passing schema checks per platform, matching portable fixtures,
allocation-failure sweeps, sparse-ID coverage and the scoped resource measurements.
No tests were rerun, files changed or commits made during that review. Win32,
Release and small-stack qualification remain later-stage gates. The subsequent
instruction separately authorised recording the review and implementing stage 4.

### Stage 4 delivery — 17 Nov 26

`CDataBlockView` now retains one private `SDataViewOwner` pointer and its generation.
The stable, parent-allocated owner contains its `CDataOperation` first and a
`TInstance<SDataViewState>` second. Metadata storage captures the operation context
explicitly and is constructed/loaded under its scope. Reverse member destruction
retires all metadata before the operation and its allocator. A small private RAII
guard handles incomplete binding; the same teardown function serves failure and
clear. The owner allocation remains charged against the storage allowance.

Removed the separate operation pointer, owns-operation flag and private borrowed
`CDataBlockView::bind` overload. The internal `SDataViewState` loader still borrows
an operation and supports several checked sources under one budget. Scope and
parent-allocator lifetime requirements are now explicit in the private/public
comments and runtime documentation. Public handles, generation checks, source-byte
borrowing and success-only publication of a binding remain unchanged.

Edit sessions already had the agreed operation-before-metadata member order.
Their metadata owner now captures the operation context directly and uses scoped
emplacement; candidate-before-catalogue destruction is documented. Operation source
bookkeeping also captures its context in its constructor, removing the unsafe
empty-container context replacement. Command resets still affect diagnostics only:
work, source/node admissions and live/peak storage accounting retain their existing
semantics. Outer bake/copy/repack/export owners and their borrowed checked states
already satisfy the agreed lifetime model and required no implementation changes.

Ordinary view queries do not install or consume the retained binding operation as
a lifetime budget. They still participate in an explicitly active caller scope.
Publication continues to use the existing parent-lifetime storage transition.
Both generic bake overloads, reference-free direct paths and validation/publication
behaviour remain unchanged; stage 5 has not begun. The generic baking source and
declarations, aggregate runtime and copy/export implementations match their saved
pre-stage snapshots.

Added focused tests for original-allocator ownership across ambient-context changes,
repeated clear/cancel and destruction under a rejecting unrelated allocator,
published output surviving its edit/view owners, nested allocator/meter restoration,
multiple checked states sharing source/node/storage allowances, and queries after
the binding work allowance has been consumed. Existing setup-allocation sweeps,
retry/rollback/publication cases and cumulative edit-work tests remain. The new
query test distinguishes ordinary queries from explicit ambient-meter participation.

Evidence and pre-stage snapshots are in `build/schema-consolidation-stage4/`.
No build-manifest or include-policy exception was needed. The temporary standard
library setup guard rejected by source policy was replaced with the private guard
before the successful builds and runtime validation.

| Stage 4 validation | Result |
| --- | --- |
| Windows x64 Debug solution and standalone fixture builds | Passed. |
| Linux GCC Debug, all CMake targets | Passed. |
| Windows ordinary `-t1` and Linux `MorphicOrdinary` | Passed: `BakedDocumentTransfer` 629/0, `SchemaModel` 1,172/0 and `Schema` 85,369/0 on each platform. |
| Focused Windows `--schema-references` | 2,137 passed, 0 failed, including ownership and shared-budget tests; also included in both ordinary suites. |
| Portable fixtures | Three iterations per 64/1024-record report per platform passed round trips and cleanup. Binaries, generated headers and inspection JSON match stage 3 and each other byte-for-byte. Cross-platform reads match; all four generated layout assertion files compile with GCC. |
| Headers, policy and formatting | Public view, private checked-state and private operation headers compile separately with repository build settings. Source policy: 0 errors, 0 warnings and the existing identity negative-test suppression. Include-policy CTest, changed-file whitespace/newline checks and repository line-ending checks pass. |

Reference-free allocation measurements remain unchanged: direct bake uses
89 allocations / 471,008 requested bytes, and live-owner construction uses
174 / 973,984; both retain a 61,440-byte largest request. View-bind measurements
agree across Windows/Linux and all three fixture iterations:

| View-bind measure | 64 records, stage 3 → stage 4 | 1024 records, stage 3 → stage 4 |
| --- | --- | --- |
| Successful allocations | 26 → 26 | 34 → 34 |
| Retained requested bytes | 9,840 → 9,872 | 15,600 → 15,632 |
| Peak requested bytes | 14,064 → 14,096 | 19,824 → 19,856 |

The explicit ownership wrapper adds 32 requested bytes per bound fixture view,
without another allocation. Whole-round-trip peak is 215,552 bytes for 64 records,
1,409,376 for the first 1024-record iteration and 1,369,856 thereafter. The prior
fixture-scope and requested-byte measurement limitations still apply; concurrent
timing samples are not a performance guarantee.

Independent implementation review found no issue in the final ownership guard,
member/context lifetime order, shared accounting or tests. The wider Win32/Release
and small-stack completion matrix remains a later-stage gate. Stage 4 is ready
for the requested review; stage 5 has not started, and no commit has been created.

### Stage 4 review — 17 Nov 26

The read-only review against the saved stage 4 baseline found no production
correctness defect. It confirmed that retained metadata is destroyed before its
operation, captured allocator contexts remain valid during teardown, and shared
budgets and cumulative edit accounting are preserved. Ordinary queries do not
consume the retained binding allowance, while explicit ambient scopes still apply.
Both generic bake overloads, including the no-remap overload, and reference-free
paths are unchanged. Existing Windows/Linux validation and fixture evidence was
reviewed without rerunning the passing matrix.

The sole finding was missing allocation-failure coverage for edit-session setup:
the existing publication sweep enabled injection after `begin()` had completed.
The authorised follow-up adds sweeps for both `begin()` and `begin_empty()`, armed
before the first setup allocation and continuing through an uninjected success.
They check failed readiness, allocation diagnostics and empty attribution before
cancellation or direct retry on the same object. They also verify allocator-scope
restoration, the populated/empty session contents, use of the empty session's
catalogue, repeated cancellation, final cleanup and unchanged source bytes.

Focused Windows x64 Debug and Linux GCC Debug test-target builds passed. The
rebuilt `--schema-references` runner passed **3,435 checks, 0 failed** on each
platform. Source policy reported 0 errors and 0 warnings, with the existing
identity negative-test suppression; scoped whitespace, changed-file newline and
repository line-ending checks passed. Logs are in
`build/schema-consolidation-stage4/review-windows-build.log`,
`review-linux-build.log`, `review-windows-references.log` and
`review-linux-references.log` under the same directory. Pre-follow-up snapshots
are in its `review-baseline/` directory.

Independent review found no issue in the added sweeps. This follow-up changes only
tests and this plan; no production implementation or stored format changed. The
review finding is resolved. The wider Win32/Release and small-stack qualification
remains a later-stage gate. Stage 5 has not begun, and no commit has been created.

### Stage 5 delivery — 17 Nov 26

Reference-aware `CSchema::validate` now owns a bounded operation and constructs a
checked temporary block without publishing it. The internal baked-construction
entry point includes preparation, so validation and baking share type resolution,
plans, encoding, relocation, complete-block admission, inheritance and reference
checks. The complete-file result cap still applies. Validation destroys its private
storage before its operation and allocator; it no longer performs or charges the
final caller-lifetime copy and the associated publication checks.

Baking retains its explicit publication step and the existing construction-context
lifetime through publication. Its storage accounting and failure guarantees are
unchanged. A file-local diagnostic finalizer is shared by bounded validation and
baking: it preserves specific schema reasons on failure, copies durable operation
details, and clears diagnostics on success. No optional-output mode, public API,
header dependency or format change was introduced.

The direct reference-free validation, materialisation and default bake paths keep
their existing behaviour; the explicit-limit bake remains bounded. Both generic
bake overloads and their implementation are unchanged. Runtime documentation now
distinguishes reference-free validation, which constructs no runtime payload, from
reference-aware validation, which requires temporary construction but no final
publication copy.

Added reference validation/bake agreement cases covering defaults, inheritance,
empty sections and ranges, missing/mistyped targets, invalid ranges/text/scalars,
cyclic declarations and complete-file result limits. A validation allocation-failure
sweep checks cleanup, scope restoration, retry and source preservation through an
uninjected success. A separate allocator probe derives the first publication
allocation from validation's measured count: validation succeeds without reaching
it, while baking fails there with a publication allocation diagnostic and empty
output. The assertions allow publication's validation scratch allocations and
preserve the existing precedence of specific schema and operation diagnostics.

| Stage 5 validation | Result |
| --- | --- |
| Windows x64 Debug solution, final test-target rebuild and standalone fixture consumer | Passed. |
| Linux GCC Debug, all CMake targets and final test rebuild | Passed. |
| Windows ordinary `-t1` and Linux `MorphicOrdinary` | Passed: `BakedDocumentTransfer` 629/0, `SchemaModel` 1,172/0 and `Schema` 87,146/0 on each platform. Includes retained no-remap/remap, reference-free, publication-lifetime and failure cases. |
| Focused Windows `--schema-references` | 3,914 passed, 0 failed; the same cases are included in both ordinary suites. |
| Portable fixtures | Three iterations for each 64/1024-record fixture per platform passed round trips and cleanup. Binaries, generated headers and inspection JSON match stage 4 and each other byte-for-byte. |
| Policy and formatting | Source policy: 0 errors, 0 warnings and the existing identity negative-test suppression. Linux include-policy CTest, scoped whitespace/newline checks and repository line-ending checks passed. No manifests or public headers changed. |

For the focused reference fixture, validation uses 85 allocations / 89,776 requested
bytes, compared with baking's 88 / 93,920 on both platforms. The removed publication
step accounts for three allocations and 4,144 requested bytes in this fixture:
the final independent buffer and two document-validation scratch allocations.
These are requested-byte observations, not peak-storage or general scaling claims.
Reference-free measurements remain unchanged: direct bake uses 89 allocations /
471,008 requested bytes; live-owner construction uses 174 / 973,984. Both retain a
61,440-byte largest request. Portable-fixture peak and view-bind measurements also
match stage 4. No timing guarantee is inferred.

Evidence and pre-stage snapshots are in `build/schema-consolidation-stage5/`:
`windows-build.log`, `windows-test-build.log`, `fixture-windows-build.log`,
`linux-build.log`, `linux-test-build.log`, `windows-references.log`,
`windows-tests.log`, `linux-tests.log`, `linux-ordinary.log`, the fixture CSVs and
`fixture-comparison.log`. Initial test API/fixture assumptions were corrected
before the successful focused and ordinary runs; production diagnostic precedence
was not changed to accommodate those assertions.

Independent implementation review found no actionable issue in the production
split or final tests. Stage 5 awaits the requested review. Stage 6, including the
wider Win32/Release and small-stack completion matrix, has not begun. No commit
has been created.

### Stage 5 review — 17 Nov 26

The read-only review against the saved stage 5 baseline found no actionable
correctness or plan-adherence issues. Validation retains construction, relocation,
reference/inheritance/cycle checks, result-size limits and final operation-health
checks; only publication-specific work is omitted. Temporary storage is destroyed
before its operation and allocator, and baking retains its publication and failure
guarantees. Both generic bake overloads, including no-remap baking, are unchanged.
Reference-free paths retain their behaviour and allocation characteristics.

The review checked the new semantic-agreement, cleanup/retry, result-limit and
publication-allocation tests against the saved Windows/Linux evidence: 87,146
schema checks per platform and 3,914 focused Windows reference checks passed.
No files changed, tests rerun or commits were made during that review. The
subsequent instruction separately authorised recording it and completing stage 6.

### Stage 6 review — 17 Nov 26

The read-only review found no actionable correctness or plan-adherence issue.
It confirmed the direct include cleanup, retained no-remap bake overload and
failure guarantees, reference-free paths and allocation characteristics, and
documentation migration with the staged history preserved. All 559 files in the
final qualification manifest still matched the checkout. Saved build, ordinary
test, small-stack, fixture/cross-reader, layout/header and policy evidence supports
the completion record, including its limited stack qualification claim. No files
changed, tests rerun or commits were made during that review. The later coordinator
review found the diagnostic gap addressed in the follow-up above.
