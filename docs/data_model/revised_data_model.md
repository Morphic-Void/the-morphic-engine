Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   revised_data_model.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   8 Sep 2026

# Data-model semantic specification

## Status and scope

This document is the normative semantic specification for the incompatible
replacement of TheMorphicEngine's first live and baked document model. It
defines the abstract model, public operations, integrity boundaries and
ownership rules. Requirements expressed with **must**, **must not** and
**only** are normative.

This records the implemented baseline. Parser observations, caller strictness,
reporting and the public ownership-transfer boundary are being reconsidered
under the [consolidation plan](../backlog/consolidation_pass.md). That plan
identifies work to agree and implement; it does not silently change the current
behaviour specified here. Update the affected contracts with the refactor.

Implementation rationale belongs in
[design notes](data_model_design_notes.md). Completed implementation history
belongs in [completed milestones](../project/completed_milestones.md).

The normative physical baked layout is defined separately in
[baked-document format](baked_document_format.md).

The v1 design and implementation are obsolete and are preserved in Git history
and the local non-redistributed archive. C++ signatures and live-record packing
remain implementation choices unless explicitly settled here.

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

Every named value is an object entry. If it is not already within an object,
an anonymous containing object is implied. This rule is independent of payload
type: it applies equally to numbers, strings, nulls, Booleans, objects, arrays,
recovered arrays and empty placeholders. An object-valued payload does not
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

The root is not replaced, detached or destroyed by ordinary callers. Reset
owns its lifecycle. Initialization must establish the complete root or leave
the document uninitialized.

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

The live document exposes each string domain's ID at a requested lexical rank.
Rank zero is the canonical empty ID; an unavailable rank is invalid. These
observations permit deterministic public-only baking and serialization without
exposing the underlying stable stores.

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

`CBakedDocumentBlock::can_reattribute_to` and `reattribute` use the existing
framework memory-context rules; a null argument selects the ambient context.
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

The checked view exposes value classification, names and both string domains,
typed scalar payloads and integer metadata, parent and sibling relationships,
child ranges, ordinal array access and object-child lookup. Canonicality and
recovered-content presence are derived observations rather than stored summary
state.

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

## Text ingestion and parsing

The parser is always relaxed. Its stages are the linter, a structural-integrity
check, and relaxed document parsing. There is no separate strict parser.
Reports identify the relaxed features required for acceptance and the
Morphic-specific features interpreted, separately from ingestion transformations.
Strict JSON syntax may itself contain a Morphic recovery representation.

The linter converts recoverable CP1252 input to UTF-8 and reports the resulting
encoding and transformations. A parser consumes successfully produced UTF-8;
it must not decode CP1252 itself or silently accept unconverted CP1252 bytes.

Embedded literal zero bytes are accepted content, not unrecoverable encoding
errors. The linter explicitly counts them separately from source terminators
and modified-NUL sequences. Its length-bounded UTF-8 output may contain literal
U+0000; accepted `C0 80` input is normalized to that scalar and reported.
Document ingestion disables global newline rewriting to preserve quoted
content. Source transformations and parser locations must identify their
coordinate space rather than imply unchanged source offsets after conversion.

Every logical NUL entering a live name or string value, whether from literal
source content or an escape such as `\u0000`, uses `C0 80` through ordinary
string admission. Physical terminal NULs in live and baked strings are
unchanged. The writer emits logical NULs as `\u0000` and counts occurrences.

The structural check establishes that the text is well formed under the
accepted grammar: quoted and escaped spans, valid token spellings, matching
delimiters, and valid placement of names, values and separators. It tracks
nesting and may provide proportionate capacity estimates without constructing
a document or a full token tree. Where practical, it shares the functions
interpreting quotes, escapes, comments, token boundaries and relaxed syntax
with parsing. Traversal and parsing are iterative and use framework allocation.

Success means that the text is structurally valid and its data has parseable
syntax; it does not guarantee that document parsing will succeed. The check
assumes numeric values can be represented, object-name collisions can be
handled, and document-construction allocations will succeed. It does not
convert numbers to enforce available numeric ranges, resolve collisions,
preflight construction allocations, or enforce other construction policies.
For example, an incomplete exponent is a syntax error, while a well-spelled
number outside the available numeric range is not a structural flaw.
Numeric range policy, collision handling and reserved-wrapper interpretation
belong to parsing. Capacity estimates are hints, not feasibility guarantees.
If the structural check itself cannot complete because of a resource limit
or allocation failure, it reports that failure separately from malformed text.

### Initial shared text grammar

`document_text_lex.hpp/.cpp` supplies bounded tokens and escape decoding for
the structural check and subsequent parser. `document_structure.hpp/.cpp`
checks the grammar with an iterative frame vector and no document or token
tree. Input is the linter's successful UTF-8 output with its explicit logical
length; encoding validation/conversion remains at the linter boundary.
The check and scanner consume `CStringView` with an explicit length, preserving
embedded NULs. An absent string is invalid input; a present zero-length string
is valid empty text. Construct the view directly from the linter's output
pointer and logical byte size so this distinction is retained.

The root is an explicit object or an unbraced member list such as
`name: 1, enabled: true`. Empty or comment-only text is an implicit empty
object. A root array or scalar is not part of this initial grammar. Names in
arrays require object wrappers; `[name: 1]` is invalid.

The initial syntax accepts:

- JSON object/array/member placement, case-sensitive `true`, `false` and `null`,
  and JSON decimal number spelling, with no range conversion. Decimal fractions
  need digits on both sides of the point; exponents need digits; multi-digit
  decimal integers cannot start with zero.
- `//` comments through CR, LF or EOF, and non-nesting `/* ... */` comments.
  Outside quoted spans, whitespace is space, tab, CR or LF.
- Single-quoted strings as well as double-quoted strings. Both support JSON
  escapes and paired UTF-16 surrogate escapes. Single-quoted strings additionally
  accept `\'`. Unknown escapes and unpaired surrogate escapes are syntax errors.
  Literal controls, including NUL and line breaks, are accepted quoted content
  and reported as a relaxation; delimiters/comment markers inside quotes are
  content. Quoted Unicode content is preserved.
- ASCII identifier-style unquoted names: `[A-Za-z_$][A-Za-z0-9_$]*`, including
  the literal words `true`, `false` and `null` in name position. Other names
  require quotes; bare identifier string values are not accepted.
- One trailing comma after a member or array element, including at the end
  of an unbraced root. Missing values and repeated commas are errors.
- Morphic leading `+`, hexadecimal `0x`/`0X` or `#`, and binary `0b`/`0B` integer
  spellings, with an optional sign before the prefix. Base-prefixed numbers
  require digits and do not have fractions/exponents. `NaN`, `Infinity`, digit
  separators and additional numeric spellings are not in this grammar.

Successful structural reports distinguish required syntax relaxations from
Morphic numeric spellings. Recovery wrappers and escaped reserved names are
ordinary syntax at this stage; only parsing can report their interpretation.
Estimates count syntactic values, objects, arrays, named entries, raw name/string
token bytes, and maximum container depth (root is depth one). They describe
occurrences before semantic normalization, not exact construction requirements.
Failures clear estimates and feature bits and report a status plus a zero-based
byte offset in the linter's output; EOF errors use its logical byte size.

### Live construction and interpretation

`document_parser::parse` in `document_parser.hpp/.cpp` takes a bounded
`CStringView` of successfully linted UTF-8 and a live destination. It performs
the shared structural check, then constructs privately through public live
operations and publishes by move only on success. Failure leaves the existing
destination unchanged. Present empty input constructs an empty root object;
absent input fails. Input may refer to the destination's existing string
storage, which remains alive until publication.

Document ingestion calls `text_linter::lint(source, 0u)` to preserve quoted
newlines, checks linter success, then constructs the view from `output.data()`
and `report.logical_text_byte_size`. Encoding conversion and its transformation
report remain at that boundary. The parser decodes quoted escapes, including
surrogate pairs and logical NULs, and uses ordinary live string admission for
the established modified-NUL storage form.

Integer tokens without a sign construct unsigned values; an explicit `+` or
`-` selects the signed domain. Magnitude must fit that domain's 64-bit range.
The parser retains decimal/hexadecimal/binary notation and the alternate `#`
prefix, and selects the smallest representable integer width. Floating tokens
construct finite binary64 values, preserving negative zero. Decimal conversion
rounds to binary64; overflow and nonzero underflow to zero are construction
errors. Neither numeric range failure nor an empty property name is a syntax
error. Empty names cannot represent object entries in the current live model
and return a specific construction status.

`CDocumentParseReport` retains the structural report, including syntax
relaxations and numeric-extension bits, even if construction subsequently
fails. Its failure offset identifies a token in the linter's UTF-8 output.
Structural resource failures retain their detailed structural status; known
parser scratch failures have allocation/storage statuses. A rejected live
creation has a general construction-failure status because the existing live
API does not distinguish its underlying allocation and storage causes.

The parser implements recovery decoding, reversible reserved-name unescaping,
singleton normalization and ordered duplicate recovery as specified below.
`CDocumentParseReport::interpretations` counts decoded recovery wrappers,
unescaped reserved data names, recovered duplicate members and removed singleton
objects by source occurrence. These counts describe a successfully published
document and are cleared on failure. Numeric spelling observations remain in
the separate structural report, even on a later construction failure.

Duplicate object members are recovered through ordinary public payload and
attachment operations. First competitors retain encounter order. A later
collision appends its anonymous payload to an existing recovered array,
including one restored from text. A recovered-array candidate remains one
nested competitor rather than having its children flattened into the receiver.

## Serialization-facing requirements

Writers consume only the checked baked interface and provide two modes:

- Morphic output retains integer domain, notation and prefix intent and
  preserves recovered-array identity and contents through writing and parsing.
- Strict output emits strict JSON where possible, including the exact decimal
  value across the full `uint64_t` range. It reports normalization of Morphic
  features and does not promise to round-trip every feature, such as explicit
  positive signed-integer intent.

Both modes quote names and strings. ASCII-only escaping is an independent
option for either mode and escapes every non-ASCII scalar, using surrogate
pairs where necessary. Finite floats use shortest-round-trip output, retain
`-0.0` and contain a decimal point or exponent. Writing is iterative.

### Named entries and anonymous objects

A named value outside an object is written inside an anonymous object wrapper.
For example, named integer, string, object and array payloads in an array write
as `[{"n": +1}, {"s": "text"}, {"o": {}}, {"a": []}]` in Morphic mode.
The same rule applies to every other payload type. Within an object, its
containing braces already supply the required context.

When an anonymous ordinary object directly within an ordinary array contains
exactly one named member after duplicate recovery, parsing removes the
redundant wrapper and retains the named child with its complete payload.
The rule does not depend on that payload's type. Empty and multi-member objects
remain objects; the implicit root remains an object. Recovered-array
competitors retain anonymous wrappers where needed to satisfy their naming
rule. Reserved recovery wrappers are decoded before ordinary unwrapping.

There is no additional semantic distinction for an intentional anonymous
singleton object requiring a preservation tag. Text round trips compare this
normalized semantic form, not redundant wrapper nodes, original live keys,
detached content or live placeholders already baked as null.

### Recovered-array text representation

The reserved property name is `$morphic`. A version-1 recovered payload writes
as the following object, with any number of anonymous competitors in `values`:

```json
{"$morphic":{"v":1,"type":"recovered-array","values":[]}}
```

The outer object has exactly one member. Control fields are unique and contain
exactly integer version `1`, string type `recovered-array`, and an array of
values; field order is insignificant. Unsupported versions, extra or duplicate
control fields and malformed reserved wrappers are explicit errors. Ordinary
duplicate recovery does not repair protocol metadata. Competitor data uses
the relaxed document grammar. The transport array is interpreted as recovered
content, preserving anonymous competitors, their order and nested recovery.
Empty, singleton and multi-value recovered arrays all round-trip.

Protocol fields are checked after string-escape decoding. Version one accepts
any supported integer spelling with value one, including `+1`, `0x1` and `#1`;
floating `1.0` is not an integer version. Unknown integer versions and string
types have distinct unsupported-version/type statuses. Invalid field types,
missing/extra/duplicate fields and invalid outer members report a malformed
recovery wrapper. The root must remain the document's implicit object, so a
reserved recovery wrapper at the root reports `invalid_root_value`.
The structural check accepts these well-formed spellings independently of
their interpretation.

Ordinary data names consisting of one or more dollar signs followed by
`morphic` are escaped by adding one dollar sign when writing: `$morphic`
becomes `$$morphic`, and `$$morphic` becomes `$$$morphic`. Other names are
unchanged. After JSON string decoding, the parser recognizes the unescaped
reserved name before removing one dollar from escaped data names. An
unescaped reserved member must form the complete wrapper above. This rule
distinguishes user-authored lookalikes and named array entries from protocol
objects without changing logical names in live or baked storage.

Both modes may emit this representation and name escaping: its syntax is
strict JSON. Recovery identity is never silently reduced to an ordinary array.

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
- the exact inventory of additional relaxed syntax and diagnostic presentation;
- live cursors and revisions; and
- the later typed-data and schema architecture.
