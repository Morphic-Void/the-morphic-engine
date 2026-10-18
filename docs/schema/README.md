Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   README.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Schema documentation

These documents describe the implemented schema system and separately identified
future revisions. Historical plans and decision transcripts are not required to
interpret its current contracts.

| Document | Purpose |
| --- | --- |
| [Specification](specification.md) | Grammar, value semantics, layout, ownership and operation contracts. |
| [Runtime API](runtime-api.md) | Public entry points, file map, lifetimes, diagnostics and implementation limits. |
| [Validation](validation.md) | Regression coverage and generated C++ layout checks. |
| [Example](schema-example.json) and [notes](schema-example-notes.md) | Authoring example, expected layouts, numeric notation and materialisation rules. |
| [Persistence and aggregation revision](persistence-and-aggregation.md) | Completed persistence contracts and reasoning; remaining aggregation and composition design, beginning with shared-schema aggregation. |
| [Persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence) | Completed eight-stage delivery, commit checkpoints and validation evidence, including gap zeroing; the implementation plan is retired. |
| [Role and construction milestone](../project/completed_milestones.md#schema-role-attachment-and-construction) | Schema-independent blocks, atomic attachment, checked/trusted switching, indexed lookup and construction/reconciliation refinements. |
| [Follow-on work](../backlog/schema_follow_on.md) | Future direction and unresolved design questions, separate from current requirements. |
| [Header survey](header-survey.md) | Historical source research supporting the ingestion discussion. |

Start with the runtime guide for C++ use and the specification for document
authoring and semantics. The system supports live/baked schemas, instances and
bulk data; shared resolution; C++ declaration generation; loading, capture and
editing; binary-authoritative reconciliation; embedded, external and stripped
output; exact-type remapping; and explicit unused-storage clearing. Canonical
schema output and reconstructed data apply the data model's integer presentation
metadata. Resolved type, member, field and enum lookup uses indexes; callers can
retain resolved indices for repeated access.

Instance and bulk data blocks hold document/payload storage independently of
schemas. The live/baked data roles carry schema associations. Initial attachment
validates an existing package or constructs its missing binary data; checked and
trusted schema switching preserve storage and document handles. Reconciliation
is explicit and rebuilds retained descriptions from authoritative binary values.
See the [attachment contract](runtime-api.md#initial-attachment-and-schema-switching).

The [header and implementation map](runtime-api.md#header-organisation) identifies
public entry points and internal support. General document copying, parsing,
writing and live/baked translation belong to the [data model](../data_model/README.md).

The sample is used by resolver, layout and data-role tests. It uses Morphic
numeric extensions; strict-JSON consumers should use the document writer's
strict mode. Its unset locators are intentional authoring input, not ready-to-bind
binary locations.

The [project milestone](../project/completed_milestones.md#schema-system-and-refinement)
records completion and validation checkpoints. Superseded design discussions and
delivery plans remain in Git history; current and future work belong in the
references above.
