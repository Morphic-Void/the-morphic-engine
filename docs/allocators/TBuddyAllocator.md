Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    TBuddyAllocator.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# TBuddyAllocator<TWord>

[`TBuddyAllocator.hpp`](../../core/allocators/TBuddyAllocator.hpp) provides a
move-only owner of one buddy-managed payload. `TWord` must be `std::uint32_t` or
`std::uint64_t`; the corresponding aliases are `CBuddyAllocator32` and
`CBuddyAllocator64`. Both work on 32-bit and 64-bit hosts.

The object contains only a `memory::CMemoryToken`. Its separate owned allocation
contains management data, alignment padding and payload, in that order. The
[packed index](buddy_index.md) and its routing summaries reside in the management
region. There are no per-block headers in the payload and no further physical
allocations during logical allocation/free operations.

## Configuration and initialization

```cpp
struct SBuddyConfig
{
    std::size_t minimum_slot_size{ 0u };
    std::size_t backing_size_limit{ 0u };
};

bool initialise(const SBuddyConfig& config, memory::CMemoryContext& context) noexcept;
```

Let `S` be the minimum slot size rounded up to a power of two, and `P` the
backing limit rounded down to a power of two. For example, `{ 17u, 1000u }`
produces `S = 32` and `P = 512`. The backing limit applies to usable payload;
`total_owned_size()` reports the separate physical cost including overhead.

Initialization rejects zero inputs, slot-rounding overflow, `S > P`, an unusable
context, excessive slot count or a footprint that the memory substrate cannot
represent or condition. The maximum slot count is 16,384 for 32-bit words and
131,072 for 64-bit words. A reduced payload uses a smaller active tree within the
same full-size management layout; it does not reduce management storage.

The unconditioned footprint is `align_up(k_management_size, S) + P`, with a
requested allocation alignment of `max(S, alignof(SManagement))`. Context
conditioning may increase the physical cost. `k_management_size` includes
platform-dependent structure padding; use the constant rather than a hard-coded
management byte count.

Initialization prepares storage privately and returns `false` without changing
the owner on failure. It rejects every already-ready owner, even one with no
live allocations. Call `deallocate()` before changing configuration or context.
The explicit context must outlive the allocation; later operations do not
rediscover the caller's ambient context.

## Allocation and alignment

`allocate(bytes)` returns an uninitialized raw block, or `nullptr` when unready,
the request is zero or greater than `P`, or no sufficiently large free block
exists. A successful request reserves the smallest power-of-two extent that
covers the request and is at least `S`. Failure does not change logical state.

The index chooses the smallest available sufficient order, then the lowest
address at that order, splitting if necessary. This can prefer an existing small
block over a lower-address large block. Fragmentation can prevent a request
despite sufficient aggregate free bytes.

Every returned address is aligned to `S`. Larger block offsets are aligned to
their extents relative to the payload base, but absolute alignment greater than
`S` is not guaranteed. There is no per-request alignment argument: choose `S`
large enough for the required alignment. There is no object construction,
zero-filling, reallocation or original-request-size history.

## Free validation and size queries

`free(pointer)` requires neither a size nor an alignment argument:

| `EFreeResult` | Meaning |
| --- | --- |
| `released` | An exact live allocation start was found and freed |
| `no_action` | The pointer was null |
| `outside_backing` | The pointer is outside the current payload, including an unready owner |
| `invalid_allocation` | The pointer lies inside the payload but is not an exact live allocation start |

The payload interval excludes its end address and management/padding region.
Validation uses integer addresses and the internal reservation tree; it does not
dereference the supplied pointer. Interior pointers, misaligned pointers and
already-freed starts are rejected without mutation. A freed address that has
since been reused cannot be distinguished from the new live allocation by a
raw pointer alone.

`allocation_size(pointer)` returns the rounded usable extent for an exact live
start, or zero otherwise. It does not modify state. Validation assumes intact
management data; this allocator does not detect arbitrary payload overruns or
isolate an external component from the process address space.

## Lifecycle and observation

Default, moved-from and deallocated owners are empty and unready. Copying is
disabled. Move construction and assignment are nonthrowing, preserve payload
addresses and attribution, and leave the source unready. Move assignment first
releases the destination's old storage; self-move preserves it.

`reset()` clears the index and logical counters while retaining the backing,
configuration and context. It invalidates every previous allocation without
zeroing the payload. `deallocate()` and destruction release the entire physical
allocation, including outstanding blocks. These operations do not run payload
object destructors or vendor cleanup callbacks. Audit live allocations first
when teardown should report leaks.

| Observer | Result |
| --- | --- |
| `is_ready()` | Whether physical storage is owned |
| `is_empty()` | Whether there are no live logical allocations |
| `slot_size()`, `backing_size()` | Normalized `S` and `P`, or zero when unready |
| `allocation_count()` | Number of live blocks |
| `allocated_size()` | Sum of rounded live extents, in bytes |
| `largest_available_block()` | Largest currently free extent, or zero |
| `total_owned_size()` | Physical owned bytes as `std::uint64_t` |
| `memory_attribution()` | Observation of the single owning token |

Logical suballocations do not add physical allocation charges to the context.
The owner exposes no reattribution operation.

`visit_allocations(visitor)` calls `visitor(pointer, bytes)` once for every live
block, in increasing address order. The pointer is `const std::uint8_t*` and the
extent is `std::size_t`. The callback must not mutate the allocator. This method
is not declared `noexcept`; the other public operations are nonthrowing. All
access requires external coordination with mutations; words are not atomic.

## Example

```cpp
#include "allocators/TBuddyAllocator.hpp"

static bool use_buddy(memory::CMemoryContext& context) noexcept
{
    CBuddyAllocator32 allocator;
    if (!allocator.initialise({ 16u, 4096u }, context))
    {
        return false;
    }
    void* const block = allocator.allocate(17u);
    if (block == nullptr) return false;
    //  The block has 32 usable bytes and at least 16-byte alignment.
    const bool size_matches = allocator.allocation_size(block) == 32u;
    const EFreeResult result = allocator.free(block);
    return size_matches && (result == EFreeResult::released);
}
```

## Costs and coverage

Allocation, free and exact-start lookup perform work bounded by the active tree
height, at most 15 or 18 node levels. Search uses at most two routing decisions
before selecting a packed-word candidate. Reset clears the full management
arrays, including inactive capacity. Scalar observers are constant-time;
enumeration traverses reserved tree paths and can visit a number of nodes
proportional to the active slot count.

[`BuddyAllocator_test_suite.cpp`](../../tests/test_suites/BuddyAllocator_test_suite.cpp)
covers both word widths, independent geometry expectations, routing state,
split/merge, invalid frees, capacity recovery, allocation failures, payloads,
enumeration, moves and attribution. See the
[design validation record](../memory/buddy_allocator_design.md#staged-delivery-and-acceptance)
for completed platform runs. For automatic growth across backings, use
[TBuddyAllocatorChain](TBuddyAllocatorChain.md).
