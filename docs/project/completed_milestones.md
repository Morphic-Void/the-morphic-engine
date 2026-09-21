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

## September consolidation: document, image and asset services

Completed and committed by 20 September 2026:

- `e60407f` completes shared document reports, byte-view parsing and the permanent
  text/reporting documentation after the parser/model/writer and style passes.
- `d1d804c`, `42a908d` and `a75962f` establish diagnostic-only memory accounting,
  uniform container/live-document attribution and baked-document storage with
  version-4 stored offsets, on-demand views and aligned file loading.
- `5b1282f` and `98ce708` add the image view over 8-bit/32-bit storage, clipped
  drawing/copying, channel write masks and TGA encoding configuration.
- `f74213f` consolidates raw, baked, JSON and TGA asynchronous asset operations.
  It supports retained and one-shot transfers, conditioning on the Host worker,
  file I/O on the I/O worker, compact borrowed-view results and failure diagnostics.
  The Executive exercises 48 sequential and 32 concurrent asset operations.
  The legacy client TGA flow and redundant catalogue identities are retired.

These checkpoints passed their recorded Debug/Release x64/Win32 builds and Core
suites; service integration also passed the Executive acceptance flow. Exact
contracts and validation are in the [image](../image/image_view.md),
[asset](../assets/asynchronous_asset_services.md) and
[data-model](../data_model/README.md) references.

## Asynchronous module lifecycle and asset disposal

Implemented and reviewed as of 21 September 2026, including the user's manual
style pass and coordinator verification. The user authorised committing this
milestone on 21 September.

- DLL load/bind and unbind/unload run on the Host I/O worker. Workers share their
  operation handlers, preserving later optional coalescing.
- The Host bootstraps without the Executive and starts it after asynchronous
  binding. Other-module requests receive acknowledgement and completion;
  Executive self-termination receives neither and immediately requests thread exit.
- Self-unload shuts down the system. Failed Executive replacement logs an
  assertion and shuts down; failed ordinary replacement leaves the service
  unavailable for the Executive to handle.
- Explicit disposal waits for accepted asset operations. Before DLL unload,
  remaining dependent assets trigger an assertion log and are destroyed while
  the DLL is still loaded. Independent assets survive replacement.

All twelve lifecycle scenarios and Core suites passed in Debug/Release x64/Win32
after the review tidy-up. Debug x64 passed again after the manual style pass.
Policy, whitespace and line-ending checks passed. The
[module lifecycle reference](../modules/asynchronous_module_lifecycle.md) records
the protocol, failure handling and exact evidence paths.

This stage is committed as `491ce78`. The separately authorised rendering DLL
stub is now implemented and accepted; see the module lifecycle reference
for its current contract and validation. Actual rendering, the general job
framework and automatic cache reclamation remain outside this bounded service.

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
The [consolidation plan](../backlog/consolidation_pass.md) preserves that history.

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
first review stage of the
[parser refactoring specification](../backlog/parser_refactoring_specification.md).

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
Its subsequent infrastructure work and remaining parser/policy migration are
tracked in the specification's implementation-progress section.

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
caller policy and recovery retirement remain in the
[refactor specification](../backlog/parser_refactoring_specification.md).

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
