Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    buddy_index.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    07 Nov 26

# Packed buddy index

[`buddy_index.hpp`](../../core/allocators/buddy_index.hpp) defines the internal
`buddy_detail::TPackedIndex<TWord>`. It owns no backing allocation and uses only
smallest-slot indices and allocation orders. The
[standalone owner](TBuddyAllocator.md) supplies storage, translates byte requests
and pointers, and enforces configuration limits. Index helpers and fields are
implementation details rather than a general public container API.

`TWord` is restricted to `std::uint32_t` or `std::uint64_t`. All words are
ordinary, non-atomic values; access must be exclusive during mutations.

## Geometry and bit layout

There are three layers of packed complete subtrees. Each upper-word endpoint
has two distinct children, represented by roots of separate words in the next
layer. No node is duplicated at a word boundary.

| Property | 32-bit words | 64-bit words |
| --- | --- | --- |
| Word width `W` | 32 | 64 |
| Node levels per word `H` | 5 | 6 |
| Node bits per word | 31 | 63 |
| Endpoint nodes per word | 16 | 32 |
| Top / middle / bottom words | 1 / 32 / 1,024 | 1 / 64 / 4,096 |
| Maximum order `D = 3H - 1` | 14 | 17 |
| Smallest slots `2^D` | 16,384 | 131,072 |
| Top / middle / bottom order ranges | 10–14 / 5–9 / 0–4 | 12–17 / 6–11 / 0–5 |

Order zero is one smallest slot; order `j` covers `2^j` slots. The uppermost bit
of each word is unused and remains zero. Low bits hold the endpoint level;
successive parent levels occupy higher bits:

| Scheme | Endpoint-to-root bit ranges |
| --- | --- |
| 32-bit | 0–15, 16–23, 24–27, 28–29, 30 |
| 64-bit | 0–31, 32–47, 48–55, 56–59, 60–61, 62 |

For an order `j` and node index `n` within that order:

```text
layer       = (D - j) / H
local_depth = (D - j) % H
level_count = 1 << local_depth
word        = n >> local_depth
bit         = W - (2 * level_count) + (n & (level_count - 1))
```

Layer bases in `words[]` are `0`, `1` and `1 + W`. Increasing positions within
a level follow increasing slot addresses. Endpoint `i` of word `w` has child
words `W*w + 2*i` and `W*w + 2*i + 1`. A child word's sibling is `word ^ 1`.

## Reservation and active-region invariants

Within a reachable active subtree, a clear node is unreserved. A set node with
clear children represents a direct allocation; a set node with reserved
children represents a split block. Descendants beneath a direct allocation
remain clear but are unavailable. Bottom endpoints have no children.

`reset(active_order)` clears all reservation/routing arrays and counters. It
sets the availability mask to advertise only the whole active root. The active
region is the prefix of `2^active_order` slots rooted at node zero of that order.
`active_order` must be at most `k_max_order`; callers must establish this before
calling the internal reset function. Value initialization alone does not
advertise a free root; reset establishes a usable index.

A smaller active region retains the full management arrays. Nodes above the
active root and slots beyond its prefix are excluded by bounds and candidate
masks. They do not need fake reservations. `k_no_slot` equals the full geometry's
slot count and is outside every usable region.

## Availability and routing

The 32-bit `available` mask records exact orders of maximal free blocks. A whole
free order-8 block advertises order 8 only, rather than all sizes into which it
could split. A free node is maximal when its sibling is reserved; a clear active
root is the sole exception. This excludes clear descendants hidden beneath a
whole free or directly allocated ancestor.

`candidates(order, word)` returns positions within the requested local level,
not packed bit offsets. For levels contained in one word, candidate positions
are clear bits whose adjacent buddy bits are set. Word roots inspect the root
in the sibling word. Masks also exclude inactive slots.

`top_routes[2 * H]` and `middle_routes[W][H]` hold per-order child-word masks.
A bit means that child subtree contains a maximal free block of the specified
order. Bottom words need no routing arrays. Routes use global order indices;
the top routes cover orders below the top word, and middle routes cover orders
below a middle word.

Search masks out insufficient orders in `available`, then bit-scans the lowest
remaining order. It follows the lowest eligible route bits from the active
root, making at most two child-word decisions, and finally bit-scans a local
candidate. It does not scan the array of reservation words.

`refresh()` updates a word's route contribution and propagates only zero/nonzero
changes toward the active root. `refresh_path()` visits the changed orders; at
a word root it also refreshes the sibling word, whose maximal-free test depends
on the changed root. This boundary update is necessary even though nodes are
stored only once.

| Storage component | 32-bit scheme | 64-bit scheme |
| --- | --- | --- |
| Reservation words | 4,228 bytes | 33,288 bytes |
| Top routes | 40 bytes | 96 bytes |
| Middle routes | 640 bytes | 3,072 bytes |
| `available`, `active_order`, `live_count`, `live_slots` | 16 bytes | 16 bytes |

These are component sizes before any implementation-dependent structure padding.
Owner metadata and payload alignment padding are additional.

## Allocation and release

`allocate(requested_order)` returns a smallest-slot start or `k_no_slot` on
failure. After finding the smallest sufficient maximal free block, it reserves
the chosen path from the requested order through the active root. Unselected
buddies remain clear. It refreshes the affected summaries and increments
`live_count` and `live_slots` by one block and `2^requested_order` slots.

`allocation_order(slot)` returns the order for an exact live start, or `-1`.
Starting at the slot's leaf, it ascends only through left children with clear
siblings until it finds a reservation. A right-child start cannot ascend into
an enclosing block, and a clear left child next to a reserved right buddy is
rejected. The latter rule prevents a second free of that left child from being
mistaken for its split parent.

`release(slot)` first performs this validation. On success it clears the direct
reservation, then clears ancestors while their other children are unreserved.
It refreshes the affected path and adjusts counters, returning the released
order. Invalid input returns `-1` without mutation. The index assumes its own
metadata invariants hold; it cannot identify stale pointers after slot reuse.

Helpers such as `reserved()`, `set_reserved()` and `route()` assume valid
geometry indices. Arbitrary direct field/helper mutation can break the
reservation, routing and counter invariants. Ownership callers use reset,
allocate, lookup and release rather than editing these arrays.

## Review and tests

[`BuddyAllocator_test_suite.cpp`](../../tests/test_suites/BuddyAllocator_test_suite.cpp)
checks both representations using independent interval reconstruction, literal
geometry cases, bounded exhaustive sequences, mixed operations and full-capacity
fill/release. It checks cross-word transitions and rejected-free preservation
as well as the owning interface.
