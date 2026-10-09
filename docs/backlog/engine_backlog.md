Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   engine_backlog.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   12 Sep 2026

# Remaining engine backlog

Updated 4 October 2026. This consolidates unresolved scope from the interim
capture. The parser, image view, concrete asset services and Executive acceptance
are complete. The asynchronous module/disposal work is committed; the separate
rendering DLL stub is implemented and accepted. Actual rendering remains
deferred. The [development filesystem image](../../development/README.md) is
also complete and accepted, including the first-pass review corrections.
The [deferred design](consolidation_deferred_design.md) preserves useful future
ideas, not the superseded filesystem implementation proposal.
[Current scope](current_scope_backlog.md) sets priorities.
[Completed milestones](../project/completed_milestones.md) records delivered work.
The sections below are deferred scope, not an implementation schedule.

## Diagnostics and developer tooling

- Expand debug reporting, fatal capture and controlled shutdown as concrete
  consumers require. Preserve early startup/late shutdown, bounded dedicated
  MPMC communication and the allocation-at-creation discipline documented in
  [the debug substrate](../debug/debug_service_substrate.md). Thread identity
  and provisioning must support worst-case reporting demand.
- Add in-engine overlays and rendered diagnostic UI when rendering exists;
  consider a main-executable popup path for early/fatal reporting without the
  platform module. Existing debug payloads, severity and output infrastructure
  are a starting point, not work to recreate.
- Add named test/test-group selection, a complete-suite default, listing,
  clear invalid-selection diagnostics and aggregate failure exit status.
- Add dedicated policy-validator regression fixtures for clean, error, warning,
  suppression, stale-suppression and project-configuration cases. Revisit
  SuiteUTF coverage only after its intended policy boundary is defined.
- Extend policy checks for dependency direction, Host service access and
  ownership/module boundaries as those contracts settle. Consider preprocessing,
  syntax or binary/import inspection where lexical checks are insufficient;
  preserve narrow explicit suppressions and component-specific policy.
- The attribution policy is documented. Audit remaining source headers and
  contributor guidance for consistent human authority and AI-assistance terms
  as they are maintained; do not repeat the initial policy-writing task.

## Bootstrap, threading and portability

- Finish allocator bootstrap ordering before removing the fallback allocator:
  install Host allocation routing before registries, module loads and thread
  creation, then make fallback use a reportable invariant and controlled shutdown.
- Build a tool-independent description that preserves Core as per-consumer
  shared source. Attempt Linux compilation/linking, distinguish portable-Core
  issues from missing platform implementations, and enable cross-platform checks.
- Extend the existing thread packages/context and identity/name registry for
  remaining provisioning and per-thread service requirements. Audit role/sub-role
  identity and TLS needs against existing code before adding layers.
- Resume formal Windows/Linux primitive and lifecycle smoke/stress tests when
  both platforms are practical: hardware count, mutexes, single/all wait-wake,
  semaphore, parking gate, native create/start/exit, repeated initialisation,
  failures and cross-platform comparison. Add sanitizer-backed validation there.

## Platform integration

Build Windows and Linux modules around system/event pumps, HID/input, windows,
dialogs and rendering surfaces. Native thread primitives and thread creation
remain below these modules; Host thread provisioning and TLS remain separate.
Host ownership and worker authority are documented in the
[asset](../assets/asynchronous_asset_services.md) and
[module](../modules/asynchronous_module_lifecycle.md) references.

## Data-model follow-ons

The linter/live/baked/writer/parser implementation, both refactoring stages,
caller policy and shared-report/byte-view API consolidation are complete.
Fixed-layout baked value editing is implemented, including canonical integer
width updates. Subtree copying and promotion share value construction and
metadata handling while retaining their separate traversal policies. The
schema/data-model header guidance and style audit are also complete.
Later source, shader, localisation and tooling ingestion should reuse the
[documented boundary](../data_model/README.md).

Still deferred: live packing with slot-link remapping, live cursors/revisions,
object-name lookup acceleration including O(1) baked lookup, and additional
general naming/value-conversion APIs only when consumers demonstrate a need.
The completed schema-role entry indexes and resolved physical-member index
do not implement these general document lookup or cursor facilities.
Qualify numeric conversion on additional supported standard-library toolchains
using the existing boundary and round-trip corpus.

The [schema system](../schema/README.md) implements document-based definitions,
resolution, C++ declaration generation, live/baked instance and bulk data,
output and remapping. Its review repairs, binary-authoritative reconciliation,
optional stripped baking, lookup/remap improvements, scalar/document-copy
consolidation and five-part tidying phase are complete through `db82da7`.
The subsequent performance pass completes inline query forwarding, shared
physical-member ordering and sorted explicit-layout overlap checking. General
schema name lookup remains caller-managed. Canonical schema output and data-role
reconstruction now apply the data model's integer presentation modes. Remaining
discussions cover conversion, related representations, typed serialisation,
selectable generation, packaging and source ingestion, as recorded in the
[schema follow-on notes](schema_follow_on.md).
Configuration, localisation, state capture/reconstruction and graphics metadata
remain potential consumers, to be selected as separate work.

## Image and graphics work

- Expand TGA validation across representative decode/encode, orientation,
  grayscale, 24/32-bit and RLE cases. The existing asynchronous TGA flow is a
  baseline; broader format coverage remains open. Prefer manual validation
  initially unless defects or tooling justify an automated suite.
- Condition and validate graphics pipelines: shader reflection, PSO metadata,
  JSON-backed descriptions, graphics state and API-specific/agnostic processing.
  Runtime rendering must accept both offline and on-demand conditioned data.
- Keep image conversion, compression/decompression and graphics asset
  conditioning separable from pipeline conditioning, for runtime and offline use.
- Build a swappable renderer/RHI with Vulkan as the primary first implementation.
  DirectX and other APIs are deferred. A renderer may request an RHI thread;
  the Host owns and provisions it. The existing passive rendering DLL is a
  lifecycle foundation, not a graphics implementation.

## Maths and geometry

Provide vector maths, rectangles, boxes, bounds/regions, circumspheres,
inspheres, frusta, standard transforms and joints. Add vector/ray intersection
with boxes, spheres, planes, triangles, quads and related primitives; useful
results include distance/parameter, point, normal and reflection. Penetration
queries may return depth and shortest-resolution direction/distance.

These are standalone geometry tools for physics, rendering, game logic and
editors. Physics adds simulation policy, constraints, broad/narrow phase and
gameplay integration rather than owning the primitive geometry library.

## Fonts, annotation and localisation

- Build the vector-glyph/Unicode mapping and shared user-facing text path for
  game, editor, debugging and prototyping. This is distinct from text ingestion.
- Extend the completed [image view](../image/image_view.md) only as consumers
  require. Clipped lines, filled/unfilled rectangles, transformed rectangle
  copies, fills and channel write masks already exist. Non-antialiased vector
  text, transparency blending and further blending operations remain deferred.
- Build JSON-backed localisation with offline conditioning, including external
  spreadsheet inputs where useful.
- Retain a small, rendering-independent Katakana UTF-8 conditioning facility
  as a specialised future consumer, separate from localisation itself.

## Resource and execution design

- [Filesystem limitations](filesystem_asset_mapping.md): deployment/platform
  bindings, UGC integration, explicit asset replacement/variants and additional
  consumer needs. Directory discovery, logical resolution, queued refresh,
  write updates and basic caching already exist.
- [Deferred design](consolidation_deferred_design.md): reference counting/cache
  eviction, document navigation, save-game integration using existing baked value
  editing, bundled outputs, module accounting periods, overlays, trust and later
  image transforms.

The [module batch runners](../modules/batch_runners.md) provide the selected
coarse-work execution path. General dependency orchestration, cancellation and
operation tracking remain questions for concrete consumers, as noted in
[deferred design](consolidation_deferred_design.md).

The backlog documents preserve design context and alternatives, not selected work.

## Higher-level consumers

Game/application, editor and physics remain placeholders. They consume the
platform, input, maths, rendering, text, threading, configuration, localisation
and Host asset services. Editors additionally need graphics/asset conditioning,
annotation and filesystem authority. Decompose these systems when their
infrastructure and first concrete workloads are ready.
