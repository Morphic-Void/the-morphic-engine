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
contract and ownership API are being revisited in the
[consolidation plan](../backlog/consolidation_pass.md).

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
the forthcoming parser refactor has been validated.

Current reference:

- [Semantic specification](../data_model/revised_data_model.md).
- [Physical format](../data_model/baked_document_format.md).
- [Design rationale](../data_model/data_model_design_notes.md).

Direct file persistence and the full Executive-controlled document run were
planned but not implemented at this checkpoint. Their test mechanics are being
reconsidered around the consolidated Host contracts before schema work.

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
