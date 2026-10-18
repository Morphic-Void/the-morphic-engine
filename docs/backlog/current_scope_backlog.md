# Current scope

Updated 17 October 2026. The selected consolidation is complete, including
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

## Next work

The schema system is implemented, including live/baked roles, resolution,
generated declarations, loading, editing, output, remapping and unused-storage
clearing. Its review follow-ups include singular input and binary-authoritative
document reconciliation, indexed lookup and remap setup, baking with optional
value stripping, shared scalar decoding and document-copy construction. The
organisation, conditional/parameter and style/comment tidying phase is complete
through `db82da7`. The subsequent performance pass completes inline query
forwarding, retained physical-member ordering for clearing/generation and sorted
explicit-layout overlap checking. The
[schema milestone](../project/completed_milestones.md#schema-system-and-refinement)
records completion; the [validation guide](../schema/validation.md) describes checks.

Integer presentation is implemented in the data model and applied by canonical
schema output and instance/bulk data reconstruction. All eight instance/bulk data
persistence stages are complete, user-reviewed and committed through `dfe076d`,
including Host complete-buffer preservation and new-payload gap zeroing. The
[persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence)
records delivery and validation; the completed implementation plan is retired.
The subsequent [role and construction rework](../project/completed_milestones.md#schema-role-attachment-and-construction)
is implemented and validated: blocks are schema-independent storage, data roles
attach atomically and support checked/trusted switching without changing binary
data. Resolved schema lookup is indexed, controlled document construction avoids
repeated attachment checks, and reconciliation compares/emits in one traversal
with a separate final fidelity check. Full baked promotion preloads strings in
lexical order; arbitrary-order string insertion and per-edit record-tail scans
remain documented costs.
The [schema follow-on notes](schema_follow_on.md) preserve the next design discussion:
fp16 conversion, related C++/shader representations, simple trait-based
serialisation, selectable declaration output, aggregation/composition and
source ingestion. Shared-schema aggregation design is active: the
[revision](../schema/persistence-and-aggregation.md#next-discussion-shared-schema-aggregation)
records agreed merge contracts and remaining interface decisions. These follow-ons
are not yet an implementation plan.
General document object-child indexing and persistent live cursors remain
deferred. The completed consolidation does not authorise further implementation.

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
