# Deferred resource and lifecycle design

Updated 23 September 2026. These future ideas and open questions are preserved
from the consolidation discussions. They are not implementation requirements,
a priority list or authority to begin work. User direction and assistant-proposed
alternatives are distinguished where they differ.

See [current scope](current_scope_backlog.md) to select subsequent work,
[filesystem limitations](filesystem_asset_mapping.md) for possible later iterations
and [job framework design](job_framework_design.md) for future scheduled execution.
Historical proposals must be reassessed against the implemented subsystem contracts.

## Implemented boundary

Memory accounting and attribution, baked storage, image drawing/copying, concrete
asset operations, explicit disposal and asynchronous DLL lifecycle are complete.
The rendering stub and Executive-controlled startup are committed as `f91ded3`.
The directory-backed [filesystem image](../../development/README.md), logical
resolution, DLL redirect, queued root refresh and cache associations are now
complete and accepted, including the first-pass review corrections.
[Completed milestones](../project/completed_milestones.md) records the checkpoints.

The [asset service](../assets/asynchronous_asset_services.md) supports retained
transfers, one-shot transfer/conditioning/save and saves by ID. Retained assets
live until explicit disposal, dependent-module cleanup or shutdown. The
[module service](../modules/asynchronous_module_lifecycle.md) drains operations
and joins affected threads before dependency cleanup and I/O-worker unbinding.
These bounded services do not implement reference counting, automatic cache
eviction, cancellation or a general scheduler. The former deferred filesystem
design is superseded by the implementation, not awaiting completion. Its useful
remaining considerations are in [filesystem limitations](filesystem_asset_mapping.md).

## Future document navigation and save-game values

The filesystem image now uses the document model and has its own logical-file
lookup. General path-navigation helpers for arbitrary live or baked documents
remain a separate possible extension: accept a starting node and a path and
return the end node. Select syntax, traversal and failure semantics when an
actual document consumer needs that API; the filesystem resolver does not
establish a universal document-path standard.

Ritchie also identifies a possible extension of baked documents for save games:
allow numeric and Boolean values to change while preserving structure, types,
layout and variable-size regions, to hold switch states and other game state.
This is a candidate for later design, not a change to the current immutable
baked-document contract. Exact permitted updates, a possible read-only/mutable
view flag and coordination with readers remain open. Fixed
layout alone does not settle those questions, including the treatment of shared
string-table entries.

The possible save-game extension is independent of the Host-owned filesystem
image and does not alter the current baked-document contract. These notes do
not select either extension for implementation.

## Asset lifetime and reference counting

Automatic reclamation and cache eviction remain deferred.
Explicit disposal is now implemented, with Host operations drained before
destruction and other borrowers quiesced by convention. Simple reference counting
without per-client tokens remains a possible later direction; its mechanics are
undecided and it is not required by the current disposal path.

Questions include acquisition/release, initial retained uses after transfer or
load, Host uses during operations, borrowed-view responsibility, final reclamation
and optional cache retention with no outstanding users. Consider client departure,
shared backing and ownership extraction in any broader lifetime design. Current
disposal invalidates the ID and borrowed views without reusing the ID.
Earlier temporary, module-scoped and durable lifetime categories are possibilities,
not a required taxonomy. Broader SYSTEM identity, publication permissions and
revocation proposals also require reassessment against actual consumers.

The motivating cases include transitory load/save assets and filesystem caching
that survives the loss of client uses. One-shot saves and explicit disposal already
cover bounded cases. Any future reclamation design must account for module code, metadata and
allocation-context dependencies as well as outstanding operations and views.

## General Host authority and asynchronous operations

Preserve the questions about authority shared among
the main Host, Host worker and execution workers; general operation states,
cancellation, requester disappearance, failed dispatch, shutdown and completion
delivery; and common ownership rules for inputs, intermediates and results.

Request correlation and compact configured results are implemented by the concrete
services. A general scheduling, cancellation or message-validity subsystem is not
required merely to implement those services. Reassess any such machinery against
demonstrated operations. Existing module compatibility and dependency checks
remain applicable now; the bounded module service is already implemented.

## Developer responsibility, core configuration and shared discovery

Ritchie's further clarification: responsibility for stability belongs with the
developer. The earlier trust discussion had drifted toward an autonomous
self-healing mechanism. Such a mechanism is not the intended foundation of the
deferred design; failure evidence can inform developer diagnosis without implying
automatic content selection or repair.

The core will likely use a distributed configuration specifying which files to
use and how to apply options for safe loading. The configuration representation
and the meaning of individual options remain to be designed. This is a proposed
direction for final content selection, not a configuration-distribution mechanism.
The implemented development root manifest already defines directory bindings,
permissions and inventory policy; that is a separate, settled first-pass contract.

Discovery has a larger role for UGC and for our own content production. Both are
expected to use much of the same local discovery and content-handling infrastructure,
with remote storage and distribution differing. Shared infrastructure does not
by itself give external UGC the provenance or permissions of developer content.

A useful consequence to consider later is that discovering available content and
deciding which content a configuration selects are separate responsibilities.
How discovered production content becomes part of a distributed core configuration
remains open. These observations do not schedule trust/layering work or reopen
the implemented filesystem-image contract.

## Overrides, layers and patches

These remain expected features, with detailed specification deferred. Preserve
the distinction between complete replacement and partial document layering, the
latter likely restricted to JSON including baked documents. Core and secondary
UGC sources may permit supplying overrides/layers, receiving them or standing
alone. The permissions and representation remain open.

The initial preference is simple replacement selection when an eligible substitute
exists. Competing substitutes, layer order, target identity, patch interaction and
composition failure need later decisions. Patches are expected to use this system.
Application-startup selection and editor-directed changes may have different
rules; neither selection policy is implemented by the filesystem image.
The existing discovery and cache service does not depend on composition.

Reassess these earlier selection proposals against developer-supplied core
configuration and the shared UGC/content-production discovery direction above.
Discovery alone does not establish permission to replace or layer core content.

## Trust, first-use evidence and recovery

The heuristic trust system is premature, and the later developer-responsibility
clarification above replaces self-healing as the intended framing. The remaining
notes in this section preserve earlier possibilities for reassessment; they do
not establish that a trust engine or journal will be needed.

Potentially useful distinctions are source provenance versus payload validity,
and a previously working configuration versus an original shipping baseline.
A successful run cannot prove every loaded asset was exercised, and an interrupted
operation does not prove which file caused a failure. The filesystem image does
not currently store or enforce a source-trust classification.

Any later diagnostic or recovery feature needs a concrete consumer and a defined
persistence/failure contract. No risk-scoring algorithm, trust-file layout,
first-use journal or recovery-selection sequence is prescribed here. Existing
payload validation and module compatibility checks do not depend on such features.

## Later image capabilities

The [image view](../image/image_view.md) already supports pixel access/plotting,
clipped lines, filled/unfilled rectangles, flipped/mirrored rectangle copies,
full fills and channel write masks. TGA provenance and encoding configuration
are part of the existing view.

Resizing, arbitrary rotation, cropping and similar transforms remain possible
extensions. The proposed ownership rule is to produce an independent result
buffer and preserve the source; copying a borrowed view is not such a transform.
Vector-text annotation, transparency and other blending also remain future work.
Select algorithms and synchronisation/version rules against actual consumers.

## Operation and publication extensions

Separate saves already permit multiple outputs from one retained asset. Bundling
binary and JSON outputs into one request remains an option, with partial success,
cancellation, output identity and failure retention still requiring a contract.
Do not reintroduce mandatory separate transfer/save: one-shot saves are implemented.

Optional retention of original encoded TGA bytes alongside decoded pixels was
proposed and remains a future service option; the current image load publishes
decoded storage. A general source/content-version relationship, cache freshness,
transformed-result publication and storage extraction from Host ownership would
need their own design. If extraction is ever added, ordinary
moves should preserve attribution, emptied owners must stop publishing views,
and already copied views remain subject to the backing owner's lifetime.

Retaining policy-rejected parse results for diagnosis is a possible future Host
policy. It must not turn rejected content into a successfully published asset.
A universal asset-capability registry was considered but is not required by the
current concrete payloads. Any later registry should separate type capability
from instance/options suitability and account for the implementing DLL's lifetime.

## Module accounting periods and diagnostic evidence

Ritchie's earlier direction was to report a quiescent imbalance and begin a fresh
zeroed accounting period on reload, so an old discrepancy is not carried forward
unless its cause recurs. Module replacement is now implemented, but that separate
report/reset policy is not: existing binding installation/unload accounting checks
remain enforced, and Host-owned module records can survive binding transitions.

A future reset requires quiescence, reporting before reset and separation from
legitimate late updates belonging to the old period. Resetting totals does not
release leaked storage or prove code, allocator or view lifetimes safe. Do not
infer permission to force unload, weaken dependencies or reset a live context.
Any wait or refusal policy needs a concrete decision.

Release-build diagnostics and user-supplied logs remain useful for investigating
local setups and patch combinations. Automatic telemetry was mentioned as a
less likely possibility; it is not selected or authorised work.

## Alternatives considered, not scheduled work

The per-client interest-token proposal was set aside as too elaborate. Its useful
distinctions remain available for future lifetime design: asset identity differs
from a client's retained use and from request correlation; copying a view or
receiving a notification need not acquire a new retained use. A holder lending
views to its workers must keep backing storage alive through their use.

That proposal used one handle per client thread per asset, recipient translation
and reverse lookup. Sharing established the recipient's interest before releasing
the sender's; repeated notifications did not create obligations. Host operations
held their own interests; retiring one client handle did not dispose other users'
handles or necessarily evict cached content. Repeated acquisition and balancing
remained open. These are historical alternatives, not the implemented disposal API.

For missing recipient interests, alternatives were discarding the message,
replacing it with a correlated failure envelope, or Ritchie's proposal for a
common validity flag making the original message unactionable. No alternative
was selected. Any future scheme must preserve safely accessible type/correlation,
prevent use of unprotected embedded views, and destroy owned payloads while their
module dependencies remain valid. Sending a zero handle alone would not suffice.

The proposed restriction of reattribution to new byte/image wrappers was also
withdrawn. Existing container and live-document transfer support is intentional;
rich working structures may still be reconstructed locally from baked views.
The implemented uniform attribution interface supersedes the old friendship-led
wrapper redesign. Another generic byte-interpretation/type-erasure layer remains
unselected. Payload descriptors would not replace content validation.

## Returning to these topics

Add new deferred observations here, with links to their reasoning. When a topic
becomes necessary, explicitly select its scope and move the relevant requirements
into a current specification, revisiting assumptions against the implemented
system. This document alone does not authorise work or require speculative extension
points in the immediate implementation.
