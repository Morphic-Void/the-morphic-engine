Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   schema_follow_on.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   4 Oct 2026

# Schema follow-on work

These notes preserve future direction separately from the implemented
[specification](../schema/specification.md) and [runtime API](../schema/runtime-api.md).
They are discussion inputs, not an implementation plan or authority to begin work.
The follow-on coordinator chat will settle design, scope and sequencing before
assigning implementation. Earlier proposals must be reassessed against current
code and the user's more recent direction.

## Direction discussed on 4 October 2026

### Typed serialisation

The caller supplies a C++ object by reference and an instance name. A simple
trait can supply the schema type name as a string; portable automatic extraction
is not required. Writing should update the named instance or extend the document
with a new instance. The intended interface also reads values back into C++ objects.
The caller and other systems are responsible for matching the C++ representation
to the schema: this facility is not to inspect members or validate layout equality.
Exact signatures, name scope, failure behaviour and specialisation interaction
remain for design. Generation and ingestion may supply traits later, but are not
prerequisites for the simple interface.

### Related C++ and shader representations

The user wants to mark a relationship between structures representing the same
logical data with different layouts or primitive types. Their example relates
`C_Sample { CVec3 position; CQuat orientation; }` to
`Shader_Sample { float3 position; half4 orientation; }`:

| C++ side | Shader-facing side | Correspondence to design |
| --- | --- | --- |
| `CVec3`: float `x, y, z` | `float3`: float `x, y, z, unused` | Three coordinates and additional destination storage. |
| `CQuat`: float `i, j, k, r` | `half4`: `fp16data_t x, y, z, w` | Four components requiring float-to-fp16 conversion. |

These are the user's illustrative structures, not a claim about a shader ABI.
Union or alias syntax was suggested, but the mechanism is unsettled. Positional
versus explicit member correspondence, nested relationships, extra fields,
conversion direction and target layout rules need discussion. No shared-storage
union or binary-compatible alias contract has been selected.

### Float-to-fp16 conversion

Consider schema-assisted remapping/conversion alongside external utilities.
Existing routes are explicit `fp16data_t` conversion into application buffers,
construction from numeric document values under an `f16` schema, and capture of
already converted buffers. Assess strided fields and arrays, rounding and
exceptional values, and whether the representation relationship should select
conversion. This does not change `CDataRemapPlan`'s current exact-type,
binary-copy-only contract or select a new conversion API.

### Selectable declaration generation

The user wants more customisable structure output, likely selecting definitions
one at a time and appending declarations to a string buffer for later export or
use as an include in shader compilation. C++, HLSL and GLSL output, dependency
selection/order, duplicate handling and the boundary between generation options
and representation metadata remain to be designed. Existing generation emits a
complete C++17 buffer; the more selective interface is not implemented.

### Document and binary packaging

Persistence is complete; the remaining work is aggregation and composition.

The [persistence and aggregation revision](../schema/persistence-and-aggregation.md)
records the completed persistence direction and ongoing aggregation design
agreements. Shared type definitions remain
separately associated with independently created data. A baked instance document
and its binary payload form one persistence entity, as do a baked bulk data document
and its payload. The implemented bundle system supplies combined loading where
needed; a combined types/instances/bulk data file is not the mandatory normal form.

Definition merging, instance aggregation and bulk data aggregation have separate
responsibilities. Complete document composition assembles already reconciled
sections and does not resolve collisions. The revision now includes the agreed
version 1 persistence format with schema-independent block validation and
separate role attachment to authoritative binary data, with opt-in retained-value
comparison. Callers supply the role's schema without compatibility signatures;
`CBakedInstancesBlock`/`CBakedBulkDataBlock` manage local or Host backing without
holding that association. It also records live baking, description
materialisation and document-promotion stripping interfaces, with contiguous
completed blocks and private staged construction permitted. Live baking now
writes records directly into the final allocation; description
materialisation still uses temporary payload storage. Host loading, admission
and saving preserve the full buffer while exposing a separate document-prefix
view. Explicit bundle saves write current storage; automatic bundle persistence
and writes to bundle entries are outside scope. Layered diagnostic contracts
distinguish storage-format, role attachment and construction failures, with
explicit location lifetime rules. Materialisation
is all or nothing, and parsing is followed by explicit caller-side construction
and optional Host admission/save. Implemented diagnostic representation is
documented in the [runtime API](../schema/runtime-api.md#completed-data-blocks);
aggregation contracts remain active design work in the revision.
Schema baking supports both the existing separate document/payload owners and
the new completed-block overloads.

All eight persistence stages are complete, user-reviewed and committed, ending
with stage 8 and gap zeroing in `dfe076d`. Newly created payload gaps are zeroed;
loading, saving and stripping preserve existing payload bytes. The
[persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence)
records delivery and validation evidence; the implementation plan is retired.
The completed naming pass standardised "bulk data" and `BulkData`/`bulk_data`
without compatibility aliases. The
[revision](../schema/persistence-and-aggregation.md#agreed-bulk-data-terminology-and-standardisation-pass)
retains the naming convention and migration mapping.

### Remaining aggregation and composition design

Type-schema merging was implemented on 18 October. A missing explicit
default expresses no merge preference and yields to an explicit default;
conflicting supplied defaults fail. Preserve the established scalar, array and
presentation-merging rules, and validate type layout and interpretation.

Each type-schema input must independently resolve. Equivalent natural and explicit
layouts are accepted, but layout-changing alignment, member/enum order and
referenced type identity remain significant. Merge original declarations and
presentation rather than normalised resolver output. References remain names;
final resolution establishes the result's own identities. The selected interface
shape is two existing read-only document views and an independently owned,
validated live document result. Adoption into a schema owner and baking remain
separate operations, preserving existing bound-owner replacement restrictions.
The implemented `merge_type_schemas` function uses stage/reason diagnostics, original
source locations for conflicts and resolver detail for validation failures.
It constructs and validates a private live document, then publishes through the
existing non-failing move assignment. Occupied destinations and source/destination
aliasing are supported; the same document may supply both sources under the
ordinary rules. Diagnostics never retain temporary-result handles. See the
[interface and diagnostics](../schema/persistence-and-aggregation.md#agreed-type-schema-merge-function-and-diagnostics)
and [publication mechanism](../schema/persistence-and-aggregation.md#agreed-type-schema-transactional-publication).

Instance and bulk data merging now use document-only working input/output, with
other forms converted at the boundary. Retained instance declarations contribute
directly. Promote stripped instances against their original schema, recovering
only differences from defaults or the immediate parent, before dropping binary
from the working representation. Reconstruct missing bulk data descriptions as
complete records. Later binary construction uses the merged schema and
declarations, so adopted defaults and merged parent declarations may intentionally
change instance values. Stripped data cannot recover lost explicit default-equal
selections or presentation. Source storage remains unchanged.

Recursive specialisations, bulk data prefix matching, transactional replacement,
presentation merging and first-encounter ordering remain. The former binary
snapshot equality rule, matching-form output selector, literal same-owner source
precondition and specialised-role overload matrix are superseded. Type-schema
merging is complete; see the [runtime API](../schema/runtime-api.md#type-schema-merging)
and [completed plan](../project/type_schema_merge_implementation_plan.md).
Prepared data aggregation is implemented through `merge_instances` and
`merge_bulk_data`: existing query views, one resolved merged schema, independent
live output and typed diagnostics. Both sources and private output validate before
non-failing publication. Complementary declaration validation shares construction
admission without allocating disposable binary; document construction reuses the
type merger. Strict prepared roots and the schema-backing destination guard apply.
Cheap explicit depth checks remain, while implicit failures belong to later binary
construction. See the [runtime API](../schema/runtime-api.md#instance-and-bulk-data-aggregation)
and [completed plan](../project/data_aggregation_implementation_plan.md).

The [19 October preparation contract](../schema/persistence-and-aggregation.md#agreed-document-preparation-interfaces)
agrees document-only and supplied-binary overloads of `prepare_instances_document`
and `prepare_bulk_data_document`, with typed preparation diagnostics and staged
publication. Preparation is not implemented. It preserves retained descriptions,
reconstructs stripped instances against their original schema and reconstructs
bulk records only when an entire entry description is absent. Present incomplete
records and count mismatches fail. Optional retained/binary comparison is explicit.
Selected-section extraction validates applicable metadata before removing it;
embedded types and sibling section values never influence the result. The supplied
source schema is the sole authority. Conversion publishes caller-owned documents;
its diagnostics remain separate, and merge locations borrow prepared inputs.
Preparation needs a reviewed implementation plan.
Implemented schema
switching still preserves binary data; document merging and subsequent
construction are separate operations.

The [composition contract](../schema/persistence-and-aggregation.md#complete-document-composition)
is agreed but unimplemented. Three `compose_document` overloads require a types
document and instances, bulk data, or both. Inputs are prepared single-domain
documents; output copies sections in types/instances/data order, preserving
contents and presentation. Resolve supplied types once, validate each data domain
against that resolution, then copy privately and publish by non-failing move.
There is no repeated result resolution/semantic validation, binary construction
or collision resolution. Typed diagnostics borrow source locations; occupied and
source-aliased destinations are supported. Composition also needs a reviewed
implementation plan; combined orchestration remains separate design work.

Direct final storage for description materialisation and selected type-definition
subsets remain optional optimisations. Compatibility signatures, persisted resolved
lookups, automatic bundle persistence and bundle-entry writes were excluded from
the completed persistence scope; they are not outstanding completion requirements.

## Earlier source-ingestion direction

This preserves the earlier proposed workflow for the forthcoming design discussion;
ingestion is not implemented and the user has further requirements to discuss.
Import would propose definitions from selected source declarations. Validation
would compare existing canonical definitions under a stated target ABI/profile
without silently changing them. A manifest could record an explicit projection
and deliberately excluded members. These modes are distinct from data documents
that consume an already resolved schema.

### Constrained direct-file workflow

Source-code ingestion is proposed as a development tool, now that the schema model
and its supported features are established.  It is a development task whose
performance needs to be workable on a fast development machine; it does not
need runtime-path optimisation or a full C/C++ parser.

Ingestion is expected to be configured infrequently, initially by the project's
developer.  Keeping the implementation small limits code and maintenance cost
and allows the operating instructions to remain short: identify each file and
structure, list prerequisites first, run ingestion, and address diagnostics.
Manual configuration is an intentional tradeoff.  Additional automation should
be justified by a demonstrated recurring need rather than convenience alone.

A JSON ingestion manifest contains an ordered list of requests, each naming
the exact source file containing a definition and the structure to extract.
The author orders requests so prerequisite member structures are ingested
before structures which depend on them.  Dependencies may also refer to types
already available in the schema.  The first implementation does not discover
or reorder dependencies automatically; unavailable prerequisites produce a
descriptive diagnostic.

Ingestion can recognise a structure whose members are all non-array base
members of the same type as a named-component form, retaining the declared
component names, order, and layout.  For example, an ordinary structure with
`f32` members `x`, `y`, and `z` supplies named components as well as positional
construction.  This recognition is based on the declared member types, not
on naming conventions alone. Named bitfields support analogous
selected-field overrides through their explicit bitfield definitions, preserving
unselected fields and unused bits in an existing value.

The initial pass reads the explicitly selected files without following
`#include` directives or searching other files for definitions.  Include
handling should be considered only if practical use demonstrates a need.
Extraction handles a deliberately limited declaration subset and reports
unsupported or ambiguous constructs rather than requiring a general parser
or silently guessing their meaning.  A preprocessed header view is not a
prerequisite for this initial direct-file workflow.  Physical layout claims
still require known layout rules and, where source ABI details matter, an
explicit target profile.

The manifest's exact field names and the initial supported declaration subset
remain to be specified.

The [header survey](../schema/header-survey.md) retains source examples and
target-ABI considerations. It is historical research, not a current SDK inventory
or a requirement to ingest whole graphics APIs. A previously contemplated
zero-length trailing source array could be discarded during extraction; it would
not become a schema array. The current schema rejects zero-length arrays.

## Earlier editor direction

Editor construction is not implemented. These intentions remain separate from
the schema's physical representation and should be selected only for a concrete
consumer workflow.

The resolved schema configuration is also the technical type system for editor
data entry.  It supplies the durable facts needed to inspect and edit an
instance: primitive type, enum labels, fixed-array extent, nested structure
shape, defaults and validation constraints.  Packed data is normally presented
through its logical members rather than solely as a raw storage word; raw
storage visibility remains useful for diagnosis.

The private development editor begins as a functional, schema-driven technical
inspector, not an attempt to reproduce the scope of Unreal, Unity or Godot.
Its default presentation is deliberately utilitarian:

- structures are expandable member groups;
- fixed arrays are indexed lists;
- enums are labelled selections;
- primitives are direct typed inputs;
- bit structures expose their logical fields; and
- validation failures identify the affected member path.

Richer controls, visual editing, custom layouts, or workflow-specific helpers
are added only where a real repeated workflow demonstrates a material
development-speed or error-reduction benefit.

Presentation and access policy remain separate from physical representation.
Schema-adjacent editor manifests may target resolved type and member paths to
provide labels, help text, grouping, ordering, specialised controls, or
visibility/read-only policy.  The private editor may expose technical fields,
layouts, packed members, formats and diagnostics.  A user-facing editor can
instead present curated higher-level choices while constructing and validating
the same schema instances.  Hiding a field in a presentation manifest is not
itself an authority or data-security boundary.

The schema inspection interface should be considered as a future reflection
source for constructing editor panes.  Editor-specific representation or
helpers may be separate if including them would substantially enlarge the
schema implementation. Editor pane construction remains separate consumer work.

## Earlier typed-override proposal

The earlier design proposed accepting a typed enum value in a destination of its
exact underlying integer type, while rejecting the reverse and distinct enum
types. The destination would keep its original declared type through later
overrides. There is no public typed-value override API implementing that proposal.
It is separate from document literals (enum labels), the current exact-type remap
predicate, and the simple serialisation interface described above. Earlier notes
also proposed separate public raw-code and normalised-float setters; current
document construction distinguishes those inputs by value kind instead. Reconsider
these helpers only if a consumer needs typed scalar overrides.

## Other boundaries

Unions, pointers, variable-sized fields, opaque handles/blobs, packed structure
layout, independent array strides and generated C output remain unsupported.
They are not automatically selected by ingestion or the shader-representation
discussion. A convenience catalogue of graphics formats, CSV review export and
a general instance-reference path language are also unselected ideas.
Resolved schema name lookup is indexed; callers can retain resolved indices for
repeated access. Combining the distinct
subtree-copy and whole-document-promotion traversals is not selected; their
shared value construction is already implemented.

## Completed construction refinements and remaining costs

Native copying, promotion and controlled schema/role construction now use a
private attachment path which skips sibling-name and ancestry scans already
made redundant by source/schema invariants. Ordinary editing and arbitrary
reader adapters retain checked attachment. Copies reuse same-document string
IDs or temporary mappings containing only copied IDs. Whole baked promotion
preloads string domains in lexical order, avoiding sorted-index shifts.

Instance reconciliation now compares and emits in one traversal with stack-local
lazy containers and pending array prefixes. It retains minimal declarations and
shortest prefixes without aggregate-wide re-comparison. The final binary-fidelity
check remains a separate bounded traversal.

These changes are implemented and validated; the
[role and construction milestone](../project/completed_milestones.md#schema-role-attachment-and-construction)
records their completion and checks.

Distinct strings admitted in arbitrary order still shift the string table's
sorted index. Extending preloading to selected copies or mixed generated output
would need to balance temporary planning/storage against the selected size.
Live instance edits currently stage by scanning the record tail even when only
a small descendant subtree is affected; that scan is linear per edit but could
be restricted to the affected subtree. Neither requires changes to schema
attachment or switching.
