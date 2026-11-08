Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    THybridAllocator.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    08 Nov 26

# THybridAllocator<TWord>

[`THybridAllocator.hpp`](../../core/allocators/THybridAllocator.hpp) combines a
[`TBuddyAllocatorChain`](TBuddyAllocatorChain.md) with a
[`CTrackedAllocator`](CTrackedAllocator.md). The aliases `CHybridAllocator32` and
`CHybridAllocator64` select the packed buddy word size, independently of pointer
width. It is a move-only byte-storage owner requiring exclusive access.

The wrapper provides generic routing, checked frees and collective ownership.
It contains no vendor callbacks, zeroing, allocation event history or error
reporting policy. SPIRV-Reflect integration remains a separate adapter task.

## Configuration and lazy storage

```cpp
struct SHybridConfig
{
    SBuddyConfig buddy;
    std::size_t buddy_threshold{ 0u };
};

bool initialise(const SHybridConfig& config, memory::CMemoryContext& context) noexcept;
```

`initialise()` validates and normalizes the buddy configuration using the same
allocation-free `TBuddyAllocator<TWord>::resolve_config()` used by the standalone
owner. The slot rounds up to a power of two; the payload budget rounds down.
All standalone representation, footprint and context checks apply. Even a
tracked-only configuration must supply a valid buddy configuration.

The threshold is the **maximum rounded extent** allowed on the buddy path,
after promotion for alignment and minimum slot size. It must not exceed the
normalized payload budget. It is not itself rounded: a threshold of 100 admits
64-byte blocks but routes a 65-byte request to tracked storage. Zero disables
the buddy path. No reflection-specific default configuration is supplied.

Successful initialization stores the normalized configuration and a non-owning
context pointer, without allocating either child owner's storage. `is_ready()`
therefore means configured, rather than physically allocated. `config()` returns
a const reference to those normalized settings, or zeroed settings when unready.
Initialization rejects already-ready owners without mutation.

Each child is initialized only when its path first receives a valid request.
The supplied context must outlive all operations and owned storage, including
the interval between configuration and first allocation. All child allocations
use that context; calls do not consult the later ambient context.

## Allocation routing and alignment

```cpp
void* allocate(std::size_t bytes,
               std::size_t alignment = alignof(std::max_align_t)) noexcept;
```

Zero-size requests, zero/non-power-of-two alignment, unrepresentable sizes or
alignments, and calls on an unready owner return `nullptr`. No storage is
initialized for invalid requests. The caller supplies vendor-specific zero-size
normalization if required.

For valid requests, let `S` be the normalized slot size and
`B = bit_ceil(max(bytes, alignment, S))`. Use the buddy path when both:

- `B <= buddy_threshold`;
- the payload can guarantee the requested alignment, namely
  `alignment <= min(B, max(S, 128))`.

Otherwise use the tracked path, retaining the original requested byte count
and alignment. A buddy request promotes its reserved extent to meet alignment;
it does not add per-allocation metadata. For example, with 8-byte slots and a
128-byte threshold, `(1, 128)` uses a 128-byte buddy block, `(128, 8)` also uses
a buddy block, and `(129, 8)` uses tracked storage. With 8-byte slots, `(1, 256)`
uses tracked storage even when the size threshold permits a 256-byte block.
A slot size of 256 would make that alignment available on the buddy path.

The selected path can grow using its normal rules. Failure returns `nullptr`
without trying the other path. Existing allocations remain valid. Successful
lazy setup or container growth can retain capacity after a later allocation
failure; a failed request never publishes a live allocation. `deallocate()` or
destruction releases retained storage as well.

The default alignment promotes even one-byte requests to at least
`alignof(std::max_align_t)`. Callers wanting finer storage granularity can
explicitly request alignment 1. Successful storage is uninitialized: there is
no `calloc`, copying, object construction or reallocation support.

## Free routing and lifetime

`free(pointer)` returns the wrapper's own `EFreeResult`:

| Result | Meaning |
| --- | --- |
| `released` | An exact live allocation start was released |
| `no_action` | The pointer was null |
| `invalid_allocation` | Any other unmatched, interior or currently freed pointer |

Free first checks the buddy chain. Only `outside_backing` falls through to an
exact-key tracked lookup. An invalid pointer inside a buddy payload is rejected
immediately. No pointer is forwarded directly to the underlying allocator until
its owning child has validated it. As with the child owners, address reuse means
an old pointer cannot be distinguished from a new allocation at the same start.

`reset()` releases tracked payloads and invalidates all buddy allocations while
retaining managers, the chain vector and any tracked index storage. Configuration
and readiness remain intact. `deallocate()` releases both owners completely and
clears configuration/readiness. Destruction releases both owners independently
of supplier cleanup. Neither operation runs destructors for payload objects.

Move construction and assignment transfer configuration, context and both child
owners. Payload addresses remain stable. A moved-from wrapper is unready and
can be initialized again. Assignment releases the destination's previous
ownership in its original context; self-move leaves ownership intact.

## Observation and accounting

Exact-address queries return zero or `EAllocationKind::none` for invalid starts.

| Observer | Meaning |
| --- | --- |
| `allocation_size(pointer)` | Buddy reserved extent, or tracked requested bytes; both are usable extents |
| `allocation_alignment(pointer)` | Guaranteed buddy alignment or tracked storage alignment; not an original-request history |
| `allocation_kind(pointer)` | `none`, `buddy` or `tracked` |
| `allocation_count()` | Total live logical allocations across both owners |
| `allocated_size()` | Sum of usable extents, mixing rounded buddy blocks with exact tracked byte counts |
| `manager_count()` | Retained buddy manager count |
| `tracked_allocation_count()` | Live independently tracked allocation count |
| `memory_attribution()` | Combined deep attribution of both owners, counting each physical allocation once |
| `total_owned_size()` | Conditioned deep physical footprint, including payload, padding, management and container capacity |

Logical extents are not physical ownership or original requested-byte totals.
The wrapper deliberately retains no requested-size history for buddy blocks.
Before either lazy path has allocated, attribution has no source and no physical
allocation despite `is_ready()` being true. The configured context pointer is
not itself an owned allocation. Existing empty child token descriptors can still
appear in `token_count`.

`visit_allocations(visitor)` calls
`visitor(pointer, usable_extent, guaranteed_alignment, kind)`, first in buddy
manager creation/address order, then in tracked address order. This is not a
single global address ordering. The visitor must not mutate the wrapper.

Routing adds constant work to allocation. Buddy operations retain the chain's
linear manager search. Free and exact-address queries can search the chain
before the tracked lookup; tracked erasure retains its existing potential
backward slot scan. Aggregate statistics, visitation and teardown retain their
children's traversal costs. No additional cross-owner index or synchronization
is introduced.

## Validation

`HybridAllocator_test_suite.cpp` covers both packed word widths: configuration
rejection without allocation, lazy readiness, normalized sizes, size/rounding/
alignment routing boundaries, tracked-only routing, pointer validation, mixed
growth, payload preservation, reset/reuse, collective teardown, failure at each
initial allocation step, no fallback after buddy growth failure, and moves across
contexts. Visitor membership, order, extents and alignment are also checked
against an independent list built from issued requests and returned pointers,
before/after partial frees, reset and reuse. Every injected initial failure is
tested with immediate explicit teardown and scope destruction, without a retry.
A rejecting ambient context checks explicit-context isolation.

The standalone buddy suite continues to validate the common configuration and
layout implementation.

Windows x64 Debug and Linux Debug ordinary suites passed on 08 November 2026,
including 4,256 hybrid-specific checks per platform. Build-time policy validation,
Visual Studio project/filter XML, line endings and diff whitespace checks passed.
