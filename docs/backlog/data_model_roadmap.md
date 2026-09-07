Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   data_model_roadmap.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   6 Sep 2026

# Data-model roadmap

## Purpose

This document records implementation status, delivery order and explicit
deferrals for the replacement data model. The normative contract is
`docs/backlog/revised_data_model.md`; rationale is in
`docs/backlog/data_model_design_notes.md`.

Each stage should remain a reviewable unit. Changes to infrastructure outside
the data model require separate approval and a separate commit.

## Current baseline

The current checkpoint follows completion of Stages 0 through 6. Stage 7 is
next; no baked-document replacement has begun.

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
- a reset-only known-bad state; and
- allocation-free detachment, identity-preserving payload extraction and
  attachment, and payload erasure.

There is no replacement baked implementation in `core/data_model`. The v1
baked model, writer and tests under `graveyard/data_model_v1_2026-09-01` are
reference material only.

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
role-specific `std::int32_t` slot links while public identity remains a
monotonic `CNodeKey`; conversion back to public keys is O(1). The duplicated
self key and attachment outcome category have been removed; attachment success
is derived from the reported rejection reason. The live node has reduced from
64 to 40 bytes. Packing remains deferred and would require the documented
rank-map remapping pass.

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
analysis plus reusable string maps and one breadth-first slot vector is
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

## Stage 8: promotion

Promote an explicitly validated baked view into a fresh compact live document.
Preserve semantic order, names, payload kinds, numeric intent, strings and
recovered arrays while creating new live keys and string IDs.

Bake and promotion form the ordinary route between allocation contexts.

## Stage 9: writer and parser

Refactor useful v1 writer behaviour onto the stable baked view. Then resume the
parser layers in order: linter, structural prepass and relaxed parser. Strict
duplicate rejection remains the ordinary policy; recovery-enabled parsing uses
the same public recovered-array and payload operations as other callers.

Design any diagnostic recovery envelope and its import symmetry together. Do
not infer a permanent format from the archived v1 representation.

## Stage 10: persistence and integration

Add direct binary and JSON file round trips, followed by asynchronous
Host/Executive functional round trips. Publish completed owning baked blocks or
lifetime-bounded views; do not move live documents across workload-thread
boundaries.

## Later typed-data phase

Schema, remapping, serialization, code generation and limited code parsing
follow a stable document pipeline. Their interfaces and intermediate forms are
not settled by the data-model work.

## Continuing deferrals

- Exact baked byte layout and format version.
- O(1) baked object-name lookup.
- Live cursors and revisions.
- Long-term modified-UTF-8 U+0000 policy.
- Diagnostic recovery serialization.
- General naming or value-conversion APIs beyond demonstrated needs.
- Detailed typed-data architecture.
