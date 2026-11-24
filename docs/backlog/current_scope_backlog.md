# Current scope

## Host communication refactor: complete

Status reconciled 24 November 2026. The 21 November communication increment
closed. Common Host endpoints and lifecycle are committed in
`ca8770d`; direct POD connections between arbitrary participating pump/thread
endpoints are committed in `84506a7`. Coordinator review, consumer migration and
validation are complete. The [milestone record](../project/completed_milestones.md#common-host-endpoints-and-direct-pod-connections)
links the delivered contracts and evidence.

The subsequent [preamble consolidation](../project/completed_milestones.md#module-execution-preamble-consolidation)
and scoped Input fixture follow-up are complete and reviewed, but uncommitted.
Their [qualification ledger](../modules/module_preamble_validation.md) separates
current results from explicit limits. No further Host increment is selected.
The [Host and endpoint backlog](host_endpoint_follow_on.md)
captures generic provider registration for HID/reports, revision-qualified
availability/topology observation, undelivered new asset IDs, long-running work,
further capacity tuning and optional authority/transport extensions. These remain
separate scopes; the Platform report handoff and input work keep their existing pauses.

## Earlier scope checkpoints

Platform/input status updated 10 November 2026. The current input stage is complete
through `89ec227`, including Windows acquisition and real Host/consumer acceptance.
HID/Input work is parked at the user's request pending further target-platform
setup and a separately selected next step. The remaining coverage and deferred
work are recorded below; no further input implementation is currently selected.

Earlier foundation checkpoint, 4 November 2026: the selected consolidation is
complete, including the rendering DLL stub and Executive-controlled startup in `f91ded3`.
The subsequent development filesystem-image stage is complete and accepted,
including the first-pass review corrections. See the
[development contract](../../development/README.md) and
[completion record](../project/completed_milestones.md#development-filesystem-image).

This stage supplies hierarchical logical-root inventory, write permissions,
executable-directory DLL discovery, queued per-root refresh and retained-asset
cache associations. Both log roots and `test-output:` are excluded from inventory.
The old deferred filesystem design is superseded in full.

## Platform foundation and input: current stage complete

Host-pumped services, generic borrows/notifications, restriction accounting and
bounded maintenance are committed through `078b595`. The on-demand Platform shell
and first Windows window are committed in `f55d514`, including user review and
formatting follow-ups. See the [completion record](../project/completed_milestones.md#host-pumped-services-and-first-windows-window)
and [implemented window contract](../modules/platform_first_window.md) for scope
and checkpoint-specific validation limits. Audio tone acceptance is also delivered;
its [validation record](../modules/audio_module_validation.md) is separate.

The subsequent input and communication stages are implemented:

- [Direct POD connections](../modules/direct_pod_connections.md): Host-owned
  growable TQueue transports, readiness, exact-instance ownership and safe retirement.
- [Opaque bulk connections](../modules/direct_bulk_connections.md): fixed-capacity
  aligned latest-state endpoints, either-role initiation, POD coexistence and
  Host backing retained through two-sided quiescence, committed in `2e2a8b4`.
- [Host timed waiting](../modules/host_timed_wait.md): aggregate wake and the
  anchored 120 Hz cadence on Windows and the current Linux shell.
- [Portable input connections](../modules/input_connections.md): typed Platform-owned
  streams, readiness/boundaries, ordered recovery, designated readers and borrows.
- [Configuration installation](../modules/input_configuration_installation.md):
  Host schema conditioning, explicit work budgets, incremental Platform installation
  and ownership through cancellation/retirement.
- [Portable mapping](../modules/input_mapping.md): configuration evaluation,
  connection integration and per-control neutral rearming.
- [Windows desktop input](../modules/desktop_input.md) and
  [GameInput controllers](../modules/controller_input.md): keyboard/text/pointer/
  capture, device discovery and assignment, and up to four independent controller
  pipes alongside the desktop connection. The current input service/endpoint is V4.
- [Real Host/consumer integration](../modules/input_host_integration.md), committed
  in `89ec227`: independently scheduled consumers, queue/work pressure, recovery,
  retirement, latency instrumentation and recorded Windows Release baselines.

Windows physical acceptance covers desktop behaviour and two Xbox controllers plus
a PS5 over USB on x64. Win32 and Linux evidence has its own limits in the linked
records; Linux currently provides portable/synthetic coverage, not native desktop
or controller acquisition. Software-path timing is not physical input-to-display
latency. Wireless, dead-key/AltGr text and mixed-DPI qualification remain untested.

Native Linux/SteamOS, Android and macOS input/UI integration awaits the required
setup and later design/implementation decisions. IME, relative mouse mode and
advanced controller features remain deferred. A first real Executive/game consumer
would need its own consumption/update-boundary contract; that work is not selected
by this closeout. The [observation cleanup review](../modules/input_host_integration.md#deferred-observation-cleanup)
belongs at that integration milestone, with the current regression support retained.

Window modes/shortcuts, rendering presentation, multiple windows/output selection
and extensions remain separate Platform work. The Foundation/Framework/Host split
and common endpoints are implemented. Generic provider registration and any broader
threaded routed-service provider adapter remain [deferred](host_endpoint_follow_on.md#generic-provider-registration-for-input-and-reports).
See the [design status](../modules/platform_host_pumped_design.md).

## Bundle review and closeout

The bundle system and completed-system review corrections are committed through
`8d57ef1`. The [bundle reference](../assets/bundle_specification.md) describes the
final format, public requests, requester ownership, retained lifetimes and worker
roles. The [completed-work record](../project/completed_milestones.md#bundle-assets-and-virtual-bindings)
contains prerequisites, implementation/review checkpoints and validation evidence.
The implementation plan is retired and documentation closeout is reviewed.
Shader-specific integration remains outside this work; the agreed bundle use
and shader compilation direction are in the
[shader compilation design](../modules/shader_compilation_design.md).

The debug-macro investigation found five production calls needing assertion-level
reports in every build without breakpoint or shutdown effects. The agreed
`MV_ASSERT_REPORT` addition and migration are implemented, tested and reviewed, with the
contract in the [debug interface](../debug/debug_system_interface.md#mv_assert_reportformat_literal-).
Low-level submission tests remain direct; no broader macro family is selected.

## Messaging closeout

Message and ownership consolidation is complete, reviewed and committed through
`3db76d6`. See the
[completion record](../project/completed_milestones.md#message-and-ownership-consolidation)
for delivered scope and validation. Optional extraction helpers and further
wrapper changes remain [deferred](consolidation_deferred_design.md#remaining-message-and-storage-simplification).
The [testing-framework review](testing_framework_consolidation.md) is a separate
discussion; messaging closeout does not authorise its implementation.

## Schema closeout and next work

The schema persistence and aggregation work is complete, user-reviewed and
committed through `aa90b9a`, followed by the separately requested buffer growth
and reserve correction in `78fcfaf`. Delivered capabilities include:

- schema-independent document/payload blocks and explicit live/baked role attachment;
- checked/trusted schema switching that preserves existing binary storage;
- type-schema merging with independent input validation and explicit-default rules;
- prepared instance/specialisation and bulk-prefix merging;
- original-schema preparation of retained or stripped data into document-only form;
- composition of types with one or both prepared data domains;
- shared validation, reconstruction, sorted string preparation and pre-allocation.

The [schema guide](../schema/README.md) describes current use and contracts.
The [delivery record](../project/completed_milestones.md#schema-delivery-status)
holds commit checkpoints and validation; the completed plans are retired.
No merge, preparation or composition implementation stage remains open.

The [schema follow-on notes](schema_follow_on.md) retain unselected discussions:
typed serialisation, related C++/shader representations and conversion, selectable
declaration generation, source ingestion, editor consumers and optional combined
orchestration. Remaining container/string/editing costs are distinguished from
the completed buffer correction. General object-child indexing and persistent
live cursors are separate data-model work. No next schema implementation is
selected by this documentation closeout.

## Other future work

The [combined shader-system reference](../modules/shader_reflection_design.md)
retains overall intent and draft resolutions. The user's review found missing detail,
ambiguity and over/under-specification. The [stage plan](shader_system_stages.md)
now guides focused clarification, implementation and review one increment at a time;
the [PSO scope proposal](../modules/pso_creation_stage1.md) is the first candidate.
The [scope inventory](../modules/shader_reflection_design.md#scope-inventory-and-remaining-specification-work)
records draft coverage rather than implementation readiness; the
[ownership glossary](../modules/shader_reflection_design.md#ownership-and-lifecycle-glossary)
distinguishes storage, decision authority, leases and cleanup custody. An overarching
coordinator reviews stage designs; each design chat coordinates its implementation
review once that work is selected. Library assembly, semantic/PSO/provider binding and generic
Host/Framework support are distinct work areas with explicit dependencies; runtime
facilities are not automatically prerequisites for assembly. No implementation is
selected here.

Vulkan is the primary first rendering API; DirectX and CPU readback are deferred,
and stream output is not planned. Rendering already performs
[Basis texture conditioning](../modules/texture_codec.md) and in-process
[DXC shader compilation](../modules/shader_compilation_design.md) through batch
runners. Compilation supports six conventional stages, bundle-local includes,
renderer-owned reports, agreed defaults/restrictions and configurable limits.
Ordinary job failures retain diagnostics and let other jobs continue. Compile-only
groups return to Rendering; Host retains explicitly exported generic bytes/documents.
Individual reflection and renderer-local compile/reflect jobs/reports are implemented.
Generic schema composition and version-1 persistence are implemented; production
combined groups, component-library assembly and payload packing/relocation remain
unimplemented.

The review draft specifies component authoring/catalogues, reusable semantic
contracts/mappings, packing/reports, PSO construction/library persistence and provider
preparation, plus observation/bulk extension controls. The detailed finite transform/
interpolation repertoire is deliberately deferred; no general numerical-proof system
is required. User configuration still selects materials/resources and
resolves eligible-provider ambiguity. Host-owned opaque bulk distribution is now
implemented; schema-aware acknowledgement, independent acquisition protection and
generic observation leases remain proposed extensions. Current views expire with
endpoint quiescence and are not those independent leases. Detailed decisions live
in the combined specification. Specialization constants remain unsupported by reflection/
conditioning, with support not planned and compile-only behaviour unchanged.

Final deployment/platform bindings, UGC providers, general document navigation,
save-game integration, automatic cache reclamation and trust/layers remain future
work. [Module batch runners](../modules/batch_runners.md) now provide coarse
module work; further scheduling or dependency orchestration has not been selected.
Basic filesystem discovery/resolution, cache reuse and explicit disposal are
implemented; those do not imply the broader
mechanisms. Fixed-layout baked value editing is already implemented through
`CMutableBakedDocument`; a save-game workflow remains separate consumer work.
[Filesystem limitations](filesystem_asset_mapping.md) records only
useful considerations for later, explicitly selected iterations.

The lifecycle fixture project remains a maintained standalone project built by
its test script. Adding it to the solution for IDE visibility, with automatic
solution builds disabled, is a suggestion awaiting selection.

## Documentation map

- [Documentation index](../README.md): implemented systems and future design.
- [Completed milestones](../project/completed_milestones.md): delivered outcomes,
  commit references and validation.
- [Engine backlog](engine_backlog.md): broader work inventory, not a priority order.
- [Deferred design](consolidation_deferred_design.md): future ideas, motivations
  and unresolved questions carried forward from consolidation.
- [Development filesystem image](../../development/README.md): implemented roots,
  inventory, resolution, refresh and cache contract.
- [Filesystem limitations](filesystem_asset_mapping.md): bounded current behaviour
  and useful future considerations, not the superseded filesystem design.
- [Module batch runners](../modules/batch_runners.md): implemented coarse
  module work and its lifetime contract.
- [Future work notes](../project/future_work_notes.md): supporting cross-task context.
