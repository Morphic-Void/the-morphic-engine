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

The original resolver/generator baseline is `5313c85`. Integration stage 1,
mutable baked integer-width updates, is complete in separate commit `d92dddd`.
The next delivery is the shared-query/resolver refactor; see the
[staged plan](implementation-contract.md#staged-implementation-plan).

The coordinator handoff, pre-refactoring gap review and record-layout proposal
pointer have been consolidated into these documents. Current requirements belong
in the design, implementation facts in the API guide, and progress/acceptance
evidence in the implementation contract; do not maintain parallel summaries.

The example predates locators and bulk-entry objects and contains later layout
features. Read its notes before using it as a test fixture. It uses Morphic numeric
extensions; strict-JSON previews should use the document writer's existing option.
