Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    MovableSlots.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# Movable slot containers

`TUnorderedSlots<T>` and `TOrderedSlots<T, TKey>` own movable objects at
reusable slot indices. They use the same slot managers as the POD facades.
The ordered form stores unique keys, supports keyed lookup, and traverses
occupied slots in key order. The unordered form traverses the occupied list.

## Elements and ownership

Each backing slot contains a constructed `T`. The slot manager tracks
occupancy, and an unoccupied slot holds a default value. Use a type with an
allocation-free default state and nonthrowing default construction, move
construction, move assignment, and destruction. Assigning the default value
must release any ownership previously held by that slot. The copy insertion
overload requires nonthrowing copy assignment. Ordered keys are trivially
copyable, support nonthrowing copy assignment, and use the same
`relationship()` contract as `TPodOrderedSlots`.

`initialise()` creates an empty slot domain with at least 32 slots.
`insert()` returns the acquired index or `-1` if acquisition fails. An
ordered insertion with a duplicate key fails before assigning the payload.
The caller must keep the source and any key argument valid throughout
reservation. In particular, a reference into the container's backing may
become invalid if it grows.

`erase()` releases a slot and assigns its default value. This releases
owned allocations for types such as `CByteBuffer` and
`memory::CMemoryToken`. Erasing an already empty slot fails.
`deallocate()` destroys the backing values and releases the container's
storage. Moving the container transfers the backing and metadata, preserving
the addresses of the occupied objects. The source can be initialised again.

## Growth and packing

Growth preserves existing slot indices and may move the objects. Reservation
follows the POD facades' allocation sequence. If a later allocation fails,
the container may retain expanded backing. The original entries and slot
indices survive, and an incoming owner from outside the container retains
its value. Any pointers into backing that grew must be reacquired, including
key pointers in the ordered form.

`pack()` compacts unordered values into the lowest indices in their original
slot-index order. `sort_and_pack()` places the ordered values and keys in
ascending key order at the lowest indices. Packing moves values in place
without a backing allocation. Acquire a rank map before packing to translate
old slot indices. The unordered map covers occupied entries, while the
ordered map describes the full slot domain. Packed entries may have new
indices and object addresses. Payload addresses follow the element's move
contract. For example, moving a `CByteBuffer` preserves its byte address.

## Attribution

`memory_attribution()` covers the container's metadata, value backing, and
key backing where present. Attribution is shallow. Allocations owned by
individual elements keep their own context when the container is
reattributed.
