Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   completed_milestones.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   26 Jul 2026

# Completed Milestones

## Purpose

This document is the concise project record for completed cross-cutting work.
It records outcomes and validation without retaining temporary implementation
plans, delegation instructions, snapshot guidance, or superseded decisions.

Permanent behavior and architectural contracts belong in the subsystem
documents linked from each milestone. Current and future work belongs in
`future_work_notes.md` and the backlog.

## Document preparation and composition

Implemented on 19 October 2026 from the
[reviewed plan](document_preparation_composition_implementation_plan.md), after
separate user authorisation. This records delivery, not user review or a commit
checkpoint. See the [runtime API](../schema/runtime-api.md#document-preparation-and-composition).

Four preparation overloads extract selected live/baked sections under the original
resolved schema, preserve retained descriptions and reconstruct only stripped
instances or wholly absent bulk arrays. Storage metadata validates before removal;
unrelated sibling contents never affect extraction. Three composition overloads
require a types document and one or both prepared data domains, resolve types once,
validate each domain and copy sections into independent live storage. Both operations
publish through non-failing move, support occupied/source-aliased destinations and
preserve original storage and handles on failure. Preparation rejects output backing
its supplied schema; composition releases its local resolver before publication.

Shared locator/range, reconstruction and string/construction helpers serve existing
roles and merges too. No temporary role/payload copy, default expansion in composition,
second composition resolution or depth preflight was introduced. Source values use
one validation/comparison path; prepared results retain their required validation.

Verification includes mixed input forms, complete metadata tables, original defaults
and parent inheritance, whole-entry rules, presentation, invalid encodings, cumulative
copied/reconstructed depth, aliasing, allocation-failure sweeps and integrated
preparation/merge/composition/construction. Adding 4,096 unrelated sibling properties
or growing an omitted array from 1 to 100 million elements leaves allocation counts
and requested bytes unchanged. Repeated-type scaling excludes per-entry metadata
worklists. Capacity planning now reserves unique string storage, copied node slots,
known structural nodes and preparation sibling batches without an extra traversal.
Reserved imports and live/baked subtree copies have no-further-allocation tests.
At 2,048 entries, preparation/composition allocation requests fall from 190/201 to
73/57; combined requested bytes fall from 33,568,064 to 16,385,536. These totals
are aggregate requests, not peak memory or measured copied bytes. Reconstruction
beyond the known minimum still grows. The buffer cap remains a
[separate follow-up](../backlog/schema_follow_on.md#shared-container-growth-during-document-construction).
`SDataLocator::valid` follows the occurrence members; x64 and Win32 layout probes
confirm that its size stays 96 bytes, with outer padding moved to the end.

Debug/Release x64 and Win32 solution builds and ordinary `-t1` suites pass, each
with 89,994 Schema checks and 13,319 BakedDocument checks, zero failures across
all suites. Logs are
`build/preallocation-{debug64,debug32,release64,release32}-{build,tests}.log`.

The final failure-injection addition uses a parent with 64 specialisations and a
following base sibling. It confirms failure of the later batch reservation for
empty, occupied and source-aliased destinations, unchanged state and handles,
cleanup, and eventual complete output. Debug/Release x64 builds and ordinary
`-t1` suites pass with 90,108 Schema checks each and zero failures. These final
coverage-only runs are recorded in
`build/batch-reservation-{debug64,release64}-{build,tests}.log`.

The five CMake source-extraction tests pass; the actual test-project manifest
includes every new translation unit. Whitespace, line endings, local document
links and Visual Studio registration/filter visibility checks pass. Visual Studio
item/filter encoding, CRLF and terminal-newline state are preserved.
Non-incremental linking uses the existing environment workaround without changing
project settings.

Release x64 preparation plus composition for 32, 128, 512 and 2,048 reconstructed
entries took 0.748, 2.847, 12.093 and 56.787 ms in the recorded run. Timings are
supporting observations; allocation/work bounds are the regression assertions.

## Instance and bulk data aggregation

Implemented on 18 October 2026 from the
[reviewed plan](data_aggregation_implementation_plan.md). This records delivery
and validation, not a commit checkpoint or user acceptance. See the
[runtime API](../schema/runtime-api.md#instance-and-bulk-data-aggregation).

`merge_instances` and `merge_bulk_data` accept prepared live/baked document views,
one resolved merged schema and an independent live destination. Each input and
the staged result validate independently. Instances retain explicit selections
and recursive specialisations; bulk data merges matching record prefixes and
retains the longer tail. Nested arrays use ordinary equality rules. Conversion,
reconciliation, schema attachment and binary construction remain separate.

The type merger's traversal, presentation, string preparation and construction
now serve all three merge operations. Complementary declaration validation shares
scalar/shape admission and visits supplied values without default expansion or
payload allocation. Baked planning shares entry-property and extent/alignment
helpers; no broad loader rewrite or extra document normalisation was introduced.
Publication uses the existing non-failing move and supports occupied/source-aliased
destinations. Destination-as-schema-backing is rejected before mutation.

Cheap explicit depth checks remain; a valid merged document may fail later implicit
binary construction at the existing depth limit. Regression tests exposed excess
Debug stack usage in that pre-existing constructor. Extracting scalar handling
from recursive dispatch and borrowing small storage/occurrence views reduced its
frames without changing admission, writer/comparison order or the depth limit.
The review follow-up replaces nested occurrence-selection expressions with
ordinary branches: the actual MSVC x64 Debug `structure()` stack reservation is
now 1,728 bytes, down from 2,160, with no additional helpers or allocations.
Live and baked documents expose optional iterative `maximum_depth()` queries,
counting the root as zero and excluding detached live values. These use constant
scratch space, allocate nothing and impose no recursive depth cap; they do not
predict implicit schema construction depth or add work to aggregation.

Verification covers mixed forms, independent validation, scalar/presentation
semantics, inheritance, schema-merge integration, aliasing, failure at each
allocation attempt, diagnostic lifetimes, packed extent overflow, shared/copied
document depth, explicit value depth and deferred implicit depth failures.
Holding declarations fixed while changing an omitted array from 1 to 100 million
elements produced identical allocation counts and requested bytes. Growth fixtures
cover wide reversed instance groups and long record prefixes with repeated strings.
Follow-up tests exercise deep specialisation chains with pending siblings,
shared/one-sided paths and mixed source forms, including transactional rejection
beyond the document-depth boundary. Fixed sparse declarations for 256 instances
use 86 allocation requests and 593,920 requested bytes at schema widths 1, 128
and 2,048 on x64. Release x64 live-input timings were 3.794, 3.891 and 3.872 ms;
baked-input timings were 1.702, 1.777 and 1.919 ms. Probe tests cover empty,
branched, detached and mutated trees, allocation denial and depth 1,023.

Initial Debug/Release x64/Win32 solution builds and ordinary `-t1` runs passed, each with
78,811 Schema checks and zero failures. The five CMake source-extraction tests
passed, and the actual test-project manifest includes all new translation units.
Final logs are `build/data-merge-final-{debug64,debug32,release64,release32}-tests.log`;
build logs use the corresponding `-build.log` suffix (Debug x64 uses
`build/data-merge-debug-build.log`). Non-incremental linking avoided an environment
issue with stale/incompatible linker state; no project linker setting was changed.

After the review follow-up, all four solution configurations and ordinary `-t1`
runs passed again: 80,653 Schema checks and 13,319 BakedDocument checks per run,
with zero failures across the suites. Logs are
`build/depth-review-{debug64,debug32,release64,release32}-{build,tests}.log`.
The rebuilt Debug x64 object's disassembly confirms the reduced frame reservation.
Whitespace, line endings and changed-document Visual Studio visibility checks
also passed.

The 19 October manual-review follow-up centralised the unchanged 256-level budget
as `document_limits::k_max_recursion_depth` in `data_model_types.hpp`, replacing
literal checks and duplicate local constants across bounded document/schema
paths. The hierarchy's 255-parent threshold is derived from the same constant;
depth conventions and iterative probes remain unchanged. Debug x64 solution
build and ordinary suites passed again with 80,653 Schema and 13,319
BakedDocument checks, zero failures. Writer, structure, declaration-validation
and encoded-comparison instructions match the preceding Debug x64 disassembly.
Logs are `build/shared-depth-debug64-{build,tests}.log`.

Release x64 measured 1.237, 5.404 and 24.220 ms for 128, 512 and 2,048 overlapping
instances, and 0.375, 1.604 and 6.538 ms for bulk prefixes of those lengths. These
include input/result validation and merge construction, excluding fixture parsing
and schema resolution. Instance allocation requests were 76, 116 and 164; bulk
requests were 41, 61 and 81. The observed roughly 4.1-4.5-fold time increase for
fourfold input growth is consistent with linear/logarithmic traversal and indexed
matching. Timings are not thresholds or worst-case guarantees for existing
containers, string-byte comparisons or schema internals.

Boundary conversion APIs for stripped instances and missing bulk descriptions,
and complete-document composition, remain future work. No implementation of those
operations is implied by completion of the prepared merge cores.

## Type schema merging

Implemented and validated on 18 October 2026. This records implementation and
validation, not a commit checkpoint or user acceptance of later aggregation work.
The [runtime API](../schema/runtime-api.md#type-schema-merging) documents the
interface; the [completed plan](type_schema_merge_implementation_plan.md) retains
the reviewed scope.

`merge_type_schemas` accepts independently valid live/baked document views and
publishes an independently owned live document containing only merged types.
It reuses schema resolution, representation compatibility and internal document
construction. Original declarations, default presence, encounter order and
presentation are preserved under the agreed merge rules. Missing defaults yield
to explicit defaults; conflicting explicit values reject the operation.

Construction preloads sorted, deduplicated strings, then uses existing document
indexes. Small shared objects use bounded local matching; larger objects use a
temporary name-ID index. Shared nesting uses an explicit work stack after boundary
testing exposed excessive recursive Debug stack usage; one-sided subtrees reuse
the existing bounded copy walker. Both paths retain the same depth limit.

Publication is a non-failing move after result validation and destruction of
temporary borrowed state. Failure preserves occupied destinations and source
aliases. Allocation-failure sweeps cover separate, first-source, second-source
and both-source destinations, including diagnostic lifetime and cleanup checks.
Tests also cover mixed live/baked sources, independent output lifetime, scalar
and presentation rules, schema adoption/baking, instance defaults, shared
dependencies, and accepted/rejected depth boundaries.

Debug/Release x64/Win32 solution builds and ordinary `-t1` runs passed. Each run
reported 70,429 Schema checks with zero failures. One concurrent Debug x64 run
had an unrelated filesystem scan failure; its isolated rerun passed all suites.
Logs are `build/schema-merge-final-{debug32,release64,release32}.log` and
`build/schema-merge-final-debug64-alone.log`. The five CMake source-extraction
unit tests also passed.

Release x64 reverse-order growth fixtures took approximately 5.05, 22.05 and
99.88 ms for 128, 512 and 2,048 overlapping types, respectively. Measurements
include source/result resolution and exclude fixture parsing. The observed
roughly 4.4-4.5-fold time increase for fourfold input growth is consistent with
the intended linear/logarithmic construction paths; it is not a timing threshold
or a worst-case guarantee for existing resolver/cache internals.

Prepared instance/bulk aggregation was subsequently delivered in the milestone
above. Boundary conversion APIs and composition remain separate future work.

## Schema role attachment and construction

Implemented and validated on 17 October 2026, after the committed persistence
milestone below. This records the current implementation and review, not a new
commit checkpoint or user acceptance of the remaining aggregation design.

Instance and bulk data blocks now retain only document/payload storage. Separate
live/baked data roles carry schema associations. Initial attachment validates an
existing package or constructs missing binary data atomically. Checked switching
reuses representation and value-construction validation against the unchanged
payload, including changed defaults; stale retained descriptions can reject the
switch and reconciliation stays explicit. Trusted switching remaps type indices
without repeating compatibility proof and can restore access after schema loss.
Both modes preserve document handles, payload addresses/bytes and backing.

Resolved type/member/field and enum lookup uses indexes. Record/group queries
use temporary neighbour cursors with logarithmic fallback; switches reuse parent
record types, remapping walks source selections, and live baking copies directly
into final payload storage. Construction shares value admission and attachment
commit logic while avoiding redundant sibling-name and ancestry scans for known
valid sources and schema-driven builders. Native copies reuse string IDs or
sparse temporary mappings; whole baked promotion preloads both string domains
in lexical order. Ordinary editing and arbitrary readers retain checked paths.

Instance reconciliation compares and emits in one traversal, lazily creating
containers and required array prefixes. It preserves minimal declarations,
parent-snapshot inheritance and existing scalar semantics, then independently
verifies the generated declaration against the payload. Failure discards staged
output without changing the source role.

Debug/Release x64/Win32 solution builds and ordinary `-t1` runs passed after the
construction changes. Each run reported 64,789 Schema, 13,289 BakedDocument and
915 BundleRequests checks, with zero failures. Logs are
`development/construction-build-{dbg64,dbg32,rel64,rel32}.log` and
`development/construction-tests-{dbg64,dbg32,rel64,rel32}.log`. Regression coverage
includes sparse string IDs, reversed name order, checked arbitrary readers,
unready destinations, equal array gaps, deep nested changes, zero-size layouts
and allocation-failure rollback. Policy, diff and line-ending checks passed.
This documentation review adds no code changes and does not repeat that matrix.

The [runtime API](../schema/runtime-api.md#initial-attachment-and-schema-switching),
[data-model construction contract](../data_model/revised_data_model.md) and
[validation guide](../schema/validation.md) describe the current implementation.
Arbitrary-order distinct-string insertion can still shift a quadratic total of
sorted-index entries; selected copies do not preload unrelated strings. Live
instance edits still scan the record tail linearly. These costs and the separate,
unimplemented aggregation/composition work remain in the
[follow-on notes](../backlog/schema_follow_on.md).

## Schema instance and bulk data persistence

Completed, user-reviewed and committed through 14 October 2026. All eight stages
are complete, including the new-payload gap-zeroing follow-up in `dfe076d`.
`.bid` and `.bbd` files retain a baked description and its binary payload as one
contiguous persistence entity. `CBakedInstancesBlock` and `CBakedBulkDataBlock`
originally exposed contained specialised interfaces. The 17 October rework
separates schema-independent block storage from caller-owned typed roles and
adds atomic attachment and checked/trusted switching. See the current
[runtime contract](../schema/runtime-api.md#initial-attachment-and-schema-switching).
The existing local/Host asset lifetime model remains in use.

| Stage | Checkpoint | Delivered outcome |
| --- | --- | --- |
| 1 | `f709d79` | Bulk data naming standardisation across APIs, consumers, tests and documentation, without compatibility aliases. |
| 2 | `d043ce3` | Version 1 format validation, shared load options and layered diagnostics with bounded location lifetimes. |
| 3 | `26d28e5` | Completed-block ownership, local adoption/release and borrowed Host attachment with failure preservation. |
| 4 | `48454ef` | Live baking into independent, contiguous completed instance and bulk data blocks. |
| 5 | `bdedfa6` | All-or-nothing materialisation from live/baked descriptions, with caller-side parsing and construction orchestration. |
| 6 | `905ed1c` | Stripping through document promotion and rebaking, preserving payload bytes while allowing a smaller complete allocation. |
| 7 | `27d4f89` | Complete-buffer Host loading, admission and binary saving for loose files and bundle-backed entries. |
| 8 | `dfe076d` | End-to-end acceptance, maintained documentation and zeroing of newly created payload gaps. |

The separately reviewed data-model change `3fe1f2d` permits exact-capacity baked
document adoption; buffer size need not be an alignment multiple. Completed
schema files retain their required aligned payload base without importing bundle
entry padding or terminators into their logical file range.

Binary data remains authoritative, with opt-in retained-value comparison and
mandatory structural checks. Construction zeroes newly created leading and
inter-entry gaps while preserving copied record bytes, internal padding and
unused bits. Loading, saving and stripping preserve existing payload bytes.
Document navigation remains prefix-only while file access and persistence retain
the complete logical buffer. Explicit whole-bundle saving writes current bytes;
automatic bundle persistence and writes to bundle entries are not provided.

Validation checkpoints:

- After gap zeroing, fresh Windows solution builds and ordinary Core runs passed
  in Debug/Release on x64/Win32. Each reported 61,740 Schema checks and 883
  BundleRequests checks, with zero failures. Core log suffixes under
  `development/logical-roots/test-logs/` are `payload-gaps-dbg64.p10736.log`,
  `payload-gaps-dbg32.p81100.log`, `payload-gaps-rel64.p92576.log` and
  `payload-gaps-rel32.p92920.log`.
- The acceptance workflows cover both file roles through live bake, local
  release/Host admission, attachment, save and physical reload; JSON parsing and
  explicit materialisation; bundle creation, closure and retained sub-file saves;
  attachment/save failures, source/discard retention and disposal after accepted
  saves drain. Shared and dedicated worker routes run these cases. Nonzero-filled
  allocation tests cover new gaps, zero-byte entries, unchanged source and record
  bytes, and repeatable output.
- Debug x64 module lifecycle acceptance passed with one and two workers before
  the gap-only follow-up. Logs are in `build/lifecycle-runtime-e046bd84/` and
  `build/lifecycle-runtime-5d368a87/`, respectively. Stage 7 Host-worker acceptance
  passed bootstrap rejection and worker requests 0, 1, 2, 3, 9 and 128, recorded
  in `build/host-workers-47499310/`; its Core log suffix is
  `host-full-buffer-stage7.p89004.log`. These results were reused because the
  follow-up did not change Host service or lifetime code.
- Policy validation, diff whitespace, tracked line endings and documentation
  links passed. Counts and local ignored logs describe historical checkpoints;
  this documentation closeout does not claim new build, runtime or Linux results.

The completed persistence implementation plan is retired. Permanent contracts
and test coverage are maintained in the [schema specification](../schema/specification.md),
[runtime API](../schema/runtime-api.md#completed-data-blocks),
[validation guide](../schema/validation.md) and
[asset services](../assets/asynchronous_asset_services.md). Intermediate delivery
and validation detail remains in Git history.

The [aggregation revision](../schema/persistence-and-aggregation.md#separate-merge-and-aggregation-operations)
and [follow-on notes](../backlog/schema_follow_on.md#remaining-aggregation-and-composition-design)
remain active design and planning references. Definition merging, validated
schema switching, instance/bulk data aggregation and complete-document composition
were not delivered by this historical milestone. The subsequent schema rework
implements switching and direct final-payload copying for live baking;
aggregation and composition remain separate work.

## Bundle assets and virtual bindings

Completed and reviewed through 13 October 2026. Bundles provide one immutable,
128-byte-aligned allocation containing a baked directory and tightly packed file
extents, with a mandatory trailing zero for each file. Deterministic baking skips
empty files and unsupported entries and prunes empty directories; real query/read
failures reject the whole operation. Complete views support strict relative lookup
and document traversal without the live filesystem image.

Bake/load requests publish an ordinary retained asset and a read-only virtual
root together. The requesting thread instance owns original-ID disposal, while
any thread may use protected views. Retained records borrowing storage and accepted
operations protect the bundle through closure; independent conversion results
have independent lifetimes. Existing sub-files remain usable and savable after
closure. Final disposal releases storage and the root before notifying the owner;
outgoing requester packages retire their notification routes.

| Checkpoint | Delivered outcome |
| --- | --- |
| `61e9654` | File-size query distinguishing empty files from errors, and loading directly into caller storage. |
| `45aa731` | Resizable baked-document blocks, prefix views and complete-buffer adoption/persistence. |
| `56cc04d` | Bundle format validation and immutable direct views. |
| `ec09716` | Deterministic construction with direct file reads into final storage. |
| `27b00ad` | Separate collection move-assignment ownership fixes found during binding work. |
| `165191d` | Virtual bindings and stable directory-backed bake-source capture. |
| `0b20519` | Host storage ownership, retained-record accounting and deferred final disposal. |
| `04fa30c` | Public requests and I/O-worker integration. |
| `c81634e` | Runtime acceptance, persistence and lifecycle coverage. |
| `8c6e217` | Linux test portability corrections and Windows/Linux CI acceptance updates. |
| `ef7e939` | Requester management ownership and notification retirement for all departing packages. |
| `8c1fb1f` | Named build-failure diagnostics with captured original-cased source paths. |
| `df46e7c` | Admission from the live image's resolved bundle ranges, eliminating duplicate baked lookup. |
| `8d57ef1` | Conditioning-worker validation/preparation and transactional Host subtree installation without whole-image bake/promote. |

The final publication path preserves unrelated Host image changes made while
preparation is outstanding. Controlled tests cover failed dispatch, pending-work
shutdown and allocation rollback. Real worker and DLL fixtures cover cache reuse,
concurrent misses, refresh, sub-file saves after closure, independent conversions,
queued/inline batch borrowers, Executive replacement and Rendering-owned bundles.
Shader files are opaque test data; shader integration is separate consumer work.

Validation checkpoints:

- The six-stage acceptance passed Windows Debug/Release x64/Win32 builds and
  ordinary suites. All 32 lifecycle cases at that checkpoint passed Debug x64
  with one/two workers, Release x64 with two and Release Win32 with one.
- The Linux/CI follow-up passed GCC 13.3 and Clang 18.1 Debug/Release builds and
  all three CTest cases on Ubuntu 24.04 in WSL. Windows Debug x64 ordinary tests
  and Host-worker acceptance across all four Windows configurations also passed.
  Workflow checks passed locally; these results do not claim hosted CI execution.
- After the architectural corrections, Windows Debug x64 build and ordinary
  tests passed, including 1,839 FilesystemImage, 321 BundleService and 277
  BundleRequests checks (`bundle-conditioning-final`, process 68364). All 33
  lifecycle cases passed with one and two workers (`lifecycle-runtime-8582e4cb`
  and `lifecycle-runtime-7f408af8`), including execution-role checks. Linux GCC
  Debug build and all three CTest cases passed in `build/linux-texture`.
  Policy, diff and line-ending checks passed. The earlier full matrices were
  not repeated for the architectural corrections.

Counts and local ignored logs describe those checkpoints, not permanent APIs.
The completed implementation plan is retired. Maintained references are the
[bundle specification](../assets/bundle_specification.md),
[asset services](../assets/asynchronous_asset_services.md),
[filesystem image](../../development/README.md) and
[module lifecycle](../modules/asynchronous_module_lifecycle.md).
The debug-macro investigation remains separate work in the
[backlog](../backlog/engine_backlog.md#diagnostics-and-developer-tooling).

## Schema system and refinement

Completed through 4 October 2026. The [schema reference](../schema/README.md)
describes live/baked schema, instance and bulk data roles; shared resolution; faithful
C++17 declarations; binary loading/materialisation and capture; coordinated
instance editing; binary-authoritative reconciliation; embedded, external and
stripped baking; exact-type remapping; and optional unused-storage clearing.

The subsequent refinement removes quadratic record and descendant bookkeeping,
indexes baked entry lookup and physical-member traversal, uses ordered live
records directly, caches bulk data type bindings and shares compatibility checks.
Scalar decoding and document copying share implementations, while subtree copying
and whole-document promotion retain their distinct traversal policies. The file,
conditional/parameter and style passes clarify responsibilities without adding
allocation to read paths. General schema name lookup remained caller-managed at
that checkpoint; the [17 October rework](#schema-role-attachment-and-construction)
adds resolved name indexes and further construction improvements.

Representative checkpoints are `db82da7` (style/header closeout), `06962f7`
(physical traversal/query forwarding), `3e3e25c` (data-model integer presentation),
`80d7b4b` (canonical schema/data formatting) and `facc22a` (documentation/sample
alignment). Integer metadata survives live/baked document round trips; schema
output selects presentation by declared type and fills missing structure details
without rewriting the source.

The physical-order change passed Debug x64, Release x64 and Debug Win32 builds
and ordinary suites, with 20,003 schema checks per run; all eight generated
layout fixtures compiled for x86 and x64. The integer-output change passed full
Debug x64 and Release Win32 builds and suites, with 20,611 schema checks per run.
Policy validation, diff and line-ending checks passed. The final sample audit
also passed the full Release Win32 suite (`schema-doc-audit-rel32`, PID 82496),
including all 20,611 schema checks. These are recorded checkpoints, not new runs
for the documentation reorganisation.

The specification, API and validation guide replace the completed implementation
contract and dated question record; original discussions remain in Git history.
[Follow-on notes](../backlog/schema_follow_on.md) separate proposed expansion and
consumer work from current contracts. Their implementation scope remains to be
settled with the user.

## September consolidation: documents, storage, images and assets

The selected consolidation is complete, including the rendering follow-up on
21 September 2026. The committed checkpoints are:

| Checkpoint | Delivered outcome | Permanent reference |
| --- | --- | --- |
| `d1d804c` | Diagnostic-only accounting updates; explicit debug-log provisioning prevents recursive allocation during reporting. | [Memory](../memory/memory_subsystem.md), [debug service](../debug/debug_service_substrate.md) |
| `42a908d` | Uniform attribution observations and context replacement across containers and live documents. | [Memory infrastructure](../memory/memory_subsystem.md#container-facing-accounting) |
| `a75962f` | Byte-buffer-owned baked blocks, borrowed views constructed on demand, aligned loading and version-4 stored offsets. | [Baked format and storage](../data_model/baked_document_format.md) |
| `e60407f` | Shared document reports and the byte-view parser API, following grammar/policy migration and manual style review. | [Document references](../data_model/README.md) |
| `5b1282f`, `98ce708` | Image view, clipped drawing/copying, write masks, TGA configuration and incremental line rasterisation. | [Image view](../image/image_view.md) |
| `f74213f` | Raw/baked/JSON/TGA asset services, retained and one-shot operations, compact borrowed results and worker diagnostics. | [Asset services](../assets/asynchronous_asset_services.md) |

Accounting no longer makes valid allocation, deallocation or reattribution fail
solely because diagnostic totals disagree. Structural ownership and allocator
compatibility checks remain. The uniform interface distinguishes empty, coherent
and mixed backing independently of totals, performs fresh preflight and includes
unallocated children in context replacement. Tests cover modular accounting,
nonallocating diagnostics, nested aggregates, complete ownership and rejected
transfers. All four Debug/Release x64/Win32 builds and Core suites passed.
Recorded runs include `uniform-attribution-final-{dbg64,rel64,dbg32,rel32}`.

Baked-storage validation covered all stored offsets/reserved fields, exact extent
versus rounded capacity, immutable allocation-free borrowed views, rejected
adoption preserving both owners and direct aligned loading. Four configurations
passed 1,711 baked-document and 568 transfer checks at the version-4 checkpoint;
`baked-v4-{dbg64,rel64,dbg32,rel32}` records those runs. These counts are historical.

Parser/report consolidation completed both refactoring stages, recovery-array
retirement, parameter const and the manual style checkpoints (`8d5ca1d`,
`ab3d81f`). The final shared report/API and permanent grammar/report documentation
were committed in `e60407f`; superseded migration interfaces are not pending work.
Their builds, Core suites, policy and line-ending checks passed on all four
Windows configurations.

Image refinement passed 314,179 ImageView checks per configuration, including
extreme coordinates, midpoint behaviour, clipped transformed copies and masks.
Evidence: `build/image-view-style-final-dbg64.log` and
`build/image-view-style-{dbg32,rel64,rel32}.log`.

Asset integration passed 48 sequential and 32 concurrent Executive operations
per configuration. Tests cover retained/discarded inputs, source-preserving
failure outcomes, document policy, binary/JSON/TGA round trips, malformed data,
correlation and lifetime. Legacy client TGA messages and redundant catalogue IDs
were retired. The readability pass gave operations/scenarios explicit names and
expectations, separated published views from owned storage, and centralised
completion construction and dispatch. Existing data-model algorithms were unchanged.
Evidence: `build/asset-review-*`, `build/asset-retire-*` and the final Debug x64
`build/asset-defaults-dbg64-*` checks after the user's style pass.

## Asynchronous module lifecycle and asset disposal

Completed on 21 September 2026 in `491ce78`, after coordinator review and the
user's manual style pass.

- DLL load/bind and unbind/unload execute on the Host I/O worker. Both Host
  workers share handlers, allowing later optional coalescing.
- The Host starts without Executive code, binds asynchronously and then starts
  its thread. Ordinary requests receive acknowledgement and completion.
- Executive self-termination requests exit immediately and receives no reply.
  Self-unload shuts down; failed self-replacement logs an assertion and shuts down.
  Ordinary replacement failure leaves the service unavailable without rollback.
- Explicit disposal waits for accepted operations. Before unload, dependent
  retained assets trigger an assertion log and are disposed of while the DLL
  remains loaded. Unrelated assets survive replacement.

All twelve then-current lifecycle scenarios and Core suites passed in
Debug/Release x64/Win32. Debug x64 passed again after manual style review.
Evidence: `build/module-review-{dbg64,rel64,dbg32,rel32}-{lifecycle,tests}.log`
and `build/module-manual-style-dbg64-{lifecycle,tests}.log`.
The [module reference](../modules/asynchronous_module_lifecycle.md) documents
the protocol, failure handling and test entry points.

## Rendering stub and Executive-controlled startup

Completed on 21 September 2026 in `f91ded3`, after coordinator and user review.
`MorphicRendering` is a separate solution DLL. Its `render_vulkan_windows`
identity at that milestone was later consolidated as `render_vulkan`.
It provisions a module thread which parks until exit; no graphics API is implemented.
Vulkan is the primary first rendering target; DirectX remains deferred.

The normal Executive chooses and requests rendering, validates acknowledgement
then readiness and reuses an available matching renderer after Executive
replacement. Selector Executives can omit rendering. Missing/unavailable services
and invalid replies cause the normal Executive to request system shutdown.
The Host does not independently select or bootstrap a rendering implementation.

Rendering load completion includes successful thread startup; failed startup is
cleaned up on the I/O worker. Teardown observes terminal state before draining
final requests and retains the package/transports until accepted replies complete.
It then joins/destroys the package, cleans dependencies and dispatches unload.
Coordinator review identified and corrected the original package-lifetime gap;
a fixture proves deferred disposal and a final exit request complete before teardown.

Solution/four-role fixture builds, all 23 lifecycle cases and Core `-t1` suites
passed in Debug/Release x64/Win32. Core coverage includes 631 ErasedOwner checks
at this checkpoint, exercising the real Executive's readiness protocol and
failed initial submission. Identity, reuse, renderer-free selectors, startup
failures, disposal ordering and output contents are checked by the harness.
Policy, whitespace and line-ending checks passed. Development configuration and
non-Windows execution were not validated by these runs.

Evidence: `build/executive-rendering-lifecycle-{debug-x64,release-x64,debug-win32,release-win32}.log`
and corresponding `build/executive-rendering-core-*.log`; process tags are
`0d3b933a`, `87ef5f2e`, `91404227` and `68912f1c` respectively. Logs are local
ignored artifacts, not files guaranteed to exist in a fresh checkout.

## Development filesystem image

Implemented 22 September 2026, reviewed and accepted on 23 September with the
review corrections applied. The
[development reference](../../development/README.md) is the permanent contract.

- The Host I/O worker constructs the startup image before Executive bootstrap
  and supplies per-root refresh observations. The Host alone integrates and
  accesses the live document.
- Schema version 2 uses named roots and nested `content` objects, sparse cached
  asset IDs, inherited source/write bindings and forward-slash stored paths.
  Both log roots retain bindings without inventory; `test-output:` is inventoried.
- Logical-file reads validate inventory membership, writes validate destinations,
  and compatible loads can reuse retained IDs. Successful writes update inventory;
  disposal removes cache associations without removing the discovered file.
- A bounded FIFO queues root refreshes. Failed scans preserve the current image;
  write serials protect newer successful writes against older observations/loads.
  Workers borrow stable Host-owned paths or scan backing, not mutable image nodes.
- `package:/bin/` discovers adjacent DLLs for the running build, including test
  DLLs, without copying binaries or discovering the executable. Module loading
  resolves those logical names through the same image.

Windows Debug x64 validation during this stage passed the Core suite, including
157 filesystem-image checks, the Executive's 48 sequential and 32 concurrent
asset operations plus 12 filesystem operations, and all 23 lifecycle cases.
Recorded tags include `filesystem-const` for the Core suite,
`filesystem-review-updates` for the final enum/logging/filter follow-up's Host
acceptance checks, and `lifecycle-8aa3eeb9` for the lifecycle run. The follow-up
solution build, policy checks, filter mappings and line-ending checks also passed.
Logs are local ignored artifacts, not guaranteed files in a fresh checkout.
These runs do not establish Linux/Android support or a new four-configuration
validation matrix.

The former deferred filesystem design is fully superseded. Only
[limitations and useful future considerations](../backlog/filesystem_asset_mapping.md)
remain; no shared-immutable-image publication or reader-retirement system is
required to finish this implementation.

## Consolidation documentation closeout

Completed plans, review notes and task handoffs have been retired from the active
documentation. Their final contracts are in the memory/debug, data-model, image,
asset and module references above. This record retains completion evidence;
the original deliberations and intermediate validation remain in Git history.

Future ideas were preserved in [deferred design](../backlog/consolidation_deferred_design.md),
[filesystem considerations](../backlog/filesystem_asset_mapping.md), the engine
backlog and schema documents. The former job-framework proposal has since been
superseded by the [module batch runners](../modules/batch_runners.md); its original
discussion remains in Git history. The deferred record distinguishes implemented
services, open choices and rejected alternatives, including accounting-period reset,
bundled outputs, possible baked save-game mutation and message-validity proposals.
The earlier documentation cleanup did not mark any future system implemented.

## Memory Ownership And Accounting

The memory refactor established one system-wide ownership and accounting model:

- raw allocation, alignment conditioning, accounting, and attribution live in
  `memory`;
- `CMemoryToken` provides relocatable and stable storage ownership;
- `CMemoryView` and `CMemoryConstView` provide bounded non-owning views;
- legacy tokens, views, stable-storage wrappers, allocation-context layers,
  and compatibility namespaces were removed;
- owning containers compose tokens rather than calling allocators directly.

Conditioned byte accounting, direct-storage statistics, and compatible-context
reattribution were completed across vectors, FIFOs, instances, byte and string
containers, POD slot containers, stable collections, and erased ownership.
Compound owners perform one aggregate accounting transaction for all direct
tokens. Storage-owning transports retain fixed attribution and remain
non-reattributable.

The explicit `bit_ops` casting audit was also completed. Platform-width
operands now select the platform-width overload directly; explicit conversions
remain only at genuine representation boundaries.

Permanent reference:

- `docs/memory/memory_subsystem.md`
- the individual documents under `docs/containers/`

Validation:

- Debug x64 and x86 solution builds;
- full memory, container, and transport suites on both architectures.

## Non-Virtual Slot Sandwich

The ordered and unordered slot families were converted from virtual
responsibility hooks to direct compile-time collaboration:

```text
container slot-backing base
    -> TOrderedSlots or TUnorderedSlots
        -> public container facade
```

The completed conversion covers:

- `TPodOrderedSlots`;
- `TPodUnorderedSlots`;
- `TOrderedCollection`;
- `TUnorderedCollection`;
- ordered and unordered test harness adapters.

Callback visitation and callback-specific locking were removed. Direct empty
traversal replaced the useful callback capability. Packing, metadata
copy/clone operations, traversal helpers, reserve behavior, ordered external
payload handling, equal-key stability, and stable collection object addresses
were preserved.

The public facade now owns user-facing lifetime orchestration while the middle
slot manager collaborates directly with protected lower backing
responsibilities. No container-hierarchy vtable or runtime responsibility hook
remains.

Permanent reference:

- `docs/containers/slots/TOrderedSlots.md`
- `docs/containers/slots/TUnorderedSlots.md`
- `docs/containers/TOrderedCollection.md`
- `docs/containers/TUnorderedCollection.md`

Validation:

- warning-free Debug x64 and x86 builds;
- focused memory, container, and transport suites;
- complete ordered deletion-permutation coverage during the conversion.

## System Identity And Erased Ownership

System transported types and generated IDs were moved into a system-owned
registration model. Owning-erasure eligibility remains separate from ordinary
type registration.

`CErasedOwner` replaced the former virtual typeless ownership mechanism with a
closed-world move-only carrier:

- one registered payload is placement-constructed in token-owned storage;
- component-local immutable tables dispatch erased destruction, accounting,
  validation, and reattribution for eligible SYSTEM and LOCAL payloads;
- SYSTEM operations are compiled into each receiving component while LOCAL
  operations remain only in their defining component;
- no vptr, callback, deleter, or executable operation pointer crosses a module
  lifetime boundary;
- payload addresses remain stable across owner and transport movement;
- the 32-bit mounting-point hazard mask is distinct from ownership,
  provisioning, and access metadata.

Payload and carrier reattribution use one aggregate accounting transaction.
`CErasedOwnerTransport` composes the non-reattributable `TOwning` primitive and
implements the fixed attribution chain:

```text
posting owner -> transport owner -> optional recipient owner
```

The inline POD message carrier moved to `system/erased_pod.hpp` as
`TErasedPod`, remaining distinct from non-POD erased ownership. Host prototype
messages and the owning host channel were migrated to the system surfaces.

Permanent reference:

- `docs/system/erased_owner.md`
- `docs/memory/memory_subsystem.md`

Validation:

- Debug x64 and x86 builds;
- 106 erased-owner and wrapper checks on each architecture;
- full test runs on both architectures.

## Bounded MPMC Transport

The fixed-capacity Rigtorp-style MPMC transport foundation is complete:

- `TMpmcIndexRing` supplies the status-free index-ring primitive;
- `TMpmcArenaTransport` supplies lifecycle-aware arena transport;
- `TMpmcJobTransport` composes work and feedback transports;
- scoped reserve and acquire helpers complete the mandatory protocol pairs;
- the family performs no live allocation and requires no internal attribution.

The implemented lifecycle is `open -> closing -> closed`, with forced
`shutdown`. Outstanding-index accounting prevents orderly closure while work
is reserved, published, or acquired but not yet recycled.

Legacy endpoint and bundle wrappers were consolidated into their core transport
headers and temporary compatibility shims were removed as part of the same
transport cleanup period.

Permanent reference:

- `docs/threading/transports/TMpmcTransport.md`
- the other documents under `docs/threading/transports/`

Validation:

- Debug x64 and x86 solution builds;
- focused capacity, sequence, wrap, lifecycle, protocol, helper, and
  composition coverage;
- full x64 and x86 test runs.

## Code Policy Validator

`MorphicPolicyValidator` established a standalone C++17 development-tooling
boundary for early detection of codebase policy violations. The validator may
use the C++ standard library and exceptions; production engine projects remain
C++17 with exception handling disabled.

The completed first version provides:

- a lightweight lexical and token-aware scanner rather than regex-only checks
  or a full C++ parser;
- deterministic diagnostics and project/configuration/platform-qualified
  reports under `logs/policy_validator`;
- central, versioned policy configuration for source scopes, include
  permissions, approved allocation infrastructure, identity surfaces, and
  project classifications;
- narrow source suppressions that name one rule, require a reason, and fail
  when malformed, unknown, unmatched, or stale;
- error-level checks for ordinary naked `new`, misplaced global/system identity
  declarations and registrations, disallowed or unresolved includes, direct
  `windows.h` use, required source roots, and Visual Studio language and
  exception settings;
- warning-level findings for context-dependent placement construction, raw
  allocation primitives, ambiguous includes, apparently unused `<new>`
  includes, macro-expanded includes, and unsupported project conditions; and
- distinct exit behavior for policy violations and invocation or policy-loading
  failures.

Host, Executive, and MorphicTests reference the validator project and invoke it
before compilation. Core remains per-consumer shared source through
`MorphicCore.vcxitems`; the validator is a separate executable rather than an
engine runtime component. MorphicTests follows engine compilation restrictions
because it compiles Core, while the validator retains its explicit tooling
exemption. SuiteUTF is deliberately outside the first-version policy scope.

Before enforcement was enabled, the existing source and project baseline was
cleaned rather than silently grandfathered. Direct include ownership was made
explicit, unused or misplaced dependencies were removed, accidental unqualified
`size_t` use was corrected, and every applicable engine configuration now
selects C++17 with exception handling explicitly disabled. Direct
non-project/system includes are governed by a reviewed default-deny allowlist.

The validator is deliberately a policy canary rather than a semantic proof. It
does not preprocess source, build an abstract syntax tree, or infer arbitrary
manual reimplementations of policy-governed macro surfaces. Global/system ID
enforcement is intentionally macro-oriented, and context-dependent risks remain
warnings until a conclusive check is justified.

Permanent reference:

- `docs/project/policy_validator.md`
- `policy/morphic_policy.cfg`

Validation:

- clean direct validator runs with zero errors and warnings;
- successful Debug, Development, and Release solution builds for x64 and x86;
- successful standalone-project dependency ordering, with the validator built
  before its pre-compilation invocation;
- exercised positive, negative, suppression, policy-loading, include-resolution,
  allocation, identity, and project-configuration cases.

## Earlier foundation and integration milestones

The interim backlog's completed foundation work comprises FIFO full-buffer
policy and non-overwriting `try_add`, cross-platform high-frequency timing,
and the native threading primitive/start layer (hardware count, mutex,
wait/wake, semaphore, two-phase parking gate and thread creation/trampoline).
Windows received basic functional validation; formal cross-platform primitive
stress remains open.

The codebase also now has the system identity/name registry, thread packages
and context, a debug service, and asynchronous TGA loading/conditioning backed
by type-erased Host assets. These are existing infrastructure for consolidation;
their presence does not complete the broader Host lifetime/provisioning design.
The project attribution policy is documented. Follow-on audits and validation
remain in the [engine backlog](../backlog/engine_backlog.md).

## First replacement data-model pipeline

Completed September 2026. The former stage-by-stage roadmap is replaced by this
outcome record. The implementation is available and tested; the parser/reporting
contract and ownership API were subsequently consolidated as recorded above.
The original migration history remains in Git; current contracts are linked below.

- Added container observations for valid stable-string counts and O(1)
  slot-to-key conversion without new persistent state.
- Replaced continuously maintained tree/string reachability counts with an
  iterative on-demand analysis. Kept framework memory accounting.
- Reduced live nodes from 64 to 40 bytes using role-specific direct slot links
  and monotonic public keys. Names imply object entries for every payload type;
  the root is an implicit object and explicit live aggregates have empty names.
- Completed detachment, payload extraction/attachment and payload erasure,
  plus public recovered arrays with anonymous children and unrestricted
  cardinality. Recovery composition uses ordinary live operations.
- Defined the incompatible `MBD2` version-1 format: a 32-byte header, 32-byte
  value records, dense child ranges and separate sorted name/value string tables.
  Added a checked immutable view and one-allocation, 32-byte-aligned owning block.
- Implemented baking and promotion in a separate translation layer using
  external scratch and staged publication. Added topology/encoding validation,
  direct queries and failure cleanup without a public mutable baked builder.
- Updated text ingestion for bounded embedded NULs, modified-NUL normalization
  and output-encoding/transformation reports (`b6b003b`).
- Added the baked-view writer with strict/Morphic output, ASCII/layout options,
  reversible names/recovery, numeric conversion and iterative traversal (`5b76a80`).
- Added the shared lexer and syntax-only structural pass, preserving absent
  versus present-empty input through CStringView (`4439900`).
- Added relaxed live parsing, construction reports, recovery-wrapper decoding,
  ordered duplicate recovery and singleton normalization (`8ef6e83`, `76027e5`).
- Added the initial baked ownership transport bridge and SYSTEM payload,
  including shell/block accounting and Host repository disposal (`96ed4a2`).

Recorded validation: all ordinary suites passed in Debug and Release on x64
and Win32 at the final parser and transfer checkpoints. These included 15,081
parser checks and, at the transfer checkpoint, 213 transfer checks per build.
Coverage includes numeric boundaries, Unicode/NULs, malformed structure,
reserved names, nested recovery, deep iterative walks, meaningful allocation
failures, unchanged transfer bytes/addresses, rejection and disposal. Policy
and line-ending checks passed. These are historical results, not a claim that
the entire parser refactor has been validated. Its completed first stage is
recorded separately below.

Current reference:

- [Semantic specification](../data_model/revised_data_model.md).
- [Physical format](../data_model/baked_document_format.md).
- [Design rationale](../data_model/data_model_design_notes.md).

Direct file persistence and the full Executive-controlled document run were
not implemented at this historical checkpoint. They are now covered by the
asset-service milestone above and committed as `f74213f`.

## Linter and shared diagnostic refactor: stage 1

Implemented, validated and reviewed on 11 September 2026. This completes the
first review stage of the parser migration. Its final contract is in
[document parsing](../data_model/document_parsing.md).

- Normalized exact modified NUL and valid CESU-8 pairs through SuiteUTF to
  canonical UTF-8; undefined CP1252 bytes now fail without replacement.
- Added grouped source findings while retaining linter aggregate statistics,
  newline-form observations, NUL provenance and abandoned UTF-8 evidence.
- Unified linter, structural and parser locations through `CTextLocation`, with
  explicit availability and 1-based line/code-point coordinates in emitted UTF-8.
  Removed public diagnostic byte offsets and preserved prospective failure
  locations when partial output is discarded.
- Added composed ingestion that normalizes all supported source line breaks to
  LF and propagates linter failures directly into parser diagnostics, leaving
  structure unexamined and the destination unchanged.
- Preserved absent versus present-empty input through direct view overloads,
  without byte-to-string view conversion. Consolidated internal linter state and
  source-cursor handling, and completed helper-order and formatting review.

Validation: Debug/Release solution builds and ordinary tests passed on x64 and
Win32, including 561 TextLinter, 1,247 DocumentStructure and 15,150 DocumentParser
checks per configuration. Coverage includes malformed encodings, fallback
provenance, every line-break form, output-relative positions, empty/absent views,
allocation/publication failures and aliased sources. Policy checks reported zero
errors and warnings with the existing negative-test suppression. A Debug x64
build also passed after the final helper-definition reorder; diff and line-ending
checks passed.

The [semantic specification](../data_model/revised_data_model.md) describes these
implemented contracts. At this checkpoint, stage 2 remained unimplemented.
The subsequent infrastructure and completed parser/policy migration are
recorded in the later checkpoints above and below.

## Document-model infrastructure: first stage-2 slice

Completed and reviewed on 12 September 2026. Live and baked documents now
support native empty names, object/array root kinds and per-string newline
escaping metadata. Public collision extension uses ordinary arrays; ordinary
insertion and renaming still reject duplicate names. Writing normalizes line
breaks to LF and strict JSON overrides newline suppression.

Live and baked values share a 16-bit flag encoding for integer metadata, name
presence and newline suppression. Baking and promotion transfer those flags
together. Explicit reserved fields preserve the 40-byte live and 32-byte baked
records; the baked format is version 2.

Debug/Release builds and ordinary tests passed on x64 and Win32, including
allocation-failure coverage and complete integer-metadata round trips. Debug
x64 passed again after the final layout/style review. Parser grammar, findings,
caller policy and recovery retirement were completed subsequently; see
[document parsing](../data_model/document_parsing.md) for the final contract.

## Retired v1 reference archive

The September 2026 cleanup assessed the old live model, mutable baked builder,
writer, design document and tests. They are outside the build and describe an
incompatible representation and superseded recovery-envelope/checksum rules.
Current specifications and replacement tests cover the retained behaviour;
the archive is no longer needed as an active implementation reference.

The 14 files formerly under `graveyard/data_model_v1_2026-09-01` were relocated
unchanged to the local, ignored
`not_for_redistribution/graveyard/data_model_v1_2026-09-01` directory. The latter
is not part of distributed checkouts. Historical versions remain available in
Git, including the tree at `96ed4a2` before relocation.
