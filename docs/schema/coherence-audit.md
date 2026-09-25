Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   coherence-audit.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Schema documentation coherence audit

These audits compare the working design, discussion agenda, header survey,
sample and its notes with the latest design decisions and the supporting
data-model contracts. It is a documentation review, not implementation or
approval to start a worker task.

## Second audit: after grammar and layout clarifications

The second pass reviewed the accepted sample and the subsequent decisions on
`detail`, computed size, optional input metadata, normalisation using a resolved
schema, internal source export, delivery scope, null rejection, and short
positional input. It found residual wording and integration ambiguities, not
a new design decision blocking the next stage. The following corrections were
made without beginning implementation or expanding delivery scope.

| Finding | Correction |
| --- | --- |
| The JSON instance section still illustrated the earlier `type`/`value` wrapper. | Replaced it with the accepted `instances` / type / name / `declaration` form. Package prose now recognises that the section layout has been accepted. |
| Some summaries unconditionally deferred explicit layout, while others used the newer retroactive-change criterion. | Applied the criterion consistently in the design, agenda, and header survey. Packed layout remains deferred; the survey cannot independently expand scope. |
| Accepted structure metadata was still called proposed, and its timing could be confused with the deferred normalisation operation. | Distinguished acceptance/computation of natural-layout `detail.alignment` and `detail.size` from support for alignment increases and from creating a new normalised document. |
| The largest-atomic-member alignment wording did not mention nested explicitly increased alignment. | Stated the recursive effective-alignment rule and its reduction to the existing natural rule when no alignment is increased. Size is rounded to effective alignment, including when a containing type has no alignment override. |
| General override-omission prose could be read as preserving the tail of a short positional replacement. | Distinguished an omitted named override member, which retains its value, from a supplied short positional replacement, whose remaining elements use defaults. Zero declared array count and short initializer length are also distinguished. |
| Mapping prose could imply every reference needed a mapped document occurrence, contrary to the efficiency-first coverage rule. | Kept semantic resolution and runtime relationships separate from the auxiliary document-slot mapping. An unmapped occurrence remains a valid zero lookup result. |
| Text singleton wrappers were described without accounting for the actual document parser's representation. | Explained that member wrappers normalise into named array children; the resolver consumes that baked form rather than requiring text wrappers to survive. |
| Duplicate rejection could imply recovering source collisions after baking. | Require schema text parsing to reject collision extension before baking, using the existing parser policy. The resolver checks duplicates present in its input; ordinary baked arrays cannot reveal erased source history. |
| Bitfield mask-constant wording could mean the logical enum/field type rather than the storage word. | Aligned the design and agenda with the reviewed sample notes: constants use the containing storage-word type, while field types determine value interpretation. |
| Naming rules could be read as category-local or applied to schema syntax such as `default`. | Clarified the single type-name domain across category groups and distinguished declaration names from grammar properties. |

The document-integration findings are grounded in the existing
[singleton/collision rules](../data_model/document_text_format.md#collision-extension-and-singleton-objects),
not proposed changes to the data model. A baked document cannot be used to
recover information the parser deliberately did not preserve. No additional
provenance mechanism is required by this audit.

The accepted sample was unchanged during this audit: a minimal authoring input
with eight types, rather than fully normalised generated output. It was expanded
afterwards with layout and short-input examples and a proposed array syntax
simplification; see its [current notes](schema-example-notes.md) for review status
and new checks. During the audit, Markdown examples and documentation
links were checked separately; no schema runtime or generator implementation
has been tested or claimed.

Remaining work is the already planned runtime/interface proposal, application
of the delivery criterion, and concrete value-conversion/validation details.
Numeric policies suggested from codebase conventions remain proposals, not
decisions silently made by this audit. Later instance/remap/I/O policies and
the internal marker spelling remain in the agenda for their relevant stages.

## Result and document authority

The main design is consistent after the corrections below. It remains a
working design with explicit open details, not a complete implementation
specification. The old JSON sample has since been replaced by a reconciled
sample whose grammar the user has accepted. It is not yet an implemented
acceptance fixture. Earlier suggestions that only document shape
remained understated the unresolved interface and validation details.

- [design.md](design.md) records agreed schema decisions and marked open issues.
- [design-questions.md](design-questions.md) tracks decisions and worker-contract
  details still needed; its proposals are not additional requirements.
- [header-survey.md](header-survey.md) explores later ingestion cases. It does
  not expand first-stage scope or settle union/explicit-layout support.
- [schema-example.json](schema-example.json) now contains the reviewed replacement;
  [its notes](schema-example-notes.md) explain the grammar and expected results.
- [Data-model documentation](../data_model/README.md) defines the existing
  document substrate. Schema-specific normalisation and immutability are
  additional consumer contracts, not changes to general document semantics.

## First audit: corrections and clarification

| Finding | Resolution |
| --- | --- |
| Instance override errors were said to invalidate the schema without distinguishing combined and separate documents. | Clarified by the user during the audit: instance failure invalidates the resolved schema only when the instance and definitions share the same baked document. Separate instance-document failure leaves the schema valid. |
| Numeric-validation prose said the resolver failure-state contract was still undefined. | Corrected to the settled contract: every new resolution attempt invalidates the previous result; failure leaves an empty schema. Only the diagnostic representation and later instance-output failure details remain open. |
| The overview could require packed structure layout immediately, and the survey claimed explicit alignment was already covered. | Distinguished first-stage bit structures from deferred packed/explicit structure layout. Reworded survey capabilities and future tests so they do not promise implementation or union support. |
| General conversion remaps were described as already supported. | Marked them as a later design direction, separate from the initial strict bulk-copy remap contract. |
| The data-model semantic specification said no public mutable baked document existed. | Corrected against `CMutableBakedDocument` and the baked-format contract: existing payloads can be edited under restrictions; structural editing remains unavailable. Schema users still require immutable backing bytes. |
| The data-model rationale derived object-entry state from a non-empty name. | Corrected to the current name-presence flag. Anonymous values and explicitly empty property names both use name ID zero and must remain distinguishable. |
| Some agenda wording treated old samples as usable directly and repeated superseded invalidation wording. | Marked sample status explicitly, aligned invalidation rules, and identified default validation as a first-stage dependency rather than solely later instance work. |

The data-model corrections were checked against
[the baked view API](../../core/data_model/baked_document.hpp),
[the baked-format contract](../data_model/baked_document_format.md#validation-and-views),
and the semantic specification's name-presence rules.

## Historical sample

The following findings concern the original sample examined during the audit,
not the replacement now at `schema-example.json`. They are retained as history:

- `format`/`version` headers are from the old draft; current design requires no
  such headers and has not adopted those properties into the grammar.
- Member declarations use a `name` field instead of the ordered singleton
  property objects now agreed.
- The original top-level `layout`, `size`, and `alignment` fields predate the
  subsequent `detail` contract. That contract accepts optional alignment/size
  metadata and computes layout; explicit offsets follow the later delivery criterion.
- Bit fields use `bit_offset`/`bit_width` instead of contiguous masks, and
  `unused_bits: "zero"` contradicts the external gap-initialisation policy.
- `Axis` uses signed `i8` but noncanonical unsigned literal spellings. These
  spellings can be accepted as input; normalisation should mark signed values.
- `SurfaceFlags.axis` allocates two bits to signed `Axis`, whose value 2 needs
  a larger signed range. Keeping the signed enum requires a wider field;
  truncation is not a valid interpretation.

The replacement corrects these issues, and the user has accepted its concrete
grammar, including bitfield types, masks, and interpretations. The subsequent
explicit-offset/structure-detail rules are recorded in the design, with feature
inclusion governed by the later delivery criterion.
Its clarified size is generated from members and alignment, not a configurable
structure stride.
Subsequent clarification also distinguishes optional input metadata from
mandatory resolved metadata: code ingestion and baked-schema resolution can
derive omitted alignment/size, while resolved descriptions always contain both.
Generated schema documents include the structure details, and normalisation
of an existing document uses its resolved schema and backing document to
produce a new document without modifying its source. Validation and layout
calculation are reused from resolution rather than duplicated in normalisation.

## Remaining specification details

### Before the first resolver worker

1. The descriptor grammar and package shape have now been accepted through
   the replacement sample. Normalised-document creation is now deferred, while
   all schema output must use normal form. Include explicit offsets/increased
   alignment initially only if deferral would force broader retroactive changes.
   Apply that criterion during runtime design and complete the
   validation contract and related invalid fixtures without reopening the
   accepted syntax.
2. Complete the literal conversions needed to validate defaults. Short positional
   structure and fixed-array input is now settled: fill from the start and use
   defaults for the remainder, without changing the declared extent. Subsequent
   clarification also settled that resolution validates
   defaults and explicit enum defaults accept only labels from the matching
   enum definition. Some remaining Q8 questions still affect definition
   validation even though complete instance construction remains later work.
3. Complete the small public contract: lookup/inspection operations, storage
   limits and overflow reporting, diagnostic representation, and generator
   invocation/validation. Subsequent clarification settled unsigned access
   indices with zero for failure/unmapped lookups. Named elements are the
   mapping baseline; ordinary arrays need a mapping to their description,
   not each element. Resolved-schema efficiency takes priority over coverage.
   User-facing access-availability rules will be derived and documented after
   the efficient resolved representation is established.
   These are implementation-contract details within the agreed ownership and
   layout rules, not reasons to reopen them.

### Before instance evaluation and subsequent features

- The earlier reference-scope questions are closed by subsequent layout
  decisions: `declaration` holds supplied values, and `specialisation` holds
  children inheriting from the enclosing instance. References are implicit in
  containment or explicit to named types. Instance-reference paths, root
  conventions, and cross-document base lookup are no longer required. Supplying
  a definitions context for separate documents remains a packaging/API detail.
- Define instance input/output ownership and failure behaviour, including
  what happens to partial destination data. The effect on resolved-schema
  validity is now settled by the same-baked-document rule above.
- Complete typed-value conversion details, including distinct-enum
  compatibility. Do not infer the bulk-remap predicate from the directional
  enum-to-underlying-integer override rule.
- Complete named-bitfield patches, remap composition/conflicting writes,
  serialisation formats, and the narrow ingestion grammar when their stages
  are reached. These already appear in the agenda and do not expand stage one.

## Decisions checked for consistency

The reviewed documents agree on global structure type names; branch-scoped
instance names; forward type references but no forward override references;
value-only specialisation; local structure defaults versus unchanged omitted
named override members; default completion of short positional input; recursive
effective alignment; computed offsets and
strides; mask-based bit structures; external gap zeroing; and resolved-schema
ownership, moves, document-slot mapping, and non-serialisability.

The audit changes documentation only. No schema implementation, compiler
validation, or runtime tests are implied by this review.
