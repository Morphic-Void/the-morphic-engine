Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   data_model_design_notes.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   8 Sep 2026

# Data-model design notes

## Purpose

These notes record the reasoning behind the normative requirements in
`docs/backlog/revised_data_model.md` and the likely implementation direction.
They are not requirements. Keeping rationale here prevents implementation
history, rejected alternatives and provisional mechanics from obscuring the
semantic contract.

## Simplicity discipline

New behaviour should normally be pursued in this order:

1. compose existing public operations;
2. reuse an existing private mechanism;
3. make a small general extension useful to several operations;
4. add one narrow primitive which enables composition; and
5. add specialised machinery only when the smaller approaches are demonstrably
   insufficient.

Code volume is continuing audit, maintenance and cognitive cost. Avoiding one
allocation or preserving an incidental key does not justify substantially more
states, rollback paths or duplicated traversal. Caller-controlled bad input
still warrants validation; impossible states created only by an implementation
defect belong primarily to integrity checks and known-bad containment.

## Infrastructure boundary

Extensions to established infrastructure must be individually identified,
expressly approved and committed independently. This includes existing live
and baked document APIs or behaviour, containers, shared utilities and the
linter. Explain why composition is insufficient and describe compatibility,
allocation and platform implications before implementation.

New self-contained writer/parser code and feature-local helpers are feature
work, not infrastructure extensions merely because they are new or reside
outside `core/data_model`.

The approved observation prerequisites are now implemented:

- `CStableStrings::string_count()` exposes the number of valid string IDs while
  excluding the internal sentinel. This supports correctly sized bake-analysis
  storage without teaching the data model about container internals.
- `TPodOrderedSlots::key_at_slot()` returns the key paired with a live keyed
  slot in O(1). The concrete key array belongs to that façade;
  `TOrderedSlots` knows only slot metadata and delegates comparison to its
  backing, so the generic slot layer cannot naturally provide the observation.
- `TOrderedCollection::key_at_slot()` provides the same O(1) observation for
  constructed slots in the non-POD keyed façade, keeping the two keyed
  container interfaces consistent.

Each prerequisite was implemented and committed independently of the data
model. They add observations over existing state rather than new container
state or ordering machinery.

`CStableStrings` already maintains lexical rank information and exposes ID/rank
conversion. Baking uses that facility rather than introducing a new sorting
framework. Any further container or memory-system change needs its own
justification and approval.

## Live representation

### Explicit aggregate nodes

Keeping explicit aggregate nodes in the mutable document still has value. They
isolate an owner's payload from changing child links and let the same value-node
operations serve scalar and aggregate values. This does not require the baked
format to retain them: immutable aggregate metadata may fit more simply in its
owner.

### Aggregate names

Duplicating an owner's name on its aggregate added an invariant, reference
accounting and update work without supplying useful independent information.
The canonical empty aggregate name removes those costs while retaining a
uniform physical field if the current node layout benefits from one.

### Object-entry state

An independent object-entry flag duplicates the fact already represented by a
non-empty name and creates states which must be cross-validated. Deriving the
state makes malformed combinations unrepresentable at the semantic level.

A name always supplies object-entry meaning, regardless of the payload type.
Outside an object, an anonymous containing object is implied. A named object
payload still needs that outer containing context for its own name. Writers
materialize the implied braces; parsers remove redundant singleton wrappers
where the destination accepts named children. No persistent implicit-wrapper
flag, extra node or object-preservation text tag is needed.

### Integer width

The smallest integer width is derivable, but repeated writer and schema queries
make derivation needless recurring work. It remains directly stored in both
live and baked values. A baked flags field can normally carry the four width
states in two bits, so direct access is expected to have negligible layout
cost. Integrity validation still checks the stored width against value and
domain.

### External keys and internal slots

The public `CNodeKey` remains a monotonic stable identity. It need not also be
the live node's internal address. Resolving a caller-supplied key through the
ordered container is an O(log n) boundary operation; following every tree link
as another key repeats that search throughout traversal.

Internal parent, sibling, owned-aggregate, owner and child links are represented
as `LiveNodeSlot` indices backed by `std::int32_t`, with `-1` invalid. Once an
incoming key has resolved to a slot, each structural hop is then O(1).
Returning a public key for a reached node is also O(1) with the implemented
`key_at_slot()` accessor. The concrete alias has no `T` prefix because it is a
complete type rather than a template parameter or incomplete type.
A height-`h` traversal changes from approximately O(h log n) key lookup to one
O(log n) boundary lookup followed by O(h) direct traversal.

The former self key duplicated the parallel key held by `TPodOrderedSlots`.
Its extra stale-slot check supplied useful diagnostics but did not justify
permanent duplication when link mutation and container integrity are already
checked. Replacing four 64-bit key links and the self key with four 32-bit slot
links reduced the live node from 64 to 40 bytes without more aggressive packing.

Role-specific link storage uses explicitly named value and aggregate structures
in a union rather than opaque `relation_0` through `relation_3` members. This
improves debugger and audit clarity without storing both roles' inactive fields
or adding new semantic state.

Slot reuse means a dangling internal index could later name another node. The
model should prevent this through the existing unlink-before-erase invariant
and verify reciprocal topology during integrity checks. Adding generations or
another internal handle layer would reproduce the defensive machinery this
simplification is intended to remove.

Future packing requires relationship remapping because slot indices are not
stable across `sort_and_pack()`. `TPodOrderedSlots::build_rank_map()` already
provides the old-slot-to-rank mapping. A document-level pack can allocate that
map before mutation, rewrite each valid link, then perform the non-failing pack.
Packing is not currently required, so this need should not burden ordinary live
mutation.

## Replaced persistent reachability accounting with analysis

The former live implementation paid on every attachment, detachment, payload
change and erasure to maintain counts intended mainly for later baking. This
coupled unrelated mutation paths to recursive traversal and made simple
operations fail or invalidate through accounting machinery.

The implemented division of responsibility is:

```text
coherent live tree
  -> one iterative analysis crawl
  -> externally owned bake analysis
  -> measured baked layout and mappings
  -> one owning baked allocation
```

The analysis structure can hold exactly what a bake needs: category counts,
recovered-content presence, per-name and per-string reference counts, final
string ranks or maps, byte totals and emission offsets. It is single-use,
single-threaded and allocated through the framework. It should not become a
second mutable document.

Queries such as `is_complete`, `is_canonical`, category counts and "contains
recovered content" use the same traversal components on demand. Reusable
visitation and accumulation helpers avoid a persistent cache, revision system
or several near-identical walks.

Memory accounting is different: it describes owned framework resources rather
than semantic reachability and remains continuously available.

## Payload operations as the compositional primitive

`detach` and `detach_payload` serve different purposes:

- `detach` moves an existing value identity and allocates nothing.
- `detach_payload` preserves the original value's identity and position while
  creating an anonymous carrier for everything beneath its name envelope.

`attach_payload` completes the transfer in the opposite direction. Requiring
the source to be anonymous and detached makes the operation unambiguous: the
target's name and topology survive, while only the payload moves. Erasing the
source shell avoids a public half-consumed object.

`erase_payload` is the other small primitive. It generalises the existing
nested-erasure behaviour by preserving the selected top value as an empty
placeholder. Together these operations express recovery repair without a
special tree-rewrite implementation.

The implementation uses one private payload-field move for extraction and
attachment. It leaves both nodes' names and structural links untouched and
retargets an owned aggregate directly when present. Payload erasure and root
clearing share the same aggregate-child erasure path. This removed the former
whole-node topology substitution rather than expanding it.

## Public recovered arrays

Making recovered arrays public simplifies both parser construction and baked
promotion. The useful invariant is structural: their children are anonymous.
Minimum cardinality and collision-only construction do not protect tree
coherence, but would require special construction modes and awkward transient
states.

A first object-member collision can be assembled from ordinary operations:

1. extract the existing member's payload into an anonymous carrier;
2. extract the candidate's payload into another anonymous carrier;
3. create a recovered array and append both carriers in encounter order;
4. attach that aggregate payload to the now-empty existing member; and
5. erase the now-empty candidate shell.

For a later collision, extract the candidate payload, append the carrier to the
existing recovered array and erase the candidate shell. A pre-existing public
recovered array can also be promoted by creating it and appending its anonymous
children normally.

This composition made the reserved recovery attachment outcomes unnecessary;
they were removed with the live-node simplification. Attachment now reports a
rejection reason; `none` means that insertion succeeded.

## Mutation failure policy

Full operation atomicity is not a goal. Cheap validation should precede
mutation, especially key, role, detached-state, naming, duplicate-name and
cycle checks. Allocation should also occur before destructive steps when the
sequence naturally permits it.

If a later step fails, the operation reports failure. A result which cannot be
proven coherent is visibly invalid, and an internally contradictory live
document becomes reset-only known-bad. This gives callers a reliable signal
without a general transaction layer.

## Baking direction

Only the final baked artifact must be one allocation. Scratch allocations are
appropriate for analysis and mapping when they reduce copying or complex
in-place algorithms. The target is low churn, not allocation-count heroics.

Useful first-layout properties are dense value indices, contiguous ordered
direct-child ranges and two directly sorted string tables. O(1) ordinal array
access follows naturally. An object-name accelerator should be added only if
real consumers justify its footprint and construction cost.

The Stage 6 record sketch confirmed that a separate baked aggregate supplies no
useful isolation in immutable data. Aggregate kind is already the owning
value's type, and its only remaining state is a first-child index and count.
Those fields are folded into one 32-byte value record.

Keeping the parent index makes parent and sibling queries O(1) and provides a
cheap reciprocal structural check. Removing it would reduce the record to 24
bytes, but would turn a useful established query into a scan. The eight-byte
cost is proportionate for the direct-user and later schema interfaces.

The replacement header stores counts and byte sizes, not five redundant
section offsets. A fixed section order derives every address and removes layout
combinations from both emission and validation. Root index zero also removes
the archived sentinel value record. Explicit reserved fields avoid unmanaged
structure padding in the byte format.

The format omits v1's checksum and derived semantic flags. A checksum detects
some accidental changes but is not authentication, requires another whole
block pass and is better supplied by a persistence envelope when one exists.
Recovered content is immutable and cheap to discover by scanning value types.

The checked view is the only public raw-byte boundary. This keeps one view type
and gives readiness a useful meaning: arbitrary bytes cannot become ready
without full validation. Baking binds its emitted bytes through that same full
validation before publishing the owning block; no trusted bypass is needed.

Full validation is linear and uses one transient framework vector. Reusing it
first for referenced-name marks, then object-local name stamps and finally
referenced-string marks verifies compact string coverage and object uniqueness
without persistent state or quadratic scans.

Existing live analysis is sufficient for baking. Its two reference-count
vectors can become string-ID maps after byte measurement, and one
baked-index-to-live-key vector drives breadth-first direct emission. Narrow
live-document observers expose the ID at a requested lexical rank in each
string domain. Baking can therefore use only the public live interface; no
private node or stable-store access is required.

## Baked interface and translation boundaries

The immutable query surface is split by expected use rather than by a blanket
inlining rule. Small direct-record observations and thin owning-block accessors
are inline in the public header. Validation, string searches, object-child
lookup and other loops remain out of line. This makes warm structural and typed
queries available to the optimiser without moving substantial machinery into
every consumer.

Full validation likewise separates concerns without weakening the single
checked-view boundary. A record-local helper validates type, canonical unused
fields, payload and integer encoding. The enclosing document validator retains
all facts requiring position, string domains, topology or comparison with
other records. Making those latter checks record members would hide required
context rather than simplify it.
The helper remains private to `CBakedDocument`; `SBakedValueRecord` stays a
passive physical-layout record rather than acquiring view-policy methods.

Baking and promotion are peers in `document_translation`, not member functions
of either representation. The live document therefore need not include or
understand the baked representation, and the baked document need not construct
or understand live storage. Baking consumes only public live observations;
promotion consumes only the checked baked query surface and public live
construction. Narrow friendship exists only to publish a completed owning
baked block; promotion publishes its successfully staged live document through
the ordinary move-assignment surface.

## Writer and parser implementation direction

The writer in `document_writer.hpp/.cpp` consumes only `CBakedDocument` queries.
Existing parent, child and sibling operations support iterative entry/exit
traversal without a new query or an allocated tree index. Keep output, numeric
spelling, escaping and layout as small reusable private functions. A private
output buffer can simply be
discarded on failure; a general rollback system is unnecessary.

Assess standard-library facilities by exception, allocation and portability
constraints. The writer uses `std::to_chars` for integer magnitudes and finite
binary64 output. Its caller-buffer interface and documented non-allocating,
non-throwing MSVC implementation fit the policy.
The no-format, no-precision floating overload selects shortest round-trip
output; the writer retains negative zero and adds a float marker when needed.
Check exact output and bit-round trips on supported toolchains, including
cross-implementation parsing when another STL becomes a supported target.
The standard's corresponding-function round-trip guarantee
alone does not establish cross-vendor behaviour. The parser should assess
`std::from_chars` by the same criteria.

References: [Microsoft charconv documentation](https://learn.microsoft.com/en-us/cpp/standard-library/charconv?view=msvc-170)
and [numeric output conversion contract](https://eel.is/c++draft/charconv.to.chars).

The writer materializes implied object braces around every named child outside
an object, independently of payload type. Recovery payloads use the documented
version-1 wrapper in either output mode. Ordinary property names use reversible
dollar escaping; protocol keys and string values do not. Output reports count
numeric normalization, non-ASCII and NUL escapes, reserved-name escapes and
recovered arrays by emitted occurrence. Failed writes discard output and
counts, retaining the failure status. The buffer supplies its own length until
publication, avoiding duplicate mutable length bookkeeping.

Writer tests construct sources through public live operations and baking.
They check exact text for all named payload types, recovery cardinalities,
nested competitors, reserved-name lookalikes, quoting and layout; numeric
boundaries and deterministic finite-bit samples check conversion independently
through `from_chars`. Deep named arrays exercise iterative synthetic wrappers,
and allocation failures after transformations check partial-output disposal
and framework accounting. Full text-to-live round trips belong to the later
parser slice; writer tests do not introduce a temporary parser.

The linter owns encoding conversion. Its UTF-8 result uses bounded lengths,
permits literal U+0000, and normalizes accepted modified NULs to that scalar.
Live string admission supplies the established `C0 80` storage form. Keeping
literal-input counts separate from modified-NUL normalization and stripped
source terminators makes transformations observable without changing document
string termination. Count raw embedded zeros once, independently of any
retry as CP1252. A failed result must not claim a ready output encoding.

The linter update is an approved separate prerequisite. It changes the former
literal-NUL rejection and modified-NUL passthrough contracts and adds explicit
output-encoding reporting. Existing framework buffers and SuiteUTF suffice;
no allocator, platform or live/baked API change is required. Existing linter
consumers must use logical lengths and inspect success and encoding. Document
ingestion passes a zero newline-normalization mask; the general linter's
existing default remains available to other callers.

The structural check and relaxed parser share feature-local lexical functions
instead of maintaining competing quote, escape and delimiter rules. Use a
framework frame vector and reusable string scratch, not a full token tree.
The initial shared grammar includes comments, single quotes, identifier-style
unquoted names, trailing commas, raw quoted controls and unbraced root members.
Its exact inventory is recorded in `revised_data_model.md`; shared relaxation
and numeric-extension bits are declared in `document_text_lex.hpp`.
There is no separate strict parser. Ingestion transformations, required syntax
relaxations and Morphic interpretations are reported independently.

The structural pass validates syntax, including numeric token spelling, but
does not establish representability or construction feasibility. It assumes
numbers are representable, duplicate names are manageable and all subsequent
document allocations succeed. Keep numeric conversion/range policy, collision
resolution and reserved-wrapper interpretation in parsing. Track nesting and
estimate capacity without treating those estimates as allocation guarantees.
A structurally valid result may still fail parsing; failure to allocate the
structural pass's own scratch is a resource failure, not a structural defect.

`document_structure::check` and the shared scanner consume bounded
linter-produced UTF-8 through `CStringView`. Its explicit length preserves
embedded NULs and distinguishes absent input from present empty text. Absent
input fails before scanning; present empty text denotes an implicit empty
object. The check's frame vector stores only object/array context and the
expected next syntactic role.
There is no arbitrary grammar depth cap or document allocation preflight;
frame allocation failure and the frame storage ceiling have resource statuses.
The scanner exposes token spans and the same escape-to-scalar operation that
construction can use later. No quoted string scratch is needed just to check
syntax. Duplicate names and reserved metadata do not require string decoding,
interning or comparison here. Report offsets refer to the linter's UTF-8 output.

Recovery wrapper recognition precedes ordinary singleton unwrapping. Protocol
control fields are validated separately from user data, and recovery transport
arrays retain anonymous competitors. Reserved data names use the reversible
dollar escaping specified in `revised_data_model.md`; no document-wide envelope
or tag for preserving redundant anonymous singleton objects is needed.

Parser construction, singleton unwrapping and recovery use ordinary live
creation, detachment, payload movement and erasure. Current public sibling and
name queries suffice to find collisions; do not add a live lookup API or
recovery privilege without demonstrating a concrete need. Build privately and
publish through the existing move operation after success. Failure signalling
and existing known-bad containment suffice without transactional mutation.

## Preserved concerns

The model deliberately retains numeric lexical intent, iterative traversal,
separate string domains, explicit untrusted baked validation and ordered
recovery competitors because writers, parsers and promotion consume them.

Modified UTF-8 `C0 80` is the live and baked logical-NUL storage contract.
Text ingestion and writing normalize at their boundaries; physical string
terminators retain their established meaning.
