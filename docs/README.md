# Documentation

System references describe implemented behaviour. Design documents preserve
future direction and alternatives without implying that implementation exists
or that work has been authorised.

## Implemented systems

- [Memory and attribution](memory/memory_subsystem.md), with individual
  [container references](containers/ByteBuffers.md) alongside it.
- [Document model](data_model/README.md): semantic model, text grammar,
  parsing/reporting and baked storage format.
- [Image view](image/image_view.md): drawing, copying, access and TGA metadata.
- [Asynchronous asset services](assets/asynchronous_asset_services.md): requests,
  ownership, conditioning, results, diagnostics and disposal.
- [Development filesystem image](../development/README.md): logical roots,
  hierarchical inventory, DLL redirection, queued refresh and cache associations.
- [Asynchronous module lifecycle](modules/asynchronous_module_lifecycle.md):
  Executive bootstrap, rendering startup, replacement, shutdown and lifecycle tests.
- [Module binding ABI](system/module_bootstrap.md), [type identity](system/type_identity.md)
  and [erased ownership](system/erased_owner.md).
- [Debug interface](debug/debug_system_interface.md) and
  [debug service substrate](debug/debug_service_substrate.md).
- Thread transports: [owning](threading/transports/TOwningTransport.md),
  [queue](threading/transports/TQueueTransport.md),
  [ring](threading/transports/TRingTransport.md) and
  [MPMC](threading/transports/TMpmcTransport.md).
- [Policy validator](project/policy_validator.md).

## Direction and future design

- [Current scope](backlog/current_scope_backlog.md) and
  [engine backlog](backlog/engine_backlog.md).
- [Deferred resource and lifecycle design](backlog/consolidation_deferred_design.md),
  [filesystem limitations and future considerations](backlog/filesystem_asset_mapping.md) and
  [job framework](backlog/job_framework_design.md).
- [Schema design](schema/design.md) and [header survey](schema/header-survey.md).
- [Future work notes](project/future_work_notes.md).

## Rationale and project record

- [Architectural principles](architecture/architectural-principles.md),
  [codebase model](architecture/codebase-model.md) and
  [engineering principles](architecture/engineering-principles.md).
- [Data-model rationale](data_model/data_model_design_notes.md).
- [Completed milestones](project/completed_milestones.md) and
  [attribution policy](project/attribution_policy.md).

Completed consolidation plans and task handoffs have been retired. Their final
contracts live in the references above, future ideas remain in the design
documents, and the original discussions remain available in Git history.
