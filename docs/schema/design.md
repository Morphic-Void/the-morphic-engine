# Schema design

This is the authoritative target specification, including the 28-29 September
live/baked document and payload decisions. [runtime-api.md](runtime-api.md)
describes the implemented schema API; the [implementation contract](implementation-contract.md)
records current delivery status, stages and acceptance checks. Existing private
records and interfaces may change to meet this design.

The [dated user answers](question-decisions-2026-09-28.md) are historical evidence.
Their decisions have been reconciled here, including subsequent clarifications;
implementations should follow this document rather than earlier discussion.

## Purpose and scope

The schema system describes logical data types and their physical
representations.  Its intended scope includes ordinary structures, bit-packed
representations, rendering-resource layouts, serialisation, and directional
construction of one representation from values in another. Packed structure
layout is deferred; bit structures are included in the first stage.

Schema definitions and instance data are distinct.  A definition declares the
meaning, layout, size, alignment, and members of a type.  An instance supplies
values of a declared type. Bulk instance data may be represented as binary or
document values, with JSON used for definitions, metadata, and ordinary small
instances. CSV is not a planned core encoding; a future review export may be
considered separately.

The canonical input to schema operations is a resolved **schema
configuration** (also described as the schema catalogue).  It contains type
definitions and their default metadata, not ordinary instance data.  Once validated and resolved, it is the basis
for instance validation, physical codecs, remap validation and
execution, direct-copy remap setup, and structure-only code generation.

[`schema-example.json`](schema-example.json) expands the previously accepted
grammar with structure `detail`, explicit offsets, and short-input examples.
It uses `element`/`count` without redundant array `kind`, and `detail.internal`
for the internal-layout marker. Its [companion notes](schema-example-notes.md)
explain expected layouts and the delivery boundary. Current resolver and generator
capabilities are recorded in the [runtime API](runtime-api.md).

The first remapper automatically matches direct members by name and type.
It does not search recursively or infer conversions. Source ingestion remains
a constrained development tool rather than a general C++ compiler.

Schema implementation code belongs in `core/schema/`.

## Authoring, resolution, and data

Schema creation and structural modification use a live document. Schema
definitions may be distributed as Morphic JSON text or baked documents. The
design separates schema-document ownership from resolution: live and
baked schema documents consume one resolver, able to read either data-model
representation through the implemented common read adapter.

```text
Live or baked schema document -> common resolver -> resolved schema
Instance document + schema access -> that document's instance byte buffer
Bulk document + schema access -> that document's bulk byte buffer
```

The **resolved schema** is constructed at runtime and is never serialised.
It contains explicit types, descriptors, and resolved references for fast
operations. Resolution, validation and layout calculation remain shared
algorithms. Re-resolution does not itself edit definitions; changes to the
document can change the schema. A usable resolution must correspond to its
definitions. Wrapper-mediated editing and inter-document reference protection
govern definition changes as described below. An intervening bake is no longer
a requirement of the target design.

There are three logical document roles with separate user-facing interfaces:
type definitions, instances, and bulk data. These remain logically separate even
when loaded from one combined baked document. Each role has the same query
interface in its live and baked forms; mutation belongs to the live form.
Instance and bulk documents consume the common schema access interface with
either live or baked schema backing. Concrete adapter and interface mechanics
remain implementation design work, not a requirement for a new general framework.

All role wrappers over combined documents may expose the original document root,
including the other sections, for source navigation and diagnostics. Their
role-specific operations act only on the data they specialise in. Schema wrappers
provide a separate `types_root()` accessor to identify the schema section, and
schema editing is confined to that section, including creation of a missing
`types` entry; it cannot change instance or bulk content. The same separation
applies to instance and bulk wrappers. Each live role wrapper owns its own
document, rather than sharing a higher-level live document owner.

The wrappers layered over the data model are:

| Role | Baked wrapper | Live wrapper |
| --- | --- | --- |
| Schema | `CBakedSchema` | `CLiveSchema` |
| Instances | `CBakedInstances` | `CLiveInstances` |
| Bulk data | `CBakedBulkData` | `CLiveBulkData` |

The baked document block and its view are separate. The block may be owned
locally or by the host; baked wrappers access it through views. Destroying a view
does not itself release the block. Its owner controls disposal and must keep the
backing alive while any remaining view needs it, including views for other logical
roles in a combined document. Promotion and demotion preserve their source;
callers explicitly dispose of versions and owned blocks when no longer needed.

The same ownership separation applies to baked instance and bulk payloads.
`CBakedInstances` and `CBakedBulkData` hold non-owning binary views, not owning
byte buffers. The associated payload allocation is external to the wrapper and
may be owned locally or by the host, independently of the baked document block.
Destroying the wrapper releases neither allocation. Both owners must keep their
storage alive and at stable addresses while views use it. Transferring an owner
to the host can preserve the view when the allocation does not move; relocating
the bytes requires rebinding views before further access.

`CLiveInstances` and `CLiveBulkData` always own their associated byte buffers.
Borrowed access to those buffers does not transfer ownership. Promotion copies
the baked payload into live-owned storage; demotion creates new externally owned
payload storage without taking the live source's buffer.

Instance and bulk documents reference their schema through inter-document
reference counts. These do not own or extend the schema wrapper's lifetime.
Mutation of a referenced live schema fails without changes and triggers an
assertion. A referenced schema cannot be moved. Failure should leave a safely
rejected or unusable state, with dependent processing prevented. A no-return panic
is an absolute last resort, not the prescribed response to a schema failure.
The 29 September review supersedes the earlier mandatory shutdown rule for
referenced destruction. Each schema keeps a reference count and an intrusive
linked list of its client bindings; the links reside in the participating clients.
No separately allocated registry or shared lifetime sentinel is required.
Before destruction releases schema state, it invalidates and detaches those
bindings. Later schema-dependent client operations reject an invalid binding
without dereferencing the destroyed schema or using stale resolved data. Releasing
an already invalidated binding is safe. Client registration, release and any
permitted client transfer maintain the list and count together. This mechanism
does not own the clients or extend the schema's lifetime. Borrowed raw views still
obey their existing lifetime rules; invalidation cannot repair a retained raw view.

Callers access underlying live documents through their wrappers so editing stays
coordinated. They can edit an unreferenced independent schema copy while existing
documents continue using the unchanged source. No automatic data migration or
inference of the caller's intended schema evolution is introduced.

Re-resolution of unchanged definitions is permitted while the schema is referenced.
On success it produces the same data and preserves the meanings of existing
unversioned handles. On failure it clears the resolution; retaining old tables
through allocation failure is not required. Access is single-threaded, including
within a multi-threaded system. This stability applies within the same schema,
not across independently promoted/demoted documents.

Public document handles have three role types: schema, instance and bulk. Live
and baked wrappers of one role share its public handle type, with potentially
different internal interpretation. Handles carry no individual document identity;
using one with another document or representation is caller error. Live handles
survive mutation when the underlying live data-model handle survives. Borrowed
names, strings and other document views expire after any mutation of that document.
Binary views across buffer mutation/growth remain the caller's responsibility;
no held-view tracking is added. Resolved names borrow document strings: a live
schema owns its live document, while a baked schema's user manages backing lifetime.

A schema can contain empty types, including in saved form. An empty type produces
no resolved data description; a member of that type has a zero-byte resolved
extent. A containing type whose members are all empty is itself empty. Otherwise
the containing type remains valid, with its non-empty members supplying its data.
Empty references do not fail resolution. Instance/bulk documents cannot reference
a schema while it is edited; later binding fails if the data description does
not match. Generated C++ omits empty type definitions and members whose types
have no resolved data description; they require no physical C++ storage. The
schema document retains those definitions and members.

Empty types retain queryable resolved type identities and zero-byte member
records. Name lookup, semantic member order and document-to-schema mappings
remain available. Having no resolved data description means that the type
contributes no physical storage; it does not remove its identity from queries.
An empty type has size and stride zero and alignment one. Metadata requesting
positive size or stronger alignment is invalid, including for a structure whose
members are all empty. Empty members do not add alignment or padding to their
containers. Arrays with a positive count of empty elements are also empty;
the positive-count rule itself is unchanged.

Malformed schema validation/resolution reports useful information for human
review, comparable in purpose to a parsing report and suitable for `MV_REPORT`
or similar logging. Internal node handles may construct names/paths; a public
diagnostic handle for direct recovery is not required. Hand-authored baked input
is the main expected source of such failures because live editing is mediated.

Loading always establishes binary backing for instances and bulk data, either
from supplied binary data or by materialising embedded document values. Each
logical instance document has one associated byte buffer; each logical bulk
document has its own byte buffer. These are per-document associations, not two
global buffers shared by every document. A payload reference therefore needs
only an offset within its associated buffer, without a per-entry buffer ID.
Bulk counts and checked extents are still necessary. The locator contract below
defines locations; the caller supplies the binary file association.

When baked loading receives a host-owned payload, it binds a view after the
ordinary alignment, extent and schema checks. When loading instead materialises
embedded values, the created owning byte buffer is returned separately from the
baked wrapper. The caller can retain it locally, save it, or transfer its ownership
to the host. Successful publication must leave a usable wrapper together with
an explicit owner for any newly allocated payload; ownership is never hidden in
the baked wrapper. Failed loading cleans up temporary owned allocations without
releasing caller-supplied backing.

### Non-destructive promotion, demotion and output

The starting managed representation is a baked wrapper with associated binary
backing (loaded directly or created on load) and resolved schema access. Promotion
constructs a new live representation and copies the associated instance/bulk
binary backing into independent storage. The baked source, its backing data and
its existing connections remain unchanged and usable on success or failure.
The extra allocation and copying are intentional. Failure must not publish a
partially constructed live result as usable; temporary destination resources
need cleanup without consuming the source.

The live document acquires all required strings during construction. Schema
promotion recreates the resolved schema against that live document and succeeds
only after resolution succeeds. Its baked source need not already be resolved;
an unresolved but valid schema can produce a resolved live result. The
source resolution remains available to its existing consumers. The new result
must use the live document's strings and occurrences and must not depend on the
old baked backing. Source handles remain scoped to the source resolution; they
do not become handles into the new version. A schema is supplied explicitly when
creating, promoting or demoting an instance/bulk document. Conversion does not
inherit its source's schema reference. Ordinary schema-matching rules determine
success during the operation, without a separate pre-validation pass. Existing
connections are not automatically redirected.

Confirmed on 1 October for bulk promotion with an explicitly supplied different
schema: referenced types must match in structure and value interpretation.
Compare type and field names by text, member order, physical layout, primitive
types, array counts, enum labels/values and bit masks/interpretations recursively.
Different defaults are permitted because promotion copies existing binary values
without applying defaults. Unrelated types in either catalogue need not match.
This does not change the stricter default-sensitive definition matching specified
for remapping below.

Instance conversion permits different defaults and regenerates declarations
from authoritative binary snapshots. The 3 October reconciliation decision
supersedes both the earlier matching-default requirement and the intervening
changed-default-only preservation rule. Bases are compared with destination
defaults; specialisations with their immediate parent's snapshot. Matching named
values are omitted, differences are explicit, and arrays retain the shortest
prefix through the last difference. Necessary equal values within that prefix
remain explicit; no sparse-array grammar is introduced. Old declarations are
not decoded or checked for agreement. Generated literals must faithfully
reproduce meaningful encoded values or conversion fails atomically.
This applies to promotion, both output forms and demotion, for live and baked
destination schemas. Snapshots are copied without applying defaults or inheritance.
Every declared instance type group is retained and checked, including empty
groups and built-in primitive groups. An empty group does not permit a missing
or incompatible destination type.

The reverse operation, demotion, creates the corresponding baked representation
with an owned `CBakedDocumentBlock` and independent copies of associated binary
payloads. It preserves the live source and its existing connections on success
or failure. Ownership of the new document block and payload buffer can subsequently
be transferred to the host; the baked query wrapper uses views over both.
Demotion returns the wrapper, owned document block and (for instances/bulk) owned
payload buffer as separate components. The caller manages their joint lifetime,
and may save or transfer either backing allocation independently. Demoting
a schema does not resolve its new baked wrapper: the caller may only want to save
the block. Host transfer must leave the borrowed view backed by storage for its
required lifetime. Callers dispose of either version explicitly when it is
no longer required, after accounting for its remaining users.

Schema promotion and demotion copy only the `types` section into an independent
document with its own root object. Instance and bulk sections of a combined
source are not included in that schema result; the combined source remains
unchanged. Demotion does not require the live schema to be resolved, but its
schema document content must be representable in the baked data model. Conversion
requires empty destinations and preserves source and destinations on failure;
it does not publish a partly constructed result. Diagnostics from failed
promotion must not retain handles into a destroyed temporary live document.

Instance and bulk promotion retain different document content:

- An instance document retains names and the nested specialisation hierarchy,
  with offsets to complete binary snapshots. Declarations are regenerated from
  those snapshots against destination defaults or the immediate parent.
- A bulk document regenerates existing embedded record arrays from binary and
  retains reference-only entries without adding arrays. Organisation, offsets
  and collection information are retained in both cases.

The describing document and managed data form one coordinated working set.
Instance declarations preserve authoring and override meaning while each binary
instance holds its completed snapshot. Capture and editing coordinate both and
update descendants as part of the user-facing operation, as specified under layering.
Replacing or removing a selection discards its previous binary value without
requiring that value to have a representable declaration. Retained selected
values still require valid declarations. Thus an unlabelled enum code may be
repaired by replacing or removing its selection, but cannot silently survive
as a retained selected value in the edited instance or its descendants.

Output creates a new live document, strips information irrelevant to the chosen
form and adds information required by it. Full-document output reads binary
values through the schema and embeds them; reference output retains offsets and
writes the associated binary block. The working representation is preserved.
The output document can then be written as text or baked, independently of the
embedded/external payload choice. Instance/specialisation output always retains
declaration information and the hierarchy needed to reconstruct complete values,
including distribution output. Embedded instances use the existing named
declarations and inheritance hierarchy, without an additional flattened-value
field. Binary output stores each instance's complete independent snapshot.
Bulk record arrays can be retained for review or omitted with binary backing.
Both forms remain fully editable after promotion. Embedded text still includes
locator objects so later baking has the reserved nodes needed by loading.

The existing generic `document_translation::promote` already copies a borrowed
baked document into a live document without consuming its source. Generic `bake`
likewise produces a block while preserving its live input. These primitives can
support the wrapper operations; wrapper-level payload copies, schema bindings
and output preparation remain additional responsibilities. Non-destructive
promotion/demotion supersede the earlier destructive ownership-transfer proposal.

### Locators, loading and buffers

Each instance/specialisation object has a sibling `locator` object alongside its
existing contents. Each named bulk entry is an object containing `locator` and,
when embedded records are present, `data`. A locator contains a zero-based unsigned
32-bit `offset` into that logical document's buffer and a separate Boolean `valid`:

```json
"locator": { "offset": 0, "valid": true }
```

Optional `size` equals resolved type size (including padding) times record count.
Stride equals resolved type size. Instance `count` defaults to one, may be present
in baked input only as one, and is stripped from live instance documents. Bulk
count is optional for manual editing, but absent embedded `data` requires at least
one of `count` or `size`. For a zero-byte resolved type, absent embedded `data`
requires an explicit positive `count`: `size: 0` cannot determine the record count.
An embedded array supplies its count even when its records occupy no bytes.
Supplied size/count are checked against the type and any
embedded array. A live bulk collection may temporarily have zero records; baked
bulk collections may not. Valid locator extents in a buffer must not overlap.

Live bulk creation can also allocate an unpopulated named array for a resolved
type and requested record count, without embedded records or a source payload.
It allocates the aligned extent in the document's associated buffer and records
its locator and count/size, making the array available as a remapping destination.
This is a live construction operation; it does not relax the baked loading rules
below. Allocation does not promise zero-filled storage or apply schema defaults.
The caller populates logical fields before reading or exporting them.

Baked documents reserve the required locator nodes and validity flag; loading
does not insert fields. With a supplied binary buffer, offsets must already be
valid and the block may be immutable. Without one, embedded values are required.
Loading materialises them using valid offsets or updates reserved offsets through
the mutable baked interface if permitted. Existing offsets are checked against
the embedded values. If offsets need updating and the block is not writable,
loading fails. The agreed mutable-integer API extension recomputes canonical
width from the replacement value, permitting widening and narrowing while
preserving signedness, notation, prefix and structural flags. Payload slots
remain 64-bit; document floating values remain fixed binary64. This extension is
implemented, coordinator-reviewed and committed separately as stage 1 (`d92dddd`).
The previous setters rejected widening and narrowing, so
an initial 0xffffffff offset alone did not solve updates to smaller offsets.

Instance/bulk buffers have 128-byte base alignment. Supplied buffers lacking it
are rejected rather than copied into aligned storage. New entries append aligned
extents. Larger bulk replacements append and leave old bytes unreferenced;
same-size/smaller replacements may retain their offset. Working buffers do not
compact, and live offsets need not follow document order. Baking creates a new
buffer in document traversal order with matching baked locators; baked extents
follow document order. Promotion/demotion remain non-destructive copies, even
when output reorders payloads.

Instance and bulk construction does not require a blanket zero-fill pass.
An optional caller-invoked operation clears unaddressable storage, as described
under gaps and storage initialisation. For deterministic binary output, invoke
it on the final output buffer after packing and copying, so it covers the final
alignment gaps and any padding imported by aggregate copies. It is not invoked
implicitly during allocation, construction, promotion, demotion or output.

Stored binary byte order is little-endian. The caller supplies the associated
binary file when creating/loading the document. File matching is manual; schema
or layout identity embedded in the file is beyond this work.

When both embedded and binary representations are loaded, comparing them is an
optional validation path for instances and bulk. Distribution can skip comparison
for speed. With it disabled, saved binary snapshots are authoritative until an
edit; loading does not rebuild them from declarations.
Comparison converts embedded values to their destination encodings and compares
addressable fields, ignoring byte padding and unused bits. Encoded values must
match exactly, including signed zero and distinct SNORM codes, except that all
NaN encodings compare equal because document text does not express NaN payloads.
No floating-point tolerance or decoded-value equivalence is applied.

Load failure is fatal only to the affected logical instance/bulk document, never
to its resolved schema, even if their views share a physical baked block. Failed
append leaves existing entries usable. Appended bytes may remain unreferenced,
but no locator for the failed entry is published. Validation/preparation ordering
for in-place edits and descendant updates is implementation work. Allocation
failure during these updates is application-critical, without a required
transactional rollback or recoverable partial-edit model.

### Schema-document normalisation

An existing schema document can be normalised by creating a new normalised
document. The source document remains unchanged, including when it is baked
or backs an existing resolved schema. This is a separate document-producing
operation; resolving a live or baked document does not silently rewrite it or require
the caller to request a normalised document first.

Normalisation uses a successfully resolved schema and its backing document to
construct the new document. Validation, type-reference resolution, and layout
calculation belong to schema resolution; normalisation reuses those results
rather than implementing a second validator or layout calculator. A convenience
entry point accepting a document should use the same resolution path before
producing normalised output, rather than a standalone normalisation engine.

The output uses the agreed canonical document representation, including
structure `detail.alignment` and `detail.size` taken from the resolved
descriptions. It preserves declaration order and meaning, including positional
member order and enum-alias preference. Invalid supplied metadata fails
resolution and cannot be silently repaired by normalisation. The new document
can subsequently be baked and resolved through the ordinary lifecycle.

The output contains schema definitions and their document metadata, not a
serialisation of resolved slots, indices, or other runtime internals. Exact
normalisation APIs remain to be specified. A request to create a new normalised
document is outside the initial delivery. Any schema-document writer provided
at any stage must nevertheless emit the normal form; deferring this operation
does not permit non-normal output from schema generation.

### Runtime representation

Declaration names remain identifiers into the backing document's name table.
Descriptive vocabulary and type-reference strings become explicit runtime
types and descriptors rather than text reinterpreted during each operation.
The owner uses variable-length sequences of fixed private records, with member
counts determined by the described structures. Public observations are distinct
from private storage and may expose wider quantities.

Current private record sizes, fields and internal range access are documented in
the [runtime API](runtime-api.md). They are implementation facts, not a public ABI
or serialised format and not constraints on representing live document identities.

Primitive, enum and bit-structure records directly supply their physical
primitive. Enums and bit structures retain their semantic category, storage
reference and ordered children. Structures and arrays have whole-object
primitive `none`; their member or element descriptions supply the next level.
Bit fields directly supply their logical primitive, including enum storage,
separately from the containing word's physical primitive.

Members expose their full byte extent, including padding owned by their type.

Layout arithmetic remains checked unsigned 64-bit arithmetic. An individual
described allocation, including nested member ends, array products and tail
padding, must fit `memory::k_byte_size_ceiling` (0x80000000 bytes, inclusive)
before narrowing. This replaces acceptance based only on the target `size_t`
range. The exact 2 GiB extent requires unsigned storage. Schema strides remain
32-bit; the memory token/view's 16-bit stride field imposes no schema limit.
Counts, document IDs and schema handles retain their existing widths. Whole-file
positions and streamed totals are a separate future domain, not in-allocation
offsets subject to this representation.

Internal operations validate their schema/type once and use transient typed
first/count ranges over the existing member, label or field vectors. Sequential
access borrows the records directly without repeated parent decoding or copying
public observations. Related type records can be reused while processing a
member or repeated array elements. These ranges allocate no persistent side
table and store no pointers into allocations. Clear, move, re-resolution and
owner destruction invalidate transient access. Checked public inspection and
instance/buffer validation remain required.

### Resolved-schema ownership and access

Wrapper ownership, handle and re-resolution rules are specified under
authoring and resolution above. The implemented live/baked wrappers and their
client-binding invalidation are documented in [runtime-api.md](runtime-api.md).

Serialisation and remapping callers mainly need to locate the appropriate
resolved type description and pass it to a generic operation along with the
instance buffer.  That operation traverses resolved indices internally.
Temporary read-only references obtained for an operation do not introduce
stored pointer/reference links between schema records.  The public interface
need not expose all slot-storage details merely to invoke these operations.

The editor navigates the document through the common query interface and
consults resolved descriptions for types, sizes, offsets and instance access.
Instance edits affect instance data; callers perform definition edits separately
and explicitly arrange resolution and any later use with data.
The mapping table supports live and baked occurrences through the shared query
boundary. Schema promotion recreates this correspondence by resolving the live
document. Mapping keys identify occurrences, not merely interned name strings.
A lookup without a mapped resolved counterpart returns zero. Public role handles
and diagnostic expectations follow the target rules under authoring and resolution.

The baseline is that named schema elements can be mapped. Named components
in the same-type named-component form are included in that baseline. Ordinary
array elements do not need individual mappings; only the reference to the
array description needs a mapping. The array's resolved description supplies
its element type, count, and stride without per-element mapping entries.

Mapping coverage is secondary to resolved-schema efficiency. Do not introduce
extra resolved records or a less efficient resolved layout solely to map every
named document element. Where efficient representation leaves an element
unmapped, lookup returns zero. The baseline is therefore a coverage aim, not
a requirement for a one-to-one document-to-resolved-slot representation.

Establish the efficient resolved representation first, then derive and document
the user-facing rules for access availability from that representation. Those
rules should explain which elements have mappings and when lookup returns zero;
they must not constrain the layout in advance merely to provide broader coverage.

A complete duplicate navigation interface over the resolved slots is not
assumed.  Editor-specific helpers may remain separate from the minimal runtime
access interface.

### Resolved data and generated declarations

**Resolved data** is instance data converted into the physical representation
described by the resolved schema.  It is distinct from the schema's runtime
machinery and will frequently be serialised.  Data formats include entirely
binary blobs as the principal bulk-data path and, in
limited cases, document-form instances distributed as baked documents or JSON
text.  Baking a document alone does not make it resolved schema or resolved
data.

Each instance document materialises loaded values into its associated byte buffer;
each bulk document uses a separate associated buffer. Live wrappers own these
buffers; baked wrappers borrow them from external owners. Selection of an individual named instance can
then copy its contiguous physical representation into adequate caller storage, without
reinterpreting document values on every access. Capture from application storage
updates or appends binary values and the associated live reference document.
Bulk records follow the same model. No application-wide population wrapper is
planned. These interfaces and their ownership/lifetime contracts are implemented
and documented in the runtime API guide.

The system also generates simple POD C++ data-structure declarations.
Generation does not produce per-structure operational code: no serialisers,
remappers, member functions, or generated access routines.  Serialisation and
remapping use generic operations over bounded byte views and the relevant
resolved schema descriptions. Typed template wrappers may be added later, but
are not required for the first pass.

Generated C++ enums use `enum class` with an explicit underlying type matching
the schema's declared integer storage.  Boolean declarations use a `using`
alias for `std::int8_t`.  `f16` uses the existing `fp16data_t` type.  Bit-structure
descriptions emit one named `constexpr` declaration for each field, consisting
of the containing storage-word type and mask value; no additional field
descriptor object is generated. The logical field type separately determines
value interpretation and signedness; a mask constant is not an enum field value.
These constants describe the fields; instance storage remains the declared
base storage type.  No operational code is implied by these declaration forms.

The next data implementation establishes binary buffers as the common runtime
representation, with document conversion at load and output boundaries. Binary
file packaging can be delivered separately from in-memory materialisation.
Source-code ingestion remains a late stage. Development is iterative, with the
definition document revised as functionality is added.

The first implementation stage resolves and inspects schemas and generates
C++ structures as part of validation.  Instance construction and serialisation
are outside that stage.  Resolution covers all described type categories:
integers, floating point, booleans, enums, general/nested structures, fixed
arrays, and bit structures. Declaration validation compiles C++17 output and checks
size, alignment, member offsets, standard layout, and trivial copyability, as
specified in the implementation contract. Compiler-based checks belong to
validation tooling, separate from generated structures.

The initial delivery excludes a separate operation for creating a normalised
document. Explicit member offsets and increased structure alignment should be
included initially only if adding them later would require retroactive changes
beyond the local scope of those features. The implementation-contract assessment
defers them: resolved types and members already contain size, alignment, offsets,
and gaps, so later support changes layout calculation and export locally. The
first delivery rejects these unsupported layout requests explicitly.

## Editor use

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
first implementation.  Editor pane construction is not part of that stage.

## Definition documents and aggregation

### Logical documents and physical packaging

The model has three logical document roles: type definitions (`types`), instances
(`instances`), and bulk data (`data`), each with a distinct user-facing interface.
There is one definitions catalogue for an evaluation context. Definitions can
be resolved alone; instances and bulk data require the applicable resolved
definitions. The three roles may share one physical document or use separate
documents. This does not require three physical files or duplicate catalogues.

Instance definitions may occupy one or several documents. Confirmed on 1 October:
one instance wrapper and its associated byte buffer cover the complete `instances`
section, including all base instances and specialisation branches. Each branch
provides a naming scope within that logical document, not an independently loaded
document or buffer. Separate instance documents each have their own wrapper and
buffer. A combined baked document may still carry schema, instance and bulk roles.
This supersedes the earlier description of each main branch as a logical document.

Reference ordering differs between definitions and instances. Structure
definitions may refer to types declared later in the definitions document.
An instance's specialisations inherit from that enclosing instance, which is
evaluated first. There are no explicit forward instance-reference strings.
Combining definitions and instances in one baked document does not introduce
a different inheritance mechanism.

The resolved schema describes the one applicable definitions document through
the common schema interface. A combined package must identify the
definitions portion separately from instance branches.  Multiple instance
inputs do not create multiple structure-definition catalogues.  Document-local
name, string, and node indices from separate baked documents must not be
compared as shared identities; explicit type references resolve against the
one applicable definitions document, and instance inheritance follows containment.
Promotion preserves the old baked wrapper and its existing connections, creating
an independent live representation. The underlying block has a separate local
or host owner, who disposes of it only after remaining borrowers have finished.

The accepted sample illustrates the combined sections; separate documents
retain their applicable sections. The layout requires no cross-instance-document base
lookup or path-root convention: references are implicit in containment or
explicit references to named types. The evaluator receives the resolved schema
and selected type explicitly for a separate instance document; document-local
identifiers remain document-local.

### Definition contents

The definitions section contains structure, enum, and bit-structure definitions.
The reviewed section name is `types`, containing category objects
`enumerations`, `structures`, and `bit_structures`, as in the accepted sample.
These categories are organisational only: names referenced as types must be
unique across the definitions document, not merely within a category.
Instance names may recur in different subtrees, provided each instance can be
identified unambiguously by its path.  A branch root acts as a namespace for
instance and override names.  A branch may contain multiple instances and
overrides, but their names must not collide within that branch.  This scoped
instance naming does not relax document-wide structure type-name uniqueness.
Built-in primitive spellings do not need to be redeclared.

### Reviewed document layout

The reviewed embedded-value package separates three concerns. Working instance
documents preserve declarations and add binary offsets; working bulk documents
can remove embedded records once backed by binary. Output projects the information
needed for its selected form. Locator syntax is specified under loading and buffers.

- `types`: structure, enum, and bit-structure definitions.
- `instances`: entries grouped by type, with named base instances beneath each
  type and specialisations nested as children of their base. Further nesting
  can express a chain of specialisations.
- `data`: entries grouped by type, with each named entry holding a `locator` and
  optional `data` record array. These collections have no specialisation mechanism.

For embedded-value authoring input, two container names and their relationship are
settled: `declaration` contains the instance's supplied member values, and
`specialisation` contains named child specialisations. Each child has the same
container arrangement, allowing further specialisation.

Nesting establishes the base relationship: a child inherits from the enclosing
instance whose `specialisation` container holds it. Its `declaration` supplies
only its overrides; omitted members retain inherited values. A top-level base
instance instead applies its `declaration` to the type's defaults. No explicit
base name or path is required in this form. This supersedes the earlier rule
requiring every override to supply a target reference. Coincident names in
other branches still establish no relationship.

An ancestor base fits the existing prohibition on forward targets and targets
below an override. The parent is evaluated before its specialisations,
regardless of the textual ordering of the two containers. The surrounding
package and type-descriptor forms are illustrated by the accepted sample;
later extensions are recorded separately.

The bulk collection name identifies the array, not its individual
records; it does not introduce references between those records.
Embedded bulk records require every member explicitly, recursively through nested records
and bit structures. Positional structures and fixed arrays must have their full
declared lengths here; short-input/default completion belongs to `instances`,
not `data`. Gaps are not members; the caller may explicitly clear them through
the unused-storage operation.
Separately, the live bulk API can allocate an unpopulated array to fill later
through remapping or other explicit writes; it requires no embedded record array.
Binary payload association, instance construction and bulk-data processing are
implemented through the live/baked roles described in the runtime API guide.

### Definition validation and assembly

Initially no mandatory document headers (such as `format` or `version`),
document versioning, or permitted-name whitelist are required.
Initially declaration names use the ASCII subset of C++17 identifiers, with
applicable keyword and implementation-reserved-identifier checks. Non-ASCII
declaration names are rejected, not transliterated or renamed. This is a syntax
restriction, not a permitted-name allow-list; general document text remains
Unicode-capable. Generated namespace identifiers use the same initial subset.
Declaration names outside these rules are hard errors, and
unknown descriptor/document properties are hard errors. Vocabulary properties
such as `default` are schema syntax, not C++ declaration names. This does not restrict
permitted numeric spellings.  Type descriptors still require enough
information to define their contents, such as an array's element type and
count. The reviewed sample establishes the descriptor forms; additional
validation details and extensions are recorded separately.

Duplicate declarations within the applicable naming domain are hard errors,
including duplicate structure type names and duplicate members within a type.
They are not merged or interpreted as overrides.  Repeated instance names in
different unambiguously identified subtrees are permitted.  Forward references to types
declared later in the definitions document are allowed. Circular type references
are rejected.  This is separate from the manually ordered code-ingestion list.

Schema text input must reject the document parser's `name_collision_extension`
finding before resolution or baking; the parser's default policy already
excludes it. Resolution checks surviving duplicate named declarations in either
document form, including repeated named entries in a member array. It cannot
reconstruct source duplicate properties already merged into an ordinary array.
Direct live input does not waive schema shape and declaration validation.

The one definitions document may contain Morphic, DirectX, Vulkan, and other
selected definitions.  Any authoring-time assembly produces that single
definitions document before resolution; it does not create additional runtime
definitions documents or type namespaces merely because inputs came from
different files.

Documents that feed or act upon the configuration are logically distinct from
its type definitions, even when packaged in the same baked document:

- ingestion manifests select and profile source declarations which may create
  proposed definitions or validate existing ones;
- code-generation manifests select resolved configuration types and output
  options; and
- instance and bulk documents consume the resolved configuration without
  redefining its types. Automatic remap setup receives source and destination
  structure definitions; it does not require an authored member-pair document.

Source ingestion has two non-interchangeable modes.  **Import** creates a
proposed definition from an allow-listed source declaration.  **Validation**
compares an existing canonical definition with such a declaration under a
specified target ABI/profile.  Validation is non-mutating: changed headers
produce diagnostics rather than silently rewriting the configuration.  A
manifest may also validate an explicit projection, recording source members
deliberately excluded from a durable configuration type.

### Development-time structure ingestion

Source-code ingestion is a late implementation stage, after the schema model
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

## Declaration and reference identifiers

The document model has separate interned domains for property names and string
values.  The schema loader preserves that distinction:

- a property-name identifier declares a schema entity or member name;
- a string-value identifier can supply reference text; and
- context determines what kind of declaration or reference is permitted.

During initial loading, the loader can build a one-way, configuration-local
mapping from common string-value identifiers to identical property-name
identifiers.  This permits a reference such as the string `"Vertex"` to be
resolved to the declared-name identity without repeated textual comparison.
Textual equality alone is not semantic resolution; the surrounding schema
position determines whether the name may denote a type, enum label, member, or
other entity.  No reverse map is normally needed.

Interned name identity alone does not identify a declaration occurrence:
different structures may declare the same member spelling, and a name may
also occur in an enum or other context. A reference or label string resolves
within its known type context: named types use the one definitions catalogue
and enum labels use the matching enum. The name-domain map does not perform
this semantic selection. After target selection, a mapped document occurrence can
be connected to its runtime description. This auxiliary table does not guarantee
a resolved slot for every occurrence or replace the resolver's own type
relationships and lookup operations.

The current reference model is:

- an explicit type reference identifies its named definition in the one
  definitions document; and
- an instance specialisation derives its base from containment and overrides
  selected values, potentially through a chain of specialisations. It does not
  need a reference string to establish this relationship.

The second use builds on the specialisation rules below. Instance names may
recur in different subtrees; their containing branches distinguish them.
An override's enclosing instance establishes its base through the
`specialisation` container; `declaration` holds changed values. No base-name
lookup or target path is needed for this nested form. There is no forward
override link or downward search for a matching name. This differs from
forward type references, which remain permitted. Coincident declaration names
alone do not create an override relationship. Each named specialisation can
be independently selected as an instance. Arbitrary
record links are not a current requirement.  A selectable instance specialisation supplies inherited
values and overrides; its referenced type determines the permitted members
and physical layout.  Selectability does not by itself require a distinct
physical layout or generated C++ structure for each specialisation.  The
remaining package details are described in the document-layout discussion.

The loader also discovers fixed vocabulary such as `u8`, `f16`, section names,
and layout keywords once, resolving them to compact internal tokens.  These
document-local IDs are never durable cross-document identities.  After
aggregation, consumers use resolved type handles, member indices, offsets, and
internal tokens instead of reparsing strings.

### Reference scope under the nested layout

The nested layout supersedes the earlier instance-reference path scheme.
There is no current requirement for relative or absolute instance-reference
strings, enclosing-namespace search, or cross-document instance-reference
visibility. The previous `../`, `/`, and array-traversal rules are therefore
not part of the current schema reference contract. Type lookup uses the one
definitions document; specialisation inheritance uses the enclosing instance.

Document navigation, diagnostics identifying member locations, and selecting
a named instance remain useful operations. They do not imply a path-based
reference language inside the schema or instance documents. Any future need
for such a language should be considered separately when a consumer requires it.

## Morphic JSON and numeric metadata

Schema documents and document-form instance data use the Morphic JSON
extensions and document metadata where appropriate.  The existing
[document numeric contract](../data_model/document_text_format.md#value-classification-and-numbers)
supplies integer signedness and notation; these should remain numeric values,
not strings introduced solely to preserve formatting.

Signed integers carry signed-domain metadata.  In text, non-negative signed
integers use an explicit `+` (including `+0`), while negative integers use `-`.
Unsigned integers have no sign.  Bitfield masks and similar bit-oriented values
use hexadecimal notation with the `0x` prefix rather than the alternate `#`
prefix. Masks use the full width of their storage type, including leading zeroes:
two hexadecimal digits for 8-bit storage, four for 16-bit, eight for 32-bit, and
sixteen for 64-bit storage. This is the containing storage width, not the width
of the selected field. Document numbers have no C++ literal suffixes.

Ordinary schema values of types `i8`, `u8`, `i16`, and `u16` use decimal notation.
Values of types `i32`, `u32`, `i64`, and `u64` use hexadecimal notation by default,
even when the current value is small. This refers to the declared schema type,
not the smallest width of the particular value or the document model's internal
numeric storage. Signed hexadecimal values retain the signed-domain sign, such
as `+0x1` or `-0x10000`; mask notation takes precedence over ordinary value rules.
Structural quantities such as counts, offsets, sizes, and alignments use decimal
within the inclusive range -65535 through +65535, and hexadecimal outside it.
Numeric notation does not itself select a schema type or change its physical
width. These conventions govern construction and normalisation, not input
spelling restrictions.
Input accepts valid JSON and supported Morphic extensions under the document
parser's acceptance policy without requiring canonical schema notation.
For example, a decimal mask is not rejected merely for lacking a `0x` prefix,
and a non-negative integer need not carry an explicit `+` on input to a signed
schema field if its value is representable in that field.  Schema processing
validates the value against its expected type before normalising its metadata.
Canonical document metadata is emitted when constructing a new document;
resolution does not rewrite its live or baked input in place.

Schema-document representation uses the existing `CIntegerMetadata` domain,
notation and prefix flags and the document writer. It must not reparse formatted
numeric strings or introduce a separate schema numeric formatter. The metadata's
width remains the document model's smallest valid width in the selected domain;
the declared schema type selects notation and must not be encoded by overriding
that width. Generated C++ source has its own literal spelling requirements.
The generic document writer does not retain leading-zero display padding, so a
generic Morphic round trip alone does not establish the full-width mask display
required of future schema-document output. Schema normalisation/output APIs and
any handling needed for that display requirement remain deferred.

The application of these declared-type notation rules to instance and bulk
output still needs clarification. Current data-role scalar decoding constructs
integer values with decimal metadata; this review does not change that behaviour
or imply that data output already performs schema-document normalisation.

Permissive spelling does not permit invalid schema definitions or instance
values.  Malformed syntax, invalid layouts, and values incompatible with their
expected types are errors.  Validation should produce descriptive diagnostics
identifying the affected type/member path and the reason for rejection.
Initially validation stops at the first error and outputs its cause, along
with the affected member and structure where appropriate and available.  The
implementation contract specifies an allocation-free structured diagnostic with
document locations and related ranges where available. Schema-resolution failure
already has the empty-result contract specified under ownership and access.
Instance/bulk load failure is confined to that logical document, including when
its baked input shares a block with schema definitions.

## Primitive physical types

### Literal validation

The following conversion rules make default validation concrete. They are
implementation choices derived from the existing document numeric contract,
the agreed permissive input policy, and the explicit `fp16data_t` exception.
The same rules apply when instance construction consumes document values;
typed override compatibility is a separate check.

- Integer destinations accept signed or unsigned integer payloads in range,
  regardless of source notation. Finite floating-point payloads are accepted
  only if integral and in range; reject fractional truncation. Check exact
  signed/unsigned boundaries before casting, without routing 64-bit integer
  payloads through `double`. Floating negative zero converts to integer zero.
- `f32` and `f64` accept integer or finite floating-point payloads, allowing
  precision rounding. Reject finite overflow and non-zero underflow to zero;
  representable subnormals are allowed. Preserve floating signed zero.
  The document has already rounded numeric text to its stored value; the
  schema cannot recover digits lost in parsing.
- `f16` uses the existing transport conversion, including its rounding,
  underflow, and finite-clamping behaviour. Do not impose the `f32` rejection
  policy around that conversion or bypass it with schema-specific bit packing.
- `b8` accepts document booleans or finite numeric payloads: either sign of
  zero becomes false and every other finite value becomes true. Do not accept
  numeric strings or floating special strings as Boolean input.
- Enum values use matching labels. Declaration values themselves must be
  integral and fit the enum's integer storage. An enum must contain a label;
  duplicate values are aliases, while duplicate labels fail.
- Strings are not general numeric coercions. Only the explicitly listed
  floating special spellings and enum labels have typed meaning. Null is invalid.

Masks and extent metadata must represent non-negative integers and satisfy
their additional width/count/alignment constraints. A bit field's integer
domain must fit both its logical type and its mask width; enum labels must all
fit that width using the enum's signedness. Boolean fields have the logical
domain 0/1. `unorm` requires an unsigned integer primitive; `snorm` requires a
signed integer primitive and at least two mask bits. Neither interpretation
applies to enums or Boolean fields. Input and defaults accept raw integer codes
or normalised floating values under the codec rules below; output uses stored
integer codes. This replaces the stage 1 restriction of explicit defaults to
finite normalised values in [0,1].
Absent interpretation means the declared type's ordinary value semantics.
Other interpretation spellings require an explicit extension, not silent fallback.

### Physical vocabulary

The initial primitive vocabulary is deliberately small:

| Category | Types |
| --- | --- |
| Signed integer | `i8`, `i16`, `i32`, `i64` |
| Unsigned integer | `u8`, `u16`, `u32`, `u64` |
| Floating point | `f16`, `f32`, `f64` |
| Boolean storage | `b8` initially |

Boolean storage is initially assumed to be 8 bits.  Wider boolean storage
types from earlier drafts are not an initial implementation requirement.
Resolved physical values are normalised to 0 or 1.
Input accepts JSON booleans and relaxed numeric interpretation: zero is false
and non-zero is true.  Canonical document-form values, including baked
documents, are JSON booleans rather than numeric 0/1.  This normalisation is
schema-aware and does not change the general document model's numeric values.

`f16` uses the existing IEEE-754 binary16 transport type
[`fp16data_t`](../../core/types/fp16data_t.hpp), including in generated C++
declarations.  Conversions reuse its existing machinery rather than introduce
a schema-specific implementation.  Note that its default conversion can clamp
infinities to finite values.  This is existing transport-type behaviour to
document, not a requirement for schema-specific work to bypass or change it.

An enumeration is a logical type with one of the integer types as its declared
physical storage type.  Its symbolic values are part of the enum definition;
the underlying storage supplies size, alignment, range, and binary encoding.
Enum names must be unique within their declaration, but different names may
have the same numeric value.  Values not named in the enum definition are
illegal.  Output uses the first declared name for a value; any declared alias
matching that value is semantically sufficient and must not be rejected merely
because it is not the preferred output name.

## Structures and layout

A structure has one canonical declared sequence of members.  This declaration
order is its semantic positional order.  A structure also has resolved size and
alignment, and every member has a resolved physical offset.
Empty types are permitted in live and saved schemas. They have no resolved data
description and contribute zero-byte members. Generated C++ omits empty types
and their members; non-empty containing structures retain their physical layout.

The working declaration form is an array of named members, each expressed as
a single-member object whose property name declares the member and whose
value describes it:

```json
"members": [
  { "x": { "type": "f32" } },
  { "y": { "type": "f32" } },
  { "z": { "type": "f32" } },
  { "w": { "type": "f32" } }
]
```

Array order establishes member indices, while names support named access.
An explicit default and an optional explicit offset belong in the named member's
description object, alongside its type.
The replacement `schema-example.json` uses this ordered named-member text form.
Under the document model's singleton normalisation, these wrappers become
named children of the `members` array, retaining the descriptor payload.
Resolution reads that baked representation; it must not require a redundant
anonymous wrapper object to survive parsing. See the
[document interpretation rules](../data_model/document_text_format.md#collision-extension-and-singleton-objects).

Offsets do *not* determine positional construction order.  Natural layout may
leave padding; padding is not a member and never consumes a positional
initializer position. Non-monotonic explicit offsets do not change schema
declaration order.

Overlapping member storage ranges are rejected.  Bit-structure members may
share a storage word, but their actual bit ranges must not overlap.  An overlap
diagnostic should identify both conflicting members and their byte or bit
ranges. Union support is outside current scope; if introduced, it will be an explicit
schema construct with defined rules permitting overlap among its alternatives,
not an implicit interpretation of otherwise invalid overlapping members.

Natural layout is the default. Explicit offsets and increased alignment obey
the rules below. Packed layout will not
be used unless selected ingested structures demonstrate a need; the planned
review of candidate Vulkan structures may inform that decision. The original
sample's explicit layouts were superseded; the revised sample now illustrates
both natural layout and the subsequently agreed explicit-offset rules.

Natural layout calculates member offsets and array strides. Explicit layout
retains the same resolved observations; independent array-stride overrides remain
deferred. If any member supplies an explicit offset, every member of that
definition must supply one; explicit and inferred offsets are not mixed.

Natural alignment follows the current compilation target. Simple members
align to multiples of their size; compounds use their members' effective
alignment recursively. Without explicit increases, this is the size of the
largest atomic member. Thus a nested 12-byte structure containing three `f32`
members naturally aligns to 4 bytes, not 12. An explicit increase on a nested
type propagates to its containing layout. Natural-layout structure size is rounded
to the effective structure alignment; an explicit-layout size must already be a
valid multiple of that alignment. Each resolved definition must
provide fixed size, alignment, offsets, and where relevant
stride.  The member-list sequence is sufficient to establish ordinal meaning;
a separate ordinal property is not needed unless a later source format cannot
preserve declaration order.

The structure descriptor includes its total size, taking natural alignment
and tail padding into account.  This describes the structure as a whole; it
does not introduce an instance member or positional initializer slot.

### Explicit offsets and structure details

The explicit-layout form adds `offset` directly to each named member descriptor,
without a new nesting level or a separate layout-mode field. If any member has
an offset, every member of that definition must have one. Offsets are relative
to the start of that structure, regardless of where a containing structure
places it. The member's placement must respect its type's alignment, and
overlapping member extents remain invalid. Declaration order remains semantic
member order even when offset order differs.

The `detail` object beside `members` contains structure-level metadata,
replacing the earlier name `config`. It is optional for natural layout and
required to supply size for explicit layout. Authored
`alignment` may increase the structure's alignment requirement above
its natural requirement, not reduce it. A containing layout must respect the
effective alignment of a nested type, including explicit increases; the
natural largest-atomic-member rule alone is insufficient in that case.

If `alignment` is absent, including when a natural-layout definition omits
`detail`, use the agreed natural alignment rules. Omission does not select packed layout or byte
alignment. Explicit alignment requirements of nested member types still apply.

Generated schema documents include both `detail.alignment` and `detail.size`,
recording the resolved effective alignment and total extent. For natural layout,
both remain optional on input: size is calculated and a supplied size must match
that calculation. Source ingestion determines layout under its supported target
rules and emits these facts; normalisation reuses the resolved facts.

For explicit-offset structures, `detail.size` is required and authoritative.
Resolution validates the supplied extent rather than deriving the structure size
from its members. All member ranges must fit, offsets must respect member
alignment, and ranges must not overlap. The size must be a multiple of effective
alignment so consecutive array elements remain aligned. Extra trailing space
belongs to the structure and contributes to its gap flag. Omitted alignment still
uses the rules above. This replaces the earlier rule requiring an explicit-layout
size to equal the rounded last member end. Resolved runtime records themselves
remain non-serialisable.

Only positive-size member ranges participate in overlap checks. An empty member
retains its authored offset between zero and the structure size, inclusive, but
occupies no bytes and does not introduce a gap.

Accepting and validating generated `detail` metadata for natural layouts is
part of the base schema contract. Deferring the separate normalisation operation
does not defer that input form or computing its values. A supplied alignment can
equal the natural requirement or increase it, subject to the alignment rules.

Every successfully resolved type description contains its effective alignment
and size regardless of whether the input supplied them. Optional document
metadata for natural layouts does not mean optional runtime metadata. A normalised
document records validated structure details without modifying the input document.

For example, generated details for an aligned position would be:

```json
{
  "detail": { "alignment": 16, "size": 16 },
  "members": [
    { "x": { "type": "f32", "offset": 0 } },
    { "y": { "type": "f32", "offset": 4 } },
    { "z": { "type": "f32", "offset": 8 } }
  ]
}
```

For natural layouts the size calculation remains:

```text
size = round_up(max(member.offset + member.type.size), structure.alignment)
```

Member ends use each member type's full resolved size, including its own
padding; this applies recursively to nested structures and arrays. The explicit
example supplies size 16 for a member extent of 12 bytes. Bytes 12-15
are padding owned by the structure, including when it is embedded. Consecutive
elements in an ordinary array start 16 bytes apart, and a containing structure
cannot place another member inside that padding. The gap indication includes
this extra tail padding. A supplied size of 32 would also be valid for this
explicit layout: bytes 12-31 would belong to it and its array stride would be 32.

The resolved descriptor exposes `size` as a precomputed extent for operations
such as copying or clearing the whole structure. It does not impose automatic
zeroing during construction. Array element stride is derived from that size; `stride` is
not a separate structure property. An explicit-layout size includes all owned
spacing, rather than introducing a separate stride override. A supplied alignment
sets a layout requirement; a supplied explicit-layout size sets the total extent.
Explicit structure alignment is capped at 128 bytes. Accepted explicit layouts
must produce faithful C++ declarations under the selected compiler settings;
reject layouts that cannot meet that contract. Generated padding can account for
member offsets and total size, but it is not a logical schema member or an
initialisation policy. Compiler checks establish offsets, alignment and size.
The earlier approximate/review-only declaration path is removed. Generated C++
fields may be emitted in physical offset order when offsets are non-monotonic
in the schema. Schema queries, member ordinals and document positional values
retain schema declaration order. C++ aggregate initialiser order therefore need
not match document positional order. Reject layouts that cannot meet physical
fidelity instead of falling back to approximate output. Compiler evidence is
required for the accepted layout subset.

The internal marker is `detail.internal: true`; omission is equivalent to false.
It permits neither bypassing validation nor falling back to review-only output.
Natural-layout declaration generation remains
unchanged, and padding declarations do not add generated operational code.

This section records the full layout rules. The expanded sample includes both
natural and explicit layout examples. The initial-delivery assessment deferred
explicit offsets and increased alignment to integration stage 3.
Normalised-document creation remains a later operation.

### Gaps and storage initialisation

The resolved description records whether any gaps exist, without separate
categories for unused bitfield bits and alignment padding.  The indication
covers internal and tail padding and gaps contained in nested member types
or array elements, so an enclosing structure exposes whether any of its
storage contains gaps. The flag occupies bit zero of the private type record's
control byte.

Clearing unused storage is an optional, explicit operation named
`clear_unused_storage`; its typed-buffer, document-buffer and live-role entry
points are described in [runtime-api.md](runtime-api.md). It uses resolved
layouts to zero internal and tail padding recursively
through structures and arrays. Unused bitfield bits are preserved by default;
clearing them is a separately enabled option, which preserves every declared
field bit. At document-buffer scope, locator extents also identify gaps
between records/collections and other unreferenced bytes within the used payload
range. Unused allocation capacity is outside that range. The pass does not
compact storage, move records or change locators.

A standalone typed-buffer call requires its byte-view size to be an exact
multiple of the nonzero type stride. A remainder makes the entire view invalid;
reject it without processing any records or changing bytes. For a standalone
fixed-array type, the view must additionally contain complete declared array
values: divisibility by element stride alone is insufficient. Document-buffer
calls use locator extents to distinguish occupied storage from unreferenced bytes
within the supplied used range. Preflight, including any needed allocation,
completes before writes so failed clearing leaves the payload unchanged.

Every addressable field is preserved bit-for-bit, including fields not selected
by a particular remap. The operation neither applies defaults nor populates
uninitialised fields. It is idempotent and requires writable storage with valid
layout and range information. Like remapping, it can use a layout-driven traversal;
it does not need a second schema representation or source-value buffer.

This replaces mandatory pre-construction zeroing. Unpopulated bulk arrays have
no zero-fill guarantee. Raw copies and whole-aggregate remaps continue to copy
source padding; running the optional pass afterward removes byte padding and,
when explicitly enabled, unused bitfield bits.
For stable file output, run it after final packing/copying on the output buffer.
Non-zero gaps remain legal when the caller elects not to clear them. Logical
field initialisation remains separate from unused-storage clearing.

## Fixed arrays

An array is a normal type constructor, usable as a member type or nested inside
another array.  A fixed array declares an element type and count.  Its default
physical stride, size, and alignment derive from its element type.  The first
pass calculates stride; an explicit element stride may be considered later
when an API layout needs inter-element padding.

Arrays can therefore contain primitive values, structures, or further fixed
arrays.  A structure may contain fixed arrays in the same way as any other
member type.

The array descriptor is `{ "element": "f32", "count": 2 }`, removing the
previously illustrated `kind: "array"`. `count`
identifies the array and both properties are required; `element` accepts a
named type or another array descriptor. Count 1 remains an array. The old `kind`
property is not an alternative spelling; it is rejected as an unknown property.

Zero-length arrays are rejected. There is no current exception for a trailing
array. This restriction concerns the declared array count, not the length of
a short initializer completed with defaults. If source ingestion ever revisits
this rule, the contemplated case is
only a trailing zero-length array which is discarded from the extracted
schema, not retained as a schema array type.

### Named-component forms

A general array accepts positional values. An array of schema-defined
named components also supports overrides of selected components by name.
The schema establishes the component names and their
order; names are not inferred from arbitrary array contents.  A structure of
same-type, non-array base members can supply this form, as in a vector with
named `x`, `y`, and `z` components.  Recognising that form does not change its
declared physical layout.

For base construction, an omitted positional tail is completed from defaults.
For a specialisation, supplied positions select values in the independent
alternative and the omitted tail inherits from its base. A named declaration
can select only `y`, retaining the other inherited components. Neither form
modifies the base. This does not introduce placeholder values or a general
indexed-array patch syntax. The accepted sample recognises
the form from same-type, non-array primitive members without an extra marker,
and uses a named `declaration` object for selected-component overrides.

## JSON instance data

Nulls are not allowed in schema-defined values, including defaults and instance
input. An explicit `null` is an error, not an omitted value or an instruction to
use a default. This schema rule does not change the general document parser's
ability to represent nulls. Named omissions retain their established default
or inheritance semantics.

The type comes from the enclosing type group in `instances` or `data`, or from
an explicitly supplied resolved type description in a value-construction operation.
The reviewed named-instance form is:

```json
{
  "instances": {
    "Position": {
      "example": {
        "declaration": { "x": 0.0, "y": 0.2, "z": 0.7 }
      }
    }
  }
}
```

A base instance accepts either named-object or positional-array construction:

```json
{ "x": 0.0, "y": 0.2, "z": 0.7 }
```

```json
[0.0, 0.2, 0.7]
```

The array maps index zero onward to the structure's declared member sequence,
not its offset order. A shorter positional input is valid: supplied values
fill from the first member and remaining members use their defaults. The same
rule applies to fixed arrays, whose remaining elements use their applicable
defaults. Excess values are an error; a shorter initializer does not change
the declared member count, array extent, or physical size.

For a four-component vector, `[2.0, 1.0, 4.0]` supplies `x`, `y`, and `z`;
`w` uses its declared default or implicit zero. This supports a fourth component
used for padding without requiring an author to spell it out. It remains a
logical member with a default, distinct from unnamed alignment or bitfield gaps
which the optional unused-storage pass can clear.

An array is therefore a compact construction form, including default completion
for a base instance. In a specialisation, its supplied prefix instead selects
alternative values and omitted positions inherit. An object identifies selected
values by member name. The specialisation rules below apply recursively.

Small sequences of complete instances may be represented as a JSON array, but
large collections are normally expected to use a binary payload with a
separate schema-aware descriptor.

## Floating-point special spellings

The document model does not acquire native numeric NaN or infinity values.
Instead, schema conversion recognises predefined strings when the expected
destination type is floating point.  Relaxed document parsing may accept such
spellings unquoted, but they still reach schema processing as document strings;
quoted and unquoted source spellings are therefore equivalent at that boundary.
Schema conversion produces actual non-finite values in the resolved floating
point storage, subject to the existing `fp16data_t` conversion behaviour noted
above for `f16`.  This does not add native non-finite numbers to live or baked
documents.

Recognition is ASCII case-insensitive and otherwise exact. Accept `nan`, `inf`,
`+inf`, `-inf`, `infinity`, `+infinity`, and `-infinity`. Canonical output uses
`nan`, `inf`, and `-inf`. No signed or payload-bearing NaN syntax is defined.
Matching does not trim whitespace or use locale-sensitive case conversion.

Strict JSON output writes these values as strings.  Relaxed diagnostic or
authoring output may write the selected spellings unquoted.  Textual `NaN`
constructs a canonical destination NaN; exact NaN payload preservation belongs
to raw binary handling or an explicit future raw-bit form.

## Layering and partial overrides

"Specialisation" and "override" denote the same concept throughout this
design; they are not separate operations or kinds of declaration.

Structure definitions can provide explicit default member values.  A member
without an explicit default uses zero, or the first declared enum value when
its type is an enumeration.  "First" means declaration order, not the smallest
numeric value; an enum's default therefore need not be numerically zero.
Boolean zero corresponds to false.  Explicit defaults must be valid values
of the member's declared type.

Schema resolution validates default values before publishing a usable schema.
An explicit default for an enum member must be a label declared in that member's
matching enum definition; numeric values are not accepted as enum defaults,
even when a label has that numeric value. Any declared alias in that enum is a
valid label. An invalid default is a schema-resolution error and leaves the
resolved schema empty. The implicit first-declared-enum default is unchanged.
Use labels from the matching enum for ordinary document-form instance values too.
Numeric literals do not implicitly acquire enum identity. Typed enum values may
supply that same enum or their matching raw underlying integer type; distinct
enum types are not interchangeable merely because their storage or values agree.

Defaults apply only within the specific structure definition that declares
them.  They do not propagate from an enclosing structure into nested
structures.  Each nested structure uses its own declared defaults, or the
implicit zero/first-declared-enum defaults for members without one.  This is
separate from an instance or override explicitly supplying nested member values.
An enclosing member whose type is a structure cannot supply an aggregate
`default`; reject that property rather than treating it as a local override of
the nested type. Arrays of structures likewise use the element structure's own
defaults. This applies to nested bit structures as well: field defaults belong
to their own field declarations, not an enclosing aggregate default.
Primitive/enum members and arrays of those types may supply explicit defaults;
short array defaults complete the tail with element defaults. A bit field may
have an explicit scalar default in its named field descriptor, validated against
its logical type, mask width, and interpretation.

Base construction starts from these schema defaults and applies the values
supplied by the instance. An omitted member uses its default. A specialisation
instead starts from an independent copy of the values established by its
enclosing base: omitted members or positional elements inherit, and defaults
are not reapplied at each step. This applies recursively to nested structures
and fixed arrays. The user confirmed this rule on 30 September, superseding
the earlier positional-replacement rule for specialisations. The effective
values follow the chain from the structure definition through the base
instance and each subsequent specialisation; creating an alternative does
not modify any earlier instance in that chain.

Confirmed on 1 October: an omitted `declaration` means no selected values. A
base instance therefore uses its schema defaults; a specialisation inherits its
complete parent unchanged. Optional embedded/binary comparison uses the same
meaning. Omission never denotes unknown or discarded declaration intent.

Default values apply to logical members; clearing gaps is a separate optional
operation. Defaults cannot generally be implemented merely by
zeroing storage because enum or explicit defaults may be non-zero. Explicit
defaults use `default` in the named member descriptor, as in the accepted sample.

Containment selects the base instance for specialisation. Each
specialisation has exactly one base, which may itself be a specialisation,
forming a chain. The base's declared type determines the permitted members;
this does not require an override to redeclare that type. Earlier
specialisations do not restrict which of those members later steps may change.
Missing bases, invalid members, and circular specialisation are hard errors.

Instance/bulk load errors invalidate that logical document and leave its schema
valid, regardless of whether the views share a physical baked block.

Specialisation and overrides change values only.  They never change the
target's underlying types or modify structure definitions.  Compatibility is
directional from the supplied value's type to the target member's declared
type and requires effectively the same underlying type; equal byte size alone
does not establish compatibility.

An enum is a stronger type than its underlying integer type.  A stronger
typed value can supply a weaker destination with that same underlying type,
but a weaker typed value cannot supply the stronger destination.  This
relationship governs acceptance of the supplied value, not type promotion:
the destination retains its weaker declared type after the override.  A later
override is still checked against that original destination type, not the type
of the value supplied by an earlier override.

For example:

- An enum with underlying type `u8` may supply an override for a `u8` member.
  The target remains a `u8` member.
- A raw `u8` may not override a member declared as an enum backed by `u8`.
  Matching storage does not remove the target enum's type restrictions.

The same principle applies along the entire specialisation chain.  These
rules concern typed override compatibility; they do not by themselves specify
document-literal conversion rules or the compatibility predicate for bulk
remapping.

An override lives in its base instance's `specialisation` container, with
changed member values in its own `declaration` container. The enclosing base
is evaluated first. A shared name in another subtree neither selects a base
nor creates an override relationship. Parent-based nesting cannot form an
inheritance cycle in a document tree.

Every named specialisation is independently selectable as an instance,
including intermediate steps in a chain.  Selection uses its inherited values
and its own overrides, while the referenced type determines valid members.
Each specialisation copies its base's crystallised (fully materialised) binary
instance and replaces the parts it specialises. It occupies its own instance
extent and is independently selectable. Editing a base updates all descendant
specialisations recursively, parent before child, as part of the user-facing edit
operation. This replaces the separate caller-invoked dependent-update step.
Runtime values remain independent snapshots, not delta chains traversed during
access. A specialisation's immediate parent supplies its fully realised value,
including every specialisation earlier in the chain. Inheritance never skips
back to the original instance or schema defaults when that parent is itself
specialised. The retained document tree supplies base relationships and a recursive
walk through lower specialisation branches discovers impacted instances.
Authored declarations preserve override intent during construction and editing,
including explicitly supplied values equal to the base. Reconciliation and
conversion subsequently derive selections from snapshot differences, removing
equal overrides. Capturing a complete binary value for an
existing specialisation takes only the parts selected by its retained declaration,
updates those parts in its complete image and recursively updates descendants.
Confirmed on 1 October: descendant updates preserve each descendant's selected
binary values and synchronise its retained declaration to those values. This
also applies when a saved snapshot disagrees with the authored declaration:
an explicit selection declaring `x = 3` but holding authoritative binary `x = 9`
retains `9` after an ancestor edit. Unselected values inherit from the updated
immediate parent. These edit operations honour current selections; explicit
reconciliation or conversion replaces them with snapshot-derived selections.
Removing a selected member rebuilds from the base; adding one supplies a replacement
value. Both operations update descendants. Live document access is mediated by
the wrappers. Failure partway through editing/updating still needs a validity
contract. Resolved schema metadata does not become mutable instance provenance.

Base construction and specialisation are separate operations.

- Complete construction establishes every member from supplied values or the
  explicit/implicit schema defaults.  Omission alone does not make an instance
  incomplete.
- A specialisation creates an independent alternative from an already complete,
  validated base instance. Its declarations never modify that base.
- Named objects select members; positional arrays select a prefix in semantic
  declaration order. Omitted members/elements retain their inherited values.
  The same rules apply recursively within nested structures and fixed arrays.
- Schema-defined named components may be selected by name. Named bitfields
  support selected-field overrides through explicit masks. Writing a selected
  field in the alternative preserves every bit outside its mask, including
  unused bits.

For example, an alternative differing in only one coordinate declares:

```json
{ "position": { "y": 0.2 } }
```

This declaration selects only the alternative's `y` member. A positional
specialisation `[2.0, 1.0, 4.0]` of base `[2.0, 1.0, 4.0, 7.0]` retains
the inherited fourth value, producing `[2.0, 1.0, 4.0, 7.0]`. The supplied
positions remain explicit selections even when equal to their base values.
Selected component overrides use the names supplied by the schema. Null
placeholders remain invalid.

Semantic coordinate names such as `x`, `y`, and `z` should be represented by a
named `Position` structure rather than only as `array<f32, 3>`.  That structure
may still be constructed positionally, while retaining named specialisation paths.

## Bit ranges and packed formats

The schema describes deterministic bit selection independently of C++ bitfield
layout.  A bit structure has a fixed storage unit and named members described
by hexadecimal masks and an interpretation.  Each mask selects one non-empty
contiguous bit range within the storage unit.  Masks must not overlap, but
gaps between fields and unused bits elsewhere in the storage unit are allowed.
Offsets and widths can be derived from masks during resolution; they are not
the authored selection form.

The declared base type determines signedness.  Ingested C/C++ bitfields are
converted into this mask-based description with their base-type signedness
retained; interpreting their source allocation still requires known ABI rules.
Interpretations may include signed or unsigned integers, enums, normalised
values, and raw bits.  Unused bits contribute to the common gap indication;
the optional unused-storage pass preserves them by default and clears them only
when its unused-bit option is enabled. Byte padding is cleared in either mode.

Packed texel formats should initially be constructed from this general
bit-range machinery rather than introduced as primitive schema types.  Thus an
RGB10A2 format is a named packed `u32` representation with four bit-range
members, while RGBA8 can be a structure of four `u8` members.  A later
convenience catalogue may name standard API formats without making them
fundamental types.

The reviewed sample uses `mask` and `type` on each field, with a separate
containing-word `storage` and optional `interpretation`. Its companion notes
explain that separation. The original `bit_offset`, `bit_width`, and
`unused_bits` fields are removed.

## Encodings and remapping

### Normalised field codecs

The 30 September decision replaces `floor(f * 2^n)` with GPU-style normalised
scaling and nearest rounding, and adds signed normalisation. Rounding is
deterministic: nearest integer, with halfway cases rounded away from zero.

For an `n`-bit `unorm` field, integer input/defaults are raw codes in
`[0, 2^n - 1]`. Floating input/defaults encode as
`round(clamp(f, 0, 1) * (2^n - 1))`. Decoding is `code / (2^n - 1)`.
UNORM supports mask widths from one through 64 bits.

For an `n`-bit `snorm` field, integer input/defaults are raw signed codes in
`[-2^(n-1), 2^(n-1) - 1]`. Floating input/defaults encode as
`round(clamp(f, -1, 1) * (2^(n-1) - 1))`. Decoding is
`max(code / (2^(n-1) - 1), -1)`. SNORM supports mask widths from two through 64
bits. Preserve the most-negative raw code: for eight bits, both -128 and -127
decode to -1.0, while floating -1.0 encodes to -127.

NaN becomes zero before clamping; infinities clamp to the corresponding endpoint.
Both raw and encoded codes must fit the logical primitive as well as the mask
width; reject out-of-range codes. Endpoint and rounding arithmetic must avoid
out-of-range casts and signed overflow, including at 64 bits where the positive
integer maximum is not exactly representable in binary64.

Resolved defaults and document output use unshifted integer codes, unsigned for
UNORM and signed for SNORM. Thus integer `1` means raw code one, while `1.0`
means normalised maximum; integer `-1` is a raw SNORM code, while `-1.0` means
the negative normalised endpoint. Implicit zero retains the logical signedness.
Raw-code and normalised-float setters are separate public operations. Existing
floating special-string handling supplies non-finite document input; the data
model itself retains finite numeric payloads.

The scaling follows the GPU normalised representations described by
[Vulkan fixed-point conversions](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fundamentals-fixedfpconv).
Our explicit tie rule follows
[Direct3D normalised conversions](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm);
it defines this encoder's deterministic result without promising bit-identical
results from every hardware conversion permitted by those APIs.

### Instance and bulk operation boundaries

The instance and bulk interfaces each manage a document association and a separate
binary byte buffer. Embedded values are converted through the resolved schema on
load, applying instance defaults/specialisation or complete-bulk-record rules as
appropriate. A specialisation copies a completed base and applies its changes
into its own extent. Loading reference-form input uses the associated binary
block with matching physical representation. Repeated access need not redo
document conversion or layout calculation.

Capture of an individual named instance updates an existing extent or appends an
extent and updates/adds its live document entry. Bulk capture follows the same
model for a named record array. Instance documents retain declarations as well
as binary offsets; bulk documents need not retain embedded records. Specialisation
capture uses the retained selection and updates descendants. Role handles, borrowed
view lifetimes and buffer placement follow the rules above; implemented signatures
are documented in the runtime API guide. Replacing a bulk entry through raw
capture removes any existing embedded record array, leaving a reference to the
new bytes. Reconciliation or embedded output can regenerate complete records.

`CLiveBulkData` supports creating a named, unpopulated array by resolved type and
record count. It uses the same aligned allocation, bounds checks and failed-append
publication rules as other bulk creation. The resulting handle supplies a bounded
destination view to the existing remapping executor. One or more mappings may
populate the array; allocation does not promise zero-filled backing.
The caller must populate required record fields before reading or exporting them.
This construction operation does not apply schema defaults. Clearing unused
storage does not populate logical fields left unwritten by the caller.

Validate schema/type, bounds, counts and required alignment before access or
transfer. Compatible binary instances can be copied contiguously into adequate
application storage. There is no application-wide population wrapper. Generic
typed-description plus pointer operations remain appropriate underneath these
interfaces, without generated per-type serialisers.

The established low-level failure rule allows partial destination output which
the caller must discard; it does not promise rollback. Load failure affects only
its logical data document; append failure preserves existing entries and publishes
no failed locator. Ordering validation and preparation around in-place edits and
recursive descendant updates is implementation work. A memory allocation failure
during such an update is application-critical; the API need not add transactional
rollback or a recoverable partial-edit model for that situation. Implementation
must use the application's critical-failure handling rather than allow continued
use of partially updated data. Critical severity does not by itself require a
no-return panic when a safe no-further-processing state can be established.

Named-bitfield updates preserve bits outside their selected masks. The optional
unused-storage pass preserves those bits by default; its unused-bit option
clears only bits not addressed by declared fields.
Schema access requires its backing document to remain alive
and its resolution to correspond to the definitions. Promotion can recreate the
resolution against the live document; the result must no longer borrow the old
baked document before its owner disposes of the backing block.
Binary views across mutation/growth are caller-managed; no held-view tracking is added.

### Representations and bulk copies

Document declarations and completed binary values serve coordinated roles.
Output builds a new live document as described under promotion, demotion and
output. Full output obtains completed values from binary; bulk references expand
into record arrays. Reference output retains offsets and writes each associated
block. Text versus baked output is independent of embedded versus external data.
For baked instance/bulk output, the payload owner remains external to the baked
wrapper, just like the document-block owner. The caller can save newly generated
payloads or transfer them to the host without extracting storage from a wrapper.
Export does not destructively strip the working instance declarations or require
bulk records to remain duplicated in the working document.

Full output for a specialisation reads its completed values from its own binary
extent. All instance output retains instance names and the inheritance hierarchy,
and derives declarations from snapshot differences. An explicit override equal
to its parent is removed, so it follows subsequent parent edits. Loading without
supplied binary reconstructs values from that hierarchy. No separate flattened
value field is required. Binary output retains complete independent snapshots.
Both output forms remain editable after promotion.
Embedded/binary comparison on load is optional; when disabled, binary snapshots
remain authoritative until an edit. Loading supplied binary does not reconstruct
it from declarations. Exact source text and alias spellings need not survive.

Both instance output forms reconcile declarations without modifying the source
document or any snapshot. Each base is compared with destination defaults, then
each specialisation with its immediate parent's snapshot. This treats schema
default changes and raw-write discrepancies consistently. A child whose parent
changed but whose own bytes did not acquires explicit differences that preserve
its current snapshot. Equal named values are removed, including previously
explicit overrides and empty selections. Array declarations retain the shortest
prefix reaching the final differing element; equal scalar values inside that
prefix remain explicit. Nested compounds follow the same comparison recursively.

The generated document must reproduce meaningful encoded values. Structural
safety, schema interpretation and representability remain validated, but stale
declaration values are not decoded and disagreement with them is not a failure.
Optional binary-versus-document comparison on loading remains available as an
integrity check, independently of reconciliation.

Schema interpretation recognises singleton compound selections unwrapped by the
text parser inside arrays. For an array element whose structure has members
`a` and `b`, `[{b:9}]` selects only `b` even when its document form is a named
`b` value directly in the array. The same interpretation applies to bit-structure
selections and compound values in positional declarations. Loading and authored
edits interpret those selections; reconciliation and output derive new selections
from binary. Recognition is contextual to compound values; named scalar array
elements remain invalid. Both live and baked roles use this interpretation
without changing the parser.

The same contextual interpretation applies to the outer array of bulk records.
A named singleton record must still supply every member of its declared type;
the bulk completeness rule does not acquire instance defaults or inheritance.

Confirmed on 3 October: a structure with exactly one primitive or enum member,
or a bit structure with one scalar field, accepts that scalar directly as a
shorthand value. This rule applies wherever such a value is expected: an
instance declaration, a specialisation selection, a nested member, an array
element or a bulk record. For `Value { a: u8 }`, `1`, `[1]` and `{"a":1}` select
the same member and encode the same value. A bulk collection of two `Value`
records accepts `[1,2]`, `[[1],[2]]` or `[{"a":1},{"a":2}]`; its outer array
still supplies two records rather than assigning one record twice.

Shorthand never recursively unwraps arrays or compound members, and does not
apply to empty or multi-member compounds. All scalar conversion, range,
enum-label and bitfield interpretation rules continue to apply. Schema-default
restrictions are unchanged. Capture and edits may emit the explicit aggregate
form rather than retain the exact shorthand spelling. Reconciliation and output
derive selections from binary regardless of the original input form.

For both roles, embedded output must round-trip under the existing encoded-field
comparison rule: NaNs compare equal, while padding and unused bits are ignored.
Noncanonical Boolean codes and enum codes without labels cannot be embedded
faithfully under that rule and are rejected with a diagnostic. Instance conversion
reconciles declarations in both forms and therefore rejects such values in either
form. Reference-only bulk output can preserve their exact binary encodings
without generating document literals.

Binary loading/copying assumes matching offsets, stride, byte order and primitive
representation. Packaging must establish that association and check buffer bounds;
stored payloads are little-endian. File association is supplied manually by the
caller, without on-file schema/layout identity in this work. Raw copying is not
a cross-layout or cross-endian conversion. A future review CSV
export may be considered separately; it is not a core encoding or planned input
path. Locators and buffer rules are specified above.

### Automatic direct-member remapping

The initial runtime remapper covers fat to
thin and thin to fat vertex or structured records only where matched members
have identical physical representation.  A validated runtime plan resolves to
a small sequence of strided copies.  Each copy identifies source and
destination byte offsets, a fixed contiguous compatible byte-range size,
source and destination strides; execution derives the element count from its
bounded views. A range may cover one
member or several consecutive physically compatible members in either record;
remap setup coalesces adjacent copies where doing so is safe.  It performs no
numeric conversion, enum translation, packing, unpacking, or general
per-record interpretation.

Each bulk mapping is based on two resolved structure types describing source
and destination array records. Setup automatically matches direct members by
name and type; there is no authored member-pair list or recursive member search.
It need not use every source member or fill every destination member. Members
without a matching name or exact type remain unmapped. This is expected, not a
semantic failure: partial plans and plans with no matches are valid. The same
rule applies to every type category; enums need no separate mismatch policy.
Matching enums can be copied unchanged; no enum translation is performed.
Primitives must have the same primitive type and size. Matched named structures,
enums, and bit structures require the same
type name and matching resolved definition; representation equality alone is
insufficient. Compare names by text across documents, not document-local IDs.
Array matches require matching counts, strides, and compatible element types.
A compound already excluded by a difference in type category, name, size or
layout is skipped whole, without examining its contents to classify the mismatch
or find partial transfers. A compatible compound direct member is copied whole.
When equality of otherwise possible matches requires comparing definitions,
stop at the first difference; this is an equality check, not nested selection.
Definition matching includes member/label order, names, referenced types,
layout, defaults, masks, and interpretations as applicable; numeric formatting
metadata does not change type identity. Enum aliases must agree in declaration
order. Different schemas may participate if this comparison succeeds.

The enclosing source and destination record types need not have the same name:
compatibility applies to the automatically matched members. Thus different vertex record
types can transfer matching primitive or named-member types. Enum-to-raw-integer
override compatibility does not grant a bulk-copy conversion.

Coalescing requires contiguous corresponding source and destination ranges and
must not overwrite unselected destination members. Multiple sources contribute
through multiple mappings, but a combined plan rejects overlapping destination
writes during setup, even when they would write identical bytes. Separate calls
are independently validated operations, not an implicitly ordered combined plan.

At setup, the remap constructs an execution plan that selects pre-written fast kernels
from this mechanical shape and stores their offsets, range sizes and strides.
Execution supplies the derived count. It does not generate code for an individual
schema. The important
kernel cases are a source stride equal to the
range size (sequential source loads and strided destination writes), or a
destination stride equal to the range size (strided source reads and
sequential destination stores).  For example, a 16-byte `vec4` source with a
16-byte stride can be copied into 16-byte destination fields in records whose
stride is a multiple of 16.  The contiguous side can use bulk/vector-register
loads or stores where alignment and platform support allow; the other side may
still require individual transfers.  If neither side is contiguous for the
element, the operation falls back to ordinary individual strided copies.

A mapping leaves unselected destination members untouched.  Destination
initialisation is separate from the partial transfer; a caller may initialise
storage, apply defaults, or populate it through other mappings as appropriate.
An unpopulated array created by `CLiveBulkData` is one such destination; remapping
only a subset of its fields or records leaves the rest untouched for the caller
to populate as required. The unused-storage pass does not clear unselected fields.
A whole selected aggregate copies its full extent, including owned padding.
Selecting individual members grants no permission to bridge intervening padding
or unselected fields when coalescing. Destination initialisation remains separate;
unused storage may be cleared explicitly after the transfer.
Individual packed fields require bit operations and are excluded from
this byte-copy remapper; a compatible complete bit-storage value can be copied.
Source/destination memory overlap is unsupported by the first executor and must
be rejected before writes; it must not accidentally acquire `memmove` semantics.

Execution receives bounded current source/destination views and derives their
complete record capacities from the stored sizes and strides. A single `execute`
operation processes the minimum capacity; callers narrow views to restrict the
transfer. Every source and destination view for a nonzero-sized type must have
a byte size exactly divisible by its stored type stride. A remainder rejects
the entire operation before any writes, including when another view is empty
or the plan has no matching members. Remap roots are structures; fixed arrays
are matched members whose complete extents are already part of their type.
There are no supplied record counts, copied-count output or execution
diagnostic: execution returns success/failure after checking memory validity
before any writes. Zero-byte types impose no storage limit, and an entirely
zero-byte transfer is a successful no-op without a logical count. Setup retains
diagnostics for actionable construction failures. Instance/bulk callers extract
entry views and use the same executor without separate handle-based overloads.

Remapping into a live instance, whether a base or specialisation, changes only
that instance's binary snapshot. It does not change declarations or selection
intent, restrict writes to selected fields, or update descendant snapshots.
Handle adapters therefore perform the same binary transfer as bounded-view
execution, without invoking capture, coordinated editing or reconciliation.
Subsequent conversion reconciles declarations from the resulting snapshots.

The primary destination is an application-owned data buffer outside the schema
system, with its representation described by the destination resolved layout;
it requires no instance or bulk document. Sources may be baked or live instances
or bulk data. Writing into live instance or bulk backing within the schema system
is an additional destination path through the same bounded binary executor.

Fast runtime binary updates are the primary remapping use case. Creating new
document forms from remapped results is a separate workflow. The live instance
and bulk roles expose `reconcile(diagnostic)` to rebuild their whole document
from current binary values. It never modifies payload bytes or propagates parent
values into descendants. Bulk entries receive complete records; instance bases
and specialisations follow the defaults/parent comparison rule above. Publication
is atomic: failure retains the old document and handles. Success replaces the
document, invalidating its handles, while payload allocation, offsets and views
remain stable. Reconciliation is not part of the binary executor.

Authored renaming, nested selection, numeric conversion, bitfield transforms and
enum translation are outside this delivery, not implicit requirements for a
general remapping engine.

Logical JSON is not necessarily a lossless representation of every physical
bit pattern.  Raw binary remains the lossless representation where reserved
bits, padding, or floating-point payloads matter.

## Delivery and deferred scope

The [implementation contract](implementation-contract.md) owns the staged plan,
code reuse evidence, validation and current progress. The [runtime API](runtime-api.md)
owns implemented observations and measured layouts. Stage 2 uses the agreed
intrusive client-binding list for safe invalidation on schema destruction.
Source ingestion remains a later design stage.

No wire version, hash scheme or editor dependency system is implied. Instance
and bulk buffers are associated with their separate interfaces, not the resolved
schema owner; live wrappers own them and baked wrappers borrow external storage.

Unions, pointers, variable-sized fields, opaque handles/blobs, packed layout,
independent array strides, and generated C output are unsupported in the current
scope. Reconsider one when a concrete use case requires it; it is not a pending
question that an initial implementation must answer.
