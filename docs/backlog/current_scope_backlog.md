# Current scope

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
and extensions remain separate Platform work. Framework contracts are implemented;
wider Core decomposition and a broader threaded routed-service provider adapter
remain separate decisions. See the [design status](../modules/platform_host_pumped_design.md).

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

Vulkan is the primary rendering API planned for first implementation; DirectX
is deferred. The rendering DLL already performs
[Basis texture conditioning](../modules/texture_codec.md) through batch runners.
[Shader compilation](../modules/shader_compilation_design.md) now provides:
six stages, bundle-local includes and renderer-owned groups containing one report
per compilation and a summary. Ordinary shader failures allow other jobs to
continue. Compile-only operations publish the completed group to the Host;
future conditioning operations may retain intermediate results within rendering
and publish a finished package instead.
Reflection is separate; CPU readback is deferred and stream output is not planned.
Compilation runs in-process with exceptions contained at the boundary.
Vector-relaxed buffer layout is the agreed default; CPU and shader representations
may differ. A small bundled shader support include provides binding/location
macros and controlled target defines; richer binding descriptions are deferred
to reflection and pipeline conditioning. Request/result structure choices,
development defaults, feature restrictions and configurable service limits are
implemented. Remaining binding conventions and platform validation are recorded
in the design; reflection and subsequent conditioning remain future work.
The agreed [reflection/component-library design](../modules/shader_reflection_design.md)
uses schema associations, interfaces and decorations, explicit shared-type
mappings, per-vertex/per-patch declarations and combined diagnostic reports.
Intermediate live results stay in rendering for library aggregation before
baking/assembly. Generic schema composition and version-2 `.bsd` persistence are
implemented. Explicit shader/variant entries, reversible qualified identities,
deterministic binary packing and the initial unsupported-feature policy are agreed
starting points. Their detailed grammar/encoding, packing records, additional
producer/consumer convention metadata and public API limits remain to settle.
The reflection design distinguishes semantic binding from
concrete CPU buffer layouts and records the agreed compact storage terminology.
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
