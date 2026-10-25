Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   README.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   20 Oct 2026

# Schema documentation

These documents describe the implemented schema system and separately identified
future work. Historical plans and decision transcripts are not required to
interpret its current contracts.

| Document | Purpose |
| --- | --- |
| [Specification](specification.md) | Grammar, value semantics, layout, ownership and operation contracts. |
| [Runtime API](runtime-api.md) | Public entry points, file map, lifetimes, diagnostics and implementation limits. |
| [Validation](validation.md) | Regression coverage and generated C++ layout checks. |
| [Example](schema-example.json) and [notes](schema-example-notes.md) | Authoring example, expected layouts, numeric notation and materialisation rules. |
| [Common binary block](common-binary-block.md) | Version-2 .bsd storage, required data binaries and explicit assembly. |
| [Persistence and aggregation](persistence-and-aggregation.md) | Implemented aggregate merging/composition, import, persistence and diagnostic rules. |
| [Delivery status](../project/completed_milestones.md#schema-delivery-status) | Completed capabilities, reviewed commit checkpoints and validation evidence; completed implementation plans are retired. |
| [Document model milestone](../project/completed_milestones.md#schema-document-model-and-runtime-construction) | Binary-free aggregates, stripped runtime owners, explicit edits and shared construction, with final validation evidence. |
| [Persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence) | Completed eight-stage delivery, commit checkpoints and validation evidence, including gap zeroing; the implementation plan is retired. |
| [Role and construction milestone](../project/completed_milestones.md#schema-role-attachment-and-construction) | Historical block, attachment, indexed lookup and construction milestones; later document-model changes supersede its old switching/reconciliation APIs. |
| [Follow-on work](../backlog/schema_follow_on.md) | Future direction and unresolved design questions, separate from current requirements. |
| [Header survey](header-survey.md) | Historical source research supporting the ingestion discussion. |

Start with the runtime guide for C++ use and the specification for authoring.
`CSchema` is a binary-free aggregate with optional types, instance and bulk
sections. Composition/merging permit incomplete documents; the caller requests
full validation when ready. Validation, live materialisation and runtime bake
require contained types. Named definitions are indivisible during merging.

Live/baked runtime data documents are always stripped. Instances retain hierarchy
and selected-field masks; actual values live in complete binaries. Whole-instance
and component edits explicitly preserve or propagate into descendants. Equal pins
and empty selections survive conversion. Scoped direct edits require explicit
completion. Bulk arrays remain directly writable without reconciliation.

Runtime .bsd blocks require complete backing for each present data section. Storage
assembly copies bytes; typed attachment checks them. Promotion preserves bytes and
masks. Explicit import into `CSchema` reconstructs only the source role's section,
without linked types, and rejects values lacking faithful literal representation.
The previous runtime envelope and superseded APIs have no compatibility layer.

The existing array layout, decorations, associations/interfaces, C++ declaration
generation, exact remapping and unused-storage clearing retain their physical rules.
The [header map](runtime-api.md#header-organisation) identifies public entry points.
General document operations belong to the [data model](../data_model/README.md).

The [sample](schema-example.json) is binary-free authoring input with no locators.
It uses Morphic numeric extensions; strict JSON consumers should use the writer's
strict mode. Its contained types drive validation and construction in the tests.
Historical delivery checkpoints remain in the linked milestones; future work belongs
in the follow-on notes.
