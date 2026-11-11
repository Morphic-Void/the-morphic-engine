Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    TBuddyAllocatorChain.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# TBuddyAllocatorChain<TWord>

[`TBuddyAllocatorChain.hpp`](../../foundation/allocators/TBuddyAllocatorChain.hpp)
owns a homogeneous collection of [buddy managers](TBuddyAllocator.md) in
`TVector<TBuddyAllocator<TWord>>`. Aliases `CBuddyAllocatorChain32` and
`CBuddyAllocatorChain64` select the packed word width. The vector is the chain's
only data member. All managers share one normalized slot/backing configuration
and explicit memory context, obtained from the first retained manager.

The chain is move-only and requires exclusive access. Manager objects are not
exposed. Vector relocation moves ownership tokens while preserving all live
payload addresses.

## Initialization and growth

`initialise(config, context)` uses the standalone `SBuddyConfig` normalization
and validation rules. It prepares one empty manager and vector storage, then
publishes both together. Failure at either allocation point releases temporary
ownership and leaves the chain unchanged. Initialization is rejected while the
chain is ready, including when it contains no live allocations.

The vector uses its explicit-context constructor, so initialization works with
no ambient context. The context must outlive the vector allocation and every
manager allocation. Later growth uses that same context without consulting the
calling thread's ambient context.

`allocate(bytes)` scans managers in creation order and uses the first whose
largest available block can accommodate the request. The selected manager then
chooses its smallest sufficient available order. Selection is not a global
best-fit search across managers.

If no existing manager fits, the chain prepares a new manager privately and
allocates the requested block from it. Only successful insertion into the vector
publishes the manager and permits the pointer to be returned. Manager allocation
or vector-growth failure destroys the candidate, preserving existing live
allocations, manager count and vector capacity. Existing blocks and their
contents survive successful descriptor relocation.

Growth also occurs when free space is fragmented. Zero-sized requests, requests
larger than one manager's backing and requests on an unready chain return
`nullptr` without physical allocation. Reaching the vector's element limit also
prevents growth. A request never spans managers. There is no aggregate payload
budget, automatic empty-manager removal or exceptional large-block fallback.

## Pointer routing and lifecycle

`free(pointer)` scans managers until one reports a result other than
`outside_backing`. It preserves the standalone `EFreeResult` categories;
`free(nullptr)` returns `no_action`. `allocation_size(pointer)` returns the
rounded extent for an exact live start in any manager, or zero otherwise.
Routing is linear in manager count and uses no external address index.

Default, moved-from and deallocated chains are empty and unready.
Nonthrowing moves preserve payload addresses and original context attribution.
Move assignment first destroys the destination's previous ownership; self-move
is a no-op. A moved-from chain can be initialized again with another context.

`reset()` invalidates all live blocks while retaining every manager and vector
capacity. Managers are reused in creation order by subsequent allocation.
Freeing the last block likewise retains the manager. `deallocate()` and
destruction release the vector storage and every manager's management/payload
allocation, even if logical allocations remain. No payload destructor or vendor
cleanup callback is invoked. Enumerate outstanding blocks before reset or
teardown if a leak audit is required.

The standalone alignment, raw-storage and validation limitations apply to every
block. Every manager has a payload base aligned to at least 128 bytes, so
128-byte blocks are 128-byte aligned even with smaller slots. Minimum-slot
alignment remains guaranteed for the smallest blocks. Exact requested sizes
are not recorded, and reused addresses do not identify stale pointers.

## Observers and accounting

| Observer | Meaning | Cost for `M` managers |
| --- | --- | --- |
| `is_ready()` | At least one manager is retained | Constant |
| `is_empty()` | No live blocks across all managers | `O(M)` |
| `manager_count()` | Retained managers, including empty ones | Constant |
| `manager_capacity()` | Manager descriptor capacity in the vector | Constant |
| `slot_size()`, `backing_size()` | Per-manager normalized sizes, or zero when unready | Constant |
| `allocation_count()` | 64-bit sum of live block counts | `O(M)` |
| `allocated_size()` | 64-bit sum of rounded live extents | `O(M)` |
| `largest_available_block()` | Maximum free extent in any manager, or zero | `O(M)` |
| `memory_attribution()` | Deep observation of vector storage and all managers | `O(M)` |
| `total_owned_size()` | 64-bit physical byte total from deep attribution | `O(M)` |

`backing_size()` is a payload budget for each manager, not a limit on the total
chain. Spare vector capacity reserves descriptors only; it does not preallocate
their backing memory. The ready chain has `M + 1` physical allocations: one per
manager plus the vector storage. Accounting includes all conditioned management,
padding and payload costs exactly once; logical reserved bytes are not added as
separate context charges. An unready chain owns no physical allocation, while
its empty vector token remains observable. Cross-context reattribution is not
exposed.

`visit_allocations(visitor)` uses manager creation order, then address order
within each manager. It does not promise global address order. Callback
arguments and mutation restrictions match the standalone owner. The visitor
method is not declared `noexcept`; the other public operations are nonthrowing.
Its cost is the sum of the managers' traversals.

Allocation searches up to `M` summaries before performing a bounded tree
operation. Growth additionally initializes one full management region and may
move `M` descriptors. Pointer routing checks up to `M` payload ranges before
validating a matching tree path. Reset clears each manager's index; deallocation
destroys each manager and releases vector storage.

## Example

```cpp
#include "allocators/TBuddyAllocatorChain.hpp"

static bool use_chain(memory::CMemoryContext& context) noexcept
{
    CBuddyAllocatorChain32 chain;
    if (!chain.initialise({ 16u, 64u }, context))
    {
        return false;
    }
    void* const first = chain.allocate(64u);
    if (first == nullptr) return false;
    void* const second = chain.allocate(64u);
    if (second == nullptr) return false;
    //  Each full-size block occupies its own manager.
    const bool grew = chain.manager_count() == 2u;
    const EFreeResult result = chain.free(first);
    chain.reset();          //  Invalidates second; retains both managers.
    chain.deallocate();     //  Releases both backings and descriptor storage.
    return grew && (result == EFreeResult::released);
}
```

## Coverage and related work

[`BuddyAllocatorChain_test_suite.cpp`](../../tests/test_suites/BuddyAllocatorChain_test_suite.cpp)
tests both word widths: fragmented growth, initialization and insertion failure,
descriptor relocation, pointer routing, reset/reuse, moves between contexts,
deep accounting and operation without an ambient context. The
[TVector reference](../containers/TVector.md) describes the underlying relocation
and explicit-context contracts.
