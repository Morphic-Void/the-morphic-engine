Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   decorations-extension.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   22 Oct 2026

# Type-schema decoration extension

## Agreed scope

This contract was agreed and recorded before implementation on 22 October 2026.
Decorations describe type-schema declarations and declaration occurrences.
Metadata varying between instances or bulk records remains application data.

Accept an optional `decorations` object on named structures, enumerations and
bit structures, structure members, bit-structure fields, and each fixed-array
descriptor. Nested arrays have independent decoration objects. Built-in types
have no authored declaration to decorate. Enumeration labels, sections, resource
interfaces, instance/bulk entries and value initializers are outside scope.

Each decoration has a nonempty, unique string key and a string value. Keys use
the document's string rules rather than C++ identifier rules. Empty values are
valid and distinct from missing keys. Empty objects are valid. Key/value
comparison is exact and case-sensitive, without trimming, case folding or
Unicode normalization. Prefixes are consumer conventions; Core registers no
vocabulary and preserves unknown keys. Non-string values are rejected.

Decorations have no implicit inheritance, propagation or override semantics.
A member and its referenced type may independently carry the same key. Core
does not interpret those relationships. Decorations never change layout,
defaults, construction or the validation of physical values.

## Preservation, merging and compatibility

Copying, promotion, baking and canonical schema output preserve decorations and
their attachment locations. Canonical output does not synthesize empty objects
or interpret strings. Schema-bearing preparation, composition and persistence
preserve the retained schema's decorations; data-only output acquires none.
Reconstructing values from bytes cannot synthesize decorations.

At corresponding attachment points, merging retains keys present on one side,
collapses equal strings for the same key, and rejects different strings for the
same key with locations in both original sources. No implicit precedence or
concatenation is introduced. Failure preserves inputs and destination. Key order
has no semantic significance; output uses existing deterministic merge ordering.

Representation compatibility, checked storage switching and exact binary copies
ignore decorations. Strict definition comparison includes decoration maps at
every supported attachment point, ignoring key order and treating an omitted
object as equivalent to an empty object. Merging continues to check physical
representation before combining declaration content. Consumers perform any
additional application-specific semantic checks.

## Implementation and verification

Extend grammar validation and provide allocation-free read-only lookup and
enumeration, including source handles for diagnostics. Missing values remain
distinguishable from empty strings. Observations borrow the immutable source
document and follow existing schema lifetimes. Reuse document storage; no new
baked encoding, physical type categories or larger layout records are planned.

Audit live/baked schema lifecycles, canonical output, schema-bearing composition
and persistence. Reuse document merging where it already implements the agreed
rules. Extend strict definition comparison while retaining representation rules.
Generated C++ remains unchanged: no decoration attributes, comments, naming or
layout effects. Unknown grammar properties outside this facility remain errors.

Test all attachment categories, nested arrays, exact string behaviour, malformed
input, missing/empty distinctions, query lifetime/error conventions, preservation,
definition/representation comparison, merge union/conflicts and allocation-failure
atomicity. Verify existing schemas and generated C++ remain unaffected. Update
the specification, runtime guide, persistence guidance and validation record.

Sections, resource interfaces, per-instance metadata, annotation inheritance,
structured decoration values, shader semantics and C++ decoration translation
are explicitly deferred. Implementation is authorised; committing is not.

## Implementation notes

`SDecoration` exposes borrowed key/value strings and the source string node.
`find_decoration`, `first_decoration` and `next_decoration` read existing document
storage without allocating or adding fields to layout records. The resolver
validates maps at the supported descriptor objects; duplicate object keys remain
subject to the document model's existing uniqueness and parser-admission rules.

The existing document copier and transactional merger already preserve string
maps and report conflicting original string nodes. No changes to baked encoding,
merging, C++ generation or schema copying were needed.

Remapping previously selected strict definition comparison to retain default and
internal-marker checks. It now selects an explicit remapping comparison policy
which preserves those checks while ignoring decorations. Strict definition
comparison includes decoration maps; representation comparison ignores them.

The lifecycle audit confirmed that composition and whole-document baking retain
selected types, whereas role-specific data promotion, preparation and persistence
select only their data domain. Those existing boundaries remain unchanged; the
separately associated schema retains its decorations.

## Validation result, 22 October 2026

The ordinary `MorphicTests -t1` suite passed in Debug and Release on x64 and
Win32, with 91,774 schema checks and zero schema failures in each configuration.
Final test tags were `decorations-final-dbg64`, `decorations-final-dbg32`,
`decorations-final-rel64` and `decorations-final-rel32`.

All nine generated C++ headers matched the previously compiled x86/x64 layout
fixtures byte-for-byte. A separate regression compares decorated and undecorated
versions of the same schema and requires identical generated C++. Resolved-record
size checks remain unchanged. Policy validation reported zero errors and warnings
with the existing intentional suppression; line-ending and diff-whitespace checks
passed. The initial persistence fixture was corrected to respect the existing
prepared-composition versus locator-bearing materialisation boundary.
