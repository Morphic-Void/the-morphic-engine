# Current scope

Updated 20 October 2026. The selected consolidation is complete, including
the rendering DLL stub and Executive-controlled startup in `f91ded3`.
The subsequent development filesystem-image stage is complete and accepted,
including the first-pass review corrections. See the
[development contract](../../development/README.md) and
[completion record](../project/completed_milestones.md#development-filesystem-image).

This stage supplies hierarchical logical-root inventory, write permissions,
executable-directory DLL discovery, queued per-root refresh and retained-asset
cache associations. Both log roots are excluded from inventory; `test-output:`
remains included. The old deferred filesystem design is superseded in full.

## Bundle review and closeout

The bundle system and completed-system review corrections are committed through
`8d57ef1`. The [bundle reference](../assets/bundle_specification.md) describes the
final format, public requests, requester ownership, retained lifetimes and worker
roles. The [completed-work record](../project/completed_milestones.md#bundle-assets-and-virtual-bindings)
contains prerequisites, implementation/review checkpoints and validation evidence.
The implementation plan is retired and documentation closeout is reviewed.
Shader-specific integration remains outside this work.

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
is deferred. The current rendering DLL only waits for an exit request.
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
