# Framework consolidation discussion

Updated 15 September 2026. Discussion record for the framework consolidation
described in [the consolidation plan](consolidation_pass.md).

This file preserves discussion history, including superseded agreements. Use the
[active stages](consolidation_design_order.md) for current scope and the separate
[deferred design resource](consolidation_deferred_design.md) for later candidates
and historical alternatives. Neither history nor deferred notes expand the
immediate implementation requirements.

Latest reassessment, 15 September: Ritchie considers his earlier estimate of
reattribution/friendship problems too broad after reading the existing code.
Preserve container reattribution and its flexibility; the proposed narrowing could
remove useful existing capability. Specific cleanup may be better addressed by
hand. The baked-document and image-wrapper design questions remain, but Ritchie
is considering a different approach. Earlier wrapper-led restructuring below is
discussion history, not the current design premise.

Latest ownership decision, 15 September: stage 1 specifies permanent Host ownership
until application exit. The Host does not dispose of, recycle, erase or deallocate
accepted assets during the run. Host asset operations require an already owned
asset ID: a client transfers ownership, receives the ID, then sends a separate
operation request naming it. Rejected admission leaves ownership with the client;
subsequent operation failure does not undo accepted ownership. Loads produce
Host-owned assets for later operations under this same rule.

This supersedes the runtime disposal, per-use retention and combined transfer/save
proposals retained below as discussion history. The Executive exercise checks
retention through the run and deallocation at application exit, rather than
requesting disposal before success. Module unloading must respect the dependencies
of assets retained until exit. Broader lifetime and operation designs stay deferred.

Latest scope update, 15 September: defer the broader Host reference-counting and
authority/asynchronous lifecycle design stages. Define the concrete load/save and
Executive exercise contracts without making those general designs prerequisites.
Moving module load/unload to the Host worker thread is a separate follow-up after
the Executive exercise. See the renumbered [design stages](consolidation_design_order.md).

Current direction, 15 September: Ritchie considers the interest-token design
over-engineered for the intended game and wishes to reassess it. The earlier
interest rules below remain a discussion record, not a requirement to implement
per-client tokens, reverse lookup or message translation. No simpler replacement
has yet been selected. Preserve necessary ownership and lifetime outcomes while
reconsidering the machinery used to achieve them. The trust system is also
considered premature and is set aside from the current implementation direction.

Further direction on 15 September: consider simple reference counting without
interest tokens. Exact counting rules remain open. Simplify memory-context
transfer around the existing mechanisms and concrete needs rather than extending
it into a general ownership framework. For saving, the Host should need only
byte buffers (including baked document blocks) and rectangular byte buffers
with metadata, potentially restricted to images. The prior broad container
coverage and generic asset-capability proposals must be reconsidered against
that smaller surface. This is a design revision, not implementation authorisation.

Assistant-raised implications for that revision: preserve a stable asset ID for
identification while counting explicit retained uses, including asynchronous Host
work; borrowed view copies need not acquire references when covered by an owner's
lifetime. Retain/release balancing and failed-dispatch cleanup still require a
contract. Distinguish the small resource payload set from any owning request
metadata, such as filenames, that must also survive transport. Define JSON/image
conversion over the supported representations without assuming a universal
capability registry is required. These details await discussion.

Ritchie further questions whether existing string-buffer reattribution is needed
now that rich structured text can cross threads as an owned baked byte block.
Treat string containers as candidates for remaining local to construction and
processing, rather than automatically preserving their transfer machinery.

Inspection on 15 September found that the production TGA load/save requests own
`CSimpleString` filenames and register that nested storage for erased-owner
transfer. `CStringBuffer` and `CStableStrings` have public reattribution methods
and SYSTEM identities, but are not standalone registered erased-owner payloads
in the built-in payload list. Their reattribution code includes composition-specific
context-replacement hooks. Review actual consumers and simplify request storage
before removing any support; a small owned filename does not by itself justify
transfer support for a complete stable string table. Exact removals and the new
request representation remain undecided.

Ritchie clarifies the intended distinction: containers offering rich manipulation
of their content need not themselves transfer between threads. Rich working
representations can be reconstructed from a baked document when required.
The transferable representation can remain byte storage with a description of
its interpretation. Reducing `CBakedDocumentBlock` to that form remains a proposal;
the exact metadata and checked-view binding contract are not settled.

Filename transfer is the remaining concrete gap identified in this discussion.
It can use simple owned byte-buffer backing initially. Once the filesystem image
exists, its representation may instead be embedded directly in thread messages.
Whether that means a source identifier, another value representation or inline
name data is open; do not assume a borrowed pointer into a catalogue has the
required lifetime. Encoding, logical length and any physical terminator needed
at the platform boundary are small contracts still to define.

This supersedes any suggestion that rich string-container transfer must remain
merely to support current request filenames. Local manipulation and reconstruction
remain separate from the limited owned-storage transfer surface.

Ritchie now narrows the intended reattributable payloads to byte buffers with a
content descriptor and images. Consider making `CByteBuffer` and `CByteRectBuffer`
simpler with respect to reattribution, each wrapped by a reattributable owner
granted friendship. The wrapper form and exact access boundary remain proposals.

The assistant suggests that buffers continue owning and accounting for their
allocations while explicit wrapper operations control attribution changes. The
memory token/context primitive remains the accounting implementation; moving the
public transfer boundary need not duplicate it in each wrapper. Keep friendship
limited to the small set of ownership wrappers rather than adding consumer- or
format-specific friends. String and other rich containers then use ordinary
buffer ownership without gaining cross-context transfer through that composition.
The erased carrier's separately allocated payload shell is retained, as clarified
by Ritchie. It contains the ownership wrapper, which in turn contains the buffer.
Its accounting must remain coherent with the wrapped allocation through transfer
and rejection.
Content descriptors and image metadata require defined lifetime and validation
rules, but no general container-registration framework is implied.

Ritchie requires move extraction of the underlying buffer from its wrapper.
The wrapper must consequently support an empty state. Extraction moves storage
ownership to the destination; it does not destroy the wrapper's payload shell.
An empty wrapper inside an allocated erased payload is therefore distinct from
an empty erased carrier. Wrapper source observations and metadata must remain
coherent after extraction; the exact reset/query behaviour is still to be defined.

The assistant proposes preserving ordinary move semantics: extraction alone does
not reattribute an allocation. Explicit transfer remains responsible for changing
contexts. Shell accounting remains until the erased owner releases it, while the
extracted buffer's allocation continues to be accounted for by its new owner.
An emptied wrapper must not continue publishing an image/document view as though
it still owned the backing storage. Previously borrowed views remain subject to
the actual backing owner's lifetime; extraction cannot automatically clear copies.
These detailed move/view rules await specification.

Ritchie identifies three current uses for the reattributable byte-buffer wrapper:

- Baked document storage, requiring 32-byte alignment and a checked document view.
- Filename storage with a corresponding string view.
- An uninterpreted data blob.

These concrete cases form the core of the wrapper design. Ritchie clarifies that
the intended capability is obtaining a view after checking that the recorded
content type and requested view are coherent, rather than another general layer
of type erasure. No descriptor layout or exact accessor API has been chosen.

The assistant suggests considering one byte-storage ownership/reattribution path
with a small content-kind descriptor and only the metadata needed by these views.
The existing erased carrier identifies the wrapper type; the descriptor identifies
how its bytes may be interpreted. It need not introduce another allocation,
destructor table or general registration mechanism. Establish validation and view
construction per case before deciding whether any runtime operation table is
needed. A content-kind claim alone does not validate a baked document.

The filename case needs agreed encoding, logical length and terminator rules.
The blob exposes bounded bytes without semantic interpretation. Baked validation
uses actual content length and required base alignment. Views remain borrowed
from the wrapper's storage; descriptor state after move extraction must be
specified alongside the required empty-wrapper state. These are design questions,
not an instruction to add a new type-erasure subsystem.

This records Ritchie's initial discussion points and the ownership boundary
explicitly clarified during that discussion. It is not an implementation
specification. Proposed mechanisms, assistant suggestions and open questions
remain distinct from agreed contracts. Further concerns may be added as the
design develops; this initial collection does not close the scope.

## Agreed client–Host ownership boundary

- Files loaded through the Host are owned by the Host.
- Saving requires a resource already owned by the Host, identified through
  client access, or a transfer of ownership to the Host.
- The Host supplies borrowed views to clients. It receives ownership, not
  client-supplied views as backing storage for requested operations.
- Entry of an ownership transfer may be rejected. Rejection leaves the asset
  owned by the client; accepted entry transfers ownership to the Host.
- Admission and the outcome of requested work are distinct. A later operation
  failure does not itself reverse an accepted ownership transfer.
- A view supplied to a client must have its backing lifetime secured when
  supplied. Publication and retention must not leave an access gap.

These are client–Host service contracts. The mechanics of internal worker
borrowing, transport and scheduling remain to be designed.

## Clarified meaning of a user and an interest

A user is a thread holding an interest in the asset through a Host-issued
handle. Supplying handle-based access to an additional client requires remapping
to that client's handle. It is distinct from lending a view.

A holder can supply views to worker threads for which it has responsibility
without giving each worker an independent interest. Receipt or copying of a
view does not automatically establish an interest. The holder is responsible
for maintaining the backing lifetime through those workers' use of the views.

This distinguishes a thread with an independently retained interest from a
thread merely using borrowed data under another holder's responsibility.
Precise release, worker-completion and shutdown mechanics remain to be defined.

## Agreed interest acquisition boundaries

Loading, ownership transfer and interest sharing are the mechanisms by which
an interest is acquired. Transfer of interest is sharing with the recipient
followed by disposal of the sender's interest. The recipient's interest must
therefore be established before the sender relinquishes its own.

A notification carrying an interest handle does not acquire an interest. It
can only be delivered to a client for which that interest has already been
established. Repeated notifications do not create additional interests or
release obligations. Handle receipt as information is distinct from acquisition.
Host internal interests use the same interest mechanism; operation work does
not acquire retention merely by receiving a notification containing a handle.

An ordinary message containing an interest handle for identification must
translate that handle to the recipient's existing handle for the same backing
asset. This requires reverse lookup by asset and recipient as well as forward
lookup from the sender's handle. Lookup is therefore required outside acquisition
boundaries too. Translation identifies an existing interest and must not
implicitly share or acquire one.

If the recipient has no corresponding interest, whether it never held one or
has retired it, translation failure must be logged. The original message must
not be delivered as though translation succeeded. It is discarded or, if the
recipient still exists, an appropriate error message is sent to that recipient.
Neither outcome creates or restores an interest. The choice between discard and
error notification, and handling of error-notification delivery failure, remain
to be specified. The error must not depend on successful translation of the
missing interest handle.

One proposed notification form replaces the original message with an
explicit failure message. The recipient must be able to identify the original
message type but must not receive its payload. Forwarding the original message
with interest handle zero was considered; an accompanying view could still
expose storage whose lifetime is no longer protected, so payload replacement
was favoured before the validity-flag alternative below. Zero can remain a no-interest indicator without
making original-payload delivery safe.

An assistant-proposed minimal failure envelope carries the original message
type, a reason identifying unavailable recipient interest, and request correlation
where supplied. Correlation must be recoverable independently of the discarded
payload. The exact fields and common message-header requirements remain open.
Original owned payloads still require proper cleanup; payload disposal must
respect their ownership and module-lifetime dependencies.

An alternative proposed by Ritchie gives every message a validity flag. Failed
interest translation marks the original message unactionable while preserving
its type and normal delivery path. The process waiting for that message can
handle the failed outcome directly. This would avoid requiring the Host to
understand the payload or clear individual embedded views. Neither this option
nor replacement with a failure message is selected yet.

For evaluation, the assistant suggests that validity be checked before any
normal payload use, including dereferencing views, and that type and correlation
remain safely accessible outside the payload. Invalid delivery would complete
the waiting operation as unsuccessful without creating or restoring an interest.
A common receiving interface could enforce this distinction. Retaining an
unactionable message still requires safe transport and destruction of its carrier
and any owned content; a validity flag does not remove module dependencies.

This gives the one-handle-per-client-per-asset direction a concrete use in
ordinary message identification. A per-asset collection searched by thread is
a candidate implementation; no reverse-index representation has been selected.
Repeated acquisition/release behaviour still needs definition.

Interest identity, underlying asset identity and request/response correlation
are separate concerns. The Host can resolve two handles to compare their backing
assets without encoding a relationship in the tokens. Recipient-handle lookup
is required for message translation despite that distinction. A caller-supplied request
identifier echoed in responses is a proposed correlation mechanism; its scope
and representation remain open.

## Agreed disposal meaning

A client's disposal request relinquishes that client's interest in the backing
asset and retires its handle. It does not retire other clients' handles.

The backing asset is actually disposed of only once all interests have been
retired and it is not marked for persistence without outstanding interests.
Persistence here means Host retention; it does not mean saving a file to disk.
Retiring a client handle does not imply that backing storage has been reclaimed.

Host internal interests use the same mechanism as client interests: the Host
creates its own interest handle. Previously accepted operations retain the
backing storage they need through Host interest handles, so retiring the client's
handle does not remove that protection. Host handles are retired when their
interests end and participate in the same all-interests-retired disposal rule.
The mapping of concurrent operations to Host handle holders remains to be
specified within the proposed one-handle-per-user model.

Client-distributed borrowed views remain the supplying holder's responsibility
and must cease use before it relinquishes its interest.

## Clarified loading behaviour

A JSON text load through the document service includes parsing and baking before
the requester receives a successful handle and view. Publication remains subject
to the established processing and acceptance-policy contract.

A TGA load request must specify whether the Host should retain the original
undecoded file bytes in addition to the decoded image. The representation of
that choice and access to retained source bytes remain to be designed. Retained
source bytes and decoded content have distinct lifetime concerns.

## Ritchie's discussion points

### Asset identities and client relationships

Consider retaining `CAssetId` as the Host-only direct storage identity and
issuing clients separate monotonic handles that resolve to it. Clients include
threads in the same or other modules; the clarified unit of interest is the
thread described above.

The proposed relationship is one handle per user per resource. When access is
passed to another user, the Host returns that recipient's existing handle or
creates one if absent. The recipient should receive its own handle rather than
use the sender's handle unchanged. Disposal retires the client's handle as
clarified above; repeated acquisition semantics remain to be specified.

The usual flow would transfer ownership, receive a handle and request operations
through it. A transfer may also carry initial instructions, such as saving to
binary, JSON or both. No message layout or sequencing is settled.

### Retention beyond handles

Support transitory Host ownership: receive an asset, apply requested operations
and dispose of it afterwards. Also consider lifetime instructions permitting
retention after all client handles disappear and after an originating module
unloads. Filesystem caching is an intended use, avoiding unnecessary reloads.

The flag representation, retention categories, eviction policy and source
freshness rules remain open. Survival of a module must be reconciled with the
existing consolidation requirement that surviving resources have no dependency
on its code, type metadata, destructors or allocation context.

Existing transfers already carry flags for references to module code and data.
Apply this flagging appropriately as part of consolidation. The current
`CErasedOwner` mechanism is a mounting-point hazard mask; ownership moves retain
it. Review how dependencies are marked, preserved and consulted by lifecycle
decisions, keeping hazards distinct from interests and retention policy.

### Transitory ownership and save sequencing

The primary transitory-ownership cases are file loading and saving. When an
ownership transfer also requests saving, the client could specify disposal or
retention after the operation. Saving one baked document as both JSON text and
binary raises the question of whether saves should be bundled.

An alternative minimum service would accept ownership as one operation, receive
save instructions separately and receive an explicit disposal request afterwards.
In that proposal, the Host must keep the asset alive until instructions received
before disposal complete. Neither bundled operations nor this separate-instruction
interface is selected yet. Disposal now has the client-interest meaning defined
above; precise ordering and completion acknowledgement remain to be specified.

### Common reattribution and type registration

The current friendship relationships in memory buffers and their owning
wrappers raise concern about proliferation. Friendship for the erased carrier
is readily justified; additional composition-specific friendships suggest a
gap in the common reattribution mechanism.

Extend reattribution coverage across eligible containers through a common
mechanism associated with SYSTEM type identity. Inventory the potential
containers and their ownership constraints; do not assume the current set of
supported payloads is complete. The mechanism, eligibility rules and treatment
of composed storage remain to be specified.

### Host knowledge of supported operations

The Host needs minimal knowledge of registered assets sufficient to determine
which of its limited operations apply. Examples include binary saving, image
compatibility with conversion to TGA, and document saving as JSON. How type
identity exposes this knowledge is open.

### Operation configuration and compact responses

Requests will carry operation configuration. Clients generally need success or
failure, possibly a basic reason, a handle, a view or a small content report.
For JSON loading, a client may want findings bits but ordinarily does not need
the full parser/linter reports. Failures are likely to be logged for human
review; logging responsibility and diagnostic selection are not yet agreed.
The low-level parser's existing no-logging contract remains unchanged.

### Image resource and view

Clarified by Ritchie on 15 September: initially the image wrapper has no
significant image-manipulation capabilities. It holds the rect-buffer-backed
image, provenance information from TGA decoding and configuration for TGA
encoding, exposed through a useful queryable representation. The exact metadata
fields remain to be specified; this does not promise retention of every original
TGA file field or byte-for-byte reproduction of its encoded representation.

Later additions include line drawing, filled/unfilled rectangle drawing and
possibly rectangle copying between images. Resizing, rotation, cropping and
similar transformations must produce new image wrappers with their own rect
buffers; they never modify the source image referenced by the original wrapper.
This rule is specific to those transformations. Drawing and rectangle-copy
destination mutation/access rules remain open, so the image wrapper is not
declared universally immutable.

The initial ownership/metadata contract belongs in the foundational design.
Manipulation algorithms are later extensions, not prerequisites to TGA transfer
and load/save integration. Producing an independent transformed result must not
be confused with merely copying a borrowed image view.

## Questions and suggestions raised during discussion

The following are assistant-raised implications to evaluate, not approved
interfaces or policies:

- With the user clarified as a thread, define handle sharing versus
  relinquishing the sender's interest, and how thread/module shutdown resolves
  interests and outstanding borrowed use.
- Decide how multiple independent uses within one user relate to its single
  handle, including concurrent Host operations using Host interest handles.
- Consider operation, user, explicit Host and cache retention as potentially
  overlapping reasons rather than assuming one total handle count decides
  disposal. Host operation retention is now explicitly through interest handles;
  the remaining policy descriptions are not settled lifetime flags.
- A transitory submission may need only operation correlation and completion,
  without returning an asset handle. Define cleanup acknowledgement.
- Define partial outcomes for multiple requested outputs and retention or
  disposal after failure or cancellation.
- Explore a composable owned-storage contract building on existing erased-owner
  destruction, accounting and reattribution operations. SYSTEM identity alone
  does not establish complete transfer support.
- Separate type support for an operation from suitability of a particular
  instance and its options. Binary saving requires a defined representation;
  raw owning-object memory is not inherently a serialisation format.
- Account for the module lifetime of any implementation used by registered
  operations. Decide where capabilities and their implementations reside.
- Distinguish filesystem source identity from loaded content versions and
  cache freshness.
- Preserve useful distinctions between processing failure and policy rejection
  in compact responses, without requiring full reports in each completion.

## Acceptance and next design work

The discussion now also develops [filesystem asset mapping](filesystem_asset_mapping.md):
configured scan roots, storage purposes, editor discovery, rescan updates that
preserve cached-asset references, and possible live/baked catalogue documents.
That note separates Ritchie's direction from proposed representations and open
platform/service integration choices.

The deferred data-model asynchronous tests are part of this consolidation's
acceptance exercise: binary save/load, JSON load and conditioning, and JSON
save. They must demonstrate coherent ownership, retained access, failure
handling and cleanup as well as correct content. Direct persistence checks
precede full asynchronous integration. The consolidation plan retains the
detailed coverage, including all-node fixtures, deterministic baking, normalised
JSON semantics, stale access and allocation cleanup.

The [design and implementation order](consolidation_design_order.md) provides a
provisional sequence for filling in the detail, with decision boundaries and
research checkpoints. Develop contracts through discussion and concrete scenarios
before choosing interfaces. Capture out-of-order thoughts for the appropriate
stage and revisit earlier decisions if their dependencies change.

Substantial implementation requires explicit instruction following agreement on
the specification. This record does not authorise implementation or commit.
