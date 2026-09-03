Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   revised_data_model.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   1 Sep 2026

# Revised data model

## Status, authority and scope

This document is the source of truth for the intentionally incompatible
replacement of TheMorphicEngine's first live and baked document model. It
defines the abstract data model, the integrity invariants at its boundaries and
the ownership rules which later implementations must preserve.

Where this document conflicts with the archived v1 design at
`graveyard/data_model_v1_2026-09-01/docs/backlog/live_dom_design.md`, this
document takes precedence. In particular, the replacement does not retain the
old model's
publicly creatable recovered value kind, replaceable root, absent-name sentinel,
live acceptance of non-finite floating-point values, separate public baked
builder, or settled version-3 byte layout. Useful decisions from that work are
restated here rather than inherited implicitly.

This is a semantic specification, not a frozen C++ interface or binary-layout
specification. Requirements expressed with **must**, **must not** and **only**
are normative. Sections labelled as implementation direction describe a
plausible route without requiring that algorithm. Concrete APIs, flag packing,
record sizes, offsets and format version remain deferred unless this document
explicitly says otherwise.

The immediate scope comprises:

- the common abstract node model;
- mutable live-document construction and editing;
- duplicate-member recovery during attachment;
- live string and numeric admission;
- construction, validation and promotion of an immutable baked document; and
- the document ownership and threading boundaries needed by later stages.

The JSON parser, direct JSON and binary file I/O, asynchronous Host/Executive
round trips and JSON-writer refactor are later Part 1 work. Schema and its
domain-specific interpretations remain Part 2. This document records the
contracts those later layers must consume, but does not design those layers.

## Terminology

A **value node** represents one JSON-shaped value. Its base payload kind is
null, Boolean, integer, floating point, string, array or object.

An **aggregate node** is the composition-owned payload of an array-valued or
object-valued node. It provides ordered child storage and explicitly identifies
its aggregation kind as object, array or recovered array. It is not an
independent JSON value.

An **owner** is the value node whose payload refers to an aggregate. An
aggregate has exactly one owner.

An **object-entry value** is a value whose orthogonal object-entry flag is set.
It has a non-empty name and can be attached directly to an object aggregate.
The flag does not replace or imply a base payload kind. For example, a named
integer is integer plus object-entry, while a named object is object plus
object-entry and can itself own a multi-entry object aggregate.

A **live key** or **live string ID** is meaningful only within one live-document
lifecycle. A **baked index** is meaningful only within one baked artifact.
Neither is durable identity and neither is serialized as an application-level
reference.

## Abstract node model

The live representation uses the same physical node structure and key domain
for value and aggregate nodes. Role and type validation determine which fields
are meaningful.

Aggregate nodes remain internal implementation payloads despite sharing that
key domain. Public document operations expose and accept keys for value nodes
only. A key lookup or relationship query which fails, is inapplicable to the
node's role or would otherwise expose an aggregate as a value returns an invalid
key or the corresponding invalid result.

The baked representation may retain explicit aggregate records in its node
index domain or fold an aggregate's immutable metadata into its owning value
record. That physical choice is deferred. Either encoding must preserve the
same logical value/aggregate distinction and strict alternation for validation,
promotion and traversal.

The hierarchy strictly alternates:

```text
value
  scalar payload                         (end)

value: array or object
  |
  +-- owns exactly one aggregate payload
        |
        +-- owns zero or more value children in order
              |
              +-- scalar payload         (end)
              `-- or one owned aggregate (continue)
```

The following invariants are normative:

- Every value has a payload.
- A scalar value terminates its branch and owns no aggregate.
- Every array or object value owns exactly one aggregate created atomically
  with it.
- Every aggregate has exactly one value owner at a time and is never shared or
  independently user-addressed. Its complete payload may be reattributed
  atomically between compatible value owners when a structural operation
  requires it; the owner relationship, duplicated name and accounting must all
  move consistently.
- Every non-root value has either exactly one parent aggregate or is detached.
- Value siblings share the same parent aggregate.
- An aggregate is never an array element, object member, root value or
  independently detachable subtree.
- Child order is semantic and must be preserved by live mutation, baking,
  promotion and writing.

An aggregate stores its child count and references to its first and last value
children. Its aggregation kind and duplicated owner name must agree with the
owning value. Integrity validation must reject disagreement. The owning value
also makes the semantic array/object type available; redundant aggregate
fields and canonical values for fields unused by a role are layout decisions,
not additional semantics.

The structure is a tree rather than a directed acyclic graph. Aggregate
payloads are composition-owned, and attaching a value cannot introduce shared
ownership or a cycle.

Live node keys are allocated monotonically and are never reused during a
document lifecycle. The physical slots which hold nodes may be reused after
erasure, so key order, slot order and slot adjacency have no structural
meaning. Live navigation uses the stored parent, previous-sibling,
next-sibling, owner, first-child and last-child relationships. Gaps caused by
creation and erasure are ordinary and must not affect traversal.

The initial live implementation keeps the shared role-dependent relationships
in one 64-byte node record. Role, value type, aggregate kind and object-entry
state form one common usage substructure. Role-specific accessors decode the
shared relationship fields and return an invalid key when an accessor is
inapplicable, making their contextual meaning explicit without expanding the
record beyond that boundary.

## Root contract

`CLiveDocument` initialization creates an implicit anonymous object value as
the root. The root owns an object aggregate. It is the first node in the
logical tree and in the baked node table.

The root is not explicitly created, selected, replaced, detached or erased by
ordinary callers. Reset and deallocation own its lifecycle. Initialization
must either establish the complete valid root value and aggregate or leave the
document uninitialized; it must not publish a half-created root.

The root name is the canonical empty name. Its object-entry flag is clear.

## Names and object-entry values

Every value and aggregate node has a name reference. There is no absent-name
state. Index zero in a baked name table is the canonical empty string: a
zero-length, physically NUL-terminated entry. Live property-name and
string-value ID zero is likewise the valid canonical empty entry. Invalid IDs
remain distinct from empty and represent failed or inapplicable queries, not a
string-table entry.

Creation accepts an optional name:

- An empty name selects the canonical empty entry and leaves the object-entry
  flag clear. The result is an anonymous ordinary value.
- A non-empty name selects a nonzero name entry and sets the object-entry flag.
  The result is a named value representing one object entry.

Empty JSON property names are deliberately unsupported. An object entry must
have a non-empty name, so an empty name always means anonymous rather than an
empty-named member.

An aggregate duplicates its owner's name, including the canonical empty name.
Owner and aggregate name agreement is an integrity invariant. Any explicit
operation which changes an owner's name or object-entry state must update the
owned aggregate consistently and update all affected reference counts.

### Attachment by aggregate kind

An object aggregate accepts only named object-entry values. Immediate child
names must be unique by exact normalized byte sequence.

An ordinary array aggregate accepts named or anonymous values of every base
payload kind. This includes named scalar entries, named single-entry arrays or
objects, and anonymous multi-entry arrays or objects. Attaching a named value
to an array does not remove its name or clear its object-entry flag.

A recovered-array aggregate accepts the anonymous competing values created by
duplicate recovery. It is not a general-purpose array container.

These rules intentionally collapse a single named object entry when it appears
inside an array:

```text
JSON:  [{"a": 1}]

array value
  array aggregate
    integer value: name "a", object-entry set, payload 1
```

A multi-entry object still requires an anonymous object value and its secondary
aggregate:

```text
JSON:  [{"a": 1, "b": 2}]

array value
  array aggregate
    anonymous object value
      object aggregate
        integer value: name "a", object-entry set, payload 1
        integer value: name "b", object-entry set, payload 2
```

An anonymous object value cannot attach directly to an object aggregate. A
future explicit conversion or naming operation may make it a named
object-entry value, but attachment must not perform that conversion implicitly.

## Live string admission and accounting

Live property names and string values occupy separate stable-string-style
containers. Equal bytes in the two roles do not merge their identity or
accounting. Their IDs remain distinct strong types even where the live
implementation shares their compact representation and common behaviour.

Admission is length-aware. Before interning or comparison, it must:

1. replace each literal U+0000 byte with the exact modified-UTF-8 byte sequence
   `C0 80`;
2. preserve an existing exact `C0 80` sequence; and
3. otherwise require strict UTF-8.

All other overlong forms, malformed or truncated sequences, isolated
surrogates and scalars beyond the Unicode range must be rejected. Admission
first scans and validates the supplied bytes. Canonical input is interned
directly; temporary normalized storage is allocated only when literal zeroes
actually require promotion to `C0 80`.

Validation failure adds no entry. A later creation or allocation failure may
leave a completely formed zero-reference intern entry, because live IDs are
stable and such entries are already valid inventory. It must not leave a
partial string entry, a partial owner/aggregate pair or an incorrect reachable
reference count.

Normalization precedes equality and uniqueness checks. A literal zero and an
already modified `C0 80` therefore denote the same interned content and the
same object name.

Every live string-table entry has a reference count covering references from
nodes currently reachable from the root. The policy applies consistently to
value names, aggregate duplicated names, canonical-empty references and
string-value payloads. Detached nodes retain their interned IDs and content but
do not contribute to these counts.

An entry whose count reaches zero remains in the live table. Reference counts
are accounting information, not permission to remove or renumber live entries.
The live document also maintains the number of distinct non-empty property-name
and string-value entries whose reference count is nonzero. The canonical empty
entry is excluded from both these totals.

Attachment increments references throughout the attached subtree. Detachment
decrements them throughout the detached subtree. Detached creation contributes
no references, and destruction of an already detached subtree does not
decrement them again. Ordinary mutation assumes the document's established
invariants and updates these counts directly. An explicit integrity audit
independently traverses from the root and verifies the recorded counts.

This makes attachment and detachment proportional to subtree size unless a
future storage design provides suitable summaries. That cost is accepted in
exchange for making the live string tables an authoritative inventory of the
names and values needed by baking. Baking can select every entry with a
nonzero count, create the sorted baked string tables and construct the
live-to-baked ID maps before walking the node tree.

Live compaction is deliberately bake followed by promotion. It is not in-place
removal or renumbering of zero-reference entries.

Live property-name and string-value IDs are stable and are never reused. The
parallel reference-count vectors define the allocated ID extent; the live
document does not maintain or expose redundant totals for all interned entries.
Its public bulk totals concern only distinct non-empty entries currently
referenced by root-reachable nodes.

## Numeric semantics and admission

### Integers

An integer records its numeric value and the intent necessary for Morphic JSON
round trip:

- signed or unsigned domain;
- the smallest valid width in that domain: 8, 16, 32 or 64 bits;
- decimal, hexadecimal or binary notation; and
- standard or alternate prefix selection where defined.

Width is derived from the value and selected domain. Wider and narrower
metadata are invalid rather than editable presentation hints.

Unsigned values have no sign character. Signed negative values use `-` and
signed non-negative values use `+`. The sign precedes any prefix and magnitude.

Hexadecimal supports normalized `0x` and alternate `#`; input `0X` normalizes
to `0x`. Binary supports normalized `0b`; input `0B` normalizes to `0b`, and no
alternate binary prefix exists. Exact flag-bit packing is deferred.

### Floating point

Floating-point values are finite IEEE-754 binary64 values. Live admission must
reject NaN and positive or negative infinity atomically. Negative zero is valid
and its sign must survive creation, baking, promotion and writing.

A float has no separate signedness or sign metadata and a non-negative float
does not receive a leading `+`. Writer output must distinguish a float from an
integer lexically by including a decimal point or exponent.

Baked validation repeats the finite-value and numeric-metadata checks
independently. It must not trust that bytes originated from a valid live
document.

## Creation and structural mutation

### Creation

Creation and attachment are separate operations. Creation returns a detached
value or detached subtree. Array-valued and object-valued creation atomically
creates both the owner value and its aggregate payload. Allocation or
validation failure must not expose either half.

This paired creation is implemented by a bounded live-document helper which
creates the records one at a time and rolls back the first if the second fails.
It does not require an atomic multi-insert operation or any extension to the
ordered-slot container. `TPodOrderedSlots`, `CStableStrings`, `TPodVector` and
the other generic storage architecture are complete infrastructure and are not
modified for this data-model work.

The optional creation name establishes the value's intrinsic named or
anonymous form. Creation does not pre-validate whether that form will be
acceptable to a future destination.

### Attachment and insertion

Attachment accepts a detached value and validates it at the destination. It
must validate, before structural mutation:

- that every supplied numeric key resolves locally in the target document to
  the required value role and state. Callers must supply keys from that live
  document lifecycle; a key from another document which numerically collides
  with a local key is indistinguishable and is outside the API contract;
- that it is detached;
- that roles and aggregate kinds are compatible;
- the destination's naming and uniqueness requirements;
- absence of cycles;
- membership of an optional insert-before position in that destination;
- child-count and relationship limits; and
- the reachable accounting limits affected by the operation.

Ordinary attachment, insertion, detachment and erasure require no scratch
allocation. Cycle prevention is a bounded ascent through the established parent
chain. These mutation paths do not perform a general integrity audit of the
document or candidate subtree.

Attachment is where structural validity is decided. Creation does not promise
that every destination will accept the result.

Rejection caused by caller input or an applicable accounting limit leaves both
the destination tree and detached candidate unchanged. Successful attachment
preserves the candidate's entire subtree and intrinsic name/object-entry state.

### Detachment, movement and erasure

Detaching removes a non-root value from its parent aggregate while preserving
its name, object-entry flag, payload, owned aggregate and descendants. The
result remains available for reuse in the same document. An aggregate cannot
detach independently.

Moving is detach followed by attach. Reattachment does not rename or otherwise
convert the value merely to satisfy a destination. If the destination rejects
the value's intrinsic form, the caller must use a future explicit conversion
operation if one is provided.

Erasure is distinct from detachment and is recursive. Erasing an attached
non-root value first detaches it, then destroys its aggregate and all
descendants. Root-reachable reference counts have already been removed by
detachment and must not be decremented again during destruction. Erasing the
root is synonymous with erasing all content below it: the root value and its
aggregate remain as the empty initialized document. Reset and deallocation own
the root pair's lifecycle.

## Duplicate-member recovery during attachment

The current live-document stage implements strict duplicate rejection only.
Non-strict recovery construction remains deferred, and the requirements below
must be revisited when that path is implemented.

Duplicate recovery applies only when a named value collides with an existing
name in an object aggregate. The colliding payloads may be of any base kind.
Recovery is not available at the root or at an ordinary array position, and is
not an independently creatable value kind.

Attachment or insertion has a strict duplicate policy whose default is true.
Strict attachment rejects a collision atomically. Non-strict attachment,
principally used by the parser, performs recovery. Non-strict or relaxed in
this context changes only duplicate handling; it never relaxes UTF-8, numeric,
ownership or structural validation.

Recovery state is expressed solely by a recovered-array aggregate. The one
existing named entry value remains the surviving object member and becomes an
array-valued object-entry owner of that aggregate. The aggregate duplicates the
collision name. Its competitors are anonymous values in source order.

Competitor anonymity is an open implementation-review point. Before recovery
is implemented, establish whether prohibiting retained names and object-entry
state provides a necessary semantic or structural guarantee. If it does not,
prefer avoiding the corresponding candidate mutations and revise the recovery
integrity, reference-accounting, baking, writer and repair rules together. The
present live integrity audit continues to enforce anonymity until that decision
is made.

### Second occurrence

For the second occurrence, attachment must:

1. retain the existing named owner's key, name and sibling position;
2. move its former payload and complete subtree into a new anonymous first
   competitor;
3. convert the incoming detached named candidate, without losing its payload
   or subtree, into an anonymous second competitor;
4. convert the surviving owner to an array-valued object-entry value;
5. create its recovered-array aggregate carrying the collision name; and
6. attach the two anonymous competitors in original source order.

If moving a container payload changes the owner/name pairing of its aggregate,
the duplicated aggregate name and all affected reference counts must be
updated.

Capacity needed by recovery should be acquired before mutation where this is a
small, direct preparation step. A pre-mutation failure leaves the original
object and incoming candidate unchanged. If a later unexpected failure cannot
be handled by a small local rollback, the live document enters its known-bad
state rather than carrying a general transaction mechanism.

### Later occurrences

A later occurrence with the same name appends the converted anonymous
candidate to the existing recovered aggregate in source order. It is subject
to the same validation and failure-state requirements.

An insert-before position applies only when inserting a previously absent
name. A collision always retains the original member's position.

The operation result must distinguish at least normal insertion, creation of a
new recovery aggregate, extension of an existing recovery aggregate, rejection
and allocation failure. It must make the surviving owner key available for
parser diagnostics. Exact result types and API names are deferred.

### Diagnostic import and repair

The supported writer diagnostic envelope will later be recognized
automatically by the Morphic parser. Recognition consumes the envelope as
parser syntax: the envelope itself and its fixed identification properties are
not passed to `CLiveDocument`. The parser instead reconstructs the live
document from the envelope's root-attached `document` content, as it existed
before diagnostic serialization. The recognized `document` object supplies
the members of the live document's implicit root object; it does not become a
new child object beneath that root.

The same rule applies to each recognized duplicate-member-array marker. Its
wrapper object, marker fields and `values` property do not become ordinary live
nodes. The parser reconstructs the native binary form: the existing named
owner, its recovered-array aggregate and its anonymous competitors in order.
Any duplicate members newly encountered while parsing the reconstructed
content are handled by the same non-strict attachment path. If they collide
with an already reconstructed recovery owner, they extend that owner's native
recovered aggregate rather than creating nested identification wrappers.

An envelope containing no recovered nodes produces an import-report
observation only and no persistent recovery state. Re-serialization derives a
new diagnostic envelope from the native recovery aggregates; it does not
preserve the parsed wrapper as document content. Malformed envelopes and
unsupported versions are parser-policy work. Source locations which are absent
from the envelope must not be invented.

A genuine recovered aggregate represents at least two competitors. The exact
construction mechanism used when importing a completed competitor list is
deferred; it must preserve the same placement and ownership invariants rather
than expose an unrestricted recovered aggregate.

Repair by selecting a competitor preserves the named owner's key, name and
position. It replaces the owner's payload with the selected competitor's
payload, recursively erases the recovered aggregate and unselected competitors,
and preserves any nested recovery inside the selected subtree. The exact repair
API is deferred.

## Integrity and canonicality

Integrity checking must validate the complete representation relevant to its
boundary, including node roles, value/aggregate alternation, ownership,
parentage, sibling order, counts, first/last references, aggregate-kind and
owner-type agreement, duplicate names, owner/aggregate name agreement, string
references and counts, numeric metadata and finite floats.

Routine access and mutation guard against invalid, stale and role-inappropriate
keys, including checking that the record's stored self key matches the lookup
key. They otherwise operate on the assumption that an initialized document's
established structure is valid. Comprehensive traversal belongs to the
explicit integrity check rather than every mutation path.

If a mutation encounters an internal contradiction, or if a failure after
mutation begins cannot be recovered by a small local rollback, the live
document is marked as having known-bad integrity. It is then not ready for
further use and fails integrity checking. Reset or deallocation is the recovery
boundary. This state is for unexpected corruption and severe internal failure;
ordinary input rejection and pre-mutation allocation failure do not poison the
document.

The live document maintains exact totals for root-reachable value nodes and
root-reachable aggregate payloads. The aggregate total includes object,
ordinary-array and recovered-array aggregates. Detached nodes and payloads do
not contribute. Attach, detach, recovery, repair, recursive erasure, reset and
promotion update or establish both totals with the structural change, and an
integrity audit independently recomputes them from the root. The separately
maintained recovered-aggregate total is a subset used for canonicality.

For user-facing information, a document is **canonical** exactly when no
recovered-array aggregate is reachable from the root. Detached recovery
subtrees do not affect this answer. Integer notation and signedness do not
affect canonicality.

The live document maintains the exact number of recovered-array aggregates
currently reachable from the root. Canonicality is therefore equivalent to
that count being zero. Attachment, detachment, recovery creation, repair,
recursive erasure, reset and promotion update or establish the count with the
corresponding structural change. Integrity checking must independently
recompute it from the root and compare the result with the stored count.

Detached subtrees do not cache a recovery count. Attachment and detachment
already traverse the subtree to update root-reachable name and string reference
counts; the same traversal counts recovered-array aggregates and adjusts the
document-level total. Recovery creation or repair within an attached subtree
can update the total directly. No additional node or per-subtree field is
reserved for recovery accounting.

Canonicality is descriptive only. It does not prohibit mutation, baking,
promotion, writing or other functional operations. Those operations apply
their own explicit validity requirements and must not use canonicality as a
substitute for integrity validation.

## Live lifetime and move semantics

`CLiveDocument` is transitory, single-thread working storage with no internal
synchronization. It is never transferred between workload threads. Reads are
permitted only while mutation is prohibited and the document is quiescent;
lifetime and external coordination remain the caller's responsibility.

The eventual live type must support explicit `noexcept` move construction and
move assignment so a parser can publish a completely parsed private result
atomically within the permitted ownership boundary. A move resets the source
and retains allocator attribution and the validity of keys, stable IDs, string
views and cursors to the extent supported by the selected storage design.
References to the source document object do not follow its contents. Move
support does not authorize cross-thread transfer of a live document or of a
builder-like working representation.

`CLiveDocument` does not support memory-accounting reattribution. Its directly
owned allocations retain their original accounting contexts for their complete
lifetime, including across moves. Moving a live document while a different
context is ambient must not retag its storage. A document which must cross an
accounting-context boundary is baked in its current context and promoted into a
fresh live document whose allocations are made in the destination context.
That bake-and-promote boundary is the only supported accounting-context
transfer path for live content.

## Baked documents

`CBakedDocumentBlock` owns one transferable immutable byte block.
`CBakedDocument` is a non-owning immutable view over such bytes. Editing a
baked document requires promotion into a new compact live document.

Creating or resetting a view must not automatically perform a full-document
integrity check. Full integrity checking is an explicit, user-controlled
operation. Callers decide when the cost and trust boundary require it. An
unchecked view must not be described as checked merely because it has been
bound to bytes, and callers must not treat untrusted bytes as validated until
the explicit check succeeds. The exact readiness/checked-state API is deferred.

There is no public `CBakedDocumentBuilder` or parallel mutable baked data
model. An owning block builds directly and atomically from a const
`CLiveDocument`. Internal staging, mappings and measurement passes are
permitted implementation details.

Only root-reachable nodes are baked. Detached values and other unreachable
storage are omitted. Only strings referenced by emitted nodes are retained,
apart from the mandatory empty entry in each table. Live keys and stable IDs
are rewritten into the baked artifact's own dense index domains.

The nonzero live reference counts identify the complete source entries for the
two baked string tables without a preliminary node-tree walk. Baking may select
and sort those entries, then establish both live-to-baked string maps before
node emission. The later node traversal still validates and emits structure;
it is not needed to discover which strings are live.

The root value is the first logical value record in the baked node table.

The direct value children of every object, ordinary array and recovered array
must occupy one contiguous fixed-stride record range in semantic order. No
aggregate record may be interleaved with that range. The range representation
must make ordinal access a checked index calculation from its first record and
count, so array access by ordinal is O(1). Object members use the same linear
representation: callers and debuggers can inspect them as sequential value
records, and ordinal object-member access is O(1). This requirement does not
itself require O(1) object lookup by name; a later layout may add a separate
lookup accelerator without changing the ordered value range.

Baked child ranges therefore require an explicit first/count range boundary,
which also determines the last element, but no stored previous- or next-sibling
links. Dense range position supplies their linear addressing.

Nested aggregate metadata and descendants lie outside the containing
aggregate's direct-child range. If the baked layout retains explicit aggregate
records, each aggregate record precedes its associated direct-child range and
refers to that value-only range; whether it is immediately adjacent is a
physical-layout choice. If aggregate metadata is folded into the owning value,
the owner refers directly to the range. Empty aggregates use a canonical empty
range in either encoding.

### Implementation direction: staged live construction

A natural atomic construction path is:

```text
const source live document
        |
        v
clone reachable content into a staged live document
        |
        +-- omit detached/unreachable nodes and zero-reference strings
        v
sort staged name and value-string containers
        v
measure one final block
        v
emit -> commit block by move
```

This direction reuses live-document invariants for canonicalization and
compaction, but it is not a mandated algorithm. An implementation may use
other private phases if the observable result and failure atomicity are the
same. If callers need a separate canonicalization workspace, they use another
`CLiveDocument`, not a public baked builder.

### Baked string tables

The baked representation has separate property-name and string-value tables.
Each table always contains a valid zero-length, NUL-terminated string at sorted
index zero. Zero is not an invalid sentinel.

Every other emitted entry is used by reachable content. Nodes refer directly
to the final sorted index. Offset/size records occur in exactly the same sorted
order as the packed terminated bytes; there is no secondary ordering table.

Sorting uses unsigned-byte, content-first lexicographic comparison. The first
differing byte determines order; if one value is a prefix of another, the
shorter sorts first. Thus:

```text
empty < a < aa < b
```

This is not length-first ordering.

Every string remains physically NUL-terminated. Its logical size excludes the
terminator. Payloads may contain canonical modified `C0 80` for logical U+0000
but no literal zero byte. Baked validation must independently check ordering,
uniqueness, references, logical bounds, physical terminators and encoding.

### User-controlled baked validation

Baked bytes may be untrusted input even when produced locally. A caller which
requests a full integrity check must cause the view to independently recompute
and validate, at minimum:

- header identity, supported format version and all known flags;
- section bounds, alignment, non-overlap and complete block size;
- node roles and strict semantic value/aggregate alternation under the selected
  explicit or folded aggregate encoding;
- root position and root invariants;
- single ownership and parent/child relationships;
- direct-child range bounds, value-only membership, single-parent ownership,
  non-overlap and semantic order;
- aggregate-kind and owner-type/name agreement, including the placement and
  range reference of explicit aggregate records when that encoding is used;
- valid placement and minimum cardinality of recovered aggregates;
- name and string indices, sorting, uniqueness, terminators and UTF-8/`C0 80`;
- finite binary64 payloads and valid integer metadata; and
- any checksum or integrity field selected by the eventual layout.

Binding a view to bytes does not implicitly run these checks. Local accessor
preconditions or defensive bounds checks are distinct from a whole-document
integrity audit and do not silently promote a view to checked status. Code
which requires trusted structure must explicitly request the audit and observe
its result.

The exact header fields, header and node sizes, alignment, offset widths,
required-range index encoding, optional lookup indices, packed flags and new
format version are not settled here.
The replacement format is intentionally incompatible. Old formats must be
rejected by version rather than inferred or silently migrated.

## Promotion

Promotion constructs a new compact `CLiveDocument` from a baked view whose
integrity the caller has explicitly established. It establishes a fresh
live-key and stable-ID universe while preserving all document semantics:
order, names, object-entry state, payload kinds, numeric intent, string
contents and recovery aggregates.

Promotion must be atomic. Failure leaves the destination unchanged. The
promoted document receives the ordinary implicit root contract rather than a
caller-selected root.

## Preserved writer-facing semantics

The current JSON writer and tests will be archived during migration and later
recovered as a refactor base after the new live and baked documents stabilize.
This document does not require writer changes now, but the following semantic
requirements survive:

- Morphic-preserving output retains integer domain, notation and prefix choice.
- Strict JSON output emits the exact decimal value of the full `uint64_t`
  range and may drop Morphic lexical intent only where strict syntax requires.
- Finite floats use a shortest-round-trip representation, retain `-0.0`, and
  include a decimal point or exponent.
- Names and strings are quoted. Canonical `C0 80` is never emitted as raw
  modified UTF-8 and is handled by the explicit logical-U+0000 policy below.
- Optional ASCII-only escaping remains available.
- Traversal remains iterative rather than consuming one call-stack frame per
  nesting level.
- A root-reachable recovered aggregate causes automatic diagnostic-envelope
  output, with competitors preserved in source order.

The supported outer envelope remains:

```text
{"$morphic.recovery":{"format":"diagnostic-document","version":1,"document":<root>}}
```

Each recovered duplicate is represented as:

```text
{"$morphic.recovery":{"kind":"duplicate-member-array","values":[<competitors>]}}
```

The original colliding member retains its name and object position. Recovery
output does not fabricate source locations. The future parser will recognize
this supported envelope automatically; malformed and unsupported envelope
policy remains parser design.

### Logical-U+0000 writer policy

The writer detects canonical modified `C0 80` while it already scans each
emitted name and string for quoting and escaping. The live and baked documents
do not maintain a presence flag or summary count for it.

The writer provides three policies:

- **Escape**, the default: emit each logical U+0000 as the standard JSON escape
  `\u0000`.
- **Reject**: fail the complete write atomically if any emitted name or string
  contains logical U+0000.
- **Replace**: substitute every logical U+0000 with a caller-supplied valid
  Unicode scalar value.

The replacement must be an alternative to U+0000 and must not be a surrogate
code point or otherwise invalid Unicode. It is inserted only as string content
and then processed by the ordinary JSON quoting, control-character and optional
ASCII-escaping rules, so it can never be interpreted as JSON structure. An
invalid replacement option fails before output begins.

On successful output, the writer reports the number of logical-U+0000
occurrences it handled and which policy handled them. Under Escape this is the
number of `\u0000` escapes emitted; under Replace it is the number of
substitutions. Under Reject, encountering an occurrence returns the dedicated
failure status, discards partial output and leaves completed-output counters at
zero under the writer's atomic-failure convention.

## Thread and architecture boundaries

Live documents and any private bake staging are confined to one workload
thread and do not cross workload-thread boundaries.

Ownership of a completed `CBakedDocumentBlock` may transfer only to Host under
the Stage 3 architecture. Other threads receive views whose required integrity
checks have been explicitly controlled by the responsible caller, load
responses or storage identifiers under the previously agreed ownership model.
A non-owning view never extends the lifetime of its backing bytes.

Schema, parser and writer layers do not gain ownership of a live document or
baked block merely by consuming it. Their ownership and publication behavior
must follow these boundaries explicitly.

## Deferred API and physical-layout choices

The following are intentionally unresolved implementation work and must not be
inferred from examples or from the superseded implementation:

- the exact packed encoding of value kind, aggregate kind, object-entry state
  and numeric intent;
- canonical values for role-inapplicable fields and how much aggregate
  machinery can be eliminated physically;
- cursor and revision representation;
- concrete create, attach, insert, convert, repair and diagnostic-import API
  names, signatures and result types;
- the safe construction API for an imported completed recovery list;
- internal bake staging and live-to-baked mappings;
- whether baked aggregates use explicit records or metadata folded into their
  owning value records;
- the exact first-child/count encoding for the required contiguous value ranges
  and any supplemental object-name lookup structure;
- baked header fields, flags, record sizes, alignment, section order, offset
  widths, checksum and format version;
- the concrete option and report types for logical-U+0000 escape, rejection and
  caller-supplied Unicode-scalar replacement;
- the explicit baked integrity-check and checked-state interface, including
  which operations require a previously successful user-controlled check; and
- the exact parser behavior for malformed or unsupported diagnostic envelopes.

These choices may be settled only by later design and implementation evidence.
They must preserve the normative semantics and operation-specific failure
contracts above.

## Subsequent work and migration boundary

After this specification is reviewed, the obsolete implementation and tests
will be archived through a separate migration task. That workflow is not part
of this document change and must not be inferred as permission to move, remove,
format or edit existing implementation files.

Part 1 can then proceed in this order:

1. implement and test the replacement live and baked documents;
2. recover and refactor the writer against the stable baked view;
3. resume the parser layers: linter, compatible structural prepass and relaxed
   parser;
4. add direct binary and JSON file round trips; and
5. add asynchronous Host/Executive functional round trips.

Schema remains a later Part 2 consumer. It may interpret base strings or other
values as domain-specific data, but such interpretation does not add base node
kinds or weaken this document's ownership and validation rules.
