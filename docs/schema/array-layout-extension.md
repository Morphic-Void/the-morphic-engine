Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   array-layout-extension.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   22 Oct 2026

# Fixed-array stride and alignment extension

## Agreed scope

This scope was recorded before implementation on 22 October 2026. The extension
implements the agreed scope below. The specification and runtime guide describe
the resulting contracts.

Extend fixed-array descriptors with optional `stride` and `alignment`. Count
remains positive. Omitted stride uses the element's complete size; an explicit
stride must be at least that size and divisible by the element's alignment.
Omitted alignment uses the element's alignment; explicit alignment must be a
power of two, at least the element alignment and no greater than 128 bytes.

Array size is `round_up(count * stride, alignment)`, with checked arithmetic and
the existing storage ceiling. Every element owns a full stride slot, including
the last, and any additional array tail padding belongs to the array. There is
no separately authored array size. Each nested array applies these rules
independently. Empty-element arrays retain size/stride zero and alignment one;
explicit metadata cannot add storage or stronger alignment.

Resolved observations retain their existing meanings: array stride is element
spacing, size is complete rounded extent, and alignment applies to the array's
start. Gap reporting includes element gaps, slot padding and array tail padding.
Arrays repeated in a buffer advance by complete size, not element stride. A
complete array size need not be divisible by its element stride.

## Operations and output

Audit construction, editing, capture, reconstruction, comparison, exact remapping,
attachment/switching, aggregation, persistence and unused-storage clearing.
Preserve existing value and padding policies: binary copies retain padding;
logical comparisons/reconstruction concern element values; explicit clearing
handles all owned padding; unpopulated storage gains no zero-fill guarantee.
Different effective layouts remain incompatible; no conversion or repacking is
introduced. Omitted properties and explicitly stated defaults resolve equally.

Canonical schema output includes effective array stride and alignment using
structural-integer presentation. Source-preserving copying and baking retain
authored omission. Existing documents remain valid; old readers reject the new
properties through their existing strict grammar.

## C++ generation

Continue supporting faithful C++17 generation of the complete catalogue. Emit
element wrappers when stride exceeds element size, and complete-array wrappers
when alignment exceeds element alignment. Emit explicit padding as needed.
Array payloads are named `array`; final non-array element wrapper payloads are
named `value`. Ordinary contiguous arrays keep their existing representation.
Wrappers are data-only, standard-layout and trivially copyable, with deterministic
collision-safe helper names and dependency ordering. Helpers and padding do not
introduce logical schema members or extra document initializer levels.

Generation currently supports visual inspection and manual validation. Preserve
readability and the ability to generate every accepted structure. Selective
generation and aliases to shader types are future work, not part of this change;
generated helper names do not establish a permanent naming API.

## Verification and boundaries

Cover defaults, explicit properties, nested/count-one/empty arrays, propagation
into structures, both forms of padding, invalid layouts, arithmetic boundaries,
round trips, compatibility/merging and storage operations. Compile generated
layout assertions for x86 and x64, including helper-name collisions, size,
alignment, offsets, standard layout and trivial copyability. Run appropriate
repository validation without repeating passing matrices unnecessarily.

Update the specification, runtime guide, validation guide and backlog boundaries.
Matrix semantics, runtime-sized types, wider Boolean semantics, typed references,
representation conversion, shader reflection and Host redesign are excluded.

## Implementation notes

The resolver validates the two optional properties, computes rounded extents
and propagates gap/alignment observations without enlarging resolved records.
Canonical output records effective array metadata. C++ generation collects
required helper types before emission, orders them through existing dependencies
and qualifies references to avoid helper/payload/member name collisions.

The operation audit found that codecs, editing and reconstruction already use
element stride, while bulk storage uses complete record size. Compatibility
already compares size, alignment and stride. Standalone unused-storage clearing
now admits complete array extents without requiring divisibility by element
stride; recursive clearing already handles both inter-element and tail gaps.
No changes to persistence envelopes or remap conversion semantics were needed.

## Validation result, 22 October 2026

The ordinary `MorphicTests -t1` suite passed in Debug and Release on x64 and
Win32, with 90,647 schema checks and zero schema failures in each configuration.
Final test tags were `array-checked-dbg64`, `array-checked-dbg32`,
`array-checked-rel64` and `array-checked-rel32`.

All nine generated-layout assertion sources, including `schema_array_layout`,
compiled successfully with the maintained C++17 `/W4 /WX` helper for both x86
and x64. Policy validation reported zero errors and warnings, with the existing
intentional suppression. Line-ending and diff-whitespace checks passed.
