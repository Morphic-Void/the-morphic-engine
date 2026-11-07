Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    buddy_allocator_design.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# Buddy allocator and SPIRV-Reflect containment plan

## Status and purpose

Design and staged delivery plan from the discussion through 07 November 2026.
The standalone index and owner are implemented in
[`buddy_index.hpp`](../../core/allocators/buddy_index.hpp) and
[`TBuddyAllocator.hpp`](../../core/allocators/TBuddyAllocator.hpp), with both word
widths covered by `BuddyAllocator_test_suite.cpp`. The `TVector` chain is
implemented in
[`TBuddyAllocatorChain.hpp`](../../core/allocators/TBuddyAllocatorChain.hpp),
with both word widths covered by `BuddyAllocatorChain_test_suite.cpp`.
Shader measurement and reflection integration remain future stages requiring
separate instruction.

Implemented API and representation references are maintained in
[`docs/allocators/`](../allocators/README.md). This document retains the design
rationale, staged delivery plan and validation history.

The primary purpose is to validate SPIRV-Reflect allocation/free behaviour when
updating its pinned version, and to reclaim all owned memory independently of
vendor cleanup. Allocation reuse and performance are secondary to correct
ownership, rejection of invalid frees, and useful regression evidence.

The existing adapter uses a growing `CBumpBuffer`, ignores individual vendor
frees, and releases its storage collectively. Normal vendor destruction still
runs. Initial backing allocation failure is recoverable; allocation failure
during vendor execution is fatal because safe vendor handling of null allocation
results has not been established. These are the baseline contracts to preserve
or explicitly revise during integration.

## Agreed architecture

| Part | Responsibility | Planned location |
| --- | --- | --- |
| Packed buddy index | Reservation state, allocation-order selection, splitting, merging, allocation-boundary validation | `core/allocators/` |
| Single buddy manager | Compact movable owner of management data and payload backing; byte/offset conversion and accounting | `core/allocators/` |
| Chaining utility | Own managers in `TVector`, select suitable backing, grow transactionally, find the owner of a pointer | `core/allocators/` |
| Reflection allocation owner | Buddy chain, exceptional large allocations, operation identity, diagnostics and audit | Private Rendering integration |
| Allocation analysis | Record vendor requests/frees and compare configurations using power-of-two buckets and lifetime replay | Reflection tests and tools |

The owning interface is `TBuddyAllocator<TWord>`, with aliases
`CBuddyAllocator32` and `CBuddyAllocator64`. Chaining uses
`TBuddyAllocatorChain<TWord>` and a homogeneous manager type within each chain.
Restrict `TWord` to `std::uint32_t` and `std::uint64_t`; word width is a
compile-time choice. The packed index is an independently testable internal
mechanism; its helper type names remain implementation details.
Core must not include SPIRV-Reflect types or headers. The earlier
general memory-token allocation-set extension and the Audio intrusive allocator
are separate work, not prerequisites for this plan.

## Packed tree geometry and state

Use three symmetrical layers of complete packed subtrees with distinct nodes.
Every endpoint in an upper word has two children, each represented by the root
of a separate word in the next layer. There are no duplicated roots or mirrored
reservation bits. Support designs based on `std::uint32_t` and `std::uint64_t`
with the same logical rules and the compile-time interface described above.

| Property | 32-bit words | 64-bit words |
| --- | --- | --- |
| Node bits per word | 31 | 63 |
| Endpoints per word | 16 | 32 |
| Distinct node levels per word | 5 | 6 |
| Words in top/middle/bottom layers | 1 / 32 / 1,024 | 1 / 64 / 4,096 |
| Tree bytes before summaries/counters/padding | 4,228 | 33,288 |
| Smallest slots | 16,384 | 131,072 |
| Maximum allocation order | 14 | 17 |
| Theoretical payload with 16-byte slots | 256 KiB | 2 MiB |

The uppermost bit is reserved and initially zero. Pack the endpoint level into
the lowest bits and successive parent levels above it:

- 32-bit: endpoint bits 0..15, then 16..23, 24..27, 28..29, root 30.
- 64-bit: endpoint bits 0..31, then 32..47, 48..55, 56..59, 60..61, root 62.

Within each level, increasing bit indices follow increasing payload addresses.
For word width `W`, let `H = log2(W)` and maximum order `D = (3 * H) - 1`.
At local depth `r` from the word root, level bits begin at `W - 2^(r + 1)`.
At global depth `d`, the word layer is `floor(d / H)` and local depth is
`d % H`. A global node index `p` at that depth maps to word index
`floor(p / 2^r)` within its layer, and position `p % 2^r` within the local level.
These mappings keep all public indices in smallest-slot units.

For endpoint `i` of word `w` within a layer, the child words in the next layer
are `W * w + 2 * i` and the following word. Conversely, child word `c` has
sibling word `c ^ 1`, parent word `floor(c / W)`, and parent endpoint
`floor((c % W) / 2)`. Its side is `c & 1`. No logical level is skipped when
crossing a word boundary.

Use the PoC's reservation-state idea rather than a bitmap of leaf occupancy
alone. In a reachable, active subtree:

- A clear reservation bit denotes an unreserved block at that level.
- A set bit with clear immediate children denotes a direct allocation.
- A set bit with occupied children denotes a split block reserved for descendants.
- Clear descendants under a directly allocated ancestor are not available.

This retains rounded allocation boundaries without a header in vendor memory
or a separate order value per live allocation. It does not retain the exact
requested byte size. Upper-layer endpoints are real allocatable nodes; interpret
their children by reading the two next-layer word roots. Only bottom-layer
endpoints are final leaves.

Use exact-order availability masks and per-order child-routing bitmaps. The
reservation tree is the ownership authority; summaries are its derived search
index. Availability bit `j` means that the participating subtree contains a
maximal free block of order `j`. A whole free order-8 subtree advertises only
order 8, not orders 0 through 8. Descendants beneath a whole free or directly
allocated block do not participate in the search index.

Maintain a 32-bit availability mask for the entire active allocator. For request
order `r`, mask out orders below `r` and bit-scan the lowest remaining set bit
to obtain selected order `j`. If no bit remains, no existing block fits. This
selects the smallest available order before splitting a larger free block.

Every word with child words has a routing bitmap for each order represented in
those child subtrees. `routes[j]` bit `c` means child word `c` within that parent's
child group contains a participating maximal free block of order `j`. Each mask
uses the same word width as the reservation tree, so it covers exactly that
parent's 32 or 64 children. Bottom words need no routing arrays.

| Scheme | Top routing | Middle routing | Total routing bytes |
| --- | --- | --- | --- |
| 32-bit | 10 order masks, 4 bytes each | 32 words, each with 5 masks of 4 bytes | 680 |
| 64-bit | 12 order masks, 8 bytes each | 64 words, each with 6 masks of 8 bytes | 3,168 |

Together with the packed tree and the global 32-bit availability mask, these
require 4,912 bytes for the 32-bit scheme or 36,460 bytes for the 64-bit scheme,
before configuration, counters, ownership state, alignment, or optional caches.

Once order `j` is selected, locate the block from the active root. If that order
is represented inside the current word, derive its local candidate mask and
bit-scan it. Otherwise, bit-scan `routes[j]`, enter the selected child word, and
repeat. Each logical order belongs to exactly one packed layer. In the 64-bit
scheme, orders 12..17 are local to the top word, 6..11 to middle words, and 0..5
to bottom words. In the 32-bit scheme these ranges are 10..14, 5..9, and 0..4.
Thus selection needs at most two child-routing decisions and a local candidate
selection; it does not scan all words. Choose the lowest indexed eligible child
or local block as the initial deterministic tie-break.

At an ordinary local level, maximal free blocks are clear bits with set buddy
bits. With the level extracted into low bits, this is
`(~level_bits) & swap_adjacent_bits(level_bits)`, masked to that level's width
and the active region. At a word root, use the root bit in the adjacent sibling
word for the same test. A clear active root is the special case representing
the entire backed region as one free block. A clear root of an arbitrary word
is not by itself a free-block candidate: its ancestor may own the whole region.

Split the selected block down to the requested order, setting
the chosen path and its ancestors only as far as the active root. Unselected
free buddies and descendants of the final direct allocation remain clear.
Crossing an endpoint means selecting one of its two child-word roots and
continuing with the same rule. Search summaries accelerate this selection but
must agree with the underlying reservation/active-region rules.

Splitting an upper-word endpoint activates its two child-word summaries. The
free sibling advertises its root order, while the selected child advertises
whatever maximal free blocks remain after further splitting/allocation. Merging
both children back into that endpoint removes both routing entries and advertises
the endpoint at its own order. These rules apply recursively: a whole free or
directly allocated block has no participating descendant routing entries.
Allocation of a whole large block therefore does not require clearing an entire
descendant tree of summaries; its descendants were already excluded while that
block was represented as one free block.

After mutation, compare the affected child's previous and resulting availability
masks. For each changed order, update its bit in the parent's corresponding
routing mask. A routing mask transitioning between zero and nonzero may change
the parent's advertised orders. Combine that with changes to locally represented
free blocks and with boundary activation/deactivation; propagate only resulting
availability changes upwards, stopping at the active root. Update the affected
path and sibling relationships, not every child word.

Initially recompute an affected word's small availability mask from its local
free-block masks and routing-array nonemptiness. Additional per-word cached
availability masks are optional and are not included in the storage totals.
No child-word scan is needed for this recomputation. Keep mutation externally
indivisible under the exclusive-access contract; no partially updated summary
state may be observed through a public operation.

On initialization/reset, all child-routing masks are zero and the global
availability mask is `1 << K`, where `K` is the active allocation order. Only
the backed subtree is advertised, even when its root lies inside a packed word.
No summary above that root participates. Test-only integrity reconstruction
should independently derive the expected summaries from reservation state and
active extent; ordinary allocation/free need not run that full reconstruction.

All index API inputs and outputs use smallest-slot units, regardless of the
selected order. Validate range and block-start alignment before mutation. For
an upward free search, a clear current slot permits ascent only from the first
child and only when its buddy is also clear. A live buddy means the parent is
reserved for descendants, not a direct allocation to release. Apply this rule
consistently across packed-word boundaries. At a clear word root, an even word
index and a clear adjacent root permit ascent to the previous layer's endpoint;
an odd index or a reserved buddy rejects the proposed allocation start. Stop at
the active root and reject if no direct allocation was found. A set node reached
this way is a direct allocation because the descent-side pair was established
clear, or because the node is a final leaf.

After validation, clear the allocation bit and ascend while the sibling is
clear, clearing each parent reservation. Coalescing accepts either child side;
the first-child restriction applies to recovering an allocation start, not to
merging after a valid release. Stop at the active root or the first reserved
sibling. For 64-bit words, boundary ascents are order 5 to 6 and order 11 to 12;
for 32-bit words they are order 4 to 5 and order 9 to 10.

An in-memory design model checked both widths at every active-root order
(33 configurations), comparing free-block selection to an independent interval
model and checking boundary recovery, repeated-free rejection, and complete
coalescing. This supports the logical rules; it does not replace implementation
tests or establish performance.

The PoC reference is `D:/TheVoidEngine-Legacy/common/libs/allocators/buddy_index.*`.
Reuse its index/payload separation, preference for existing split pairs, and
transition-based wide summary updates. Do not copy its unscaled parent-level
allocation return values or its unchecked upward free traversal. The latter
issue requires an invalid free, such as freeing the first child twice while
the second remains live; valid-use behaviour should be assessed separately.

Use ordinary unsigned words initially. A packed word is a unit for bit
operations, not a claim of thread safety. Each manager/chain requires exclusive
access, consistent with the per-operation reflection owner. Concurrent
allocation and cross-word atomic protocols are outside this initial design.

## Compact manager, backing size, and ownership

The manager object must remain small. Reservation words, child-routing arrays,
the global availability mask, and any future per-word availability caches all
live together in the separately owned management allocation, not inline in the
movable manager. The allocation also contains alignment padding and the exposed
payload region. An owning memory token can hold that allocation; compact
configuration and offsets describe its parts. Routing storage must not introduce
another ownership allocation or enlarge each `TVector` element with inline
arrays. Check all size and alignment arithmetic against the memory substrate's
limits before allocation.

Moving a manager transfers this ownership without moving management data or
payload bytes. Its moved-from state is empty and safely destructible. Avoid
self-pointers into the movable manager; derive views from the owned allocation
or maintain only pointers whose targets remain stationary. This permits
`TVector<TBuddyAllocator<TWord>>` growth without invalidating vendor pointers.

Management bytes are never exposed to the vendor. Pointer validation uses the
payload base and actual backed extent, excluding metadata and padding. A shared
underlying allocation is not a hardware protection boundary: arbitrary vendor
out-of-bounds writes can still corrupt management or unrelated memory. Optional
guards add detection, not memory isolation.

Accept two positive configuration values in bytes: `minimum_slot_size` and
`backing_size_limit`. They express intent and need not be powers of two. Normalize
them as follows, with checked integer operations:

- Effective slot size `S = bit_ceil(minimum_slot_size)`: the smallest power of
  two at least as large as the requested minimum.
- Effective payload backing `P = bit_floor(backing_size_limit)`: the largest
  power of two no larger than the requested payload limit.

Exact powers of two remain unchanged. Reject either zero input, slot-rounding
overflow, `P < S`, a slot count `P / S` beyond the chosen word scheme, or a
combined allocation/conditioning overflow or substrate-limit violation. Validate
the normalized pair before allocation or mutation. Do not further shrink the
backing, enlarge the slot, or change schemes to hide an invalid combination.

For example, a 24-byte minimum and 100 KiB payload limit produce 32-byte slots
and 64 KiB of payload backing. A 17-byte minimum and 31-byte payload limit are
rejected: they normalize to a 32-byte slot and only 16 bytes of backing.

The backing limit applies only to usable payload. Management state, routing,
alignment padding, and allocator conditioning are additional ownership costs,
reported separately through the complete allocation footprint. Thus `P` never
exceeds the requested payload budget; the complete allocation can exceed that
number. The limit is per manager, not a total budget for a growing chain.

Store the backing-size exponent `B` and slot-size exponent `s`; active allocation order
is `K = B - s`, with `0 <= K <= D`. The active region is the contiguous prefix
`[0, 2^K)` in slot units. Its root is global node `(depth = D - K, index = 0)`;
it may occur inside a word rather than at a word root.

Initialize all reservation words to zero. Allocation searches and summaries
operate only in that active subtree, and free/coalescing never ascend above it.
All other nodes, including conceptual ancestors, are outside the managed tree.
Thus the unbacked suffix needs no fake allocation bits, sentinels, or per-slot
mask. It remains unavailable by configuration. Restrict level masks to the active
subtree before searching; do not globally interpret every clear bit as usable.
Reset clears state while retaining the same active root. Independently reject
addresses beyond the actual backing extent before reading allocation state.

For example, 32 KiB backing and 16-byte slots give `K = 11`. In the 64-bit
scheme the active root is middle word 0's root (global depth 6); in the 32-bit
scheme it is at depth 3 within the top word. The same active-root rule handles
both without special blocked-region reservations.

Backing size and smallest slot size are independent configuration choices. A
smaller backing retains the same tree geometry and slot size while disabling
unused regions through the active-root limit. Include the word-width-dependent
fixed management cost when evaluating small backings; neither 256 KiB nor 2 MiB
is a required initial payload allocation. Size the initial metadata for the chosen
scheme; compacting away inactive management words is a separate optimization.

Align the payload base to the normalized smallest slot size. Every returned
pointer guarantees that alignment; the initial per-request API accepts only a
byte count, with no separate alignment argument. Reflection must configure a
slot size of at least `alignof(std::max_align_t)`. Do not promise stronger
alignment merely because an allocation occupies a larger buddy block.

The combined allocation must satisfy both slot alignment and the management
types' alignment. Its layout is management header, reservation words, routing
arrays, padding to slot alignment, then payload backing. Compute offsets using
checked arithmetic and validate the complete conditioned allocation footprint.

Capture an explicit, non-owning `CMemoryContext` that outlives all storage.
Allocation/free operations must not rediscover the calling thread's ambient
context. Provision internal container contexts while empty, using existing
public attribution APIs if needed; do not mutate context pointers without the
required accounting operation. Existing containers do not all expose
context-taking constructors, so verify provisioning in the implementation.

Context accounting covers actual backing allocations, including management
and padding. Buddy allocation counts and reserved bytes are separate logical
statistics and must not be added again as physical context allocation charges.
No per-vendor-allocation context pointer is required. Whole-owner movement
preserves attribution; cross-context transfer is not needed for initial use.

## Standalone allocator contract

The index layer allocates a rounded slot extent and releases a validated exact
start index. The owning manager translates byte requests to orders and returned
indices to payload addresses. Adopt the following public operation shape:

```cpp
struct SBuddyConfig
{
    std::size_t minimum_slot_size{ 0u };
    std::size_t backing_size_limit{ 0u };
};

bool initialise(
    const SBuddyConfig& config,
    memory::CMemoryContext& context) noexcept;

void* allocate(const std::size_t bytes) noexcept;
EFreeResult free(void* const pointer) noexcept;

std::size_t allocation_size(const void* const pointer) const noexcept;

void reset() noexcept;
void deallocate() noexcept;
```

`SBuddyConfig` expresses minimum slot and backing-size intent in bytes. Store
the normalized configuration internally as exponents. Initialization prepares
the combined management/payload allocation through the explicit context. It is
permitted only when the object has no backing, even if an existing backed object
currently has no live suballocations. Reject reinitialization without mutation;
use `reset()` for reuse or `deallocate()` before changing configuration.

The manager is default-constructible in an empty, unready state, non-copyable,
and nonthrowing-movable/destructible. Movement preserves payload addresses and
attribution; moved-from objects are empty, unready, and safe to destroy or
initialize again. Move assignment releases the destination's previous backing
before transferring ownership. No cloning operation is provided.

`reset()` invalidates all suballocations and retains backing, configuration,
and context. `deallocate()` and destruction release the combined allocation
without supplier calls. After deallocation the object is empty and unready and
can be initialized again with explicit configuration/context. These lifecycle
operations do not replace the integration owner's pre-release leak audit.

`allocation_size()` returns the rounded usable byte extent for an exact live
allocation start, or zero otherwise, without modifying state. The recovered
extent is not the original requested size.

| `EFreeResult` value | Meaning |
| --- | --- |
| `released` | A live allocation was released |
| `no_action` | The supplied pointer was null |
| `outside_backing` | The pointer is outside this manager's active payload, including when the manager has no backing |
| `invalid_allocation` | The pointer lies within the payload but is not an exact live allocation start |

The last category includes misaligned, interior, and currently unallocated
starts; the API does not promise to distinguish their histories. Rejection must
leave reservations, routing summaries, and counters unchanged. Core returns
the result without vendor-specific logging or a sticky error flag. The reflection
owner decides how to report it and mark the operation failed.

Expose readiness, effective slot/backing sizes, live allocation count, live
rounded allocation bytes, largest available block, and live allocation
enumeration for auditing. Enumeration visits exact starts and rounded extents
under exclusive access; callbacks must not mutate the allocator. Keep these
counters in the allocated management header where practical. Normalized sizes
must be observable so callers can see the configuration actually obtained.
Physical ownership attribution covers the single combined allocation; requested
byte history and additional statistics remain separate diagnostic concerns.

The implemented observers are `is_ready()`, `is_empty()`, `slot_size()`,
`backing_size()`, `allocation_count()`, `allocated_size()`, and
`largest_available_block()`. `total_owned_size()` reports the actual conditioned
allocation cost, including management and padding, while `memory_attribution()`
observes that allocation through the existing token accounting. The owner is
exactly one `CMemoryToken` in size; it does not store routing or counters inline.
`visit_allocations(visitor)` invokes `visitor(const void*, std::size_t)` in
address order with each live start and rounded extent. The callback must not
mutate the allocator. An empty or unready owner produces no callbacks.

`buddy_detail::TPackedIndex` is an internal mechanism, not an owning public API.
It assumes an initialized active order in the supported range. The owning API
checks byte configuration, pointer range and slot alignment before using it.
Its allocation result is always a smallest-slot index, independent of the
requested order.

The implementation updates summaries for the changed orders only. Splitting
an order `j` block for an order `r` request changes maximal-free sets at orders
`r..j`; merging reverses those changes. For each such order, recompute the
affected word's local candidates and propagate its presence through the routing
bitmaps. If the changed node is a packed-word root, refresh the sibling word
too, because its maximal-free test depends on that root. Propagation stops when
a parent bitmap's zero/nonzero status stays unchanged. Neither allocation nor
free scans the word arrays. Reset deliberately clears all management arrays.

Required behaviour:

- Apply the adopted configuration rounding, then reject invalid combinations,
  overflow, unrepresentable configuration alignment, oversized allocation
  requests, and exhausted capacity without changing live allocations.
- Reject zero-size allocations; a vendor adapter may deliberately normalize
  them to one byte. Failed allocation returns null without changing live state.
- Return `no_action` for null free, distinct from invalid non-null frees.
- Validate range, minimum alignment, exact live allocation start, and recovered
  allocation order before changing reservation state or counters.
- Reject unknown pointers, interior pointers, unbacked slots, and currently
  unallocated starts without dereferencing vendor memory or mutating state.
- Return rounded usable extent for a valid allocation. Requested-size tracking
  is optional diagnostic state, not silently inferred from rounded extent.
- Reset invalidates all suballocations and preserves the active-root limit;
  complete deallocation releases storage without calling supplier code.
- Reject initialization while backing is owned, whether or not it has live
  allocations. Failed initialization preserves the object's prior state.

Pointer comparison and offset calculations must follow the engine's supported
address-representation conventions, avoiding subtraction of unrelated C++
pointers and overflow in end-address calculations. Range checks alone do not
establish allocation membership. Reused addresses cannot distinguish stale
pointers from new live allocations without an additional temporal mechanism.

## TVector chaining utility

`TBuddyAllocatorChain<TWord>` owns compact, nonthrowing-movable managers in a
`TVector<TBuddyAllocator<TWord>>`. Aliases `CBuddyAllocatorChain32` and
`CBuddyAllocatorChain64` select the word width. The vector is the chain's only
member; configuration and context are obtained from the first retained manager.
All managers use the same normalized backing/slot configuration and context.

`initialise(config, context)` prepares one empty manager and its vector storage
transactionally. It rejects initialization while ready, including an empty but
backed chain. The context must outlive all owned storage. The new explicit-context
`TVector` constructor configures descriptor storage without requiring an ambient
context or allocating during construction.

`reset()` clears logical allocations but retains every manager and vector
capacity for reuse. `deallocate()` and destruction release all physical storage
without supplier calls. Moves transfer ownership without moving payloads or
allocating; assignment releases the destination's previous ownership. Moved-from
and deallocated chains are empty, unready and can be initialized again. Empty
managers are not erased during operation, avoiding backing allocation churn.

Search managers in creation order and use the first with a sufficient available
order. Grow when none can satisfy the request, including fragmentation cases
where free bytes remain. Zero-sized requests fail without allocating.
Requests larger than one configured backing are reported as unsuitable for the
chain; they do not cause futile manager creation.

Create a candidate manager privately, obtain its backing and requested block,
then publish it into the vector. If backing allocation or vector insertion
fails, release the candidate and return failure without exposing its pointer
or changing existing live allocations, manager count or vector capacity. Test
failure at each actual allocation point, including vector growth after the
private candidate has obtained its requested block.

Locate a pointer's manager with a linear range search, preserving the standalone
free-result categories and exact-start `allocation_size()` queries. Record lookup
cost and manager count before adding an ordered range index. Manager objects
are not exposed by the public API. Any future index should refer to stable
IDs/indices or backing addresses, not vector element addresses.

`slot_size()` and `backing_size()` describe one manager; `manager_count()` and
`manager_capacity()` describe the vector. `allocation_count()` and
`allocated_size()` are 64-bit sums across managers; `largest_available_block()`
is the largest available block in any retained manager. These aggregate queries
scan the managers. `is_empty()` observes the aggregate allocation count, while
`is_ready()` means at least one manager is retained. `visit_allocations()` visits
managers in creation order and allocations in address order within each manager.
Callbacks must not mutate the chain; all access requires external exclusion.

`TVector::memory_attribution()` is shallow. The chain's deep
`memory_attribution()` explicitly includes vector storage and each manager's
owned allocation exactly once. `total_owned_size()` reports that physical cost
as a 64-bit sum. Logical reserved bytes are not charged again to the context.
Detailed requested-byte telemetry remains separate; cross-context reattribution
is not exposed by the chain.

## Staged delivery and acceptance

### 1. Standalone representation and allocator

Create `core/allocators/` with the reviewed index and manager implementation.
First establish the bit representation, cross-word boundary invariants,
availability summaries, fixed-unit index convention, and compact ownership
layout. No SPIRV-Reflect integration is part of this stage.

Add focused allocator tests using the existing test allocator/failure support:

- Every order, exact fit, exhaustion, split/merge, arbitrary valid free order,
  and complete recovery after all allocations are released.
- Packed-word transitions and blocks spanning multiple child subtrees,
  especially order 5/6 and 11/12 for 64-bit words, order 4/5 and 9/10 for 32-bit
  words, and the complete root.
- Multiple large allocations returning distinct correctly scaled starts.
- Invalid/interior/out-of-range frees, repeated-free-with-live-buddy regression,
  and state/counter equality before and after every rejected operation.
- Every active-root order, including roots inside a packed word; attempts to
  free unbacked regions; and reset after mixed allocation sizes without exposing
  unused space or ascending into inactive ancestors.
- Exact-order availability and routing compared with an independent traversal
  of the reservation tree after each tested mutation. Cover child activation,
  coalescence/deactivation, zero/nonzero routing transitions, full/exhausted
  subtrees, and allocation of a whole block whose descendants must stay excluded.
  Verify smallest-sufficient-order selection and deterministic address ties in
  both word widths, including active roots inside words. Rejected operations
  must preserve summaries as well as reservations and counters.
- Alignment, zero size, overflow, configuration limits, and allocation failure.
- Configuration normalization at exact powers of two and immediately adjacent
  values: upward minimum-slot rounding, downward payload-budget rounding,
  effective slot greater than effective backing, scheme-capacity excess, and
  rounding/combined-footprint overflow. Verify effective sizes and total cost
  reporting, with no silent extra configuration adjustments.
- Public free-result categories, nonmutating allocation-size queries, rejection
  of initialization on any backed object, reset/reuse, explicit deallocation,
  and safe reinitialization of moved-from or deallocated objects.
- Nonthrowing moves, moved-from destruction, retained payload contents, and
  final context counts/bytes returning to baseline.
- A simple independent reference model with deterministic mixed-size sequences;
  compare nonoverlap, live boundaries, allocation/free results, and recovered
  capacity. Exercise reduced trees exhaustively where practical.

### Initial standalone implementation validation — 07 November 2026

The ordinary test suite passes on Windows Debug and Release, each for x64 and
Win32, and on Linux GCC Debug. Each run includes **1,165,977 buddy checks** across
both word widths. The Linux policy include tests also pass. The packed-index
portion separately passes **933,627 checks** with AddressSanitizer and
UndefinedBehaviorSanitizer enabled.

The tests reconstruct reservations and routing independently from an ordered
map of live intervals, compare exact state after mixed operations and rejected
calls, explore bounded exhaustive sequences on a small tree, and fill/release
the complete capacity of both schemes. Owning tests additionally cover byte
rounding, payload contents, enumeration, explicit-context isolation, allocation
failure, moves and final attribution. `std::map` is confined to the test oracle;
the production index uses fixed arrays and allocation/free perform no further
backing allocation after initialization.

Windows validation used single-worker MSBuild. Release module DLLs were rebuilt
before the successful ordinary runs so dynamically loaded modules matched the
test executables. The repository-wide line-ending helper reports the existing
modified solution file's CRLF endings; that unrelated edit was preserved.
Changed-file line-ending checks and the scoped whitespace diff check pass.
Local build/test records are under `build/buddy-*`; the final Release runs use
the `*-matched-tests.log` files. The sanitizer driver is a local build artifact,
not an additional production or test-runner entry point.

### Standalone logical review — 07 November 2026

No allocator correctness defect was found under the documented exclusive-access,
valid-metadata and context-lifetime requirements. The 14 `TPackedIndex` function
bodies now follow the class declaration in the same header. Their signatures
and bodies were compared with the pre-refactor snapshot; only qualification and
indentation changed. Template definitions remain available to both word-size
instantiations, and the static helpers retain their `constexpr` declarations.

The review checked the following invariants directly, rather than inferring
correctness from the test count:

- **Geometry:** three distinct-node tiers contain `(W - 1) * (1 + W + W^2)`
  nodes, equal to `W^3 - 1`. Their smallest-slot counts are therefore 16,384 and
  131,072. Local root bits are 30 and 62; the spare bit is never addressed.
  Each upper endpoint has two distinct child-word roots. Returned indices are
  always in smallest-slot units, including across both word boundaries.
- **Free recovery:** upward traversal proceeds only from a clear left child
  with a clear sibling. Thus the first reserved ancestor encountered has no
  reserved children and represents a direct allocation, not a split node.
  Odd-child and live-sibling checks reject interior starts and the repeated-free
  case with a live buddy. Coalescing stops at the active root or a reserved buddy.
- **Availability:** a reachable clear node beside a reserved sibling is a
  maximal free block; the active root is the exception. Closed descendants
  remain clear and cannot advertise smaller blocks. A split or merge changes
  only the orders between the allocation and the selected/coalesced block.
  Refreshing both words at a word-root boundary preserves sibling contributions;
  propagation may stop when a parent bitmap's zero/nonzero state is unchanged.
  Lowest-set-bit selection consequently chooses the smallest sufficient order,
  then the lowest address, without scanning word arrays.
- **Configuration and ownership:** accepted power-of-two payloads are below
  the 2 GiB substrate ceiling, hence at most 1 GiB. Slot size is bounded by that
  payload, so metadata-offset arithmetic is safe on 32-bit hosts; the combined
  conditioned footprint is checked before allocation. Range and alignment checks
  precede index access. Initialization publishes only a completed candidate,
  and default moves delegate ownership and source clearing to `CMemoryToken`.

The review identified two coverage gaps: the interval oracle and full-capacity
tests reused implementation geometry constants and equivalent address formulas,
and the owner move tests used one context throughout. Neither exposed an
allocator failure. The follow-up below adds independent geometry expectations
and verifies move assignment between distinct contexts.

The small-tree exploration is exhaustive only through seven operations on four
slots. Mixed tests use deterministic finite sequences, while full-capacity
fill/drain checks do not reconstruct every intermediate routing state. These
limits are why the invariant review complements the runtime checks.

Post-refactor validation passed the Linux GCC Debug ordinary and policy suites
(1,165,977 buddy checks), the packed-index AddressSanitizer/UndefinedBehaviorSanitizer
run (933,627 checks), and MSVC x64/x86 syntax checks instantiating both word sizes
with the repository's Debug policy definitions. Records are under
`build/buddy-review-*`. The tests themselves were unchanged during this review.

### Review follow-up tests — 07 November 2026

`test_literal_geometry` now fixes the expected capacity, maximum order, word
count and levels per word with literal assertions for each scheme. Twenty-one
literal node/word/bit anchors per word size cover root bits 30/62, endpoints,
both child-word boundaries, adjacent words, and the far end of every tier.
Encoding checks the complete reservation-word image. Decoding starts from a
separately written literal image, so matching encoding and decoding errors
cannot satisfy a simple round-trip test.

`test_cross_context_move` uses two explicit contexts with distinct allocator
states and unrelated live allocations establishing nonzero baselines. Both
owners have live suballocations before move assignment. The test checks release
of the destination's previous backing, unchanged source attribution and payload,
absence of new backing allocations during the move, reuse of the moved-from
object under the other context, logical free/reallocation, and final destruction.
Both contexts must return first to their original baselines and then to zero
after the unrelated sentinel allocations are destroyed. Both new tests run for
32-bit and 64-bit packed words; production allocator code is unchanged.

Validation passed the ordinary suites on Windows Debug x64/Win32 and Linux GCC
Debug, with **1,166,231 buddy checks** per run (254 added runtime checks), plus
the Linux policy tests. The updated geometry/index sanitizer driver passed
**933,795 checks** under AddressSanitizer and UndefinedBehaviorSanitizer. MSVC
x64/x86 syntax checks also instantiated both word sizes successfully. Local
records are under `build/buddy-coverage-*`; changed-file line endings and
whitespace checks passed.

### 2. Chain utility

Implemented in `TBuddyAllocatorChain.hpp` with tests for both word sizes covering
suitable-manager selection, fragmentation growth, insertion/backing failures,
oversize rejection, vector relocation, pointer routing, retained-manager reuse,
collective cleanup, and deep ownership accounting. Existing payload pointers and
contents survive chain growth. Tests also cover initialization with no ambient
context, initialization rollback, moves between distinct contexts and self-move.

Register sources/tests through the existing Visual Studio shared items and test
runner; CMake derives source lists from Visual Studio manifests. Preserve the
CRLF/encoding conventions of shared-item and filter files. Follow the repository
build/test instructions and relevant Windows architecture/configuration and
Linux checks once per implementation checkpoint, without repeating passing
matrices absent a change or concern.

Validation on 07 November 2026 passed the full ordinary suites on Windows Debug
x64/Win32 and Linux GCC Debug. Each run reported 1,144 chain checks covering both
word sizes and 1,949 `TVector` checks, with zero failures. The standalone buddy
suite also retained its 1,166,231 passing checks. MSVC x64/x86 syntax checks,
Windows build-time policy validation and Linux CTest policy checks passed.
Changed-file line endings and whitespace checks passed. Local build/test records
are under `build/buddy-chain-*`; Release configurations were not rerun for this
stage.

### 3. Shader allocation analysis

After standalone allocator and chain acceptance, add a private measurement path
at the adapter's `malloc`, `calloc`, and `free` boundary. First capture actual
vendor requests and lifetime events using the existing bump backing, with free
events recorded even though physical reclamation remains deferred. This provides
a baseline independent of the new allocator's routing and reuse decisions.

Use deterministic allocation IDs rather than raw addresses in exported traces.
Record shader/operation identity, phase, event sequence, allocation kind,
requested byte count, required alignment, success, free result, and allocation
ID. For `calloc`, validate multiplication and record both dimensions and the
product. Keep zero-byte requests in a separate bucket while recording the actual
one-byte normalization used by the current adapter. A complete-trace flag and
explicit recorder failure are required; truncated evidence must not be presented
as a valid complete measurement.

Recorder storage and output must bypass the observed vendor allocation path.
Track requested extents externally during collection; the existing bump owner
cannot recover them from a pointer. Preserve the existing failure contract and
avoid introducing recursive allocation through diagnostics.

Run the existing reflection and conditioning corpus, including all six stages,
resource/interface/aggregate fixtures, and `arena_measurements()` generated
scaling shaders with both debug-free and source-debug compilation. Include
ordinary failure/unsupported paths as separate result classes. Keep intentional
corruption and allocation-failure injection runs separate from normal workload
statistics. Capture the pinned SPIRV-Reflect revision, DXC version/options,
platform, pointer width, build configuration, artifact identity/hash, and corpus
coverage so later vendor updates can be compared meaningfully.

Bucket positive requested sizes by the smallest enclosing power of two: bucket
`2^k` covers `(2^(k-1), 2^k]`, with size 1 handled explicitly. Report counts,
requested-byte totals, hypothetical rounded totals/waste, and largest request
per bucket, per shader, and in aggregate. Do not confuse cumulative allocation
traffic with peak live demand, or sum unrelated per-bucket peaks as if concurrent.

Replay ordered allocation/free traces through the tested allocator for candidate
word widths, slot sizes, active backing sizes, and exceptional-allocation thresholds. Candidate
slot sizes start at the required alignment and increase by powers of two; do not
assume that the provisional 16-byte slot is optimal on every target. For an
eligible request, rounded buddy size is `bit_ceil(max(requested_bytes, S))` under
the initial alignment contract. Ensure active capacity fits `16384 * S` for
32-bit words or `131072 * S` for 64-bit words.

Compare both fixed-backing experiments (isolate slot granularity) and complete
configurations. Include peak live requested/reserved bytes, peak retained backing
plus metadata/container overhead, manager count, fragmentation-triggered growth,
exceptional counts/bytes, and lookup/allocation cost. Report per-shader outliers
as well as aggregates. Recommend a configuration against stated memory and
runtime objectives; a histogram alone cannot establish an optimum. Record the
corpus as evidence, not a bound on arbitrary future shaders.

Deliver a machine-readable trace/bucket report and a repository analysis note
with configuration comparisons. Select production defaults only after reviewing
that evidence. This stage does not require switching normal reflection to the
buddy owner.

### 4. Exceptional allocations and reflection integration

Retain the agreed large-allocation path: `TOrderedCollection<CByteBuffer, Key>`,
keyed by the exact exposed allocation address, with logarithmic lookup. Its
threshold is chosen from stage 3 and can be below a manager's theoretical or
configured maximum. The reflection owner composes this collection with the
chain. If a separate reusable wrapper proves useful, keep vendor policy out of
its Core interface.

Allocate a temporary buffer, obtain its address, and move it into the collection
only once allocation succeeds. Expose the pointer only after insertion succeeds.
Erasure destroys its buffer. Never resize an indexed buffer without replacing
its address key transactionally. Collection attribution is shallow: aggregate
contained-buffer storage explicitly. Provision these allocations through the
owner's explicit context. Supplier-specific `realloc` support is not required
by the current reflection adapter and is not an initial deliverable.

Replace the private bump callbacks with validated allocation/free routing while
preserving active-owner scope and the lifetime through extraction and vendor
destruction. Null free is inert; unknown/interior/already-free pointers produce
an audit failure without state mutation. The Core allocator returns structured
failure information; the Rendering adapter records a sticky operation failure
and emits nonrecursive diagnostics. Decide whether any corruption category
requires immediate termination before enabling this path in normal execution.

Normal successful creation follows: vendor creation, engine extraction/copy,
vendor destruction, outstanding-allocation audit, independent backing release.
Creation failures use the cleanup actually valid for that vendor state, audit
residual allocations, and then release backing independently; never call vendor
destruction blindly on an incompletely initialized module. Fallback owner
destruction releases backing without supplier calls. Report live allocations
before sweeping so recovery does not hide leaks. Integration must propagate
audit failure into the operation/test result before a success result is published.

Preserve recoverable preparation failure and fatal in-vendor allocation failure
until a separate vendor failure-path audit justifies changing them. Chaining
removes the need for the old estimate to predict all retained demand, but input
limits and any overall memory budget remain explicit. Review estimation fields,
preparation, statistics, and panic evidence deliberately; do not silently assign
new meanings to existing `reflection_arena_*` fields.

Use the existing `omit_code_release`, `interior_code_release`, and
`unknown_code_release` fixtures as regression starting points. Change the
current ignored-free expectations to verify detection, preserved allocator
state, failed audit status, and complete independent reclamation. Add double
free, mixed buddy/large allocation, chain growth, and vendor-update baseline
cases. Adapt the disposable-process fatal tests to the actual new failure points
and diagnostics. Confirm reflected schema results and independent result
lifetimes remain unchanged for valid inputs.

## Diagnostic scope and remaining checkpoints

Required initial protection is external allocation bookkeeping, exact-start free
validation, outstanding-allocation audit, and independent cleanup. Exact-request
metadata is required for analysis, but need not be permanently attached to every
buddy allocation in the generic allocator. Guards, poisoning, and delayed reuse
are optional follow-up diagnostics; decide their storage/cost separately and
include their overhead in any measured configuration. This allocator cannot
prove absence of arbitrary writes, use-after-free, or stale-pointer reuse.

The distinct-node mapping, active-root limit, exact-order child-routing summaries,
compile-time word-width API, compact ownership, configuration rounding, and
standalone lifecycle/failure contract above are the adopted design. Configuration
limits apply to payload backing, with total ownership cost reported separately.
Implementation-level helper names and enumeration syntax may follow repository
conventions without changing these contracts. Implementation starts only on a
separate instruction. After standalone acceptance, perform the shader
measurements. After measurement review, settle
slot/backing sizes, large threshold, limits, integration error policy, and any
additional diagnostics. No Audio changes, vendor update, memory-token mode
extension, or further container redesign is implied by this plan.
