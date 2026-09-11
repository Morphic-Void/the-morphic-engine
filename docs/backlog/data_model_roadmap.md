Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   data_model_roadmap.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   8 Sep 2026

# Data-model roadmap

## Purpose

This document records implementation status, delivery order and explicit
deferrals for the replacement data model. The normative contract is
`docs/backlog/revised_data_model.md`; rationale is in
`docs/backlog/data_model_design_notes.md`.

Each stage should remain a reviewable unit. Extensions to established
infrastructure, including live/baked APIs, require individual approval and a
separate commit. New self-contained features and feature-local helpers do not
require infrastructure approval merely because they are new.

## Current baseline

Stages 0 through 8 are complete. The Stage 9 writer and parser path is
implemented, validated and approved for commit, including its final recovery
and normalization slice. Stage 10 persistence and integration follows that checkpoint.

The live implementation currently provides:

- a 40-byte role-dependent live node representation, monotonic public keys and
  direct internal slot links;
- the implicit root object;
- framework memory accounting;
- separate stable property-name and string-value domains;
- public lexical-rank observation for both string domains;
- alias-safe validated string admission;
- empty, scalar, array and object construction;
- numeric intent and finite-float validation;
- strict append and insertion, detachment and recursive erasure;
- explicit integrity, canonicality and completeness observation;
- on-demand root-reachable analysis with optional caller-owned string references;
- a reset-only known-bad state;
- allocation-free detachment, identity-preserving payload extraction and
  attachment, and payload erasure.

The replacement baked implementation in `core/data_model` provides the
physical records, checked non-owning view and owning single-allocation block.
A separate public translation layer provides staged baking and promotion
without either document representation depending on the other. The v1 baked
model, writer and tests under `graveyard/data_model_v1_2026-09-01` are
reference material only.

Post-implementation consolidation is also complete. The baked public header
contains the small direct-record queries intended for repeated use, while
substantial scans and validation remain out of line. Local record-encoding
checks are separated from whole-artifact topology and cross-record validation.
The live slot-index type is explicitly named `LiveNodeSlot` rather than using
the template-style `T` prefix. A complete const-correctness and manual style
pass has also been applied across the replacement data-model implementation.

## Settled direction

- Retain memory accounting, but remove persistent tree, content and string
  reachability accounting.
- Compute observations on demand and bake prerequisites into an external
  analysis structure.
- Keep explicit live aggregates, with canonical empty aggregate names.
- Derive object-entry state from a non-empty name.
- Retain directly queryable integer width in both live and baked documents.
- Keep stable public node keys, but prefer direct slot indices for internal
  relationships and remove the duplicated node self key.
- Make recovered arrays a public value type accepting anonymous children, with
  no cardinality or collision-origin restriction.
- Provide distinct detach, payload-detach, payload-attach and payload-erase
  semantics which recovery and parsing can compose.
- Require one allocation for the completed baked artifact, not for the entire
  baking process.
- Avoid specialised allocation-context move machinery.

## Stage 0: documentation remap

Split the previous mixed specification into the normative specification, these
roadmap notes and nonnormative design notes. Remove superseded requirements and
make the settled simplification direction explicit.

Status: complete.

## Stage 1: container observation prerequisites

Implemented and committed as separate infrastructure changes:

- `CStableStrings::string_count()` exposes the valid entry count while
  excluding its internal sentinel.
- `TPodOrderedSlots::key_at_slot()` provides O(1) conversion from a live keyed
  slot to its paired key.
- `TOrderedCollection::key_at_slot()` provides the corresponding observation
  for constructed non-POD slots.

The accessors have proportionate container tests, including behavior across
sorting and packing. No ordering or sorting facility was added; existing
lexical-rank and rank-map support remains the intended basis for baking and any
later live-document packing.

Status: complete.

## Stage 2: remove persistent reachability accounting

Remove live reference-count vectors and continuously maintained semantic
totals. Simplify creation, attachment, detachment, erasure, string interning,
integrity checking and move handling around their absence.

Add a reusable iterative analysis path which can accumulate on-demand document
observations and externally owned bake prerequisites. Retain and audit the
existing memory-accounting totals.

Tests should assert public semantics and analysis results, not reproduce the
deleted bookkeeping implementation.

Root-reachable observations exclude detached subtrees; integrity checking still
covers all document-owned nodes.

Status: complete. Live semantic totals and reference vectors have been removed.
The shared iterative crawl supports allocation-free structural observations and
optional caller-owned string-reference analysis. Distinct referenced-string
totals are available through the explicitly fallible analysis operation.
Framework memory accounting remains composed from the document's owned storage.

## Stage 3: simplify node semantics

Make aggregate names canonically empty and derive object-entry state from value
names. Remove redundant update and validation paths. Preserve the current node
size only if it remains useful.

Evaluate replacing internal `CNodeKey` relationships with direct slot indices,
using explicitly named role-specific link structures, and removing the node's
duplicated self key. Keep `CNodeKey` as the stable public identity. Confirm the
resulting node size and use the container's rank map as the defined route for
any later document packing.

Reassess the current attachment result categories once their recovery-specific
uses are removed. Keep only outcomes which callers can act on.

Status: complete. Aggregate names are canonically empty and object-entry state
is derived directly from value names. Internal relationships use explicit
role-specific `LiveNodeSlot` indices backed by `std::int32_t`, while public
identity remains a monotonic `CNodeKey`; conversion back to public keys is
O(1). The duplicated self key and attachment outcome category have been
removed; attachment success is derived from the reported rejection reason.
The live node has reduced from 64 to 40 bytes. Packing remains deferred and
would require the documented rank-map remapping pass.

## Stage 4: complete payload composition

Audit the existing operations against the settled public contracts and complete:

- allocation-free `detach`, which already has the intended identity semantics;
- allocating `detach_payload`, preserving the original value identity;
- allocation-free `attach_payload`, preserving the target value identity; and
- payload-preserving-top-node `erase_payload`.

Prefer existing detach, nested erase, subtree traversal and slot-release
mechanisms. Test the operation boundaries, ownership and failure signalling.

Status: complete. `detach_payload` now leaves the original value empty in its
existing topology and returns a newly allocated anonymous detached carrier.
`attach_payload` moves that carrier's payload into an empty target without
allocation, preserves the target key and topology, and erases the consumed
carrier. `erase_payload` preserves an ordinary value as an empty node while
erasing its payload and descendants; for the root it performs the same clear as
`erase(root)`. A shared payload-field move replaces the former whole-node
substitution, and both root entry points use the shared root-clear handler.
The live document's duplicate string-table readiness flags have also been
removed; `CStableStrings` owns lazy initialization and allocation failure.

## Stage 5: public recovered arrays

Expose recovered-array construction and observation. Apply ordinary attachment
and insertion with the anonymous-child rule. Remove provisional minimum-size,
collision-origin and relaxed-attachment machinery.

Exercise parser-style first and later collision composition, recovery repair,
nested recovered arrays and empty/single-child recovered arrays. Add specialised
production helpers only if composition proves materially insufficient.

Status: complete. Recovered arrays are an explicit public value type created
through the shared container path. Ordinary append and insertion accept only
anonymous children for this type and report a specific rejection for a named
candidate. Empty and single-child forms are coherent, nested recovered arrays
are permitted, and the former minimum-cardinality and test-only kind mutation
have been removed. First and later collision handling and repair compose from
the general payload and topology operations, so no specialised recovery helper
was added.

## Stage 6: baked-format checkpoint

Before implementation, settle only the physical choices required by the first
baked artifact:

- explicit versus owner-folded aggregate metadata;
- value records and canonical unused fields;
- contiguous direct-child range encoding;
- header, section, alignment, offset and version needs;
- sorted name and string-value tables; and
- the full-validation and checked-view boundary.

Use measured live-analysis needs to choose scratch mappings. Do not design O(1)
object lookup or a public mutable baked builder without demonstrated demand.

Status: complete. The normative physical format is recorded in
`baked_document_format.md`. Immutable aggregate metadata is folded into a
32-byte value record with parent and contiguous child-range fields. A 32-byte
header and fixed section order derive all offsets. The root is dense index zero;
separate lexical string tables physically include empty index zero. The format
uses magic `MBD2`, version 1, no checksum and no semantic-summary flags.
Arbitrary bytes enter through one fully validating checked view. Existing live
analysis plus reusable string maps and one breadth-first value-key vector is
sufficient, so this checkpoint adds no live or infrastructure prerequisite.

A subsequent unused-path audit removed the unneeded live aggregate total,
duplicated node-role state, redundant attachment survivor output and dead
key/name forwarding helpers. Defensive topology validation and explicit
failure signalling remain unchanged.

## Stage 7: baked block, view and baking

Implement the owning single-allocation baked block and non-owning immutable
view. Build it from a coherent live document using external scratch analysis,
reachable-content compaction, dense indices, sorted string tables and contiguous
direct-child ranges.

Implement explicit full validation for untrusted baked bytes. Failure must not
publish an incomplete block as ready.

Status: complete. The first slice defines the exact header, value and string
reference records and implements the checked non-owning view. Binding validates
the complete physical artifact with one transient framework vector. The second
slice completes classification, interned-text, relationship, ordinal-array,
object-lookup and typed-payload queries without persistent indices or scratch.
The final slice adds the owning block and public translation bake. Baking
reuses the live analysis reference vectors as string-ID maps and uses one
breadth-first value-key vector to emit directly into an exactly sized,
32-byte-aligned final allocation. The completed bytes are validated before
publication; a failed rebuild leaves an existing block unchanged. The
translation implementation is separate from the baked representation and has
only narrow publication access to the owning block.

The physical-format header is self-contained and remains separate from the
public view declaration. Small direct-record queries and thin owning-block
observers are defined inline in the public header; validation, scans, lookup
and other substantial work remain out of line. Record-local encoding checks
are factored separately from positional, naming, topology and cross-record
validation, retaining a single full-validation boundary without one monolithic
validator.

## Stage 8: promotion

Promote an explicitly validated baked view into a fresh compact live document.
Preserve semantic order, names, payload kinds, numeric intent, strings and
recovered arrays while creating new live keys and string IDs.

The public translation layer's bake and promotion operations form the ordinary
route between allocation contexts.

Status: complete. Promotion uses only the checked baked query surface and the
public live creation and attachment operations. One breadth-first discovery
vector records the baked values and their parent indices while counting the
exact live value-plus-aggregate node requirement. Construction occurs in a
fresh staged live document; it replaces the destination only after all values
are attached and the result passes its integrity check. Promotion lives beside
baking in the translation layer rather than coupling either representation to
the other.

## Stage 9: writer and parser

The parser pipeline is linter, structural-integrity check and relaxed document
parsing. There is no separate strict parser. The writer has Morphic and strict
output modes, with independent ASCII escaping. Named values imply object
entries for every payload type; anonymous singleton wrappers normalize under
the destination rules in the semantic specification. Recovery identity and
competitor order round-trip through the explicit reserved JSON wrapper.

Delivery and review boundaries are:

1. Update the semantic contract, rationale and roadmap for these decisions.
2. Update the existing linter as an individually approved infrastructure
   commit: accept/count embedded zeros, normalize modified NULs to UTF-8
   U+0000, and report output encoding and CP1252 transformations. Preserve
   length-aware document string admission and physical terminators.
3. Implement the writer against public checked baked queries. Reuse useful
   archived formatting behaviour, qualify `std::to_chars` under the constrained
   STL policy, and implement reversible recovery/name escaping. No archived
   checksum, mutable-builder or automatic diagnostic-envelope contract returns.
4. Add shared feature-local lexical helpers and the structural check, with
   iterative nesting tracking and useful estimates rather than a full token
   tree. Validate syntax without numeric range checks, collision resolution or
   document-allocation preflight. Structural success does not promise that
   construction or policy-dependent parsing will succeed.
5. Implement relaxed live construction and feature reports, followed by
   reversible Morphic wrapper decoding, singleton normalization and ordered
   duplicate recovery using ordinary public operations. Review these as
   separate slices, completing end-to-end round trips before Stage 9 is done.

Tests cover numeric extrema and shortest round trips, negative zero, escaping
and ASCII output, literal/escaped/modified NULs, CP1252 conversion and reports,
all named payload types, singleton eligibility, malformed structure, reserved
name lookalikes, every recovered-array cardinality, nested recovery and later
collisions. Semantic comparisons use normalized wrapper form and fresh live
identities. Exercise meaningful allocation failures and deep iterative walks.
Structural tests distinguish malformed token spellings from well-spelled
out-of-range numbers and accept colliding object names. Keep construction
failures and policy-dependent outcomes in parser tests.
Run ordinary suites and relevant Windows Debug/Release x64/Win32 checks;
qualify additional supported numeric toolchains with the same corpus.

Status: documentation checkpoint and reviewed linter update complete.
Ordinary suites pass in Debug and Release on x64 and Win32, including the
writer suite and 203 linter checks in each configuration.

The writer slice is implemented, validated and approved for commit. It
uses the checked baked queries, supports Morphic/strict output and independent
ASCII escaping, wraps every named payload outside an object, and emits the
reversible recovery and reserved-name representation. Numeric formatting uses
caller-buffer `std::to_chars`; output growth uses the framework byte buffer.
Validation covers exact text, numeric boundaries and bit-round trips, deep
nesting, Unicode/NUL escaping and allocation-failure cleanup. Policy validation
and line-ending checks pass.
The shared lexical helpers and structural check are implemented, validated and
approved for commit. They accept explicit or unbraced root
objects and the documented relaxed grammar, without numeric range conversion,
collision handling or recovery-protocol interpretation. All ordinary suites
pass in Debug/Release on x64/Win32, including 1,203 structural checks per
configuration. Coverage includes absent versus present-empty string input,
malformed syntax, bounded truncation, escape
decoding, huge numbers, collisions, policy-invalid metadata, writer output,
deep nesting and scratch-allocation failures. Policy and line-ending checks
pass.

The initial relaxed live parser is implemented, validated and approved for
commit. It shares the structural scanner and escape decoder, constructs every
ordinary payload through public live operations, retains integer metadata and
syntax feature reports, and publishes only after successful construction.
That initial slice reported numeric range errors, empty property names,
duplicate names and reserved protocol names explicitly. Its tests cover ingestion, decoded
strings and NUL admission, numeric boundaries, source aliasing, deep nesting
and allocation-failure cleanup. All ordinary suites pass in Debug/Release on
x64/Win32, including 1,439 parser checks per configuration. Policy and
line-ending checks pass.

The final parser slice implements recovery-wrapper decoding, reserved-name
unescaping, singleton normalization and ordered duplicate recovery, and is
validated and approved for commit. Its reports distinguish successful semantic
interpretations from structural syntax observations. Tests cover control-field
validation and ordering, nested and extended recovery, normalization contexts,
decoded-name collisions, deep recovery nesting and allocation-failure cleanup.
End-to-end tests construct live fixtures, bake and write them in both modes
with independent ASCII and layout options, lint and parse the text, compare
normalized live semantics, and check stable rewriting.
All ordinary suites pass in Debug and Release on x64 and Win32, including
15,081 parser checks per configuration. Policy and line-ending checks pass.
Validation includes 512 nested recovery wrappers and failures at successive
framework allocation points during decoding, normalization and collision
recovery, with unchanged destinations and complete allocation cleanup.

## Stage 10: persistence and integration

Add direct binary and JSON file round trips, followed by asynchronous
Host/Executive functional round trips. Publish completed owning baked blocks or
lifetime-bounded views; do not move live documents across workload-thread
boundaries.

The agreed review boundaries are:

1. Integrate baked-block ownership with the existing erased-owner transport,
   including complete memory attribution and SYSTEM payload registration.
   Propose required extensions to established interfaces for approval before
   implementation; verify successful and rejected transfers and destruction.
2. Prepare the Host loading and conditioning paths. Carry caller-supplied
   alignment through the asynchronous file-load request to
   `platform::filesystem::loadFile`, retaining a 16-byte alignment floor.
   Add JSON ingestion, parsing and baking to `CHostWorkerThread`, dispatched
   on `thread_ids::bg_conditioning`. Verify these paths before implementing
   the full Executive test, with review before committing this preparation.
3. Exercise direct binary and JSON persistence, including validation of loaded
   bytes. Binary reloads can retain the existing loaded-file owner and expose
   a checked baked view, without an additional block-adoption API.
4. Implement the Executive-controlled functional run below through the real
   Host, worker and Executive DLL messaging path.

The baked-block ownership extension is implemented and validated, awaiting
review. It delegates standalone reattribution to the existing byte buffer and
provides private nested-storage hooks for the `BakedDocumentAsset` SYSTEM
payload. Focused tests cover allocator compatibility, unchanged bytes and views,
complete shell/block attribution, rejection of mixed source contexts, owning
messages from Executive to Host contexts, repository disposal, full-queue and
incompatible-allocator rejection, and unread-message destruction. Host loading
and conditioning preparation, persistence tests and the Executive-controlled
document run remain the subsequent review units.

Validation: all ordinary suites pass in Debug and Release on x64 and Win32,
including 213 baked-document transfer checks in each configuration. The policy
validator reports no errors or warnings, and the line-ending check passes.

### Executive-controlled document run

The Executive controls the sequence and performs the comparisons. The Host
owns transferred and loaded assets in its existing type-erased repository,
addressed by monotonically issued `CAssetId` values. Returning a loaded view
and handle does not return ownership to the Executive.

1. The Executive creates and populates a live fixture on its own thread.
   Include every supported value type, including empty placeholders' baking
   behavior, and repeat payload types in the contexts needed to exercise named
   object members, named array children and anonymous values. Include numeric
   intent and boundaries, Unicode/logical NULs, reserved data names, ordinary
   singleton objects, and empty, singleton, multiple and nested recovery.
2. Bake the unchanged live document twice independently. Retain one block as
   the Executive's reference and prepare the other for transfer.
3. Before transfer, require identical byte lengths and byte-for-byte equality
   of the two baked blocks.
4. Transfer ownership of the second block to the Host. The Host stores its
   erased owner and acknowledges successful storage with the asset identifier.
5. Advance to binary saving through that identifier. The Host routes the save
   to file I/O and reports success or failure before the Executive continues.
6. The Executive requests loading of the saved binary file. The asynchronous
   load carries the required baked-storage alignment through to the file
   loader, with a minimum of 16 bytes. The Host validates the loaded bytes,
   retains the new owning asset, and returns its distinct identifier and a
   checked baked view.
7. The Executive compares the loaded copy with its retained reference. The
   binary round trip must preserve the complete baked bytes exactly.
8. Repeat save, load and validation for Morphic JSON. The Executive writes
   JSON from its retained reference baked view and supplies the resulting text
   to the Host for saving through the asynchronous file-I/O path. After save
   confirmation, the Executive requests loading. The Host coordinates linting,
   parsing into a thread-local live document and baking a new owned block on
   `CHostWorkerThread` using `thread_ids::bg_conditioning`. Retain the loaded
   text throughout conditioning; live construction and scratch stay on that
   worker. Return the completed owning block to the Host, which publishes the
   new asset's identifier and checked view to the Executive.
9. Compare the JSON-loaded baked view with the reference using the agreed
   normalized semantics: preserve recovery identity and competitor order,
   account for redundant singleton wrappers and inferred integer widths, and
   compare values, names and retained numeric intent. Strict-output cases, if
   included, must account explicitly for their documented numeric normalization.
10. The Executive stops using the borrowed views and requests Host disposal of
    every asset created by the full functional test flow that remains Host-owned.
    This covers the transferred original, both loaded copies, transferred JSON
    output and any retained request or intermediate assets. Track flow ownership
    so that internal assets are covered even when their identifiers were not
    returned to the Executive; assets already released during processing are
    accounted for without requiring a second erasure.
11. The Host completes disposal after outstanding uses have finished and
    acknowledges the result. Require that disposed identifiers no longer resolve,
    no assets belonging to the test flow remain owned, and their allocations are
    released. The Executive waits for disposal confirmation before declaring
    success and completing the run.

Each asynchronous step waits for its correlated result. A failure enters the
cleanup sequence, preserving the original failure while requesting disposal
of assets already created. The Host must retain immutable backing storage
throughout every worker and Executive view use; handles alone do not provide
lifetime protection. Disposal requires completion of outstanding uses, including
file I/O, and the Executive must not access a view after requesting disposal.
Explicit acknowledged disposal is part of the functional test, before thread
shutdown. Shutdown cleanup remains a fallback if communication or execution
fails and cannot substitute for successful disposal verification. The Executive
releases its own reference before DLL shutdown, and Host-owned SYSTEM payload
destruction must remain valid after the Executive DLL unloads. Verify attribution
and complete cleanup on success, failure and shutdown. Pause before each commit
for review.

## Later typed-data phase

Schema, remapping, serialization, code generation and limited code parsing
follow a stable document pipeline. Their interfaces and intermediate forms are
not settled by the data-model work.

## Continuing deferrals

- O(1) baked object-name lookup.
- Live cursors and revisions.
- General naming or value-conversion APIs beyond demonstrated needs.
- Detailed typed-data architecture.
