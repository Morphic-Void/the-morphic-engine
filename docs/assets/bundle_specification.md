# Bundle specification

Implemented format, API and lifetime contract. Updated 13 October 2026.
Completion history and validation are in the
[completed-work record](../project/completed_milestones.md#bundle-assets-and-virtual-bindings).

A bundle is a general-purpose asset containing a baked directory document and
aligned, directly addressable file content. The Host retains it as an ordinary
asset, binds it under one read-only virtual logical root, and can publish an
immutable complete view to clients. Shader sources, includes and configuration
are one use; the format is not shader-specific.

Shader compilation, include resolution and compiler integration are separate
consumer work. Existing shader files serve as opaque test data.

The thread instance requesting a bake or load is the bundle's management owner.
Only that owner can dispose of the original bundle ID. Views may be used by any
thread under the lifetime rules below; acquiring a view does not transfer
management authority. Host retention of storage is distinct from requester ownership.

## Scope and existing foundations

The implementation loads the entire uncompressed bundle into one allocation.
The existing allocation ceiling, `memory::k_byte_size_ceiling` (`0x80000000`,
2 GiB), remains unchanged. Construction scratch is separate and every individual
allocation remains subject to that ceiling.

The design builds on the existing
[baked document format](../data_model/baked_document_format.md),
[logical filesystem image](../../development/README.md),
[asset service](asynchronous_asset_services.md) and
[batch runner lifetime rules](../modules/batch_runners.md).

Bundle reference counting is a limited contract described below. Existing
retained assets do not currently have general client reference counting, and
this design does not introduce it across the asset system.

Memory mapping, streaming, compression, nested bundles, automatic invalidation
and hot replacement are outside this contract. No graphics API, GPU layout
or device-feature baseline is selected by this specification.

## Physical layout

The baked directory starts at byte zero. There is no separate outer header.
The backing allocation is aligned to 128 bytes. Every contained file starts at
an offset divisible by 128, measured from the beginning of the bundle.

```text
Baked directory document
Padding to a 128-byte boundary
File bytes, followed by a mandatory zero byte and alignment padding
File bytes, followed by a mandatory zero byte and alignment padding
...
```

Let `D` be the exact baked directory length and `N` a file's exact content size:

```text
directory storage extent = round_up(D, 128)
file storage extent      = round_up(N + 1, 128)
```

The file storage rule applies to the final file as well. Total bundle size is a
multiple of 128. A bundle must contain at least one non-empty file; empty files
and empty directories are not represented.

| Content bytes | Reserved file storage bytes |
| ---: | ---: |
| 1 | 128 |
| 127 | 128 |
| 128 | 256 |
| 129 | 256 |
| 255 | 256 |
| 256 | 384 |

The byte immediately following each file's content is zero. The builder zeroes
all padding, but validation only requires this immediate trailing byte to be
zero. Remaining padding bytes are not interpreted.

Every file has a distinct offset and a strictly positive content size. Reserved
file extents must not overlap one another or the padded directory region.
A returned file view excludes its
trailing zero and all padding. Bundle alignment does not establish the member
layout or ABI of any contained structure.

Version 1 requires tightly packed reserved extents with no unused aligned gaps.
In ascending file-offset order, the first file begins immediately after the
padded directory, every subsequent file begins immediately after the preceding
file's reserved extent, and the final reserved extent ends exactly at the
declared bundle size. Mandatory trailing zeroes and alignment padding are part
of reserved extents, not gaps. Validation does not require directory entries to be sorted
or file payloads to follow the builder's traversal order; this rule concerns
physical coverage only.

All additions, rounding and range calculations must be checked before overflow
or narrowing can occur. A payload that fits the allocation ceiling by itself
can still be too large once directory, trailing-zero and padding storage are
included.

## Directory schema and encoding

The document root directly represents the bundle directory. It has neither the
main filesystem image's outer `roots` object nor an embedded logical-root name.

| Root property | Document type | Meaning |
| --- | --- | --- |
| `fileType` | String | Exactly `morphic-bundle` |
| `version` | Unsigned integer | Bundle schema and layout version; `1` |
| `size` | Unsigned integer | Exact complete bundle length, including all reserved storage |
| `content` | Object | Immediate entries beneath the bundle root |

Each key inside a `content` object is a file or directory name component.

| Entry property | Document type | Applicability and meaning |
| --- | --- | --- |
| `physicalName` | String | Optional original spelling when it differs from the folded key |
| `content` | Object | Required for directories; absent for files |
| `offset` | Unsigned integer | Required for files; byte offset from the start of the bundle |
| `size` | Unsigned integer | Required for files; exact content length |

Directories have no `offset` or `size`. Every directory, including the bundle
root, has a non-empty `content` object and ultimately contains at least one file.
A file cannot also have `content`. Entries live only beneath `content`,
so files named `size`, `version` or `content` do not collide with metadata.

Use the existing ASCII folding policy for entry keys. Preserve non-ASCII
characters according to the existing document and filesystem name rules;
introduce no Unicode case folding. `physicalName` is a single valid component
whose ASCII-folded spelling equals the key, and is omitted when spelling already
matches. Metadata property names and the file type identifier have their exact
specified spelling; they are not folded.

Stored hierarchy components follow the existing logical filesystem's valid-name
rules. Duplicate names are invalid. The strict relative lookup rules below do
not permit noncanonical stored names.

The directory uses the existing baked-document encoding unchanged, including
little-endian multibyte integers, string encoding and canonical integer metadata.
Numeric schema properties must be unsigned document integers, not signed or
floating-point values. Their values must fit their specified domain and actual
buffer bounds. Offsets and lengths fit 32 bits within the current allocation
ceiling; this is a value constraint, not a new raw integer representation.

The baked header's `total_size` describes the exact directory document length,
excluding its alignment padding. Root `size` describes the complete bundle and
must equal the supplied buffer length. Do not duplicate directory length as a
root property.

The baked-document version describes document encoding; bundle `version`
describes this schema and the fixed alignment rules. Require supported versions
of both, without guessing compatibility or converting unknown versions.
Version 1 rejects unknown schema properties. Future schema extensions require
an explicit versioning decision.

Contained file bytes are preserved exactly. The format does not add shader
types, dependency lists, timestamps, compression metadata, source paths, asset
IDs, binding names, reference counts or writable flags. The ordinary filesystem
image does not gain file-size properties as part of this work.

## Validation and publication

Validate the complete container before publishing a binding or client view:

1. Check allocation alignment, supplied length and the allocation ceiling.
2. Safely establish a bounded directory extent from the baked header, then
   validate the baked document using that exact extent, not the whole bundle.
3. Validate root identity, both format versions, schema property types and the
   exact complete bundle length.
4. Validate hierarchy, canonical names, original spellings, unique entries and
   file-versus-directory structure. Reject empty directories, including an empty
   root, and file entries with zero content size.
5. Validate every aligned file offset, content length and complete reserved
   extent with overflow-safe arithmetic. Reject duplicate offsets, overlaps and
   intrusion into the padded directory region. Require tight coverage from the
   end of the padded directory through the declared end of the bundle, with no
   unused gaps or excess trailing storage.
6. Validate the mandatory zero byte immediately after every file's content,
   including the final file.

Validation establishes safe byte ranges; it does not parse every payload as an
image, document or shader. Consumers perform their normal interpretation and
validation when an entry is used.

Read, allocation, validation or binding-preparation failure publishes neither a
partial asset nor a partial binding. Release temporary storage and any reserved
root name. Report an ordinary asset-operation failure and a useful diagnostic,
including the offending entry where available.

## Construction and persistence

The requester supplies an existing source logical root and subdirectory, plus
the virtual logical root to bind the resulting bundle under. The source subtree
becomes the bundle root, preserving relationships beneath it. Its construction
root name is not embedded in the bundle.

The requester keeps source content unchanged throughout baking. The Host owns
filesystem I/O. Include non-empty inventoried regular files and the directories
needed to contain them. Silently skip zero-length source files and omit empty
directories, including directories left empty after filtering their descendants.
Skip unsupported entries recorded as `kind: "other"`; their presence does not
fail the bake. Do not follow symbolic links or reparse points
or change the existing discovery exclusions. This operates on the selected
inventory rather than introducing a second discovery pass.

Every included regular file must be read successfully; a missing or unreadable
file fails the entire bake. Establishing that a file is empty is distinct from
failing to obtain its size or read its content. Skipping empty files and
unsupported inventory entries does not permit skipping genuine I/O failures.
If filtering leaves no files, produce no bundle or binding and report that the
selected source has no packable content; do not publish an empty root. The
public bake request reports `invalid_request` for this case.
There is no partial successful bundle or filesystem snapshot/change-detection
mechanism.

Construction assembles the directory and reserved file storage, then produces a
validated owning bundle. Success publishes the retained asset and requested
binding together and returns its ordinary monotonic 64-bit `CAssetId` and
borrowed complete view.

Baking executes as one job on the Host I/O worker. The Host validates the
request, reserves the destination root and captures an immutable description of
the selected inventory with resolved source paths. The worker never borrows
nodes from the mutable live filesystem image.

The reusable builder constructs a private live directory, obtains file sizes,
filters empty/unsupported entries and establishes deterministic ordering. It
bakes the directory once, fills its offsets and total size through a mutable
baked view, resizes the owning block to the aligned final extent and reads each
file directly into its assigned region. Resizing can relocate storage, so views
are recreated afterwards. The final bundle is validated before it leaves the
I/O worker. No separate per-file content buffers are retained.

The conditioning worker prepares a private live directory from that validated
view. Loaded bundles instead arrive as aligned bytes and are adopted and fully
validated on the conditioning worker before directory preparation. The Host
then rechecks the reservation and installs only the prepared subtree. It does
not revalidate complete bytes or bake/promote the whole filesystem image for
bundle publication. Failure releases unpublished state and the root reservation.

Use the existing [filesystem file API](../../core/platform/filesystem/file.hpp):
`queryFileSize()` distinguishes an empty source from failure, and `loadFileInto()`
reads a complete non-empty file into an exact caller-owned range without writing
padding. Any query or read failure discards the unpublished construction,
including any partially written content.

The [builder API](../../core/assets/bundle_builder.hpp),
`bundle::build()`, takes a stable `CLiveDocument`, the selected directory's
`content` node and a separate table of `SBuildSource` records. Each record maps
an input file node to its resolved, zero-terminated UTF-8 physical path. Supply
exactly one record per regular file, including empty files; duplicates, missing
records and extra records are invalid input. Paths and inventory remain alive
and unchanged for the synchronous call. `CImage::capture_bake()` supplies a
movable `CBakeInput` owning the copied inventory and stable physical paths.
An empty subdirectory selects the whole root; other selections are strict
relative directory paths with ASCII case folding. Capture accepts only
directory-backed roots, including existing source overrides and redirects;
baking an already bound bundle is outside this implementation. Failed capture
preserves an existing output. Moving the capture preserves path addresses.
The worker must never borrow the mutable main image.

The builder copies the selected hierarchy and original spellings into its own
live document, retaining only bundle metadata. It returns `SBuildResult`, with
an input entry identity when available, and replaces the destination block and
complete view only on success. Failure preserves both outputs. `no_content`
distinguishes filtering to an empty selection; query, read, allocation, size,
baking and validation failures are separately reported. Existing document
translation and validation APIs may include allocation failure in their general
failure outcomes. The core builder neither binds nor saves the result.

The reusable builder is independent of thread identity. Its production execution
belongs on the I/O worker, while tests can call it directly with prepared inputs.
One bake occupies the I/O worker
until the job completes, delaying other work on that worker. Saving the finished
bundle remains a separate request.

Construction is deterministic. Sort each directory's children lexicographically
by their folded name keys without grouping files and directories separately.
Place file payloads in the order encountered by a depth-first traversal of that
hierarchy, and emit metadata properties in a fixed order. Unchanged input,
including names, original spellings and file bytes, must produce the same
layout and bytes with the same format and builder version, independently of
filesystem enumeration order. Sorting is a builder rule; lookup and validation
do not require entries to be sorted. Compare folded keys lexicographically by
unsigned byte values, independent of locale. Emit root properties in the order
`fileType`, `version`, `size`, `content`; directory properties in the order
optional `physicalName`, `content`; and file properties in the order optional
`physicalName`, `offset`, `size`.

Baking does not select a save path. The requester saves the bundle as an asset,
using its ID and an explicit destination logical path through the ordinary save
workflow. Saving the complete bundle to a writable destination is permitted;
writing individual entries through its read-only binding is not.

The requester determines the saved filename and extension. Tests use `.bdl` as
the bundle filename extension; it is a testing convention, not a format or
runtime requirement. Bundle identification uses the stored directory metadata.

## Bundle storage

Use the existing `CBakedDocumentBlock` for both newly constructed and loaded
bundles, following the [baked document storage contract](../data_model/baked_document_format.md).
`adopt_buffer()` retains the complete loaded bytes; ordinary `adopt()` trims to
the document and is unsuitable for bundle adoption. Document views and `bytes()`
cover only the directory prefix; `buffer_bytes()` supplies the complete logical
bytes for I/O. Spare allocation capacity is never serialized.

During private construction, use `resize_buffer()` with 128-byte alignment to
preserve the directory and zero the added extent. Discard and recreate borrowed
views around resizing; never resize while published views or asynchronous
borrowers remain in use. Bundle schema, layout validation and lookup remain
separate: a valid baked prefix alone does not validate a complete bundle.

Published views are read-only; directory structure, file extents and allocation
addresses remain fixed. Runtime asset references and cache associations stay
outside the bundle. An explicitly authorised bounded content edit through an
existing mutable document view requires writable backing and quiescent borrowers;
it must preserve directory structure and file extents. There is no new public
mutable bundle access or dirty-state mechanism. Explicit whole-bundle saving
writes the current complete bytes, including such edits, while independent
document promotion leaves the source bundle unchanged. No change automatically
rewrites the source file, and virtual entry paths remain read-only.

## Loading and virtual binding

The requester supplies a bundle's logical filename and the requested binding
root. Loading, validation and binding publication form one operation. The
retained asset and usable binding are published only after complete success.

Each retained bundle instance has one binding. Root names follow the existing
ASCII folding policy. Reject a root already occupied by any binding or reserved
by a pending bind or disposal. Do not merge, replace or silently alias roots.

The binding is virtual and read-only, with no corresponding native directory.
Ordinary logical asset requests resolve its entries transparently through the
Host. Missing files use the ordinary asset-service failure path. Changing or
removing the source bundle file does not alter the retained snapshot.

The Host associates the binding with the bundle owner. It must resolve either
ordinary filesystem content or a retained bundle range without exposing that
storage distinction to ordinary clients. The mutable main filesystem image is
not borrowed by clients as the complete bundle view.

## Live filesystem integration

The conditioning worker constructs a private directory from the validated
bundle. The Host copies it into a detached subtree of the live image, attaches
it beneath the requested root only when complete, and publishes the retained
asset and binding together. Failure erases the partial subtree and cancels the
reservation. Existing roots and intervening Host updates remain intact; interned
strings and allocation capacity may remain after rollback. Descendants inherit
the root's backing-bundle association.

The live copy retains names, hierarchy, offsets and sizes. Resolve entries
through the existing live hierarchy, then interpret their ranges against the
associated bundle instead of constructing native filenames. These offsets and
sizes belong specifically to bundle-backed entries; ordinary filesystem entries
still have no observational file-size property.

Use the live image's mutable asset associations and private Host cache
bookkeeping for resources loaded through the asset service. Keep process-local
asset IDs out of the baked directory. The live copy's content structure and
ranges remain fixed for the binding's lifetime; changes to Host bookkeeping do
not require synchronisation back into the bundle or a new save.

The complete bundle view always traverses the original immutable baked
directory and returns direct content views. It requires neither the live image
nor sub-file asset IDs. Complete-view references protect this access while the
bundle is open; the requester must ensure they are released before requesting
bundle disposal, as specified below.

This duplicates directory metadata, not file content. The shared baked directory
remains immutable; no mutable asset-ID fields are reserved inside the bundle.

The [filesystem image API](../../core/filesystem/filesystem_image.hpp)
provides the following Host-side mechanism:

- `reserve_bundle()` returns a nonzero, image-local monotonic binding identity,
  distinct from the bundle asset ID. Case-folded root collisions are rejected.
- `CBundleDirectory::prepare()` constructs the private directory without accessing
  the live image. `prepare_bundle()` consumes that directory and associates the
  original asset ID with a valid reservation; failure leaves the directory intact.
  A bundle identity may have only one binding. `publish_bundle()` installs only
  that subtree transactionally into the current image.
- `root_state()` distinguishes ordinary, reserved, prepared, open and closed
  roots. `close_bundle()` blocks new resolution and cache access without freeing
  the root. `remove_bundle()` cancels unpublished state or removes a closed
  root and its cache associations. Closure and removal do not allocate.
- The `SResolvedSource` overload of `resolve()` returns either an owned physical
  path or the binding identity, bundle asset ID, offset and size. These numeric
  ranges do not borrow or protect the bundle; the Host must protect its owner
  before accessing storage. The original physical-path overload rejects virtual
  roots. Writes to virtual roots are rejected by both resolution and completion.
- `loaded()` accepts the binding identity captured at admission (zero for an
  ordinary source). A late completion from a closed, removed or replaced binding
  cannot populate another binding's cache. The retained result's lifetime remains
  the asset service's responsibility.

Preparation, publication and capture preserve their observable outputs on
failure. Binding records use root names and numeric identities rather than live
node pointers, so ordinary refresh can replace the document safely. Whole-image
adoption is rejected while any bundle binding or reservation exists. These APIs
do not own bundle bytes or implement retained-reference accounting;
`CAssetService` coordinates asset publication, storage protection and final removal.

## Cache reuse and concurrent loads

Preserve the existing asset-service cache policy, with one cached representation
associated with each entry. Reuse its ID when the requested format and existing
compatibility checks permit it. A cache hit creates neither another retained
record nor another bundle reference. Existing format, alignment and document
policy checks still apply. This does not introduce a general cache of option
variants; existing TGA decode options do not become separate cache keys.

Asset loads through a bundle root reject requested alignment greater than 128
bytes before cache reuse or conversion. They do not rely on accidental address
alignment or copy a borrowed file into a separately aligned allocation to satisfy
such requests. Independently allocated decoded or parsed results retain their
own output-storage contracts.

Changing the cached association does not dispose of the previously associated
asset. For example, raw and parsed loads may produce separate retained IDs even
though only one is currently cached against the entry. Disposing an asset clears
only associations that still name that ID, without clearing a newer result.

Cache associations belong to the particular binding instance. Removing its
binding clears those associations. Later reuse of the same logical root must
not inherit them, even if independently owned results from the previous bundle
remain alive.

Do not coalesce concurrent uncached loads. A raw entry can normally be admitted
directly on the Host so subsequent requests find its cached ID. Parsing or
decoding may still be outstanding when another request arrives; those requests
may produce separate retained results. Each successful completion can update
the cache association without invalidating earlier results. There is no
first-request-wins guarantee.

Every resulting retained record that borrows bundle storage contributes one
bundle reference, whether or not it remains the cached representation.
Independently owned results do not retain the bundle after conversion completes.
Accepted operations protect their source bundle throughout conversion.

## Refresh of virtual roots

Refreshing an active bundle-backed root succeeds as a no-op through the ordinary
correlated refresh result and existing refresh queue/ordering rules. Do not scan
the filesystem, reread the bundle file, change its copied hierarchy, reset cached
IDs or alter reference counts. Its immutable inventory is already current.

A root closed for disposal is unavailable for refresh; refresh cannot reopen it
or extend its lifetime. Refreshing the ordinary filesystem root containing the
saved bundle file remains a separate operation and does not modify an active
bundle snapshot.

Replacing content requires explicit disposal followed by loading and binding a
replacement. The requester must wait for the final-disposal notification before
assuming the old bundle is gone and its virtual root name is available again.

## Asset identities and storage ownership

The bundle receives an ordinary monotonic 64-bit `CAssetId`; there is no separate
bundle identity scheme. A sub-file receives an asset ID on first exposure
through the Host asset service, not simply because it exists in the directory.
Compatible later loads follow ordinary cache behaviour. Direct lookup through
the complete view needs neither an ID for the entry nor a Host round trip.

A retained resource that borrows bundle bytes records its parent bundle and
holds one reference to it. This includes raw sub-file assets and document views
adopted directly over embedded baked bytes. A parsed or decoded result that owns
all its storage has an independent asset identity and lifetime. Its source
bundle is protected during conversion but need not remain alive afterwards.
Cache association alone does not create ownership.

IDs, parent relationships and counts belong to Host runtime bookkeeping. They
do not modify the baked directory or create a persistence requirement.

## References and disposal

Count retained asset records, not load calls, cache hits or copies of borrowed
views. Reusing an existing cached sub-file ID does not add another reference.
Clients sharing an ID retain the existing responsibility to coordinate safe
disposal of that record.

The requester's retained bundle asset initially holds a reference. A client
needing an independently disposable complete view acquires its own retained
reference while the bundle is open. Copying a complete view does not acquire
one. By convention, the requester ensures complete-view uses are quiescent and
all additional complete-view references are released before requesting bundle
disposal. Acquiring a reference does not grant permission to continue complete
bundle access after closure. The acquisition interface is specified below.

| Event | Required effect |
| --- | --- |
| Retain a new asset borrowing bundle storage | Add one bundle reference |
| Dispose such an asset | Stop further use through that record under the ordinary disposal rules; wait for its accepted operations, then retire the record, clear its cache association and release its reference |
| Dispose an additional complete-view reference | Release that reference after applicable operations and borrower obligations are satisfied; do not independently close the binding |
| Management owner disposes the bundle | Mark the original bundle ID logically dead, close new admissions and complete-bundle operations, and release the initial reference; preserve existing retained sub-file assets |
| Last reference and accepted operation are gone after closure | Destroy bundle storage, remove the virtual root and release its name, then notify the requester of final disposal |

Owner disposal alone is insufficient while retained dependent assets or
applicable operations remain. It does not forcibly invalidate sub-file IDs.
Their later disposals do not individually free the file regions. Before
closure, a disposed sub-file may be loaded again and receive a new ID.

After closure, reject new saves or other operations on the complete bundle,
whether addressed through the original bundle ID or an additional complete-view
reference. Outstanding references keep storage alive but do not make the
original asset ID usable again. Complete-bundle saves accepted before closure
finish normally.

If a complete-view reference unexpectedly remains at closure, diagnose the
convention violation and continue retaining the storage until that reference is
released. Reject further complete-bundle operations but permit disposal of the
reference. Do not deallocate prematurely or treat closure as successful final
destruction merely because the requester has released its initial reference.

Requests accepted before closure remain protected and finish normally, including
publication of their results. No new asset admissions from bundle content are
accepted after closure. The Host must protect the bundle throughout accepted
work even before a dependent result record exists. Root reservation lasts until
final disposal; the source cannot be replaced beneath draining work.

Clients must quiesce borrowed views before disposing the retained record that
protects those views. The count does not reveal how many raw pointers or copied
views exist. Independently owned decoded or parsed assets survive bundle
disposal and are disposed of separately.

Final bundle disposal is reported to the requester as a notification, not as a
correlated response completing the original disposal request. Sending disposal
does not establish that storage has been freed or that the root can be reused.
Notify only after all references and applicable operations have finished, the
backing storage has been released, and the virtual root has been removed and its
name released. The notification identifies the disposed bundle and carries no
borrowed view. It reports completed disposal rather than reserving the freed
root for a particular future caller.

Ordinary asset disposal continues to emit correlated `AssetDisposeResult`
messages. Additional complete-view and sub-file records follow that ordinary
contract; only original bundle disposal uses the final notification.

## Final disposal notification

Use `BundleDisposedNotification { CAssetId asset; }`, declared and registered
in `core/system/transported_types.hpp` and the system type catalogue.
The ID identifies the original bundle asset, not the last sub-file or additional
reference released. The notification carries no status, root string or borrowed
view: it denotes completed destruction and removal of the associated virtual
root. The requester already knows the root associated with that monotonic ID,
which distinguishes this bundle from any later occupant of the root name.

The recipient is the requesting thread instance recorded when the bundle was
published. A disposal request from another thread receives `invalid_request`
and does not close the bundle. Do not redirect the notification to a replacement
thread or to a later occupant of the same thread-package address. Enqueue it
only after backing storage destruction, binding removal and root-name release.
It is an independent notification without the disposal request's asynchronous
correlation slot. Enqueue once per completed disposal, with no acknowledgement
or replay protocol. This does not guarantee consumption by a recipient that
subsequently exits, or reserve the freed root for its next bind request.

The Host detaches the owner/notification route before destroying any requester
package, including Rendering as well as Executive. This does not release other
clients' retained references or make a replacement thread the management owner.

If the intended recipient is still available, failure to post is a terminal
transport error, consistent with existing completion-delivery failures. If it
has exited, retire the notification without delivery; do not retain a dead route
or delay shutdown waiting for consumption.

Invalid, stale and duplicate disposal requests retain the existing immediate
disposal-error response and do not generate a successful final-disposal event.
An accepted bundle disposal produces no correlated success response, only the
eventual notification under the delivery policy above. The Host sends it with
asynchronous slot `-1` after destroying the control record and its storage.

## Sub-file operations after closure

Existing retained sub-file IDs remain usable until their own disposal is
requested. Bundle closure permits reading and inspecting those assets, work
using their existing borrowed views, saving individual sub-files to writable
destinations, and disposal of their IDs. Individual saves may perform temporary
encoding without creating another retained asset or reviving the bundle.

Reject loads through the closed logical root, including potential cache hits,
and new Host requests creating another retained asset from a bundle-backed
sub-file. A sub-file cannot be used to obtain the complete bundle. Existing uses
do not reopen the binding or grant access to further bundle assets.

Each retained sub-file record continues protecting backing storage. Once that
record's own disposal is accepted, apply ordinary admission closure and draining
of accepted operations to its ID. Client-managed batch work retains the existing
convention: finish using borrowed views before disposing the protecting record.
The Host tracks operations submitted through its services, not every copied
view or client-managed job.

Independently owned decoded or parsed resources are outside these restrictions
and retain their ordinary operations and disposal rules.

## Teardown and shutdown

Stop users of borrowed storage before releasing that storage. Bundle reference
counts supplement this rule; they do not establish that executing workers or
client-managed jobs have stopped accessing memory.

During ordinary module unload or replacement, the departing module quiesces its
borrowed views, drains batch work and disposes of references for which it is
responsible. The Host drains accepted operations and performs existing module
dependency cleanup before unloading code. References legitimately held elsewhere
remain valid; unloading one module must not force destruction of their bundle.

Pending final bundle disposal must not block the module cleanup needed to release
remaining references. In particular, do not treat a bundle waiting solely for
reference releases as an ordinary outstanding worker operation when deciding
whether module cleanup can progress. `CAssetService::is_idle()` excludes these
reference-only waits, avoiding teardown waiting on itself.

Normal Host shutdown quiesces module threads and batch borrowers and drains
accepted work while required services remain available. Cleanup retires remaining
asset records, closes/removes any remaining bundle bindings and releases storage
after consumers have stopped. Full service deallocation retires notifications;
it does not wait for ordinary final-disposal delivery from undisposed bundles.

Once all possible consumers have stopped, shutdown does not wait indefinitely
for disposal messages from clients that have already exited. Preserve existing
requirements to destroy module-dependent objects before their code is unloaded.

On terminal worker or transport failure, stop/join workers or otherwise establish
that they cannot access the storage before failing pending operations and
releasing it. Reference counts alone do not justify freeing storage still used
by a worker. Existing precautions for retaining DLLs when safe unloading cannot
be established remain applicable.

Deliver final-disposal notifications while the intended requester remains
available. If it has exited, do not retain a dead message route or wait for
notification consumption during shutdown. Use the final-disposal notification
delivery policy above.

## Acquiring a complete bundle reference

Use the registered POD request
`BundleReferenceRequest { CAssetId source; }`. The source identifies either the
original bundle asset or an existing retained complete-bundle reference, not a
sub-file asset. The Host routes it through `CAssetService::request_reference()`
and returns an `AssetResult` at the caller's asynchronous slot. This does not
introduce a general retain operation for all assets.

The Host resolves the source and verifies that the bundle remains open to new
admissions. It then creates a retained reference record, increments the bundle's
reference count and returns the record's new monotonic `CAssetId` together with
the immutable complete bundle view. Each successful acquisition creates a
distinct ID. All such views address the same directory and content; acquisition
neither copies content nor creates another binding.

Release an acquired reference with the ordinary `AssetDisposeRequest`. Disposal
waits for applicable accepted operations and releases that record and its
bundle reference. It does not close the binding. Disposal of the original
bundle ID closes admissions and releases the initial reference as specified
above.

Once closure is accepted, reject new reference acquisitions, including requests
using an existing complete-bundle reference as their source. Unexpectedly
remaining reference records continue to protect storage and may be disposed of,
but do not authorise further complete-bundle operations. Failed acquisition
returns no ID or view and leaves the reference count unchanged.

Acquisition has an acknowledged completion. A client must receive success before
treating its reference as acquired. Merely posting an acquisition does not
establish ordering across queues. The requester coordinates acquisition and
release so complete-view references are gone before requesting disposal.
Accepted acquisitions retain the ordinary pre-closure operation protection;
an acquisition that completes after closure must be safely released under the
convention-violation handling above.

## Lookup and enumeration

File lookup takes a path relative to the bundle root and returns a status and
an immutable byte view. Match each name component using the existing ASCII
case-insensitive policy; callers need not lowercase their input.

Accept forward-slash-separated file paths such as `shaders/Main.hlsl`. Reject
logical-root prefixes, absolute paths, backslashes, empty components, `.` and
`..`, together with other invalid components under the existing logical-path
rules. The bundle view already establishes the root.

`bundle::ELookupStatus` distinguishes `invalid_path`, `not_found`, `not_file`
when the path resolves to a directory, `success`, and `not_ready` for an
unattached view. Every successful file view has a positive content size.

Expose the immutable baked directory and use its existing document traversal
facilities for enumeration. No separate bundle enumeration system is required.
Lookup walks the baked hierarchy with state local to each
call. An index may be considered if measurements justify one, without changing
the lookup contract.

Consumer-specific path resolution belongs above strict bundle lookup. Shader
include handling is outside this task and is not a requirement to design or
implement a bundle lookup adapter here.

## Interface boundaries

The [bundle API](../../core/assets/bundle.hpp) provides full
validation through `CBundleView::reset(bytes, size)`. Failure clears the view.
`SValidationResult` carries a status and offending baked value index when
available; that index refers to the input directory while its storage remains
alive. Baked-prefix validation failure includes failures to allocate the
document validator's scratch, which its existing interface does not distinguish.

`bundle::adopt()` validates a loaded `CByteBuffer` before transferring it into a
`CBakedDocumentBlock` and returning its view. Failure preserves both owners and
the output view; success retains all bytes without copying the allocation.
The view contains a `CBakedDocument` and a compact complete-buffer descriptor,
fits the existing asset-result view storage, and is trivially copyable. Copying
or looking up through a checked view allocates nothing and retains no storage.

The internal Host lifetime boundary is
[`CAssetService`](../../host/runtime/asset_service.hpp):

- `publish_bundle()` takes completed `BundlePreparation` and a reserved binding.
  It checks the prepared view against its block without repeating validation.
  Success moves the allocation into a stable Host control record and publishes
  the original asset ID and binding together. Failure cancels the reservation
  and preserves the caller's block.
- `acquire_bundle()` accepts an open original or complete-view ID and returns a
  distinct complete-view record. `AssetResult::bundle_view()` exposes its compact
  immutable view without increasing the fixed message capacity.
- `begin_bundle_file()` accepts the live image's resolved range, checks its binding
  identity and bounds, and starts a move-only `CBundleOperation` guard. It does
  not repeat path lookup in the baked directory. `retain_bundle_file()` publishes
  a raw or directly validated baked sub-file record, including completion after closure. The guard
  must remain alive until workers finish borrowing its input. Releasing it does
  not dispose any retained result.
- Asset records own backing leases. Record erasure, dependency cleanup and full
  shutdown release those leases; independent conversion results carry no lease.
  `is_idle()` includes accepted operations but excludes reference-only waits.
- `request_disposal()` checks the management owner before closing the original
  ID and binding immediately.
  `complete_disposals()` removes drained bindings and storage before delivering
  the final notification. `detach_client()` retires the recipient route before
  the Host destroys any outgoing requester package. Full shutdown requires all
  borrowers to have stopped and retires remaining records without notification.

These are Host-internal methods. The public messages are registered in
[`transported_types.hpp`](../../core/system/transported_types.hpp):

| Request | Fields | Result |
| --- | --- | --- |
| Owning `BundleBakeRequest` | `paths.source_root`, `paths.subdirectory`, `paths.root` | Original bundle ID and complete view, bound under `paths.root` |
| Owning `BundleLoadRequest` | `paths.file`, `paths.root` | Original bundle ID and complete view, bound under `paths.root` |
| POD `BundleReferenceRequest` | `source` asset ID | New complete-view ID addressing the same storage |

All three return `AssetResult`. An empty bake subdirectory selects the whole
directory-backed source root. Load filenames are logical paths to physical
files; loading a nested bundle from a virtual root is outside this implementation.
The destination root is a binding name, never a save path. Save complete bundles
with `AssetSaveRequest` and `settings.format = EAssetFileFormat::raw`; the full
buffer is written without conversion. Existing save and disposal messages also
handle retained sub-files.

The Host reserves a root before dispatch, captures owned bake input in the
pending operation, and sends `BundleBuildWork` to its I/O worker. That worker
borrows the capture until returning an owning `BundleBuildResult`. Loading uses
`FileLoadRequest` at 128-byte alignment. Both paths then send `BundlePrepareWork`
borrowing an address-stable, Host-owned `BundlePreparation`; only the conditioning
worker accesses that state until returning `BundlePrepareResult`. Built bundles
reuse their validated view; loaded bytes are adopted and validated there.
The Host validates completion type, phase and worker before publishing. One
configured worker performs both roles; otherwise preparation uses `bg_conditioning`.
Every unsuccessful completion removes its reserved root. Shutdown stops workers
before releasing captures, preparation state or pending reservations.

Malformed admission arguments, empty selections and oversized construction
report `invalid_request`; a root collision reports `busy`. Missing or unreadable
input reports `read_failed`; malformed loaded bundle content reports
`conditioning_failed`. Detectable allocation failures report `allocation_failed`
and failed worker posts report `delivery_failed`. The existing boolean capture
and baked-adoption APIs cannot distinguish allocation failure from other failure
in those steps; their failures report `invalid_request` and `conditioning_failed`,
respectively. No asset status values are added.

The I/O worker logs named build-failure reasons and, when available, the captured
original-cased physical source path. Empty selections report the selected source
directory. The conditioning worker reports loaded-document adoption, validation
and directory-preparation failures. These diagnostics do not change the result
payload or turn a skipped empty/unsupported entry into an error.

Ordinary `AssetLoadRequest` resolves virtual entries before cache reuse, rejects
alignment above 128 bytes, and protects uncached input until completion. Raw and
baked loads publish direct retained views on the Host. Their byte views expose
the exact entry range, excluding separators and alignment padding; baked document
views separately expose the validated prefix. Binary saving preserves the complete
entry even after bundle closure while its retained asset remains alive.
JSON and TGA use the existing conditioning worker and publish independent
results. Cache updates carry the admission-time binding identity.

Runtime ownership also covers published view metadata. Lookup state remains
local to each call so multiple jobs can read concurrently. Variable-length
request names use owning storage. Fixed-size messages carry compact
identities/views rather than lists of entries or arbitrary payload content.

## Tests and references

`Bundle`, `BundleBuilder`, `FilesystemImage`, `BundleService` and `BundleRequests`
in `MorphicTests -t1` cover the format, deterministic construction, bindings,
retained lifetimes and real/controlled worker routing. Failure coverage includes
allocation rollback, failed dispatch and shutdown with preparation pending.
`BundleRequests` also exercises `.bid`/`.bbd` typed attachment and exact byte
preservation through bundle closure and loose saving, plus explicit whole-bundle
saving after a bounded document edit. See the
[schema validation coverage](../schema/validation.md).

The normal Executive exercises bake/reference/save/close/reload. The
[DLL lifecycle harness](../modules/asynchronous_module_lifecycle.md#validation)
covers persistence, refresh/cache rules, batch borrowing, requester ownership,
notification retirement, module transitions and shutdown. The bundle fixtures
check restored allocation attribution and worker execution roles.
Recorded build matrices are in the
[completed-work log](../project/completed_milestones.md#bundle-assets-and-virtual-bindings).
