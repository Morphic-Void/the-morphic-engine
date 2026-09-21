# Current scope

Updated 21 September 2026. The selected image, asset-service and asynchronous
module-lifecycle consolidation is implemented, reviewed and accepted. The final
documentation reconciliation is complete, and the user has authorised the commit.

## Completed consolidation

- Parser/model/writer migration, parameter const and manual style passes are
  complete. Shared reports and the byte-view parser API are committed as
  `e60407f`; the permanent [data-model documentation](../data_model/README.md)
  describes the resulting contracts.
- Diagnostic accounting, uniform container/live-document attribution and baked
  storage/aligned loading are committed as `d1d804c`, `42a908d` and `a75962f`.
- The image view and its drawing/copying utility are committed as `5b1282f` and
  `98ce708`; see [image view](../image/image_view.md).
- Raw, baked, JSON and TGA asset services, retained and one-shot ownership,
  compact results and the Executive's 48 sequential/32 concurrent acceptance
  operations are committed as `f74213f`. The legacy client TGA flow is retired.
- Asynchronous DLL load/bind and unbind/unload on the Host I/O worker, Executive
  bootstrap/replacement/shutdown, explicit asset disposal and dependent-asset
  cleanup are implemented. User review, the manual style pass and coordinator
  checks are complete; the user authorised the commit on 21 September.

## Active work

1. Following this accepted consolidation, undertake the rendering DLL stub as a separate
   work stage: a minimal module thread waiting for an exit request, with its
   Host-managed lifecycle. Revisit successful-path fixture duplication then;
   this stub is not part of the current implementation.
2. Reassess subsequent schema and other consumers after that stage. Existing
   schema design material does not mean a schema implementation is complete.

Filesystem images/resolution, document path navigation, cache eviction/reference
counting, trust/layers and the general job framework remain deferred. Explicit
asset disposal does not implement those broader lifetime mechanisms.

## Documentation map

- [Coordinator handoff](consolidation_coordinator_handoff.md): current status,
  working arrangement and next-stage boundary.
- [Asset services](../assets/asynchronous_asset_services.md) and
  [module lifecycle](../modules/asynchronous_module_lifecycle.md): implemented
  ownership, message, shutdown and validation contracts.
- [Engine backlog](engine_backlog.md): remaining broader and deferred work.
- [Data-model documentation](../data_model/README.md): current semantic, text,
  parsing/reporting and baked-format contracts, with design rationale kept
  separately. Grammar, findings and policy need no backlog reference.
- [Completed milestones](../project/completed_milestones.md): implementation
  outcomes and validation, including the first document pipeline.
- [Future work notes](../project/future_work_notes.md): supporting cross-task
  context, without a second priority list.

The dated [consolidation plan](consolidation_pass.md), design stages and discussion
notes preserve earlier proposals. Their historical pending-work statements do
not override this status or the implemented subsystem contracts.
