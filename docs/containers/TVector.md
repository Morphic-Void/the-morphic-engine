Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    TVector.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    06 Nov 26

# TVector<T>

Owning storage follows the [container memory-context contract](memory_contexts.md):
explicit-context construction allocates nothing, and growth, replacement and
reuse retain the selected context until ownership transfer or reattribution.

`TVector<T>` is a move-only owner of contiguous, tightly packed objects. It
requires C++17 and uses no exceptions. `TPodVector<T>` remains the byte-copying
container for trivially copyable elements; `TOrderedCollection` and
`TUnorderedCollection` provide stable addresses for constructed objects.

| Container | Element transfer | Object addresses |
| --- | --- | --- |
| `TPodVector<T>` | Byte copies of trivially copyable values; no element construction or destruction | Can change during capacity changes and positional edits |
| `TVector<T>` | Nonthrowing construction, move construction and destruction | Can change during capacity changes and positional edits |
| `TOrderedCollection<T, TKey>` / `TUnorderedCollection<T>` | Objects constructed in stable backing storage | Remain stable while the objects are retained |

## Element lifetimes

`T` must be a non-array, non-const, non-volatile object type with nonthrowing
move construction and destruction. Copy construction, default construction and
assignment are not general requirements. Each emplacement requires nonthrowing
construction from its supplied arguments. Copy insertion requires nonthrowing
copy construction; `resize()` requires nonthrowing default construction,
including calls that shrink. Non-default-constructible types can shrink with
`discard_back()` or `erase()`.

Exactly `[0, size())` contains live objects. Spare capacity is raw storage:
reserving it never constructs objects. Construction uses placement new;
relocation uses move construction followed by destruction, never byte copying
or assignment. Erase, discard, shrink, clear and deallocation destroy objects.
`resize()` value-initialises newly added elements, including zero-initialising
scalar types. There is no raw size setter or uninitialised/zeroed insertion API.

## State and access

A default or moved-from vector is valid, empty, unallocated and reusable under
the engine's normal memory-context provisioning. Allocated storage with zero
live elements is valid, empty and ready. `data()` observes backing storage;
only the first `size()` elements may be accessed as live objects.

`explicit TVector(memory::CMemoryContext& context) noexcept` configures an empty
vector directly from the supplied context, without allocation or an ambient
context requirement. The context must outlive owned storage. Growth and reuse
after `deallocate()` retain the captured context.

`size()`, `capacity()` and `available()` count elements. `get(index)` returns
null unless the index identifies a live element. `operator[]`, `first()` and
`last()` require valid element access and assert that precondition; there is no
fallback object. Const vectors expose const element pointers and references.
No typed views are provided.

Reference access is a caller precondition, not a recoverable bounds check.
Assertions may be disabled or allowed to continue; use `get()` when the index
has not already been validated. Mutations require exclusive access, and element
constructors/destructors must not reenter mutations on the same vector.

## Insertion and aliasing

- `emplace_back(args...)` and `emplace(index, args...)` return the inserted
  object's pointer, or null on allocation failure or an invalid index/limit.
- `push_back(item)` and `insert(index, item)` return success as a boolean.
  Rvalues move-construct; const lvalues copy-construct.
- An insertion index may equal `size()` but must not exceed it.
- Self-insertion and constructor arguments referring to existing elements or
  their subobjects are supported. Required allocation completes before the
  arguments are consumed. The incoming object is constructed before existing
  elements move. In-place middle insertion stages a temporary before shifting.
- Successful move insertion from an existing element leaves its original
  logical entry moved-from. That entry shifts right if insertion precedes it.
- A constructor that retains pointers/references into vector storage must obey
  the ordinary invalidation rules. Argument evaluation before the call and
  reentrant mutation of the vector are outside the insertion guarantee.

There are no bulk insertion operations. A nonthrowing constructor may represent
its own resource failure through ordinary object state. Successful emplacement
means that construction completed; it does not inspect that state.

## Capacity and failure

| Operation | Contract |
| --- | --- |
| `allocate(capacity)` | Exact capacity and size zero on success. Reuses matching capacity after destroying live objects. |
| `reallocate(capacity)` | Exact capacity, preserving all live elements. Rejects capacity below size. Matching capacity is a no-op. |
| `reserve(minimum)` | Ensures at least the requested capacity using `memory::vector_growth_policy()`. |
| `ensure_free(extra)` | Ensures the requested number of spare element slots. |
| `shrink_to_fit()` | Exact capacity equal to size; releases storage when empty. |
| `resize(size)` | Preserves the prefix, destroys the removed suffix or value-initialises the added suffix. Grows capacity using the vector policy. |
| `clear()` | Destroys all live elements and retains capacity. |
| `deallocate()` | Destroys all live elements and releases backing storage. |

`allocate(0)` releases everything. `reallocate(0)` succeeds only when size is
zero. Satisfied reservations, including zero requests on empty vectors, do not
allocate. `erase(index, count)` preserves remaining order; `discard_back(count)`
removes a suffix. Invalid removal ranges return false without changes; zero
counts succeed at valid positions, including the end.

Every required backing allocation completes before any existing element is
destroyed or moved, or any insertion argument is consumed. Allocation failure
preserves values, addresses, size, capacity and attribution, including the state
of a move-insertion source. Destruction and construction after that point cannot
throw. Replacement storage briefly coexists with the original allocation.

Storage uses a relocatable `CMemoryToken` with `sizeof(T)` stride and
`memory::t_default_align<T>()` alignment. The token's 16-bit stride limit applies;
the shared byte ceiling determines `k_max_elements`. Replacement allocation uses
the existing token context, including after explicit reattribution.

## Operation costs

The following costs count element operations and exclude allocator work and
work performed inside element constructors, moves and destructors.

| Operation | Element operations |
| --- | --- |
| Observers, `get()` and valid reference access | Constant |
| Append with spare capacity | One construction |
| In-place middle insertion | Linear in the shifted suffix, plus one staged automatic `T` temporary |
| Insertion requiring growth | Linear in the old size, plus one inserted construction |
| Capacity change preserving contents | Linear in the size; no-op at matching capacity |
| `resize()` without capacity change | Linear in the number of added or removed elements |
| `erase(index, count)` | Linear in the erased range plus the shifted suffix |
| `discard_back(count)` | Linear in count |
| `clear()`, `allocate()` reset and `deallocate()` | Linear in the old size |
| Vector move construction | Constant; no element moves |
| Vector move assignment | Linear in the destination's old size; no source element moves |

The engine's automatic vector policy caps capacity growth steps. Reserve a known
required count ahead of large append sequences to reduce relocation work.

## Invalidation and ownership transfer

Capacity changes invalidate all element pointers and references. In-place
insertion invalidates the suffix beginning at the insertion index; erase
invalidates the suffix beginning at the erased index. Removal invalidates the
removed objects. `allocate()` destroys every existing element even when storage
is reused. `clear()` retains storage but ends every element's lifetime.

Moving the vector itself transfers backing ownership and attribution without
moving elements or allocating. Move assignment first destroys the destination's
contents. The source retains its configured context while becoming empty and
reusable. Self-move assignment is a no-op.

Objects' owned payloads follow their own move contracts. In particular, moving
a `CByteBuffer` preserves its payload address even when the buffer object moves.

## Shallow memory attribution

`memory_attribution()` observes only the vector's backing token, including its
capacity allocation. The common context-replacement hook and
`memory::reattribute()` affect that token, including its unallocated state.
They neither observe nor reattribute allocations owned by contained elements.
For example, a vector of byte buffers can have one backing context and several
independent payload contexts. A higher-level owner must explicitly implement
deep attribution when it needs that policy.

## Checked ownership example

This example assumes an engine memory context is already provisioned. Each
failed backing allocation returns before consuming the supplied buffer owner.

```cpp
static bool build_buffers() noexcept
{
    TVector<CByteBuffer> buffers;
    CByteBuffer buffer;
    if (!buffer.allocate(128u) || !buffer.set_size(128u))
    {
        return false;
    }
    const std::uint8_t* const payload = buffer.data();
    if (!buffers.push_back(std::move(buffer)))
    {
        return false;
    }
    if (!buffers.reserve(64u)) return false;

    //  Insert the existing owner at the front. Its former entry shifts to 1
    //  and remains a live, moved-from CByteBuffer.
    if (!buffers.insert(0u, std::move(buffers[0u])))
    {
        return false;
    }
    return (buffers[0u].data() == payload) && buffers[1u].is_empty();
}
```

## Object-model and test coverage

The C++17 toolchain must support the corrected raw-allocation object model:
storage can contain an array object whose elements are constructed individually.
This is the dynamic-array case addressed by
[WG21 P0593R6](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0593r6.html).
Array-element replacement also follows the containing-object rules clarified by
[WG21 P2103R0, US041](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2103r0.html).
Creating that array object does not default-construct spare elements of a
nontrivial `T`; their lifetimes still begin through placement construction.

`tests/test_suites/TVector_test_suite.cpp` covers lifetime counts and self-address
checks, all small erase ranges and self-insertion positions, move-only types
without assignment/default construction, const/reference members after slot
reuse, scalar value initialisation, over-alignment, source reuse after vector
moves, and allocation-failure preservation followed by successful retry.
Owning-buffer cases check payload-address preservation, release during removal
and reset, shallow accounting, and explicit context transfer. Explicit-context
construction, growth and reuse are also tested with no ambient context.

The tests establish behaviour on the validated toolchains; runtime lifetime
counts alone do not prove every aspect of the C++ object model.
