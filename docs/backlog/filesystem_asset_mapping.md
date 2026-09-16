# Filesystem asset mapping

Updated 16 September 2026. Initial design discussion, not an implementation
specification. This develops the filesystem portion of the
[consolidation plan](consolidation_pass.md). Current ownership follows the
[active stages](consolidation_design_order.md): Host assets remain owned until
application exit and asset operations use already issued Host asset IDs.

This file retains historical proposals as well as current filesystem discussion.
The separate [deferred design resource](consolidation_deferred_design.md) collects
the later overlay, trust and lifetime topics; those portions of this history do
not add requirements to the immediate design or its implementation.

Latest deferred direction, 15 September: stability is a developer responsibility;
the earlier self-healing trust model is being reconsidered. Core content will
likely be selected through a distributed configuration specifying files and
safe-loading options. Discovery has a larger role for UGC and our own content
production, which are expected to share much of the local infrastructure while
differing in remote storage and distribution. The
[deferred design resource](consolidation_deferred_design.md#developer-responsibility-core-configuration-and-shared-discovery)
records this clarification; the historical trust/selection proposals below must
be read in that light. Immediate design and implementation scope is unchanged.

Current direction, 15 September: Ritchie considers the trust system premature.
Its risk scoring, staged first-use recording, trust files and heuristic recovery
selection below are preserved for possible future consideration, not current
implementation requirements. This does not by itself discard filesystem mapping,
source trust classification, validation or module compatibility checks. The
interest-token machinery referenced here is superseded; see the
[design-order scope update](consolidation_design_order.md).

Further scope clarification, 15 September: overrides and document layering remain
expected requirements, but their detailed specification is premature and is
deferred. The general filesystem image remains current: configured locations,
discovery, source identification/classification and a useful representation for
loading, saving and editor browsing. Earlier patch/override/layer rules below
are retained as future context rather than prerequisites to this initial image.
Do not infer that deferring selection/composition policy removes the need for
the image itself. Its exact minimum fields and update contract remain open.

## Immutable image publication and identifier lifetime

New discussion, 16 September. This captures Ritchie's proposed direction for the
deferred filesystem image; it does not schedule implementation or settle the
remaining concurrency and identity contracts. Earlier references below to interest
handles do not prescribe the retention mechanism for this proposal.

Part of the image's purpose is to let requests specify files by identifier without
transporting a filename or path. The image supplies the mapping needed to resolve
that identifier. Filesystem identifiers and IDs for already loaded Host assets
have separate roles; their relationship remains to be specified.

Treat each published filesystem image as immutable. Build an updated image
separately and publish it by atomically replacing the pointer to the current image.
Use entry/exit accounting for image users so that a drain process can retire an old
image once all its users have exited. Publication and reclamation are separate
steps: replacing the current pointer does not itself make the previous image safe
to reclaim. No particular entry/exit implementation is selected yet.

Ritchie identifies two additional requirements to resolve:

- IDs in flight may need to preserve the image against which they were issued,
  including time spent queued before the receiving operation enters the image.
- A bulk query, such as building a path tree for display, needs a way to determine
  whether the current image changed during that query.

The following gaps are recorded for later design, rather than resolved here:

- **Identifier meaning across replacements.** Decide whether identifiers are
  image-local, include an image generation, or have stable meaning across images.
  Define how a receiver selects the right image and handles an unavailable or
  removed entry. Reusing an identifier must not silently select a different file.
- **Safe entry and retirement.** Define the ordering between obtaining the current
  pointer, registering a use, replacing the pointer and draining old uses. There
  must be no interval in which a reader holds an unprotected pointer that the
  drain process can reclaim. Atomic pointer replacement alone does not settle
  this acquisition protocol.
- **Queued work and retention.** Specify who retains an image while an ID is in
  flight, how that retention reaches the receiver, and when it ends on completion,
  cancellation, failed dispatch or discarded work. Zero active readers is not
  sufficient for retirement if queued work still depends on the old image.
- **Bulk-query consistency and change detection.** Distinguish keeping one image
  alive for a consistent traversal from checking whether a newer image became
  current. Define a version/change indicator and whether a changed query result
  remains usable, is marked stale or must be rebuilt. Comparing pointer addresses
  alone needs consideration of address reuse. Do not silently combine entries
  from different images during one traversal.
- **Update and shutdown boundaries.** Define publication ordering if updates
  overlap, unsuccessful replacement handling, final drain and the treatment of
  long-lived users. Image retention remains separate from loaded-asset retention.

Immutability here applies to the catalogue, not to bytes on the device filesystem.
Existing questions about external file changes and already Host-cached content
still apply. Scans, user-triggered refresh and updates from internal saves can all
produce replacement images; this proposal does not change their game/editor policy.

## Direction supplied by Ritchie

- Support Windows, Linux and probably Android through common resource mapping
  for loading and saving, allowing for platform-specific storage considerations.
- Discover game-packaged assets and distinguish temporary assets, local
  configuration and user-generated content. The latter may be made available
  through mechanisms supplied by Epic and Steam; specific integrations remain
  to be investigated and selected.
- Scan predetermined search directories, not the entire device. Discovery data
  also supplies the editor's file pickers.
- Categorise discovered assets, likely using location and extension. Payload
  self-identification belongs outside filesystem discovery.
- Perform an initial scan and support optional rescanning and updates. Changes
  must be resolved without destroying references to already Host-cached assets.
- Consider live and baked documents as assets useful to this service.

## Clarified source groups, trust and overlay participation

Ritchie distinguishes core assets from secondary assets (UGC). Core sources are
assumed trusted; UGC sources are assumed untrusted. These are source-trust
assumptions, not claims that discovered payloads have passed format validation.

Within each group, items need identification of their overlay participation:
items that may be overlaid, items that may overlay others, and items that must
stand alone. Further combinations are expected; the complete vocabulary and
representation are not settled. Source group alone does not determine an item's
overlay role. The relationship of temporary storage and local configuration to
these groups remains to be specified.

For discussion, treat permission to act as an overlay and permission to receive
an overlay as separate properties. Stand-alone items do not participate in that
relationship. Whether both permissions may apply together, how targets are
identified, who authors the rules and how multiple eligible overlays are ordered
remain open.

Ritchie defines two overlay forms:

- Override: select a complete replacement asset according to priority.
- Layered: modify parts of an existing asset. This is likely to be restricted
  to JSON documents, including baked binary JSON; that restriction is provisional.

Ritchie proposes a simple selection rule: where an eligible replacement for a
core file exists, normal loading uses it; otherwise it uses the original.
No numeric priority scheme is implied. Multiple competing replacements and their
interaction with cached results remain open.

Ritchie clarifies two recovery levels:

- Safe mode uses the last known safe configuration.
- A harder safe mode uses only the original shipping assets, DLLs and other
  shipping components.

This supersedes the earlier shorthand that all safe-mode loading means original
content only. Ordinary safe mode does not inherently exclude patches, overrides
or layers; inclusion depends on the recorded safe configuration. Recovery scope
includes executable modules as well as filesystem assets. How recovery evidence
is recorded and how required versions remain available are open design questions.

### Heuristic assessment of known-safe configurations

Ritchie defines "known safe" as a heuristic assessment subject to revision.
Loading content does not establish that all of it has been exercised. Hard
failures and DLL handshaking failures can be observed; other content may carry
a risk factor that decreases with increased use without failure. The factor,
measurement of use, thresholds and update rules are not yet specified.

Ritchie identifies relative starting risks by asset type/role:

| Asset type or role | Relative risk |
| --- | --- |
| DLLs | High; more likely to cause immediate, catastrophic failures |
| Shaders | Similarly high danger |
| Configuration JSON | Also high |
| General content, maps, textures and similar assets | Relatively low |

These are qualitative heuristic categories, not numeric scores or guarantees.
The assistant suggests assigning baseline risk by intended role as well as
format: a configuration document and a map document need not have the same
risk merely because both use JSON. Risk remains separate from source trust and
overlay participation. Exact category mapping and how baseline risk combines
with successful-use and failure evidence remain open.

For discussion, the assistant proposes keeping explicit failure evidence
separate from accumulated confidence. Successful startup or long runtime alone
must not be recorded as proof that every loaded asset was exercised. Evidence
should identify the relevant content/module versions and configuration so a
changed contribution does not automatically inherit confidence from its prior
version. A failure affecting a configuration does not by itself establish which
asset caused it. These evidence and attribution details remain proposals.

The heuristic informs selection of the last known safe configuration; it is
distinct from source trust, overlay eligibility and the original shipping
baseline available through harder safe mode. The procedure for choosing and
promoting a recovery configuration remains open.

Ritchie proposes persisting a record of files being used for the first time
before making use of them. If the application crashes before it can diagnose
the cause, the next run can identify those files as possible contributors.
The record supplies suspicion/evidence, not a finding that a particular file
caused the crash.

Ritchie clarifies that first-use completion is asset-type-dependent and likely
staged. For a DLL, the proposed evidence boundaries are:

- Successfully loaded: establishes that the DLL was available and could load.
- Successfully bound: establishes that binding completed without an immediate
  crash; explicit handshaking failures remain separately observable.
- Shut down without issue: provides strong further evidence, probably weighted
  by the time the DLL was present before unloading.

The assistant proposes retaining the highest completed stage and recording the
next potentially failing stage before entering it. A later crash would then
preserve the earlier successful evidence. Elapsed loaded time can contribute to
weighting without claiming continuous execution or complete code-path coverage.
Exact stage definitions, persistence points, weights and which observations
retire first-use monitoring remain open. The transition from shutdown completion
to native unload completion also needs precise definition.

The assistant suggests identifying the content version and active configuration
in each record, rather than relying only on a reusable pathname. Define stages
for other asset roles and how session completion affects confidence. Concurrent first uses may leave
several candidates; recovery must not assume one last filename identifies the
cause. An uncleared record also does not distinguish an asset crash from other
abnormal termination.

The record belongs in a permitted persistent, writable recovery area available
on the next launch. Its write must precede the potentially failing use; merely
queueing an asynchronous write does not establish that ordering. Required
durability, interrupted-write handling, growth/compaction and behaviour when
recording fails remain to be specified. Recording before a relevant operation
may mean before module load/initialisation or asset processing, not only before
a finished asset is published. These mechanics are proposals for discussion.

Ritchie suggests that risk management could be represented by a live document
or a process file consumed and reconciled at the next application load. The
following file arrangement develops that proposal. A dedicated trust directory
is agreed; file formats and platform-specific physical placement remain provisional.

### Proposed trust-state and process files

Ritchie proposes `trust_state` as a JSON file and `trust_process` as a CSV file.
Both belong in a dedicated `trust` directory, as agreed by Ritchie; the earlier
logging-directory proposal is superseded. On some platforms `trust_state` may
need to form part of saved-game storage, with the common trust-area contract
mapped to that platform's permitted persistent storage.
These are proposed logical file names; exact extensions and platform mappings
are not fixed. The trust naming here refers to accumulated risk/recovery evidence,
distinct from the core/UGC source-trust classification.

The proposed application-load sequence is:

1. Load `trust_state` and the previous run's `trust_process`, where present.
2. Reconcile the process evidence into the trust state.
3. Save the updated trust state.
4. Delete the consumed process file.
5. Create a new process file for recording the current run.

This is the recovery-record lifecycle, not the complete framework design or
implementation order. First-run handling, corrupt or incomplete inputs, storage
failures and precise reconciliation rules still need definition.

The assistant proposes giving each process record a run identifier and recording
its consumption in the saved state. If the application stops after saving state
but before deleting the old process file, the next startup can finish cleanup
without applying the evidence twice. Deletion must follow successful persistence
of the reconciled state. The fresh process file must be ready before the first
use requiring a persistent marker. These recovery details are not yet agreed.

Platform placement must make the state and previous process record available
at the required startup boundary. If saved-game storage is used, clarify whether
that means application/profile metadata or a particular gameplay save, and how
reconciliation occurs before monitored module/content use. The process record
must also remain available across launches in the dedicated trust area.

The assistant suggests that these may be complementary: a thread-local live
document holds current risk assessments, while a durable per-run process record
preserves stage intentions, completed boundaries and relevant timing evidence.
At the next launch, reconcile that evidence with prior assessments before
selecting a recovery configuration. A live document in memory alone cannot
supply evidence after a process crash; persistent writes remain necessary.

Persist the reconciled assessment before retiring its input record, and define
how retry after interrupted reconciliation avoids applying the same evidence
twice. Clean-session evidence can contribute confidence as well as incomplete
records contributing suspicion. Whether the process file itself uses the document
format, a sequence of events or another representation, and when evidence is
consolidated, remain open. Avoid requiring a complete risk-document rewrite at
each stage unless its cost and interrupted-write behaviour are acceptable.

Overrides and layers are primarily resolved when loading the application as a
whole. The filesystem service establishes which files are permitted for that
application load. External filesystem changes and refreshes of the application's
resource image are separate concerns; refreshing the latter is user-triggered.
This supersedes any implication that every asset load automatically reselects
overrides or layers from the current external filesystem.

### Game and editor refresh policy

In the game, refreshes will likely be limited to saved-game lists. Major external
content changes are not reflected during the running game; they take effect
after exit and restart. The exact scope of minor refreshes remains provisional.

Assets changed internally and exported during play can update the filesystem
image from information already held by the application, without rediscovering
the change through a device-filesystem scan. Saving still changes the device
filesystem. Updating the application's filesystem image and scanning the device
are therefore separate operations; not every image update requires a rescan or
user refresh command.

The editor needs user intervention and decision making around refresh and
loading overlays or overrides. The exact choices, refresh granularity and
publication workflow remain open. Game startup/restart policy must not be
assumed to constrain editor workflows in the same way.

An assistant-proposed distinction for internal exports is to keep live asset
state separate from confirmed saved-source state: an attempted or failed save
must not make the catalogue claim that an on-device file was successfully
updated. The timing of catalogue publication and save-failure reporting remain
to be specified.

### Patches as overrides and layers

Ritchie expects patches to make use of the override/overlay system. Overlay
participation therefore cannot be treated as synonymous with UGC. Patch
provenance, trust assignment and relationship to the core/secondary grouping
need explicit definition; these are not determined merely by being a replacement
or layer. Patch installation/distribution mechanisms are outside this initial
resolution discussion.

Safe-mode treatment follows the clarified recovery levels above: use the last
known safe configuration, or the original shipping baseline in the harder mode.
Patch approval alone does not establish membership of the last known safe
configuration. Selection must retain the distinction between those baselines.

Multiple patch contributions, their order and interaction with UGC also need
definition without assuming a general numeric-priority scheme. The established
game policy remains that major external content changes take effect after exit
and restart; reuse of the overlay mechanism does not imply live patch adoption.

### Remaining overlay and publication questions

An assistant-proposed consequence is that cached content must correspond to the
selected configuration; a matching logical name alone is insufficient. Harder
safe mode cannot reuse modified content as original shipping content. Whether
mode changes are possible during a run, how refresh revises active selection,
and what the editor exposes in recovery modes remain open. Selection at startup
does not by itself require eagerly loading every permitted asset's bytes.

Layer semantics are not yet specified: matching members, modifying arrays,
deletion, type changes, ordering and conflicts all remain open. In particular,
layering must not implicitly adopt parser collision-extension behaviour as a
merge rule. The interaction between override selection and layer application
also needs definition.

The assistant proposes that discovery/resolution identify eligible sources,
their overlay forms and ordering, while document conditioning interprets and
applies layers. Already published baked assets remain immutable; a layered
result would be constructed privately and published as a new asset. Its source
dependencies and trust provenance would include the contributing layers.
These execution and publication details are proposals for further discussion.

An assistant-proposed invariant is that selecting UGC through a trusted core
logical name must preserve its untrusted provenance. Eligibility to overlay
should be checked against framework-authorised rules for the target, rather
than granted by an untrusted source's own claim. Discovery should retain the
individual sources and their group/trust information even when resolution selects
one of them. These enforcement and provenance details await discussion.

## Proposed separation of responsibilities

The following is an initial assistant proposal for discussion.

### Configured roots and logical resource mapping

A root describes an approved discovery location, its logical namespace and
storage purpose. Platform-specific locations resolve behind that mapping.
Packaged content, temporary storage, local configuration, platform-specific
settings, saved games and user-generated content are purposes to distinguish;
they are not yet final enum values.

Purpose, permitted operations and content category are different properties.
For example, configuration may be JSON, and user-generated content may contain
both images and documents. Root purpose should not by itself imply that every
source is writable. Saving also needs to resolve destinations that do not yet
have a discovered source entry.

Ritchie's direction is to define permitted save areas and apply that restriction
uniformly on all platforms, including PC, anticipating Android and console
requirements. Platform-specific settings and saved games therefore need explicit
storage categories. Exact platform APIs and physical destinations are not yet
selected; this is a common framework policy, not a claim that platform rules
are identical. Console support is an anticipated consideration, not a settled
implementation target.

An assistant proposal is for save requests to identify a storage purpose and
destination relative to an authorised root. The Host resolves and checks the
destination; editor save pickers expose destinations allowed by the same policy.
Read/discovery access and overlay eligibility do not imply write permission.
Where selected content came from should not automatically decide where a save
is written. These routing details and how permissions vary by client or mode
remain open. The direct-location service, if retained, must obey the common
permitted-save-area restriction too.

Avoid requiring the public resource key to be an absolute native path. Whether
all initial roots are ordinary directories, and how packaged or service-provided
content is exposed, remain open. No Epic, Steam or Android API is selected here.
Predetermined scan scope does not settle the separately proposed direct-location
loading service or its authorisation rules.

### Discovery catalogue

Discovery records source location, names, hierarchy and category evidence from
configured rules. It does not establish that file contents are a valid image or
document. Opening and conditioning retain their own validation boundary.

Candidate metadata includes root identity, root-relative name, logical source
key, extension/category, availability and observed change information. Exact
fields and identity rules are open. A logical key and a catalogue row or document
node index must not be assumed to be the same identity.

The editor can browse and filter this catalogue. Selection identifies a source
to resolve through the service; a catalogue entry alone neither loads content
nor grants a retained interest in its loaded asset.

### Loaded assets and rescan updates

Keep discovered source records distinct from the application's active resource
selection and from loaded content versions and their Host-owned assets. External
changes do not automatically refresh the application's resource image. External
refresh follows the game/editor policy above, while internal save/export results
can update the image directly. Neither path may repurpose an existing interest
handle to identify different bytes or destroy its backing asset.

For example, a client may retain an image loaded before its source file changes.
A subsequent user-triggered refresh can record the change while that image
remains usable. Publication of refreshed content need not overwrite the old
image. Source disappearance likewise does not retire existing asset interests.
Whether discovery inspection can run separately from applying a refresh remains
open, as does the handling of changed source bytes when a permitted file has
not yet been loaded. Startup selection alone does not provide a byte snapshot.

Change detection, refresh scope, cache reuse and replacement-publication policies remain open.
Also distinguish an absent source from a source or root that could not be scanned.
A failed or partial scan must not silently report every unobserved file as deleted.
The initial implementation need not provide automatic watching, full content
hashing or complete cache remapping.

### Live construction and baked catalogue views

One candidate is to construct or update scan metadata in a thread-local live
document, then publish a baked catalogue snapshot for editor and other readers.
Its backing storage would use ordinary Host ownership and interest handles.
Readers retaining an older snapshot can finish using it while a new snapshot
is published. Snapshot retention and retention of assets described by the
snapshot are separate; describing a file does not require keeping its content
loaded.

Live keys and baked indices are document-local and must not become durable
source identities. Decide which stable source identities appear in the document
and how runtime lookup indexes relate to it before choosing a representation.
This proposal preserves the existing thread-local live-document contract.

## First questions to resolve

Use the [design and implementation order](consolidation_design_order.md) to stage
these questions. Platform research, including Android/Quest 3+ storage and
Steam/Epic UGC services, precedes fixing the contracts that depend on it.

- Define roots and logical source keys within the core/secondary grouping,
  including how standalone items are addressed. Specify overlay eligibility,
  target matching, precedence and conflicts without conflating them with trust.
- Define relative resolution, name comparison and path normalisation across
  platforms, including containment when traversing configured roots.
- Define permitted save areas for temporary storage, configuration, platform
  settings, saved games and user-generated content, and resolve new destination
  names under the common restriction on every platform.
- Establish category rules and handling of unknown or ambiguous extensions.
- Specify game saved-list refresh and editor-directed refresh, including
  publication, incomplete scans and changed/missing-source status. Define direct
  image updates from internal exports and their relationship to save completion.
  Major external game-content changes take effect only after restart.
- Decide cache reuse and handling of externally changed, not-yet-loaded sources
  while existing interests retain their current assets. Renames and
  source-identity continuity remain open.
- Evaluate the catalogue document shape and whether snapshots meet editor needs.

The asynchronous binary/JSON persistence acceptance exercise remains part of the
overall consolidation. Filesystem tests should additionally cover constrained
scan scope, classification, source changes/removal, failed scans, retained old
catalogue/content views and correct resolution of subsequent load/save requests.
