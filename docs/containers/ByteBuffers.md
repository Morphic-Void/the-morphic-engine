Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   ByteBuffers.md  
Author: Ritchie Brannan  
Date:   1 Apr 26  

# ByteBuffers

## Overview

This document defines the behaviour of contiguous and rectangular byte
storage utilities implemented in ByteBuffers.hpp.

The layer provides owning buffers and non-owning views over raw byte
storage. It does not impose element, texel, pixel, or structure
semantics. All sizes, capacities, extents, and alignment values are
expressed in bytes.

The implementation is noexcept and assumes allocation failure is
reported by return value. Operations that fail leave the destination
object unchanged unless explicitly stated otherwise.

## Requirements and scope

- Requires C++17 or later.
- No exceptions are used.
- Storage is untyped and byte-addressable.
- Alignment is expressed as a power-of-two byte value.
- Higher-level interpretation of the data is out of scope.

## Core concepts

### Ownership and views

Owning types manage storage through memory::CMemoryToken.

Non-owning types reference storage through memory::CMemoryView
or memory::CMemoryConstView together with metadata describing
the logical extent.

Metadata is always stored separately from the underlying storage handle.

### Status model

All types expose three status queries:

- is_valid reports whether invariants hold.
- is_empty reports logical emptiness or canonical empty state.
- is_ready reports a non-empty state suitable for normal access.

Accessors are fail-safe. If an object is not ready, accessors return
null, zero, or empty results.

### Canonical empty state

Each type defines a canonical empty state:

- contiguous view: data == nullptr, size == 0
- contiguous buffer: data == nullptr, size == 0, capacity == 0
- rect types: row_pitch == 0, row_width == 0, row_count == 0

Empty and invalid are not equivalent. Invalid states may still be empty.

## Constness model

Const on an owning buffer or mutable view does not imply immutable
storage.

- CByteBuffer and CByteView may return mutable pointers
  from const member functions.
- Immutable access is represented by CByteConstView and
  CByteRectConstView.

Const on the wrapper type constrains the wrapper, not the
underlying memory.

## Alignment model

Alignment applies to the base address of an allocation or the origin
of a view.

For contiguous buffers:

- Passing align == 0 reuses the current alignment intent when available.
- If no allocation is currently present, align == 0 results in
  allocation-layer normalisation behaviour.

For rect buffers:

- Passing row_align == 0 reuses the alignment intent of any previous
  when available.
- Otherwise allocation-layer normalisation rules apply.
- row_pitch is derived by rounding row_width up to a multiple
  of the effective alignment.

For subviews:

- Alignment guarantees may be reduced according to the byte offset from
  the original origin.

## Metadata types

### MetaByteView

Fields:

- size: logical byte extent

State rules:

- size == 0 is the canonical empty state
- valid requires size <= memory::k_byte_size_ceiling
- ready requires size != 0 and size within bounds

### MetaByteBuffer

Fields:

- size: logical byte extent
- capacity: allocated byte extent

State rules:

- {size == 0, capacity == 0} is canonical empty
- {size == 0, capacity != 0} is a valid ready state
- valid requires size <= capacity and capacity within bounds
- ready requires size <= capacity and capacity within bounds

Derived:

- byte_view returns a MetaByteView over the logical extent

### MetaByteRectView

Fields:

- row_pitch: byte step between row starts
- row_width: active byte extent within a row
- row_count: number of rows

State rules:

- {0, 0, 0} is canonical empty
- non-empty states require all fields to be non-zero
- valid requires:
  - row_width <= row_pitch
  - row_pitch * row_count <= memory::k_byte_size_ceiling
- ready requires valid and non-empty

Derived:

- contiguous when row_width == row_pitch
- size_as_buffer returns row_pitch * row_count when contiguous
- byte_view returns a MetaByteView when contiguous, otherwise empty

## Contiguous byte model

For contiguous buffers:

- size is the logical byte extent
- capacity is the allocated byte extent
- size may be less than capacity

Logical size may be adjusted without reallocation provided it does not
exceed capacity.

### Append operations

append(data, size):

- appends source bytes to the logical end
- grows capacity if required
- with non-null data, ensures the 4 KiB minimum capacity even when size is zero,
  providing lazy initialisation without changing logical size
- fails if allocation fails or data is null with non-zero size
- succeeds with no effect when data is null and size is zero

append(size, clear):

- extends the logical size by size bytes
- if clear is true, the appended region is zeroed
- otherwise the appended region is uninitialised
- when size is zero, returns buffer validity without allocating storage

### Logical size changes

set_size(size):

- sets logical size without initialising new bytes
- requires size <= capacity
- leaves storage contents unchanged

### Allocation and reallocation

allocate(capacity, align):

- allocates storage with logical size zero
- equivalent to reallocate(0, capacity, align)

reallocate(size, capacity, align):

- replaces storage while preserving prefix bytes
- preserved bytes: min(old_size, new_size)
- if size increases, newly exposed bytes are zeroed
- if capacity is zero, the buffer is deallocated
- when capacity and effective alignment permit, reallocation may be
  skipped and existing storage reused
- on failure, state is unchanged

### Growth operations

resize(size, align):

- sets logical size
- grows capacity using the buffer growth policy if required
- newly exposed bytes are zeroed

reserve(min_capacity, align):

- ensures capacity >= max(min_capacity, 4 KiB), applying the growth policy only
  when current capacity is below that requirement
- logical size is unchanged
- sufficient capacity is reused unless a stronger alignment requires relocation
- reserving zero establishes at least the 4 KiB minimum capacity, including on
  an empty buffer or one with a smaller exact allocation

ensure_free(extra, align):

- ensures at least extra bytes of free space beyond current size
- logical size is unchanged
- uses reserve, including its capacity-reuse and alignment rules

Automatic buffer growth uses a 4 KiB minimum capacity and a 64 KiB capped
growth step. The cap controls incremental growth; a large request jumps directly
to a sufficient capacity in one allocation. Vector growth has its own policy.
Known-size construction can reserve once, or use allocate/reallocate for exact
capacity. Filling reserved storage does not grow it while capacity and alignment
remain sufficient, subject to reserve's 4 KiB minimum. Pointer-based appends with
non-null data retain this minimum even for zero bytes; null-source zero-length
appends and size-only zero-length appends do not allocate.
Failed growth or alignment upgrades preserve the existing buffer and contents.

shrink_to_fit():

- reduces capacity to match current size

### Copy and construction

construct_and_copy_from(view):

- if view is invalid, fails and leaves state unchanged
- if view is empty, deallocates and succeeds
- otherwise allocates exact size and copies all bytes

### Deallocation

deallocate():

- releases storage and resets metadata to canonical empty

### Zero fill

zero_fill():

- zeroes bytes in range [0, size)

## Contiguous view behaviour

### Construction and assignment

set(data, size, align):

- assigns a view when data is non-null and size is non-zero
- otherwise resets to empty

set(memory_view, meta):

- assigns when both inputs are ready
- otherwise resets to empty

reset():

- clears the view to canonical empty

### Subviews

subview(offset, count):

- returns a view over [offset, offset + count)
- returns empty if out of range or not ready
- resulting alignment may be reduced depending on the offset

head_to(count):

- returns a view over [0, count)
- returns empty if count exceeds size

tail_from(offset):

- returns a view over [offset, size)
- returns empty if offset is out of range
- resulting alignment may be reduced depending on the offset

### Zero fill

zero_fill():

- zeroes bytes in the current view range

## Rect byte model

Rect storage is described by row_pitch, row_width, and row_count.

- row_pitch is the byte step between rows
- row_width is the active byte extent within a row
- row_count is the number of rows

A rect is contiguous as a byte buffer only when row_width == row_pitch.

Byte views derived from rect types are empty when the rect is not
contiguous.

row_data(y) returns the start address of row y when y is in range.

## Rect buffer behaviour

### Allocation

allocate(row_width, row_count, row_align, clear):

- allocates storage for row_count rows
- derives row_pitch from row_width and alignment
- if clear is true, all allocated bytes are zeroed

### Reallocation

reallocate(row_width, row_count, row_align, clear_uninitialised):

- preserves overlapping prefix of each preserved row
- preserved rows: min(old_row_count, new_row_count)
- preserved bytes per row: min(old_row_width, new_row_width)

When clear_uninitialised is true, zeroing applies to:

- newly added rows
- newly exposed bytes within preserved rows
- row tail bytes not written during copying

Passing row_width == 0 and row_count == 0 requests deallocation.

On failure, state is unchanged.

### Copy and construction

construct_and_copy_from(view):

- if view is invalid, fails and leaves state unchanged
- if view is empty, deallocates and succeeds
- otherwise allocates and copies row by row

Destination behaviour:

- row_pitch is derived from row_width and source alignment
- bytes [0, row_width) are copied for each row
- bytes [row_width, row_pitch) are zeroed

### Deallocation

deallocate():

- releases storage and resets metadata to canonical empty

### Zero fill

zero_fill():

- if contiguous, zeroes the full byte range in one operation
- otherwise zeroes each row over [0, row_width)

## Rect view behaviour

### Construction and assignment

set(data, row_pitch, row_width, row_count, align):

- assigns when data is non-null and metadata is valid
- otherwise resets to empty

set(memory_view, meta):

- assigns when both inputs are ready
- otherwise resets to empty

reset():

- clears the view to canonical empty

### Subviews

subview(x, y, width, height):

- x and width are in bytes within a row
- y and height are in rows
- origin advances by x + y * row_pitch
- row_pitch is preserved
- row_width and row_count are reduced to the subregion
- returns empty if the request is out of range
- resulting alignment may be reduced depending on the origin offset

### Byte view conversion

byte_view and byte_const_view:

- return a contiguous byte view when row_width == row_pitch
- otherwise return an empty view

### Zero fill

zero_fill():

- if contiguous, zeroes the full range
- otherwise zeroes each row over [0, row_width)
