Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    memory_contexts.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    08 Nov 26

# Owning container memory contexts

Owning containers accept an explicit `memory::CMemoryContext&` constructor
argument. This selects the context for their backing storage without allocating
storage or requiring an ambient context. The context is borrowed and must
remain alive while the owner uses it, including deallocation and later reuse.

| Container family | Context-controlled backing |
| --- | --- |
| `TVector<T>`, `TPodVector<T>`, `TPodFifo<T>` | Contiguous element storage |
| `TInstance<T>` | Storage for the contained object |
| `CByteBuffer`, `CByteRectBuffer` | Byte/row storage, including replacement allocations |
| `CBumpBuffer` | Stable block directory and blocks |
| `CSimpleString`, `CStringBuffer` | String/packed byte storage |
| `CStableStrings` | Packed strings and all four indexing vectors, including sort replacements |
| `TPodOrderedSlots`, `TPodUnorderedSlots`, `TOrderedSlots`, `TUnorderedSlots` | Metadata and value backing, plus keys for ordered containers |
| `TOrderedCollection`, `TUnorderedCollection` | Metadata, slot records, stable object storage and its directory, plus ordered keys |

## Capture and retention

Explicit-context construction allocates nothing. It configures direct backing
tokens and passes the context to composite members. Owners with deferred storage
configuration retain the context until their initialization parameters are known.
No temporary ambient-context override is used.

Default construction selects the ambient context where available. Changing the
ambient context later does not redirect a configured owner's initialization,
growth, replacement allocations or reuse. Default construction without an ambient
context does not provide the explicit constructor's usable-storage guarantee;
use the explicit overload when ambient provisioning is absent.

Deallocation releases physical storage but retains context selection for reuse.
Stable collections also retain their payload token's configuration; later
initialization can choose a new block configuration using that same context.
Other reset/clear behaviour, including whether capacity is retained, remains
specific to each container.

Moves transfer backing ownership and its attribution. Move assignment releases
the destination's previous ownership first. A moved-from container retains its
context selection for reuse. Swapping owners, where supported, swaps their
backing ownership and associated contexts.

`memory::reattribute(owner, &target)` is the explicit operation for changing
context. It performs the required allocator/accounting checks and updates every
backing member, including unallocated members. Replacement and reuse then use
the new context. An empty owner's `memory_attribution()` describes no physical
allocation and does not expose its retained context; reuse demonstrates that
selection.

## Backing ownership and contained objects

Context selection is shallow with respect to contained objects. A vector or
collection controls its own backing, but does not implicitly pass the context
to allocations owned by its elements. Supply such contexts through element
construction or the higher-level owner's explicit deep-attribution policy.

For `TInstance`, the context-taking constructor configures the instance's storage;
arguments to `emplace()` independently configure the contained object. Existing
factory functions keep their ordinary argument-forwarding semantics.

Views own no storage and need no memory-context constructor. `TProcessLifetime`
stores its object inline; its existing forwarding constructor can pass a context
to that object's constructor. Independently returned objects, such as a slot
rank map, remain separately constructed owners rather than container backing.

## Replacement and failure behaviour

Byte-buffer copy construction from a view, FIFO growth and string sorting all
allocate replacements using the destination owner's context. The source view's
storage and the calling thread's ambient context do not choose that destination.
`CStableStrings::sort()` prepares each replacement from the corresponding
existing backing's context, preserving attribution even during temporary overlap.

This contract does not change each operation's failure guarantee. Transactional
replacement keeps existing storage when allocation fails. APIs that release old
contents before preparing new storage retain their existing behaviour. Context
selection survives failure and subsequent retry; allocation failure never falls
back to another context.

## Example

```cpp
static bool make_buffer(memory::CMemoryContext& context) noexcept
{
    CByteBuffer buffer{ context }; //  No allocation yet.
    if (!buffer.allocate(128u)) return false;
    if (!buffer.reserve(1024u)) return false;
    buffer.deallocate();
    return buffer.allocate(64u);  //  Reuses the same context.
}
```

The same context-taking form is available on every owning type listed above.
Existing constructors taking contents or capacities remain available and use
their default context provisioning; the context-taking constructor prepares an
empty owner for its normal population API.

## Coverage

[`ContainerContexts_test_suite.cpp`](../../tests/test_suites/ContainerContexts_test_suite.cpp)
exercises all 16 owning types with absent and rejecting unrelated ambient
contexts. It checks allocation-free construction, initialization/growth, live
and empty reattribution, moves, moved-from reuse, deallocation/reuse, default
context capture and final accounting. Replacement tests cover byte/row copying
and failure at each of the five stable-string sort allocation points.

See [memory and attribution](../memory/memory_subsystem.md) for the common token
and transfer contracts. Individual container references describe their own
element lifetimes, capacity policies and allocation-failure guarantees.
