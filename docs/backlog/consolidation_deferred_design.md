# Deferred consolidation design

Updated 18 September 2026. Reference material for later design work, separate from
the [active design stages](consolidation_design_order.md) and their subsequent
implementation. This document preserves motivations, possible approaches and
unresolved questions. It is not an implementation specification or a dependency
list for the current work. Inclusion does not settle a proposal or schedule it.

The [framework discussion](framework_consolidation_discussion.md) and
[filesystem discussion](filesystem_asset_mapping.md) retain the fuller history.
Where those discussions contain earlier agreements that have since changed, the
current active outline takes precedence.

Latest reassessment, 15 September: retain the existing container reattribution
support. The earlier proposal to restrict transfers to new byte/image wrappers
has been set aside as the working direction; the baked-document and image designs
are being reconsidered. Statements below about keeping rich containers local or
limiting transfer support describe the preceding approach, not a requirement to
remove existing capabilities. Specific cleanup is expected to be considered by hand.

## Current boundary

The narrowed accounting, container/live-document attribution and baked-storage
work is complete in `d1d804c`, `42a908d` and `a75962f`; see the
[current outline](consolidation_design_order.md) and
[final baked-storage record](baked_document_storage_specification.md). The earlier
generic wrapper migration was superseded. The owning image wrapper and filesystem
image remain deferred; an image view may be sufficient for the next concrete
consumer. These completions do not authorise the broader stage list or discard
the eventual asynchronous data-model exercise.

The separately recorded Host contract retains accepted assets until application
exit and requires an already Host-owned asset ID for operations: transfer ownership,
receive the ID, then send a separate operation request. How much of the remaining
work is needed immediately will be reconciled with the narrowed design.

Preserve the filesystem discovery/mapping notes and image ownership/manipulation
ideas as later design resources. Their earlier development does not make them
prerequisites for the narrower baked-document work.

Moving module load/unload to the Host worker remains an explicit follow-up after
the Executive exercise in the active outline. It is not postponed with the broader
authority/lifecycle work below. The deferred asynchronous data-model tests are
also still part of active acceptance, despite having previously been deferred.

## Filesystem image publication and in-flight identifiers

Ritchie's 16 September proposal is recorded in the
[filesystem image discussion](filesystem_asset_mapping.md#immutable-image-publication-and-identifier-lifetime).
The image lets messages identify files without carrying paths. Published images
are immutable; updates atomically replace the current pointer, with entry/exit
accounting and a drain process retaining old images until their users have exited.

The proposal still needs a safe acquisition/retirement protocol, defined ID
meaning across replacements, retention for IDs in flight, and change detection
for bulk queries such as editor path-tree construction. Keeping a traversal's
image alive and determining whether it is still current are separate concerns.
These are deferred design questions, not additions to the current implementation.

## Future document navigation and save-game values

Ritchie's 18 September direction: the document model will represent the filesystem
image. Add filesystem-path-like navigation as side functions that accept a live
or baked document, a starting node and a path, and return the end node. These
helpers sit alongside the document types. Path syntax, traversal rules and the
result for an unresolved path remain to be specified; no native filesystem path
semantics or particular path standard is selected by this note.

Ritchie also identifies a possible extension of baked documents for save games:
allow modification of values while preserving the layout, to hold switch states
and other game state. This is a candidate for later design, not a change to the
current immutable baked-document contract. Which values may change, how updates
preserve validity and how access is coordinated with readers remain open. Fixed
layout alone does not settle those questions, including the treatment of shared
string-table entries.

The possible save-game extension does not change the proposed immutable
filesystem-image publication contract. These notes capture future uses without
resuming implementation or selecting their APIs now.

## Asset lifetime and reference counting

Deferred from former stage 2. Revisit when runtime reclamation or cache eviction
is needed. Simple reference counting without per-client tokens is the proposed
direction; its mechanics remain undecided.

Questions include acquisition/release, initial retained uses after transfer or
load, Host uses during operations, borrowed-view responsibility, final reclamation
and optional cache retention with no outstanding users. Consider client departure,
shared backing, ownership extraction and stale IDs if runtime removal is introduced.
Earlier temporary, module-scoped and durable lifetime categories are possibilities,
not a required taxonomy. Broader SYSTEM identity, publication permissions and
revocation proposals also require reassessment against actual consumers.

The motivating cases include transitory load/save assets and filesystem caching
that survives the loss of client uses. They do not change today's retention until
exit. Any future reclamation design must account for module code, metadata and
allocation-context dependencies as well as outstanding operations and views.

## General Host authority and asynchronous operations

Deferred from former stage 3. Preserve the questions about authority shared among
the main Host, Host worker and execution workers; general operation states,
cancellation, requester disappearance, failed dispatch, shutdown and completion
delivery; and common ownership rules for inputs, intermediates and results.

Request correlation and compact configured results are needed by current concrete
services. A general scheduling, cancellation or message-validity subsystem is not
required merely to implement those services. Reassess any such machinery against
demonstrated operations. Existing module compatibility and dependency checks
remain applicable now; the scheduled module migration is a bounded separate task.

## Developer responsibility, core configuration and shared discovery

Ritchie's further clarification: responsibility for stability belongs with the
developer. The earlier trust discussion had drifted toward an autonomous
self-healing mechanism. Such a mechanism is not the intended foundation of the
deferred design; failure evidence can inform developer diagnosis without implying
automatic content selection or repair.

The core will likely use a distributed configuration specifying which files to
use and how to apply options for safe loading. The configuration representation
and the meaning of individual options remain to be designed. This is a proposed
direction, not a settled manifest format or configuration-distribution mechanism.

Discovery has a larger role for UGC and for our own content production. Both are
expected to use much of the same local discovery and content-handling infrastructure,
with remote storage and distribution differing. Shared infrastructure does not
by itself give external UGC the provenance or permissions of developer content.

A useful consequence to consider later is that discovering available content and
deciding which content a configuration selects are separate responsibilities.
How discovered production content becomes part of a distributed core configuration
remains open. These observations refine the deferred direction without expanding
the immediate filesystem-image specification or scheduling trust/layering work.

## Overrides, layers and patches

These remain expected features, with detailed specification deferred. Preserve
the distinction between complete replacement and partial document layering, the
latter likely restricted to JSON including baked documents. Core and secondary
UGC sources may permit supplying overrides/layers, receiving them or standing
alone. The permissions and representation remain open.

The initial preference is simple replacement selection when an eligible substitute
exists. Competing substitutes, layer order, target identity, patch interaction and
composition failure need later decisions. Patches are expected to use this system.
Selection primarily occurs at application startup; editor intervention and refresh
can have different rules. The current filesystem image does not require this
selection/composition design to be completed first.

Reassess these earlier selection proposals against developer-supplied core
configuration and the shared UGC/content-production discovery direction above.
Discovery alone does not establish permission to replace or layer core content.

## Trust, first-use evidence and recovery

The heuristic trust system is premature, and the later developer-responsibility
clarification above replaces self-healing as the intended framing. The remaining
notes in this section preserve earlier possibilities for reassessment; they do
not establish that a trust engine or journal will be needed.

The earlier discussion distinguished a last
known safe configuration and a harder recovery mode using original shipping assets
and modules. A successful run cannot prove every loaded asset was exercised.

Ideas to revisit include asset-type risk, recording impending first use before
using a file, and staged success evidence. DLL loading, binding and clean unloading
were candidate boundaries, with time in use affecting confidence. Shaders and
configuration were also considered higher risk than ordinary maps and textures.
These are design hypotheses, not a settled scoring algorithm or safety guarantee.

The earlier proposed persistent representation was `trust_state` in JSON and a per-process
`trust_process` CSV, in a dedicated trust directory rather than the logging directory.
At startup, reconcile the previous process record into state, save the updated
state, then remove the consumed record and create the new process record. Durable
update/recovery behaviour and incomplete records need specification before use.
Platform restrictions may require mapping this storage through save facilities.

Source provenance and the initial trusted-core/untrusted-UGC distinction remain
useful to current discovery. Payload validation and module compatibility do not
depend on the deferred trust machinery. Platform research is needed when the
trust storage and platform-service interfaces become concrete.

## Later image capabilities

The initial image wrapper holds rectangular byte storage, TGA decode provenance,
encode configuration and queryable views. Later additions include line drawing,
filled/unfilled rectangles and possibly rectangle copying between images.
Resizing, rotation, cropping and similar transformations will produce new wrappers
with their own buffers, preserving the source image. Manipulation interfaces and
algorithms are deferred.

## Superseded approaches, not scheduled work

Per-client monotonic interest tokens, reverse lookup, recipient-handle translation
and their associated missing-interest message handling were explored and then set
aside. They are historical alternatives, not features waiting for implementation.
Neither notifications nor copied views should be taken as a reason to resurrect
that machinery without a fresh requirement.

General transfer support for rich editable containers, another general type-erasure
layer for byte interpretation and a universal Host asset-capability registry are
also outside the chosen direction. The current design uses limited wrappers with
checked view access and reconstructs richer working structures locally.

Combined ownership transfer/save requests and disposal after operations are
superseded for current work by separate admission and asset-ID-based requests,
with permanent Host ownership until exit. Multiple output operations on an already
owned asset remain a concrete service question in the active design.

## Returning to these topics

Add new deferred observations here, with links to their reasoning. When a topic
becomes necessary, explicitly select its scope and move the relevant requirements
into a current specification, revisiting assumptions against the implemented
system. This document alone does not authorise work or require speculative extension
points in the immediate implementation.
