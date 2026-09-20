# Asynchronous asset-operation consolidation notes

The subsequent implementation and settled contracts are recorded in
[asynchronous asset services](../assets/asynchronous_asset_services.md). Retention
on failure, correlated completion, live conditioning, load-result views and the
caller-managed mutation convention supersede the open questions below. The image
view is complete in commits `5b1282f` and `98ce708`. Module-worker migration remains
separate; the broader deferred features are unchanged.

Captured 18 September 2026; updated 19 September for the coordinator handoff. These
notes deliberately develop the first three of the four consolidation steps being
tracked elsewhere.  They are design direction and a record of unresolved choices,
not an implementation specification or authorisation to start speculative work.

Subsequent image-view discussion on 19 September authorised implementation of
the bounded utility described in [image view](../image/image_view.md). That
document records the settled drawing, copy, access and encoding contracts and
the clarified lower-numeric-Y endpoint ordering. These supersede the tentative
image-view scope and major-axis endpoint ordering recorded below. The broader
asynchronous operation contracts remain design work.

They refine the earlier conclusion that all accepted assets have permanent Host
ownership and can only be acted on through a separate asset-ID request.  Durable
Host ownership remains valid, but it is not the only useful operation form.  The
required design must also support a one-shot save that receives ownership,
performs its requested action and disposes of the transferred resource when the
action completes.

Ritchie's clarification, 18 September: every successful load creates a permanent
Host-owned asset, retained until application exit. Ownership transfers only to
the Host, never from it. Returned IDs and interfaces provide access rather than
ownership; some interfaces may permit controlled mutation of Host-owned content.

## Two transfer and lifetime forms

For client-owned inputs to save operations, select one of these forms explicitly:

1. **Retained Host asset.** The requester transfers ownership to the Host, which
   returns an asset ID.  Later asynchronous operations name that ID.  This supports
   multiple output or transform operations over the same retained asset, which
   remains Host-owned until application exit.
2. **One-shot consume and save.** The requester transfers the input together with
   a specified save operation.  The Host owns the resource only for that request and
   disposes of it after completion.  No durable asset ID is necessary unless a
   chosen failure/recovery contract makes one useful.

Loads always produce permanent Host assets; they do not offer a caller-owned
result or temporary-result ownership mode. Saving an already retained asset by
ID does not make it temporary or dispose of it when the save completes.

The mechanism, rather than a downstream exercise, must prove both forms work
asynchronously.  Explicit rules are still required for admission failure,
operation failure, cancellation, requester disappearance, completion delivery,
and whether inputs or outputs remain accessible in the Host after a failure.
Recovery must not introduce ownership transfer back to the requester. Rejection
before admission leaves ownership with the requester; it is not a reverse transfer.

## Principal transferable representations

The intended surface is deliberately small:

| Representation | Intended role |
| --- | --- |
| `CByteBuffer` | Blind/raw byte buffer. |
| Baked document | Validated, serialized document block. |
| Live document | Parsed/editable document representation, normally baked before asynchronous persistence. |
| Rectangular byte buffer | Pixel storage used for decoded images and TGA encoding. |

The design should keep rich working representations local where possible.  A
document or image view exposes checked interpretation over the underlying storage.
Backing-resource ownership may transfer to the Host; an asset ID identifies a
resource already owned by the Host and does not transfer ownership.

## Initial operation inventory

Applicable saves support either one-shot input transfer or a retained Host asset
ID. Loads create permanent Host-owned results:

1. Save a raw `CByteBuffer`.
2. Load a file into a raw `CByteBuffer`.
3. Load bytes, validate them as a baked document, retain them in the Host, then
   return a baked-document interface and asset ID to the requester.
4. Load bytes, parse them into a live document, bake the document, retain the
   baked block in the Host, then return a baked-document interface and asset ID.
5. Bake a supplied live document and save the resulting baked buffer.
6. Save a supplied baked document in its baked/binary representation.
7. Save a supplied baked document as JSON.
8. Transfer a rectangular byte buffer, encode it as TGA, and save it.
9. Load a TGA file and decode it into rectangular byte storage.

The precise request/result interfaces remain open, but load-result ownership is
settled: raw, baked, JSON-conditioned and decoded-image results remain permanently
Host-owned. Requesters receive asset IDs and appropriate interfaces/views, never
ownership of the loaded backing resource.

## Views and limited mutation

A baked-document view and an image view are likely sufficient; separate mutable
and immutable view *types* would be unnecessary.  Each view has one mutability
flag or mode:

- A read-only view inspects content and metadata.
- A mutable view permits only changes that preserve the backing buffer's major
  representation and layout.

Mutability grants permission to modify supported content, not ownership of the
backing asset. The Host remains its owner throughout such access.

For a baked document, the intended mutable use is game/save state: numeric and
Boolean values can change, while schema, structure, layout, type arrangement and
variable-size regions cannot.  This is an extension of the baked-document model,
not a presently selected public API.

For an image, a mutable view may change supported pixel data, but it cannot resize
the image or alter its fundamental rectangle, storage layout or allocation shape.
The mutation contract must later define synchronisation, in-flight save behaviour,
and whether there is an explicit commit/version boundary.

## Image views and TGA

Existing TGA loading and saving demonstrates useful behaviour but is cumbersome.
The next design should introduce an image view over a rectangular byte buffer.
On decode, the Host may return such a view over the loaded pixel storage.  The
view can expose decode-derived metadata needed to make an explicit later encode
request: dimensions, pixel/channel layout, origin convention, alpha handling,
colour/bit-depth information and compatible encoding expectations.

Ritchie's further clarification, 18 September: the image view is expected to be
a richer class with more mutable state of its own than a typical view. Its own
state is distinct from permission to mutate the backing pixels, and does not
make it an owner of the backing storage.

That class may itself be a reasonable representation of encode/save configuration,
or it may prove too large or carry too much unrelated state for that purpose.
Whether requests use it as configuration or extract a smaller settings object
remains open. This qualifies the earlier categorical exclusion of the view from
encode requests; it does not change the ownership boundary. An encode-and-save
request still identifies its resource through a transferred rectangular byte
buffer (one-shot) or a retained asset ID. Supplying configuration does not transfer
ownership of the storage referenced by a view.

Ritchie is now considering including the small core drawing set in the image
view itself, rather than deferring its implementation:

- Bresenham line drawing without anti-aliasing, with clipping that causes no judder.
- Filled and unfilled rectangles.
- Rectangle copies from other images.

This is a proposed scope expansion for the next design, not authorisation to
implement it yet. It is deliberately limited and is not intended to grow into a
full image-manipulation tool. Other desirable operations remain later candidates.
Operations that fundamentally change an image should produce a new backing
resource rather than mutate its original major form.

Drawing requirements must inform the view's state and size. Determine what
belongs in persistent view state and what can be supplied per operation; no
particular drawing-state fields are settled yet. Include that state when assessing
whether the view itself is appropriate as encode/save configuration.

Ritchie clarifies the required line behaviour:

- Moving both endpoints sideways by one pixel, thereby increasing clipping, must
  look like pixel scrolling the line. The remaining visible raster must translate
  consistently, without clipping-induced judder or a changed stepping pattern.
- Swapping the start and end points must produce exactly the same pixels.
- Mirror symmetry is expected to emerge from the other drawing rules, including
  clipping: a line clipped on the right should have an exact mirrored counterpart
  when reflected in the image. Ritchie clarified this on 19 September as an
  expected consequence, rather than a separate rule imposed on the algorithm.

Translation stability and endpoint-order independence are acceptance requirements.
The design should also verify the expected mirror behaviour. Examine pixel/tie
conventions against these properties together, including lines
passing exactly between candidate pixels; naming Bresenham alone does not settle
those conventions. Record any conflict explicitly rather than silently weakening
one requirement. Use translated, endpoint-reversed and mirrored cases in tests.

Also settle rectangle boundary conventions, copy format compatibility,
source/destination clipping and whether overlapping copies of the same backing
storage are supported. These are bounded operation contracts, not a requirement
for general compositing or format conversion.

### Proof-of-concept line algorithm

Ritchie's description, 19 September, supplies the behavioural/algorithmic reference
in place of extracting the old proof-of-concept source:

- The line includes both endpoints, subject to clipping.
- Horizontal, vertical and diagonal lines had specialised paths for speed.
- Endpoint order is normalised using the deltas. The coordinator's reading is:
  when abs(dx) > abs(dy), X is major and endpoints are swapped if dx < 0;
  otherwise Y is major and endpoints are swapped if dy < 0. Thus the chosen
  major coordinate advances in the positive direction.
- The cumulative value begins at half the major delta. The minor delta is
  subtracted from it during stepping.
- Clipping adjusts that cumulative value to account for the starting position
  within the drawing window. It preserves the stepping state reached from the
  original line rather than starting a fresh line at the clipped endpoint.
- The major-axis choice separates the continuously advancing coordinate from
  the coordinate whose advances depend on the cumulative value.

The exact half-delta rounding, threshold comparison and minor-step/remainder
update order have not yet been specified. Preserve these as details to establish
against the translation and endpoint requirements and expected mirror behaviour
above; do not infer that the sketch alone proves all tie cases. No implementation
is selected here.

## Relationship to the tracked consolidation steps

The facilities here cover and expand the first three of the four tracked
consolidation steps.  The activity previously described as the third task—using
the facilities for binary/JSON/TGA/document workflows—is not an independent
downstream feature.  It is the acceptance proof that ownership transfer, retained
asset IDs, one-shot consumption, views and asynchronous completion behave
correctly end to end.  The resulting workflows are useful capabilities, but their
main architectural role is to exercise the mechanism.

## Decisions to make before implementation

- Safe shutdown of permanent assets and completion/cleanup boundaries for
  temporary save inputs and intermediate resources, including operation-held uses.
- Failure, cancellation and recovery semantics for one-shot ownership transfer.
- Asset-ID, interface and view validity rules, including operation ordering and
  concurrent reads/mutations.
- The exact set of mutable baked value kinds and validation rules.
- The image metadata and encoding-settings schema, including whether preserving
  source TGA characteristics is a compatibility goal or an explicit opt-in.
- Whether the stateful image-view class is suitable as encode/save configuration
  or should supply a smaller settings object, and which state a request captures.
- Concrete completion/result and error contracts for every operation above.
