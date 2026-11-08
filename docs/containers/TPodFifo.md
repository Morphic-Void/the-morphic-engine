Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   TPodFifo.md  
Author: Ritchie Brannan  
Date:   20 Apr 2026  

# TPodFifo<T>

Owning storage follows the [container memory-context contract](memory_contexts.md):
explicit-context construction allocates nothing, and growth, replacement and
reuse retain the selected context until ownership transfer or reattribution.

## Overview

This document defines FIFO storage utilities implemented in
TPodFifo.hpp.

The layer provides growable FIFO storage for trivially copyable
elements.

The implementation is noexcept. Allocation failure is reported by
return value. Accessors are fail-safe.

## Requirements and scope

- Requires C++17 or later.
- No exceptions are used.
- T must be non-const and trivially copyable.
- Storage is contiguous T storage interpreted as FIFO contents through
  size and read-index metadata.
- Sizes, capacities, and indices are expressed in elements.

Scope:

- Models single-threaded FIFO storage only.
- Does not perform construction or destruction.
- Does not support non-trivial relocation semantics.
- Does not provide transport semantics, blocking semantics, or
  random-access array semantics.
- Higher-level element meaning belongs in wrapper layers.

## Storage model

Ownership is provided by `memory::CMemoryToken`.

The container stores:

- backing storage token
- m_size
- m_capacity
- m_read_index

The write position is derived and is not stored.

While ready:

- storage exists
- m_size <= m_capacity
- m_read_index < m_capacity
- m_read_index == 0 when m_size == 0
- logical FIFO contents begin at m_read_index
- logical FIFO contents occupy m_size elements with at most one wrap

## Observation model

- accessors are fail-safe
- observers reflect container invariants
- size == 0 reports empty even if capacity != 0
- size == 0, capacity != 0 is a valid ready state

data() returns the backing storage base address.

data() does not guarantee that the logical FIFO contents are physically
packed from storage index 0. The logical FIFO sequence is packed only
after pack() or after an operation that normalises layout.

available() reports currently writable space in the current allocation:

    capacity() - size()

available() is not a growth guarantee. It does not account for later
reallocation.

## State model

Canonical empty:

    data == nullptr
    size == 0
    capacity == 0
    read_index == 0

Ready:

    data != nullptr
    size <= capacity
    capacity != 0
    read_index < capacity
    size != 0 or read_index == 0

After pack():

- contents are physically packed from storage index 0
- m_read_index == 0

## FIFO operation model

push_back(), pop_front(), and discard_front() are all-or-nothing.

push_back():

- appends elements at the logical back of the FIFO
- fails if the full requested count cannot be accepted in the current
  allocation
- does not reallocate automatically

pop_front():

- removes elements from the logical front of the FIFO
- fails if the full requested count is not currently present

discard_front():

- removes elements from the logical front without copying them to a destination
- fails if the full requested count is not currently present

Single-element, pointer-count, and view overloads follow the same FIFO
semantics.

Pointer rules:

- a raw pointer-count operation with zero count succeeds without requiring
  allocated storage and does not inspect the pointer
- a non-zero count requires a non-null pointer
- a non-zero source or destination within the FIFO's own backing allocation is
  rejected
- a view overload requires a valid view; a zero-length invalid view fails

Removing the final element, whether by pop_front() or discard_front(), resets
m_read_index to 0.

## Packing and layout normalisation

pack() is a mutating normalisation operation.

It rewrites the current logical FIFO contents into packed physical order
starting at storage index 0 and resets m_read_index to 0.

pack() does not change:

- logical size
- capacity

pack() may be required before treating data() as a packed contiguous
logical FIFO sequence.

## Capacity model

This is a growable container.

Capacity uses the same general growth-policy family as the other POD
containers.

allocate(capacity):

- allocates storage with logical size 0
- sets read index to 0
- if capacity matches the current capacity, the container is reset to
  empty within the existing allocation

reallocate(capacity):

- requires capacity >= size()
- may allocate new storage and repack logical contents
- may also repack in place when capacity matches the current capacity
- resets m_read_index to 0 after successful normalisation

reserve(minimum_capacity):

- ensures capacity >= minimum_capacity
- does not mutate layout if no growth is required
- succeeds without allocating when the requested capacity is already satisfied

ensure_free(extra):

- ensures room for at least extra additional elements without requiring
  immediate reallocation afterward

shrink_to_fit():

- reduces capacity to match size()
- deallocates when size() == 0

deallocate():

- releases storage
- restores canonical empty state

## Reallocation and preservation model

Successful reallocation preserves the logical FIFO sequence, not the
previous physical layout.

The resulting layout is packed from storage index 0.

This differs from reserve() when no growth is required:

- reallocate(current capacity) may normalise layout
- reserve(current or lower capacity) leaves layout unchanged

## Copy and representation model

- element transfer uses byte-wise copy
- no construction or destruction is performed
- storage is interpreted as tightly packed T elements
- byte-level equality is not guaranteed for semantically equal values

## Non-goals and caveats

- This container is single-threaded
- It is not a transport
- It does not provide overwrite-on-full behaviour
- It does not provide implicit growth during push_back()
- data() is not a packed logical-sequence guarantee unless pack() or
  layout-normalising reallocation has been performed
