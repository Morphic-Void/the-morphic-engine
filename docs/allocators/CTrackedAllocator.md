Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    CTrackedAllocator.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# CTrackedAllocator

[`CTrackedAllocator.hpp`](../../core/allocators/CTrackedAllocator.hpp) owns
independent allocations in `TOrderedSlots<memory::CMemoryToken, SAddressKey>`.
Each key is the token's allocated starting address, converted to `std::uintptr_t`
and compared numerically. The ordered slots provide logarithmic exact-address
lookup. No allocation header or caller-supplied size is read when freeing.

The allocator is move-only, uses no exceptions and requires exclusive access
during mutation. Its only data member is the ordered slot container. Index
growth may move tokens but preserves all payload addresses and contents. Tokens
and keys are not exposed for external mutation.

## Initialization and context

```cpp
bool initialise(memory::CMemoryContext& context,
    const std::size_t initial_slot_count = 0u) noexcept;
```

Initialization prepares an empty index with at least 32 slots. A larger initial
slot count reserves additional index capacity; it does not allocate payloads.
The context must be usable and must outlive all owned storage. All three index
backings and every payload use this context. No ambient context is required or
changed, and later operations use the captured context.

Initialization on a ready allocator is rejected, including when empty. A failed
initialization leaves the allocator unchanged and releases all temporary index
storage. Default, moved-from and deallocated objects are empty and unready and
can be initialized with a context. Construction itself does not allocate.

The explicit-context construction path is also available directly on
`TOrderedSlots<T, TKey>` and `TPodVector<T>`. It configures their storage without
allocating; see [movable slots](../containers/MovableSlots.md) and
[TPodVector](../containers/TPodVector.md).

## Allocation and exact-key release

```cpp
void* allocate(const std::size_t bytes,
    const std::size_t alignment = alignof(std::max_align_t)) noexcept;
CTrackedAllocator::EFreeResult free(void* const pointer) noexcept;
```

`allocate()` requires a positive byte count and a nonzero power-of-two alignment.
Requests exceeding the memory substrate's size/alignment representation or
physical-allocation limits fail. Zero-sized requests, invalid alignment and an
unready allocator return `nullptr`. The default alignment is
`alignof(std::max_align_t)`.

Each token uses stride one, retaining the exact requested byte count and
alignment. Context conditioning may increase physical size or alignment; those
extra bytes are not part of the reported logical extent. Successful allocation
returns uninitialized raw storage. There is no zero-fill, object construction,
reallocation, adoption of external pointers or aggregate memory budget.

A candidate token obtains its allocation privately. Its address becomes the
index key, and the pointer is returned only after insertion succeeds. If payload
allocation or insertion fails, the candidate is released and existing entries
remain intact. A late index-growth failure may retain expanded token/key
backings, as allowed by `TOrderedSlots`; physical cost can increase even when
`allocate()` returns null. Failed growth never publishes or leaks its candidate
payload.

| `CTrackedAllocator::EFreeResult` | Meaning |
| --- | --- |
| `released` | The exact live address was erased and its token released |
| `no_action` | The pointer was null |
| `invalid_allocation` | No live entry matches the supplied address |

Interior, foreign and already-freed addresses all produce the single invalid
result, without changing ownership. Validation does not dereference the supplied
pointer. An address reused for a new live allocation cannot identify an older
stale pointer. The index is separately owned management storage, but the
allocator is not a process isolation boundary or an overrun detector.

`allocation_size(pointer)` and `allocation_alignment(pointer)` report the exact
requested size/alignment for a live start, or zero if absent. They use exact-key
lookup and do not accept an interior pointer as an allocation identifier.

## Lifecycle, accounting and traversal

`reset()` frees all payloads and retains the index backings for reuse. Each new
payload still requires a physical allocation. `deallocate()` and destruction
release payloads and all index storage. Neither operation runs destructors for
objects placed in payload memory or invokes vendor cleanup. Audit live entries
before sweeping when outstanding allocations should be reported.

Moves transfer ownership and attribution without allocating or changing payload
addresses. Move assignment first releases the destination's ownership;
self-move is a no-op. A moved-from object is unready and reusable through
`initialise()`. Cross-context reattribution is not exposed.

| Observer | Result | Cost for `N` live allocations |
| --- | --- | --- |
| `is_ready()` | Whether index storage is prepared | Constant |
| `is_empty()` | Whether there are no live payloads | Constant |
| `allocation_count()` | Number of live payloads | Constant |
| `allocation_size(pointer)` | Exact requested extent, or zero | `O(log N)` |
| `allocation_alignment(pointer)` | Requested alignment, or zero | `O(log N)` |
| `allocated_size()` | 64-bit sum of requested bytes | `O(N)` traversal |
| `memory_attribution()` | Index backing plus each live token's physical attribution | `O(N)` traversal |
| `total_owned_size()` | 64-bit total physical bytes from deep attribution | `O(N)` traversal |

The ready allocator has `N + 3` physical allocations: one per payload, plus
metadata, token backing and key backing. Empty spare slots hold default tokens
with no payload ownership. Deep observation counts the three container tokens
and live payload tokens, and charges each physical allocation exactly once.
Logical requested bytes are not added again to the context's physical charges.

`visit_allocations(visitor)` visits live entries in increasing numeric address
order, calling `visitor(const void*, std::size_t bytes, std::size_t alignment)`.
The callback must not mutate the allocator. Traversal allocates no scratch
storage. The visitor method is not declared `noexcept`; the remaining public
operations are nonthrowing.

Exact-key lookup and tree insertion/removal are logarithmic in the live entry
count `N`. Index growth additionally moves a number of descriptors proportional
to retained index capacity `C`. When erasure removes the highest occupied slot,
the slot manager may scan backwards through empty slots to find the new highest
occupied slot. A single free can therefore take `O(C)` time.

Reset erases live entries individually. Its backwards scans traverse at most
the retained slot domain in total, giving an `O(N log N + C)` upper bound.
Complete teardown also destroys the default tokens in spare capacity.

## Example

```cpp
#include "allocators/CTrackedAllocator.hpp"

static bool use_tracked_allocator(memory::CMemoryContext& context) noexcept
{
    CTrackedAllocator allocator;
    if (!allocator.initialise(context)) return false;
    void* const block = allocator.allocate(37u, 64u);
    if (block == nullptr) return false;
    //  Exactly 37 requested bytes; the address is aligned to at least 64 bytes.
    const bool size_matches = allocator.allocation_size(block) == 37u;
    const CTrackedAllocator::EFreeResult result = allocator.free(block);
    return size_matches && (result == CTrackedAllocator::EFreeResult::released);
}
```

## Coverage

[`TrackedAllocator_test_suite.cpp`](../../tests/test_suites/TrackedAllocator_test_suite.cpp)
covers exact size/alignment queries, invalid frees, mixed allocation/free
sequences, address-ordered enumeration, growth with preserved payloads, failure
at every initialization/growth allocation point, retained-index reuse, moves
between contexts, deep accounting and operation with no ambient context.
[`MovableSlots_test_suite.cpp`](../../tests/test_suites/MovableSlots_test_suite.cpp)
also checks direct explicit-context construction, growth, packing and reuse.

Validation on 07 November 2026 passed the ordinary suites on Windows Debug
x64/Win32 and Linux GCC Debug. Each run reported 61,974 tracked-allocator checks
and 1,006 movable-slot checks with zero failures. Build-time policy validation,
Linux CTest policy checks, changed-file line endings and local documentation
links also passed. Local run records are under `build/tracked-allocator-*`.
Release configurations were not rerun for this change.

This allocator is independent of the buddy allocator and SPIRV-Reflect.
Composition with the [buddy chain](TBuddyAllocatorChain.md), allocation-size
thresholds and vendor diagnostics remain integration decisions.
