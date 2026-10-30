# Documentation

System references describe implemented behaviour. Design documents preserve
future direction and alternatives without implying that implementation exists
or that work has been authorised.

## Implemented systems

- [Memory and attribution](memory/memory_subsystem.md), with individual
  [container references](containers/ByteBuffers.md) alongside it.
- [Message construction and ownership](system/erased_owner.md#message-construction-and-inspection),
  with the [messaging completion record](project/completed_milestones.md#message-and-ownership-consolidation).
- [Document model](data_model/README.md): semantic model, text grammar,
  parsing/reporting and baked storage format.
- [Schema system](schema/README.md): live/baked schema and data roles, resolution,
  schema-independent persistence blocks, attachment/switching, generated
  declarations, editing, reconciliation, output, remapping, unused-storage clearing,
  type/data merging, document preparation and composition.
- [Image view](image/image_view.md): drawing, copying, access and TGA metadata.
- [Asynchronous asset services](assets/asynchronous_asset_services.md): requests,
  ownership, conditioning, results, diagnostics and disposal.
- [Bundle specification](assets/bundle_specification.md): directory format,
  virtual bindings, asset references and disposal.
- [Development filesystem image](../development/README.md): logical roots,
  hierarchical inventory, DLL redirection, queued refresh and cache associations.
- [Host bootstrap configuration](project/bootstrap_configuration.md): the
  bounded startup file and checked-in flow-test presets.
- [Asynchronous module lifecycle](modules/asynchronous_module_lifecycle.md):
  Executive bootstrap, rendering startup, replacement, shutdown and lifecycle tests.
- [Module batch runners](modules/batch_runners.md): Host-owned execution of
  coarse module work, return channels and requester lifetime rules.
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
- [Deferred resource and lifecycle design](backlog/consolidation_deferred_design.md)
  and [filesystem limitations and future considerations](backlog/filesystem_asset_mapping.md).
- [Schema source-ingestion survey](schema/header-survey.md).
- [Platform module and host-pumped lifecycle](modules/platform_host_pumped_design.md):
  agreed window/input ownership, optional Host pumping, explicit window uses,
  extension callbacks, lifecycle gaps, implementation stages and open decisions.
- [Audio module: one requested tone test](modules/audio_module_design.md):
  a single Executive test request, miniaudio containment, finite tone/fade,
  callback and shutdown ownership, implementation stages and remaining choices.
- [Shader compilation direction](modules/shader_compilation_design.md): DXC
  integration findings, source bundles, renderer-owned reports, publication,
  agreed defaults and configurable limits, remaining design choices, deferred
  reflection and the decision not to plan stream output.
- [Shader reflection and component libraries](modules/shader_reflection_design.md):
  agreed schema mappings, resource and stage interfaces, naming and explicit type
  sharing, combined reports/publication, live library assembly and remaining
  decisions before implementation.
- [Schema follow-on work](backlog/schema_follow_on.md): conversion, related
  representations, typed serialisation, selectable generation, ingestion and
  optional orchestration beyond the completed aggregation APIs.
- [Future work notes](project/future_work_notes.md).

## Rationale and project record

- [Architectural principles](architecture/architectural-principles.md),
  [codebase model](architecture/codebase-model.md) and
  [engineering principles](architecture/engineering-principles.md).
- [Why Morphic Engine](architecture/why_morphic_engine.md): the project's
  position as a foundation for bespoke game technology and its relationship to
  *Locality*.
- [Data-model rationale](data_model/data_model_design_notes.md).
- [Building and testing](project/building_and_testing.md),
  [completed milestones](project/completed_milestones.md) and
  [attribution policy](project/attribution_policy.md).

Completed consolidation plans and task handoffs have been retired. Their final
contracts live in the references above, future ideas remain in the design
documents, and the original discussions remain available in Git history.
