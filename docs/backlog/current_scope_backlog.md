# Current scope

Updated 4 October 2026. The selected consolidation is complete, including
the rendering DLL stub and Executive-controlled startup in `f91ded3`.
The subsequent development filesystem-image stage is complete and accepted,
including the first-pass review corrections. See the
[development contract](../../development/README.md) and
[completion record](../project/completed_milestones.md#development-filesystem-image).

This stage supplies hierarchical logical-root inventory, write permissions,
executable-directory DLL discovery, queued per-root refresh and retained-asset
cache associations. Both log roots are excluded from inventory; `test-output:`
remains included. The old deferred filesystem design is superseded in full.

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
[implementation contract](../schema/implementation-contract.md) records delivery
and validation.

Integer presentation is implemented in the data model and applied by canonical
schema output and instance/bulk reconstruction. Remaining schema discussions
concern fp16 handling and source/header ingestion.
General schema name lookup remains caller-managed. Select these or new
consumer work with the user; the completed consolidation does not authorise
further changes.

Vulkan is the primary rendering API planned for first implementation; DirectX
is deferred. The current rendering DLL only waits for an exit request.
Final deployment/platform bindings, UGC providers, general document navigation,
save-game integration, automatic cache reclamation, trust/layers and the general
job framework remain future work. Basic filesystem discovery/resolution, cache
reuse and explicit disposal are implemented; those do not imply the broader
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
- [Job framework design](job_framework_design.md): future scheduling and module
  execution design, separate from the implemented DLL lifecycle service.
- [Future work notes](../project/future_work_notes.md): supporting cross-task context.
