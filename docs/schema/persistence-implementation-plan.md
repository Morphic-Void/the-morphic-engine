Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   persistence-implementation-plan.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   14 Oct 2026

# Instance and bulk data persistence implementation plan

## Status and scope

This plan translates the agreed [revision](persistence-and-aggregation.md) into
eight implementation and review stages. Stages 1-4 are implemented and user-reviewed;
stage 5 is implemented for review, with validation recorded below. Stages 6-8
have not started. Work stays in the shared
checkout; stage boundaries do not imply automatic commits, branches or separate
implementing chats. Further implementation and commits require user instruction.

Deliver `.bid` and `.bbd` persistence, owning specialised blocks, live baking,
all-or-nothing materialisation, byte-preserving stripping, generic Host full-buffer
persistence, diagnostics and consistent bulk data naming. Caller-supplied schemas
remain separately owned. `.btd` remains an ordinary types-only baked document.

Exclude aggregation, collision policies, complete document composition, schema
signatures, persisted resolved lookups, partial materialisation, automatic bundle
persistence, writes to bundle entries and combined asynchronous construction
requests. Explicit whole-bundle saving writes its current complete bytes.
Direct construction into final allocation remains a later optimisation.

## Review sequence

| Stage | Deliverable | Depends on | Review emphasis |
| --- | --- | --- | --- |
| 1 | Bulk data naming standardisation | None | Mechanical completeness; unchanged behaviour. |
| 2 | Format validation and diagnostic vocabulary | 1 | Bounds, format rules and useful failure detail. |
| 3 | Completed block ownership and attachment | 2 | Lifetimes, atomic failure and specialised access. |
| 4 | Live baking into completed blocks | 3 | Reuse of output semantics; contiguous publication. |
| 5 | Materialisation from live/baked descriptions | 4 | All-or-nothing construction; independent source lifetime. |
| 6 | Stripping existing completed blocks | 5 | Metadata preservation and exact binary preservation. |
| 7 | Host full-buffer load, admission and save | 3 | Consistent owned/borrowed extents and existing asset lifetime. |
| 8 | End-to-end acceptance and documentation completion | 1-7 | Complete workflows and regression evidence. |

Use this order by default. Stage 7 is independently implementable after stage 3,
but there is no need to interleave its review with construction changes. Each
stage must build, carry its relevant tests and be reviewable before starting the
next. A review unit is not a time estimate. If either role needs substantially
different machinery, split that stage into instance and bulk data review units
without changing the agreed interfaces or widening scope.

## Concrete API and internal boundaries

Add public `baked_instances_block.hpp` and `baked_bulk_data_block.hpp` alongside
the existing role headers, with corresponding implementation files. Put shared
options and diagnostics in `data_block.hpp`; keep common format parsing and
assembly helpers private to schema implementation. The block headers and
attachment APIs, live bake overloads and description materialisation now exist;
stripping interfaces remain planned. The public
blocks do not depend on Host services.

Use `SDataBlockLoadOptions { bool compare_embedded{ false }; }`,
`SInstancesBlockDiagnostic` and `SBulkDataBlockDiagnostic`. Following stage 1, the
latter contains `SBulkDataDiagnostic`; the former contains `SInstanceDiagnostic`.
Select the following common diagnostic representation:

- `EDataBlockStage`: `none`, `preconditions`, `format_validation`,
  `schema_binding`, `specialised_loading`, `description_preparation`,
  `materialisation`, `document_promotion`, `document_bake`, `assembly`.
- `EDataBlockReason`: `none`, `destination_not_empty`, `invalid_input`,
  `invalid_option`, `invalid_document`, `unsupported_document_version`,
  `missing_property`, `unknown_property`, `invalid_property_type`,
  `wrong_content_type`, `unsupported_format_version`, `invalid_alignment`,
  `size_overflow`, `storage_limit`, `file_length_mismatch`, `nonzero_padding`,
  `schema_unavailable`, `schema_binding_failed`, `specialised_failure`,
  `allocation_failed`, `description_preparation_failed`,
  `document_promotion_failed`, `document_bake_failed`, `assembly_failed`.
- `EDataBlockLocationKind`: `none`, `source_live_node`, `source_baked_value`,
  `file_byte`. `SDataBlockLocation` contains this discriminator, `CNodeKey`,
  `CBakedValueIndex` and a `std::uint64_t` byte offset. Only the field selected
  by the discriminator is meaningful; other fields stay default-initialised.
- `SDataBlockDiagnostic` contains `stage`, `reason` and `location`. Each
  specialised block diagnostic contains this common diagnostic as `block` and
  its existing role diagnostic as `role`. A nested role failure uses
  `specialised_failure`, preserving its precise role reason.

Locations refer to the caller-supplied source document or complete source file.
Keep nested role occurrences only when they have that same valid source
provenance. Clear unmappable temporary-output locations before cleanup. A failure
without a reliable location is valid; diagnostics never retain temporary storage.
Report a specific resource failure only when the underlying API establishes it.
Do not expand generic document translation diagnostics solely for this work.

All fallible block operations return `bool`, are `[[nodiscard]]` and `noexcept`.
Use these argument orders, with explicit overloads rather than a new public
schema template abstraction:

| Operation | Arguments |
| --- | --- |
| `adopt_local` | `CBakedDocumentBlock&&`, schema reference, `const SDataBlockLoadOptions&`, corresponding diagnostic reference |
| `attach_asset` | `CAssetId`, `CByteConstView`, schema reference, `const SDataBlockLoadOptions&`, corresponding diagnostic reference |
| `release_local` | Empty `CBakedDocumentBlock&`; retains its agreed Boolean precondition result without adding diagnostics |
| Live role `bake`, const | Corresponding block reference, destination schema reference, `EDataOutputForm`, corresponding block diagnostic reference |
| Block `materialise` | Const description reference, schema reference, `EDataOutputForm`, corresponding block diagnostic reference |
| Block `strip_to`, const | Corresponding destination block reference, destination schema reference, corresponding block diagnostic reference |

Schema references have `CBakedSchema&` and `CLiveSchema&` overloads. Description
references have `const CLiveDocument&` and `const CBakedDocument&` overloads;
materialisation therefore has four combinations per block. Preserve existing
lower-level bake and materialisation APIs. Observation, const role access,
`clear()`, non-copyability and non-movability follow the revision unchanged.

## Stage 1: naming standardisation

- [x] Apply the revision's supporting-type rename table, including diagnostics,
  reasons, entry views, handles and document queries. Inventory corresponding
  helper names such as handle access, and domain-specific functions or variables.
- [x] Update repository consumers, tests and maintained documentation together.
  Use `BulkData`/`bulk_data` for domain identifiers and "bulk data" for domain
  prose. Introduce no compatibility aliases. Keep generic bulk-copy terminology,
  persisted `data` keys, `.bbd` and content identifiers unchanged.
- [x] Search for stale identifiers and review deliberate exceptions. Keep this
  diff mechanical: no persistence logic or unrelated formatting changes.

Primary files: `core/schema/baked_bulk_data.*`, `live_bulk_data.*`,
`document_query.*`, other schema consumers and `Schema_test_suite.cpp`.
Completion evidence: solution build, ordinary Core tests, stale-name audit and
updated documentation references. No new tests are needed solely for a rename.

Implemented for review on 14 October 2026. Supporting types, internal helpers,
construction modes and schema test names/variables use the agreed convention.
The source-token audit verifies that non-comment changes are exclusively
identifier substitutions, preserving literals, expressions and fixture content.
Old public identifiers remain only in the revision's migration table. Generic
transport/copy terminology and the test fixture property named `bulk` are
deliberately unchanged. No compatibility aliases or persistence behaviour were
introduced.

Validation: Debug x64 solution build and all ordinary Core tests passed with
`Schema: 20611 passed, 0 failed`, using:

```powershell
.\tools\invoke_sandbox_build.ps1 -MSBuildPath 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' -RunTests -TestMode 1 -LogTag bulk-data-naming-stage1
```

The successful command ran outside the sandbox after both installed MSBuild
versions encountered `LNK1101` for `MSPDB140.DLL` in the sandbox. No toolchain or
build-script changes were made. Logs use suffix
`bulk-data-naming-stage1.p91376.log` under
`development/logical-roots/test-logs/`. Line-ending policy, diff whitespace and
documentation link checks passed. Stage 1 was reviewed and approved for commit
by the user on 14 October 2026.

## Stage 2: format and diagnostics

- [x] Add the common declarations above and private bounded format validation.
  Return document and payload slices only after safe extent calculations.
- [x] Enforce version 1 identifiers, unsigned metadata, strict root members,
  section selection, `stripped` type, base alignment, exact file length, zero
  container padding and the complete-file 2 GiB ceiling. Preserve the existing
  baked encoding and reserved-header rules. Typed entry validation stays in the
  existing roles. Do not treat a zero-byte payload as missing data.
- [x] Define format checks in deterministic order: safe header bounds and
  encoding, root metadata and required section, checked payload placement and
  exact extent, then padding. Report unavailable details conservatively.
- [x] Add independently assembled fixtures and malformed variants to the Core
  schema tests; avoid relying exclusively on the new writer to test its reader.

Review cases: both file kinds; empty sections; zero-byte payloads; property type
and version errors; truncation/extra bytes; alignment and arithmetic boundaries;
nonzero padding; unknown root properties; wrong section/content kind. Exercise
limit arithmetic without allocating multi-gigabyte fixtures.

Implemented for review on 14 October 2026 in `data_block.hpp` and internal
`data_block_format.hpp/.cpp`, with focused cases in
`SchemaDataBlock_test_cases.cpp` called by the existing Schema suite. Header and
metadata validation publishes no partial views; typed entry checks remain for
the existing roles at attachment. No stage 3 owner or Host changes are included.

Validation: Debug x64 solution build and all ordinary Core tests passed with
`Schema: 53970 passed, 0 failed`, using:

```powershell
.\tools\invoke_sandbox_build.ps1 -MSBuildPath 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' -RunTests -TestMode 1 -LogTag data-block-format-stage2
```

The command ran outside the sandbox because of the previously established PDB
toolchain limitation. Logs use suffix `data-block-format-stage2.p90836.log` under
`development/logical-roots/test-logs/`. Line-ending policy, diff whitespace,
documentation links and Visual Studio XML checks passed. Visual Studio item and
filter files retain their existing CRLF, encoding and final-newline state.

## Stage 3: ownership, attachment and release

- [x] Implement both block classes with a contained existing role, local complete
  owner or borrowed Host range/ID, and the revision's observation methods.
- [x] Validate and initialise against stable bytes before consuming adopted
  ownership or publishing an attached block. Use a narrow internal mechanism for
  committing prepared role state; do not add public role movement or rebinding.
- [x] Implement ordered clear/destruction and local release. No Host messages,
  extra asset references, hidden disposal or storage resizing after publication.
- [x] Preserve lower-level diagnostics, with valid source locations, including
  schema-unavailable, unknown-type, range/overlap and comparison failures.

Review cases: live and baked schema overloads, successful local adoption, borrowed
attachment, occupied destination rejection, failed-adoption owner preservation,
release into empty/occupied outputs, destruction order and schema invalidation.
Verify bytes remain savable/releasable when typed readiness is lost. A local
borrowed fixture can test attachment mechanics; real Host lifetime is stage 8.
Confirm opt-in comparison and acceptance of payload gaps/trailing storage.

Implemented for review on 14 October 2026 in `baked_instances_block.hpp/.cpp`
and `baked_bulk_data_block.hpp/.cpp`. The shared private attachment helper
validates the envelope and prepares the existing specialised role before
publishing any ownership or binding. A narrow friendship reuses each role's
existing private state-transfer method. Public roles remain non-movable.

The extended independent fixtures exercise both schema overloads, local
ownership and repository-backed borrowed ranges, failure preservation, retained
diagnostic locations, opt-in comparison, schema destruction, and allocation
failure cleanup. An observed deallocation also verifies that the schema binding
is released before local file backing is freed.
Construction and Host service changes remain in later stages.

Validation: Debug x64 solution build and all ordinary Core tests passed with
`Schema: 54470 passed, 0 failed`, using:

```powershell
.\tools\invoke_sandbox_build.ps1 -MSBuildPath 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' -RunTests -TestMode 1 -LogTag data-block-ownership-stage3
```

The command ran outside the sandbox for the established PDB toolchain
limitation. Logs use suffix `data-block-ownership-stage3.p75396.log` under
`development/logical-roots/test-logs/`. Diff whitespace, source line endings,
documentation links and Visual Studio XML checks passed. Item/filter CRLF,
encoding and final-newline state are preserved.

## Stage 4: completed blocks from live baking

- [x] Add the live-role bake overloads, reusing existing output preparation and
  preserving embedded/external/stripped semantics and source binary snapshots.
- [x] Add persistence metadata and assemble one aligned final allocation with
  zero container padding; attach the destination role only after storage is final.
- [x] Share checked sizing/assembly code between both roles without merging their
  specialised semantics. Keep source roles unchanged and failure transactional.

Initial storage may include a temporary live description, baked prefix, prepared
payload and final complete allocation. Account for those simultaneous owners and
role scratch rather than claiming a single-allocation peak. Enforce checked
arithmetic and the final-file ceiling; do not invent a separate arbitrary memory
budget. Allocation failure cleans up private state. Copying into final storage is
permitted; direct emission into it is not part of this stage.

Review cases: both schema kinds and all output forms, empty and zero-byte content,
instance specialisation snapshots, bulk data counts, alignment and stable final
views. Reattach completed bytes independently and compare payloads against the
existing lower-level output. Test reachable failure paths and allocation failures
where existing test facilities support deterministic injection.

Implemented for review on 14 October 2026. The new live-role overloads reuse
`prepare_output()` and share `data_block_construction.hpp/.cpp` for metadata,
checked sizing and final assembly. Final attachment uses the stage 3 adoption
path only after storage is complete. Preparation diagnostics retain source live
locations; final-attachment diagnostics clear temporary-output locations.

Tests cover both source/destination schema kinds, all three forms, empty and
zero-byte output, edited instance snapshots and specialisations, bulk data counts,
byte-for-byte comparison with existing baking, source independence and copied-file
reattachment. Failure cases cover preconditions, unavailable/incompatible schemas,
unrepresentable embedded values and allocation failures throughout construction.
Validation: the final Debug x64 solution build and all ordinary Core tests
passed with `Schema: 57188 passed, 0 failed`, using:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\MorphicEngine.sln /t:Build /m:1 /p:Configuration=Debug /p:Platform=x64 /v:minimal
.\build\bin\x64\Debug\MorphicTests.exe -t1 --log-tag=data-block-bake-stage4-final
```

Validation ran outside the sandbox for the established PDB toolchain limitation.
Logs use suffix `data-block-bake-stage4-final.p30240.log` under
`development/logical-roots/test-logs/`. Diff whitespace, source line endings,
documentation links and Visual Studio XML checks passed. Item/filter CRLF,
encoding and final-newline state are preserved.

Following review, confirmed that `CByteBuffer` uses memory-token stride 1.
Generic baked adoption now accepts exact byte capacity, retaining base alignment
and bounded document validation. Stage 4 assembly requests exact file-size
capacity. Physical allocation conditioning remains the memory context's concern;
bundle separator/alignment rules remain in the bundle layer. Regression tests
cover 114-, 129- and 259-byte capacities at 32- and 128-byte base alignment,
both adoption forms, unchanged attribution and clean release. The updated Debug
x64 solution build and all ordinary Core tests passed:
`BakedDocument: 6304 passed, 0 failed` and `Schema: 57260 passed, 0 failed`.
The test command was
`MorphicTests.exe -t1 --log-tag=data-block-exact-capacity`; logs use suffix
`data-block-exact-capacity.p25500.log`. Bundle and bundle-builder suites also
passed with their existing alignment and separator rules.

## Stage 5: materialisation from descriptions

- [x] Implement the four description/schema overload combinations per block,
  preparing private document storage and constructing the complete payload using
  existing defaults, inheritance and complete-record rules.
- [x] Apply output form after consuming required values, then reuse stage 4's
  metadata/assembly path. Do not patch caller documents or bundle bytes.
- [x] Keep JSON parsing outside the block API. Demonstrate explicit parse/load,
  caller-side materialisation, and optional later admission/save. No partial
  binary repair or combined asynchronous workflow is introduced.

Review cases: live and baked descriptions, values/defaults sufficient for complete
construction, insufficient records, stripped descriptions requiring missing
binary, all output forms and zero-byte types. Destroy the source after success
and continue using the result. Failures leave source and destination contracts
intact and never return handles into deleted preparation documents.

Implemented for review on 14 October 2026. Each block exposes the four explicit
overloads and reuses shared private construction support. Live input is baked
privately; baked input is generically promoted and re-baked privately. Existing
baked-role materialisation supplies the complete payload and publishes reserved
locators only in that private document. Its failures map occurrences back to
the original tree by child ordinals without allocating diagnostic scratch.

Successful preparation promotes only the selected role section. Bulk data
counts are made explicit before removing arrays; instance declarations survive
embedded/external output and are removed for stripped output. Assembly replaces
source persistence metadata and publishes the complete block transactionally.
Valid offsets, including gaps, retain their lower-level materialisation meaning.
Required reserved locator fields and the existing rejection of stripped input
are unchanged. No live-role promotion or binary-to-literal reconstruction is
needed. Source descriptions and their asset associations are untouched.

Coverage includes the full overload/output matrix, defaults, nested inheritance,
complete/incomplete bulk records, missing/empty sections, zero-byte records,
inferred counts, source byte preservation and destruction, comparison-enabled
reattachment, diagnostic source locations and rejected allocations throughout
preparation, materialisation, assembly and attachment. The runtime API documents
explicit parsing followed by construction and optional later owning transfer;
Host persistence and end-to-end admission/save remain stages 7 and 8.

Validation: Debug x64 solution build and all ordinary Core tests passed with
`Schema: 59452 passed, 0 failed`, using:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\MorphicEngine.sln /t:Build /m:1 /p:Configuration=Debug /p:Platform=x64 /v:minimal
.\build\bin\x64\Debug\MorphicTests.exe -t1 --log-tag=data-block-materialise-stage5-check
```

Validation ran outside the sandbox for the established PDB toolchain limitation.
Logs use suffix `data-block-materialise-stage5-check.p53824.log` under
`development/logical-roots/test-logs/`. The solution policy validator reported no
errors or warnings. Diff whitespace, tracked line endings and documentation
links passed. No project registrations, layout or generator changes were needed.

## Stage 6: stripping completed data

- [ ] Implement `strip_to()` through generic document promotion/copying, preserve
  or derive required metadata before removing values, set the stripped marker,
  re-bake and copy the original payload unchanged into a new complete allocation.
- [ ] Preserve payload-relative offsets, counts, gaps, unused bits and trailing
  bytes. Rebuild role state for the new document; do not promote through a live
  specialised role or decode binary values into literals.
- [ ] Treat already stripped input as an independent equivalent output, following
  the same empty-destination and source-preservation rules.

Review cases: both roles/schema kinds, local and borrowed sources, inferred bulk
data counts, changed document length, already stripped input and binary values
that cannot be represented as document literals. Use byte-for-byte payload
assertions, including unreferenced storage; document byte identity is not required.

## Stage 7: Host complete-buffer preservation

- [ ] In `host/runtime/asset_service.cpp`, switch loose baked load adoption to
  `adopt_buffer()` and owned baked result bytes to `buffer_bytes()`.
- [ ] Preserve the entire exact entry range in `retain_bundle_file()` while
  retaining its separately validated document prefix and existing backing lease.
  Audit cached returns and alignment checks for the same result contract.
- [ ] Audit `BakedAssetTransfer`, retained records, `AssetResult::byte_view()` and
  baked saving so complete logical extents survive all paths. Keep document
  views prefix-only and JSON output document-only. Do not add a schema asset kind,
  unmanaged schema transport or new reference mechanism.
- [ ] Audit save cache associations and byte-length assumptions across consumers.
  A cached record must supply the representation and alignment requested; document
  navigation must not accidentally use the full appended extent as document size.
- [ ] Keep writable filesystem destination checks and current bundle closure
  rules. Explicit whole-bundle saves use current bytes; no dirty-state machinery,
  automatic persistence or bundle-entry writes are added.

Primary interfaces: `core/system/transported_types.hpp`,
`core/assets/asset_repository.hpp` and `host/runtime/asset_service.*`. Change only
what the extent audit requires. Add targeted coverage to existing baked transfer,
bundle service/request and Host acceptance tests. Ordinary prefix-only baked
files must continue working; arbitrary appended bytes are preserved generically
without claiming schema-format validity. Review loose and bundle paths together
so no public full-buffer contract is delivered for only one source kind.

## Stage 8: acceptance and documentation completion

- [ ] Exercise live bake -> local block -> owning Host transfer -> attach -> save
  -> loose reload for `.bid` and `.bbd`, checking exact complete-file bytes.
- [ ] Exercise JSON load -> explicit materialisation -> admission/save, with
  independent disposal of the parsed source and no hidden schema lifetime.
- [ ] Build a bundle from saved files through the filesystem image; load each
  complete entry, attach, close the bundle, save retained sub-files to a writable
  root and dispose them under existing lifetime rules. Verify bundle padding is
  excluded and writes to bundle entries are rejected.
- [ ] Test Host success followed by specialised attachment failure, source versus
  discard retention, save failure preserving a retained asset, and admitted work
  draining before disposal. Use existing Host/lifecycle harnesses rather than
  inventing an alternative asynchronous test environment.
- [ ] Cover explicit whole-bundle saving after a permitted bounded mutable change
  to its backing bytes, retaining valid structure. Reload and verify current
  bytes were saved. Verify an independent promoted document does not mutate its
  source bundle; do not add production mutable access solely for this test.
- [ ] Update schema specification/runtime API/validation and asset/bundle
  references to describe implemented contracts. Keep the revision as rationale,
  mark completed stages with validation evidence, and update the backlog/status.

## Validation and review discipline

Extend existing suites in `tests/test_suites/` and the real Host acceptance paths;
put substantial new schema persistence cases in focused test files if that keeps
the existing suite manageable. Register new sources in the Visual Studio project
and filters, preserving their CRLF, encoding and final-newline conventions.

For each behavioural stage, build Debug x64 and run ordinary Core tests with
`tools/invoke_sandbox_build.ps1 -RunTests -TestMode 1` and a stage-specific log
tag. The runner has no assumed new per-suite selector. Keep tests with the stage
that introduces the behaviour; stage 8 adds cross-layer workflows, not deferred
unit coverage. Record commands/results and unresolved limitations per review.

After integration, cover Debug/Release on x64 and x86 (Win32 output) once, reusing
already passing results for unchanged code. Run the Host worker harness for its
shared/dedicated acceptance paths and the module lifecycle harness with worker
counts 1 and 2 on Debug x64. Broaden Host configurations only for failures or
unresolved platform concerns. Do not repeat a passing matrix without a relevant
change. Expensive `-t3` modes are not an automatic requirement for this work.

Completion requires working end-to-end persistence for both roles, unchanged
binary data through loose/bundle save paths, enforced ownership/failure contracts,
valid diagnostic lifetimes, completed naming standardisation and current docs.
No aggregation or allocation optimisation is needed to declare this scope done.
