Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   BumpBuffer.md  
Primary implementation: OpenAI Codex  
Date:   27 Oct 26  

# BumpBuffer

## Overview

This document defines the behaviour of CBumpBuffer, implemented in
BumpBuffer.hpp.

The class provides an owning bump allocator over fixed-size,
address-stable byte blocks. Individual requests receive contiguous raw
storage. Further requests may allocate additional blocks without moving
existing allocations.

Use the buffer for groups of temporary allocations that can be discarded
together. Resetting the buffer retains its blocks for reuse. Individual
allocations cannot be freed.

The implementation is noexcept. Buffer allocation reports failure through
a bool return value; bump allocation reports failure through nullptr.
Changing the buffer configuration is destructive, as described below.

## Requirements and scope

- Requires C++17 or later.
- No exceptions are used.
- Storage is untyped and byte-addressable.
- Sizes and alignment values are expressed in bytes.
- Block size and configured alignment must be non-zero powers of two.
- Callers manage object construction and destruction when using typed objects.
- Storage is not zero-filled, including after reset.
- Concurrent access to a shared buffer requires external synchronisation.

## Core concepts

### Ownership and blocks

CBumpBuffer owns its backing storage through memory::CMemoryToken in
stable mode. allocate prepares the first block. bump consumes space in
the current block, advancing to another block when necessary.

Each bump fits entirely within one block. A request larger than the
configured block size fails. When a request does not fit after alignment,
the remaining tail is skipped until the next reset. Retained blocks are
reused before new storage is allocated.

The blocks do not form one contiguous byte range. There is no whole-buffer
data pointer or view.

### Status model

The class exposes three status queries:

- is_valid reports whether the token and allocation statistics satisfy
  the buffer invariants.
- is_empty reports that there are no current bump allocations.
- is_ready reports a valid buffer with allocated backing storage.

An empty buffer can be ready. This is the normal state immediately after
successful allocate or after reset.

### Default and deallocated states

Default construction performs no allocation and selects no configuration.
The buffer is valid and empty, but not ready. All statistics, block_size,
and align return zero.

deallocate clears the allocations and releases the blocks, while retaining
the block size and alignment. The buffer remains valid and empty, but is
not ready. A successful allocate is required before bump can be used again.

## Alignment model

Configured alignment is the guaranteed alignment of each block origin.
It may be greater than the block size.

allocate(block_size, align):

- defaults align to alignof(std::max_align_t) when omitted
- requires both arguments to be non-zero powers of two
- rejects unsupported token configurations without changing existing state
- treats every alignment change as a configuration change, including a decrease

bump(size, align):

- defaults align to zero, selecting the configured block alignment
- requires an explicit non-zero alignment to be a power of two no greater
  than the configured alignment
- inserts leading padding when required to align the returned address
- does not round up the requested payload size or add trailing padding

Passing zero alignment to allocate is invalid. Its default argument is a
specific alignment, not a request to retain the previous setting.

## Buffer behaviour

### Allocation and reconfiguration

allocate(block_size, align):

- returns true when the buffer is ready for bump allocation
- resets current bump allocations and their statistics
- with matching settings and existing storage, retains all blocks without
  calling the underlying allocator
- with matching settings but no storage, allocates the first block
- with changed settings, releases all existing storage and attempts to
  allocate one fresh block
- leaves state unchanged when the requested configuration is invalid

If a fresh block cannot be allocated, the buffer is empty and not ready,
with zero buffers and allocation statistics. The requested valid
configuration is retained for a later retry. When settings changed, the
old allocations have already been discarded and cannot be recovered.

Configuration and growth remain subject to the memory token's limits,
including memory::k_byte_size_ceiling and directory capacity limits.

### Bump allocation

bump(size, align):

- requires a ready buffer
- returns a pointer to size contiguous bytes on success
- fails for zero size, size greater than block_size, or invalid alignment
- uses the next retained block, or allocates another block, if the current
  block cannot accommodate the request
- preserves earlier allocations and their contents during growth
- leaves the cursor, statistics, backing storage, and earlier allocations
  unchanged on failure

Failure of one bump does not undo earlier successful bumps. The caller
can reset the buffer when abandoning a group of requests.

### Reset

reset():

- discards all current bump allocations
- rewinds allocation to the start of the first block
- clears all three allocation counters
- retains configuration and every backing block
- does not clear stored bytes or invoke object destructors

Pointers from earlier bumps must no longer be used as live allocations
after reset, even when the same addresses are returned by later bumps.

### Deallocation and lifetime

deallocate():

- discards current allocations and clears their statistics
- releases all backing blocks and token directory storage
- retains configuration for a later allocate call

Destruction releases owned storage automatically. Copy construction and
copy assignment are disabled.

Move construction and move assignment transfer storage, configuration,
and statistics without moving the allocated bytes. Pointers into the
source storage remain usable through the new owner. Move assignment
releases any storage previously owned by the destination. The moved-from
buffer is empty and not ready, retains its configuration, and can be
prepared again with allocate. Self-move assignment has no effect.

## Statistics

Allocation statistics describe successful bumps since the most recent
reset or allocation/reconfiguration reset:

- allocation_count: number of successful bump requests
- allocated_size: sum of requested payload bytes, excluding alignment padding
- allocated_size_with_alignment: payload bytes plus leading alignment
  padding consumed by successful bumps
- buffer_count: number of currently retained backing blocks, including
  blocks unused after reset

The aligned total excludes skipped block tails, unused capacity, and the
memory token's directory. It is not the total backing allocation size.
buffer_count excludes the directory and is unchanged by reset.

For example, in a 64-byte block, bump(3, 1) followed by bump(5, 8) produces
two allocations, 8 payload bytes, and 13 bytes including alignment. The
second allocation starts at offset 8. After reset, bump(60, 1) followed by
bump(8, 1) uses two blocks but reports 68 bytes for both byte totals: the
skipped 4-byte tail is not alignment padding.

## Usage

Include containers/BumpBuffer.hpp directly, or use the containers.hpp
convenience header. Keep the buffer alive across repeated work to reuse
its retained blocks.

```cpp
#include <cstring>

#include "containers/BumpBuffer.hpp"

bool prepare_scratch(CBumpBuffer& scratch) noexcept
{
    //  Repeated calls with these settings reset and retain existing blocks.
    if (!scratch.allocate(4096u, 64u)) return false;

    void* const records = scratch.bump(160u, 16u);
    void* const text = scratch.bump(37u, 1u);
    if ((records == nullptr) || (text == nullptr))
    {
        scratch.reset();
        return false;
    }

    //  Initialise and consume the scratch storage before the next reset.
    std::memset(records, 0, 160u);
    std::memset(text, 0, 37u);
    return true;
}
```

After successful return, scratch owns two bump allocations in its first
block; any other retained blocks remain available. The caller may call
reset when that work is finished, or call
prepare_scratch again to reset through allocate. Use deallocate when the
retained storage is no longer needed.

For typed objects, request sizeof(T) bytes with alignof(T), construct the
object in the returned storage, and destroy it before reset, reconfiguration,
deallocation, or destruction of the owning buffer. The configured block
size and alignment must support that request.

## Memory accounting

The first valid allocate configuration selects the ambient memory context
unless a context was already installed by ownership-transfer infrastructure.
Reconfiguration preserves the token's existing context.

memory_attribution observes all owned backing allocations, including the
token directory and allocator-conditioned sizes. Use this for backing
memory accounting; use the allocation statistics for bump usage.

unsafe_replace_memory_context_without_accounting is an infrastructure
operation. It requires completed source/allocator preflight and accounting
adjustment, and also replaces the context when no blocks are allocated.
