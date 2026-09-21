# Current scope

Updated 21 September 2026. The selected consolidation is complete, including
the rendering DLL stub and Executive-controlled startup in `f91ded3`.
No subsequent implementation stage has been selected.

## Next work

Select the next bounded stage with the user. Schema and its consumers are
candidates, not an automatically authorised continuation. The existing schema
documents are design material, not an implemented schema system.

Vulkan is the primary rendering API planned for first implementation; DirectX
is deferred. The current rendering DLL only waits for an exit request.
Filesystem discovery/resolution, document navigation, save-game mutation,
automatic cache reclamation, trust/layers and the general job framework remain
future work. Explicit asset disposal does not implement those broader mechanisms.

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
- [Filesystem mapping](filesystem_asset_mapping.md): proposed discovery,
  resolution and immutable catalogue publication.
- [Job framework design](job_framework_design.md): future scheduling and module
  execution design, separate from the implemented DLL lifecycle service.
- [Future work notes](../project/future_work_notes.md): supporting cross-task context.
