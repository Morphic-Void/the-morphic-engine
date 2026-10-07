Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   TOrderedSlots.md  
Author: Ritchie Brannan  
Date:   1 Apr 26  

# TOrderedSlots<TSlotBacking, TIndex, TMeta>

## Purpose

TOrderedSlots<TSlotBacking, TIndex, TMeta> is a general-purpose ordering and
slot-management toolkit that maintains an ordered index over selected
occupied slots for a lower slot-backing layer.

It provides:

- Slot acquisition and recycling
- Optional ordering via an AVL tree
- Deterministic traversal via rank indexing
- Structural mutation and compaction utilities

It stores metadata only and does not store or access payload.

This template is intended as a base class, not a concrete container.

## Toolkit nature

TOrderedSlots is a composable structural layer.

The slot-backing layer:

- owns payload storage
- defines key comparison
- implements payload movement
- controls capacity approval

The public facade:

- exposes the public API
- coordinates whole-container lifetime

Ordering and traversal are optional and depend on facade usage.

## Interface model

The interface is primarily protected.

The lower TSlotBacking provides container-specific state and primitive
operations. TOrderedSlots provides metadata and structure. The upper facade
defines public behaviour.

## Structural model

Each slot is in exactly one state:

- lexed
- loose
- empty
- unassigned

Structures:

- lexed -> AVL tree
- loose -> circular doubly-linked list
- empty -> circular doubly-linked list

Stable-state invariant:

    lexed_count() + loose_count() + empty_count() == capacity()

## Terminology

"lexed" means ordered according to on_compare_keys().

It does not imply textual or string-based ordering.

## Slot index

Slot index domain:

    [0, capacity())

- identifies metadata and slot-backing payload
- stable except during sort_and_pack()

Sentinel:

- -1 is not a valid slot index
- may be used as failure sentinel
- selects the default empty slot during acquisition

## Rank model

Traversal order:

    lexed (ordered) -> loose -> empty

Rank index is defined by traversal order.

- lexed: [0, lexed_count())
- loose: [lexed_count(), lexed_count() + loose_count())
- empty: [lexed_count() + loose_count(), capacity())

Traversal order defines rank.

After sort_and_pack():

- slot_index == rank_index for all slots

## Equal key ordering

Equal keys preserve insertion order.

Equal-key runs are stable across:

- insertion
- relex_all()
- sort_and_pack()

## Ownership boundary

The slot-backing layer owns:

- payload
- key definition
- payload movement
- payload reserve coordination

TOrderedSlots owns:

- metadata
- tree and list structures
- metadata lifecycle

The public facade owns:

- public API
- coordinated container lifecycle

The facade destroys payloads or objects before releasing slot metadata. The
lower slot-backing object must remain alive whenever the middle slot manager
calls movement, reserve, or comparison responsibilities.

## Slot-backing responsibilities

TSlotBacking provides:

- query_type
- on_move_payload(source_index, target_index)
- on_reserve_empty(minimum_capacity, recommended_capacity)
- on_compare_keys(source_index, target_index)
- on_compare_query(query, candidate_index)

### on_compare_keys

Defines ordering.

Both operands identify stored keys at valid slot indexes. Structural operations
use this hook when comparing existing entries.

Comparator must be strict weak ordering.

### query_type and on_compare_query

The backing supplies a `query_type` and compares a caller-owned query against a
stored key at `candidate_index`. It returns the query-to-candidate relationship
using the same ordering as `on_compare_keys`.

Searches accept `const query_type&`. The slot layer forwards that reference and
retains no query state. It owns no keys and requires no knowledge of their
representation or container. The query type is determined by the existing
backing specialization; search methods have no additional template parameters.

`acquire(query, slot_index, lex, require_unique)` and `reserve_and_acquire(...)`
use the supplied query for uniqueness checks and initial tree placement before
the facade writes the new key. The query must remain valid throughout the call,
including any backing reservation. Stored-slot duplicate checks exclude the
specified occupied slot itself; separately named `has_duplicate_query` variants
include every matching entry. The distinct names support integer query types
without ambiguity with slot indexes.

### on_move_payload

Applies payload remap during sort_and_pack().

Supports:

- in-place remap using temporary storage (-1)
- external payload remap

### on_reserve_empty

Capacity negotiation.

Must satisfy minimum_capacity.

Metadata and payload-side reserve retain the established non-transactional
cross-layer behavior. A coordinated rollback redesign is a separate
architectural decision.

## Re-entry contract

The slot-backing responsibility functions must not re-enter the slot manager.
This is a usage contract rather than a runtime locking mechanism.

Mutation and lifetime changes require exclusive access. Read operations may
share unchanged, safely published storage when the backing comparison and
payload read operations also support concurrent reads. Query arguments remain
local to each call; no staged query key or query-dependent shared state exists.

Each `find_*` function checks safety before traversing metadata. Separate
unchecked `locate_*` functions have been removed because they had no independent
callers. Acquisition and duplicate-check implementation helpers remain shared
by entry points which have already checked safety.

## Internal layering

- direct compile-time calls to the protected lower slot-backing layer
- private structural helpers

## Mutation model

Operations:

- lex_all()
- unlex_all()
- relex_all()
- sort_and_pack()

sort_and_pack():

- reorders payload and metadata
- produces packed canonical layout

External-payload packing, metadata copy/move/take/clone facilities, and
facade-facing traversal helpers are retained capabilities even when a
particular production caller is not currently visible. Packing preserves the
`-1` payload-movement scratch convention and stable equal-key ordering. That
packing convention is independent of query handling.

## Capacity model

- capacity()
- minimum_safe_capacity()
- capacity_limit()

Operations:

- safe_resize()
- reserve_empty()
- reserve_and_acquire()
- shrink_to_fit()

No automatic shrink.

## Lifecycle

Destructive:

- initialise()
- shutdown()
- clear()

Non-destructive:

- resize
- reserve
- acquire
- erase

## Traversal operations

Lexed, loose, and empty slots expose direct first, last, previous, and next
traversal functions. Full-domain traversal order is lexed, then loose, then
empty, and is also available through rank mapping.

## Invariants

- exclusive slot categories
- counts sum to capacity
- AVL tree valid
- lists circular
- high_index correct

## Complexity

- AVL operations: O(log n)
- list operations: O(1)
- sort_and_pack(): O(n)

## Out of scope

- thread safety
- payload management
- comparator validation
- automatic packing
