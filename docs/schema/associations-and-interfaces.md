Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   associations-and-interfaces.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   23 Oct 2026

# Associations and interfaces

## Agreed scope

Recorded before implementation on 23 October 2026. Add `associations` and
`interfaces` declaration categories inside the existing `types` section. They
share the existing declaration namespace and identifier rules with physical
types. They describe checked non-storage relationships: no embedded value,
pointer, handle, ownership, runtime object reference or asset identity is implied.
Core introduces no rendering or compiler vocabulary.

An association requires `target`, either a declaration-name string or explicit
`null`, and optionally accepts `decorations`. Missing targets are errors; null
means deliberately targetless. Targets may be built-ins, named physical types,
associations or interfaces. Inline array descriptors are excluded. Expose the
immediate target without flattening chains or treating associations as aliases.

An interface requires an ordered `members` list and optionally accepts
`decorations`. Use the existing array of singleton named descriptors convention.
Member names are unique within an interface. Each member requires a `type` name
and optionally accepts positive fixed `count` and `decorations`. Omitted count
means one; the initial range is 1 through UINT32_MAX. Members may reference the
same declaration categories as association targets, including nested interfaces.
Empty interfaces are valid. Count describes one-dimensional occurrence
multiplicity, without physical size, offset, stride or alignment, and does not
expand resolved records. Multidimensional and runtime multiplicity are deferred.

Decorations retain the existing string-map rules at each declaration and interface
member. No inheritance, propagation, override or automatic binding arithmetic is
introduced. Consumers interpret their own vocabulary and validate semantic rules.

## Resolution and physical boundaries

All references must resolve within the supplied schema. Forward references and
shared dependencies are valid. Association-target and interface-member edges are
checked together for cycles, with existing physical dependency constraints still
enforced. A chain may terminate at a targetless declaration or reach an interface;
it need not end in storage. Diagnostics identify the failing reference and related
declaration where applicable.

Add general declaration lookup/enumeration and dedicated association, interface
and interface-member observations. Preserve existing physical type lookup,
definition enumeration and layout queries. Non-storage declarations must fail
physical queries rather than resemble zero-byte physical structures. Physical
members and array elements cannot reference them. Defaults, instance/bulk data
construction, remapping and storage clearing must reject non-storage inputs.
Existing lifetime, checked-index and allocation-free observation conventions apply.

## Comparison, merging and preservation

Declaration compatibility compares category and identity, immediate target
presence/identity, ordered interface member names, referenced identities and
effective counts. Follow dependencies to validate their compatibility. Equal byte
layouts do not equate differently named targets. Strict definition comparison
also includes decorations and the existing physical default/internal checks.
Physical representation/remapping policies remain restricted to physical types.

Schema merge dispatches to physical or declaration compatibility as appropriate.
Matching interfaces must have the same ordered member list and counts; merging
does not extend interfaces. Matching associations must agree on target presence
and immediate identity. Decoration maps retain existing union/equal/conflict rules.
Each input must independently resolve before merging, and the result must resolve
before publication. Cross-document references and fragment assembly are deferred.

Copying, promotion, baking, canonical schema output and schema-bearing composition
preserve the new declarations and decorations. Data-only operations remain
data-only. No baked document encoding or new persistence envelope is introduced.
Older schema readers reject the new categories through their strict grammar.

C++ generation continues emitting every physical definition. It emits deterministic
comments describing associations, targets, interfaces and member counts; no C++
storage declarations or aliases represent non-storage declarations. Decorations
remain uninterpreted and do not become C++ attributes or comments.

## Implementation and validation

Extend resolver grammar, separate declaration records/observations, reference
mapping and graph validation; extend decoration queries to the new owners. Add
declaration comparison and integrate schema merging without weakening physical
comparison. Audit lifecycle, data boundaries, move/clear and C++ generation.
Preserve physical record sizes and behaviour for existing schemas.

Test forward/shared/nested references, targetless declarations, duplicate names,
invalid targets/counts/properties, cycles, physical-operation rejection, exact
comparison, transactional merges, source diagnostics, lifecycle preservation,
allocation failures and generated C++ comments. Run existing regression suites and
generated-layout validation appropriate to the changes.

Matrices, resource interfaces with domain semantics, associations attached to
physical fields, runtime bindings, external imports, fragment assembly, structured
decorations, multidimensional counts and selective/alias C++ generation are outside
this increment. Implementation is authorised; committing is not.

## Implementation notes

The general catalogue is exposed by `find_declaration`, `declaration_count/at`
and `declaration`. It enumerates existing authored physical definitions first,
then non-storage declarations, retaining source order within each partition.
Built-ins are queryable by name and excluded from authored enumeration. Existing
physical definition indices and enumeration keep their previous meaning.

`association`, `interface_info`, `interface_member_at`, `find_interface_member`
and `interface_member` provide dedicated observations. Separate record tables
keep non-storage declarations out of physical layout/default queries. Interface
member counts are stored once, without expanding occurrences. Decoration queries
reuse the existing document nodes and source handles for the new owners.

`CDeclarationCompatibility` shares operation-local graph traversal with physical
comparison while exposing a separate entry point and policy. Schema merge uses
this declaration comparison; physical comparisons and remapping retain their
existing entry points. Canonical output formats authored interface counts and
retains omitted counts. Schema wrappers and composition already preserve the
complete types section, so their existing copying machinery required no changes.

Generated comments show immediate association targets (including null), interface
member names/references and effective counts. They preserve chains for inspection
and emit no interpreted decoration text. Existing physical output remains unchanged.

## Validation result, 23 October 2026

The ordinary `MorphicTests -t1` suite passed in Debug and Release on x64 and
Win32, with 92,756 schema checks and zero schema failures in each configuration.
Final tags were `associations-final-dbg64`, `associations-final-dbg32`,
`associations-final-rel64` and `associations-final-rel32`; all processes exited zero.

Mixed physical/non-storage and non-storage-only generated C++ fixtures compiled
as C++17 with `/W4 /WX` for both x86 and x64. All nine existing physical generated
headers matched the previously validated decoration fixtures byte-for-byte.
Physical record-size assertions remain unchanged. Policy validation reported zero
errors and warnings with the existing intentional suppression; line-ending and
diff-whitespace checks passed.

The first comparison run exposed a test mutation that did not remove the intended
interface member because of a line break in the fixture. The fixture was corrected
and mutation checks added; the final tests verify that member removal is rejected.

## Review follow-up

The correctness review found no further behavioural defects. The duplicate built-in
name list in C++ comment generation was replaced by `schema::primitive_name`, backed
by the resolver's authoritative `k_primitives` table. It returns a static borrowed
view without allocation; none and invalid enum values return an empty view.

The x64 Release ordinary suite was rebuilt and rerun as
`associations-review-rel64`: 92,769 schema checks passed with zero failures,
including all primitive spellings and invalid-enum boundaries. All eleven generated
headers matched the pre-refactor fixtures byte-for-byte. The previously passing
four-configuration matrix was not repeated for this small accessor refactor.
