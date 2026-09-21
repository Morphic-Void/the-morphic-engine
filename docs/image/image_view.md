Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   image_view.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   19 Sep 2026

# Image view

`image::CImageView` in `core/image/image_view.hpp` is a development utility for
experiments and markup of captured textures. It borrows a rectangular buffer;
it does not allocate storage or participate in the rendering system. The owner
must keep that storage alive and unchanged in shape for every use of the view.
Copies of the view remain borrowers. Concurrent access requires coordination
by the caller; this class does not establish asynchronous asset access rules.

## Storage and access

The description is the TGA decoder's `Gray`, `RGBX` or `RGBA`. Gray uses one byte
per texel; the other descriptions use four, with the codec's existing lane
layout (`0xAABBGGRR`). Colour storage requires four-byte alignment, row width and
row pitch. Padding is outside the image and is never filled or copied.

Coordinates, signed rectangle extents, rectangle bounds and reported image
dimensions are `std::int32_t`. Attachment rejects either image dimension above
65,535, matching TGA's dimension limit. Rectangular-buffer pitch and allocation
limits still apply. Rectangle endpoints use checked, saturating 32-bit addition:
an endpoint beyond the signed range remains outside the image, preserving its
clipped result without a wider rectangle representation.

Line setup uses unsigned 32-bit distances because the span from `INT32_MIN` to
`INT32_MAX` exceeds the positive signed range. Its phase calculations alone use
64-bit products to skip extreme off-image spans without overflow. Rasterization
uses 32-bit counts and native-width `std::uintptr_t` byte offsets and deltas.

`texel(x, y)` always returns `std::uint32_t`, zero-extending greyscale. Invalid
reads return zero. `plot` and all other drawing operations assert on read-only
destinations and then reject the operation, including when assertions are
disabled or execution continues. Other invalid drawing requests are ignored.
Greyscale writes take only the low eight
colour bits. The write mask applies to colour images:

```cpp
result = (previous & ~write_mask) | (colour & write_mask);
```

Attachment from a const rectangular view is permanently read-only. A mutable
attachment can be temporarily restricted using `set_read_only`. It returns
`false` if writable access is requested for const or unattached storage, leaving
the state unchanged. Requesting the current state succeeds, including read-only
access on an empty view. The image view exposes only a const backing-buffer view,
so it does not provide a route around
its own read-only flag. Existing external mutable aliases remain the caller's
responsibility. Invalid attachment resets the image view.

A compact access state distinguishes const storage, temporarily read-only
mutable storage and writable storage. Drawing uses a guarded `const_cast` of
the backing-view pointer; no separate writable pointer is stored.

Logical `(0, 0)` is top left. By default it addresses the first texel in the
buffer and increasing Y advances through physical rows. `vertical_flip` reverses
row addressing for bottom-up storage, including reads, writes and copies.

## Drawing and copying

- `fill` fills every texel, respecting the colour write mask.
- `fill_rectangle` and `draw_rectangle` take an anchor and signed width/height.
  Normalise the two corners first, then include left/top and exclude right/bottom.
  Zero extents draw nothing. An outline is clipped without introducing a new
  border along the clipping boundary.
- `draw_line` includes both endpoints and clips to the entire image.
- `copy_rectangle` takes source/destination anchors, shared signed width/height,
  flip/mirror flags and a colour write mask. Both rectangles are normalised, then
  shortened by identical amounts at each corresponding edge. Flip and mirror
  apply to the surviving source rectangle after this shared clipping.

Copies require matching texel sizes; RGBX and RGBA can copy to each other without
conversion or metadata changes. Copy returns `false` for invalid views, a
read-only destination, incompatible formats, invalid flags or overlapping byte
regions. It checks overlap after clipping, across all participating rows, even
when views start at different addresses or use different pitches. Failure makes
no writes. A valid copy with an empty clipped region returns `true`.

### Line stepping

Ritchie's clarified rule, 19 September 2026, supersedes the earlier assumption
that the major coordinate must always increase:

1. Begin at the endpoint with the lower **numeric Y**, regardless of the view's
   physical row direction. Horizontal lines use their own span path.
2. Choose X-major when `abs(dx) > abs(dy)`, otherwise Y-major. Use absolute deltas
   in the stepping arithmetic and signed coordinate advances. An X-major line
   may progress toward decreasing X.
3. Initialise error to `major >> 1`. Emit the current pixel, then subtract the
   minor delta. Advance the minor coordinate only when that subtraction would
   underflow, adding the major delta to restore the remainder. Equality is not
   underflow.
4. Group the major-axis steps into runs using a precomputed whole quotient and
   a fractional remainder; no division is needed inside the run loop.
5. Clip the original sequence, preserving both its phase and any partial first
   run. Offscreen spans are skipped analytically, not traversed pixel by pixel.

Line drawing writes directly to the buffer rather than calling rectangle fills
or the coordinate-based texel writer. It calculates the first address once and
advances it using precomputed major/minor byte deltas. Greyscale, full-colour and
masked-colour variants are selected once per line, so the pixel loop has no
format/mask branching or repeated coordinate-to-buffer calculations. Horizontal,
vertical and exact diagonal lines need only a single constant-stride run.

Horizontal, vertical and exact diagonal lines have specialised paths. Tests
compare the run implementation with an unclipped scalar reference, and verify
endpoint reversal, translation through clipping and left/right reflection,
including midpoint cases. For example, `(0,0)` to `(2,1)` emits `(0,0), (1,0),
(2,1)`; its counterpart ending at `(-2,1)` emits `(0,0), (-1,0), (-2,1)` before
clipping. This is the intended reflection comparison; rotation is not a mirror.

## TGA configuration

The view retains the decode description and encoding preferences. Drawing does
not inspect or maintain alpha metadata. RGBX defaults to RGB encoding, RGBA to
RGBA, and Gray to greyscale. Explicit RGB/RGBA source selection changes that
interpretation without changing pixels. `AutoTrue32` and single-channel R/G/B/A
encoding remain explicit choices for colour storage. CLUT and RLE eligibility
can be configured independently and default to allowed, as in the existing codec.

`encode_options()` returns the existing small `codec::tga::EncodeOptions` value.
That is the configuration carrier; the borrowed storage is supplied separately:

```cpp
auto bytes = image::codec::tga::encode(view.buffer_view(), view.encode_options());
```

The current TGA codec defaults to bottom-up buffers. Its `vflip` therefore has
the opposite sense to the image view's `vertical_flip` when encoding. To attach
a freshly decoded image:

```cpp
image::codec::tga::decoded_image_desc desc;
auto pixels = image::codec::tga::decode(file_bytes, desc, true); // top-down
image::CImageView view{ pixels.view(), desc };
```

Alternatively, decode with the existing default `vflip=false` and attach using
`vertical_flip=true`. This preserves codec behaviour while giving the view a
consistent top-left coordinate convention. Original file compression, palette
choice and ancillary TGA metadata are not exposed by the decoder and are not
invented or retained by this view.

## Validation

The Core `ImageView` suite covers line octants and degenerate lines, exact midpoint
reflection, endpoint reversal, translated clipping, extreme signed endpoints,
rectangle boundaries, copy clipping/transforms, alias rejection, read-only access,
write masks, row padding and TGA orientation/alpha round trips. Boundary cases
include accepted 65,535 and rejected 65,536 dimensions, masked/unmasked lines,
and extreme signed rectangle/copy extents against an independent wider reference.

The view measures 56 bytes on x64 and 36 bytes on x86; `EncodeOptions` is 4 bytes
on both. The smaller encoding configuration avoids carrying borrowed pointers
and drawing-access state into encoding requests. Host admission and returned
views follow the [asset-service contract](../assets/asynchronous_asset_services.md).
Recorded validation is in [completed milestones](../project/completed_milestones.md).
