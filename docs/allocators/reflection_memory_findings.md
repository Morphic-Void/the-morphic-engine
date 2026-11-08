Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    reflection_memory_findings.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    08 Nov 26

# SPIRV-Reflect memory findings

Windows x64 measurements covered 47 unique successful shader artifacts across
all six stages, with and without source debug information. These comprised
41 ordinary fixtures and six generated stress fixtures. Each artifact had equal
weight; the averages do not represent a production shader frequency distribution.

The 6,887 allocation requests totaled 13,630,540 bytes. Requests of at most
16 bytes accounted for 73.8% of calls but only 0.28% of requested bytes. Requests
above 1 KiB accounted for 4.4% of calls and 96.1% of bytes. Three requests exceeded
1 MiB; the largest was 2,210,656 bytes. An independently tracked large-block path
therefore remains useful even with generous buddy backing.

Nearly all requested storage overlapped in lifetime. The main memory saving
comes from avoiding the former bump allocator's generous reservation, rather
than repeatedly reusing slots. Mean peak live requested storage was 282.7 KiB;
the bump baseline owned a mean 928.7 KiB and a maximum 8 MiB. Ordinary fixtures
averaged 67.7 KiB live requested storage, versus 1,751.7 KiB for stress fixtures.

Historical replay of the actual allocators showed these representative costs:

| Configuration | Mean peak owned memory | Underlying allocation calls |
| --- | ---: | ---: |
| Tracked only | 290.9 KiB | 7,307 |
| 32-bit words, 16 KiB backing, 4 KiB buddy threshold | 307.8 KiB | 450 |
| 32-bit words, 64 KiB backing, 16 KiB buddy threshold | 335.5 KiB | 321 |

Both buddy configurations used 8-byte minimum slots, the native
`alignof(std::max_align_t)` on that target. Calls are totals over one pass of the
47 artifacts; peaks are per-operation means, not concurrent demand. Owned memory
includes backing, management data, chain and tracked indices, and temporary
storage overlap during growth. Native heap hidden overhead is excluded.

Eight- and 16-byte slots had nearly identical owned-memory costs in the smaller
configuration; larger slots eventually increased waste and manager counts.
The fixed index data for 32-bit words was about 4.8 KiB per manager, versus
35.6 KiB for 64-bit words, favoring 32-bit words for small backings. Larger backing
reduced physical allocation calls in some configurations but increased unused
capacity; the 1 MiB configuration with the fewest calls exceeded the bump
baseline's mean owned memory.

The [reflection adapter](../modules/shader_reflection_design.md) uses 32-bit words,
native alignment slots, 64 KiB backing and a 16 KiB rounded buddy threshold to
favor fewer underlying allocations over the smaller alternative. Buddy chaining
and individually tracked large allocations cover growth. The recorded replay
predates the 128-byte backing alignment and integrated setup behavior, so its
costs are historical comparisons rather than exact current costs or evidence of
a reflection throughput improvement.

Observed vendor cleanup left no invalid frees or outstanding allocations before
the independent sweep, both in the baseline capture and subsequent hybrid
validation. Runtime auditing and regression tests remain necessary when updating
the pinned vendor version; these observations cover only the exercised paths.
