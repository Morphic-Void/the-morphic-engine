Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   revised_data_model.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   6 Sep 2026

# Revised data model

## Status and scope

This document is the normative semantic specification for the incompatible
replacement of TheMorphicEngine's first live and baked document model. It
defines the abstract model, public operations, integrity boundaries and
ownership rules. Requirements expressed with **must**, **must not** and
**only** are normative.

Implementation rationale and proposals belong in
`docs/backlog/data_model_design_notes.md`. Delivery order and current status
belong in `docs/backlog/data_model_roadmap.md`. Neither document overrides this
specification.

The normative physical baked layout is defined separately in
`docs/backlog/baked_document_format.md`.

The archived v1 design and implementation are reference material only. C++
signatures and live-record packing remain implementation choices unless
explicitly settled here.

## Terminology

A **value node** represents an empty live placeholder, JSON scalar, array,
object or recovered array.

An **aggregate node** is an internal composition-owned payload holding ordered
value children. Its kind is object, array or recovered array. It is not an
independent value.

An **owner** is the value node which owns an aggregate. An aggregate has exactly
one owner.

A **named value** has a non-empty property name. Its object-entry state is
derived from that name. An **anonymous value** has the canonical empty name.

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

value: array, object or recovered array
  `-- aggregate
        `-- zero or more value children   (repeat)
```

The following invariants apply:

- A non-empty scalar value owns no aggregate.
- An empty live value owns no payload.
- An array, object or recovered-array value owns exactly one aggregate of the
  corresponding kind.
- Every aggregate has exactly one value owner and is never shared or exposed
  as a public value.
- Every non-root value is either attached to exactly one aggregate or detached.
- Child order is semantic.
- The structure is a tree. Attachment must not introduce sharing or a cycle.
- An aggregate's name is always the canonical empty name. The owner's name is
  authoritative.
- Object-entry state is derived from a value's non-empty name and is not an
  independent semantic property.

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

The root is not selected, replaced, detached or erased by ordinary callers.
Reset owns its lifecycle. Initialization must establish the complete root or
leave the document uninitialized.

Erasing the root through either `erase` or `erase_payload` clears its
descendants and restores the initial root object; it does not leave an empty
root value.

## Names and strings

Property names and string values occupy separate strong ID domains and
separate stable stores. Equal bytes in the two roles do not share identity.

ID zero is the valid canonical empty string in each live domain. Baked string
index zero likewise denotes a physically NUL-terminated empty string. Invalid
IDs are distinct from zero.

A non-empty name makes a value named and eligible for object attachment. An
empty name makes it anonymous. Empty JSON property names are unsupported.

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

## Numeric values

An integer records:

- signed or unsigned domain;
- its smallest valid width in that domain: 8, 16, 32 or 64 bits;
- decimal, hexadecimal or binary notation; and
- standard or alternate prefix selection where defined.

Width is derived from the value and domain. Unsigned values have no sign.
Signed negative values use `-`; signed non-negative values use `+`. Hexadecimal
uses normalized `0x` or alternate `#`. Binary uses normalized `0b` and has no
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
unsigned integer, floating point, string, array, object and recovered array.
Recovered arrays are not restricted to collision-handling internals.

### Attachment and insertion

Attachment validates before changing topology that:

- supplied keys resolve locally to the required roles;
- the candidate is detached;
- the destination accepts the candidate's name form;
- an object destination has no child with the same normalized name;
- an optional insertion position belongs to the destination; and
- the attachment cannot create a cycle.

Destination rules are:

- an object accepts only named values and requires unique immediate names;
- an ordinary array accepts named or anonymous values; and
- a recovered array accepts only anonymous values.

Recovered arrays have no minimum cardinality and no collision-only origin
requirement. An empty or single-child recovered array is coherent. A recovered
array may contain an anonymous recovered array.

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

These operations may be composed for ordinary transfer, duplicate recovery,
parser construction and promotion. They do not imply specialised recovery
privileges.

## Recovered arrays

A recovered array preserves an ordered set of anonymous competing values. It
is a first-class public live value type so that parsers, promotion and users can
construct and inspect recovered content using ordinary operations.

Its special invariant is limited to anonymous children. The model does not
require that it was produced by a collision, that it contains at least two
children or that it remains attached to an object.

Recovery repair can be expressed by ordinary composition: detach the selected
anonymous competitor, erase the recovery owner's payload, then attach the
selected payload to that owner. Exact convenience APIs and result types remain
interface choices.

## Observation and integrity

Public observations may include completeness, canonicality, recovered-content
presence and node/category counts. Unless a value is already held for another
essential purpose, these observations are computed on demand rather than kept
coherent through every mutation.

A live document is **complete** when no empty value is reachable from the root.
It is **canonical** when no recovered array is reachable from the root.
Detached subtrees do not affect either observation. Both properties are
descriptive: they do not substitute for integrity and do not by themselves
prohibit mutation, baking, promotion or writing.

The document must retain allocation accounting needed by the framework,
including memory token, allocation count and allocation size totals. This is
separate from tree, content and string-reference accounting.

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

Before emission, a live-tree crawl may build an externally owned analysis
structure using framework allocation. That analysis may contain reachable
value counts, recovered-content presence, per-string reference counts,
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
- recovered-array anonymity rule;
- names, strings, ordering, uniqueness, bounds, terminators and encoding;
- payload/type agreement and numeric metadata.

Public binding of arbitrary bytes performs full validation. A baked view is
ready only after that validation succeeds; there is no public unchecked view.

The replacement byte format is intentionally incompatible with archived
formats. Once its version is selected, validators must reject unsupported
versions rather than infer or silently migrate them.

## Promotion

Promotion constructs a new compact live document from an explicitly validated
baked view. It creates fresh live keys and string IDs while preserving order,
names, payload kinds, numeric intent, string contents and recovered arrays.

A failed promotion must not publish a partially constructed destination as a
coherent live document. It need not preserve unused capacity or allocation
history.

## Serialization-facing requirements

Later writers must preserve these semantics:

- Morphic output retains integer domain, notation and prefix intent.
- Strict JSON emits the exact decimal value across the full `uint64_t` range.
- Finite floats use shortest-round-trip output, retain `-0.0` and contain a
  decimal point or exponent.
- Names and strings are quoted and optional ASCII-only escaping remains
  available.
- Traversal is iterative.
- Recovery-aware output preserves competitor order.

Canonical modified `C0 80` represents logical U+0000. A writer supports escape
to `\u0000` as the default, rejection, or replacement with a caller-supplied
valid nonzero Unicode scalar. It reports how many occurrences it handled.
Diagnostic recovery envelopes and their import symmetry remain deferred.

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
- diagnostic recovery serialization and malformed-input policy;
- whether modified UTF-8 U+0000 remains the long-term encoding policy;
- live cursors and revisions; and
- the later typed-data and schema architecture.
