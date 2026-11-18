Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   shader_system_stages.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   16 Nov 26

# Shader system staged development

## Status and purpose

On 16 November the user selected staged, iterative development after reviewing the
[overall system reference](../modules/shader_reflection_design.md). It captures the
intended system, but still contains ambiguity, missing detail and excessive detail.
Its completed draft inventory is a coverage record, not implementation readiness.

The working cycle is: decompose the work; select and clarify one small stage;
implement and review that stage; then update affected earlier stages when integrating
new work. This document records the approach and proposed work boundaries. It does
not approve every draft mechanism, freeze delivery order or authorise implementation.

## Work areas and initial stages

Numbers identify the proposed progression, not a mandatory dependency chain.
Each area can have several increments; only the selected increment needs a precise
implementation specification. Prefer concrete consumers to speculative generality.
The selected prerequisite and increment order for stage 1 is recorded below;
that order does not impose a dependency chain on the remaining work areas.

| Stage | Small initial delivery | Extension and dependency boundary |
| --- | --- | --- |
| 1. Device/display discovery and minimal GPU execution | Ordered increments: 1A capability audit with persisted-baseline comparison, 1B presented graphics triangle, then 1C verified compute job. Each has a separate acceptance checkpoint. | First accept the schema text/span extension, then the Host-owned Platform report mechanism. Rendering owns GPU/backend work and its combined audit baseline; Platform owns native display/window facilities. Requires explicit execution/lifetime contracts and a narrow compute buffer/readback case. See the [focused design](../modules/pso_creation_stage1.md). |
| 2. Explicit resource and structure binding | Caller-selected resources/data with a precise, deliberately small compatibility model. | Refine structure correspondence before automatic mapping. Add packing/conversion only for supported cases; use stage 1's physical requirements. |
| 3. Component catalogues and libraries | Identify, retain and reuse compiled components and reflection results needed by PSO consumers. | Add authoring, packing and explicit shared-type consolidation in useful increments. Catalogue work can proceed independently of native PSO creation. |
| 4. PSO library persistence | Save/load compiled PSO libraries through Host file services, check the compilation environment and regenerate as required. | Distinct from stage 1A's discovery-audit baseline. Needs recipes/components and a native PSO path, not providers or Executive integration. Review the existing packaging/cache proposal when specifying this stage; compiled-PSO persistence remains library-based. |
| 5. Semantic contracts and mappings | A small set of concrete contracts and explicit mappings over established physical interfaces. | Extend vocabulary/equivalence/coverage through worked uses. Strict semantic validation applies to the semantic facility; a physical-only earlier result cannot claim it. Inactivity-proof capabilities remain empty until separately qualified. |
| 6. Rendering-local providers | Register singular/selectable supplies locally and resolve simple bindings uniquely. | Introduce explicit compositions, shared preparation, coherence and freshness when concrete consumers require them. No initial cross-module provider protocol. |
| 7. Executive integration and shared lifetime support | Connect a demonstrated external data requirement using existing communication facilities. | Introduce schema acknowledgement, independent bulk acquisitions and observation support where actual lifetimes demand them. Host/Framework work remains generic and separately reviewable. |

Initially keep domain work within Rendering. Host supplies existing file/asset
services for loading and saving; this does not require Executive to author or drive
the shader/PSO workflow. Existing batch and module facilities remain available, but
a new scheduler, generic closure service or bulk extension is not an initial dependency.

Stage 1 now includes Platform cooperation for display discovery and presentation.
Its narrow compute binding/readback case informs stage 2 without settling general
structure correspondence or the user's further structure-binding ideas. Audit
refresh-rate modes separately from achieved workload frame rates; capability,
pipeline creation, execution and verified output remain separate evidence.

These stages preserve the intended eventual system. Deferred functionality must be
visible: physical validation, semantic validation, provider satisfaction and actual
device execution are different claims. A stage rejects unsupported inputs rather
than silently accepting them under the final system's name. Simplification must not
misrepresent a partial facility as the completed production library contract.

## Stage 1 prerequisite order and review gates

The user selected the following order on 16 November 2026. The overarching
coordinator reviewed the focused design on 17 November and found no blocking
findings in the two prerequisite designs. Both are design-approved for separately
authorised implementation and review. This plan reconciles their dependencies;
design approval and ordering do not authorise implementing tasks or execution.

| Order | Scope and ownership | Gate before proceeding |
| --- | --- | --- |
| Prerequisite 1: schema extension | Foundation/schema text and typed span semantics, document string retention, explicit reference contexts, transactional copy/edit/repacking and relocation. Whole referenced target collections are retained in the first pass; that retention strategy remains provisional. | Reviewed implementation and independent schema acceptance, including layout, reference validation, relocation, failure ownership and report-shaped timing evidence. |
| Prerequisite 2: Platform reports | Platform owns native queries and bounded capture. Framework owns the shared schema/messages. Host independently owns, validates and atomically publishes the immutable current report, with authenticated current/update requests and independent reader acquisitions. | Reviewed implementation, portable/Host lifecycle acceptance and native Windows qualification, including the measured execution-placement decision. |
| Rendering 1A: audit | Combine Rendering's GPU queries with Host-held Platform facts; compare fresh candidates with the persisted audit baseline and assess dependent invalidation/rebuilds. | Complete and review the remaining 1A interfaces/representation and integration decisions, then implement and accept 1A separately. |
| Rendering 1B: graphics | Validated graphics PSO, triangle rendering/presentation and safe retirement through the Platform window contract. | Complete and review the graphics handoff, then implement and accept 1B separately. |
| Rendering 1C: compute | Explicit buffer binding, deterministic dispatch, CPU readback and exact output verification. | Complete and review the compute handoff, then implement and accept 1C separately. |

Accept the schema prerequisite before implementing the Platform report prerequisite;
accept both before beginning Rendering 1A, then proceed through 1B and 1C. These are
separate implementation/review/acceptance checkpoints. Delivery order does not make
compute execution depend on a display or swapchain, or require Platform installation
for compile-only/headless work. Audit and graphics remain the primary delivery.

The Host report starts capture after Platform activation, independently of Rendering.
Current-report reads acquire the held snapshot without starting discovery; concurrent
update requests are declined as update-in-progress. Failed refresh preserves the
current report for the same installation. Unload makes the current report unavailable;
reload captures afresh. Historical acquisitions remain protected independently of
Platform code/native objects after Host's independent ownership boundary is complete.
The [focused report contract](../modules/pso_creation_stage1.md#agreed-platform-to-host-capture-handoff-and-publication-path)
owns the detailed capture, handoff, publication and retirement rules.

Carry the coordinator's conditions into each prerequisite handoff:

- Preserve the measured timing/placement gate. Local synchronous schema operations
  and Host-pumped native capture are the starting points; the capture deadline is
  not responsiveness evidence. Qualify the actual costly phase before selecting
  offloading or accepting an observed latency tradeoff.
- Review the concrete new integration: expose the baker's string-ID mappings,
  restrict or extend reference-bearing byte-copy paths, and implement report
  publication, reservation and retirement handlers. Existing acquisitions and
  pumped transport are reuse points, not an already implemented report service.
- Follow [pre-release versioning policy](../../AGENTS.md#versioning-and-compatibility)
  for affected contracts and maintained artifacts: remove legacy/transition code,
  reset affected markers to 1 before the completion rebuild, regenerate affected
  data with rebuilt code and validate the final current layout. Keep explicit
  format checks and preserve unrelated formats/data.

The [prerequisite acceptance gates](../modules/pso_creation_stage1.md#agreed-validation-and-acceptance-gates-for-prerequisites)
remain authoritative. No prerequisite implementation or acceptance is claimed here.

## Stage 1A audit-baseline persistence boundary

Stage 1A includes discovery-audit persistence and comparison through existing Host
file/asset services. Its authoritative baseline is `.bsd` at
`state:/rendering_device_display_audit.bsd`, with a configurable Host logical-path
override and JSON inspection/export. Keep one writer per configured baseline path;
acceptance fixtures use isolated paths/root mappings. The audit schema/version and
storage-container version are separate contracts.

Load and validate the saved baseline at startup, then compare fresh discovery.
Explicit refresh likewise produces a candidate comparison. Compare meaningful
claims using native identity evidence, excluding timestamps, local IDs and enumeration
order; report unresolved facts explicitly. Adopt usable candidates after comparison
and impact assessment. Failed, truncated or inconsistent required queries must not
silently replace a good baseline. Save the first usable baseline and meaningful
adopted changes through Host's temporary-write/replace path; save failure preserves
the previous file and leaves the current in-memory baseline explicitly unsaved.

Assess rebuilds against actual dependencies. Device/driver changes invalidate all
existing or cached PSOs for the prior environment for new use, while accepted
in-flight work retains protection through retirement. Display/configuration changes
affect only dependent resources. Confirmed environment changes invalidate affected
PSOs even when another query prevents candidate adoption. Adoption, invalidation,
rebuild success and persistence outcomes remain separate.

The baseline stores observations, provenance and query outcomes. It restores no
native handles, live selection authority, enabled device state or execution proof.
A matching audit does not prove shader/recipe inputs unchanged or a cached artifact
usable; native GPU objects still require creation at startup. Host's current Platform
report and Rendering's combined saved audit have distinct ownership and purposes.
This scope introduces no native PSO cache or compiled-PSO library format; those remain
stage 4 work. General structure correspondence and semantic/provider selection remain
separate work areas.

The [focused baseline contract](../modules/pso_creation_stage1.md#agreed-persisted-baseline-and-candidate-comparison-direction)
owns adoption, missing/corrupt/unsupported/read-failure handling and identity rules.
Rendering's concrete combined-report representation, save/load/request interfaces
and necessary Host I/O status integration remain part of the open 1A handoff.

## Structure-binding boundary

Refine three related concerns separately:

- Consolidation/reuse of physical type declarations.
- Correspondence between supplied data and shader structures, including layout.
- Semantic meaning and selection of the supplying resource/provider/item.

They need explicit interfaces, not necessarily one mapping mechanism. Start stage 2
with concrete data/resource examples and the user's proposed simplifications. Do
not freeze aggregate/member mapping, shared-type rewriting or conversion behaviour
from the monolithic draft before that discussion.

## Documentation responsibilities

The overall reference retains system intent, agreed policies, implemented checkpoints
and earlier draft proposals. This plan owns stage boundaries, dependencies and the
development workflow. Each selected stage gets one focused document containing its
use case, supported subset, inputs/outputs, ownership, failure behaviour, acceptance
criteria and unresolved decisions. Record review status there.

Only extract/refine the material needed by the selected stage. Link to existing
contracts rather than copying the full system into every stage. New proposals stay
labelled as proposals. If a stage changes an earlier decision or interface, record
what changes, why and which consumers need revision; obtain the relevant review.
An intentional scope deferral does not silently repeal an agreed eventual requirement.

## Chat coordination and review

The user requested the following arrangement:

1. An overarching coordinator chat maintains the system direction, stage boundaries,
   dependency decisions and cross-stage consistency. It works with the user to
   select focused stage-design chats.
2. A stage-design chat refines its selected scope with the user, inspects the actual
   implementation and produces a focused design. It makes gaps and simplifications
   explicit rather than inheriting every draft detail as a requirement.
3. The overarching coordinator validates that design against the system goals,
   earlier stages and ownership boundaries. Resolve findings before implementation.
4. Once the stage is selected for implementation, its design chat creates the
   implementing chat and acts as that implementation's review coordinator. The
   implementation handoff identifies the reviewed design and its exact scope.
5. The implementing chat supplies changes and appropriate verification evidence;
   the design chat reviews them and coordinates corrections. Material system/interface
   changes return to the overarching coordinator. Completion updates the stage record
   and affected system documentation.

Keep the roles distinct: system consistency review belongs to the overarching
coordinator; implementation review belongs to the stage-design coordinator. Neither
approval silently selects another stage or expands scope. Create implementing chats
only when requested/authorised, as required by repository instructions.

Use the shared main checkout. Coordinate file ownership between active chats and
preserve unrelated changes. A handoff should name relevant files/sections, decisions,
open questions, dependencies and verification evidence; it should not require replaying
the entire originating conversation. Read the current files before editing them.
Commit only on explicit user instruction and after applicable coordinator review;
the user performs pushes. No new branch/worktree is implied by chat separation.

## Current handoff

On 17 November the prerequisite designs passed overarching coordinator review with
no blocking findings. The order is schema extension, Host-owned Platform reports,
Rendering 1A audit with baseline persistence/comparison, 1B presented triangle and
1C verified compute. This stage plan now records that order and the boundary to
stage 4, completing the overarching-plan reconciliation condition for handoff.

The focused stage-design chat owns [the detailed design and review record](../modules/pso_creation_stage1.md#review-status-and-remaining-decisions)
and will coordinate prerequisite implementation/review when explicitly instructed.
The complete Rendering handoff remains open on its request/control/result interfaces,
Host-report consumption and combined-audit persistence representation, Vulkan
loader/build/deployment, selection tie-breaking, limits/waits/stall diagnostics and
detailed submission/presentation accounting. Prerequisite design approval does not
close those items. The user's further structure-binding ideas remain for discussion
in the relevant stage.

No stage implementation is authorised by this documentation update. Detailed
transform/interpolation profiles remain deliberately deferred. Source/fixture tests
already passing are not a reason to rerun full matrices during documentation work.
