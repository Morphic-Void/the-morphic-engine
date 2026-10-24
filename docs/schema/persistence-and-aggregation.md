Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   persistence-and-aggregation.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   20 Oct 2026

# Schema persistence and aggregation


## Status and scope

Persistence uses the common [.bsd contract](common-binary-block.md), agreed on
24 October 2026. It replaces the former separate binary schema formats outright.
Type/data merging, preparation and document composition retain the contracts
below. See also the [runtime API](runtime-api.md).

## Expected use and reasoning

Definitions can be shared externally or embedded with instances and bulk data.
The same common storage representation permits any combination of these sections.
A bundle packages related files without establishing their schema associations.

## Normal persistence units

CBakedDataBlock owns or borrows the complete document and its independently
available instance/bulk binaries. CBakedSchema, CBakedInstances and CBakedBulkData
remain separate specialised interfaces. CBakedDocumentBlock remains a lower-level
allocation owner, not a separate schema persistence format.

## Agreed version 1 persistence format

The extension is .bsd. Required root identification is the string
fileType: "morphic-schema-data" and unsigned version: 1. The MBD2 encoding and
reserved-zero header fields are unchanged.

Optional object sections are types, instances and data. Optional unsigned
instancePayloadSize and bulkPayloadSize declare exact supplied lengths.
Omission means unavailable; explicit zero means supplied zero bytes. Each length
requires its corresponding section; empty sections cannot claim nonzero bytes.
An optional Boolean stripped marker retains its meaning for both data domains.
Unknown properties, incorrect types and unsupported versions are rejected.

The file contains its document, instance binary and bulk binary in that order.
The base is 128-byte aligned; align the cursor only before each nonempty binary.
Container padding must be zero. Empty/unavailable ranges add no padding, and
there is no final alignment tail. Locators remain relative to their own binary.
Validate checked arithmetic, exact extent and the 2 GiB complete-file ceiling.
Internal payload padding/unused bits retain existing semantics.

## Caller-supplied schema

Storage validation checks framing only. Typed attachment validates definitions,
locators and data. Aggregate materialisation uses embedded types when present;
otherwise it uses an external schema when data requires type resolution.
There is no implicit merge of embedded and external definitions. Empty ordinary
sections can materialise without a schema.

## Ownership and asset backing

The common owner is noncopyable/nonmovable. Local backing owns its allocation;
asset backing borrows a caller-protected range and acquires no reference.
Borrowers must finish before clear, destruction, release or replacement.
Schema-binding guards continue protecting typed access.

Constructing missing binary returns separate working storage and never modifies
the source block. Explicit assembly publishes an independent replacement;
interfaces are not silently rebound.

## Host loading, admission and saving

Use the generic baked asset format and request 128-byte alignment. Host loading,
owning admission and binary saving preserve the complete logical file, excluding
allocation capacity and enclosing bundle padding. JSON saving serialises only
the document; declared supplied binaries must still be provided separately.
Asset and bundle leases remain caller responsibilities. Host I/O does not
interpret schemas or rendering catalogues.

## Block interfaces and lifecycle contracts

Include schema/baked_data_block.hpp. Observations are document(), has_types(),
has_instances(), has_bulk_data(), instance_binary_available(),
bulk_binary_available(), instance_payload_view(), bulk_payload_view(), backing(),
loaded_ready(), asset_id() and file_bytes(). An empty view alone cannot distinguish
unavailable from supplied-zero binary.

adopt_local and attach_asset take SDataBlockDiagnostic, validate before publication
and require an empty destination. release_local transfers the complete allocation
into an empty generic owner; clear releases/forgets backing.

CBakedSchema::set_block borrows embedded types. The two data roles expose
attach_block(block, schema, returned_owner, compare_embedded, diagnostic), with
live/baked schema overloads. Supplied bytes are borrowed. Missing bytes are
constructed into the unallocated working owner; the role retains a private
locator-bearing document while the original block remains unchanged. Existing
lower-level document/payload interfaces remain available.

## Construction interfaces and completed storage

SDataBlockPayloads contains optional pointers to instance and bulk byte views.
Null means unavailable; a pointer to an empty view means supplied zero bytes.

| Operation | Contract |
| --- | --- |
| assemble(document, payloads, diagnostic) | Schema-independent copying/framing with explicit availability. |
| materialise(document, form, diagnostic, payloads) | Resolve embedded types, validate supplied binaries and construct missing ones. |
| materialise(document, schema, form, diagnostic, payloads) | External-schema fallback only when embedded types are absent. |
| strip_to(destination, diagnostic) | Remove retained values using embedded types, constructing missing binaries first or failing. |
| strip_to(destination, schema, diagnostic) | External-schema fallback for stripping. |
| Live instance/bulk bake | Pack directly into the corresponding common-block binary range. |
| Live schema bake | Produce a types-only common block and borrowed schema interface. |

Assembly/materialisation accept live or baked descriptions. All construction
stages privately, requires an empty destination and preserves inputs on failure.
compose_document output can be materialised directly; missing locators are
added only in private staging. Omitted domains remain absent; explicit empty
domains produce supplied-zero binaries. Existing valid offsets are honoured.

Declared supplied lengths require their explicit bytes, including zero.
Stripped descriptions without binary fail even for empty sections. Other missing
binaries can be constructed from sufficient descriptions, defaults and inheritance.

## Live use, binary construction and retained document content

Output forms govern descriptions independently of binary availability. External
output retains instance declarations and omits bulk arrays. Stripped output
omits both and sets the common marker. Supplied stripped content can be converted
to retained forms by reconstructing descriptions through existing preparation.

Stripping supplied binaries preserves every byte and retains the existing policy
of structural validation without comparing stale retained declarations. New
construction validates values before dropping descriptions. Live packing preserves
complete records and zeroes introduced gaps. Assembly preserves whole binaries.

## Diagnostics and failure reporting

Envelope/assembly operations use SDataBlockDiagnostic: stage, reason and optional
original live node, baked value or complete-file byte location.
SDataBlockBuildDiagnostic adds schema, instances and bulk_data detail.
Single-domain live bake retains its existing specialised diagnostic type.

Handles borrow caller sources; temporary handles are mapped back or cleared.
Generic Boolean translation/validation failures do not claim more precise
resource diagnoses than the underlying operation supplies. Success clears
diagnostics. Failure preserves source and destination, including failures in the
second domain after constructing the first.

## Agreed bulk data terminology and standardisation pass

The completed stage 1 implementation standardised the supporting types and domain prose
alongside the existing `CBakedBulkData`, `CLiveBulkData`, `baked_bulk_data.hpp` and
`live_bulk_data.hpp` names. Completion and validation are recorded in the
[persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence).

The agreed convention is "bulk data" in domain prose and
`BulkData`/`bulk_data` in domain-specific public identifiers, matching the main
roles and `CBakedDataBlock` and `SBulkDataBlockDiagnostic`. The standardisation
pass covered supporting types, domain-specific helpers, call sites, tests and
maintained documentation. Persistence interfaces use the agreed naming.

The public supporting-type changes are recorded here as a migration reference;
the previous names are not compatibility aliases:

| Previous name | Standardised name |
| --- | --- |
| `SBulkDiagnostic` | `SBulkDataDiagnostic` |
| `EBulkLoadReason` | `EBulkDataLoadReason` |
| `SBulkEntryView` | `SBulkDataEntryView` |
| `SMutableBulkEntryView` | `SMutableBulkDataEntryView` |
| `CBulkHandle` | `CBulkDataHandle` |
| `CBulkDocumentQuery` | `CBulkDataDocumentQuery` |

The pass updated domain-specific names and references together, including headings
and prose that used "bulk" as shorthand for the schema domain. Applicable builds,
schema tests and stale-name searches validated the changes. This mechanical
stage preserved data semantics and introduced no compatibility aliases.

The diagnostic contract above reuses `SBulkDataDiagnostic`. Generic uses of
"bulk" for batched or contiguous operations
need not become "bulk data". The persisted `data` section is unchanged; common .bsd storage now replaces the specialised envelope.

## Separate merge and aggregation operations

Merging responsibilities belong to the individual domains.
The following operations are conceptually separate and implemented. Type-schema
and prepared data merging share internal document construction while retaining
their domain-specific validation. Boundary preparation and composition are also
implemented as separate document-only operations.

| Operation | Responsibility |
| --- | --- |
| Definition merging | Reconcile independently produced type definitions and their dependencies. |
| Instance aggregation | Merge instance declarations and their specialisation hierarchy in document form. |
| Bulk data aggregation | Merge bulk data groups and complete record descriptions in document form. |

### Agreed non-destructive merge rules

The merge core combines document content. A field present in only one source is
included in the result. Content supplied by both sources must agree under the
following rules or the entire merge fails. There is no automatic renaming or
first/last-source precedence for conflicting values. Schema-aware structural
checks still apply; generic object union must not change the layout or
interpretation of a same-name type.

| Content | Merge rule |
| --- | --- |
| Objects | Combine fields recursively, subject to the schema type rules below. |
| Scalar values | Require matching values under the document scalar equality rules. |
| Arrays other than the outer bulk data record sequence | Require equal lengths and recursively merge corresponding elements. No prefix extension. |
| Outer bulk data record sequence | Require the shorter sequence to match the longer prefix under the document value rules; retain the longer sequence. |

The equal-length rule includes type-definition arrays, defaults supplied by both
sources, instance declaration arrays, positional structure descriptions and
supported auxiliary-metadata arrays. An array field supplied by only one source
survives. Arrays are not matched by embedded names, treated as sets or implicitly
concatenated. A valid short instance initializer remains a short initializer;
its default/inheritance completion does not relax the merge length rule when
both declarations explicitly supply that array.

### Type-schema merging and defaults

Type-schema merging is implemented. Distinct type names are
included together. Same-name types must retain compatible layout and value
interpretation, including their referenced definitions. Members, member order,
array extents, enum definitions and bit-field interpretations cannot be silently
extended or changed by ordinary missing-field merging.

Each input must independently resolve as a valid type schema before merging.
This includes association targets and interface-member references: a declaration
in the other input cannot satisfy a missing reference. Cross-document references
and fragment assembly are deferred. One input cannot repair invalid declarations
in the other. The complete merged result must also resolve before publication.

Associations and interfaces are retained in the ordinary types section during
copying, promotion, baking, canonical output and composition. Merge compatibility
compares immediate target identity/presence, ordered interface members, effective
counts and the compatible definitions of dependencies. Differently named targets
are not interchangeable even when their layouts match. An interface cannot gain
members through merging. Decorations retain the existing content merge rules.

Physical compatibility is based on resolved layout and interpretation. Natural layout
and explicitly supplied layout metadata are compatible when they resolve to
the same layout. Explicit alignment that changes the effective layout conflicts;
the absent-default rule below does not make omitted layout a wildcard. Member
order, enum label order and referenced type identity remain significant, even
when a different declaration could occupy the same number of bytes.

Merge the original document declarations and their presentation, using resolved
information only to check compatibility. Do not first regenerate declarations
with `CResolvedSchema::prepare_output`: its normalisation can change presentation
and add layout metadata. Type references remain their declared type-name strings.
Resolving the completed result establishes its own local identities and indexes;
no persistent source-index translation table is required.

Explicit defaults use the following merge policy:

| Source defaults | Result |
| --- | --- |
| Neither source supplies a default | Leave the default unspecified. |
| Only one source supplies a default | Retain that explicit default and its presentation. |
| Both supply matching defaults | Merge them using the existing scalar, array and presentation rules. |
| Both supply conflicting defaults | Reject the merge. |

A missing default means no preference for this merge. It is not an explicit zero,
null or empty array. Runtime construction still gives an omitted default its
ordinary effective meaning: zero, the first enum value, or nested type defaults.
The merge must preserve whether a default was supplied until this policy has
been applied; comparing only effective defaults would wrongly reject an absent
zero default against an explicitly supplied nonzero default. An explicit null
is not absence and remains invalid wherever the schema disallows it.

The rule applies at each supported default declaration, including members and
bit fields. When only one source supplies an array default, retain that supplied
array. When both supply arrays, the existing equal-length and recursive-value
rules apply. Omitted tails are completed by normal construction after merging;
merging does not expand an array default to its declared extent.

Adopting an explicit default can intentionally change the binary reconstructed
for instances that omit that value. This is distinct from schema switching,
which preserves an already constructed binary package. Conflicting explicit
instance values still fail document merging; an explicit instance declaration
takes precedence over the resulting schema default.

For example, if one schema omits the default for `speed` and the other supplies
`10`, the merged schema supplies `10`. An instance declaration omitting `speed`
then constructs `10`; a declaration explicitly supplying `speed: 0` constructs
zero. Two explicit schema defaults of `0` and `10` conflict.

Presentation-only differences do not establish a type conflict. Preserve and
combine them under the presentation rules below. Existing resolved-type
comparison is a reusable foundation, but definition comparison alone is not
this merge policy: the merge needs supplied-default presence as well as resolved
layout and interpretation.

### Instance and bulk data document merging

Base instances collide by type and name. Merge their declarations under the
document rules and combine child specialisations recursively. Under a matching
parent, distinct specialisation names survive; matching names merge their own
declarations and children. A missing declaration contributes no explicit values.
Absence follows schema defaults for a base and parent inheritance for a
specialisation. It does not demand equality with another source's previously
materialised snapshot.

The resulting instance binary is constructed from the merged declarations and
merged schema, visiting each parent before its specialisations. New defaults or
merged parent declarations may therefore change inherited values. This is
intentional reconstruction, not preservation of source snapshots.

Bulk data entries collide by type and name. Merge supported metadata under the
document rules and apply the prefix rule to their complete record sequences.
Bulk data records must be fully specified; defaults do not fill missing record
values. Prefix matching is not concatenation, and arrays within each record keep
the shape required by the type schema. Instance and bulk data names occupy
separate domains.

Merge corresponding records in the common prefix, including their presentation,
then retain any tail from the longer sequence. Merely selecting the longer input
would lose presentation preferences contributed by the shorter input.

Equivalent typed spellings are not automatically normalised before merging.
For example, a named declaration `{"x": 1}` and a positional declaration `[1]`
conflict when both are supplied at the same location, even if construction would
produce equal values. The existing document kind, scalar and array rules apply;
typed equivalence is not an additional merge predicate.

Source handles, resolved type indices and payload offsets are not destination
identities. Translate references and parents. Storage-derived metadata such as
locators, payload size and record counts must be removed or regenerated at the
boundary rather than treated as conflicting user content. Old locators must
never make reconstructed output appear to borrow discarded source bytes.

### Agreed document scalar equality

Compare stored document values independently of presentation preferences, without
coercion between scalar kinds. Presentation differences do not cause conflicts;
they are combined under the rules in the next section.

| Scalar kind | Equality rule |
| --- | --- |
| Null | Matches null only; an absent field is not an explicit null. |
| Boolean | Require the same boolean value; no equivalence with integers. |
| Integer | Require the same exact mathematical integer, independent of signed/unsigned storage domain and decimal, hexadecimal or binary presentation. |
| Floating point | Require the same stored representation, including the sign of zero; no numerical tolerance. |
| String | Require identical decoded contents, including case and actual whitespace; source escape spelling does not participate. |
| Different scalar kinds | Conflict, even where a conversion could produce equal values. |

For example, signed `+12` and unsigned `12` match, but `-1` and `UINT64_MAX` do
not. Integer comparison must retain full integer precision rather than convert
through floating point. Integer `1`, floating-point `1.0`, string `"1"` and
boolean `true` are distinct. Ignoring document integer storage-domain metadata
does not ignore schema type declarations: `"i32"` and `"u32"` remain different
string values and describe different types.

Document floating-point nodes contain finite values. Schema-aware interpretation
of special-value strings is separate from document scalar equality: `"nan"` and
`"NaN"` conflict as document strings, although typed conversion may recognise both
as NaN. Once binary values are being compared, the agreed NaN equivalence and
permitted-value rules apply.

Object fields are matched by name; different object member order is not a
conflict. Array order remains significant under the agreed array rules. Defaults,
labels, descriptions and other semantic metadata stored as document fields are
ordinary merge content: missing fields survive, and conflicting supplied values
fail. Textual descriptions are content even when they do not affect binary layout.

Type-schema `decorations` are string maps on named types, members/bit fields and
array descriptors. Their keys merge by the same object rules: one-sided keys
survive, equal strings collapse and differing strings conflict with locations in
both sources. There is no inheritance or precedence. Strict definition comparison
includes these maps, whereas representation compatibility and exact remapping
ignore them. Remapping still checks defaults and internal markers as before.

Schema copying, promotion, baking, canonical output and composition preserve
decorations in retained type sections. Role-specific data preparation, promotion
and persistence remain data-only and do not embed their attached schema.
Decorations on instance/bulk entry descriptors and section roots are unsupported;
ordinary application fields named `decorations` retain their existing meaning.

### Agreed presentation merging

Presentation merging preserves likely intent when equal values have different
writing preferences. It does not select between conflicting values. Fields
present in only one source retain their presentation. For duplicate integers:

- Explicit hexadecimal or binary presentation wins over adaptive
  decimal-or-hexadecimal or ordinary decimal presentation.
- Within the same explicit radix, retain the largest minimum digit count.
- Between explicit hexadecimal and binary, compare requested minimum widths in
  bits: hexadecimal digits multiplied by four versus binary digits. The larger
  width wins; binary wins equal widths.
- Adaptive decimal-or-hexadecimal wins over ordinary decimal. Two adaptive
  preferences remain adaptive; preserve the policy rather than replacing it with
  whichever radix displays the current value.
- Use the prefix style belonging to the winning presentation, including when
  the minimum digit count decides the winner. If the presentations otherwise tie
  completely, either source prefix is acceptable.

| Competing presentations | Result |
| --- | --- |
| Decimal and adaptive decimal-or-hexadecimal | Adaptive decimal-or-hexadecimal |
| Adaptive and explicit hexadecimal or binary | The explicit radix and its digit preference |
| 2-digit and 8-digit hexadecimal | 8-digit hexadecimal |
| 4-digit hexadecimal and 16-digit binary | 16-digit binary |
| 8-digit hexadecimal and 16-digit binary | 8-digit hexadecimal |
| 4-digit hexadecimal and 32-digit binary | 32-digit binary |

Decimal digit counts do not enter the bit-width comparison. These preferences
select a minimum display width, never a truncation width; the stored value still
determines any additional digits needed for safe representation. Integer storage
width metadata must remain valid for the value and is distinct from presentation
minimum digits. Adaptive presentation retains the existing writer policy of
decimal within -65535 through +65535 and hexadecimal outside that range.

For equal string values, escaped newline output wins whenever either source
requests escaping. Suppression of newline escaping survives only when both
sources suppress it. The decoded string contents are unchanged; actual newline
content and literal backslash characters are not made equivalent by this rule.

Number form, minimum digits and newline-escaping outcomes are independent of
source order and merge grouping. Prefix choice on a complete presentation tie
is deliberately unspecified. These rules replace source-precedence selection
of presentation and do not grant precedence for semantic conflicts.

### Agreed aggregation ownership and replacement

Merging borrows two explicit sources and constructs independently owned document
output. The destination may be empty or occupied and may itself be explicitly
supplied as a source. An occupied destination does not implicitly contribute its
content: only the two source arguments determine the merge.

| Destination on entry | Successful operation |
| --- | --- |
| Empty | Publish the completed document result. |
| Occupied, not a source | Replace its old content with the merge of the explicit sources. |
| Occupied and explicitly a source | Read its original content, then replace it with the merge. |

Construct and validate the replacement privately. All fallible validation,
translation and allocation finish before destination publication; the final
replacement must not fail. Failure discards temporary state and preserves the
sources and destination, including any associated payloads, bindings, ownership
and existing handles/views. Success invalidates old destination handles/views;
other sources remain unchanged. Peak memory may include the old destination,
converted working inputs and the complete replacement.

The document-only merge result has no data-role binding and borrows no source
storage. A later binary-construction operation attaches a role to the selected
merged schema using the existing contracts. Failure of that separate operation
does not retroactively undo an already completed document merge. If a future
convenience operation combines conversion, merge and construction, it must stage
all requested output before publication; its interface is not yet selected.

Callers keep borrowed documents, any conversion payloads and the schemas used
for conversion/validation usable and unchanged throughout their operations.
They prevent concurrent mutation of sources and concurrent access to the
destination during replacement. This introduces no new synchronisation or
lifetime mechanism.

Host-backed sources remain borrowed. Merging does not overwrite assets or bundle
entries, acquire asset references, send disposal requests, admit output or save
it automatically. Displaced borrowed state follows the existing ownership
contract; callers release old asset retention only after remaining consumers
have finished. All three merge operations use independent live-document staging
and non-failing move replacement. Existing persistence APIs retain their
empty-destination requirements.

### Separate validated schema-switch operation

Implemented on all four live/baked data roles as
`switch_schema(schema, ESchemaSwitchMode::checked/trusted, diagnostic)`, with
live and baked target-schema overloads. Checked switching validates the used
representation and runs shared construction rules against existing bytes.
Trusted switching skips that proof while resolving target type names and indices.
Both preserve documents, handles, indexes, payload bytes/addresses and storage
ownership; failures preserve the original binding. No description replacement,
default-use provenance tracking or runtime-record rebuilding is involved.

Initial `attach_schema()` either validates a supplied complete package or
constructs one from descriptions. There is no associated document-only state.
Zero-byte packages are complete attached states. A checked switch can reject
stale descriptions after raw binary editing; reconciliation is explicit.
Trusted switching can restore a known-compatible association after schema loss.
The full implemented contract is in the
[runtime API](runtime-api.md#initial-attachment-and-schema-switching).

This replaces the earlier document-only switching and contained-role proposals.
The document-only merge workflow below is a separate construction operation: it
can adopt defaults and rebuild binary, while switching preserves the existing
package. It neither requires a successful checked switch before merging nor
changes the implemented switching contract.

### Agreed source and result forms

The merge core has one semantic working form: document-only input and output.
Other forms are converted at the boundary. Type-schema merging accepts two
read-only `CSchemaDocumentQuery` views, each of which can refer to a live or baked
document, and produces an independently owned, validated `CLiveDocument`.
It consumes definitions, including explicit-default presence and presentation;
it does not aggregate instance or bulk data sections. Inputs need not be
physically converted to live storage merely to read them.

Adoption into a live schema and baking are separate boundary operations. The
result carries no schema-owner binding or retained resolved cache. Existing
restrictions on replacing schema owners with attached consumers still apply;
merging does not bypass them. Instance aggregation uses the existing
`CInstanceDocumentQuery`; bulk data aggregation uses `CBulkDataDocumentQuery`.
Both accept live or baked documents, including mixed source forms, and produce
an independent `CLiveDocument`. Existing query views and an internal reader
bridge support presentation and efficient matching without a new public adapter.
The result contains only `instances` or `data`, respectively, with no type schema,
binary package or retained schema binding.

| Data input | Preparation before document merging |
| --- | --- |
| Ordinary instance declarations, with or without retained binary | Use the declarations directly. Omitted values remain omitted. |
| Stripped instances with complete binary | Promote against the original schema, then use the reconstructed declarations without the binary package. |
| Complete bulk data descriptions | Use the complete record descriptions. |
| Stripped or reference-only bulk data with complete binary | Reconstruct the missing complete record descriptions against the original schema before merging. |

A stripped input without its required binary is incomplete and cannot be
converted. Zero-byte types still require the explicit complete-package route;
a zero-length payload is not a request to invent missing data. Partially described
bulk data must become complete at the boundary under the
[whole-entry rule](#agreed-whole-entry-bulk-data-preparation). Record counts and
shapes must validate rather than being silently repaired. Validate supplied
locator/count/size information before removing source
locators and the stripped-state marker from the working document. Complete bulk
record sequences determine their counts; later binary construction generates new
locators. Discarding storage metadata must not hide malformed source input.

For stripped instances, promotion must use the original schema, before its
association is lost or replaced. Base instances are reconstructed as differences
from that schema's effective defaults. Each specialisation is reconstructed as
differences from its immediate parent's complete snapshot, which already reflects
the preceding defaults/instance/specialisation chain. Promotion does not populate
every member. Unchanged nested members and wholly unchanged declarations are
omitted. Arrays retain the shortest prefix through their last changed element;
earlier equal elements appear only as needed to preserve positional meaning.

A child restoring a schema-default value still declares it when it differs from
the immediate parent; promotion compares against the parent, not directly against
the schema at every level.

For example, a stripped base storing `speed = 0` under an original implicit-zero
default promotes with `speed` omitted. If the merged schema adopts default `10`,
reconstruction from that declaration produces `10`, as intended. Promoting against
the merged schema instead would emit an explicit zero to preserve the old
snapshot, defeating this default-adoption policy.

Stripping has already erased whether a default-equal value was originally
explicit. The minimal reconstructed declaration becomes the merge input; that
lost distinction and value presentation cannot be recovered. Ordinary retained
instance declarations are not automatically promoted or reconciled: doing so
could remove explicit values or presentation that remain available to merge.
Binary edits intended to contribute must be explicitly reconciled first. The
merge does not silently refresh retained declarations from binary data.

Role promotion produces a live role with its own document and payload. The
separate preparation functions instead borrow source bytes, reconstruct only
missing descriptions and publish independent document-only storage with storage
metadata removed after validation. They do not construct temporary roles or copy
payload ownership. Source blocks, roles, payloads and bindings remain unchanged;
dropping binary refers to the working representation, not destroying source storage.

The workflow is: merge type definitions; prepare document inputs, using original
schemas wherever promotion is required; merge declarations/complete records;
then optionally construct binary with the merged schema. These first two steps
may be ordered independently provided original schemas remain available and
promotion is not redirected to the merged schema.

The previous matching-source-form policy and aggregation-specific output-form
selector are superseded. Live/baked storage, retained/stripped inputs and final
persistence forms belong to boundary conversion. Existing attachment,
materialisation and output facilities supply later binary/persistence output;
merging does not itself promise to preserve source binary bytes.

### Agreed pairwise aggregation interfaces

The operations remain separate and pairwise: type-schema merging, instance
aggregation and bulk data aggregation each combine two explicit sources into
one independently owned result. First-source then second-source encounter order
and transactional replacement remain applicable. Combining additional sources is
a sequence of pairwise operations.

The previous specialised-role overload matrix, prohibition on a schema argument,
and requirement that all sources already bind the literal same schema owner are
superseded by the document-only working form. Generic documents do not carry a
schema binding. Original schemas are needed for stripped-input promotion, while
the merged schema supplies the context for validation and later construction.
Already attached sources need not first undergo a binary-preserving switch to
the merged owner merely to contribute their declarations.

For type schemas, the two existing read-only views and independently owned live
document result are selected above. The agreed function, diagnostics and
publication mechanism are specified below. Conflicts identify both original
source locations, without retaining handles into discarded temporary output.

For instance and bulk data merging, the selected views are
`CInstanceDocumentQuery` and `CBulkDataDocumentQuery`, respectively; the destination
is an independently owned `CLiveDocument`. Each operation receives one ready,
read-only `CResolvedSchema` for the merged schema. Generic input documents have
no binding to switch. Original schemas are required by binary-to-document
conversion, not by the merge core. The agreed function and diagnostic declarations
are specified below and implemented.

Use one merge implementation per operation over prepared document input, with
binary-backed conversion at the boundary. No universal owner, attached
document-only role state or live/baked overload family is needed. Extract shared
internal merge mechanics from type-schema merging only where the data operations
demonstrate a concrete need; a generic public merge framework is not selected.

The earlier proposed `EAggregationOutputForm`, `SAggregationOptions` and
role-to-block `aggregate_instances(...)` / `aggregate_bulk_data(...)` signatures
are retired design proposals, not implemented APIs. Output form selection and
optional consistency checking belong to explicitly designed boundary operations.
Use the existing `bool` plus diagnostic-output convention for the data operations
too. Conversion diagnostics remain separate from merge diagnostics. Conversion
failures identify original source entries. Successful conversion publishes a
caller-owned document; merge diagnostics may therefore borrow locations in that
document for as long as the caller preserves it. No persistent provenance map
from reconstructed values back to stripped data is required. Any later combined
convenience operation must explicitly settle its own diagnostic lifetimes.

### Agreed data-aggregation functions and diagnostics

The following interfaces are implemented in `data_merge.hpp`:

```cpp
namespace schema
{

enum class EDataMergeStage : std::uint8_t
{
    none = 0u,
    preconditions,
    first_source_validation,
    second_source_validation,
    document_merge,
    result_validation
};

enum class EDataMergeReason : std::uint8_t
{
    none = 0u,
    invalid_schema,
    invalid_source,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

struct SInstanceMergeDiagnostic
{
    EDataMergeStage stage{ EDataMergeStage::none };
    EDataMergeReason reason{ EDataMergeReason::none };
    CInstanceHandle first;
    CInstanceHandle second;
    SInstanceDiagnostic validation;
};

struct SBulkDataMergeDiagnostic
{
    EDataMergeStage stage{ EDataMergeStage::none };
    EDataMergeReason reason{ EDataMergeReason::none };
    CBulkDataHandle first;
    CBulkDataHandle second;
    SBulkDataDiagnostic validation;
};

[[nodiscard]] bool merge_instances(
    const CInstanceDocumentQuery& first,
    const CInstanceDocumentQuery& second,
    const CResolvedSchema& schema,
    CLiveDocument& destination,
    SInstanceMergeDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool merge_bulk_data(
    const CBulkDataDocumentQuery& first,
    const CBulkDataDocumentQuery& second,
    const CResolvedSchema& schema,
    CLiveDocument& destination,
    SBulkDataMergeDiagnostic& diagnostic) noexcept;

}   // namespace schema
```

`schema` is the ready, resolved merged schema. The functions do not resolve it,
convert binary-backed input or construct a binary package. Each source is a
prepared document view; live/baked combinations use the same interface. Results
contain only `instances` or `data`, respectively. Occupied destinations and
either/both-source aliasing follow the publication contract below.

The stage/reason enums are shared, while source locations and nested validation
diagnostics retain their existing instance or bulk data types. There is no
compatibility stage: both prepared inputs are interpreted against the same
supplied schema. Unknown types, incomplete bulk records and invalid declarations
are source-validation failures.

| Failure | Diagnostic |
| --- | --- |
| Supplied schema is not ready, or destination replacement would invalidate its backing document | `preconditions` / `invalid_schema`, with no document locations or nested validation detail. |
| Invalid prepared input | Corresponding source-validation stage and `invalid_source`, retaining the existing domain diagnostic in `validation`. |
| Conflicting declarations or records | `document_merge` / `conflicting_value`, with both source locations and defaulted `validation`. |
| Allocation or storage limit | The stage where it failed and the specific resource reason, only when identified by the underlying operation. |
| Other document node-construction failure | `document_merge` / `construction_failed`; do not invent a more specific resource cause. |
| Invalid completed document | `result_validation` / `invalid_result`, retaining the domain validation reason but clearing its temporary occurrence. |

For source-validation failures, populate the corresponding `first` or `second`
handle from the nested occurrence where available; leave the other unset. The
source-validation stage identifies the owner of nested locations. For conflicts,
the two handles identify the supplied conflicting values. Unavailable locations
remain unset. Resource failures during validation follow the same source versus
temporary-result location rules and retain available domain validation detail.
For all result-validation failures, leave both source handles unset and clear
`validation.occurrence`; no staging handle may escape.

Report the first failure and clear the entire diagnostic on success. Diagnostics
own no document storage. Their source locations remain valid only while the
corresponding supplied document and its identities remain valid, including when
that input is a caller-owned conversion result. Conversion failures remain in the
separate conversion API; no conversion stage or temporary-input provenance map
is added to these merge diagnostics. Later binary-construction errors likewise
belong to the separate construction operation.

### Agreed data-aggregation validation and publication

Prepared input is an object root containing only the selected `instances` or
`data` section. An absent section is empty; a present section must be an object.
Always emit the selected object section in the result. Empty groups/sections are
valid, but a present bulk entry with an empty record sequence remains invalid.
Reject locators, stripped markers, persistence-envelope fields and unrelated root
sections. Preparation extracts the selected section directly without an extra
document copy solely to normalise its root.

The destination must not back the supplied resolved schema: resolved names and
other source access still borrow that document. Reject this case with a narrow
internal identity check as `preconditions` / `invalid_schema`. Do not impose a
general ban on harmless read-only sharing or introduce a hidden schema copy,
new binding or alias registry. Source/destination aliasing remains supported.

Validate each prepared input independently against the supplied merged schema,
then validate the completed document before publication. One input cannot repair
an invalid declaration or incomplete bulk record in the other. Validation observes
explicit instance selections without expanding defaults or inheritance into the
document. Ordinary retained declarations are not automatically reconciled; raw
binary edits contribute only after explicit reconciliation by the caller.

Complementary declaration validation shares construction admission helpers for
scalar/enum conversion, bit-field constraints, value shape, named/positional
selection and completeness. It visits supplied selections with indexed schema
lookup, without default expansion, payload access or a disposable binary package.
Existing writer/comparison behaviour and diagnostic precedence are preserved.

The prepared-document walker shares entry/property checks and checked extent
arithmetic with existing callers. Locator validation and record gathering stay
with their storage-specific callers. Validation does not attach a data role or
retain a resolved cache in the output.

Keep cheap depth checks on declaration paths already visited, and allow implicit
depth failures during later binary construction. Do not add a depth-summary cache,
extra schema-graph traversal or omitted-value expansion solely to predict those
failures. Cached type resolution does not prove that every implicit path fits the
codec's depth budget, so merge success does not guarantee later construction.
Retain existing construction guards and discard private partial construction
output on failure; the merged document remains unchanged. The
[construction-depth policy](runtime-api.md#construction-depth-policy) accepts
potentially wasted later construction work on unusually deep inputs instead of
adding a preflight to every merge. No representative workload measurement
establishes the frequency of such failures.

Construction uses a private live document, preserving the agreed ordering,
presentation and omission rules. Finish all fallible construction and result
validation, then release temporary borrowed state before publishing through
`destination = std::move(staged)`. An occupied destination contributes only if
explicitly supplied as a source. Either or both sources may alias the destination;
same-source merging still follows ordinary validation and merge rules. Failure
preserves sources and destination; success invalidates old destination identities.
No rollback owner or failing publication step is needed.

Final-result diagnostics must not retain locations in discarded staging storage.
Source conflicts identify the supplied prepared documents, including caller-owned
conversion results. Later binary construction remains a separate operation with
its own failure boundary. Composition is a separate implemented operation;
combined convenience orchestration remains future design work.

### Agreed type-schema merge function and diagnostics

The following agreed interface is implemented in `schema_merge.hpp`:

```cpp
namespace schema
{

enum class ESchemaMergeStage : std::uint8_t
{
    none = 0u,
    first_source_validation,
    second_source_validation,
    compatibility,
    document_merge,
    result_validation
};

enum class ESchemaMergeReason : std::uint8_t
{
    none = 0u,
    invalid_source,
    incompatible_type,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

struct SSchemaMergeDiagnostic
{
    ESchemaMergeStage stage{ ESchemaMergeStage::none };
    ESchemaMergeReason reason{ ESchemaMergeReason::none };
    CSchemaHandle first;
    CSchemaHandle second;
    SDiagnostic validation;
};

[[nodiscard]] bool merge_type_schemas(
    const CSchemaDocumentQuery& first,
    const CSchemaDocumentQuery& second,
    CLiveDocument& destination,
    SSchemaMergeDiagnostic& diagnostic) noexcept;

}   // namespace schema
```

Each view can refer to a live or baked document, including mixed input forms.
The result is an independently owned live type-schema document. Adoption into
a schema owner and baking remain separate operations. No output-form selector,
schema-owner argument or live/baked overload matrix is required.

The destination may be empty, occupied, or explicitly supplied as either source.
An occupied destination contributes only when supplied as a source. The same
document may supply both source arguments and follows the ordinary merge rules;
aliasing does not bypass validation or conflict checks.

Diagnostics follow the existing stage/reason and borrowed-location conventions:

| Failure | Diagnostic |
| --- | --- |
| Invalid first or second source | The corresponding source-validation stage and `invalid_source`, retaining the resolver's complete `SDiagnostic`. Its handles and ranges belong to that source. |
| Incompatible resolved types | `compatibility` / `incompatible_type`, with `first` and `second` identifying the original type declarations. |
| Conflicting supplied values, including defaults | `document_merge` / `conflicting_value`, with `first` and `second` identifying the conflicting original values. |
| Allocation or storage limit | The stage where it failed and `allocation_failed` or `storage_limit`, only where the underlying operation distinguishes that cause. |
| Other document construction failure | `document_merge` / `construction_failed`; do not guess a more specific resource cause. |
| Invalid completed schema | `result_validation` / `invalid_result`, retaining the resolver's reason and stage in `validation`. Clear all temporary-document handles and ranges. |

Resource failures during resolution retain the underlying resolver detail, with
the same source/temporary-location rules. For source validation, the corresponding
`first` or `second` location may identify the resolver's occurrence; the other
remains unset. Unavailable locations remain unset. For merge conflicts,
`validation` remains defaulted; the two source handles identify the conflict.

Report the first failure and clear the diagnostic on success. Diagnostics own
no document storage; source locations remain usable only while the corresponding
source document and its identities remain valid. Final-result diagnostics must
not expose handles into destroyed staging storage. No output-to-source provenance
map is required merely to retain those temporary locations.

### Agreed type-schema transactional publication

Use private staging and the existing non-failing `CLiveDocument` move assignment:

1. Independently resolve both source documents.
2. Check overlapping types using the existing representation comparison and
   merge original declarations and presentation into a private
   `CLiveDocument staged`. Apply the explicit-default policy to the declarations,
   not effective defaults from the resolved types.
3. Resolve the complete staged document before publication.
4. Release temporary resolvers and comparison state before replacing any document
   they borrow. Preserve only valid source diagnostics on failure.
5. Publish with `destination = std::move(staged)`.

All source reads, construction, validation and allocation finish before the final
move. Failure discards staging and preserves both sources and the destination,
including existing handles/views. Success invalidates previous destination
handles/views; a source that aliases the destination is replaced only at that
point. Other sources remain unchanged. The completed result borrows no source
storage and retains no resolved cache.

The existing move assignment transfers ownership without a fallible publication
step. No new transaction class, rollback mechanism, friend access or schema-owner
state is needed. Existing bound-owner replacement restrictions still apply to
separate adoption into a schema owner. This is transactional publication within
the existing caller-controlled lifetime/concurrency contract, not synchronisation
for concurrent readers.

### Agreed normalisation of descriptions and metadata

Document values determine merge compatibility after boundary preparation.
Retained declarations are not automatically replaced by binary-authoritative
reconstruction. If raw binary edits should contribute, the caller explicitly
reconciles them before merging. Optional consistency checking may reject a
mismatch at the boundary; it must not silently select another source of truth.
Ordinary persistence loading remains binary-authoritative under its existing
contract.

For instances, preserve explicit selections and omissions through document
merging. A missing selection adopts the merged schema default or merged parent
value during later construction. Do not expand every member into the working
document or minimise existing declarations against the merged defaults, since
either can change the intended default/inheritance behaviour. Stripped inputs
use the minimal declarations obtained under their original schema as described
above. Bulk data preparation produces complete records, not default-dependent
instance declarations.

| Content | Preparation and merge policy |
| --- | --- |
| Retained integer notation, minimum digits, prefixes and newline-escaping preferences | Preserve and combine under the agreed presentation rules. |
| Reconstructed values with no retained presentation | Use existing schema/output defaults; lost source presentation is unavailable. |
| Names, type associations and instance hierarchy | Preserve meaning and translate source-local identities. |
| Supported annotations and other non-value semantic fields | Preserve and merge recursively; conflicting supplied content fails. |
| Locators, payload size, resulting record counts and stripped-state markers | Remove or regenerate at the relevant conversion/construction boundary. |

Supported annotations remain limited by each existing document format. This
contract neither introduces arbitrary annotation fields nor permits unsupported
content to disappear silently. Validation must reject unsupported properties.
Presentation merging applies to equal supplied values; it cannot resolve a
semantic conflict or recreate preferences removed by stripping.

### Agreed binary equality and permitted values

The earlier rule requiring colliding instance snapshots to match is superseded.
Existing binary is input to explicit boundary reconstruction where descriptions
are unavailable; it is not a second equality condition on a document merge.
Schema-default changes and merged parent declarations can intentionally change
new instance snapshots. Bulk data prefix matching compares the complete document
records obtained at the boundary under the document scalar and array rules.

Conversion must validate that supplied binary values can be represented under
the applicable schema. Noncanonical booleans and enum values without declared
labels remain invalid conversion input. The permitted fp16 domain excludes
infinity and NaN; stripped input must not bypass that restriction. This does not
change ordinary persistence loading or select a new fp16 conversion policy.

Existing promotion and reconciliation verify that generated declarations
reproduce meaningful source values under the original schema. Reuse that
validation and its failure reporting at the conversion boundary. The existing
`compare_encoded` rules ignore padding and unused bits and equate NaN payloads
where NaNs are permitted. Those reconstruction checks do not replace the recorded
document scalar equality rules with a binary merge predicate.

Dropping binary representation can lose padding, unused bits and NaN payload
identities. These are not preserved by the document-only merge contract. Source
buffers themselves remain unchanged. Invalid or unrepresentable values and
allocation/size-limit failures reject conversion without partial publication.

### Agreed output ordering and binary layout

Named object members follow first-encounter order: process the first source,
merge matching names from the second at their existing position, then append
previously unseen names. Apply this recursively to groups, entries and instance
specialisations. Array order remains significant: ordinary arrays require equal
lengths, while the outer bulk data sequence uses the prefix rule and retains the
longer sequence in its existing order.

This output convention does not authorise reordering the members or enum labels
of a same-name schema type where order affects layout, positional construction
or implicit defaults. Type-schema validation must preserve that meaning.

The merge result itself has no binary layout. A later construction step creates
new snapshots/records from the merged document and merged schema, deriving fresh
locators and counts. Visit instance parents before descendants; each bulk data
entry occupies a contiguous record sequence. Pack entries in document traversal
order using their schema-required alignment, member offsets, array strides and
record sizes. Distinct entries have independent storage. Zero-sized entries
consume no payload storage.

Completed output retains a 128-byte-aligned payload base. Source gaps and trailing
slack are not copied. The payload ends at the last occupied extent, including
schema-required record tail padding, without extra trailing alignment rounding.
The existing complete-block and zero-payload format rules remain applicable.

The previously agreed policy for newly constructed aggregate binary remains:
zero structure/array padding, record tail padding, unused bit positions,
inter-entry alignment gaps and document-to-payload padding. Use the existing
unused-storage facility with `EUnusedBits::clear` on private constructed output
where necessary. A declared value is not unused storage because it is omitted,
inherited or zero. Reconstructed NaNs need not retain source payload bits.

All construction occurs in independent destination storage. Ordinary loading,
saving, stripping and schema switching retain their existing byte-preservation
contracts. Aggregation is not required to save an independent entity or package
separate entities into a bundle.

## Agreed document preparation interfaces

Agreed and implemented on 19 October 2026. Preparation converts one source
into independent document-only storage suitable for the existing merge cores.
It does not merge, attach or switch schemas, reconcile retained descriptions,
or construct a completed binary package. Already prepared documents may go
directly to merging without another preparation or copy.

Use two overloads per domain, following the existing query and diagnostic-output
conventions:

```cpp
[[nodiscard]] bool prepare_instances_document(
    const CInstanceDocumentQuery& source,
    const CResolvedSchema& source_schema,
    CLiveDocument& destination,
    SInstancePreparationDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_instances_document(
    const CInstanceDocumentQuery& source,
    const CResolvedSchema& source_schema,
    const CByteConstView& payload,
    const bool compare_embedded,
    CLiveDocument& destination,
    SInstancePreparationDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_bulk_data_document(
    const CBulkDataDocumentQuery& source,
    const CResolvedSchema& source_schema,
    CLiveDocument& destination,
    SBulkDataPreparationDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_bulk_data_document(
    const CBulkDataDocumentQuery& source,
    const CResolvedSchema& source_schema,
    const CByteConstView& payload,
    const bool compare_embedded,
    CLiveDocument& destination,
    SBulkDataPreparationDiagnostic& diagnostic) noexcept;
```

Existing queries cover live and baked documents; no role-owner overload family
or new public source adapter is selected. `source_schema` must be ready and is
the caller-supplied schema describing the source, including its original defaults.
The merged schema belongs to later merge validation and binary construction.
Preparation retains no schema binding.

The overloads distinguish no supplied binary from an explicitly supplied complete
package with a possibly zero-byte payload. The document-only overload accepts
ordinary descriptions and supported preconstruction metadata. It rejects
stripped input and storage references requiring an unavailable binary package;
it must not silently discard an unverifiable binary dependency. The supplied
overload validates applicable locators, counts, sizes, alignment, ranges and
coverage under existing storage rules before removing metadata. An empty payload
does not authorise inventing missing data; zero-byte types retain their explicit
package and count requirements.

Retained instance declarations preserve explicit selections, omissions,
specialisation hierarchy and presentation. An absent declaration in unstripped
input means inheritance, not missing information to reconstruct. Stripped
instances reconstruct minimal differences against original schema defaults for
bases and immediate parent snapshots for specialisations, as specified above.
Lost explicit default-equal selections and presentation cannot be recovered.

`compare_embedded` is explicit, with no default argument. When false, valid
retained descriptions remain authoritative even if binary edits have made them
stale. When true, disagreement with supplied binary fails. It controls only
retained-description comparison, not storage validation or representability
checks for reconstructed values. Binary edits contribute through explicit
reconciliation; preparation never silently selects a different source of truth.

### Agreed preparation storage-metadata admission

Agreed on 19 October 2026. These rules supplement the preparation interfaces
and selected-section extraction policy; they are implemented.

These are preparation rules, not changes to ordinary persistence loading or
materialisation. The payload argument, when supplied, belongs to the selected
domain only. Other sections never contribute offsets, counts, types or bytes.


**Root metadata.** Recognise authored sections and the exact common
[.bsd envelope](common-binary-block.md). Sibling sections are checked for
container shape without interpreting their entries.

Any envelope metadata requires the common fileType and unsigned version: 1.
The two payload lengths are independently optional. A selected declared length
requires an explicit payload, including zero, and must equal its logical byte
length. A missing selected length permits document-only preparation when retained
descriptions suffice. The stripped marker still requires supplied binary.
Reject incomplete/unknown metadata and legacy identifiers.

Preparation validates these claims, not complete file framing. Only the common
block loader validates actual file extent, positions and container padding.
The other domain's range never supplies the selected domain's binary.

**Entry locators.** A present locator must be an object containing only `offset`,
`valid`, optional `count` and optional `size`. Require `offset` and `valid`;
`offset` is an unsigned integer within the existing 32-bit offset limit and
`valid` is Boolean. Count and size use existing unsigned integer and extent rules.

| Locator state for a selected entry | Document-only overload | Supplied-binary overload |
| --- | --- | --- |
| Absent | Accept if the retained description is sufficient. | Reject as missing locator, even if retained values exist and comparison is disabled. |
| Present, `valid: false` | Accept as a preconstruction placeholder if retained values suffice; validate metadata, then omit the locator from output. | Reject as invalid locator; never invent a location in supplied bytes. |
| Present, `valid: true` | Reject with `binary_required`; the claimed association needs its explicit package. | Validate alignment, ordered extents, overlap and payload bounds using existing role rules. |

Document-only inputs may mix absent locators and placeholders. A false locator's
offset is checked for its required representation/range but is not an address:
do not impose alignment or overlap checks on that unused number, require zero,
calculate a replacement offset, or require mutable source storage. Output's
prospective packed extent is checked by the existing final prepared validator.

For instances, supplied `count` must equal 1 and supplied `size` must equal the
resolved type size. For bulk entries with a retained array, its positive length
determines the count; supplied count and size must agree with the entire sequence.
Without an array, the supplied-binary route uses an explicit positive count or,
where existing rules permit, derives it from size divisible by nonzero record
size. Zero-size records require explicit count when descriptions are absent.
Counts and extents must stay within existing limits. Missing instance declarations
in ordinary input still mean inheritance; missing bulk arrays require binary.

All selected entries on the supplied-binary route require valid locators,
including zero-byte entries. Validate the payload view and its existing 128-byte
nonempty base-alignment requirement before access. Apply the established zero-size
offset/overlap rules without inventing a new positive-extent requirement. Gaps
and trailing slack remain permitted; do not require the last extent to equal
`selected payload length` or require internal payload gaps/padding to be zero. `compare_embedded`
does not relax any metadata check. An explicit empty payload is still a supplied
package; it is never inferred from the absence of the argument.

**Diagnostics and precedence.** Use `source_validation` and `invalid_source` for
these failures, with existing domain detail: `unknown_property`,
`missing_property`, `invalid_input` for wrong envelope type/identifier/version,
`invalid_locator`, `invalid_count`, `invalid_range`, `overlap` or `binary_required`
as appropriate. Identify the offending original property or entry. Check root
shape/metadata and applicable package requirements before visiting entries; then
visit entries in document order, validating locator structure and count/size
claims before reporting an unavailable package for a valid locator. Resource
failures retain the agreed specific operation reason where known. No new public
diagnostic enum members are needed.

### Agreed whole-entry bulk data preparation

An entry is the complete record sequence belonging to one named bulk data entry,
not an individual record or member. Each entry has one source for its values:

| Entry description | Preparation |
| --- | --- |
| Complete retained `data` array | Validate and preserve the full description and presentation; compare with supplied binary only when requested. |
| No `data` property | Require supplied binary and validated locator/count metadata; reconstruct the entire record sequence. |
| Present array containing incomplete or malformed records | Reject; do not fill member or record holes from binary. |
| Present array shorter or longer than its declared count | Reject the count mismatch; do not recover or discard a tail. |
| Present empty array | Apply existing count rules; empty is not absent. |

A document may contain some completely described entries and some reference-only
entries. Missing descriptions are reconstructed as complete records, without
instance-style default omission. This does not relax stripped-format admission:
descriptions remain forbidden where the stripped marker forbids them. Neither
metadata nor supplied record shapes are silently repaired.

### Agreed selected-section extraction

Preparation extracts only `instances` or `data`, according to the function called.
It accepts combined documents containing the recognised `types`, `instances` and
`data` sections without letting unselected content influence prepared values.

- Check that the root is an object, root properties are recognised, and present
  recognised section containers have the required object shape. Reject unknown
  root properties rather than silently dropping potentially meaningful content.
- Fully validate and prepare only the selected domain. Do not semantically
  validate sibling data sections or resolve, compare, merge or use embedded
  `types`. The explicit `source_schema` is the sole schema authority, including
  defaults, type identity and interpretation.
- Validate known storage metadata applicable to the selected domain before
  removing it. Unselected sections and their entry metadata do not supply values,
  counts, locators or other preparation context for the selected domain.
- Preserve supported names, declarations, hierarchy and presentation within the
  selected section. Reject unsupported fields there. Known sibling sections are
  deliberately excluded from output.
- Always publish a root containing exactly the selected section. If that section
  is absent, publish an explicit empty object, subject to valid applicable
  metadata. Remove locators and stripped-state metadata; output contains neither
  binary storage nor a schema association.

For example, an unknown type used only by an unselected bulk data section does
not prevent instance preparation. An unknown root property or a malformed
section container does. Embedded type definitions, including defaults that
disagree with the supplied schema, are not consulted. Whole-document semantic
validation remains a separate caller operation.

This broader extraction boundary does not loosen the merge cores' strict
prepared-root admission: preparation publishes exactly the form they accept.

### Agreed preparation diagnostics and publication

Use shared stages/reasons and typed existing domain detail, separately from
merge diagnostics:

```cpp
enum class EDataPreparationStage : std::uint8_t
{
    none = 0u,
    preconditions,
    source_validation,
    document_preparation,
    result_validation
};

enum class EDataPreparationReason : std::uint8_t
{
    none = 0u,
    invalid_schema,
    invalid_source,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

struct SInstancePreparationDiagnostic
{
    EDataPreparationStage stage{ EDataPreparationStage::none };
    EDataPreparationReason reason{ EDataPreparationReason::none };
    SInstanceDiagnostic detail;
};

struct SBulkDataPreparationDiagnostic
{
    EDataPreparationStage stage{ EDataPreparationStage::none };
    EDataPreparationReason reason{ EDataPreparationReason::none };
    SBulkDataDiagnostic detail;
};
```

Unavailable schemas and schema-backing destination aliases fail at preconditions
with `invalid_schema`. Source admission failures use `invalid_source`; existing
domain detail distinguishes, for example, `binary_required`, `invalid_locator`,
`invalid_count` and `embedded_mismatch`. Unrepresentable binary values fail with
`invalid_source` and `unrepresentable_value` detail at the stage that discovers
them, including reconstruction during `document_preparation`. Invalid completed
output uses `result_validation` / `invalid_result`. Report allocation and storage
limits distinctly only when the underlying operation establishes the cause;
otherwise document construction failure uses `construction_failed`.

Report the first failure and clear the whole diagnostic on success. Detail
occurrences borrow original source nodes where available; reconstruction failures
may identify the original entry. Unavailable locations remain unset. Final-result
validation retains reason detail but clears all handles into temporary output.
No persistent source-provenance map is required. The caller owns successful
prepared documents, so later merge diagnostics can borrow their locations under
the usual document lifetime rules.

Construct a private live document, validate the prepared result under
`source_schema`, and publish through the existing non-failing move assignment.
Occupied destinations and source/destination aliasing are supported. Failure
preserves sources, destination and existing handles; successful replacement
invalidates old destination views. Reject replacement of the document backing
`source_schema`. Other borrowed views remain subject to caller-managed lifetimes;
source storage, payload and schema must remain usable throughout preparation.

Reuse the existing copying, string preparation, value validation and
reconstruction machinery through narrow internal helpers. Do not implement
preparation by promoting an entire temporary live role: that copies payload
storage and can reconstruct retained declarations that must instead be preserved.
Borrow source bytes and reconstruct only missing descriptions. Existing scratch
storage for original defaults and representability verification is permitted;
no disposable complete output payload or new owner class is required. Retain
existing cheap depth checks and failure semantics rather than adding a graph
preflight. The [runtime construction notes](runtime-api.md#shared-document-construction)
describe the shared paths; the [milestone](../project/completed_milestones.md#document-preparation-and-composition)
records verification.

## Complete document composition

Agreed and implemented on 19 October 2026. Composition assembles compatible
`types`, `instances` and `data` sections into a document for inspection,
interchange or further preparation. Multiple sources within a domain are merged
first; composition does not resolve collisions, overwrite competing sections or
introduce another default policy.

### Agreed composition interfaces and input policy

Always supply one types document and either an instances document, a bulk data
document, or both. Use three overloads in `namespace schema`:

```cpp
[[nodiscard]] bool compose_document(
    const CSchemaDocumentQuery& types,
    const CInstanceDocumentQuery& instances,
    CLiveDocument& destination,
    SDocumentCompositionDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool compose_document(
    const CSchemaDocumentQuery& types,
    const CBulkDataDocumentQuery& bulk_data,
    CLiveDocument& destination,
    SDocumentCompositionDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool compose_document(
    const CSchemaDocumentQuery& types,
    const CInstanceDocumentQuery& instances,
    const CBulkDataDocumentQuery& bulk_data,
    CLiveDocument& destination,
    SDocumentCompositionDiagnostic& diagnostic) noexcept;
```

Existing queries admit live, baked and mixed storage. Each input must be a
prepared, single-domain document: the types document contains its `types` object,
and each data document contains only its respective prepared section. Reject
unrelated root properties, locators and stripped descriptions. Extraction and
missing-description reconstruction belong to preparation, not composition.
Ordinary prepared descriptions need not have been produced by a particular API.

Output is an independently owned `CLiveDocument`, with sections in fixed order:
`types`, then `instances` if supplied, then `data` if supplied. An omitted domain
remains absent. A supplied empty data document produces an explicit empty section,
including when its root has no selected section, as allowed by prepared-data
validation. Within each section preserve declaration order, values, presentation,
instance omissions and specialisation hierarchy exactly. Do not merge, reconcile,
expand defaults or construct binary.

The supplied types document is the sole schema authority. There is no separate
resolved-schema argument or schema-owner association. Later binary construction
uses the composed definitions, defaults and inheritance through existing
domain-specific construction paths. A composed document is not a mandatory
runtime owner. It can be passed directly to common-block materialisation.

### Agreed composition diagnostics

```cpp
enum class EDocumentCompositionStage : std::uint8_t
{
    none = 0u,
    types_validation,
    instances_validation,
    bulk_data_validation,
    document_composition
};

enum class EDocumentCompositionReason : std::uint8_t
{
    none = 0u,
    invalid_source,
    allocation_failed,
    storage_limit,
    construction_failed
};

struct SDocumentCompositionDiagnostic
{
    EDocumentCompositionStage stage{ EDocumentCompositionStage::none };
    EDocumentCompositionReason reason{ EDocumentCompositionReason::none };
    SDiagnostic types;
    SInstanceDiagnostic instances;
    SBulkDataDiagnostic bulk_data;
};
```

Only the relevant detail field is populated. Source admission and semantic
validation failures report `invalid_source` at the corresponding validation
stage, with existing resolver or domain detail. Allocation and storage failures
retain their specific reason only where the underlying operation distinguishes
them; otherwise copying/construction failure uses `construction_failed` at
`document_composition`.

Report the first failure. Validation locations identify original source nodes;
copying failures retain a source location where available. Unavailable detail
remains empty, and no diagnostic contains handles into discarded staging storage.
Diagnostic locations borrow their corresponding input document under existing
lifetime rules. Success clears the whole diagnostic.

### Agreed composition validation and publication

1. Admit the types-only input and resolve its definitions once into a temporary
   `CResolvedSchema`.
2. Validate each supplied data document against that resolution using existing
   prepared-document validation, in instances-then-bulk order. These checks also
   enforce the strict single-domain input forms.
3. Copy the selected sections into a private live document using existing
   document construction and string preparation facilities. Checked construction
   still reports allocation, storage and depth failures.
4. Finish all source reads and release the temporary resolver before replacing
   any document it borrows.
5. Publish through the existing non-failing `CLiveDocument` move assignment.

Do not repeat schema resolution or semantic validation over the composed result.
Composition copies validated sections without changing their contents or
relationships, so one resolution, one validation of each supplied data domain
and one copy of each selected section suffice. This differs from merging, where
newly combined declarations require result validation. No disposable binary
construction or implicit-depth graph preflight is required; implicit depth
failures can still occur during later binary construction.

Occupied destinations and destination/source aliasing are supported. Failure
preserves all inputs, destination storage and existing handles. Success replaces
only the destination and invalidates its old handles/views, including an input
that aliases it. Output owns its document storage and retains no resolver, schema
binding or source dependency. Other borrowed views remain subject to the existing
caller-managed lifetime rules; inputs remain usable and unchanged during the
operation until any final aliased replacement. No transaction class, rollback
mechanism or persistent provenance map is needed. The
[milestone](../project/completed_milestones.md#document-preparation-and-composition)
records completed delivery and verification.

## Scope boundaries and follow-on work

Persistence, attachment/switching, type/data merging, preparation and composition
are delivered capabilities. The [milestones](../project/completed_milestones.md#schema-delivery-status)
record their review, commits and validation. Standalone operations retain their
own diagnostics and transactions; later binary construction is separately fallible.

Compatibility signatures, persisted resolved lookups, automatic bundle persistence
and writes to bundle entries are outside the delivered contracts. Direct final
storage for description materialisation, selected type subsets and orchestration
of multiple independently produced binary sources remain optional work. Common
aggregate materialisation and explicit assembly are now implemented.
The [follow-on notes](../backlog/schema_follow_on.md) retain these considerations
alongside source ingestion, conversion, shader representations and editor consumers.
