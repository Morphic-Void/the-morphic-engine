Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   TUnorderedSlots.md  
Author: Ritchie Brannan  
Date:   1 Apr 26  

# TUnorderedSlots<TSlotBacking, TIndex>

## Overview

TUnorderedSlots<TSlotBacking, TIndex> maintains an unordered index over slot
indices for a lower slot-backing layer.

The template stores metadata only and does not store or access payload.

This template is intended as a base class.

## Scope

- metadata only
- no payload ownership
- no payload construction or destruction

## State model

Each slot is in one steady-state category:

- loose
- empty

Internal states:

- unassigned
- terminator

Structures:

- loose -> circular doubly-linked list
- empty -> circular doubly-linked list

Invariant:

    loose_count() + empty_count() == capacity()

## Slot index

Domain:

    [0, capacity())

Sentinel:

- -1 is not a valid slot index

## Ownership boundary

The slot-backing layer owns:

- payload or side storage
- payload movement
- payload reserve coordination

TUnorderedSlots owns:

- metadata
- loose and empty structures
- metadata lifecycle

The public facade owns:

- public API
- coordinated container lifecycle

The facade destroys payloads or objects before releasing slot metadata. The
lower slot-backing object must remain alive whenever the middle slot manager
calls movement or reserve responsibilities.

## Observation model

Traversal:

- defined by list order
- does not imply rank

Rank:

- defined over loose slots only
- rank(slot_index) = number of loose slots with lower slot index
- domain: [0, loose_count())
- empty slots return -1

## Pack model

pack():

- compacts loose slots into [0, loose_count())
- remaining slots become empty
- rebuilds lists in ascending slot index order

Non-goals:

- no empty-slot preservation
- no full-domain remapping

## Slot-backing responsibilities

TSlotBacking provides:

- on_move_payload(source_index, target_index)
- on_reserve_empty(minimum_capacity, recommended_capacity)

### on_move_payload

- source_index != target_index
- indices are non-negative
- source is loose
- target in [0, loose_count())

Empty-slot overwrite allowed.

### on_reserve_empty

Capacity negotiation.

Must satisfy minimum_capacity.

Metadata and payload-side reserve retain the established non-transactional
cross-layer behavior. A coordinated rollback redesign is a separate
architectural decision.

## Re-entry contract

The slot-backing responsibility functions must not re-enter the slot manager.
This is a usage contract rather than a runtime locking mechanism.

Read operations may share unchanged, safely published storage when the backing
payload reads also support concurrent access. Mutation, attribution changes and
destruction require exclusive access. Rank lookup checks safety directly in
`find_by_rank_index`; the separate unchecked `locate_by_rank_index` helper had
no independent callers and has been removed.

## Internal layering

- direct compile-time calls to the protected lower slot-backing layer
- private structural helpers

## Mutation model

Operation:

- pack()

Metadata copy/move/take/clone facilities and facade-facing empty, loose, and
rank traversal helpers are retained capabilities even when a particular
production caller is not currently visible.

## Capacity model

- capacity()
- minimum_safe_capacity()
- capacity_limit()

Operations:

- safe_resize()
- reserve_empty()
- reserve_and_acquire()
- shrink_to_fit()

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

Loose and empty slots expose direct first, last, previous, and next traversal
functions. Traversal follows the corresponding circular list without invoking
slot-backing code.

## Invariants

- exclusive categories
- counts sum to capacity
- lists circular
- high_index valid

## Complexity

- O(1) list operations
- pack(): O(n)

## Alignment with TOrderedSlots

TOrderedSlots:

- full-domain rank
- total reordering

TUnorderedSlots:

- occupied-domain rank
- compaction only

## Out of scope

- thread safety
- payload management
- ordering
- comparator validation
