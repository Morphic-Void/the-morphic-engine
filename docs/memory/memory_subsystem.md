Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   memory_subsystem.md  
Author: Ritchie Brannan  
Date:   2 Jun 2026  

# Memory Subsystem

## Overview

The Morphic Engine memory subsystem defines the low-level allocation, ownership, view, and accounting contracts used by containers and related infrastructure.

The subsystem separates:

- allocation routing from allocation ownership;
- C++ ownership from accounting attribution;
- owning storage handles from non-owning views;
- logical byte extent from element count and stride;
- trusted extent metadata from diagnostic ownership evidence;
- shallow accounting from explicitly documented deep accounting.

This is a cross-header policy document, not an API reference. Header TOF comments and local comments remain responsible for immediate type orientation, declaration-level notes, and narrow preconditions.

## Scope

The subsystem provides:

- nothrow raw byte allocation and deallocation entry points;
- shared allocation limits and growth policies;
- alignment normalization and typed default alignment;
- per-thread / per-module / per-DLL allocation context routing;
- local allocation accounting for leak detection and memory budgeting;
- one move-only storage token for relocatable and stable modes;
- bounded mutable and const non-owning views;
- checked strided interpretation of raw storage.

The subsystem is designed for C++17, no exceptions, and explicit failure handling.

## Non-goals

The memory subsystem does not provide general container semantics.

It does not make checked view access a substitute for storage lifetime or
concurrent-access discipline.

It does not construct, destroy, or relocate non-trivial object sequences in the raw token/view layer.

It does not make views responsible for lifetime, ownership, or allocation
attribution.

It does not perform deep accounting of nested containers unless a caller or container explicitly documents and implements that policy.

It does not make const wrapper objects imply immutable referenced memory. Read-only access is represented by const view types.

It does not make allocation-context counters live atomic telemetry. Context accounting is local accounting state unless a higher-level synchronization policy explicitly provides live observation.

## Layer model

The subsystem has three conceptual layers.

### Allocation context

The allocation context is the attribution and routing layer. It binds allocation requests to an externally owned allocator and records local allocation accounting for a thread/module accounting domain.

The allocation context owns accounting state, not heap storage.

### Allocation substrate

The allocation substrate provides mechanical helpers: shared limits, growth
policies, alignment policy, allocator configuration, and nothrow raw allocation
entry points.

This layer provides allocation mechanics, not ownership semantics.

### Raw ownership and view primitives

The primitive layer defines the unified token for owning raw storage and bounded
mutable and const views for observing strided storage without owning it.

Tokens own storage and track requested extent. Views are bounded non-owning
descriptors over contiguous strided storage.

## Ownership boundaries

An owning token owns exactly the allocation it holds. Moving a token transfers C++ ownership to the destination token. Destroying a non-empty token releases its owned storage.

A view is only a reference to storage. Copying, moving, adopting, or deriving a view does not transfer allocation ownership and does not affect allocation accounting.

The system-layer `CErasedOwner` carrier uses a memory token to own one payload
allocation. Its SYSTEM-or-LOCAL type registration, component-local operation
authority, destruction policy, and hazard semantics belong above the memory
substrate.

Allocator interfaces and allocation contexts are not storage owners. An allocation context routes allocations and records accounting; the allocator object referenced by the context is externally owned and must remain valid for all allocations and deallocations routed through it.

## Allocation extent and accounting

The subsystem separates allocation identity from allocation footprint.

Allocation identity asks whether an object currently appears to own an allocation.

Allocation footprint asks how many bytes of allocation extent can be trusted and reported.

For owning memory tokens, `owns_storage()` is the allocation identity observer.
It reports whether the owning pointer is non-null and is intentionally weaker
than configuration.

For byte-footprint accounting, bytes() is the footprint observer. It is fail-safe and may report zero when extent metadata cannot be trusted.

A damaged token may therefore contain a non-null owning pointer while its alignment or extent metadata is invalid. In that case:

- `owns_storage()` may still indicate that allocation-count accounting is required;
- bytes() may report zero because the byte extent is not trusted;
- the combination is diagnostic evidence, not proof that the allocation is safe, complete, or accurately sized.

Primitive token accounting uses:

    allocation count  <- memory_allocation_count()
    byte footprint    <- memory_allocation_size()

`bytes()` reports logical payload extent. Conditioned allocation footprint is
reported separately and may include alignment conditioning, stable-storage
slack, and stable directory capacity.

## Allocation-context role

An allocation context is the local accounting and routing authority for an allocation domain.

Typical domains are expected to correspond to a thread, module, DLL, or a controlled combination of those concepts.

The allocation context tracks live allocation count and conditioned allocated
bytes. These relaxed atomic counters are audit telemetry and accounting
integrity evidence, not object synchronization.

The allocator pointer held by a context is non-owning. It must remain valid while the context can route allocations or deallocations through it.

`CMemoryAllocator` and `CMemoryContext` are non-copyable and non-movable. A
context never changes allocator. Context compatibility is allocator object
identity, which permits attribution to change without changing physical
allocation ownership.

is_usable() means the context currently has an allocator available for routing. It does not imply that the context is globally safe to inspect concurrently.

## Allocation routing

Raw byte allocation is routed through the active allocation mechanism.

The allocation substrate exposes byte allocation and deallocation entry points. These route to the active allocator/context layer.

Zero-size raw byte allocation is rejected and returns null.

The owning token layer uses a higher-level convention: requesting zero extent through token allocation/reallocation means "become empty". That operation deallocates existing owned storage and succeeds.

This layer difference is intentional:

    raw allocation layer:
        zero bytes is not an allocation

    owning token layer:
        zero extent is a request for canonical empty ownership

## Thread/DLL attribution

Allocation accounting is attributed to the allocation context that records the allocation.

Each loaded component has its own module memory context. The host and executive
contexts use distinct system identities and independent counters even though they
currently share the host allocator. Sharing an allocator makes reattribution
compatible; it does not merge component attribution or make the executive
context a reference to the host context.

The context installed in a module must carry that module's system identity and
must be empty at installation. Before native module unload, the host stops and
joins the module threads and verifies that both the context's live-allocation
count and attributed-byte total are zero. A failed audit prevents unload, so
allocator-backed storage cannot remain live after the component code responsible
for it has disappeared.

A move of an owning token transfers both storage ownership and the source
token's existing attribution. It does not select a new accounting context.

When ownership crosses a thread, module, or DLL accounting boundary,
reattribution must be requested deliberately. Compatible contexts are required.
The allocation count and conditioned-byte total are added to the target before
they are subtracted from the source.

The accounting-transfer policy uses token-side observers consistently:

    transfer allocation count using memory_allocation_count()
    transfer conditioned footprint using memory_allocation_size()

Accounting discrepancies are diagnostic in development and release builds. They
do not reject an otherwise valid transfer, roll back its adjustments or request
shutdown. `memory::reattribute` retains its boolean result for allocator
incompatibility; ownership and source-context coherence remain caller preflight
requirements. Diagnostic totals are modular and independently permit zero.

The unsigned 32-bit allocation count and unsigned 64-bit byte count are adjusted
independently with relaxed atomic fetch operations. Each report uses that
operation's returned before value and derives after at the counter's width.
Report when `((before ^ after) & high_bit_mask) != 0`, in either direction, with
context, counter and adjustment details. Further activity with the same high bit
does not repeat the report; concurrent transition clusters are permitted. Retain
the adjustment without clamping or resetting. This heuristic does not detect every
possible wrap for arbitrary adjustment magnitudes.

The primitive transfer accepts a `std::size_t` allocation count. A value beyond
the 32-bit counter range is reported and narrowed modulo 2^32 before adjustment;
the diagnostic does not reject the transfer.

Real allocator failure remains distinct from an accounting incident: failed
allocation reverses its reservation, and refused deallocation restores its
subtraction. A misaligned allocation retains accounting if the allocator cannot
release it. Reporting must not allocate through the engine's accounting path;
the [debug service](../debug/debug_service_substrate.md) opens logs before being
published and does not lazily reopen them while reporting. No counter-reset or
reload-period policy is implied by diagnostic-only primitive updates.

Accounting transfer must not infer deep ownership. If a container owns nested containers, the outer container's shallow memory token accounts only for its own direct storage unless the container explicitly implements and documents recursive accounting.

## Threading and observation model

The ambient memory context is resolved from thread-local context first and then
module-local context. Context installation is provisioning state and must not
race live use.

`CMemoryContext` allocation count and allocated-byte counters are relaxed
atomics. They provide audit telemetry and accounting integrity checks, not a
general synchronization mechanism for the objects stored through a context.
`belongs_to_module()` validates the component encoded in the context's system
identity. `is_attribution_empty()` requires both accounting counters to be zero
and is intended for quiescent installation and teardown boundaries.

## Raw storage ownership

Raw storage ownership is represented by `memory::CMemoryToken`.
A configured token records its memory context, element stride, storage-alignment
intent, requested count, and relocatable or stable mode. Relocatable storage is
contiguous; stable storage may be segmented. An empty token may retain its
configuration and context so it can be reused after a move or deallocation.
The token occupies 24 bytes on x64 and 16 bytes on x86.

`count()` is the requested user-visible capacity. Stable buffer slack and
directory capacity are implementation details. `bytes()` is `count() *
stride()` and excludes conditioning, stable slack, directory allocation,
allocator metadata, and platform overhead.

Configuration is immutable while storage is owned. `storage_alignment()`
reports normalized user intent, not stronger incidental alignment. It need not
divide stride. `element_alignment()` derives the alignment recurring at each
indexed element from storage alignment and stride.

`data()` is available only for relocatable storage. `index_ptr()` is
mode-neutral. `allocate()` replaces storage in either mode, `reallocate()`
applies only to relocatable storage, and `grow_to()` applies only to stable
storage. Replacement paths allocate and validate new storage before mutating
the token.

Mode predicates already establish that the corresponding configuration is
present. Use `is_configured()` separately only for mode-neutral validation.

Tokens are move-only. Move construction and assignment transfer storage
unconditionally. The source retains its context, stride, alignment, mode, and
stable-buffer configuration while becoming empty and immediately reusable.

Cloning copies logical payload storage. The default clone preserves the source
context; the context-taking form overrides it, with null selecting the ambient
context. Exact self-clone is a no-op. Self-clone to another context creates
replacement storage transactionally.

## Views and non-ownership

`memory::CMemoryView` and `memory::CMemoryConstView` are bounded non-owning
descriptors over contiguous strided storage. They carry pointer, count, stride,
and guaranteed storage alignment.

Index and range observers validate against the recorded count. Invalid
subviews return empty views. A valid range still depends on the referenced
storage remaining alive and on the caller's synchronization discipline.

Subview origins reduce guaranteed storage alignment according to their byte
offset. `element_alignment()` reports the recurring alignment implied by that
origin and stride.

## Constness

Wrapper constness applies to the wrapper object only.

A const memory token or const mutable view wrapper does not imply immutable
referenced memory.

Read-only access is represented by const view types.

## Alignment model

The allocation substrate applies an alignment policy before raw allocation.

The byte allocation alignment policy reduces the requested alignment to a power-of-two alignment and applies at least the pointer-alignment floor.

Memory tokens and views store normalized alignment intent.

Memory views report guaranteed alignment for the current address. This may be
less than the actual physical alignment of the address, but it must not
overstate the guarantee.

Byte subviews reduce guaranteed alignment based on byte offset.

No divisibility relationship is required between storage alignment and stride.
Element alignment is derived from both values.

## Extent model

`CMemoryToken`, `CMemoryView`, and `CMemoryConstView` carry element count and
stride. Their logical byte extent is `count() * stride()`.

Views are bounded descriptors, not owners. Their recorded extent does not
extend the lifetime of referenced storage.

Containers are responsible for maintaining their own logical size and capacity. Token extent usually corresponds to capacity allocation, not necessarily to logical element count.

## Reallocation semantics

Reallocation preserves exactly the caller-specified copy extent.

For relocatable ownership:

    reallocate(new_count, copy_count, zero_new)

preserves exactly `copy_count` elements.

The copy extent must be valid for both the current and requested extents:

    copy_count <= min(current_count, requested_count)

This is container-facing policy. Containers distinguish logical size from capacity, so reallocation must not implicitly preserve the full current allocation extent unless the caller asks for that.

Allocation and reallocation leave the current token state unchanged on failure.

A zero requested extent at the token layer deallocates existing storage and leaves the token canonical empty.

When `zero_new` is true, the unpreserved suffix of the destination extent is
zero-filled. This includes same-extent reallocations where the requested extent
is unchanged but the preserved prefix is smaller than the extent.

## Deallocation metadata integrity

Deallocation requires correct metadata.

Allocator-facing deallocation is a critical boundary. Invalid ownership or
allocator metadata, missing callbacks and real deallocation failure remain genuine
failure conditions. A discrepancy in diagnostic counters cannot by itself skip
valid freeing, reject valid allocation or reattribution, or request shutdown.
Allocation exhaustion from an otherwise valid allocator is
recoverable and may be used speculatively.

The allocator callback reports deallocation failure to `CMemoryContext`.
`CMemoryContext::deallocate()` consumes that result and restores the accounting
subtraction when the allocator rejects deallocation; failure does not propagate
through tokens or containers. This inverse adjustment reflects a real allocator
failure, not rejection because a diagnostic counter looked wrong.

Fail-safe observers are for safe observation and diagnostics. They are not a license to silently deallocate with untrusted metadata.

If an owning pointer exists but required deallocation metadata is corrupt, the subsystem should prefer fatal diagnostic handling over undefined allocator interaction.

## Allocation failure and zeroing

Allocation functions are nothrow.

Raw allocation failure returns null.

Token allocation/reallocation failure leaves existing ownership unchanged.

Token allocation may optionally zero the entire new extent.

Token reallocation may optionally zero the unpreserved suffix. When reallocating from an empty source and zeroing is requested, copied-prefix-equivalent bytes may also be zero-filled so that the requested preserved region is deterministic.

`CErasedOwner` creation allocates direct payload storage through a memory token
and placement-constructs the registered payload. The carrier's payload
requirements are documented with the system-owned facility.

## Container-facing accounting

Container allocation accounting is shallow unless explicitly documented otherwise.

Participating containers, backing classes, metadata classes and document owners
expose the same two public infrastructure methods:

```cpp
//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;
```

`SMemoryAttribution` reports `source_state` (`empty`, `coherent` or `mixed`),
`source`, `token_count`, `allocation_count` and `allocation_size`. Empty and mixed
records have a null source. A coherent record names the single context of all
allocated backing storage. State is independent of diagnostic totals: zero or
wrapped totals never make allocated storage empty. Token count is the bounded
inventory of owned tokens, including unallocated tokens.

Leaves adapt a token with `observe_memory_attribution(token)` or forward an
owned wrapper's record. Aggregates combine complete child records with
`combine_memory_attribution(left, right)`. Empty sources are neutral, coherent
sources must match by pointer, and mixed state is absorbing. All totals are
collected even after mixed sources are found. Slot/collection outer records
include backing and metadata; metadata records alone cover only metadata.

`memory::can_reattribute_to(owner, target)` and `memory::reattribute(owner, target)`
provide the common checked operations. Null selects the ambient context. Each
operation observes the owner once after resolving a non-null target. Missing
targets, mixed sources and incompatible allocated sources reject transfer.
Reattribution always makes its own fresh observation; a prior successful query
is not a reservation.

For containers that can contain other containers, accounting is not automatically recursive. Recursive or deep accounting must be implemented and documented by that container.

Type-erased payloads need particular care. The erased carrier allocation is
one direct allocation. The recovered payload may itself contain owning
allocations, whose accounting remains part of the payload's own policy.

Compound owners gather one coherent source context from their storage-owning
tokens, preflight ownership and allocator compatibility, adjust aggregate
accounting once, and then replace every
token context without additional accounting. Empty tokens are rebound with
their owner too. Rejected ownership preflight leaves the object and all token
contexts unchanged; accounting diagnostics do not prevent context replacement.

`unsafe_replace_memory_context_without_accounting` requires completed source and
allocator preflight and the outer accounting adjustment. It replaces contexts in
all owned children, including unallocated tokens, without repeating accounting.
Empty and same-context transfers skip counter adjustment but still call this hook.
The caller must retain exclusive access throughout preflight and replacement.
Ordinary moves preserve attribution; reattribution preserves content addresses and
keys. Later growth follows the existing ambient-context allocation policy.

Aggregate allocation-count and byte totals use `add_accounting_counts` and
`add_accounting_bytes` at every sum boundary. These report unsigned overflow and
return modulo totals; a child's representability loss is reported before its
result reaches a parent. Explicit observer queries can emit these diagnostics,
including again on repeated observations. They neither reject transfer nor request
shutdown, and do not change atomic-context high-bit-transition diagnostics.
Shared preflight queries and same-context transfers also observe complete records
and can therefore emit these diagnostics.

`CLiveDocument` explicitly composes ownership of its node store, property-name
table and string-value table. Its reattribution preflights all three, adjusts
accounting once, and replaces their contexts through complete child hooks.
Caller-owned analysis scratch and temporary parser/baker storage are excluded.
This interface does not register live documents as erased payloads or add a
transport service.

A LOCAL `CErasedOwner` additionally requires both source and target contexts to
belong to the ambient component. This provenance check precedes the aggregate
adjustment, preventing direct reattribution from becoming an accidental
component-boundary transfer.

`CMemoryToken` retains its primitive member interface. `CErasedOwner` retains its
checked member operations, registered callback table and component checks. Its
typed nested adapters consume attribution records and the shared query; separate
callbacks may observe a payload independently. The one-observation guarantee
applies to each shared operation, not to an entire erased-owner operation.
`CBakedDocumentBlock` privately owns a `CByteBuffer` and forwards its common
two-method interface to that buffer. Checked adoption validates before setting
the exact logical document size and moving the buffer, preserving allocation
capacity, address and attribution. Document views are constructed on demand from
the validated block without allocation or revalidation; they borrow storage and
do not extend its lifetime. Immutable byte access exposes only the exact extent,
separate from the buffer's 32-byte-multiple capacity; see the
[baked-document storage contract](../data_model/baked_document_format.md).

General-purpose collection reattribution covers its backing storage. It does not
automatically reattribute allocations owned by user values stored in the collection.

## DLL and transport boundaries

Compatible allocation contexts are necessary but not sufficient for safe
cross-DLL transfer. Vtables, callbacks, deleters, function pointers, payload
types, and other module-local executable state must remain valid for the full
lifetime of a transferred object.

Ambient module and thread state is deliberately non-atomic provisioning state
and must not race live use. Each DLL requiring module-local ambient state must
provide its own module-local implementation rather than import another module's
fallback state.

Thread transports are deliberately not reattributable. Allocation-owning
transports accept optional explicit attribution during construction or
configuration and otherwise use the ambient context. That attribution remains
fixed for the configured transport lifetime; endpoints may observe it through
the owner but must not alter it.

## Container-facing reallocation

Containers should pass their logical preservation extent explicitly.

For byte-backed containers, this is usually the number of bytes corresponding to logical content, not capacity.

For typed containers, this is usually the logical element count, not capacity.

The token layer will not infer logical size from current allocation extent.

This avoids over-preserving stale capacity bytes and keeps reallocation semantics under caller control.

## Header map

The headers provide the following local surfaces:

- `memory_policies.hpp`: shared limits, growth helpers, and alignment policy.
- `memory_context.hpp`: callback allocator, ambient context routing, attribution,
  and allocation accounting.
- `memory_token.hpp`: relocatable and stable raw-storage ownership.
- `memory_view.hpp`: bounded mutable and const non-owning views.

The system-owned erased carrier is documented in
`docs/system/erased_owner.md`.

## Summary rules

Use a container's `memory_attribution().allocation_count` for its owned
allocation count and `.allocation_size` for its conditioned allocation footprint.
Primitive tokens retain `memory_allocation_count()` and `memory_allocation_size()`.

Do not treat views as owners.

Do not infer ownership or lifetime from a bounded view.

Do not infer deep container accounting from shallow token ownership.

Do not infer selection of a new accounting domain from C++ move alone.

Do not silently deallocate with corrupt metadata.

Preserve exactly the reallocation copy extent supplied by the caller.

Treat zero-size raw allocation and zero-extent token ownership as different layer policies.

Keep this document as the subsystem policy source. Keep headers compact and local.
