Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   validation.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   4 Oct 2026

# Schema validation

The [specification](specification.md) defines behaviour and the
[runtime guide](runtime-api.md) documents its C++ interfaces. Validation covers
observable results, failure boundaries and generated layout fidelity. Private
record organisation is not a public ABI or a substitute for those checks.

The [type-schema merge milestone](../project/completed_milestones.md#type-schema-merging)
records the latest completed four-configuration validation checkpoint. The
earlier role, persistence and schema milestones retain their historical counts.

## Type schema merge coverage

`SchemaMerge_test_cases.cpp` runs within the Schema suite. It covers independent
source validation, equivalent natural/explicit layouts, structural/reference
conflicts, absent/explicit defaults, exact document scalar equality and integer
presentation. All newline-escaping preference combinations are checked across
live/baked inputs. One-sided copies in either source position preserve integer
domain/width, notation/prefix and string presentation, both for whole subtrees and
defaults within shared types; partial default arrays retain their supplied length.
Mixed live/baked inputs exercise both successful output and source
diagnostics. The result is checked after releasing its inputs, and adopted/baked
through existing schema APIs. Instance construction confirms adoption of a merged
default while preserving an explicit declaration; bound-owner replacement remains
rejected.

Occupied destinations and first/second/both-source aliasing exercise publication.
Each allocation attempt is rejected in turn for representative merges, checking
unchanged source/destination documents, retained handles, clean attribution and
sanitised result-validation diagnostics. Shared and one-sided nesting at document
depths 255/256 checks the common limit; shared nesting uses an explicit work stack
to avoid overflowing the Debug C++ stack. Shared dependency graphs and objects on
both sides of the small-object matching threshold exercise indexed paths.

Growth fixtures merge 128, 512 and 2,048 overlapping types in reverse encounter
order, with repeated strings and one additional type. Timings are observations,
not pass/fail thresholds, and include input/result resolution. The construction
path sorts and preloads strings once, releases preparation vectors before node
construction, uses existing string indexes thereafter and builds temporary name-ID
indexes only for larger shared objects. Timing evidence and its limits are in
the milestone; no worst-case claim is made for existing resolver/cache internals.

## Running the checks

Build and run `MorphicTests` using the repository's
[build instructions](../project/building_and_testing.md). The ordinary `-t1` run
includes [Schema_test_suite.cpp](../../tests/test_suites/Schema_test_suite.cpp)
and the related document-model suites. For example, after building Debug x64:

```powershell
.\build\bin\x64\Debug\MorphicTests.exe -t1 --log-tag=schema-check
```

The Schema suite also runs
[SchemaDataBlock_test_cases.cpp](../../tests/test_suites/SchemaDataBlock_test_cases.cpp).
These cases assemble persistence files from generic baked documents and separate
payload bytes without a schema block writer. They cover both content kinds,
metadata type/version errors, unknown/missing properties, empty sections and
zero-byte descriptions, bounded header failures, alignment, truncation and extra
bytes, every container-padding length and offending byte, and diagnostic reset
and source locations. Pure layout tests cover the exact 2 GiB limit and integer
overflow without large allocations. A rejected scratch allocation verifies
failure cleanup and conservative reporting by the generic document validator.

Completed-block tests exercise both instance/bulk data and live/baked schema
combinations. They check pointer-preserving adoption/release, occupied
destinations and outputs, repository-backed borrowed attachment without hidden
disposal, absence of block-held schema references, schema-independent storage
readiness, explicit role attachment, empty/zero-byte content, stripped content,
opt-in role comparison, opaque payload gaps/trailing bytes and
source-preserving failure diagnostics. Each preparation allocation is rejected
in turn to verify cleanup, diagnostic propagation and retained local ownership.

Completed live-bake tests cover all source/destination schema combinations and
output forms, comparing payloads against existing lower-level baking. They
exercise edited instance snapshots and specialisations, bulk data counts, empty
and zero-byte content, metadata, alignment, padding, source independence and
copied-file reattachment with comparison enabled. Failures check invalid
preconditions, schema availability/compatibility, unrepresentable retained values
and each construction allocation, including final attachment. Source locations
survive preparation failures; temporary output locations do not escape cleanup.
Completed blocks and copied-file adoption also exercise exact file-size capacity,
with generic baked-document tests checking minimum and non-alignment-multiple
capacities, preserved attribution and clean deallocation.

Description materialisation tests cover all four source/schema overloads for
both roles and all output forms. They check defaults and nested instance
inheritance, complete bulk records, valid and unset offsets, inferred counts,
missing/empty sections, zero-byte records, replacement of persistence metadata
and omission of unrelated root sections. Sources remain byte-equivalent and
can be destroyed before the result is consumed or reattached with comparison.
Failures cover invalid preconditions, unknown types, locators, counts,
insufficient values and stripped descriptions. Diagnostics map back to original
live/baked values. Each construction allocation is rejected in turn, checking
empty destinations, released schema references, temporary cleanup and location
lifetimes. JSON parsing is exercised separately from the block API.

Completed-block stripping tests cover both roles, all source/destination schema
combinations, local and repository-backed borrowed sources, empty and zero-byte
content, retained/external/already-stripped descriptions and inferred bulk data
counts from arrays and byte sizes. Byte comparisons include payload gaps,
trailing storage, noncanonical booleans and unused flag bits. Tests check new
document lengths, recursive instance declaration removal, reattachment, source
independence and no implicit asset disposal. Failures cover self/occupied
destinations, unavailable supplied schemas, missing destination types and each allocation
through source validation, promotion, baking, assembly and final attachment.
Temporary-output diagnostics are cleared and all private allocations and schema
bindings are released on failure.

Schema-switch tests exercise all four data roles against live and baked targets.
They cover changed and overridden defaults, nested arrays, bit fields, signed
zero, partially declared child inheritance, reordered type indices, empty groups,
zero-byte packages and deep specialisation chains. Success and failure preserve
handles, payload addresses and bytes. Allocation sweeps check atomic binding
replacement and temporary-only allocation. Stale declarations fail checked
switches while trusted switches and explicit reconciliation remain available;
trusted switching also restores typed access after the previous schema dies.

New-payload gap tests use an allocator that fills every allocation with nonzero
bytes. Both roles and all output forms check zeroed alignment gaps, repeated
byte-identical output, preserved source and internal record padding, and zero-byte
entries between nonempty extents. Description materialisation additionally checks
leading and explicit-offset gaps. Existing supplied-payload, stripping and Host
persistence tests continue checking exact preservation of existing nonzero bytes.

Host complete-buffer coverage lives in `BakedDocumentTransfer`, `BundleService`
and `BundleRequests`. It checks appended bytes through owning transfer, loose
loading, borrowed bundle views, cache reuse and binary saving, with separate
prefix-only navigation. Cases exclude spare capacity and bundle padding, upgrade
requested alignment, reject mismatched cache representations, reparse JSON output,
reject writes into bundles and save retained entries after bundle closure.
File workflows exercise shared and dedicated conditioning workers. The real
`tests/host_workers/run_tests.ps1` harness also checks bootstrap and worker routing.
Schema-specific cross-layer workflows in the same request suite exercise both
`.bid` and `.bbd` with a shared caller-owned schema: live bake, release/admission,
attachment, complete binary save and physical reload; JSON load, explicit
materialisation and independent parsed-source disposal; bundle construction from
saved files, attachment, closure and loose saving/reloading. Typed entry values
and complete file bytes are checked at each boundary. Further cases cover failed
attachment after successful Host loading, source/discard retention, retained
assets after save failure, and disposal waiting for admitted saves. Both shared
and dedicated worker routes run these cases.

A bounded mutable document edit in test-owned bundle storage verifies that an
explicit whole-bundle save writes current bytes. The prior disk file remains
unchanged before saving; a promoted independent document leaves its source
unchanged. The test retains mutable access before Host publication and mutates
only while borrowers are quiescent; it adds no production mutation API.

The [persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence) records the final
Debug/Release x64/Win32 Core matrix, Host worker acceptance and Debug x64 module
lifecycle harness with one and two workers. The lifecycle harness covers actual
DLL retirement, in-flight saving, bundle closure and shutdown.

Logs and generated files include the tag and process ID. The runner prints their
paths; default output is beneath `development/logical-roots/test-output` and
logs beneath `development/logical-roots/test-logs`.

The schema suite generates C++ headers and separate assertion translation units.
Running the suite alone does not compile those generated files. For layout or
generator changes, pass each printed source path to the maintained helper:

```powershell
.\tools\validate_schema_layout.ps1 -Source '<printed validation source path>'
```

[validate_schema_layout.ps1](../../tools/validate_schema_layout.ps1) compiles with
C++17 and warnings as errors for x86 and x64 by default. Its `-Platforms` argument
can select either target. The fixtures cover natural and explicit layout, empty
types, the authoring sample, representation spelling and generated-name collisions.
Assertions check size, alignment, member offsets and extents, array shape, enum
storage/values, mask types/values, standard layout and trivial copyability,
including `fp16data_t`. Generated data declarations themselves contain no test
assertions or operational code. Support for a compiler/ABI requires fidelity to
the resolved layout; packing changes or approximate output must not hide a mismatch.

Use the appropriate Debug/Release and 32/64-bit configurations when code changes
affect representation, lifetime, arithmetic or allocation. Do not repeat a passing
matrix without a new change or unresolved concern. Documentation-only changes
need link, diff and line-ending checks; edits to the sample also warrant its
existing runtime coverage. Source/project changes use the repository's
[policy validator](../project/policy_validator.md).

## Coverage to preserve

| Area | Observable contracts |
| --- | --- |
| Resolution and queries | Live/baked parity, forward references, cycle/duplicate rejection, exact property sets, named/ordinal agreement, occurrence mapping, clear/move/re-resolution lifetimes. |
| Types and layout | Empty type identities and zero-byte members; nested and count-one arrays; natural and explicit layout; increased alignment; overlap/bounds diagnostic order; recursive gaps; the inclusive 2 GiB extent ceiling. |
| Values and defaults | Integer boundaries and signedness; floating rounding, overflow, signed zero and special strings; existing fp16 conversion; enum aliases and implicit first-label defaults; UNORM/SNORM raw codes and floating inputs. |
| Input structure | Complete bulk data records; instance defaults and inherited positional tails; named singleton normalisation and scalar shorthand; invalid nulls, excess values and unsupported aggregate defaults. |
| Ownership and failure | Non-owning baked views, owned live payloads, schema binding invalidation, non-destructive translation, allocation-failure cleanup and the publication rules of each operation. |
| Capture and editing | Independent snapshots, selected-value preservation, parent-before-child propagation, repair of discarded unrepresentable selections, and bulk data replacement after reconciliation/promotion. |
| Reconciliation and output | Binary authority, changed defaults, immediate-parent comparison, shortest necessary array prefixes, embedded/external reload, encoded fidelity and source preservation. |
| Stripped output | Required payloads, navigation/binary access, schema/default retention, empty types, malformed markers, contradictory declarations, promotion reconstruction and exact raw encodings. |
| Integer presentation | Declared-type policies, full-width masks, nested defaults, structural thresholds, live/baked sources, strict JSON, repeated preparation and inferred structure details. |
| Remapping | Exact-type direct-member matching, default-sensitive compound compatibility, partial/no-match plans, destination claims, range coalescing, all copy paths, exact-stride preflight and untouched unmatched bytes. |
| Unused storage | Nested padding and optional unused-bit clearing, preservation of every addressable field, complete fixed-array views, document gaps, idempotence and failure before writes. |
| Access cost | Baked sorted indexes, ordered live records, neighbour cursors with bounded fallback, scoped name and enum-value indices, parent-index schema switching, source-driven remapping, cached storage preflight and allocation-free clearing after preflight. |

The data-model suites separately cover integer metadata, string flags, subtree
copying, promotion/baking and their allocation failures. Schema-specific rules
reuse those facilities rather than introducing a second document implementation.
Allocation and complexity tests establish their measured properties; they do not
promise general timing improvements or that all name lookup is indexed.
Focused regressions cover reverse/shuffled/sparse named input, allocation-free
ordered value checks, shuffled-input allocation failures, exact duplicate
diagnostics, enum alias and signedness boundaries, and nested storage preflight
failure before writes.
Construction regressions also cover wide reverse-ordered objects, sparse source
string IDs under an allocation ceiling, same-document ID reuse, checked arbitrary
readers and deep promotion. Reconciliation covers delayed scalar/compound array
prefixes, deep last-descendant changes, enormous zero-byte arrays and allocation
failures during lazy container construction. Traversal bounds follow from the
single-pass implementation; these tests do not impose wall-clock thresholds.

## Authoring sample

`test_schema_sample` reads [schema-example.json](schema-example.json) through the
engine parser, bakes and resolves its schema, checks expected layouts, generates
compiler fixtures, materialises instances and bulk data records, promotes both roles,
and checks embedded/external baking and reload. Its eleven type definitions,
eleven instance declarations and three bulk data records exercise more than resolution.
See the [sample notes](schema-example-notes.md) for expected values and layouts.

## Validation history

The [schema milestone](../project/completed_milestones.md#schema-system-and-refinement)
retains representative completion checkpoints. Detailed historical logs and
per-change matrices belong to their commits and local test output, not the
current behavioural specification.
