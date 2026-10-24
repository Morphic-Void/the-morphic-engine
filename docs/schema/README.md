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
| [Array layout extension](array-layout-extension.md) | Agreed stride/alignment implementation scope and boundaries. |
| [Decoration extension](decorations-extension.md) | Agreed declaration metadata scope, comparison and preservation rules. |
| [Associations and interfaces](associations-and-interfaces.md) | Agreed non-storage declaration scope, checked relationships and physical boundaries. |
| [Example](schema-example.json) and [notes](schema-example-notes.md) | Authoring example, expected layouts, numeric notation and materialisation rules. |
| [Common binary block](common-binary-block.md) | Unified .bsd storage, independent binary availability and aggregate materialisation. |
| [Persistence and aggregation](persistence-and-aggregation.md) | Implemented persistence, type/data merging, preparation and composition contracts, diagnostics and publication policies. |
| [Delivery status](../project/completed_milestones.md#schema-delivery-status) | Completed capabilities, reviewed commit checkpoints and validation evidence; completed implementation plans are retired. |
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
metadata. Type merging, instance/bulk data merging, original-schema document
preparation and complete document composition are also implemented and committed.
Resolved type, member, field and enum lookup uses indexes; callers can retain
resolved indices for repeated access.

Common .bsd blocks hold documents and independently available binaries separately from
schemas. The live/baked data roles carry schema associations. Initial attachment
validates an existing package or constructs its missing binary data; checked and
trusted schema switching preserve storage and document handles. Reconciliation
is explicit and rebuilds retained descriptions from authoritative binary values.
See the [attachment contract](runtime-api.md#initial-attachment-and-schema-switching).

For aggregation, merge independently valid type documents and prepare data
documents against their original schemas. Merge the prepared declarations under
the merged schema, then optionally compose a combined document and construct
binary directly through common-block materialisation. Already prepared documents
can bypass preparation. Composition requires types and one or both data domains;
it assembles sections without resolving collisions. These document operations are
distinct from binary-preserving schema switching. See the
[runtime workflow](runtime-api.md#document-preparation-and-composition).

The [header and implementation map](runtime-api.md#header-organisation) identifies
public entry points and internal support. General document copying, parsing,
writing and live/baked translation belong to the [data model](../data_model/README.md).

The sample is used by resolver, layout and data-role tests. It uses Morphic
numeric extensions; strict-JSON consumers should use the document writer's
strict mode. Its unset locators are intentional authoring input, not ready-to-bind
binary locations.

The [project milestone](../project/completed_milestones.md#schema-delivery-status)
records completion and validation checkpoints. Superseded design discussions and
delivery plans remain in Git history; current and future work belong in the
references above.
