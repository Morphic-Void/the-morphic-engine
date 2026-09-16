Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   revised_data_model.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   14 Sep 2026

# Data-model semantic specification

## Status and scope

This document is the normative semantic specification for the incompatible
replacement of TheMorphicEngine's first live and baked document model. It
defines the abstract model, public operations, integrity boundaries and
ownership rules. Requirements expressed with **must**, **must not** and
**only** are normative.

This records the current document model: shared diagnostics, native empty names,
object/array roots, newline metadata, caller policy and ordinary collision arrays.
The text and parser contracts are defined in [the text format](document_text_format.md)
and [parsing and reporting](document_parsing.md). See the
[documentation index](README.md) for the division of responsibilities.

Implementation rationale belongs in
[design notes](data_model_design_notes.md). Completed implementation history
belongs in [completed milestones](../project/completed_milestones.md).

The normative physical baked layout is defined separately in
[baked-document format](baked_document_format.md).

The v1 design and implementation are obsolete and are preserved in Git history
and the local non-redistributed archive. C++ signatures and live-record packing
remain implementation choices unless explicitly settled here.

## Terminology

A **value node** represents an empty live placeholder, JSON scalar, array or
object.

An **aggregate node** is an internal composition-owned payload holding ordered
value children. Its kind is object or array. It is not an
independent value.

An **owner** is the value node which owns an aggregate. An aggregate has exactly
one owner.

A **named value** has its name-presence flag set. Its property name may be empty.
An **anonymous value** has that flag clear and canonical empty name ID zero.

Every named value is an object entry. If it is not already within an object,
an anonymous containing object is implied. This rule is independent of payload
type: it applies equally to numbers, strings, nulls, Booleans, objects, arrays
and empty placeholders. An object-valued payload does not
supply the containing object for its own name. The implication requires no
additional live node or baked record.

A **detached value** has no parent aggregate. It may still own its complete
payload and descendants.

A **live key** or **live string ID** is valid only within one live-document
lifecycle. A **baked index** is valid only within one baked artifact. None is a
durable application identity.

## Logical node model

The hierarchy strictly alternates between values and their aggregate payloads:

```text
value
  scalar or empty                         (end)

value: array or object
  `-- aggregate
        `-- zero or more value children   (repeat)
```

The following invariants apply:

- A non-empty scalar value owns no aggregate.
- An empty live value owns no payload.
- An array or object value owns exactly one aggregate of the
  corresponding kind.
- Every aggregate has exactly one value owner and is never shared or exposed
  as a public value.
- Every non-root value is either attached to exactly one aggregate or detached.
- Child order is semantic.
- The structure is a tree. Attachment must not introduce sharing or a cycle.
- An aggregate's name is always the canonical empty name. The owner's name is
  authoritative.
- Object-entry state is determined by name presence independently of name ID
  or length. Aggregate records never have name presence.

The live representation may use one role-dependent physical node type for
values and aggregates. Public operations expose and accept value keys only.
Role-inapplicable relationship queries return the corresponding invalid result.

Live keys are allocated monotonically and are not reused during a document
lifecycle. Physical slots may be reused, so key order and slot adjacency have
no structural meaning.

## Root

Initialization creates an implicit anonymous object value as the root and its
object aggregate. The root is the first logical value and the first baked
value.

The root is not replaced, detached or destroyed by ordinary callers. Reset
owns its lifecycle. Initialization must establish the complete root or leave
the document uninitialized.

`set_root_type` changes an empty root between object and array, preserving its
key and aggregate. A non-empty root rejects the operation. Detached values and
interned strings do not count as root contents. Erasing the root through either
`erase` or `erase_payload` clears descendants while preserving its container
kind. Reset restores the default object root. Baking, validation, promotion and
writing preserve the selected root kind.

## Names and strings

Property names and string values occupy separate strong ID domains and
separate stable stores. Equal bytes in the two roles do not share identity.

ID zero is the valid canonical empty string in each live domain. Baked string
index zero likewise denotes a physically NUL-terminated empty string. Invalid
IDs are distinct from zero.

Name presence makes a value eligible for object attachment. A present empty
`CStringView` creates an empty-named entry; an absent view creates an anonymous
value. Both use name ID zero. `name(value)` returns an absent view for an
anonymous value and a present empty view for an empty-named entry. Canonical
empty string queries also return present empty views without allocating a
synthetic table entry. `$morphic-empty` is ordinary non-empty user text.

`set_name` preserves the payload and position, rejects collisions and enforces
the destination's naming rules. `object_child` accepts either a name ID or
view; ID zero and a present empty view find the empty-named child, while an
absent view cannot select it. Detached values can be renamed and reattached
through the ordinary operations. Payload-only moves retain the target's name
and presence.

Names cannot contain LF, CR, VT, FF, NEL, LS or PS. Structural checking rejects
literal or escaped breaks in names before construction; direct admission,
renaming and checked baked validation enforce the same decoded-content rule.
The parser preserves native empty names in both quote modes.

String admission is length-aware and canonicalizes logical U+0000:

1. a literal zero byte becomes the exact modified-UTF-8 sequence `C0 80`;
2. an existing exact `C0 80` sequence is preserved; and
3. all other input must be strict UTF-8.

Malformed, truncated, overlong or out-of-range input and isolated surrogates
must be rejected. Admission must remain safe when the supplied range aliases a
live string store which the operation may grow.

Live string entries remain stable even when unused. The document does not
maintain permanent root-reachable reference counts or distinct-used-string
totals. Baking or a requested analysis discovers references by walking the
tree.

The live document exposes each string domain's ID at a requested lexical rank.
Rank zero is the canonical empty ID; an unavailable rank is invalid. These
observations permit deterministic public-only baking and serialisation without
exposing the underlying stable stores.

Each string value independently carries newline-escaping suppression, queried
by `suppresses_newline_escaping` and changed by
`set_newline_escaping_suppressed`. Non-string values reject this mutation.
The setting defaults to false, transfers with the payload and is cleared on
payload erasure. Baking, promotion and whole-document moves preserve it even
when differently configured values share the same interned text. Parsing sets
suppression for a string whose linted source contains a literal LF. Escaped-only
line breaks and literal backslash-plus-letter spellings leave it unset.

## Numeric values

An integer records:

- signed or unsigned domain;
- its smallest valid width in that domain: 8, 16, 32 or 64 bits;
- decimal, hexadecimal or binary notation; and
- standard or alternate prefix selection where defined.

Width is derived from the value and domain. Unsigned values have no sign.
Signed negative values use `-`; signed non-negative values use `+`. Hexadecimal
uses normalised `0x` or alternate `#`. Binary uses normalised `0b` and has no
alternate prefix.

The validated width is stored and directly queryable in both live and baked
documents. Consumers, including later schema processing, must not need to
rederive it from the numeric value on each query. Its exact packed encoding is
a physical-layout choice.

Floating-point values are finite IEEE-754 binary64 values. Admission must
reject NaN and infinities. Negative zero is valid and must survive baking,
promotion and writing. Writer output must distinguish a float lexically from
an integer.

## Live construction and mutation

### Creation

Creation returns a detached value or subtree. Aggregate-valued creation must
return a complete owner/aggregate pair or fail. `create_empty` creates a live
placeholder, not JSON null.

The public set of value types includes empty, null, Boolean, signed integer,
unsigned integer, floating point, string, array and object.

### Attachment and insertion

Attachment validates before changing topology that:

- supplied keys resolve locally to the required roles;
- the candidate is detached;
- the destination accepts the candidate's name form;
- an object destination has no child with the same normalised name;
- an optional insertion position belongs to the destination; and
- the attachment cannot create a cycle.

Destination rules are:

- an object accepts only named values and requires unique immediate names;
- an array accepts named or anonymous values.

Append and positional insertion differ only in the selected sibling position.
There is no relaxed attachment mode which silently bypasses these rules.

### Detachment and erasure

`detach(value)` unlinks the original complete value or subtree without
allocating. The detached value retains its key, name, payload and descendants.
It remains owned by the document and available for reuse. If abandoned, it may
be erased explicitly and is reclaimed by document reset or destruction.

`detach_payload(value)` requires a non-root, non-empty value. It allocates a new
anonymous detached value containing the complete payload and descendants of
`value`. The original value retains its key, name, parent and sibling position
and becomes empty.

`erase(value)` recursively destroys a non-root value and its payload. If it is
attached, it is first unlinked. Erasing the root has the special clear
semantics described above.

`erase_payload(value)` recursively destroys the payload and descendants while
retaining that value's key, name, parent and sibling position. A non-root value
becomes empty. For the root it has the same clear semantics as `erase(root)`.

### Payload attachment

`attach_payload(target, source)` requires:

- distinct source and target values;
- an empty target;
- a non-empty, anonymous, detached source; and
- a target which is not within the source subtree.

It moves the source payload and descendants into the target. The target keeps
its key, name and structural position. The now-empty source shell is erased.
The operation returns the target key and requires no allocation.

These operations may be composed for ordinary transfer, collision extension,
parser construction and promotion. They do not imply specialised collision
privileges.

## Explicit collision extension

`extend_object_child(object, candidate)` accepts a detached named value and
returns the retained object member on success. With no collision it performs
ordinary insertion and returns the candidate. On collision it preserves the
existing member's key, name and position, consumes the candidate shell and
combines their payloads in encounter order:

- a non-array receiver becomes an ordinary array containing its old payload
  followed by the incoming payload;
- an array receiver appends an incoming non-array payload; and
- two arrays become the two complete children of a new ordinary array.

No children are spliced or regrouped. Empty names collide normally. Empty live
placeholders remain empty children until baking substitutes null. Payload
metadata follows each payload. Failure preserves both inputs and their topology;
all required allocation precedes mutation. Ordinary insertion and renaming
continue to reject collisions. The parser uses this operation for every object-name collision.

## Observation and integrity

Public observations include completeness and node/category counts. Unless a value is already held for another
essential purpose, these observations are computed on demand rather than kept
coherent through every mutation.

A live document is **complete** when no empty value is reachable from the root.
Detached subtrees do not affect completeness. This observation does not
substitute for integrity or prohibit mutation, baking, promotion or writing.
The recovery-specific canonicality and content queries have been removed.

The document must retain allocation accounting needed by the framework,
including memory token, allocation count and allocation size totals. This is
separate from tree, content and string-reference accounting.

Live and baked values share a 16-bit flag encoding for integer metadata,
name presence and per-string newline suppression. Live nodes decode integer
metadata on demand rather than storing a second representation. Baking copies
the shared flags and adds sibling-position bits; promotion removes those bits
because live topology is held in links. Live nodes remain 40 bytes with explicit
reserved storage, and baked values remain 32 bytes. The physical encoding is
specified in [the baked format](baked_document_format.md).

An explicit integrity check walks established structure and verifies at least:

- the root contract;
- valid node roles and value/aggregate alternation;
- owner, parent and sibling reciprocity;
- single ownership and absence of cycles or shared descendants;
- aggregate kind matching its owner;
- canonical empty aggregate names;
- destination naming rules and object-name uniqueness;
- payload/type agreement and numeric validity;
- valid string IDs; and
- agreement with retained memory-accounting totals.

Integrity checking must not depend on persistent reachability summaries in
order to validate those same summaries.

Routine access and mutation still reject invalid, stale and role-inappropriate
keys. They otherwise trust established private structure rather than repeating
a whole-document integrity walk.

## Failure and invalidation

Every failure must be reported. Failure must never be accepted silently.

Caller-controlled validation and allocation failures should be detected before
mutation where doing so is simple. General transactional mutation and rollback
are not required. An operation which cannot preserve coherent data after a
partial failure must clearly invalidate the affected document or result.
Internal contradictions may place a live document in its reset-only known-bad
state.

Failed baking may retain reusable scratch capacity or an allocated output block,
but it must not publish that block as a ready baked document.

## Memory ownership and moves

The live document uses the framework's allocation and memory-accounting
infrastructure. Production algorithms use the existing no-exception containers
and controlled allocation facilities; they do not depend on STL or library
algorithms which may throw or escape that control.

The STL policy is constrained use, not a blanket prohibition. A facility must
not introduce exceptions, allocate outside framework control, or make required
behaviour materially dependent on its vendor or platform. Caller-buffer-based
numeric conversion may be used after checking its specified behaviour and
availability on supported toolchains.

Moving a live document within a compatible allocation context may be supported
when it follows naturally from the existing containers. The model does not
require elaborate accounting reattribution or allocation-context transfer.
Bake and promote are the general route between allocation contexts.

## Baking

Baking consumes a coherent live document and produces an immutable baked
artifact. The artifact itself occupies one allocation. The baking process may
use separately allocated scratch state and is not required to complete in one
allocation; it should avoid unnecessary copying, allocation and memory churn.

`CBakedDocumentBlock` owns the transferable immutable byte allocation.
`CBakedDocument` is a non-owning immutable view over compatible bytes. There is
no public mutable baked document or public baked builder.

`memory::can_reattribute_to(block, target)` and `memory::reattribute(block, target)`
use the block's common attribution and replacement interface with the existing
framework memory-context rules; a null target selects the ambient context.
An allocated block can move attribution only between contexts sharing the
same allocator. Reattribution changes accounting and the owning context,
without allocating, copying bytes, relocating storage or rebuilding the checked
view. Failure leaves the allocation, view and accounting unchanged. Empty and
moved-from blocks follow the byte buffer's empty-storage context rules.
Ordinary C++ moves preserve the source allocation's attribution.

The SYSTEM erased-owner payload `BakedDocumentAsset` contains a `block` member.
The existing owning-message transport reattributes its payload shell and baked
allocation together through private nested-storage hooks. The baked artifact
still occupies one allocation; the erased owner's payload shell is separate.
Once nested in an erased owner, transfer the complete owner rather than
reattributing its block independently: mismatched shell/block source contexts
are rejected by the existing source-validation rule. Views remain borrowed and
require the owning asset to outlive their use.

Baking and promotion are exposed by a separate public translation layer.
Neither document representation depends on the definition or construction
details of the other. The translation implementation consumes the ordinary
public observation and construction surfaces. Narrow private access is limited
to publishing a completed owning baked block; a staged live document is
published through its ordinary move operation.

Before emission, a live-tree crawl may build an externally owned analysis
structure using framework allocation. That analysis may contain reachable
value counts, per-string reference counts,
mappings, section sizes and other prerequisites. It is not persistent
live-document state.

The baked artifact contains only root-reachable content. Its indices are dense
and artifact-local. Direct children of each aggregate occupy a contiguous range
in semantic order so ordinal array access and sequential object traversal are
straightforward. Nested descendants need not be adjacent to their ancestors.

Empty is a live-only state. Baking encodes every reachable empty value as an
ordinary null value while preserving its name and position.

The baked representation folds immutable aggregate metadata into each owning
value record. It contains no aggregate records. The fixed header, value record,
section layout and encodings are defined in `baked_document_format.md`.

### Baked string tables

Property names and string values remain separate tables. Each table contains
the valid empty string at index zero. Every non-empty emitted entry is
root-reachable.

Entries are sorted by unsigned-byte, content-first lexicographic order. At the
first differing byte the lower byte sorts first; a prefix sorts before the
longer value. Offset/size records and packed NUL-terminated bytes use this same
order. There is no secondary ordering table.

### Baked validation

User-controlled full validation of potentially untrusted baked bytes must
independently check the selected layout's:

- identity, version, total size, alignment and section bounds;
- root and value/aggregate semantics;
- ownership and direct-child ranges;
- names, strings, ordering, uniqueness, bounds, terminators and encoding;
- payload/type agreement and numeric metadata.

Public binding of arbitrary bytes performs full validation. A baked view is
ready only after that validation succeeds; there is no public unchecked view.

The checked view exposes value classification, names and both string domains,
typed scalar payloads and integer metadata, parent and sibling relationships,
child ranges, ordinal array access and object-child lookup. The retired recovery
kind has no public query or stored summary state.

The current version 3 byte format is intentionally incompatible with earlier
versions. Validators reject unsupported versions rather than infer or silently
migrate them.

## Promotion

Promotion constructs a new compact live document from an explicitly validated
baked view. It creates fresh live keys and string IDs while preserving order,
names, payload kinds, numeric intent and string contents.

A failed promotion must not publish a partially constructed destination as a
coherent live document. It need not preserve unused capacity or allocation
history.

## Text ingestion and parsing

The complete [text format](document_text_format.md) defines source encoding,
line endings, grammar, root inference, string escapes, numeric classification,
NUL provenance, collision interpretation and text round trips.
[Parsing and reporting](document_parsing.md) defines the byte-view entry point,
shared report, diagnostic locations, findings and caller policy.

The model-level contract is that parsing constructs privately and publishes
only after processing succeeds and policy accepts. Failures and rejections
preserve the destination. Empty byte input constructs an object; scalar input
constructs an array containing that value. Every logical NUL enters canonical
string storage, names retain presence independently of their length, and
duplicate members use the public collision-extension rules.

### Shared findings and policy definitions

The [findings inventory](document_parsing.md#findings-inventory) and
[acceptance policy](document_parsing.md#acceptance-policy) define every shared
observation and permission. One processing state and terminal diagnosis remain
independent of accumulated findings and final policy acceptance.

### Current shared text grammar

The [text grammar](document_text_format.md#containers-and-root-selection)
defines supported syntax independently of permission. The
[standalone structural checker](document_parsing.md#standalone-structural-checking-and-linting)
checks syntax without guaranteeing numeric representability or construction
feasibility; optional capacity estimates remain separate from diagnostics.

### Live construction and interpretation

Construction retains integer domain, width, notation and prefix, finite
binary64 values, native empty names and source-derived per-string newline
metadata. Duplicate names extend ordinary arrays in encounter order.
Singleton-object unwrapping retains the named child's complete payload.
The [text format](document_text_format.md#collision-extension-and-singleton-objects)
specifies these interpretations and their source examples.

## Serialization-facing requirements

Writers consume only the checked baked interface and provide two modes:

- Morphic output retains integer domain, notation and prefix intent.
- Strict output emits strict JSON where possible, including the exact decimal
  value across the full `uint64_t` range. It reports normalisation of Morphic
  features and does not promise to round-trip every feature, such as explicit
  positive signed-integer intent.

Both modes quote names and strings. ASCII-only escaping is an independent
option for either mode and escapes every non-ASCII scalar, using surrogate
pairs where necessary. Finite floats use shortest-round-trip output, retain
`-0.0` and contain a decimal point or exponent. Writing is iterative.

Every written newline normalises to LF, recognising CRLF and LFCR as compound
breaks and also covering VT, FF, NEL, LS and PS. Layout and trailing newlines
use literal LF; selectable CRLF formatting has been removed. Within string
values, the default emits `\n`. Morphic mode emits literal LF when that value
suppresses newline escaping; strict JSON always emits `\n`. Other escaping
rules remain in force. Writing changes neither stored content nor metadata;
text round trips compare newline-normalised content.

### Named entries and anonymous objects

A named value outside an object is written inside an anonymous object wrapper.
For example, named integer, string, object and array payloads in an array write
as `[{"n": +1}, {"s": "text"}, {"o": {}}, {"a": []}]` in Morphic mode.
The same rule applies to every other payload type. Within an object, its
containing braces already supply the required context.

When an anonymous ordinary object directly within an ordinary array contains
exactly one named member after collision extension, parsing removes the
redundant wrapper and retains the named child with its complete payload.
The rule does not depend on that payload's type. Empty and multi-member objects
remain objects; the document root is never unwrapped.

There is no additional semantic distinction for an intentional anonymous
singleton object requiring a preservation tag. Text round trips compare this
normalised semantic form, not redundant wrapper nodes, original live keys,
detached content or live placeholders already baked as null.

### Ordinary arrays and dollar-prefixed names

Collision arrays serialise as ordinary JSON arrays in both writing modes.
No wrapper or reserved-name escaping is emitted. `$morphic`, `$$morphic` and
other dollar-prefixed names retain their decoded spelling. Former version/type/
values objects have no special interpretation, even at the document root;
their fields follow ordinary parsing, numeric conversion and collision rules.

Baked version 3 retains the `MBD2` family magic and the 32-byte record layout.
Earlier versions are rejected, and retired value tag 8 is invalid even under
version 3. There is no compatibility reader or recovery conversion.

## Thread and publication boundaries

Live documents and bake scratch state remain on one workload thread. A completed
owning baked block may cross the architecture's publication boundary. A
non-owning baked view never extends the lifetime of its backing bytes.

Parser, writer and schema layers do not acquire ownership merely by consuming a
document or view.

## Deferred choices

The following are not yet normative:

- exact public C++ names, signatures and result types;
- live record packing and role-inapplicable field values;
- lookup accelerators, including O(1) object lookup by name;
- future syntax extensions beyond the documented grammar and consumer diagnostic presentation;
- live cursors and revisions; and
- the later typed-data and schema architecture.
