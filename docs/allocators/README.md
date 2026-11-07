Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    README.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# Allocators

The headers in `core/allocators/` provide buddy allocation of raw byte storage,
checked individual frees and collective reclamation. These references describe
the implemented system.

| Header | Reference | Responsibility |
| --- | --- | --- |
| `buddy_index.hpp` | [Packed index](buddy_index.md) | Internal reservation tree, exact-order availability, routing and allocation-boundary recovery |
| `TBuddyAllocator.hpp` | [Standalone owner](TBuddyAllocator.md) | One combined management/payload allocation, byte requests, pointer validation and accounting |
| `TBuddyAllocatorChain.hpp` | [Chaining owner](TBuddyAllocatorChain.md) | `TVector` ownership of managers, growth, pointer routing and collective lifecycle |

Use `CBuddyAllocator32` or `CBuddyAllocator64` when one fixed backing allocation
is sufficient. Use `CBuddyAllocatorChain32` or `CBuddyAllocatorChain64` when the
owner should add managers as existing ones fill or become fragmented. The
suffix selects the packed word width, independently of the host pointer width.
The packed index is an implementation detail rather than a byte-allocation API.

Both owners are move-only and require exclusive access. Their explicit memory
context must outlive all owned storage. Moving an owner preserves live payload
addresses. `reset()` invalidates live allocations while retaining storage;
`deallocate()` and destruction release storage without calls to an external
supplier. Neither owner constructs or destroys objects placed in the payload.

The smallest slot rounds up to a power of two; the payload backing budget rounds
down. Management data and alignment overhead are additional physical costs.
Allocated-size queries report rounded block extents, not original request sizes.
See the standalone reference for alignment and free-result details, and the
chain reference for growth and aggregate accounting.

Related references:

- [Memory and attribution](../memory/memory_subsystem.md).
- [TVector](../containers/TVector.md), which owns the chain's manager descriptors.
- [Buddy allocator and SPIRV-Reflect design plan](../memory/buddy_allocator_design.md),
  including rationale, validation records, shader measurement and later integration.

Shader allocation measurement, exceptional large-block ownership and
SPIRV-Reflect integration are separate future stages.
