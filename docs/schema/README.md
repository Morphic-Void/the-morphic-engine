# Schema documentation

Each document has one primary purpose:

| Document | Purpose |
| --- | --- |
| [Design](design.md) | Authoritative target semantics, grammar, ownership and operation contracts. |
| [Implementation contract](implementation-contract.md) | Delivery status, staged plan, acceptance checks, code reuse evidence and coordinator working arrangements. |
| [Runtime API](runtime-api.md) | Implemented schema API, limits, lifetimes and generator behaviour. |
| [Example](schema-example.json) and [notes](schema-example-notes.md) | Reviewed authoring example, expected layouts and its current limitations. |
| [Header survey](header-survey.md) | Supporting research for deferred source ingestion. |
| [28 September decision record](question-decisions-2026-09-28.md) | Historical user answers. Reconciled into the design and partly superseded by later discussion; not a current implementation brief. |

Integration stages 1-8 are implemented: shared live/baked resolution, all six
schema/data roles, loading, capture, coordinated instance edits, output/baking,
bounded remapping and explicit unused-storage clearing. The stage 9 review is
complete; its follow-ups address promotion and selection-edit defects, current
documentation, singular scalar input, consistent named bulk records and instance
configuration preservation when destination defaults change, followed by general
binary-authoritative document reconciliation. Baked entry indexes, direct live-record
binary search, cached bulk type indices, indexed remap setup and shared
compatibility checking address the subsequent bookkeeping review. Baking supports
optional stripped data output. Shared scalar decoding, subtree copying and name
stabilisation consolidate the schema paths; combining copying with the general
data-model implementation remains a later discussion. See the
[implementation contract](implementation-contract.md).

The coordinator handoff, pre-refactoring gap review and record-layout proposal
pointer have been consolidated into these documents. Current requirements belong
in the design, implementation facts in the API guide, and progress/acceptance
evidence in the implementation contract; do not maintain parallel summaries.

The example includes locators, bulk-entry objects and explicit layouts, and is
used by resolver, layout and data-role tests. Read its notes for the expected
layouts and materialisation rules. It uses Morphic numeric extensions;
strict-JSON previews should use the document writer's existing option.
