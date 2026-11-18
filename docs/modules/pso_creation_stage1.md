Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   pso_creation_stage1.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   16 Nov 26

# Stage 1: Device/display discovery and minimal GPU execution

## Status and objective

Focused design in progress under [staged development](../backlog/shader_system_stages.md).
On 16 November the user selected three increments for focused design: device/display
capability audit, a presented graphics triangle and a simple verified compute job.
Audit and graphics are the primary delivery; compute retains a separate acceptance
checkpoint. This expands the earlier creation-only proposal. The prerequisite design
review is complete as recorded below; the complete Rendering implementation contract
remains open. The user authorised schema-prerequisite implementation on 17 November
2026; its implementation review and acceptance are now complete as recorded below.
The user authorised Platform-report implementation on 18 November 2026 as recorded
below. Rendering implementation remains unauthorised.
The stage-design chat refines the design with the user and the overarching
coordinator validates it.

Coordinator review, 17 November 2026: no blocking findings in the schema-extension
or Host-owned Platform-report prerequisites. Both are design-approved for separately
authorised implementation and review, subject to the handoff and acceptance
conditions recorded under "Review status and remaining decisions". Rendering 1A to
1C is architecturally consistent, with the remaining concrete interfaces, Vulkan
integration and operational choices listed there. This is not yet a complete
implementation handoff for Rendering.

Establish a small, usable path from discovery and explicit engine-produced shader
components to validated native pipelines and demonstrated GPU work. Rendering owns
graphics/compute backend objects; Platform supplies native display/window facilities.
Keep file access at the existing Host boundary. Executive data, semantic providers
and a new participation protocol are not prerequisites.

## Agreed prerequisites and implementation order

On 16 November 2026 the user identified two prerequisite pieces that must be
completed before Rendering increments 1A to 1C commence:

1. Extend the schema system with the agreed text and typed variable-length
   reference semantics, storage, retention and relocation support.
2. Implement the Host-owned Platform report mechanism and the defined Platform
   reports, using that schema support and the agreed communication/lifecycle rules.
3. Proceed with the previously agreed ordered Rendering increments: 1A audit,
   1B graphics/presentation, then 1C compute, retaining their separate review and
   acceptance checkpoints.

The user indicated Astra at Extra-High reasoning as the preferred model for these
future implementation pieces, given the schema system's size and the cross-module
Host/Platform communication and lifetime work. Record this as an implementation
preference, not an instruction to change the current design chat's model, spawn
agents or create implementing tasks. No implementation is authorised by this order.
The prerequisites have passed coordinator design review; the complete Rendering
handoff still needs refinement and review before implementation. On 17 November
2026 the overarching coordinator confirmed that the staged-plan reconciliation is
complete and no blocking design or handoff conditions remain for starting separately
authorised schema implementation. Accept that implementation before beginning the
Platform report prerequisite.

On 17 November 2026 the user requested creation of the schema implementation chat.
`01a1e917-4e55-75c1-a041-c26cdaddf868` implements that prerequisite in the shared
checkout using Astra at Extra-High, with this focused design chat
(`01a1e419-b2d2-7f10-ade3-921130d6026c`) as its implementation-review coordinator.
Its scope includes affected consumers/artifacts, schema documentation and the
agreed builds/tests. It must deliver review-ready evidence and pass independent
schema acceptance before Platform report implementation begins. No Platform or
Rendering implementation, additional implementation chats, commit or push is
authorised by this schema handoff.

## Three separable increments

Design the shared device selection, capability requirements, diagnostics and lifetime
boundaries together, then implement and review each increment separately when
authorised. Each has its own evidence and completion claim; a passing audit or native
pipeline creation alone does not establish execution or presentation correctness.

### 1A. Device and display capability audit

Enumerate graphics devices and a bounded, useful set of their API/backend identities,
features, limits, queue capabilities and relevant format support. Report attached
displays, current display configuration and available resolution/refresh-rate modes.
Define how device, display and presentation-support facts relate without assuming
a one-to-one device/display association. Keep queried support, enabled device features
and the requirements of a selected workload distinct.

Start with a discovery snapshot. Define output identities, provenance and unavailable
or unsupported query results. The focused design selects the exact fields and any
explicit re-audit operation. Continuous change observation, display-loss recovery and
mode-changing policy require separate decisions; enumeration does not imply their
implementation. Select any required initial behaviour for the execution fixtures.

Refresh-rate modes describe display capabilities/current configuration, not achieved
rendering frame rates. Workload performance measurements are a separate result.
The audit should remain useful when presentation or a suitable execution device is
unavailable, retaining the facts and reasons it could obtain.

#### Focused design: agreed audit purpose

The user accepted the discovery-snapshot purpose on 16 November 2026. Explain the
available devices and displays and supply facts for later workload selection. The
audit remains useful without creating a window or finding a suitable execution
device. Record the claims returned by the API, including reported capabilities and
support, together with query provenance and unavailable, unsupported or failed query
outcomes. A successful query means information was retrieved; it does not establish
that the reported capability has been demonstrated. The audit does not attempt an
advance test matrix of advertised devices, features, modes or combinations.

Later requests retain their own associated evidence: requested configuration,
enabled features, creation/submission/presentation outcomes and any verified output,
as applicable. Associate those outcomes with the particular request and environment;
do not overwrite the original API claim or infer success for other combinations.
Discovery does not select a workload or establish enabled features, pipeline
creation, execution or verified output.

The user accepted the persisted-baseline/candidate comparison, dependency-based
rebuild assessment, adoption/file-handling rules and baseline format/location below.
The remaining concrete interfaces still require refinement. The
agreements here are not approval of the complete stage design or implementation.

#### Agreed persisted-baseline and candidate-comparison direction

On 16 November 2026 the user requested that an explicit refresh produce a candidate
report identifying whether it matches the existing report. At startup, load the
saved report from a file and compare a newly discovered candidate against it to
identify what, if anything, needs rebuilding. This supersedes a proposal based only
on replacing the current discovery snapshot and invalidating its local references.
The user accepted dependency-based rebuild assessment and added mandatory device/
driver-change PSO invalidation. The agreed candidate-adoption and file-handling
rules follow below.

This adds audit-report persistence and change comparison to increment 1A. Use the
existing Host file/asset boundary. The saved report records API claims and query
coverage/outcomes; it does not restore native handles, live selection authority,
enabled-device state or proof of successful execution. The agreed baseline format
and location follow below.

Compare meaningful capability/configuration claims using native identity evidence
to correlate records. Ignore capture timestamps, audit-local IDs and enumeration
order when deciding whether the relevant claims match. Report matches, differences
and facts that cannot be compared; retain known differences even if other parts are
incomplete. Do not substitute old values for missing candidate facts.

Assess rebuild impact against actual dependencies:

| Change | Impact |
| --- | --- |
| Display label or unused display modes | Update discovery information; no unrelated resource rebuild. |
| Selected surface extent/configuration | Rebuild affected swapchain resources. |
| Selected attachment format | Rebuild affected format-dependent pipeline/render-pass objects. |
| Device or driver environment | Invalidate every existing or cached PSO built for the previous device/driver environment, not merely revalidate it for reuse. |
| Required facts unavailable | Report unresolved compatibility and preserve the query failure; do not infer a match. |

Device/driver-change invalidation covers all PSOs associated with the changed
environment. Prevent new use, but retain objects referenced by accepted in-flight
work until safe retirement. Do not immediately destroy them or silently retarget an
owner. Independent environments retain their own dependency assessments.

A report match does not establish that shader/recipe inputs are unchanged or that
a cached artifact is usable; those dependencies still require checks. Keep API
claims, invalidation/rebuild decisions and actual creation/execution/verification
outcomes separate. Saving fresh observations must not claim that dependent rebuilds
succeeded. Each retained artifact/resource needs an association with the
configuration it was actually built against.

Loaded audit-local IDs are historical report references. Fresh discovery establishes
live bindings; matching reports do not restore native handles or selection authority.
Native GPU objects require creation at startup even when the reports match.

The scope of rebuild decisions for current graphics resources and any saved
artifacts, together with the boundary to stage 4's compiled-PSO library persistence,
requires overarching review. Loading an audit baseline does
not by itself introduce a native PSO cache implementation. Concrete identity/
selection interfaces still require refinement against the accepted policies.

#### Agreed candidate adoption, saving and unusable-baseline handling

The user accepted these rules on 16 November 2026. Startup and explicit refresh
automatically adopt a valid candidate after comparison and impact assessment.
Apply invalidations before permitting new use of affected objects. Rebuild success
is a separate outcome; affected PSOs remain unusable when rebuilding fails.

An audit-only operation may adopt observations without running graphics or compute.
Do not automatically replace a good baseline with newly failed, truncated or
inconsistent required queries. Return that partial candidate and its diagnostics.
Legitimate absence, such as no attached displays, is a valid discovery outcome.
Every resource/cached artifact retains its own build-environment association;
saving a new audit never makes an old PSO compatible, even if the next candidate
matches that saved audit.

Save the first usable baseline and subsequently adopted reports with meaningful
changes. An unchanged comparison need not rewrite the file; its candidate records
when the latest check occurred. Use Host's existing temporary-write/replace path to
preserve the previous file on write failure. Discovery adoption, rebuild outcomes
and persistence outcomes remain separately identifiable. Save failure does not undo
successful runtime work; report that the current in-memory baseline is unsaved.

| Baseline file condition | Behaviour |
| --- | --- |
| Missing | Normal first-run path: discover, assess, adopt and save a usable candidate. |
| Corrupt/structurally invalid | Report the reason, disregard it for compatibility decisions and use fresh discovery. Retain diagnostic evidence before replacing it with a usable baseline. |
| Unsupported format version | Treat compatibility as unknown and regenerate from fresh discovery; report the version mismatch. |
| Read failure | Report an I/O failure without labelling it missing; proceed with fresh discovery where possible. |
| Save failure | Retain the current in-memory report and unsaved status; preserve any previous baseline file. |

Validate format/version, required fields, bounds and internal references before
comparison. An unusable baseline never authorises cached-PSO reuse. The distinction
between missing and other read failures must be supported by the Host I/O outcome;
the current generic read-failure result must not be guessed to mean not-found.
Identify any necessary generic status extension during interface review.

#### Agreed baseline file format and location

The user accepted the existing baked-document `.bsd` format at the following
Host logical location, with JSON available for inspection:

`state:/rendering_device_display_audit.bsd`

Use the existing Host asset load/save facilities. The audit has its own schema
version, initially 1, independently of the baked-container format version.

| Group | Contents |
| --- | --- |
| Identification | Document kind, audit schema version, backend and query-profile version. |
| Capture metadata | Capture identity/times and provenance, excluded from capability equality. |
| Discovery | Device, display, path and mode records with reported identities and relationships. |
| Query coverage | Queried fields, filters, unavailable/unsupported outcomes, failures and completeness. |

Validate the baked structure, then audit schema, types, bounds and references.
Compare meaningful fields, not serialized-byte equality. Typed integer values are
retained directly in the baked document. Use the existing document writer for JSON
inspection/export; the `.bsd` file is the authoritative baseline.

The existing `state:` root is configured for machine-specific runtime state. In the
current development manifest the baseline resolves beneath
`development/logical-roots/state/`, already ignored by Git. Keep its filename stable
across device/driver changes so startup can find the previous environment. Put it
directly beneath the existing root; no new directory-creation facility is required.

Allow a Rendering configuration/request override using a Host logical path. Keep
one baseline per configured runtime workspace initially, with one writer per
baseline path. Acceptance tests use isolated root mappings or overrides and preserve
the normal runtime baseline. Exact configuration/request entry points remain part
of the interface refinement.

References: [logical roots](../../development/README.md#roots),
[root manifest](../../development/root-manifest.json),
[checked baked documents](../../foundation/data_model/baked_document.hpp)
and [document writer](../../foundation/data_model/document_writer.hpp).

#### Agreed audit-local references, stale selections and checks before use

The user accepted this policy on 16 November 2026. Keep report references separate
from live selection references and keep transient live bindings separate from saved
baseline adoption.

| Reference | Meaning |
| --- | --- |
| Report reference | Capture identity plus a typed device/display/path/mode record ID; remains useful in saved/retained reports. |
| Live selection reference | Current Rendering audit owner, capture generation and device record ID; resolves through a live Rendering-owned binding and is never restored from the file. |

Do not reuse or wrap counters within their owning lifetime. Native identity evidence
establishes correspondence across reports; names and record numbers do not. A saved
report does not preserve its runtime owner, live binding or selection authority.

Publishing a completed refresh creates a new live generation and returns fresh
selection references. Previous live references become stale even when the reports
match. This reference change alone causes no PSO rebuild: existing pipeline/session
owners retain their dependencies and follow the dependency-based invalidation rules.
A refresh failing before publication leaves the previous generation intact, without
making its facts freshly checked.

Partial candidates may provide usable device records when all requirements for that
workload are known. An unrelated display-query failure must not prevent independent
compute execution, even though that partial candidate cannot replace the good saved
baseline. A confirmed device/driver change invalidates affected PSOs for new use
immediately, even if another query prevents candidate adoption. Preserve accepted
in-flight uses through safe retirement.

Stale, retired or missing bindings fail explicitly. Do not automatically translate
an old local ID into a new device. Explicit UUID selection resolves uniquely against
fresh discovery; missing or ambiguous matches fail.

| Boundary | Checks |
| --- | --- |
| Device creation | Confirm selected identity/driver environment and workload-relevant API, extension, feature and queue requirements. |
| Pipeline creation/reuse | Confirm the device/environment association remains valid; validate artifacts and recipe against the actual enabled device profile and relevant limits/formats. |
| Swapchain creation/recreation | Requery surface support, format/colour-space pairs, present modes and capabilities; read current Platform window state. |
| Each new frame | Check current close/minimisation/drawable state. Do not repeat full display enumeration and device-feature queries every frame. |
| Resource allocation | Obtain concrete memory requirements and compatible memory types; retain allocation outcomes separately. |

Checks produce operation-specific evidence without mutating the audit. If they
reveal a device/driver change, all PSOs associated with the previous environment
become invalid for new use. Queries and creation are not an atomic transaction with
the OS/driver; native failures remain possible after successful checks. No automatic
fallback or recovery is implied by a stale reference or failed check.

#### Agreed Platform discovery access and ownership

The user confirmed that native display work belongs in Platform, with
platform-agnostic data exchange available to any module. Rendering is one consumer
of this service; it has no privileged ownership of Platform communication or
display-discovery requests.

Expose the report contract in shared Framework headers alongside the existing
Platform contracts. Any authenticated module client may request the information
through shared, platform-agnostic communication and generic Host admission. The
exact report request/forwarding interface is being refined below. Do not add a
Rendering-only allowlist, private Rendering gateway or
requirement to obtain another module's permission to query Platform. Module loading
and service acquisition use the existing generic lifecycle facilities and confer no
exclusive ownership privilege.

Each caller has its own request correlation, results and independent borrow/use
lifetime. Authenticate cancellation/release against that caller's route and borrow;
one client's cancellation, release or departure must not invalidate another client's
result. Apply bounded capacity/admission rules uniformly. Host owns the shared
report backing rather than Platform providing independent backing for every
requester. The latest update-admission direction below supersedes the earlier
proposal to aggregate requests in progress.

| Participant | Responsibility |
| --- | --- |
| Platform | Native discovery and translation into the shared engine-owned contract; owns capture/staging state until Host accepts the result and producer-side work drains. |
| Framework | Platform-agnostic request/result declarations, schema/version contract and any reusable client-side decoding helpers. |
| Host | Generic routing, report-update admission, per-requester correlation and Host-owned immutable result storage with independent reader lifetimes; no native display-policy or Rendering-specific interpretation. |
| Requesting module | Opens its route, submits requests, consumes the shared result under its own acquisition or makes an owned copy, and releases its own use. Keeps its allocator/code dependencies alive for retained copies. |
| Rendering as one caller | Combines Platform facts with Rendering's GPU audit, persists its combined baseline and assesses its PSO/presentation dependencies. |

Public requests, statuses and payloads use engine-owned data. Windows SDK types,
DXGI interfaces and native implementation details stay inside Platform's adapter.
Where native identity evidence is required for backend correlation, use tagged
opaque values/provenance without exposing SDK structures or requiring ordinary
clients to run native queries. Shared clients must not need Windows-specific request
construction.

The initial result is an immutable snapshot with a versioned descriptor and a
defined Platform baked-document schema. Host owns the shared result; each receiving
module validates it and either retains its own protected acquisition or copies the
data before release. Snapshot publication does not require a window, Rendering
participation or GPU execution. Native Windows query details, exact generic Host
report request/result interfaces and bounded admission/progress defaults still require
focused refinement and overarching ownership/interface review before implementation.

Reference: [existing module/client cooperation](platform_host_pumped_design.md#module-loading-and-requester-lifetime).

#### Host report publication and shared result ownership

Host owns report results shared with requesters. An earlier proposal joined
matching requests to an in-progress capture's response list. The user subsequently
allowed update requests to be declined with an update-in-progress reason instead;
response-list aggregation is no longer required. Platform performs native discovery
and defines the display payload through the shared Framework contract. Host supplies
admission, storage and delivery; clients retain independent correlations and use
lifetimes.

The user subsequently asked to revisit this proposal because it had become more
complicated than envisaged. The intended model is a limited set of specific,
complete-purpose reports produced by Platform, with generic requests resembling
filesystem-image refresh. Native query operations remain entirely inside Platform.
The proposed arbitrary-command aggregation keys, operation-advertisement machinery,
four-capture/32-subscriber limits and no-completed-result-reuse rule are not agreed
requirements. Do not carry them into an implementation handoff as settled design.

Refine a small report-oriented flow: a caller names a defined information report;
Host admits a capture/refresh and forwards it to Platform; Platform collects the
report's declared information; Host owns/publishes the result and completes the
accepted request. Caller-specific native flags, per-query commands and arbitrary
operation deduplication are not part of this direction. The first pass updates the
whole report; section selection is deferred below.

The filesystem-image analogy is the request/private observation/Host publication
flow. The actual filesystem refresh currently queues up to 16 requests FIFO without
coalescing and integrates a private worker observation before replying. It is a
reference pattern, not an already implemented shared report/coalescing facility.
See [filesystem refresh messages](../../framework/filesystem/filesystem_messages.hpp)
and [current refresh behaviour](../../development/README.md#refresh-writes-and-cached-resources).

Host retention, initial capture, current-versus-updated request semantics, failed
update handling and required/permitted query outcomes are agreed below. Keep the previous
public report immutable while a fresh report is being produced. The identity of the
accepted requester must not make it the report's lifetime owner.

Platform hands off a frozen baked payload. Host must establish independent ownership
of its bytes and cleanup dependencies, for example by copying into Host allocation,
before acknowledging handoff. An apparent ownership move must not leave Platform
allocator/code dependencies hidden in the result. Once producer work and handoff
drain, retained shared-result readers do not require Platform native objects/code.

Host protects the shared backing through import, pending deliveries and all granted
reader acquisitions; result reclamation requires all those obligations to end.
Individual callers release their own acquisitions, not the shared backing on behalf
of other clients. Requester withdrawal, Host-owned capture cancellation and producer
handoff retirement follow the agreed capture protocol below. Do not carry forward the
earlier subscriber-count-based producer cancellation proposal as a requirement.

Existing Host storage/acquisition and provider `post_owner` facilities are starting
points. Define only the concrete report request/publication support needed here and
review any Host/Framework extension explicitly. The agreed single-capture admission,
native limits and request/result protocol below govern the first implementation.

#### Agreed Host-owned current Platform state and initial master report

The user confirmed on 16 November 2026 that Host owns and retains the current
assumed state of the platform as a master report. Obtain the initial report as
part of Platform startup or immediately after that module is installed, following
the filesystem-image model. This capture is independent of Rendering and does not
require a requesting module to establish private ownership of discovery.

Client modules can request the current report or an updated report through the
shared platform-agnostic interface:

- A current-report request accesses Host's held report for the current Platform
  installation without starting native discovery merely because another client
  asks to read it. Unload/reload availability follows the lifecycle policy below.
- An updated-report request asks Platform to capture fresh information for Host.
  While an update is in progress, another update request can be declined with the
  explicit reason that an update is already in progress; it need not join or queue.
- Host owns publication/storage and completes accepted requests. Platform owns
  how the defined report is captured and the native operations involved.
- The current report is an assumed snapshot with its capture identity, interval
  and outcomes, not a promise of continuously observed or atomically sampled state.

Use a limited set of Platform-defined report types, each fulfilling a complete
information need. Public requests name the information product; callers and Host
do not select individual Windows queries, native flags or native operation sequences.
The exact relationship between the master report and any separately requested
report types remains to be specified without an arbitrary-command framework.

Initial capture is scheduled immediately after Platform activation, as agreed.
Separate module/service readiness from initial report
availability; do not add a synchronous cross-module wait inside the module-load
handshake. Initial capture, refresh and failure follow the agreed lifecycle below;
the exact request/result schema and native query details require refinement.

This does not require Platform to be loaded for compile-only/headless Rendering
work. It requires the initial report when Platform is installed/started, whether
Rendering or another module requested that installation. Keep existing module
loading semantics and the distinction between loading a module and owning its uses.

Rendering remains one consumer of the shared Platform state. Its Vulkan-specific
GPU queries, combined shader/PSO audit baseline, dependency assessment and native
graphics/compute objects retain the ownership already agreed in this design.
Host's common Platform state does not acquire Rendering's PSO policy or infer
shader semantics. The combined saved audit and the Host-held Platform report have
distinct purposes and must not be silently conflated.

Reference: [Platform loading and readiness boundaries](platform_host_pumped_design.md#module-loading-and-requester-lifetime).

#### Update admission and agreed whole-report scope

The user allowed the simpler update-in-progress response on 16 November 2026.
Use one report update at a time as the working first-pass policy. Declining a new
update does not mean it was queued, joined to the running update or will receive
that update's eventual completion. Keep requests to read the held current report
separate from update admission; an existing report remains readable during capture.
Initial report availability and failed-update behaviour are agreed below.

The user confirmed on 16 November 2026 that the first pass uses whole-report
updates. Each accepted update captures a replacement for the complete defined
report. Do not add a public section mask, section-update scheduling or merging of
fresh sections into the old report for this pass. Whole-report capture does not
mean that every native query must be supported or that every field must be known.

Section updates and a bitmask identifying meaningful report sections are deferred.
If introduced later, settle section definitions, mask validation, retained-section
freshness and publication/failure behaviour. Bits identify report content rather
than Windows operations; Platform owns the native queries and dependencies.

#### Agreed initial availability, module lifecycle and failed-update behaviour

The user added an explicit unavailable case on 16 November 2026: no Platform
module is present. This is distinct from a present module whose initial capture
is pending or has failed.

The user also confirmed that unloading Platform makes the report unavailable, even
if Host previously published a report. Reloading Platform triggers a fresh
whole-report capture. Do not expose the previous installation's snapshot as the
current report while that capture runs or if it fails; a fresh usable publication
is required to restore current-report availability. An old retained acquisition
can remain memory-safe as historical data, but it no longer represents an available
current report. Preserve its original provenance rather than relabelling it as a
report from the new installation.

Associate capture work and publication with the Platform installation that produced
it. Late completion from an unloaded installation must not restore report
availability or replace a report from a subsequent installation. Unload must still
respect the existing producer-work retirement and reader-storage lifetime rules.

The user accepted this lifecycle and failure handling on 16 November 2026. Keep
report availability separate from update activity and the latest attempt's outcome;
an available report can coexist with a running or failed update.

- With no Platform module present, an update request returns unavailable with the
  reason that Platform is absent, rather than reporting a failed capture. Every
  current-report request also returns unavailable for that reason, regardless of
  previously published or retained snapshots.
- With Platform present but before the first successful capture for that
  installation, a current-report request returns promptly with no report and an
  explicit not-yet-available outcome. Include whether initial
  capture is in progress or has failed, with the available failure reason. Do not
  return an empty report as successful discovery or hold the request until capture
  completes. Module readiness remains separate from report readiness.
- During initial capture, including capture after reload, further update requests
  receive update-in-progress. With an existing current report from the same
  installation, current-report requests continue to acquire that held immutable
  snapshot while a replacement is being captured privately.
- On a successful usable capture and Host ownership/validation, publish the complete
  replacement with a new capture identity. Readers already holding the previous
  report retain valid acquisitions. Complete the accepted update only after the
  replacement has been published; do not expose a partially constructed report.
- If capture, validation, transfer or publication fails, fail the accepted update
  with the relevant reason. Preserve any existing current report from the same
  installation and its capture identity and provenance. Record the failed attempt
  separately; reading the held report
  must not imply that the attempted refresh succeeded or that its facts are fresh.
  Do not merge failed candidate fragments into it. Initial failure leaves no report
  available and does not by itself fail Platform installation or Host startup.
- Once the failed attempt's work has drained, permit an explicit whole-report retry.
  Do not add automatic retry loops or implicitly retry on a current-report read.
  The required fresh capture on each Platform installation/reload is separate from
  retrying a failed attempt within the same installation.

A usable report may explicitly contain unsupported, unavailable or unknown facts
where its schema permits them. A legitimate absence of displays is not itself a
capture failure. Required-query failure, truncation or inconsistent capture must
not be disguised as a successful replacement; retain diagnostic outcomes without
publishing that candidate as the current assumed state. The concrete schema must
identify the facts required for report publication versus permitted limitations.

#### Agreed concrete Host report request/result schema

The user accepted this communication/handshaking schema on 16 November 2026. Use
typed messages to Host over each module's existing authenticated Host channel,
resembling filesystem refresh rather than requiring clients to open a discovery
route directly to Platform. Shared Framework declarations define the report meaning;
Platform owns its capture. The schema-system report representation below remains
a separate proposal and does not change the agreed request/result fields.

| Client message | Payload and behaviour |
| --- | --- |
| `PlatformReportRequest` | No payload fields. Acquire the current report for the installed Platform instance, without starting capture. |
| `PlatformReportUpdateRequest` | No payload fields. Request a whole-report update and complete after publication or failure. Decline while another update is in progress. |
| `AssetReleaseRequest` | Reuse the existing client-authenticated acquisition release for a returned report. |

Use the existing message type and asynchronous reply slot for operation selection
and correlation. There is one master report in this first-pass interface: no report
selector, section mask, native parameters, caller-supplied owner or extra request ID.
The Host-to-Platform capture request is separate producer communication, associated
with the exact installed Platform instance. Clients do not construct that request.

Both report requests return `PlatformReportResult` with the following logical fields:

| Field | Meaning |
| --- | --- |
| `status` | Outcome of this request: `success`, `unavailable`, `not_ready`, `update_in_progress`, `invalid_request`, `failed`, `allocation_failed`, `interrupted` or `delivery_failed`. |
| `availability` | `unavailable`, `not_ready` or `available`, sampled by Host when constructing the result. |
| `update_in_progress` | Whether a report capture/update is currently active. |
| `last_update` | Latest completed capture attempt: `none`, `succeeded` or `failed`; initial/reload capture counts as an attempt. A rejected busy request does not replace this outcome. |
| `failure` | Fixed diagnostic fields: stage (`none`, `capture`, `validation`, `transfer`, `publication` or `lifecycle`), engine-owned reason, and an optional tagged numeric native error. On request failure describe that failure; on a successful read after a failed update expose the retained update failure. No native SDK structures or dynamically owned diagnostic text. |
| `acquisition` | Host-issued `CAssetAcquisitionId`, valid only on success. |
| `bytes` | Immutable `CByteConstView` over the complete validated report storage, valid only while this acquisition is held. Empty on failure. The agreed schema-system encoding below uses a complete baked schema data block. |

Grant the acquisition as part of a successful result, protecting its backing before
reply publication. Do not return a bare asset identity followed by a separate acquire
that could race report replacement or unload. Existing Host asset acquisitions and
release validation are the agreed basis; any needed internal storage extension
still requires ownership/interface review. Keep compact status/diagnostic fields
within the existing 48-byte POD message payload on supported targets; validate
layout during implementation rather than expanding the shared transport.

A successful update returns an acquisition of the report it actually published,
even if another update subsequently runs. An unsuccessful update returns no report
view or acquisition, even when `availability` is `available`; the caller can request
the retained current report separately. An acquisition-allocation failure on a read
does not make the Host's report unavailable. Reply delivery failure does not undo a
successfully published report, and undelivered acquisitions must be reclaimed.

The immutable report contains `kind = platform_report`, report `schema_version = 1`,
platform/backend identification, the producing Platform instance, a Host-issued
capture identity, capture interval with its clock basis, query-profile identification,
query coverage/outcomes and the agreed display, source/desktop-relationship and mode
records. The previously proposed placement directly at the baked-document root is
refined by the schema-system proposal below: runtime envelope fields and typed
application data have separate locations. The report schema version is distinct
from its storage-format versions and the combined Rendering audit's schema.
Instance/capture identities are runtime provenance, not persistent device identity.

Keep current availability, update activity and failed-attempt diagnostics in the
result rather than rewriting an immutable report after failure or unload. A new
Platform instance starts with no current report and no completed attempt; old
reports retain their original provenance. Define required-query coverage with the
native query set before implementation. This report does not absorb Vulkan device
queries or Rendering's baseline comparison and PSO policy.

#### Report packaging and superseded provisional representation

The user favoured using the existing schema system and requested discussion of the
report form before settling native Windows queries and publication requirements.
On 16 November 2026 the user explicitly kept the report layout provisional while
settling schema support for text and typed references to variable-length data.
The concrete version-1 schema below was subsequently accepted on 16 November 2026.
The earlier report-managed text pool/range sketch is superseded by the agreed
schema-understood text and span representation. These design agreements do not
authorise implementation; communication and lifecycle contracts remain unchanged.

Use one immutable, self-contained `schema::CBakedDataBlock` with contained shared
type definitions, a singleton header instance and named bulk record collections.
The returned byte view spans the complete block, including all binary payloads,
not just its leading baked document. Schema definitions belong to the shared
Framework report contract; Platform fills those definitions with observations.
Do not synthesise different type layouts for different display counts or outcomes.

| Schema content | Purpose |
| --- | --- |
| `types` | Versioned named structures, enums, flags and supporting value types for the shared report contract. |
| `instances` | One `PlatformReportHeader` holding report kind/version, platform identification, Platform instance, capture identity/interval/clock basis and query profile. No specialisation hierarchy is needed. |
| `data` | Named bulk collections for displays, desktop sources, source/display paths, modes and scoped query outcomes. Record counts vary without changing definitions; the concrete proposal below represents known-empty results by empty spans and omits zero-count bulk entries. |
| Text storage | Reuse the underlying document's interned string values and agreed retained-text metadata rather than a report-specific byte collection or separate text payload. |

The existing common runtime root uses `fileType: morphic-schema-data`,
`version: 2`, `stripped: true`, sections and payload lengths. The reference extension
changes that contract. Under the subsequently formalised pre-release policy,
temporary development versions are allowed, but the completed task resets the
affected schema data-block marker to version 1 with coordinated rebuilds and
replacement of legacy data. Do not add report kind,
capture metadata or other custom root fields that its existing validator rejects.
Report schema version 1 lives in the typed header and remains separate from both
the schema data-block version and the underlying baked-document container version.
Include the types in each published block for independent validation and inspection;
do not require a separate schema asset or retain a Platform-owned types object.

Use fixed-width numeric fields, enums, rational refresh values and explicitly tagged
native identity evidence. Cross-record references use capture-local typed IDs or
validated ranges, never native pointers or implicit schema instance inheritance.
Represent unknown/not-applicable values explicitly with field validity/status and
query outcomes; zero or false alone must not stand for missing evidence. Text is
UTF-8 data rather than a C++ string object embedded in binary records. Bounds-check
all text ranges and references. Exact field layouts follow the already agreed fact
coverage and the native-query decisions; no new fixed display/mode count is implied.

Platform builds private candidate data with the existing schema construction
facilities and produces a complete runtime block before handoff. `CSchema::bake`
provides contained-type/value validation and runtime construction; generic
`CSchema::save` alone performs only structural authoring admission and is not a
substitute. Existing live-role construction plus explicit common-block assembly is
also available if needed for capture, without changing the published representation.

Host establishes independent backing and validates storage, expected report version
and shared type contract, typed data and report relationships before publication.
A self-consistent embedded schema alone does not prove conformance to the known
Platform report contract. Schema validation also does not establish native query
completeness or atomic capture; required query outcomes follow the separately agreed
publication profile below. Preserve the agreed failure/adoption and unload/reload rules.

Consumers use shared schema/report readers under their existing acquisition; role
attachment does not acquire backing by itself. Existing schema facilities can
reconstruct supported typed values for JSON inspection. A manual text pool would
reconstruct bytes and numeric ranges, not logical strings; that interpretation
would still require report-specific code. Native text support must close that gap.
The refined proposal below retains document string values instead of a second pool.
Dumping only the stripped runtime description would show
locators/selections rather than the observed binary values, so inspection export
must explicitly reconstruct the data and include its types.
This does not require the raw-UTF-8 `SchemaPrepareRequest` conditioning service or
change the agreed public request/result into a prepared-schema descriptor interface.

References: [schema runtime construction](../schema/runtime-api.md#aggregate-authoring),
[common runtime block](../schema/common-binary-block.md),
[typed persistence and reconstruction](../schema/persistence-and-aggregation.md).

#### Agreed schema semantics for text and typed variable-length references

The user accepted these semantics on 16 November 2026. They are design requirements,
prompted by the report rather than an implementation plan. Declaration syntax,
physical representation and version handling have separate agreements below. Work remains
confined to this focused design document; changing the schema subsystem requires
separately selected implementation scope and review.

Provide the schema-understood `text` and `span<T>` value categories; the latter
notation illustrates the agreed span descriptor's element type:

- `text` is counted UTF-8 content. Its logical authoring/inspection value is a
  string; the schema system validates UTF-8, stores its bytes and reconstructs a
  string without report-specific pool interpretation. Do not require a trailing
  zero or include one in the logical length. Preserve exact content without case
  folding or Unicode normalisation. Counted embedded U+0000 is representable;
  conversion to a native zero-terminated API remains an explicit caller operation.
- `span<T>` is a typed range in a named bulk collection in the same owning schema
  block. `T` is part of the declaration and must match the target collection's
  element contract. The logical reference identifies that collection and its
  first element/count; it does not expose a raw byte offset or native pointer.
  It can describe a display's modes, path indices or arbitrary bytes via a byte
  element type. It is a range reference, not a device/object identity.

Both categories have a fixed-size physical descriptor, with variable content
backed by the owning block. Exact descriptor fields, pool arrangement and integer
widths are not selected here. Text backing is managed by the schema system;
typed ranges target explicitly declared collections. Target collection identity
must resolve without process addresses and remain valid when the complete block
moves. Shared/read-only overlapping ranges are permitted and do not imply ownership
transfer. Cross-block references, cyclic range graphs and native pointers are
outside the agreed initial extension. Permit structures with text or range
members when their resolved range graph is acyclic; do not limit ranges to scalars.

Make nullability explicit for these categories. A nullable absent value differs
from a present empty string or zero-element range. Absence alone does not mean
unsupported, disconnected or query failure; those remain report-level outcomes.
Omitted instance members retain the existing default/inheritance meaning. Any
explicit selection clearing an inherited nullable value must have an unambiguous
representation without repurposing existing positional null inheritance holes.
The exact declaration/value syntax needs separate agreement.

Schema validation must check target existence and element type, checked range
arithmetic, count/stride/alignment against actual storage, UTF-8 and nullability,
and reject cycles. It must handle legitimate empty ranges and zero-byte element
types with explicit counts. Apply operation-level storage/traversal limits to
referenced content too; a small descriptor must not bypass construction or export
budgets. Report-specific native identity, record-ID relationships and query coverage
still need their own checks.

Treat support as an end-to-end value contract: resolution and layout, construction,
typed borrowed access, defaults/instance selection, validation, persistence,
copy/repacking and inspection/import must agree. Borrowed text/range views remain
valid only under their existing block owner/acquisition. Materialising or copying
selected values into a different owner must copy/rebind referenced content through
schema-aware operations; copying descriptors alone cannot transfer their referents.
Do not silently extend the existing exact byte-copy remapper into such an operation.
Its treatment of reference-containing types must be explicit in the extension design.

Generic text export reconstructs strings. Generic range export preserves explicit
collection references and their ranges, alongside the named collection values;
it must not turn references into untyped byte offsets. Automatically flattening
shared ranges into nested arrays is not needed for the initial contract. Logical
comparison uses text content and typed reference meaning rather than physical pool
offsets; stable application IDs remain separate from collection locations.

Older readers must reject unsupported schema/storage versions or value categories
rather than misinterpret descriptors as ordinary integers. Do not add general
pointers, arbitrary object graphs, unions, Unicode normalisation or cross-asset
lifetime management merely to serve this report. The exact schema work boundary
and sequencing remain to be agreed before freezing the report's physical layout.

#### Agreed declaration syntax and physical reference refinements

The user accepted the refinements on 16 November 2026, including reuse of document
string storage, signed indices/-1 absence, an unsigned span count, removal of the
reserved word and canonical present-empty spans. The declaration/value syntax and
descriptor rules below form the agreed design direction. The exact text-retention
grammar and bake/copy remapping protocol are agreed separately next. This does
not authorise implementation or freeze the complete Platform report layout.

Extend inline type descriptors alongside existing fixed-array
descriptors; leave the existing `{ "element": ..., "count": ... }` spelling intact.
Use a required `kind` for the two new forms and optional `nullable` defaulting to
false. `text` has fixed UTF-8 meaning, so it needs no encoding selector. `span`
requires `element`, naming the physical type of its target bulk collection.

```json
{ "label": { "type": { "kind": "text", "nullable": true } } }
{ "modes": { "type": { "kind": "span", "element": "DisplayMode" } } }
```

These are member descriptor examples, not new top-level categories. Text/span
descriptors are physical value types and may occur inside structures/fixed arrays.
They are not permitted as enum storage or bit-field types. An unknown property,
invalid `nullable` or unresolved/non-storage element declaration rejects. Existing
declaration-cycle restrictions remain; this does not enable recursive type graphs.

Use the following logical value forms, also for reconstruction/inspection:

| Value | Authoring/inspection form |
| --- | --- |
| Present text | A JSON string, including `""` for empty text. |
| Typed collection range | `{ "collection": "modes", "first": 0, "count": 3 }`; with element `DisplayMode`, the target is `data.DisplayMode.modes`. |
| Present empty range without a collection | `{ "empty": true }`. |
| Explicit absence | `{ "absent": true }`, accepted only for a nullable text/span declaration. |

Marker objects accept exactly their one true-valued property. A range requires
all three properties, a nonnegative signed-32-bit first index and an unsigned
32-bit count within the target collection's domain limit. The agreed refinement
validates an explicitly targeted zero-count range, then canonicalises it to the
present-empty form. Retaining a position in a collection with no referenced elements
does not serve the initial report need. Absent/empty
markers are interpreted under the expected text/span type, not as arbitrary
structure values. Existing raw `null` inheritance holes keep their meaning.
Selecting text or a span, including an explicit absent/empty value, selects that
whole field atomically; its descriptor words are not independently inheritable
schema members. Omission continues to inherit/default as already agreed.

Implicit defaults are present-empty for non-nullable fields and absent for nullable
fields. Explicit text defaults may be strings or permitted absence. Span defaults
may be unbound-empty or permitted absence; collection-bound defaults are excluded
because resolving a types catalogue alone has no capture-specific collection.
Complete bulk records still supply their fields explicitly under the existing rule.

Use fixed-width little-endian descriptors with four-byte alignment on every host
architecture; no native `size_t`, pointer or implicit C++ padding is persisted:

| Descriptor | Fields | Size |
| --- | --- | --- |
| Text | `i32 string_value_id`, referring to the owning document's string-value table. | 4 bytes |
| Span | `i32 collection_id`, `i32 first`, `u32 count`. | 12 bytes |

For text, -1 denotes absence, zero denotes the document's canonical empty string,
and positive values identify nonempty strings. Negative values other than -1
reject. This reuses the existing `CStringValueId` domain through checked conversion;
it does not change that domain's public unsigned type or invalid sentinel. Schema
text retains logical content under the data model's existing encoding policy,
including its canonical modified-UTF-8 representation of logical U+0000. Text
access must make that representation clear rather than claiming every borrowed
stored byte sequence is strict UTF-8 with literal zero characters.

Do not add a separate binary text payload or `textPayloadSize`. Preserve needed
document string values while stripping numeric data. A schema-owned retained text
section containing ordinary string nodes is the working retention mechanism,
including strings referenced only from bulk records, inherited values or defaults.
Those nodes share the underlying interned text rather than duplicating its bytes.
The exact retained-section grammar is agreed below. Merely leaving unused
strings in the string table is insufficient: existing generic document validation
requires every nonempty string entry to have a document-value reference, and bake
orders/reassigns IDs. Construction must map text slots to the final owning document's
IDs; promotion, reconstruction and partial copy must remap as necessary. The same
document context supplies text lengths, so binary slots do not repeat them.

Span collection IDs are block-local: zero denotes present-empty and -1 denotes
absence, with first/count zero in either case. Positive signed-32-bit IDs identify
bulk entries; other negative IDs reject. Every bulk entry receives a
unique `collectionId` in its runtime description; its containing type group and
entry name establish the target type and authoring identity. IDs are regenerated
when constructing a block and carry no persistent application identity. A live
block's resolver provides the ID-to-entry binding; readers do not guess entry order
or repeatedly search the whole document. Nonempty spans require a nonnegative
first index and positive count. Validate the target element contract and
`first + count` using checked wider arithmetic, including for zero-byte elements.
The target entry supplies payload location and resolved stride/alignment, so these
are not repeated in every reference. Remove the earlier `reserved_zero`: it was
spare format space, not required state or padding for four-byte alignment.

The framework byte/element ceiling is 0x80000000 inclusive. Its valid element
indices fit the nonnegative i32 range, but a collection count can be 2^31. Retaining
u32 for count preserves that boundary; an all-i32 descriptor would require an
explicit tighter count limit. Keep that distinction visible rather than silently
losing a supported count. The user explicitly accepted the unsigned count.

Use final pre-release schema data-block version 1 for this representation. The
earlier version-3 proposal is superseded as a completion requirement; it may be a
temporary development marker within the implementation task. Retain the generic baked
document container encoding and the existing 2 GiB/128-byte block conventions.
The extended format adds schema-understood retained string content and runtime bulk-entry
`collectionId`; it adds no new text binary section. File order remains baked
document, instance payload, bulk payload. Each nonempty payload starts at 128-byte alignment;
padding between sections is zero, and there is no final alignment tail. All
payload extents are exact. These are schema-container fields, not custom Platform
report root fields. Rebuild affected readers/writers and data against the final
version-1 representation; do not retain a legacy reader or change the unrelated
generic baked-document encoding merely for this schema extension.

The owning document's string table and the collection directory form the schema
block's reference context. Role attachment, materialisation and typed access to reference-bearing
data must bind that context explicitly; an isolated instance/bulk byte span is
insufficient. Complete-block copying preserves relative descriptors. Partial copy,
repacking or export/import must preserve named target meaning and rebuild backing/
IDs as necessary through schema-aware operations. Existing exact-copy remapping
must reject reference-bearing transfers until an explicit supported relocation
contract exists, rather than copying descriptors into an unrelated owner.

Physical-layout code generation may expose matching data-only descriptor types;
dereferencing remains a checked schema/block-view operation under the owner or
acquisition lifetime. This proposal does not define a C++ owning string/vector ABI.
Exact API signatures remain implementation-interface review items; the subsequent
agreements below govern mutation, retirement, limits, execution and version handling.

#### Agreed retained-text document section and bake/copy remapping

The user accepted this retention/remapping protocol on 16 November 2026. Add a schema-owned
runtime root field `retainedText` in the extended schema data block (final pre-release
version 1). It is an array of
ordinary nonempty string values, containing each distinct text referenced by the
materialised instance/bulk binaries once, in the existing document string lexical
order. Require the array even when empty. It contains no numeric string IDs, and
its array indices are not the IDs stored in text fields.

```json
"retainedText": ["Display A", "Display B"]
```

The strings share the document's existing interned string-value storage, including
strings also occurring in definitions or other retained content. There is no second
copy of their bytes. Ordinary document references keep those strings reachable for
generic baking and validation. Empty text already has canonical ID zero; absence
has no string value, so neither requires an array entry. Reject duplicate, empty or
non-string entries in a completed runtime block. Final construction regenerates
this section from effective typed payload values, including inherited/defaulted
text, rather than copying an old list or scanning only authored selections.

`retainedText` is generated storage metadata, not a new authoring `CSchema` section
or a user-maintained string catalogue. Authoring and inspection/import still use
strings in their logical fields. Defaults remaining only in the type definitions
already have ordinary document references; they need a retained entry as well only
when used by a materialised binary text field. All positive binary text IDs must
resolve in the owning string-value domain and name text represented in this array.
Other ordinary string values, such as type names, are not text-field targets merely
because their IDs are valid. Extra retained entries not needed by any binary text
field are excluded from newly constructed blocks; edits/compaction policy must
preserve live references until their transaction commits.

Use an explicit staged bake protocol:

1. Resolve the effective values and capture each text value together with its source
   document context. Intern its logical content into the private destination
   description, build `retainedText`, and retain a mapping to the destination's live
   string ID. Values from multiple documents use separate source-ID domains.
2. Finish the complete runtime description, including types/defaults, locators,
   retained text and metadata, then freeze it for baking. Any later content change
   invalidates this bake's ID map.
3. Bake the description and obtain its exact live-string-ID to baked-string-ID map.
   The generic baker already creates that map internally; expose an optional,
   operation-owned string-value remap output without adding schema callbacks or
   teaching the data model to scan schema binaries. Publish document and remap
   together on success; failure leaves both caller outputs unchanged.
4. Encode binary text fields using the mapped final IDs. If source payload bytes
   were staged earlier, rewrite their schema-identified text slots in private
   destination storage. Preserve -1 absence and zero empty directly; never scan
   arbitrary integers looking for potential IDs. Assemble and validate the entire
   block, including typed text references, before publishing it.

The map covers actual retained string values only. Missing, out-of-range or
unrepresentable positive IDs are failures, never substitutions with empty/absent.
The mapping and any source-context translation are temporary operation state,
bounded with the existing construction work/storage budgets and discarded after
completion. Do not persist a source-to-destination map or patch the document's
interned-string table after baking. The completed binary always refers to its own
document, never to the source authoring aggregate or a different block.

Complete byte-for-byte block copying can preserve IDs because it preserves the
exact owning document. Schema-aware subset copy, composition, reconstruction or
repacking must read text through each source's owning document, intern it into the
destination, and apply the destination's final bake map. Deduplicate equal content
across sources, not equal numeric IDs. Copy the necessary text for the selected
effective values, not every source's historical retained list; sources remain
unchanged. Whole-document promotion currently preserves string IDs by preloading
them, but that optimisation must be established for the actual operation rather
than assumed for partial copies. Any document transformation that changes IDs
requires corresponding payload remapping before the result is usable.

Existing generic document copying continues to remap ordinary string nodes only;
it does not silently relocate integer slots inside opaque schema binaries. A
reference-bearing role therefore cannot be saved/copied independently of its
required document context through a generic raw-copy path. Logical JSON export
resolves text fields back to strings; import rebuilds the retained section and IDs
instead of accepting runtime IDs as portable text values. Retention and remapping
errors preserve the source and never publish an incomplete destination.

References: [existing string-table and bake rules](../data_model/baked_document_format.md#string-tables),
[document baker](../../foundation/data_model/document_baking.cpp),
[document string copying](../../foundation/data_model/document_copy.hpp).

#### Agreed first-pass span target retention and copy/repacking relocation

The user accepted this target-retention and relocation policy for the first pass
on 16 November 2026. Complete-target retention is not certain to be the final end
state. Revisit it with actual consumer needs and size/performance evidence before
selecting any element-compaction or alternative retention policy; the initial
extension does not commit the schema system permanently to whole collections.

A nonempty
span retains a dependency on its target bulk collection within the same owning
schema block. The collection remains an ordinary named entry under `data`; do not
add a second `retainedTargets` catalogue. The block owns the directory and complete
backing, so a returned range view needs the block's existing acquisition rather
than an independent acquisition for every target. No referenced bytes remain
borrowed from a different source block after a copy completes.

For the initial extension, copy a referenced target collection in full. Begin with
the operation's selected instances/collections and required instance context, then
follow effective nonempty spans transitively. Include every referenced collection,
and inspect all records of each included collection for further spans and text.
Repeated references to one source collection retain one destination copy; shared
and overlapping ranges preserve their relationship. Detect cycles and apply
operation-wide record/storage/traversal limits to the complete retained dependency
set, including zero-byte record types. A limit failure rejects the operation; it
does not truncate a collection or silently omit a dependency.

Retain element order and counts inside each copied collection. Initial repacking
may remove unused allocations/collections and move each retained collection's
binary storage, with the required alignment and new locators. It does not delete,
reorder or deduplicate individual records inside a retained collection. Consequently
span first/count values remain unchanged even when physical byte offsets change.
Element filtering/reordering needs a separately agreed relocation contract; if an
operation cannot keep a referenced range contiguous, it must reject rather than
split one span or redirect it to different elements.

Plan relocation in private staging before publication:

1. Validate each source block's schema/reference context, then inventory the selected
   roots and complete target dependency set. Identify a source collection by both
   its source block and its collection ID, never the numeric ID alone.
2. Establish destination named entries and compatible types, assign fresh positive
   destination collection IDs, and record source-to-destination collection mappings.
   Distinct source collections remain distinct even if their bytes are equal.
3. Plan payload offsets/alignment, copy the retained collections and effective
   instance data, and rewrite schema-identified span descriptors to the mapped
   destination collection ID. Preserve first/count for nonempty spans, and preserve
   canonical absent/empty descriptors without target lookup. Remap embedded text
   with the agreed retained-text and final document-bake protocol.
4. Assemble the new document, directory and payloads. Check every target/type/range,
   retained dependency, absence/empty encoding and cycle restriction against the
   completed destination before publishing it. Missing or invalid mappings fail;
   never resolve them through a same-named unrelated collection or empty fallback.

For example, source `(collection=17, first=2, count=4)` becomes
`(collection=3, first=2, count=4)` when that same collection is copied under new ID 3.
Its physical binary offset may change independently. A second overlapping source
span uses that same destination ID and its own unchanged first/count.

Within the destination, collection names remain unique in each element-type group.
A relocation copy encountering distinct same-type/same-name source collections
must report a naming conflict unless the caller explicitly supplies a destination
rename/mapping. Do not silently merge them or infer shared identity from names.
This relocation rule does not replace existing authoring schema merge semantics;
semantic merging and dependency-preserving runtime copying are separate operations.
Logical export reconstructs the destination type/name and first/count, rather than
persisting transient numeric collection IDs as authoring references.

Whole-block byte-for-byte copying is the special case that can preserve the exact
directory, IDs and all first/count values. Other copy/repacking operations construct
an independent destination, preserve source blocks and acquired views, and retain
their source protections until all required bytes and metadata have been consumed.
The temporary relocation map ends with the operation. A live edit cannot remove
or shorten a target below a retained span's range without updating/removing those
spans in the same validated transaction. Published reports remain immutable.

This first-pass policy may retain unused records within a needed collection. Accept that
explicit cost for the first extension; bounded complete-collection retention avoids
adding element-compaction and range-splitting policy before the Platform report
prerequisite. Copy/mutation APIs and implementation limits are agreed below.

#### Agreed reference-aware copy/mutation APIs and operation limits

The user accepted these APIs and limits on 16 November 2026. Names illustrate the
intended public surface; final C++ parameter spelling/placement follows the existing schema APIs.
Use Foundation/schema operations with explicit source reference contexts. Do not
require JSON serialisation to copy a runtime block or create a new Host messaging
protocol for these local operations.

Provide a checked, borrowed `CDataBlockView` binding the document, resolved types,
instance/bulk roles and reference directory. It grants no ownership and is valid
only under the caller's source owner/acquisition. Establishing or revalidating this
context consumes the operation's limits. Bare payload bytes are insufficient for
reference-bearing inputs.

| Operation | Contract |
| --- | --- |
| `copy_data(sources, roots, name_map, limits, output, diagnostic)` | Copy selected named instances/collections and their agreed complete-target dependency set into an independent empty `CBakedDataBlock`. Root selection includes necessary instance hierarchy/context. Multi-source mappings identify source context as well as entry; naming conflicts require explicit mappings. |
| `repack_data(source, limits, output, diagnostic)` | Preserve all named entries, instance selections and record ordering; reconstruct compact backing and remap references. Do not guess which named collections the caller considers disposable. Explicit subset copying is the path for dropping entries. |
| `CDataBlockEdit::begin(source, limits, diagnostic)` | Create a private reference-aware editing candidate from one source block. Source remains unchanged and protected until its required input has been consumed. |
| `CDataBlockEdit::begin_empty(types, limits, diagnostic)` | Begin private construction using a validated shared types catalogue, without requiring a pre-existing runtime block. |
| Edit instance/field operations | Create instances and write logical typed values through paths, reusing existing member/fixed-array path steps and explicit instance descendant policies. Accept text strings and logical span values, not caller-invented binary IDs. Provide explicit absent/empty setters or equivalent typed values. |
| Edit collection operations | Create/replace complete typed record collections; rename or erase entries. Use logical record input or a bound source context for runtime records. No implicit type conversion or descriptor-only capture from arbitrary bytes. |
| `commit(output, diagnostic)` | Validate and bake the whole candidate, rebuild retained text/collection mappings and publish into an empty output only on success. |
| `cancel()` | Discard private candidate state and release its input protections; never publish. Destruction also discards private storage and never commits implicitly. |

Copy and repack may share the edit transaction's internal planning/relocation
machinery, without forcing the caller through a public edit session. Preserve
unchanged runtime record bytes, padding and instance selections except the text/span
reference slots explicitly relocated. Reconstructing all values as literals would
lose support for raw encodings that the existing schema import cannot represent.
Logical writes change only selected values under the existing selection/descendant
policy; they must not silently normalise unrelated records.

Use a single private candidate per edit object, with no concurrent mutation and
no global transaction manager. Each accepted command copies its logical inputs
before returning or retains an explicit bound input protection. Reject locally
invalid types, paths, encodings or stale/foreign candidate handles. A failed command
leaves the prior candidate state intact, while consumed work budget is not refunded.
Relationships may be temporarily incomplete within private construction, allowing
forward targets or coordinated replacement/removal; commit requires the complete
candidate to be valid. Rename keeps references attached to that entry rather than
retargeting them by the new spelling. Removing/shrinking a target requires all
affected spans to be corrected in the same transaction before commit.

Commit failure leaves the source/output unchanged and the candidate available for
correction if its budget permits. Successful commit ends the edit session; callers
reacquire handles/views from the completed result. Candidate handles carry their
owner/generation and are not identities in the output. Existing readers of source
blocks remain valid; installing a replacement in Host still follows its independent
publication/acquisition lifecycle. No borrowed writable view of candidate reference
descriptors is part of this initial API.

Existing reference-free role APIs retain their behaviour. For reference-bearing
values, unbound raw capture, `mutable_entry`, direct instance writable views and
raw byte-copy remapping must reject or use the new bound operation explicitly.
Do not silently infer a source document or add relocation to the existing opaque
byte executor. Ordinary `CSchema` logical authoring remains supported; its extended
construction paths use the same bounded text/reference validation and backing rules.

Agreed initial defaults for one `SDataOperationLimits`, shared by copy, repack and
the entire edit session (begin, every command and commit):

| Limit | Default |
| --- | --- |
| Aggregate inspected source-block bytes, each source counted once | 64 MiB |
| Peak operation-owned storage, including staging, output, scratch, maps and resolved/index data | 64 MiB |
| Complete output block, including document and all payloads | 16 MiB |
| Document-node admissions across sources and candidate construction | 262,144 |
| Retained records/instance snapshots, including zero-byte records | 262,144 |
| Total work credits across all participating passes/commands | 4,194,304 |
| Type/value/reference traversal depth | 256 |

These are per-operation first-pass defaults, not new format/index limits or a
maximum report/device/mode count. Trusted callers may explicitly supply other
finite limits up to the existing hard format/allocator limits; external service
policy may impose lower ceilings. New APIs require explicit limits or select these
named defaults, never an implicit unlimited allowance. Existing unrelated schema
API overloads and Host preparation-service limits are not changed by this agreement.

Charge source/context validation, document nodes, record/member/reference visits,
cycle/closure walks and relocation/map work to one operation-owned work allowance;
use a conservative bounded charge for byte scanning/copying (one credit per started
64-byte chunk). Participating value writers share the operation's allowance rather
than resetting a budget per collection. Count every attempted record/element visit,
including zero-sized values. Check size/count products and sums in wider arithmetic
before allocation or narrowing. Source bytes are bounded even when borrowed; their
existing allocation is excluded from operation-owned memory, while newly retained
scratch/copies count toward the peak. Avoid rescanning a shared target needlessly,
and charge repeated actual work when another pass is required.

No retry inside an edit session resets its work counter. Report limit exhaustion
separately from allocation failure, with stage, limit kind and copied entry/path
detail where available; diagnostics never require allocating or retaining handles
into discarded staging. Limit or validation failure never yields a partial usable
output. Storage/work caps are not wall-clock guarantees or a pre-emption mechanism.
Execution starts with local synchronous schema operations as agreed below. Measure
their actual cost against the owner-pump budget; the operation caps do not prove
that a complete large construction is suitable for an owner-pump invocation.

#### Agreed local synchronous execution and measurement-led offloading

On 16 November 2026 the user agreed local synchronous schema operations as the
starting point, with offloading justified by measurements. No report-specific timing
measurements establish a conditioning-thread requirement. The agreed operation
limits are defensive ceilings, not estimates of normal report size or processing
cost. The earlier worker/resumable-engine proposal was not agreed and is not
a first-pass implementation prerequisite.

Use caller-executed synchronous schema operations with private staging
and atomic commit, using the agreed bounded data/work limits. Keep scheduling out
of the schema value/storage contract. Do not require a new cross-module schema-job
protocol, resumable implementation of every algorithm, fixed worker quanta or a
five-second schema deadline for this report without an established need. The report
schema and existing Platform-to-Host publication still need their agreed shared
contract regardless of execution placement.

Measure representative and deliberately large report construction, schema/type
validation, reference relocation, baking, Host admission checks and retirement,
including cold initial construction. Compare owner-thread work with the existing
pump budget before deciding whether the complete operation can execute there.
Infrequent startup/explicit refresh does not by itself prove that an owner-pump
stall is acceptable. If measured cost requires it, select bounded local phases or
the existing nominated worker path for the specific expensive stage, with the
minimum required ownership/message extension. Native Windows query latency is
a separate placement concern and must not be attributed to schema processing.

The schema prerequisite therefore needs timing evidence for the chosen report
path before any offloading decision is justified. Existing source/acquisition
safety and publication rules remain applicable whichever placement is selected.

The earlier conditioning-worker/resumable-engine proposal is superseded for the
first pass. It creates no required worker protocol, step engine, fixed schema
quanta or schema-specific five-second deadline. The agreed local synchronous
placement and measurement-led reassessment above govern implementation.

#### Agreed native Windows query profile and publication requirements

The user accepted this native query profile/publication policy on 16 November 2026.
All native calls,
SDK types, COM interfaces and Windows-to-portable translation remain inside Platform.
The report describes the calling process/session's observable state and identifies
query scope and capture interval. It does not claim unrestricted machine-wide
visibility. Rendering still performs Vulkan-specific queries independently.

| Information | Native queries and coverage |
| --- | --- |
| Current topology and discoverable inactive targets | `GetDisplayConfigBufferSizes` followed by `QueryDisplayConfig(QDC_ALL_PATHS)`. Deduplicate targets by native adapter LUID/target ID, retain API-reported availability separately from path activity, and include inactive available targets. Retain actual active source/target relationships, not every possible inactive source/target pairing as a desktop relationship. |
| Current source/signal configuration | Decode the returned source, target and applicable desktop-image mode records, with checked mode indices/kinds and identity matches. Capture position, desktop/signal extents, pixel format, rotation, scaling, scan order and rational refresh as actually supplied. Inactive paths do not acquire invented current modes. |
| Virtual-mode/refresh interpretation | Request `QDC_VIRTUAL_MODE_AWARE` and `QDC_VIRTUAL_REFRESH_RATE_AWARE` where accepted. Use finite fallback when newer flags are rejected. A malformed initial virtual-aware CCD inventory may restart once with base `QDC_ALL_PATHS`, retaining the rejected query and its diagnostic; discard derived topology and validate the fresh inventory and final core check. Record the actual profile and reduced coverage. Keep reported virtual/physical refresh distinct without claiming VRR ranges. |
| Names and native identity evidence | `DisplayConfigGetDeviceInfo` with source-name, target-name and adapter-name packets. Preserve GDI source names, monitor device paths and tagged adapter identity evidence; friendly names are labels and may be unavailable. Connector technology comes from valid path/device information. |
| Session desktop/primary information | `EnumDisplayDevicesW` for adapter/source and monitor records; use `EDD_GET_DEVICE_INTERFACE_NAME` for monitor interface evidence. Read primary/desktop participation flags and correlate with CCD identities/source names where possible. Do not promote remembered or unmatched entries to confirmed connected displays. |
| Format-qualified mode lists (initial profile; revised scope below) | A fresh `CreateDXGIFactory1`, `EnumAdapters1`/adapter descriptions, `EnumOutputs`/output descriptions and `IDXGIOutput1::GetDisplayModeList1`. The initial implementation queries `R8G8B8A8_UNORM` and `B8G8R8A8_UNORM`, including interlaced/scaled modes through their enumeration flags, without requesting stereo. The [scope and condensation decision](#platform-report-scope-and-condensation) and agreed compact layout below replace this with one RGBA8-filtered catalogue per output; implementation of that revision is outstanding. |
| Current desktop depth and restricted fallback | `EnumDisplaySettingsExW(ENUM_CURRENT_SETTINGS)` for valid `dmFields`, and sequential indexed enumeration only when the relevant DXGI mode query is unavailable/unsupported/failed. Use flags zero: no raw driver modes and no all-orientation expansion. Record integer refresh precision and source scope; never use registry settings as current state. |

Use a new DXGI factory per capture so an earlier factory's adapter inventory is not
mistaken for refreshed discovery. DXGI is used for native output/mode information,
without creating a Direct3D device or swapchain. Its two initial UNORM queries are
display-mode filters; Rendering's four Vulkan colour-format capability queries
remain separate. A successful zero-mode DXGI result is valid for its declared
format/filter and does not by itself trigger fallback or mean the display has no
other modes. GDI fallback outcomes retain the original DXGI outcome as well.

Keep modes at their evidenced output/source scope if cloning or incomplete identity
prevents a unique physical-target association. Names or enumeration positions must
not establish identity. Convert native text through existing checked text handling;
preserve unknown enum/raw identity evidence with tags instead of exposing SDK
structures. No window, mode switch, EDID blob parsing, HDR/VRR detail or device
creation is required for this profile. HMD/specialised-display discovery is outside
the initial desktop-display scope and must be labelled as such.

`QDC_ALL_PATHS` is selected to discover available inactive targets,
but Microsoft documents that it can be expensive. This native cost is independent
of schema processing and must be measured when settling capture execution. Do not
describe all reported possible paths as active connections or all possible targets
as physically attached monitors. A path may validly remain active while its target
is reported unavailable during a removal transition; retain both observations.

Agreed publication rules:

| Outcome | Publication decision |
| --- | --- |
| Complete, internally consistent CCD inventory/configuration | Required for a normal discovery report. Native source/target identities and available active-mode records must translate into valid portable relationships. Successful zero discovered displays is valid. |
| Explicitly documented core-query limitation, such as `ERROR_NOT_SUPPORTED` or `ERROR_ACCESS_DENIED` | Permit a limitation report with display discovery marked unsupported/unavailable and its native reason. Do not claim an empty display inventory, restore old facts or describe the core query as successful. This is a published report about limited visibility, distinct from Platform itself being absent. |
| Missing friendly names, adapter paths, primary annotation, DXGI correlation or mode-list query results | Permit publication with explicit unknown/unsupported/unavailable/failed outcomes at the affected scope. These queries are attempted when applicable; dependent queries blocked by missing inputs are recorded as such. Their success is not a prerequisite for the core inventory. |
| Unexpected core-query failure, malformed/inconsistent required data or unstable core topology after bounded retry | Fail the update and preserve the previous current report from this Platform installation. A failed query is not a successful zero-display result. |
| Allocation/global operation-limit failure, failed schema/reference validation, incomplete handoff or obsolete Platform instance | Fail publication under the already agreed lifecycle/failure rules. Never present a truncated list as a complete capture. |

For an inventory-bearing report, read and validate a final core topology/configuration
snapshot before publication.
If its relevant native identity/activity/current-configuration facts differ from
the initial snapshot, discard the mixed attempt and retry within the capture's
finite allowance. Use only mode-index references from their own CCD query result.
Count-and-fetch buffer races (`ERROR_INSUFFICIENT_BUFFER`, `DXGI_ERROR_MORE_DATA`)
also use bounded retries and fresh sizes; do not grow/retry indefinitely. Exact
native retry/count/time caps and capture placement are agreed below. A final core
check detects some races but does not make the
different API queries an atomic snapshot or prove the mode catalogue unchanged.

Every declared query scope must have a recorded outcome, actual flags/filters and
source API. Store retrieved facts only at their supported precision and scope.
Optional-query failures do not authorise borrowing previous report values into
the candidate. The combined Rendering baseline keeps its independent comparison/
adoption requirements; publishing a valid Platform limitation report does not
establish that a saved combined audit is compatible or should be replaced.

Sources: [CCD query semantics and limitations](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig),
[buffer sizing races](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdisplayconfigbuffersizes),
[native device information](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-displayconfiggetdeviceinfo),
[target availability](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_path_target_info),
[session display enumeration](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaydevicesw),
[DXGI factory refresh](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-enumadapters),
[format-qualified mode enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-getdisplaymodelist1),
[mode flags](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-enum-modes),
[GDI current settings and filters](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw).

#### Agreed native retry/capacity defaults and initial capture placement

The user accepted these first-pass limits and initial execution placement on
16 November 2026. Limits
are internal trusted configuration, recorded in capture diagnostics/coverage rather
than added as native-operation parameters to public report requests. One capture
is active per installed Platform instance; overlapping update requests are declined
under the agreed admission rule. There is no queued retry or periodic re-audit.

| Allowance | Initial limit |
| --- | --- |
| Complete capture attempts, including attempts discarded by the final topology/configuration check | 3 total, not three retries after an initial attempt. |
| Count/allocate/fetch passes for one CCD snapshot or one DXGI output/format list | 3 total per selected flag profile/query scope. Retry only documented buffer-growth races. |
| CCD compatibility profiles | At most the three fixed profiles: virtual-mode plus virtual-refresh awareness; virtual-mode awareness only; base `QDC_ALL_PATHS`. Advance when newer flags are rejected. For `invalid_data` during initial virtual-aware CCD fetch or translation, permit one fresh base-profile inventory, retaining the original failed outcome and field/value diagnostic. Clear all topology derived from the rejected inventory. Keep cumulative native-call accounting and the original eligible-time budget. Base-profile failure remains terminal. Reuse the accepted profile for the attempt's final core check; a malformed final check does not enter this retry. |
| Native adapter enumeration | 64 entries per enumeration backend. |
| Recorded desktop sources, recorded active/available targets, DXGI outputs and GDI monitor/interface records | 256 each across the candidate. |
| Raw CCD path / mode arrays | 8,192 paths and 16,384 mode records per snapshot, checked before allocation. |
| Mode-list records | 4,096 per DXGI output/format or GDI source list; 32,768 aggregate rows in the candidate, before deduplication. |
| Cumulative native query/enumeration calls | 65,536 across the entire capture, including failed calls, compatibility attempts, retries and final checks. |
| Peak capture-owned storage | 64 MiB, including native buffers, translated observations, schema staging/output and scratch; the existing schema-operation sublimits also apply. |
| Complete published report | 16 MiB, including its document and payloads. |
| Capture deadline | 5 seconds of eligible awake time from capture admission through publication/handoff, shared by all phases, retries and backpressure. |

These ceilings are independent and the first reached governs. They are defensive
first-pass defaults, not guaranteed operating-system maxima, expected report size
or permanent limits of the schema system. Previously published/acquired reports
are protected separately and cannot be evicted to force a new capture to fit.
Count capacities from returned/requested native counts before allocating or
deduplicating. At an indexed-enumeration boundary, permit the bounded termination
probe needed to distinguish an exact-size inventory from a further record; do not
retain that extra record or silently truncate. Check arithmetic before narrowing.

For CCD/DXGI insufficient-buffer/more-data results, discard the incomplete query
buffer, obtain fresh sizes and retry within the stated allowance. Do not combine
prefixes from different attempts. Recreate the DXGI factory for a new complete
capture attempt; a count/fetch retry within the same attempt can retain its output
context while it remains valid. Retry complete capture only for observed core
topology/configuration change or a recognised capture race, not every native error.
No sleep/backoff loop runs inside a native callback; retries resume at another
outer-pump opportunity. None resets the cumulative call count or deadline.

An optional mode query exhausting its buffer-race allowance records failure and
can use the agreed GDI fallback; the core inventory may still publish with explicit
mode limitations. Exhausted core capture attempts fail the update. Any configured
capacity/global work/storage/deadline exhaustion fails the complete update, even
if encountered in enrichment; it must not be hidden as a successful partial list
or converted into a limitation-only report. Known API unsupported/session-denied
outcomes retain their separate agreed limitation-report handling.

Return the existing request failure with stage/reason such as retry-exhausted,
limit-exceeded or deadline-exceeded, plus the applicable query scope/counts in
diagnostics. Preserve the prior current report and its capture identity on failure;
initial failure leaves no current report. A later explicit request may try again
once accepted work and temporary resources have drained. It receives a new budget;
there is no automatic chain of fresh-budget retries.

The placement and baking assumptions below describe the initial implementation.
The [18 November Host schema service decision](#host-schema-services-placement-decision)
supersedes local Platform baking; native execution and the revised handoff remain
to be settled.

Start with capture owned and executed by Platform on its existing Host-pumped
owner thread, outside native callbacks, restricted nested maintenance and the
module-load/activation handshake. Initial capture is scheduled after activation;
refresh is an outer-pump operation. Use a small capture state machine for core
query, enrichment, final core check, local schema construction and Host handoff.
Keep the native SDK/COM objects and staging private to Platform, and keep schema
calls local and synchronous. Do not create a conditioning-thread schema request
protocol or a new Platform service thread as the default implementation.

Between native calls, yield through the normal pump and allow window/input/close/
shutdown work to progress. Initially allow at most one native query/enumeration
call per outer-pump invocation, with a cooperative 500-microsecond share of the
remaining pump budget for capture bookkeeping/translation between calls. Do not
chain a complete enumeration or all retries inside one invocation. Source-to-report
schema construction and Host validation/adoption remain separately timed phases;
their synchronous calls are not falsely presented as resumable schema algorithms.

The existing pump target is not a guaranteed upper bound on a synchronous Windows
query, allocator call or complete schema phase. The APIs provide no caller timeout
argument for forcibly interrupting an entered query. Native call time counts toward
the capture deadline; only existing external Host restrictions/system sleep pause
eligible time. Check deadline, stop/unload state and capture identity before each
new phase/call and immediately after it returns. An over-deadline call's result
cannot publish, but its return is still required before its state can be reclaimed.
Do not detach/kill the thread or unload Platform beneath it.

Measure cold and refreshed capture duration, maximum individual query time,
schema build/adoption/retirement time and actual owner-pump stalls with a live
window/input session. Owner-thread placement is a starting implementation to be
qualified, not proof that `QDC_ALL_PATHS` meets the pump budget. If measurements show
unacceptable stalls, revise the particular expensive native stage to a Platform-
owned background execution path, retaining its session/desktop scope and exact
module lifetime. Discuss/review that concrete placement change; do not move native
queries into Rendering or execute Platform callbacks on a Host conditioning worker.
The schema APIs and shared report request/result contract need not change with
that placement decision.

Host publication checks the current Platform instance/capture and closed admission
again after handoff. Cancellation/unload prevents subsequent calls/publication;
already entered synchronous calls finish before cleanup. A client losing interest
does not itself own or invalidate Host's maintenance capture. Window close alone
does not unload Platform or invalidate this window-independent report. Capacity
exhaustion and timeout never bypass the existing ownership/quiescence rules.

API bases for the finite growth retries: [CCD buffer changes](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
and [DXGI mode-list changes](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-getdisplaymodelist1).
The numerical allowances and initial execution placement above are agreed engine policy,
not promises or limits supplied by Windows.

#### Agreed Platform-to-Host capture, handoff and publication path

The user accepted this producer-side capture/handoff/publication protocol on
16 November 2026. The [18 November Host schema service decision](#host-schema-services-placement-decision)
requires revision of the already-baked candidate handoff below; its exact replacement
is not yet settled. Reuse existing authenticated
Host/client POD delivery to Platform's pumped inbox and Platform's `post_pod` /
`post_owner` path back to Host. Add the specific shared Framework message types and
Host/Platform handlers needed for this report; do not create a Rendering-owned
route, a generic remote schema editor or an arbitrary native-query command channel.
No synchronous cross-module call/wait is required.

Host starts the initial capture after Platform activation is ready, and uses the
same path for an accepted explicit update. Reserve the Host capture/publication
record, fixed outcome storage, expected producer correlation and any public reply/
acquisition reservation before dispatch. Host issues a nonzero capture ID that is
not reused in its lifetime and associates it with the exact `SModuleInstance`.
Keep the requesting client/async slot only in Host's record; it does not become
the capture's owner and is not forwarded to Platform as an authority.

| Internal message | Direction and payload |
| --- | --- |
| `PlatformReportCaptureRequest` | Host to Platform, POD: Platform instance, capture ID and a restriction-generation/eligible-deadline anchor. No window, requester, file path, native flags or section mask. |
| `PlatformReportCaptured` | Platform to Host, owning message: Platform instance, capture ID and `CBakedDocumentBlock` storage spanning the complete frozen schema data block. This offers a candidate, not a published report. |
| `PlatformReportCaptureFailed` | Platform to Host, POD: Platform instance, capture ID and the existing fixed portable failure detail. Requires no report allocation and carries no usable byte view. |
| `PlatformReportCaptureCancel` | Host to Platform, POD: instance/capture ID and portable cancellation reason. Stops further phases for that capture; it cannot interrupt an entered native call. |
| `PlatformReportCaptureReceipt` | Host to Platform, POD: instance/capture ID, disposition (`published`, `rejected` or `interrupted`) and fixed failure detail where needed. It settles the transferred candidate/outcome and requires no receipt-of-receipt message. |

Keep POD messages within the existing transport's 48-byte payload. The deadline
anchor identifies the same Host restriction-aware eligible-time origin already
available to Platform through its execution context; it must not restart five
seconds when delivery is consumed. Host remains authoritative through publication.
Use a shared checked deadline helper if the existing duration-only budget API needs
an anchor form; do not expose Host timer pointers or reinterpret a wall-clock time.
The candidate header carries the matching Platform instance and capture ID, actual
capture interval/clock basis and query profile. Transport identity authenticates
the sender; payload IDs alone do not grant authority.

Platform consumes the request in its ordinary outer pump, validates its exact
instance, deadline and idle state, and progresses the agreed capture phases.
Capture failure produces the POD failure outcome without first allocating a report.
On success, release native query/COM objects and unnecessary staging, freeze the
complete candidate and move it into the registered owning handoff envelope. Its
nested allocation attribution and module hazards must be visible to existing
ownership transport. Creating/posting that envelope can fail; preserve the prior
Host report and use the reserved failure path rather than losing the correlation.
There is no separate capture-accepted message; Host knows request delivery through
the existing consumed-prefix bookkeeping and awaits one captured/failed outcome.

`post_owner` busy retains the same envelope for retry; successful transfer ends
Platform access to its bytes. Do not recreate/repost a moved outcome. Platform
retains only its capture bookkeeping until receipt and cleanup drain. Existing
ambiguous/poisoned transport rules still protect potentially exposed ownership;
a failure return must not justify double-send or premature destruction.

Host processes the received outcome outside protected/nested Platform invocations:

1. Authenticate the producing client/instance and match the live capture ID. Check
   current Platform installation, admission and the original deadline before doing
   expensive work. Old-instance, duplicate or otherwise obsolete outcomes cannot
   replace current state; safely retire their owned payloads under existing hazards.
2. For a candidate, check exact storage extent/version/alignment/size, then copy the
   complete document-plus-payload range once into Host-owned aligned allocation.
   Do not copy only `CBakedDocumentBlock::bytes()`, which excludes appended payloads.
   A complete byte-for-byte copy preserves text/span IDs and offsets, requiring no
   semantic relocation/reconstruction. Source and destination overlap count toward
   the capture's memory ceiling; earlier reports/readers retain their own protection.
3. Establish Host-owned asset/backing and reader metadata. Validate the copied block
   against the shared report schema, identities, typed references and portable
   publication/coverage contract. Host interprets engine-owned outcomes; it does
   not run Windows queries or decode SDK structures/native policy to reproduce
   Platform's discovery. All fallible storage/index/acquisition preparation occurs
   before the live current-report change.
4. Recheck instance/capture, deadline and admission immediately before commit.
   Publish the immutable backing and matching capture/state metadata together on
   Host's owner thread. Only this step changes the current report and records a
   successful completed update. Existing reader acquisitions keep old backing alive.
5. Release the incoming Platform-attributed envelope after its required reads end,
   then send the receipt using reserved outcome capacity. Its `published` disposition
   means Host accepted and published independently owned storage. `rejected` or
   `interrupted` means the previous current report/lifecycle state remains authoritative.
   A receipt never asks Platform to hold a borrow for report readers.

For this first pass, prefer the explicit one-copy boundary over relying on memory
reattribution alone to erase Platform allocator/code dependencies. Host constructs
the retained asset wrapper and cleanup operations in Host context. Draining the
incoming envelope and producer bookkeeping is distinct from the lifetime of the
new Host report. Receipt publication follows either commit or definitive rejection;
it is not merely acknowledgement that bytes entered a queue.

After publication, complete the originating public update using its pre-reserved
acquisition of the exact published backing and the agreed `PlatformReportResult`.
Initial automatic capture has no external requester. If the requesting client has
departed, withdraw its delivery/acquisition reservation while allowing Host's
maintenance capture to publish. Reply backpressure retains protected backing;
delivery failure does not undo a committed master report. Preserve existing
potential-exposure/quiescence rules before reclaiming undelivered acquisitions.

Keep the capture occupied through receipt consumption/producer cleanup. Public
current-report reads may already see the new committed report while that handshake
drains; new update requests still receive update-in-progress. Consumed-prefix or
terminal-client/drained-transport evidence settles the receipt without another
acknowledgement. Never redirect a late receipt/result to a reloaded Platform instance.

Host cancellation follows confirmed request-delivery order, or withdraws an unoffered
queued request before it can start; it must not overtake that request and become a
lost cancellation of an unknown future capture. After ownership transfer, reject
and retire any unwanted candidate at Host rather than asking Platform to free it.
Before transfer, Platform owns cleanup and returns the matching failure outcome.
One captured-or-failed producer outcome settles each admitted capture; receipt is
idempotent for its exact operation. Unknown/foreign IDs do not release another
operation's resources or change its state.

Unload/Host shutdown closes admission, makes the current Platform report unavailable
as already agreed, and drains capture/native work, incoming owner envelopes and
receipt bookkeeping before the corresponding module context can be reclaimed.
Retained Host report acquisitions contain no Platform dependency. Failed validation,
copy, timeout or handoff preserves the previous current report where this instance
still exists; a new installation cannot reuse the old instance's report. No handoff
implicitly persists a file, rebuilds a PSO or replaces Rendering's combined baseline.

Implementation review must cover the concrete message registrations, zero/one-reply
credit classification, pumped inbox/consumed-prefix handling, owner-envelope hazards,
Host asset/acquisition reservations and module-unload accounting. Existing facilities
are the basis; the report handlers and their retirement accounting do not exist yet.
Measure Platform build and Host copy/validation/adoption separately under the agreed
local synchronous placement before adding any worker-specific protocol.

References: [pumped POD/owner transport](../../host/runtime/pumped_client.cpp),
[Host client inbox and credits](../../host/runtime/client_record.hpp),
[complete baked storage](../../foundation/data_model/baked_document.hpp),
[Host asset backing](../../host/assets/asset_repository.hpp).

#### Agreed concrete Platform report schema and fields, version 1

The user accepted this concrete version-1 report schema and field definitions on
16 November 2026, following the agreed schema extension, native profile and handoff.
It supersedes the earlier provisional layout/manual-text-pool sketch. V1 serves the
defined desktop-display discovery need, not every possible
Platform domain. Shared Framework schema definitions are the canonical contract.

Package contained `types`, exactly one base instance
`instances.PlatformReportHeader.report` with no specialisations, and these named
bulk entries when nonempty:

| Element type | Collection name |
| --- | --- |
| `PlatformAdapter` | `adapters` |
| `PlatformDisplaySource` | `sources` |
| `PlatformDisplay` | `displays` |
| `PlatformDisplayPath` | `paths` |
| `PlatformModeCatalogue` | `mode_catalogues` |
| `PlatformDisplayMode` | `modes` |
| `PlatformNativeIdentity` | `native_identities` |
| `PlatformByte` | `identity_bytes` |
| `PlatformQueryResult` | `queries` |

The completed runtime envelope uses schema data-block version 1 under the
pre-release reset policy, with generated retained text,
collection IDs, payload lengths and stripped selection metadata. The application
report version is separately 1. A complete byte view includes the document and all
payloads. Use ordinary schema natural layout, fixed-width scalars, u32-backed enums
and named u32 bit structures for validity/filter masks. The schema resolves offsets
and sizes; do not copy Windows structure layout or store native pointers/size_t.
`Text?` and `Span<T>?` below abbreviate the agreed nullable descriptor syntax;
unqualified text/span is non-nullable. They introduce no further schema features.

Every fact/query record has a nonzero capture-local `id: u32`, unique within its
record category and within the nonnegative signed-index domain. Foreign IDs use
u32 with zero meaning no established association where explicitly permitted.
Application IDs are distinct from schema collection IDs, row ordinals, native IDs
and the Host capture ID. A copied complete report retains its fact IDs while schema
relocation can change collection/string IDs. IDs carry no authority across captures.

Use empty spans for known-empty results and omit the corresponding zero-count bulk
entry; the current schema does not persist zero-count bulk entries. Nullable absent
spans denote information not obtained. The header discovery status and query outcomes
must agree with those distinctions. No dummy rows or new empty-bulk-entry feature
is required. Modes and identity bytes are reached through catalogues/identity rows;
the header roots the other collections explicitly.

`PlatformReportHeader`, in field order:

| Fields | Type and meaning |
| --- | --- |
| `kind`, `schema_version` | Report-kind enum `platform_report = 1`; u32 value 1. |
| `platform`, `query_profile` | Engine enums: `windows = 1`, `windows_desktop_discovery_v1 = 1` for this profile. |
| `platform_instance`, `capture_id` | u64, matching the authenticated Host capture/handoff identity. |
| `clock`, `visibility_scope` | Engine enums: `system_awake_ns = 1`, `calling_session = 1`; no UTC or unrestricted-machine claim. |
| `capture_begin_ns`, `capture_end_ns` | u64, actual observation interval from before the first native query through the last accepted native consistency check, on the declared awake clock. Nondecreasing and separate from eligible deadline accounting. |
| `discovery`, `coverage` | Discovery enum `captured = 1 / unsupported = 2 / unavailable = 3`; coverage enum `complete = 1 / limited = 2`. No published failed-capture value. |
| `capture_attempts`, `native_calls` | u32 cumulative counts for this capture, including discarded attempts and retries. |
| `inventory_query_id`, `consistency_query_id` | u32 references to the accepted attempt's core inventory and final consistency outcome; a limitation report uses an explicit blocked/not-applicable consistency outcome. |
| `limits`, `schema_limits` | Embedded effective `PlatformCaptureLimits` and `PlatformSchemaLimits`, defined below. |
| `adapters`, `sources`, `displays`, `paths`, `mode_catalogues` | Nullable spans of their corresponding record types. Absent when the inventory/dependent discovery was not obtained; present-empty when its applicable query establishes no entries. |
| `native_identities`, `queries` | Non-nullable spans of `PlatformNativeIdentity` and `PlatformQueryResult`. Queries are nonempty, including for a limitation report. |

`PlatformCaptureLimits` stores, in order, u32 fields `capture_attempts`,
`query_passes`, `flag_profiles`, `adapters_per_backend`, `sources`, `displays`,
`outputs`, `monitor_records`, `ccd_paths`, `ccd_modes`, `modes_per_scope`,
`total_mode_rows`, `native_calls`; then u64 `owned_bytes`, `report_bytes`,
`eligible_duration_ns`. `PlatformSchemaLimits` stores u64 `inspected_source_bytes`,
`owned_bytes`, `result_bytes`, `work_credits`; then u32 `document_nodes`, `records`,
`depth`. Values record the effective agreed limits, not a new set of defaults.

Shared value structures:

| Type | Fields in order |
| --- | --- |
| `PlatformExtent` | `width: u32`, `height: u32`, in physical pixels. |
| `PlatformRect` | `x: i32`, `y: i32`, `width: u32`, `height: u32`; retain negative desktop positions, check native edge-to-extent arithmetic. |
| `PlatformRefresh` | `precision: PlatformRefreshPrecision`, `numerator: u32`, `denominator: u32`. `unknown = 0`, `rational = 1`, `integer_hz = 2`, `unspecified = 3`; rational denominator is positive, integer-Hz denominator is 1. Preserve native rational components without inventing precision; API default/unspecified tokens are not numerical Hz facts. |
| Normalised enum value wrappers | Each format/scan/scaling/rotation/connection wrapper has its named engine `value: enum` and `native_code: u32`. Query provenance identifies the native code domain. Unknown/other native values remain inspectable. |
| `PlatformDesktopState` | `known_fields: u32 bit structure`, `rectangle: PlatformRect`, `format: PlatformFormatValue`, `bits_per_pixel: u32`, `refresh: PlatformRefresh`, `rotation: PlatformRotationValue`, `scaling: PlatformScalingValue`, `query_id: u32`. Known bits 0..5 respectively identify rectangle, format, depth, refresh, rotation and scaling. |
| `PlatformSignalState` | `known_fields`, `extent: PlatformExtent`, `reported_refresh`, `physical_refresh`, `virtual_refresh` (all `PlatformRefresh`), `scan: PlatformScanValue`, `rotation: PlatformRotationValue`, `scaling: PlatformScalingValue`, `query_id`. Known bits 0..6 follow that order. Report virtual refresh only when the accepted native profile supplies that interpretation. |
| `PlatformDesktopImageState` | `known_fields`, `path_source_extent: PlatformExtent`, `image_region: PlatformRect`, `image_clip: PlatformRect`, `query_id`. Known bits 0..2 follow that order; this preserves virtual-desktop image distinctions without conflating them with signal extent. |

Here and below unqualified `known_fields`, `query_id`, flags, IDs and native codes
are u32. A clear known bit means the corresponding numeric/value wrapper is not a
fact; initialise its storage to zero/unknown rather than making zero the sentinel.
Query outcome explains the missing evidence. For published V1, undefined mask bits
reject. Known false/zero and known-empty values remain distinguishable from unknown.

Topology records, with fields in order:

| Record | Fields |
| --- | --- |
| `PlatformAdapter` | `id`, `label: Text?`, `label_query_id`, `identities: Span<PlatformNativeIdentity>?`, `discovery_query_id`. This is native display-adapter evidence, not a Vulkan device/feature record. |
| `PlatformDisplaySource` | `id`, `adapter_id`, `active: PlatformTruth`, `desktop_attached: PlatformTruth`, `primary: PlatformTruth`, `identities: Span<PlatformNativeIdentity>?`, `desktop: PlatformDesktopState`, `gdi_current: PlatformDesktopState`, `native_flags`, `topology_query_id`, `annotation_query_id`. Keep CCD current source configuration and GDI current settings as separate observations. Primary/desktop annotations come from the latter's evidenced session discovery, not coordinate inference. |
| `PlatformDisplay` | `id`, `adapter_id`, `label: Text?`, `label_query_id`, `available: PlatformTruth`, `active: PlatformTruth`, `connection: PlatformConnectionValue`, `connection_query_id`, `identities: Span<PlatformNativeIdentity>?`, `native_flags`, `state_query_id`. Active/unavailable is a representable native observation; availability is not a claim of physical connection beyond API evidence. |
| `PlatformDisplayPath` | `id`, `source_id`, `display_id`, `signal: PlatformSignalState`, `desktop_image: PlatformDesktopImageState`, `native_flags`, `query_id`. Store actual active relationships only; inactive possible CCD pairings do not become desktop paths. Multiple displays can reference a source, preserving cloning. |

Core CCD-backed source/display rows have a valid adapter association and their
tagged CCD source/target identity tuples; each CCD adapter retains its LUID evidence.
Supplemental adapter rows may lack optional label/identity evidence. Do not attach unmatched GDI
annotations to arbitrary core rows. Missing required path endpoints fail validation;
unknown optional associations use zero only where specified for a mode scope below.
Native flags are the raw returned flags for the row's stated native provenance;
normalised activity/availability fields remain the portable interpretation.

Mode records, with fields in order:

This is the initial implemented layout. The subsequent
[scope and condensation decision](#platform-report-scope-and-condensation)
requires a revised representation; its proposed sharing supersedes the original
prohibition on deduplication across catalogues once the revised schema is settled.

| Record | Fields |
| --- | --- |
| `PlatformModeCatalogue` | `id`, `scope: PlatformModeScope`, `adapter_id`, `source_id`, `display_id`, `identities: Span<PlatformNativeIdentity>?`, `format_filter: PlatformFormatValue`, `orientation_filter: PlatformRotationValue`, `filter_flags: u32 bit structure`, `query_id`, `modes: Span<PlatformDisplayMode>?`. |
| `PlatformDisplayMode` | `id`, `catalogue_id`, `known_fields`, `extent: PlatformExtent`, `refresh: PlatformRefresh`, `format: PlatformFormatValue`, `bits_per_pixel`, `scan: PlatformScanValue`, `scaling: PlatformScalingValue`, `rotation: PlatformRotationValue`. Known bits 0..6 follow extent, refresh, format, depth, scan, scaling, rotation. |

Scope codes are `display = 1`, `source = 2`, `native_output = 3`. Display scope
requires a display ID; source scope requires a source ID and leaves display ID zero;
native-output scope requires an adapter ID and leaves source/display IDs zero unless
an independently evidenced source association is retained. Native-output identity
evidence can be shared by its two format-filter catalogues. Do not invent a physical
target association to make the record fit. Cross-scope extra IDs must agree with
the known topology and never narrow an ambiguous source scope to one clone target.

Catalogue filter bits 0..5 mean monitor-compatible filtering, current-orientation
filtering, interlaced inclusion, scaled-mode inclusion, stereo exclusion and explicit
format filtering. Set them only when supported by the actual query contract. Raw
native filter bits are preserved in the query record. A GDI catalogue has no precise
channel-format filter; it can report depth without claiming an RGBA/BGRA layout.
A successful zero-mode query has present-empty `modes`; failed/unsupported/blocked
queries have absent `modes`. Store one contiguous range per catalogue in the shared
mode collection, preserving each combination's qualifiers. Each mode points back
to that catalogue. Do not deduplicate across query/catalogue scopes; exact duplicates
within one scope may be removed, with the native returned count retained separately.

Identity and query records, with fields in order:

| Record | Fields |
| --- | --- |
| `PlatformNativeIdentity` | `id`, `kind: PlatformNativeIdentityKind`, `query_id`, `bytes: Span<PlatformByte>?`, `text: Text?`. Kind selects exactly one payload. Identities may be shared by multiple records; their schema references do not carry native handles. |
| `PlatformByte` | `value: u8`; used only for canonical tagged native evidence. |
| `PlatformQueryResult` | `id`, `kind: PlatformQueryKind`, `api: PlatformQueryApi`, `subject_kind: PlatformSubjectKind`, `subject_id`, `status: PlatformQueryStatus`, `reason: PlatformQueryReason`, `error_domain: PlatformErrorDomain`, `native_error`, `native_flags`, `returned_count`, `attempts`, `call_count`, `elapsed_ns: u64`, `blocked_by_query_id`, `diagnostic: Text?`. |

Initial identity kinds are `windows_adapter_luid = 1` (8 bytes),
`windows_ccd_source = 2` and `windows_ccd_target = 3` (12 bytes: canonical adapter
LUID then u32 source/target ID), `windows_adapter_device_path = 4`,
`windows_monitor_device_path = 5`, `windows_gdi_source_name = 6`,
`windows_dxgi_output_name = 7`, `windows_monitor_interface_name = 8` (text for 4..8).
Canonical numeric evidence is little-endian, with LUID low/high 32-bit words in
that order. Keep native matching scoped to the identity kind; ordinary text is not
case-folded or promoted into identity by its label. This is tagged value evidence,
not Windows SDK layout or a new generic blob type.

Query kind codes 1..11, in order, are `inventory`, `consistency_check`,
`source_identity`, `display_identity`, `adapter_identity`, `desktop_annotation`,
`adapter_description`, `output_description`, `adapter_output_inventory`,
`mode_catalogue`, `current_desktop_settings`. API codes 1..5 are `windows_ccd`,
`windows_display_device_info`, `windows_gdi_devices`, `windows_gdi_settings`,
`windows_dxgi`. Subject codes 1..8 are `report`, `adapter`, `source`, `display`,
`path`, `mode_catalogue`, `native_identity`, `query`; report scope uses ID zero,
other subjects require a resolving capture-local ID of the stated category.

Query statuses 1..6 are `success`, `unsupported`, `unavailable`, `failed`, `blocked`,
`not_applicable`. Reason codes are `none = 0`, then `api_not_supported = 1`,
`access_denied = 2`, `dependency_missing = 3`, `identity_ambiguous = 4`,
`value_not_reported = 5`, `native_failure = 6`, `profile_fallback = 7`,
`buffer_retry_exhausted = 8`, `topology_changed = 9`. Error domains are
`none = 0`, `win32 = 1`, `hresult = 2`; native codes retain their unsigned raw bits.
Do not invent GetLastError evidence for APIs without a documented error-code result.
Blocked/not-applicable outcomes have zero executed calls and can reference their
blocking query. Successful retrieval may still have a value-not-reported limitation;
it does not certify an optional field or demonstrated capability.

One query record describes a logical scope and actual flag/profile, aggregating
its count/fetch calls and bounded retries. Keep rejected compatibility-profile
outcomes as separate records in the retained attempt. The header's core IDs point
to the accepted inventory/final check. Discarded whole-capture attempts contribute
to cumulative counters/diagnostics, not mixed old facts in the published tables.
`returned_count` is the native pre-deduplication count, and `elapsed_ns` is query
time, never achieved rendering rate. Coverage `limited` records unavailable/failed
enrichment, reduced profiles or missing applicable facts; `complete` does not mean
that the hardware supports every query or that the capture was atomic.

Common portable enum codes are explicitly versioned:

| Enum | Values |
| --- | --- |
| `PlatformTruth` | `unknown = 0`, `no = 1`, `yes = 2`. |
| `PlatformFormat` | `unknown = 0`, `rgba8_unorm = 1`, `bgra8_unorm = 2`, `desktop_8bpp = 3`, `desktop_16bpp = 4`, `desktop_24bpp = 5`, `desktop_32bpp = 6`, `other = 255`. Depth-only desktop formats do not assert channel order/HDR. |
| `PlatformScan` | `unknown = 0`, `unspecified = 1`, `progressive = 2`, `interlaced_upper_first = 3`, `interlaced_lower_first = 4`, `interlaced_unspecified = 5`, `other = 255`. |
| `PlatformRotation` | `unknown = 0`, `identity = 1`, `counterclockwise_90 = 2`, `counterclockwise_180 = 3`, `counterclockwise_270 = 4`, `other = 255`. Platform normalises each native convention rather than assuming matching numeric enums. |
| `PlatformScaling` | `unknown = 0`, `identity = 1`, `centered = 2`, `stretched = 3`, `aspect_preserving = 4`, `custom = 5`, `preferred = 6`, `unspecified = 7`, `other = 255`. |
| `PlatformConnection` | `unknown = 0`, `internal = 1`, `displayport = 2`, `hdmi = 3`, `dvi = 4`, `vga = 5`, `wireless = 6`, `virtual_output = 7`, `other = 255`. Classify only from native evidence, not labels. |

Unknown enum zero is not a valid placeholder for required header/query selectors.
The corresponding `...Value` wrappers above use these enums plus raw native code.
Report version 1 requires the exact expected type names, fields, order and enum/
mask meanings. Unknown native codes use an existing `other` wrapper; unknown report
contract fields/categories require an explicitly versioned contract decision rather
than silent interpretation. No generic runtime schema migration is implied.

In addition to schema/reference validation, check unique category-local IDs, all
foreign IDs and query subjects, acyclic blocking-query links, catalogue/mode
ownership, expected identity payload kind/length, canonical unknown/absent storage,
and capture/header/limit consistency. A normal core inventory has present root
spans and valid path endpoints; a core visibility limitation uses absent inventory
spans and the explicit unsupported/unavailable discovery status. It never becomes
a successful zero-display claim. The query matrix and allowed publication outcomes
remain those already agreed; this layout does not strengthen optional queries into
mandatory publication dependencies.

Application-ID relationships are report validation, not extra schema ownership
edges. A valid standalone Platform report copy includes its header and rooted
collections; copying an isolated adapter record with generic schema operations
does not by itself produce a complete valid report. No Vulkan/device features,
window handles, pipeline state, HDR/VRR details or mutable availability bookkeeping
are added to the immutable report.

Schema examples use the agreed forms: `label` has
`{ "kind": "text", "nullable": true }`; catalogue modes have
`{ "kind": "span", "element": "PlatformDisplayMode", "nullable": true }`.
When implementation is authorised, create the canonical schema artifact and
generated/layout-checked C++ declarations from these agreed field definitions,
not an ad hoc duplicate record layout.

Sources informing field separation: [desktop image regions](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_desktop_image_info)
and [valid GDI current-mode fields](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-devmodew).
The concrete report field names, numeric codes and packaging above are agreed
engine schema policy, not a reproduction of Windows structures.

#### Focused design: agreed display and mode fields

The user accepted the following fields on 16 November 2026. Platform supplies the
native display facts; engine-owned records preserve unknown/unavailable fields and
query limitations. Detailed HDR/colour capability, variable-refresh ranges and EDID
parsing are deferred. Reported pixel depth does not establish HDR support.

| Record | Fields |
| --- | --- |
| Display identity | Audit-local ID, reported name, native identity where available and connection type. Names are labels; native identity evidence supports the cross-audit policy above. |
| Desktop participation | Reported connection/availability, active-on-desktop state, primary desktop source and source-to-display relationships, retaining mirrored displays separately. |
| Current configuration | Desktop position and pixel extent, output-signal pixel extent, rotation, scaling, scan order and refresh information. Desktop and signal extents remain distinct. |
| Available mode | Pixel width/height, refresh rate, progressive/interlaced status, format or reported pixel depth, orientation and scaling qualifiers where supplied. |
| Query coverage | Source API, relevant filters, outcome and limitations; unavailable fields are explicitly unknown. |

Include connected but inactive displays when discoverable. Their current active
configuration is absent; saved settings are not substituted for current settings.
Preserve query-reported connection/availability separately from active-path state.

Retain refresh numerator/denominator where reported, including distinctions such as
60000/1001 Hz versus 60 Hz; decimal rounding is for display only. Do not invent
fractional precision when a source reports only an integer rate. Current virtual
and physical signal refresh rates remain separately labelled when exposed. These
are configuration claims, not measured instantaneous rates or workload frame rates.

Each mode is one reported combination of resolution, refresh and qualifiers. Do not
form a Cartesian product of separate value lists. Retain enumeration coverage such
as monitor-compatibility, orientation, format and scaling filters. A reported mode
does not establish Vulkan presentation usability or successful mode switching.
The native query profile, enumeration filters and shared report contracts are
agreed above and require coordinator ownership/interface review before implementation.

Windows API references informing these field distinctions:
[display path state and virtual/physical refresh](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_path_info),
[target availability](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_path_target_info),
[mode enumeration](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw)
and [mode descriptors](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_mode_desc1).

#### Focused design: agreed device identity and capability coverage

The user accepted the following device fields and coverage on 16 November 2026.
Rendering enumerates all devices reported by the backend. Enumeration does not
rank devices or select an execution device; selection is a separate request under
the policy below.

| Group | Fields |
| --- | --- |
| Audit identity | Audit-local device ID and backend/API. |
| Device description | Reported name, vendor ID, device ID and device type, including integrated, discrete, virtual and CPU implementations. |
| Native identity | Device UUID and native adapter identity where available, with validity information. Names and enumeration order are not identity keys. |
| Driver/environment | Device API version, raw driver version, available driver name/information, driver UUID and separately labelled pipeline-cache UUID. Retain the discovery instance/API configuration separately. |
| Queues | Each family's index, operation flags, queue count, timestamp support and image-transfer granularity. |
| Memory | Heap sizes/flags and memory types, including heap associations and host-visible/coherent/cached properties. |
| Capabilities | Reported baseline core features and limits, advertised device extensions, and explicitly scoped additional feature/property and format queries. |

Capture the baseline core feature/limit records and advertised extension list, then
query additional properties and formats needed by the graphics/compute cases. State
the query coverage explicitly; unqueried capabilities remain unknown. The additional
query set is recorded below. The precise native baseline/version contract still
requires refinement; this agreement does not
require querying every core-version or extension property exposed by a newer device.

Heap sizes describe API-reported resources, not available allocation budgets or
guarantees of successful allocation. Reported feature support is separate from
features enabled for any logical device. Retain raw driver versions because their
encoding is implementation-defined; any decoded text needs an identified decoding
rule. Driver UUID, device UUID and pipeline-cache UUID retain their distinct roles.

When valid and available, a native adapter identity can establish an evidenced
association between Rendering's device and Platform's native display-adapter facts.
It does not establish that the device drives every display or can present to every
surface. Apply the cross-audit identity/correlation policy above; names and
enumeration positions must not silently substitute for native identity evidence.

Vulkan references informing this coverage:
[device/driver identity](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html),
[device properties and version encodings](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceProperties.html),
[queue families](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html),
[memory properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryProperties.html)
and [feature reporting/enablement](https://docs.vulkan.org/spec/latest/chapters/features.html).

#### Focused design: agreed additional capability queries

The user accepted this query set on 16 November 2026, adding depth/stencil support
to the colour-format coverage. These are API queries, not advance creation,
allocation or execution tests. The device-selection policy is recorded below.

| Area | Queries |
| --- | --- |
| Identity/driver metadata | Device/driver UUIDs and valid native adapter identity; driver name/information where the relevant API version or extension supplies it. |
| Shader requirements | Determine API-reported support for requirements retained in actual compiled artifacts from advertised extensions and applicable core functionality. Include retained Google HLSL/user-type metadata and non-semantic debug information where emitted. |
| Colour formats | Format capabilities for `R8G8B8A8_UNORM`, `R8G8B8A8_SRGB`, `B8G8R8A8_UNORM` and `B8G8R8A8_SRGB`; retain reported feature flags. These are initial triangle candidates, not a selected attachment format. |
| Depth/stencil formats | Format capabilities for the core depth-only, stencil-only and combined formats listed below; retain reported feature flags, including depth/stencil attachment support. |
| Conditional presentation | When a surface exists: per-queue-family presentation support, surface format/colour-space pairs, present modes, image-count/extent limits, supported image usages, transforms and composite-alpha modes. |
| Presentation retirement | Instance/device extension availability and the `swapchainMaintenance1` feature for a supported KHR or EXT maintenance path, including its dependencies. Required for graphics execution, independently of audit/compute. |

Depth/stencil format coverage comprises `D16_UNORM`, `X8_D24_UNORM_PACK32`,
`D32_SFLOAT`, `S8_UINT`, `D16_UNORM_S8_UINT`, `D24_UNORM_S8_UINT` and
`D32_SFLOAT_S8_UINT`. Keep linear/optimal tiling capabilities distinct. Retain the
baseline depth/stencil framebuffer sample-count limits as well; do not imply that
an aggregate limit establishes every format/usage/sample-count combination. This
extends audit coverage only: the initial triangle still has no depth/stencil state
or attachment requirement. Unsupported depth/stencil formats do not by themselves
make a device ineligible for that triangle or for compute.

Evaluate shader requirements against applicable core-version functionality as well
as extension names. For example, non-semantic shader information is core in Vulkan
1.3; a missing extension name alone does not establish lack of support. No features
are implicitly enabled and no artifact metadata is silently stripped or downgraded.

The standalone audit creates no window merely to query presentation. Without a
surface, surface-specific results are unqueried; their absence is not a claim of
unsupported presentation. Associate later surface facts with the exact device,
queue family where applicable and surface. They remain API claims, not proof of
successful swapchain creation or presentation. Surface availability and query
results may change before a later request.

The baseline supplies workgroup/dispatch, storage-buffer, descriptor, alignment and
memory-property facts for the proposed simple 32-bit compute case. No additional
optional execution-feature queries are initially selected for that case. Its exact
shader/buffer contract is specified in increment 1C and checked against these facts.

When a concrete resource description requires it, query image support for the exact
format/type/tiling/usage/creation-flag combination. Buffer/image memory requirements
and compatible memory types are obtained after the requested resource is created;
the audit does not create trial resources to populate those results. Memory-budget
telemetry and additional optional execution features remain outside this initial
query set.

References:
[format definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormat.html),
[format capability flags](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatProperties.html),
[image-combination queries](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties.html),
[surface-specific support](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceSupportKHR.html),
[surface capabilities](https://docs.vulkan.org/refpages/latest/refpages/source/VkSurfaceCapabilitiesKHR.html)
and [non-semantic information promotion](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_shader_non_semantic_info.html).

#### Focused design: agreed device-selection policy

The user accepted these selection rules on 16 November 2026. The agreed eligibility
requirements and graphics/compute recipes follow below. Remaining implementation
boundaries are listed at the end of this document.

- Select for the particular workload. Graphics checks its shader/state requirements
  and presentation support for the actual surface. Compute checks its own shader,
  queue, buffer and dispatch requirements without requiring presentation. A device
  may serve both when it satisfies both sets of requirements.
- Filter by requirements before applying preferences. Retain candidate eligibility
  and reasons. Missing surface information leaves graphics eligibility pending;
  it does not establish unsupported presentation.
- Permit explicit selection by audit-local device ID or device UUID. An absent,
  ambiguous or unsuitable explicit selection returns a clear result and does not
  silently select another device. Cross-audit use of a local ID is not implied.
- Automatic selection prefers eligible discrete GPUs, then eligible integrated
  GPUs. Virtual, CPU and other implementations require explicit selection. This
  ordering is a convenience policy, not a measured performance ranking. Selection
  among multiple eligible devices of the same preferred type remains to be refined.
- Keep selection separate from native creation. Record the selected device,
  applicable requirements and preference decision. Enable only capabilities
  required by the selected configuration; queried support is not blanket enablement.
- Report subsequent device/pipeline creation or execution failures against that
  selection without automatically cycling through other devices. Retain the API
  claims separately from those request outcomes. Any later attempt using another
  device is a separate explicit request, not an automatic fallback.

#### Focused design: agreed eligibility requirements

The user accepted these requirements on 16 November 2026, including Vulkan 1.1 as
the minimum and support for separate graphics/presentation queue families.
Eligibility means the queried claims satisfy the workload's pre-creation
requirements. Native creation, allocation, execution and verified output remain
separate request outcomes; eligibility does not demonstrate them in advance.

| Scope | Requirements |
| --- | --- |
| Shared baseline | Vulkan 1.1 or later, compatible with the actual shader target and artifact capabilities/extensions and required layout rules. No silent removal of metadata or reduction of requested options. |
| Graphics queues | At least one graphics-capable queue and one queue supporting presentation to the actual surface. Prefer a shared family; support separate families. |
| Graphics output | At least one of the four colour candidates usable as a colour attachment and advertised with the supported SDR colour space for that surface. Only one suitable combination is required. |
| Graphics state/limits | The specified single-sample recipe, inter-stage interface, colour output, framebuffer extent and viewport/scissor requirements fit reported support/limits. Depth/stencil support remains audit information. |
| Presentation configuration | Swapchain support, a supported/enabled swapchain-maintenance presentation-fence path with its dependencies, and a usable combination of image count, extent, colour-attachment usage, transform, composite alpha and present mode. FIFO is the initial recipe's present mode. |
| Compute queues | At least one compute-capable queue; no dedicated compute queue, graphics capability or presentation support is required. |
| Compute limits | The selected calculation's local size, total workgroup invocations, dispatch dimensions, storage-buffer range and descriptor requirements fit the reported limits. |
| Compute memory | Reported memory properties permit the proposed CPU-readback strategy. Exact resource-compatible memory types and successful allocation remain later checks. |

Report eligibility as eligible, ineligible or unresolved, with reasons. Missing
required queries leave eligibility unresolved. A minimised/zero-sized window means
drawing must wait; it does not make the device unsuitable. Keep temporary drawable
availability separate from device eligibility and later execution results.

Check surface format/colour-space pairs and the joint presentation configuration;
independent support for individual values is not enough. The detailed graphics
state, supported colour-space choice and compute calculation/buffer/dispatch values
come from the agreed recipes below, subject to overarching review. Do not invent
extra hardware requirements beyond those recipes. API-reported memory properties are preliminary
evidence for a strategy, not proof that a particular resource can be allocated.

### 1B. Graphics triangle and presentation

Use one conventional vertex/fragment graphics pipeline to exercise stage matching
as well as native creation. Render and present a procedural triangle through the
existing Platform window facilities. This includes the smallest necessary surface,
swapchain, command submission, synchronisation and presentation path, with explicit
startup, completion and teardown behaviour.

The representative case uses a procedurally generated triangle, one floating-point
inter-stage value and one colour output. It needs no vertex buffer or application
descriptor resources. The value is consumed by the fragment stage so that matching
is meaningful. Device/backend built-ins retain their recognised meanings; application
semantics are not inferred from the varying's type or name.

Use the explicitly described state profile below. Its state and attachment-format
selection are part of the supported recipe, not hidden global defaults.

Native creation/destruction remains an independently testable checkpoint within this
increment. Its completion also needs visible triangle presentation and appropriate
execution evidence. The design must choose handling for resize, minimisation, close
and presentation failures using the actual Platform contract. Full-screen/exclusive
presentation, mode switching and multiple windows are separate extensions unless
explicitly selected during refinement.

#### Focused design: agreed graphics recipe

The user accepted this recipe on 16 November 2026. It specifies the representative
physical graphics case; the agreed submission and window-handling paths follow
below. No general transform/interpolation convention or application-semantic mapping
is established by this fixture.

| Concern | Choice |
| --- | --- |
| Draw | One non-indexed triangle: three vertices, one instance, first vertex/instance zero. Generate positions from vertex ID. |
| Stages/resources | Vertex and fragment stages only; no vertex buffers, application descriptors or push constants. |
| Stage connection | One 32-bit floating-point scalar at explicit location 0, using ordinary perspective interpolation and consumed by the fragment stage. |
| Fragment output | One `float4` at colour location 0, alpha always 1. |
| Assembly | Triangle list; primitive restart disabled. |
| Rasterisation | Filled polygons, culling disabled, counter-clockwise front-face setting, line width 1; depth clamp, depth bias and rasterizer discard disabled. |
| Viewport/scissor | Exactly one of each, both dynamic and explicitly set for every draw to the full drawable extent. Positive viewport height; depth range 0 to 1. No other dynamic state. |
| Sampling | One sample, sample mask 1; sample shading, alpha-to-coverage and alpha-to-one disabled. |
| Depth/stencil/blending | No depth/stencil attachment or tests. Blending and logic operations disabled; all RGBA channels writable. |
| Render target | One ordinary render pass/subpass, rendering directly into the acquired swapchain image; clear to opaque black and store the result. |

Generate three clip-space positions forming a centred triangle, all with z = 0.5
and w = 1. Their scalar outputs are 0, 0.5 and 1. The fragment shader produces
`(t, 1 - t, 0.25, 1)`. Retain recognised backend built-ins for vertex ID and position
and use the existing explicit-location support for the application varying. The
gradient makes consumption of that varying observable; its type/name does not
imply an application meaning.

Select the first usable surface combination in this order: `B8G8R8A8_SRGB`,
`R8G8B8A8_SRGB`, `B8G8R8A8_UNORM`, `R8G8B8A8_UNORM`, each paired with the SDR
sRGB nonlinear colour space. Record the selected combination in the materialised
recipe. Initial visual acceptance checks geometry and a visible gradient without
requiring exact colour matching across formats.

Pipeline creation remains separately testable from rendering/presentation.
Accepting this recipe does not establish its implementation or execution.

References:
[rasterisation state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRasterizationStateCreateInfo.html)
and [viewport state](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html).

#### Focused design: agreed swapchain and submission/synchronisation path

The user accepted this path and its swapchain-maintenance requirement on
16 November 2026. A graphics-submission fence alone does not establish completion of
presentation's use of its wait semaphore. Core `vkDeviceWaitIdle`/`vkQueueWaitIdle`
alone must not be presented as a guaranteed proof for reclaiming all presentation
resources. See the [Khronos semaphore-reuse and shutdown discussion](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).

Require swapchain-maintenance presentation fences for the graphics execution
profile, using the KHR extension or its equivalent EXT form with the applicable
dependencies and feature enabled. This extends the agreed additional queries and
graphics eligibility requirements above. Vulkan 1.1 remains the minimum core API;
that core version alone does not satisfy the presentation requirement. Audit and
compute remain usable independently. Include the dependency in overarching review
before implementation.

The KHR path requires `VK_KHR_swapchain_maintenance1` with its
`swapchainMaintenance1` feature and the dependent `VK_KHR_swapchain` and instance
`VK_KHR_surface_maintenance1` functionality. The equivalent EXT path uses
`VK_EXT_swapchain_maintenance1` and `VK_EXT_surface_maintenance1`. The surface
maintenance dependency also requires `VK_KHR_get_surface_capabilities2` and
`VK_KHR_surface`. Query/enable a complete supported dependency path; do not infer
support from a newer core version or mix extension names without a valid path.

| Concern | Choice |
| --- | --- |
| Control | Serial Rendering-owned path with one graphics submission in flight and presentation resources tracked per swapchain image. |
| Swapchain | FIFO, one image layer, colour-attachment usage only, clipping enabled. Request minimum image count plus one, capped by a nonzero reported maximum; use the actual returned count. |
| Extent | Use a surface-required extent; otherwise clamp Platform's drawable pixel size to the supported range. Wait while minimised/zero-sized. |
| Queues/sharing | One graphics queue and one presentation queue, sharing a queue where possible. Exclusive image sharing for one family; concurrent sharing across both families when different. |
| Resources | One reusable graphics command buffer, one binary acquisition semaphore and one graphics-completion fence. Image views/framebuffers, binary render-finished semaphores and presentation-fence tracking are associated with the swapchain images. |
| Commands | Explicit acquisition-to-colour-rendering synchronisation and layout transition, clear/draw, then transition to presentation layout. Use Vulkan 1.1 submission/barrier facilities; no timeline semaphore or synchronisation2 dependency. |

Each frame follows this sequence:

1. Observe completion of the previous graphics submission before reusing command
   resources. Observe the relevant presentation-fence completion before reusing its
   fence or semaphore; a graphics fence is not a substitute.
2. Acquire an image with a finite timeout, preserving responsiveness to window/stop
   handling. Associate presentation resources with the returned image index.
3. Submit graphics, waiting on acquisition at the colour-output stage and signalling
   that image's render-finished semaphore. Reset the graphics fence only when
   committing a submission, not before an acquire that may produce no image.
4. Queue presentation waiting on render-finished and attach a maintenance presentation
   fence. Track its resource-release completion separately from graphics completion.

The command buffer and graphics fence may be reused only after their own completion
requirements hold. Reset/reuse of a presentation fence must follow its tracked
completion; per-image allocation alone does not prove it safe. Window-state changes,
abandoned acquisition, failed submissions/presents and retirement must preserve the
outstanding obligations of the exact operations that occurred. Their agreed
state/error transitions follow below.

Presentation fences establish the specified resource-release condition, not proof
that a frame was visibly scanned out. See the [present fence contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)
and [swapchain maintenance extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_swapchain_maintenance1.html).
See also [surface maintenance dependencies](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_surface_maintenance1.html).

#### Focused design: agreed window-state and failure handling

The user accepted this handling on 16 November 2026, including automatic close
acceptance by the initial fixture's controller and ending the graphics session on
surface loss. Reuse the existing Platform snapshot/close/borrow-retirement contract;
Rendering does not take over native window dispatch or destruction.

| Event | Behaviour |
| --- | --- |
| Resize | Coalesce changes to the latest drawable pixel extent. Stop acquiring, drain outstanding work, retire existing swapchain resources, then rebuild from fresh surface queries. Retain the device and compatible pipeline objects. |
| DPI/logical-size change | Rebuild only when required drawable extent or presentation configuration changes; a snapshot revision alone is insufficient. |
| Minimisation/zero extent | Pause acquisition/drawing. Continue notifications, shutdown and outstanding completions. Retain resources until rebuilding/retirement is needed and recheck on restoration. |
| Acquire timeout/not-ready | No image was acquired. Service other work and retry later; do not reset the graphics fence or report device failure. |
| Out-of-date | Mark the swapchain for rebuilding while preserving obligations of work already submitted or presentation already queued. |
| Suboptimal | Complete the acquired frame and schedule rebuilding. Repeated suboptimal results for an unchanged configuration must not cause an endless rebuild loop. |
| Close intent | Pause new frames. Only the Platform controller answers the exact close-request sequence; the initial fixture's controller automatically accepts. |
| Accepted close/Host shutdown | Stop new work and retire the graphics session. Window close does not itself request engine shutdown. |
| Surface loss/other fatal session error | Report failure and retire safely. No automatic surface recreation or device fallback initially. |
| Device loss | Stop all work using that logical device and follow Vulkan lost-device cleanup rules. Do not wait for ordinary successful execution evidence that can no longer arrive. |

Normal resize/close retirement follows this order:

1. Resolve any acquired image that was not presented. Use maintenance image release
   after device use has finished, and safely consume or retire its semaphore state.
2. Observe graphics completion and the applicable presentation resource-release
   fences. Track the actual submitted/queued operations and their result semantics.
3. Destroy dependent graphics/swapchain objects and, when ending the session, the
   surface. Retain any device objects still protected/used by another consumer.
4. Stop all borrowed window-state access and acknowledge Platform borrow quiescence.
   Platform retains native-window destruction authority.

All waits preserve control-loop progress. A prolonged stall produces a diagnostic
and visibly pending cleanup; elapsed time does not authorise resource destruction.
Failure and cleanup completion remain separate outcomes. Device loss uses the
applicable Vulkan cleanup rules rather than fabricating signalled fences or
successful work. Retain unresolved lifetime dependencies when cleanup cannot be
established.

References:
[Platform close and retirement](platform_first_window.md#close-authority-and-retirement),
[maintenance image release](https://docs.vulkan.org/refpages/latest/refpages/source/vkReleaseSwapchainImagesKHR.html),
[presentation error semantics](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)
and [lost-device rules](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html#devsandqueues-lost-device).

### 1C. Simple verified compute job

Create one compute pipeline from an engine-produced component, dispatch a small
deterministic calculation and check the returned values against expected results.
Specify its workgroup/dispatch sizes, supported buffer representation and relevant
device limits. Submission success alone does not verify the calculation.

This introduces a deliberately narrow explicit buffer-binding, allocation,
synchronisation and CPU-readback case, extending the earlier proposal's empty
resource layout and deferred readback boundary. Its agreed contract follows below
and still needs overarching review before implementation. It does not settle general
structure correspondence, packing/conversion, semantic selection or provider APIs. Feed concrete requirements
back into stage 2; preserve the user's further structure-binding ideas for discussion.
Compute has a separate acceptance checkpoint and should not depend on a display or
swapchain merely because graphics uses one.

#### Focused design: agreed compute calculation and buffer/readback contract

The user accepted this contract on 16 November 2026. It is one narrow explicit
physical binding/readback case, not a general supplied-data/structure mapping,
semantic selection, provider API or numerical-proof facility. Compute retains its
separate implementation/review/acceptance checkpoint and runs without presentation.

| Concern | Contract |
| --- | --- |
| Calculation | For indices 0 through 255, write `output[i] = (3 * i) + 7` using unsigned 32-bit integers. Expected values run from 7 to 772. |
| Dispatch | Local size `(64, 1, 1)`; dispatch `(4, 1, 1)` workgroups. Each invocation writes its own element. |
| Shader resource | One `RWStructuredBuffer<uint>` at explicit set 0, binding 0; descriptor count 1, compute visibility only. No other application resources or push constants. |
| Physical representation | Element offset 0, stride 4 bytes, 256 elements; descriptor offset 0 and range 1,024 bytes. Validate these facts against reflection. |
| Buffer usage | Storage buffer accessed by one compute queue. |
| Memory/readback | One allocation used solely by this buffer, with a resource-compatible host-visible memory type. Prefer coherent memory and also support non-coherent memory. Read back through its mapping. |
| Submission | One command buffer, one dispatch and one completion fence. No presentation or semaphore dependency. |
| Verification | Compare every element exactly. Retain returned values, checked count, mismatch count and the first mismatch's index/expected/actual values. |

The current reflection path recognises structured storage buffers and records
runtime-sequence offset/stride. Check the concrete fixture's unsigned scalar width,
resource coordinates/count, element layout and execution dimensions; do not infer
compatibility from its HLSL name alone. No general aggregate correspondence or
representation conversion is introduced.

The execution sequence is:

1. Create, bind and map the buffer, then initialise all 256 elements to
   `0xFFFFFFFF`, which cannot equal an expected output value.
2. Flush host writes when memory is non-coherent.
3. Establish the host-write-to-compute-write dependency, bind the pipeline and
   descriptor, and dispatch.
4. Establish the compute-shader-write-to-host-read dependency and signal the
   submission fence.
5. Observe fence completion and invalidate non-coherent memory before reading.
6. Copy values into owned CPU result storage and verify all 256 elements.

Map the whole allocation and use that range for any required flush/invalidate,
respecting non-coherent atom-size and mapped-range rules. Coherent memory avoids
those cache-maintenance calls, not the execution/dependency requirements. No host
read/write overlaps device use. Invalidation alone does not wait for device work.

If no compatible host-visible memory type exists, report the resource requirement
as unsatisfied. No automatic staging-copy path is included; that is a later
extension. Mapping/allocation/submission/completion/readback/verification outcomes
remain distinct, and failed execution cannot claim verified output. The agreed
resource/result lifetimes and failure handling follow below.

References:
[readback synchronisation](https://docs.vulkan.org/refpages/latest/refpages/source/vkInvalidateMappedMemoryRanges.html)
and [mapped-range requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkMappedMemoryRange.html).

## Agreed result ownership, lifetimes and failure handling

The user accepted these rules on 16 November 2026. Live objects remain on the serial
Rendering control path; ownership of a diagnostic report is not ownership of an
active GPU operation. This is a local ownership contract, not a new general
observation/provider framework or cross-module transfer protocol.

| Object | Ownership and lifetime |
| --- | --- |
| Audit snapshot | Owns records, strings and diagnostics; contains no borrowed enumeration pointers or live native handles. May outlive discovery. |
| Pipeline owner | Rendering-owned and bound to its device context. Protects the device, native dependencies and code required for use/destruction. Failed construction returns no usable owner. |
| Graphics session | Retains its pipeline, command resources, swapchain/presentation resources and Platform window use until retirement completes. |
| Compute operation | Retains its pipeline, descriptors, buffer/allocation, mapping and command resources through submission, readback and verification. |
| Execution report | Owns diagnostics, configuration/component associations, environment information and CPU results. No mapped-memory pointers or borrowed job views escape. May outlive GPU/window teardown. |

Owned result storage still depends on its captured allocator contexts and Rendering
code. The enclosing owner must keep those alive until result destruction; moving a
result does not acquire those lifetimes. Reuse the existing
[conditioning ownership contract](../../rendering/runtime/shader_conditioning.hpp)
and generic module/lifetime services. No hidden lifetime pin is inferred from an
owning byte/string container.

Reserve minimal outcome bookkeeping before accepting an operation. Admission
failure returns directly; an accepted operation has storage for an identifiable
outcome. Failure to allocate optional diagnostic text must not erase the failure
code or completed evidence. Retained upstream compilation/reflection diagnostics
own their backing or remain under explicit existing lifetime protection, never
unprotected views into finished jobs or released sources.

Preserve separate validation, native-creation, submission, completion, presentation
and verification checkpoints. A compute mismatch retains all returned values and
successful earlier checkpoints; successful submission is not verified output.
Construction failure cleans up only newly owned resources and leaves independent
pipelines intact. Device loss affects every owner using that logical device.

Cancellation stops further work and drains accepted work. Before submission, no
GPU work starts. After submission, referenced resources remain protected until the
applicable completion/cleanup rules permit release. Preserve completed evidence;
cancellation is not a proof of quiescence or permission to abandon a pending use.

Keep the work outcome separate from cleanup. Expose a detected failure promptly in
the controller's operation state. For execution fixtures, publish the final report
after cleanup; a stall remains visibly failed with cleanup pending and resources
retained. Live-object destruction follows explicit drain/retirement on the Rendering
control path. Dropping a diagnostic report cannot abandon GPU work. Pipeline
creation's successful owning result remains separate from execution-fixture final
reports and their completed cleanup requirement.

## Inputs, outputs and validation

| Concern | Initial scope |
| --- | --- |
| Discovery | Engine-owned audit results with identities, supported/current modes, capability facts and query outcomes. Define snapshot validity and presentation-specific queries during refinement. |
| Components | Explicit engine-produced vertex/fragment or compute artifacts and successful reflection results. Reuse existing compilation/conditioning; no catalogue file or external-binary import is required. |
| State | Explicit supported recipes and a compatible Rendering-owned device/backend context. Reject unsupported state and stages. |
| Physical checks | Selected entry/stage agreement, graphics connections/output compatibility, compute buffer/dispatch requirements and relevant device/format limits. Define exact supported rules during refinement. |
| Resource layout | Graphics has an empty application-resource layout. Compute admits only its reviewed explicit buffer case. Unsupported resource classes fail visibly; no automatic binding convention is inferred. |
| Success | Separately identifiable audit, owning pipeline, graphics presentation and verified compute results, with recipe/component associations and required dependencies. |
| Failure | Identifiable discovery, setup, compilation/reflection, interface/state/device, native creation, submission, presentation or result-verification failure. No partially usable PSO result; retain available upstream diagnostics and completed audit facts. |

No new public PSO-library authoring grammar is needed to prove this path. An initial
Rendering-local request can use explicit engine types. Its exact API is a stage-design
decision. Do not expose vendor handles/types to Executive or Host contracts.

This is a physical pipeline and execution facility. Semantic coverage/provider
satisfaction are not checked yet and must not be reported as successful. Later semantic conditioning
can consume the physical result through an explicit interface without pretending
that low-level PSO creation implements the final production-library rules.

## Prerequisites and ownership

Rendering currently has compile/reflection and composed job/report facilities;
native Vulkan device/pipeline creation is not supplied by those facilities. The
stage must explicitly account for backend/device setup, execution and teardown.
Inspect existing dependency/build support before choosing loader integration or
adding infrastructure.

Rendering owns GPU capability queries, device selection, native pipelines, surfaces,
swapchains, command resources and GPU synchronisation. Platform owns native display
information, windows and window transitions; shared contracts use engine-owned data.
Refine their cooperation against the [implemented window contract](platform_first_window.md)
and [presentation ownership](platform_host_pumped_design.md#presentation-state-and-monitor-selection).
The current window path does not already supply a complete display audit or
presentation implementation. Identify any needed Platform/Framework extension
explicitly and return material ownership/interface changes to the coordinator.

Select a supported device through the agreed policy above. Device support,
surface-specific presentation support and availability of a usable display are
separate facts. Unsupported or unavailable cases produce clear results.

Prefer a serial Rendering-owned control path initially. Retain borrowed inputs through
their use and make allocator/module/device/window dependencies explicit. Submitted
GPU work must complete or reach a defined failure/retirement outcome before referenced
resources are reclaimed. Release graphics resources before releasing the borrowed
Platform window. Define partial-startup cleanup, close/shutdown ordering and device
failure behaviour. Existing Host/module/window lifetime facilities remain the starting
point; a generic observation service or asynchronous provider lifecycle is not an
implicit dependency.

Use existing Host asset/file services for sources, fixtures and the requested saved
audit baseline. In-memory engine-produced inputs can isolate PSO logic during
verification. The audit-report format/save/load contract is now part of focused
refinement. Saving compiled PSO libraries remains stage 4 work unless separately
selected; the audit-baseline request does not authorise per-PSO persistence.

## Agreed validation and acceptance gates for prerequisites

The user accepted these prerequisite validation and acceptance criteria on
16 November 2026. They describe future work, not evidence of tests already run.
Accept the schema extension independently,
then accept Platform reports using it, before beginning Rendering 1A to 1C.
Do not infer either prerequisite's success from a later visible triangle or from
one happy-path native capture. Keep implementation/review/acceptance evidence for
each piece separately, under the already agreed dependency order.

### Schema extension acceptance

| Area | Required evidence |
| --- | --- |
| Declaration/value contract | Accept the agreed text/span syntax, nullability, explicit absent/empty forms and permissible element types. Reject invalid/unknown properties, non-storage targets and malformed values. Preserve existing fixed-array syntax, defaults, positional null inheritance holes and explicit instance descendant policies. |
| Text semantics | Preserve non-ASCII text, exact case/combining content and logical U+0000 through the existing canonical string encoding. Distinguish absent from present-empty; reject malformed text without normalising unrelated content. Exercise repeated strings, bulk-only text and effective inherited/defaulted text. |
| Physical contract | Independent compiler/layout checks establish four-byte text, twelve-byte span, four-byte alignment and u32 count on Win32/x64. Independently constructed raw fixtures exercise the extended storage with its final version-1 marker, exact extents/alignment, malformed IDs/payloads and required retained-text/directory metadata. Do not rely only on writing and rereading through the same implementation. |
| Retention/remapping | Construct source documents with deliberately different live/baked ID orders and conflicting numeric IDs across documents. Bake, promote, subset-copy, repack and inspect/import; assert logical text and span targets, not incidental equality of IDs. Whole-block copies preserve their exact reference context. No copied result borrows source backing after completion. |
| Span dependency retention | Cover nested acyclic dependencies, shared targets and overlapping ranges; retain complete referenced collections once, preserving order/first/count. Reject cycles, missing/wrong-type targets, invalid counts/indices and wider-arithmetic overflow. Exercise zero-byte element/count boundaries without allocating enormous buffers. |
| Mutation transactions | Exercise typed writes, explicit absent/empty, create/replace/rename/erase, coordinated target replacement and both instance descendant policies. Invalid command/commit preserves source/output and the prior candidate as specified. Test stale/foreign edit handles, budget exhaustion without resets, cancellation before commit and no implicit commit on destruction. |
| Runtime-byte fidelity | Reference-aware copying preserves non-reference bytes, padding, selections and existing raw encodings that lack a faithful literal representation. Remap only declared reference fields. Reject unbound raw capture/direct writable/reference-copy paths; reference-free operations retain their established behaviour. |
| Limits and failure ownership | Targeted boundary tests use small configured allowances for every limit and representative allocation-failure sweeps across retention, maps, candidate building and commit. Failed operations publish no partial owner; references, attribution and source protections balance. Diagnostics remain valid after private staging is discarded. |

Deliver the canonical text/span declarations, runtime format/API documentation and
focused regression coverage alongside the implementation. Include a standalone
reader/round-trip fixture using Foundation/Framework only, with no Platform native
types or module execution. Generic inspection reconstructs strings and typed named
collection ranges; it must not expose a byte-pool convention as the logical value.

Keep version policy explicit. Temporary development versions are allowed, but schema
prerequisite completion requires the affected final marker set to 1, a coordinated
rebuild, regenerated current data and removal of affected legacy code/data. V2
reading is not a prerequisite and the earlier V3 proposal is not the final release
marker. Validate the extended version-1 format and clear rejection of unsupported
versions/layouts, including structurally invalid historical data with a reused
version number. Reference-free behaviour remains covered using the current format.
Schema/report format numbers do not mean the product has reached release version 1.
The generic baked-document encoding remains unchanged by this schema extension.

### Platform report portable and Host integration acceptance

Use a private injectable native-query adapter and deterministic clocks, following
existing Platform test seams. Injection is confined to test fixtures, not a public
fake-report/native-command mode or new production configuration. Required coverage:

- Build normal, zero-display and recognised unsupported/session-unavailable reports
  with the exact V1 schema. Check required header/type/enum/mask meanings, capture
  identity, query subjects, native payload tags/lengths, catalogue ownership,
  rational/integer precision, signed positions and desktop/signal separation.
  Include cloning, inactive available targets and active/unavailable transitions.
- Exercise optional labels/primary/mode failures, blocked dependencies, ambiguous
  output/source association, native unknown codes, successful zero-mode results and
  the defined GDI fallback. Check publication classification and explicit unknowns;
  no inherited old facts, invented physical-target links or Cartesian mode products.
- Force all flag-profile and buffer-growth paths, exact retry boundaries and final
  topology-check restarts. Use small configured native caps to test the termination
  probe, pre-allocation rejection, aggregate/raw-before-dedup limits and non-resetting
  cumulative budget. Core retry exhaustion and every global limit reject publication;
  optional query exhaustion follows the agreed enrichment/fallback policy.
- Advance a fake awake clock around native calls and Host handoff. Check restriction
  pauses, counted native time, shared deadline, post-call expiry and no publication
  after timeout. Never use forced thread termination as a successful cleanup test.
- Use real Host/pumped-module routing, ownership transport and acquisitions with two
  authenticated non-Rendering consumers. Initial read is not-ready, current reads
  trigger no capture, updates while busy are declined, and successful refresh returns
  the exact newly published acquisition while old acquisitions remain readable.
- Cover producer failure, Host copy/validation/allocation rejection, queue/reply
  backpressure, receipt handling, requester departure, duplicate/stale/foreign IDs,
  late outcomes after reload and failure at each handoff boundary. Prove one producer
  outcome, correct receipt disposition, reserved-credit balance and no double release
  or hidden retry after ambiguous delivery. Poisoned transport tests retain any
  potentially exposed backing under the existing exceptional rules.
- Hold a historical report acquisition across Platform unload/reload. Current report
  becomes unavailable, retained Host bytes remain safe without Platform allocator/code,
  reload requires new capture, and obsolete completions cannot republish old state.
  A rejected/in-use unload request must not pretend the installed instance vanished.
  Test pending capture cancellation and orderly shutdown with handoff/receipt work
  outstanding, preserving resources until consumed-prefix/terminal-drain proof.
- Verify no report operation creates a window, changes display modes, invokes Vulkan,
  starts Rendering or persists the combined Rendering baseline. Report failure does
  not fail Platform installation; independent existing window/input facilities keep
  their lifecycle behaviour.

Prove producer-envelope allocator/module dependencies end after Host copy and
handoff drain, while Host reader backing outlives Platform independently. Inspecting
only reference counts is insufficient: dispose the source context where safe and
continue reading/releasing the retained result through Host-owned code. Preserve
existing in-use window/input/module-unload checks; a Host report acquisition is
not silently turned into a native Platform borrow.

### Native Windows qualification and timing evidence

Run a real Windows x64 capture through actual Host/Platform DLL activation and
the public current/update interface, without Rendering. Retain the exact schema
block plus reconstructed inspection JSON, query profiles/outcomes/counters, OS/
session context and selected actual limits in isolated test artifacts. Compare a
sample of topology/current geometry/mode facts with independently observed native
values or Windows display settings, accounting for UI rounding and query scope.
The evidence establishes faithful API reporting, not advance capability execution.

Exercise startup, repeated reads, explicit refresh, held historical readers,
unload/reload and shutdown natively. Also run capture/refresh with an existing
Platform-owned window/input session to measure responsiveness; discovery itself
must not require that window. Use available multi-display/clone/inactive setups
where present; otherwise mark those cases synthetic-only or unobserved. Do not
automatically hot-plug devices, change desktop modes or induce driver failures.
Known remote/session restrictions may be synthetic unless that native environment
is actually available. Missing environments never count as native coverage.

Record cold/warm native-call durations, overall eligible capture time, schema build,
Host copy/validation/adoption/retirement time, peak temporary storage and maximum
capture-related owner-pump stalls. Deterministic tests establish one-native-call per
pump and the cooperative bookkeeping allowance; native measurements disclose entered
calls/synchronous phases that overrun the target. A successful five-second deadline
alone is not responsiveness evidence. Repeated normal processing cost exceeding the
available pump budget blocks unqualified acceptance of that placement: revise the
specific stage using measurements or obtain an explicit documented acceptance of
the observed latency tradeoff. Do not silently require a general conditioning or
resumable-schema subsystem instead of assessing the concrete cost.

### Validation configurations and completion gate

Initial qualification comprises focused Windows x64 Debug tests and native runs,
Win32 Debug generated-layout/reference-format and affected portable/Host tests,
plus an x64 Release smoke of schema round trip and real report lifecycle. Portable
Foundation/Framework tests also join the existing Linux GCC Debug path; no native
Linux display backend is implied. Exercise Host integration with one worker and
the normal supported worker configuration to detect hidden conditioning-thread or
startup-order assumptions; repeat native scenarios only where that distinction
affects execution. Ordinary/headless CI uses injected queries and requires no
physical displays, GPU runtime, visible window or Vulkan validation tools.

Run affected existing data-model/schema/ownership/Host/Platform suites and required
repository source-policy/build/line-ending checks once for the changed surfaces.
Broaden or repeat only for a new change, failure or unresolved concern. Canonical
schema artifacts, shared message registration/build integration and consumer/layout
checks form part of delivery; no test-only declarations substitute for the shared
production contract. Exact runner commands are implementation artifacts to record
with evidence, not new flags claimed to exist already.

For each prerequisite, acceptance requires its reviewed implementation, passing
applicable focused/regression checks, explicit native-versus-injected coverage and
unresolved limitations, balanced failure/lifetime evidence, and the documented
timing/placement decision. The schema gate uses portable report-shaped fixtures
and compiler/layout evidence; it does not wait for the Platform native implementation.
Native Windows capture qualification belongs to the subsequent Platform-report gate.
Schema acceptance precedes use by the Platform report;
both accepted prerequisites precede Rendering 1A. This plan does not authorise an
implementing task, builds/runtime tests, commits or subsequent stages.

## Agreed Rendering acceptance criteria and validation plan

The user accepted this plan on 16 November 2026. These are future validation
requirements, not evidence of tests already run. Audit, graphics and compute have
separate acceptance gates, with compute retaining its separate implementation,
review and acceptance checkpoint.

| Increment | Required evidence |
| --- | --- |
| 1A: audit | Report agreed device/display fields, mode combinations, query coverage and partial outcomes. Preserve API claims separately from later requests and demonstrate useful results with presentation unavailable. |
| 1B: graphics | Create/destroy the pipeline, repeatedly render/present the visible triangle and gradient, and retire safely. Exercise resize, minimise/restore, ordinary close and Host shutdown through the real Host/Platform path. |
| 1C: compute | Run without creating a window, surface or swapchain. Read all 256 values, compare exactly, report zero mismatches and complete cleanup. |

Focused portable automated coverage includes:

- Selection ordering, explicit-selection errors and eligible/ineligible/unresolved
  outcomes.
- Valid stage matching, deliberate missing/incompatible connections, unexpected
  resources and incorrect compute layout/dispatch requirements.
- Resize coalescing, acquire timeout, out-of-date/suboptimal handling and close
  authority.
- Representative construction, submission, presentation and readback failures,
  including operations remaining outstanding after an error.
- Cancellation before/after submission, partial-startup cleanup, report retention
  and stalled cleanup without premature reclamation.
- Compute mismatch reporting, including unchanged sentinel values.

These are deliberate acceptance fixtures, not an attempt to exercise every
advertised capability. Label injected outcomes explicitly; they do not establish
real device execution.

Initial native qualification targets Windows x64:

- Debug runs with Vulkan validation and synchronisation validation enabled.
- A Release smoke covering audit, graphics presentation/retirement and independent
  compute verification.
- Visual confirmation or captured-window evidence for the triangle. Successful
  presentation calls alone do not establish visibility.
- Evidence recording device/driver, enabled capabilities, selected formats/queues/
  memory properties, completed checkpoints and cleanup outcome.

Give shared/separate queue families, coherent/non-coherent memory and unavailable
display cases individual coverage labels. Exercise native variants where available;
otherwise record focused synthetic coverage and the remaining native limitation.
Do not force device loss or change display modes for acceptance.

Portable tests join the existing supported build/CI paths. GPU/visible-window
runners remain explicit opt-in, matching the existing
[native acceptance arrangement](../project/building_and_testing.md#acceptance-and-lifecycle-harnesses).
Missing runtime, extensions or validation tools identify an unmet qualification
requirement, never a passing result. Exact runner entry points/commands are part of
the remaining fixture contract; do not present new flags as already implemented.

The following cross-cutting acceptance requirements remain applicable:

- 1A reports the discovered devices, selected capability facts, attached displays,
  current configuration and supported mode combinations, including query limitations.
  An absent display does not turn all discovery into an unexplained failure.
- 1B creates/destroys the representative graphics PSO and renders/presents the triangle
  on a supported real device through a Platform-owned window. Native creation and
  actual presentation evidence are recorded separately.
- 1C creates/destroys a compute pipeline, dispatches the selected calculation and
  verifies read-back results. Its acceptance can run independently of presentation.
- The inter-stage matching path accepts the supported pair and rejects deliberate
  missing/incompatible connections before native creation where sufficient facts exist.
- Invalid state, unexpected resources and unsupported capabilities have clear outcomes.
- Failed construction publishes no usable PSO and releases newly owned dependencies
  when their lifetimes permit; it does not damage another independently owned PSO.
  Execution teardown respects GPU completion and Platform window-use lifetimes.
- Compilation/reflection diagnostics remain available when later validation fails.
- Verification records its device/backend environment and which checks actually ran.
  An unavailable runtime is reported as unverified, never a passing device test.

No build/runtime validation has been run during this focused design discussion.
Do not repeat passing matrices without a new change or unresolved concern.

## Formalised version handling and release boundary

On 17 November 2026 the user requested formalisation of their pre-release policy
and its addition to [repository instructions](../../AGENTS.md#versioning-and-compatibility).
Product release version 1 has not been reached. The policy applies to engine-owned
interfaces, module/message contracts, schemas and stored formats; external platform
and API support requirements retain their separate agreed scope.

Versioning mechanisms are always allowed. The user further clarified on
17 November 2026 that temporary versioning may be used within a task, but the task
is not complete until everything affected has been rebuilt with current code,
affected version markers are set to 1, and legacy code/data have been removed and
the data rebuilt. Retain the mechanism, not legacy implementations at completion.
This applies before product release 1; it does not declare that release reached.

Support the current contract and coordinate affected producers, consumers and data
when it changes. Historical pre-release formats and interfaces need no ongoing
compatibility reader, shim, dual-write mode or permanent migration framework.
Temporary transition handling stays within the task and is removed before its
completion. This permits deliberate breaking changes within the authorised work;
it does not require unrelated cleanup of still-used interfaces or authorise
implementing this design.

Use the following concrete rules:

| Boundary | Pre-release handling |
| --- | --- |
| Source APIs and module/message contracts | Update affected callers/providers/shared declarations; remove legacy paths, reset the affected version to 1 and rebuild the engine and affected consumers with final code. |
| Schemas and binary/file layouts | Retain identification/version mechanisms. Temporary development markers may distinguish changing layouts; completion uses affected marker 1 with current-only code/data. Validate the actual schema/layout as well as the marker. |
| Maintained fixtures, examples and authored configuration/data | Update them, generators and expected results to the final contract; remove maintained legacy artifacts and regenerate current outputs with the rebuilt code. |
| Rebuildable caches, generated output and discovery baselines | Remove affected legacy data and regenerate through the current construction/discovery path before the task is complete. The runtime mismatch/fresh-discovery failure policy remains useful; a plan to regenerate later does not satisfy implementation completion. |
| Required retained information | Preserve the information through an explicitly scoped conversion where necessary, then keep the current representation and remove obsolete task-scoped data/transition scaffolding. No permanent migration service is implied. |
| Validation | Exercise the final version-1 code and regenerated data end to end. Construct negative unsupported-version/layout cases without maintaining a legacy-format dataset or support implementation solely for those tests. |

For these prerequisites, the extended schema data-block representation finishes
with marker 1 and no V2 reader. The earlier V3 marker may be temporary only. Platform
report schema version 1 and the schema data-block marker 1 still identify different
contracts; matching numbers do not merge their meaning. The generic baked-document
container and module/interface markers remain separate boundaries; apply the reset
to affected task contracts, not unrelated external/API versions or unrelated code.
The generic baked-document encoding is unchanged by the extension.

The implementation completion order is: remove legacy/temporary code, set affected
markers to 1, perform the coordinated rebuild, remove/regenerate affected legacy
data with the rebuilt code, and validate the final result. Verify that maintained
artifacts and consumers use the final contract and that no task-scoped legacy code
or data remains. Actual rebuilt/regenerated evidence is required. Version 1 alone
does not prove historical bytes compatible, especially after a reset; full current
schema/reference validation continues to reject incompatible structure.

Before product release 1 ships, explicitly document the supported public API/ABI
and persisted-data versions, compatibility guarantees, deprecation process and
preservation/upgrade paths. From that release onward, published contracts and data
retention commitments govern changes. Breaking changes need explicit versioning
and consumer/data transition handling; this does not promise every historical
version will remain supported indefinitely. The exact release numbering/support
windows can be selected for that release rather than inventing them now.

The policy is recorded in root `AGENTS.md` for subsequent repository work. This
formalisation changes instructions and design documentation only; it does not
start prerequisite implementation, regenerate files or authorise a commit.

## Review status and remaining decisions

The overarching coordinator completed a read-only design review on 17 November
2026 and found no blocking issues in the two prerequisites. The schema extension,
Platform report schema/mechanism, native profile, capacity/retry policy, initial
placement, version handling and prerequisite acceptance gates are design-approved
for separately authorised implementation and review. Rendering 1A to 1C is
architecturally consistent; its complete implementation handoff remains open.

Carry these coordinator conditions into the prerequisite implementation handoffs:

- The overarching coordinator confirmed plan reconciliation complete on 17 November
  2026. The [prerequisite order and review gates](../backlog/shader_system_stages.md#stage-1-prerequisite-order-and-review-gates),
  [audit-baseline persistence boundary](../backlog/shader_system_stages.md#stage-1a-audit-baseline-persistence-boundary)
  and [current handoff](../backlog/shader_system_stages.md#current-handoff) now record
  the agreed order and persistence scope. No additional coordinator design review
  is needed before starting the unchanged, separately authorised schema scope.
  Platform report implementation remains gated on schema review and acceptance;
  the remaining Rendering decisions do not block either prerequisite.
- Preserve the measured timing/placement gate. Owner-pump execution is provisional
  until qualified; the five-second capture deadline does not establish acceptable
  responsiveness. Assess the concrete costly phase before adding offloading.
- Apply the current AGENTS.md pre-release completion policy to affected contracts,
  consumers and maintained artifacts. Reset affected markers to version 1 before
  the completion rebuild, regenerate affected data with rebuilt code, remove legacy
  and transition support, and validate the final current format. Retain explicit
  layout checks; unrelated formats remain outside scope.
- Implement and review the missing integration explicitly: the document baker's
  existing string-ID mappings must be exposed for schema relocation, while existing
  byte-copy schema paths must be extended or restricted for reference-bearing data.
  Host acquisition and pumped transport support exists; concrete report publication,
  reservation and retirement handlers remain implementation work.

Exact C++ signatures, message registrations, allocator/accounting integration and
measured execution costs remain implementation/review work under those agreements.
No additional generic framework is requested. The review edited no files, ran no
builds/runtime tests and created no implementation chats. The user subsequently
authorised the schema implementation chat recorded above. Other implementation
and chat creation still require explicit user instruction.

### Schema prerequisite implementation review and acceptance

The focused coordinator reviewed the delivered text/span extension and the user's
subsequently authorised six-stage consolidation. The schema prerequisite is
accepted for its agreed scope, with no outstanding actionable review findings.
This clears its dependency gate for later, separately authorised Platform report
work; it does not accept Platform capture placement or authorise its implementation.

Review covered the delivered contract, reference retention/relocation and copy
closure, checked views, transactional edit/publication, operation accounting,
allocator lifetimes, reference-free boundaries, format/artifact inventory and
saved qualification. Both generic bake overloads and direct reference-free paths
remain. The final schema marker is 1, the unrelated generic document marker is 5,
and no transitional schema reader/writer remains in scope.

The coordinator independently verified the 559-file final source/test/build hash
manifest and 40 saved cross-configuration artifact/independent-reader comparisons,
and inspected the Windows x64 Debug, Win32 Debug, x64 Release and Linux GCC Debug
build/test, compiler-layout and header evidence. The qualified broad matrix was
reused rather than rerun. See the [consolidation qualification](../schema/consolidation-validation.md)
and [original extension record](../schema/reference-extension-validation.md).

One review finding was corrected before clearance: invalid operation limits could
make edit-session setup fail with no operation reason. The analogous bounded bake
gate had the same gap. The implementation now records `invalid_input` at those
validity checks, preserving earlier failure precedence and cleanup. New tests
reproduced 21 diagnostic failures before the fix; the final focused reference suite
passed 4,127 checks with zero failures on Windows x64 Debug and Linux GCC Debug.
The coordinator re-reviewed the fix/tests and verified the updated 559-file
manifest, whose changes are limited to two production files and one test file.
Detailed commands, hashes and logs are in `build/schema-coordinator-review/`.

Acceptance preserves the placement caveat: standalone large-fixture Release bake
samples take about 17.8–19.1 ms and copy about 8.3 ms, exceeding the default 2 ms
pump allowance. These are observations, not latency guarantees or a production
Platform report measurement. Qualify the concrete Platform phases and choose or
explicitly accept their placement/timing at that prerequisite's gate.

The user authorised the implementing chat to commit all outstanding uncommitted
files after coordinator review. Initial delivery was committed as `3bf428d`.
The subsequently authorised navigation/filter follow-ups and deferred Host
responsiveness backlog entry passed coordinator review and were committed as
`fc3c957`. Independent comparison established that all 78 affected C++ files
changed only in include paths across 62 moves, with all 20 public schema headers
retained at the root. This follow-up changed no algorithms, API shapes, ownership,
stored layouts or markers. Neither commit was pushed by the agents; the user
performs pushes. Schema acceptance remains valid, including its placement caveat.

### Platform report implementation handoff

On 18 November 2026 the user explicitly requested the Host-owned Platform report
implementation chat using Astra at Extra-High. Chat
`01a1ee5c-4046-7da3-bf87-14bc68702fdd` implements the agreed prerequisite in the
shared checkout, starting from `fc3c957`, with this focused design chat
(`01a1e419-b2d2-7f10-ade3-921130d6026c`) as its implementation-review coordinator.
The schema prerequisite's acceptance gate is satisfied.

The authorised scope includes the production Framework schema/messages, Platform
native capture, Host admission/ownership/publication and lifecycle integration,
build integration, documentation, applicable portable/Host/native validation and
timing evidence. The agreed first-pass Windows query set, complete-report updates,
limits, handoff/receipt protocol and independent reader lifetimes remain binding.
The [Host responsiveness backlog](../backlog/engine_backlog.md#host-responsiveness-and-workload-placement)
does not select instrumentation, thresholds, automatic migration or a new placement.
Native qualification and the measured execution-placement decision remain part of
this prerequisite's independent review/acceptance gate.

This implementation authority does not authorise Rendering work, additional
implementation chats, commits or pushes. Substantive contract/placement changes
require discussion; the implementing chat delivers review-ready evidence before
claiming independent acceptance. The top-level design/review record remains with
this focused coordinator.

The independent first-pass review is recorded in
[Platform report coordinator review](platform_report_review.md). Acceptance remains
open for the observed placement/deadline qualification and follow-up integration.
The confirmed query-outcome/known-fact finding F1 is corrected and independently
reviewed for the user-authorised checkpoint. The user agreed the compact inventory
and generic Host service contract below. The known-field packing concern was
resolved: those flags already occupy one mask. Native capture thread ownership,
resource lifetime and whether an extra thread is appropriate remain open.

### Host schema services placement decision

On 18 November 2026 the user settled schema baking as a generic Host service,
available to any module and executed through the Host conditioning thread. Platform
transfers ownership of its authored schema input to Host for baking. This supersedes
the initial local Platform bake and already-baked candidate handoff assumptions;
it does not select a thread for native capture or authorise a report-specific Host
construction service.

Platform retains responsibility for the meaning of the report, including which
observations become records, their fields, relationships and query provenance.
Host's generic schema service understands schema structure, types, references,
limits and runtime representation. It must perform the complete schema bake,
including typed payload construction and reference remapping; the existing generic
document bake alone does not provide that result.

The user also identified schema merging as a reasonable generic Host service.
Composition/merging can combine module-supplied declarations and authored data
without Host knowing display-report construction rules. Its concrete inputs,
conflict policy, reuse of static declarations and whether composition and baking
form one conditioning operation remain design decisions. No change to the report's
self-contained stored representation is implied.

The concrete service and revised capture handoff still require design: accepted
ownership transfer, result delivery/acquisition, bounded admission and diagnostics,
and cancellation/unload/drain must retain inputs and results through worker
retirement. The existing capture identity, deadline, independent report validation
and owner-thread publication requirements continue to apply. Generic schema
validation/baking and report-specific semantic admission are distinct operations;
the former does not replace the latter. Moving bake work does not settle placement
or cost of Platform's remaining record construction.

The user requires a dedicated implementation chat for extending generic
Host-provided services. Settle its service contract and scope before opening that
chat; do not fold that implementation into the Platform report task. No new chat
or implementation work is started by this design record.

The user also reopened whether the Platform report captures more information than
the first pass needs. Review full available-mode enumeration, its two native colour
formats and interlaced/scaled inclusion, alongside output-less adapter diagnostics.
Distinguish removing observations from sharing repeated representation. Retain the
existing coverage until a revised scope is agreed; a smaller report is not selected
merely by identifying these candidates. Current display state, available inactive
targets, display-owner identities and query provenance remain relevant to the
agreed discovery-snapshot purpose independently of the graphics proof.

### Cross-platform discovery source preference

On 18 November 2026 the user stated a general preference for Vulkan as a data
source where reasonable, to reuse work across platforms. Initial targets are
Windows and Linux, including SteamOS; secondary targets are macOS and Quest 3 or
equivalent Android devices; Nintendo Switch 2 is a longer-term target. This guides
source selection without expanding the current Windows prerequisite into an
implementation of all backends.

The user subsequently agreed that fixed platforms such as Quest 3 and Nintendo
Switch may use bespoke handling or manually populated information from available
documentation when support for each platform is added. Their requirements need
not force a universal discovery mechanism into the current pass. Preserve the
common platform-agnostic report model and identify documented/platform-supplied
facts by their actual source rather than presenting them as successful API queries.
Documented hardware properties do not establish mutable session state or the
capabilities of a particular attached external display. Concrete fixed-platform
providers, authored data and any required provenance representation are deferred
until those platforms are supported.

Public-source assessment on that date does not establish a representative driver
support percentage for `VK_KHR_display`. It is an optional display/mode enumeration
path, separate from the ordinary window-surface queries. Assess extension exposure,
the relevant displays and facts actually returned, and whether queries work
passively under the intended session. Enumeration and acquisition are distinct:
the [direct-mode extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_direct_mode_display.html)
explicitly permits enumeration of window-system-owned displays; inability to take
exclusive control does not by itself prove they cannot be queried. Discovery must
not acquire a display or alter desktop state merely to obtain an inventory.

| Target | Evidence and qualification needed |
| --- | --- |
| Windows | Vulkan defines optional WinRT display mapping/acquisition, but this is not evidence of cross-vendor extension availability or complete desktop coverage. Qualify passive enumeration on target drivers. [Windows display mapping](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetWinrtDisplayNV.html). |
| Linux / SteamOS | Vulkan has DRM display mapping that does not require DRM-master permission, distinct from acquisition that does. Access to the DRM device and driver/session behaviour still need qualification. Gamescope can expose a virtual screen to a game, so client-visible and physical-output inventories need explicit scope. [DRM mapping](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDrmDisplayEXT.html), [Gamescope](https://github.com/ValveSoftware/gamescope). |
| macOS | MoltenVK's current supported-extension list omits `VK_KHR_display` and uses Metal surfaces for presentation. Native display queries remain a candidate for desktop/mode facts. [MoltenVK runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md). |
| Quest / Android | For Quest XR, the runtime exposes supported refresh rates through OpenXR; ordinary Android has `Display.getSupportedModes`. These are appropriate alternatives to investigate rather than assuming direct-display Vulkan enumeration. XR view/render extents must not be relabelled as physical panel modes. [Meta refresh-rate queries](https://developers.meta.com/vr/documentation/native/android/mobile-display-refresh-rate/), [Android display modes](https://developer.android.com/reference/android/view/Display#getSupportedModes()). |
| Nintendo Switch 2 | Khronos publicly lists Nintendo Switch 2 as Vulkan 1.4 conformant, dated 3 May 2025. That establishes Vulkan support, not `VK_KHR_display` support or the concrete SDK integration. Check those separately when this target is pursued. [Khronos conformance registry](https://www.khronos.org/conformance/adopters/conformant-products/vulkan). |

The proposed next qualification is a small passive inventory probe on relevant
Windows drivers and Linux/SteamOS sessions, comparing native and Vulkan identities,
display coverage and mode facts without changing display configuration. It should
record explicit unsupported, denied, empty and failed outcomes. A positive extension
advertisement alone is insufficient evidence that native inventory work can be
removed. Exact source priorities, retained fields, cross-source comparison and
report integration remain design decisions; no concrete native query is removed
by the source preference alone. Report ownership and access remain shared through
Host independently of which module implements a Vulkan data provider.

### Platform report scope and condensation

On 18 November 2026 the user chose a conservative reduction: remove Platform
capture of texel/rasterisation-format information obtainable from Vulkan, and
condense retained information to eliminate duplication where possible. Keep the
display topology, current configuration and available timing-mode inventory.
Vulkan remains responsible for its resource-format features and rendering/surface
format support. The concrete reduced schema and native query profile were
subsequently accepted below; production integration remains outstanding.

The current Platform implementation does not query texture or render-target
feature tables. Its relevant format data consists of native desktop observations
and RGBA/BGRA-qualified DXGI mode catalogues. Native desktop depth/current pixel
configuration describes actual OS state, not Vulkan resource support; retain
that distinction rather than claiming Vulkan can replace those observations.

The initial reduction proposal, refined by the agreed layout below:

- Remove the second DXGI format enumeration performed solely for two-format
  coverage. Enumerate timing modes using one fixed native format where the API
  requires that argument, and retain it once with the query/catalogue provenance.
  The exact format and revised coverage description remain to be settled. The
  returned list stays explicitly scoped to that query; do not describe it as all
  modes across every format or as proof of Vulkan usability.
- Remove repeated per-mode RGBA/BGRA format values that can be derived from that
  catalogue qualifier. Do not claim this removes an existing Platform GPU-format
  capability table, since none is present.
- Store retained mode values in a capture-local shared pool. Catalogues reference
  their exact membership through compact indices; mode values need no catalogue
  back-reference. Share identical membership sequences where possible. This permits
  cross-catalogue reuse without falsely merging query scope or outcomes. Exact
  record fields, index representation and header rooting remain to be settled.
- Reuse identical text and identity payload storage where their type/context
  permits, while preserving distinct evidence records when query provenance
  differs. Inspect existing string interning before adding another deduplication
  mechanism. Catalogue-level qualifiers are stored once rather than on every mode.

Equality must include all retained values, known/unknown states, raw qualifiers and
refresh precision; rounded Hz or matching labels are insufficient. Never replace
exact catalogue membership with a union or a Cartesian product. Preserve distinct
desktop/signal observations, query outcomes, absent versus present-empty results
and identity evidence. Deduplication must fit existing capture/schema limits and
be measured for both representation size and authoring/bake cost; a byte reduction
alone is not proof of improved responsiveness.

The existing implementation and captured evidence still describe the initial
layout. Apply any agreed revision to producer, schema, validators, consumers,
fixtures, generated layouts and current data under the pre-release version policy.
Generic Host service extensions remain a separate implementation task.

#### Agreed native filter and compact mode layout

On 18 November 2026 the user accepted the native filter and compact layout below.
This supersedes the initial two-format mode profile, per-mode IDs/back-references
and prohibition on cross-catalogue range sharing. The user requested agreement on
the generic Host schema service contract before starting its dedicated
implementation chat. Its handoff is recorded below.

For the first Windows profile, enumerate one catalogue per output through
`GetDisplayModeList1`, using the bounded count/fetch sequence with
`DXGI_FORMAT_R8G8B8A8_UNORM` and
`DXGI_ENUM_MODES_INTERLACED | DXGI_ENUM_MODES_SCALING`. Do not request stereo,
disabled stereo or an additional BGRA catalogue. Retain the required format filter
and flags once per catalogue/query as provenance. This preserves the first of the
existing two queries and its timing/scan/scaling coverage; it claims only the modes
returned for that filter. A successful empty result remains known-empty, without
trying another format or declaring that no other display modes exist.
[DXGI filter contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-getdisplaymodelist1).

Retain the previously agreed restricted GDI fallback when the applicable DXGI
inventory/query is unavailable, unsupported or failed. Use `EnumDisplaySettingsExW`
with flags zero, preserving monitor/current-orientation filtering, valid `dmFields`,
integer refresh precision and source scope. Retain the DXGI outcome independently;
the fallback is not a cross-format support claim. Existing limits, finite retries,
final topology check and deadline still apply.
[GDI filter contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw).

The retained Release capture supplies a useful representation comparison. Selecting
only its RGBA catalogues leaves three lists containing 67, 75 and 648 rows. After
removing row ID, catalogue ID and channel format, all 790 retained values remain
distinct, including their known masks, native qualifiers and refresh components.
There is no additional cross-list value sharing in this sample. This is evidence
about that capture, not a claim about every future configuration.

Consequently, refine the earlier index-pool suggestion for the first pass: use one
shared mode-value collection with direct typed catalogue spans, and intern identical
complete lists. This reuses existing span relocation and removes an unnecessary
four-byte index per membership on this capture. Partial overlap between unequal
lists is not factored in this initial representation; reconsider value indices
if measured sharing justifies their storage and semantic-validation cost.

Agreed `PlatformDisplayModeValue`, natural four-byte alignment:

| Field | Storage | Meaning |
| --- | --- | --- |
| `known_fields` | u32 bit structure, 4 bytes | Bits 0..5: extent, refresh, depth, scan, scaling, rotation. |
| `extent` | `PlatformExtent`, 8 bytes | Width/height in physical pixels. |
| `refresh` | `PlatformRefresh`, 12 bytes | Precision and exact reported numerator/denominator. |
| `bits_per_pixel` | u32, 4 bytes | Native reported display depth, where obtained; not a Vulkan resource format. |
| `scan` | `PlatformScanValue`, 8 bytes | Normalised and raw native values. |
| `scaling` | `PlatformScalingValue`, 8 bytes | Normalised and raw native values. |
| `rotation` | `PlatformRotationValue`, 8 bytes | Normalised and raw native values. |

This is 52 bytes per value, without per-value IDs, catalogue back-references or
RGBA/BGRA fields. A value is interpreted through its catalogue and local ordinal;
it has no independent persistent identity. `PlatformModeCatalogue.modes` becomes
`Span<PlatformDisplayModeValue>?`; the remaining catalogue scope, identity, filter
and query fields are retained. Header roots remain unchanged: catalogues reach the
mode collection through typed spans. No separate membership collection is needed.

Canonicalise a list by exact retained value, remove exact duplicates, and sort it
deterministically. Share its complete range only with an equal canonical list in a
compatible native-code context, retaining separate catalogue/query records. Text
uses existing interning; identical typed identity-byte payload ranges may be shared
while evidence rows keep their own provenance. Distinct CCD/GDI current observations
remain distinct. Keep all references local to the complete self-contained report.

Host admission checks the revised layout, six-bit known mask, zero unknown payloads,
typed ranges, catalogue/query relationships and outcome/known-fact consistency.
Validate every catalogue association even when its mode range is shared. Known-empty
and absent remain distinct. Native returned counts and capture limits continue to
count observations before deduplication; sharing cannot hide excess query work or
reset memory/work allowances. Canonicalisation must remain bounded by those limits.

On the retained sample, direct mode values would occupy 41,080 payload bytes versus
107,440 initially, a 61.77% reduction for this part of the report. Values plus an
index membership layer would occupy 44,240 bytes with no further sharing in that
sample. These calculations exclude declarations, catalogue/query metadata, other
tables and staging; they are not a measured bake-time or whole-report saving.

### Agreed generic Host schema service contract

On 18 November 2026 the user accepted the following service contract and requested
agreement on concrete request/result schemas and ownership handoff before starting
its dedicated implementation chat. The generic service
is available to any authenticated module, and to Host's own consumers through the
same operation machinery. Host schedules Foundation/Host code on its conditioning
worker; a request never contains a module callback or report-construction function.

#### Operations and results

| Assembly operation | Inputs and semantics |
| --- | --- |
| Existing schema | One supplied schema document; preserve its contained sections. |
| Compose roles | Select at most one source for each of `types`, `instances` and `data`, from at most three input documents. The same source may supply multiple roles. A missing optional role is empty; final runtime output still requires valid contained types and references. There is no implicit search or last-writer precedence. |
| Merge two schemas | Two schema documents, using the existing schema merge compatibility/conflict rules. Reject incompatible definitions and conflicting values; do not invent a Host-specific overwrite policy. |

Choose the output explicitly: an immutable schema document, or a complete immutable
schema runtime data block. Composition/merging and optional runtime baking execute
as one atomic conditioning operation. Schema-document output supports reusable
declarations and further authoring; it is structurally admitted and does not assert
runtime readiness. Runtime output requires full schema validation and the complete
typed bake, including retained text, payloads and reference remapping. Generic
document encoding alone is not runtime baking. Neither output implies Platform
semantic validation, public report publication or file saving.

#### Inputs, ownership and reuse

An input is either an owning transferred `CLiveDocument`, or the identity of an
immutable Host-retained schema document. Host pins retained sources through worker
retirement; accepted owning transfer leaves no caller mutation/borrow. A rejected
delivered envelope is retired through normal ownership transport, not returned as
an implicitly usable caller document. A post that never transfers ownership leaves
the caller's owner intact. Exact admission and terminal outcomes use existing
authenticated async correlation and owning-message settlement.

Reuse a Host-retained declaration document by asset identity across captures. This
avoids repeatedly parsing/transporting the same declarations without making the
output dependent on an external dictionary. Each runtime result remains complete
and self-contained. Prepared/resolved-type caching may optimise this internally,
but the existing prepared-schema descriptor is not a drop-in reference-aware
baker, and the contract does not promise resolution occurs only once.

Transferred nested storage must be correctly attributed and have a valid allocator
and destruction lifetime. Host cannot unload a source module while a retained
allocator/deleter still depends on its code. Report assembly remains the provider's
responsibility; role composition does not interpret observations or generate report
records. The revised Platform handoff may submit its authored input to this service
internally in Host without returning a baked candidate to Platform first.

#### Output lifetime, limits and failure

Success returns a typed output-kind tag, Host asset identity, an already-granted
acquisition and the complete immutable byte range. Inputs may retire once worker
reads finish; output lifetime is independent of producer lifetime. Acquisition
release ends the borrow; asset disposal is separate, using existing asset rules.
Host's report service may retain the resulting backing for semantic admission and
publication. A failed operation exposes no partial output and does not mutate a
retained source or previously published result.

Allow at most four in-flight operations for this new service, including queued,
executing, pending reply and operation retirement. Published retained assets use
the existing asset ownership/disposal limits rather than permanently occupying an
execution slot. Reserve terminal outcome/acquisition capacity before worker
dispatch. Defaults follow `SDataOperationLimits`: 64 MiB inspected input, 64 MiB
simultaneous owned storage, 16 MiB result, 4,194,304 work credits, 262,144 document
nodes, 262,144 records and depth 256. Callers may tighten them. Count all inputs,
composition/merge intermediates, resolution, baking and retirement consistently;
do not grant fresh allowances at each phase. Existing composition/merge entry
points need a bounded integration path rather than an assumption that bounded
baking alone covers their work. Limits are not a hard per-call time guarantee.

Optional caller deadlines use the existing eligible-time model, covering queueing
and processing without reset. The Platform capture retains its five-second
deadline; that report policy is not a universal schema-service timeout. Cancellation
or deadline expiry prevents publication and stops work at supported safe boundaries;
already-entered synchronous schema work may finish before retirement. Host retains
all accessed storage until worker acknowledgement or proved drain. No detached
worker, forced termination or timeout-based freeing of live state is permitted.

Return durable status/stage/reason, input/role attribution and available bounded
path/limit details, without borrowed schema handles or diagnostic strings. Distinguish
invalid input, conflict, limit, allocation, busy, cancellation, deadline and delivery
failure. Follow existing ambiguous-delivery and client-quiescence rules. Expensive
construction, validation, bake and normal reclamation belong on the conditioning
worker; Host's owner thread performs bounded admission, source protection, final
lifecycle/deadline checks and result publication. Qualify the existing one-worker
I/O/conditioning configuration as well as normal worker placement.

#### Agreed concrete messages and ownership handoff

On 18 November 2026 the user accepted the concrete request/result schema and
ownership handoff below and explicitly requested its dedicated implementation
chat using Astra at Extra-High. Use the existing
authenticated owning-request route to Host and POD replies to the requesting async
slot. Register shared Framework types and nested memory attribution; introduce no
module callbacks or new general reply transport. Preserve the existing
`SchemaPrepareRequest` service as its separately defined materialisation operation.

`SchemaProcessRequest` is an owning envelope with these fields:

| Field | Type and meaning |
| --- | --- |
| `contract_version` | u32, 1; reject unsupported values. |
| `request_id` | Strong u64 ID, nonzero and monotonically increasing for this authenticated client installation. Never reuse it; it grants no authority across clients or reloads. |
| `operation` | Enum: `existing`, `compose_roles`, `merge_two`. |
| `output_kind` | Enum: `schema_document`, `runtime_data_block`. |
| `input_count`, `inputs[3]` | Dense bounded input array; each tagged input contains either one owned `CLiveDocument` or an immutable retained schema-document `CAssetId`. Inactive alternatives/slots are empty. No external raw pointers or UTF-8 parsing requests. |
| `types_source`, `instances_source`, `data_source` | i32 input indices for role composition; -1 means absent. Reusing one input for several roles is valid. These selectors are -1 for existing/merge operations. |
| `limits` | Copied `SDataOperationLimits`, checked against service ceilings and shared across all phases. |
| `deadline` | Optional copied eligible-time anchor; absent means no caller deadline. Host validates supplied anchors and retains authoritative restriction/time accounting. A report-owned operation inherits the original capture deadline. |

Existing-schema operations require exactly one input; merging requires two;
composition permits up to three and requires at least one selected role. Reject
out-of-range selectors, unselected/extraneous inputs, noncanonical inactive fields,
inaccessible/disposal-pending assets and invalid retained input kinds. A retained
input is an immutable schema document, not an unvalidated runtime payload treated
as an interchangeable document. No implicit files, external declarations or
parser operation are added to this request.

Use three small POD messages on the same registered contract:

- `SchemaProcessAccepted`: request ID and contract version, on the original async
  slot. Host queues this after admission/source protection and before worker
  dispatch. Admission reserves both this reply and one terminal result. A rejected
  request receives only the terminal result.
- `SchemaProcessCancel`: request ID and contract version, authenticated to the
  original client. Send only after `Accepted`; a caller wishing to cancel sooner
  remembers that intent until admission is known. This prevents a POD cancellation
  overtaking the owning request. Cancellation has no separate result: the original
  terminal reply settles the operation. A result already published is not revoked.
- `SchemaProcessResult`: one terminal success/failure result on the original slot.
  Common fields are request ID (u64), contract version (u16, 1), status (u8), output
  kind (u8, `none` on failure) and reserved-zero u32, followed by a tagged body.

The success body contains `CAssetId`, `CAssetAcquisitionId` and a pointer to immutable
`SSchemaOutputV1`. The descriptor contains explicit size/version, output kind and
the complete `CByteConstView`, including appended runtime payloads. Its lifetime
and every byte it describes are covered by the returned acquisition. It is supplied
with the reply, requiring no second acquire/fetch request. Consumers check
size/version/kind before reading. The descriptor belongs to the retained backing,
not a recyclable operation slot; disposal with outstanding acquisitions preserves
both descriptor and bytes until those acquisitions retire. Destruction is Host-owned.

The failure body is an allocation-free, copied 32-byte diagnostic: stage (u8),
location kind (u8), reason (u16), input index (i8, -1 if unavailable), role (u8),
exceeded-limit kind (u8), reserved-zero u8, requested/allowed values (u64 each)
and two u32 location components. Location kind defines those components as source
line/column or record/member ordinals. Final field order/alignment must keep this
body at 32 bytes and the complete terminal reply within 48 bytes on Win32/x64.
Detailed copied paths and underlying Foundation diagnostics are recorded in Host
diagnostics under the authenticated request identity; no failure result borrows
source handles, staging text or an expired diagnostic descriptor. Output storage,
asset and acquisition are absent on failure. Status distinguishes success, invalid
request/source/schema, conflict, limit, allocation, busy, cancelled, expired and
delivery failure.

Ownership linearisation and retirement:

1. Before successful owning transport, the sender retains its envelope and may
   retry an explicit untransferred busy outcome. After transport takes ownership,
   the sender never reads, mutates, frees or retries that envelope, even if Host
   later rejects admission. Transport success is not service admission.
2. The caller keeps retained source assets alive until `Accepted` or rejection.
   Host performs bounded envelope/identity checks, reserves operation and reply
   capacity, and pins every retained source. Host rejection owns retirement of
   transferred inputs and produces one failure result. Use bounded transport and
   reclamation capacity; a busy result must not force large owner-thread teardown.
3. Host publishes `Accepted` before dispatch, then gives the worker exclusive
   access to address-stable input/budget storage. Cancellation state is synchronised
   separately; owner progress never mutates a document the worker is reading.
   Host owns source bindings and
   allocator/deleter hazards until acknowledgement; accepting the envelope or
   sending `Accepted` does not by itself make a source module safe to unload.
4. The worker composes/merges, validates and encodes/bakes, then retires input/scratch
   dependencies before reporting completion. Host rechecks cancellation, deadline
   and exact client identity, then publishes a result for that client alone.
   Failure/cancellation terminal results wait until worker access and input
   retirement are settled; they are not early permission to free worker inputs.
5. Success publication atomically establishes the asset/acquisition/descriptor.
   Definite undelivered results retire privately. Ambiguous publication retains
   potentially exposed backing through existing recipient-quiescence/drain proof;
   never retry or reclaim it on an assumed delivery failure. Client loss cannot
   retarget a replacement installation or invalidate another client's acquisition.
6. `AssetReleaseRequest` ends the acquired borrow after all reads cease;
   `AssetDisposeRequest` separately drops asset retention. Host may retain a result
   for report publication or reusable declarations without retaining an execution
   slot indefinitely. Normal heavy output retirement uses conditioning.

The agreed descriptor-based reply carries the existing full byte
view and asset/acquisition together within the POD ceiling. It introduces no
persisted external schema dependency and does not change byte-block format markers.
Compile-time message/descriptor checks and injected delivery/cancel/unload cases
must validate the final implementation on the supported architectures.

#### Platform checkpoint before the separate Host service implementation

The user authorised the Platform implementing chat to fix F1, obtain coordinator
review and then commit a checkpoint of the outstanding Platform work. F1 is now
corrected and independently reviewed. The checkpoint was committed on shared main
as `c818202c319c542f16cd948a2818996f23ff1ac8`, with 64 reviewed files and a clean
checkout, including submodules. Nothing was pushed. The known placement/Win32
deadline qualification and agreed compact layout/Host service integration remain
outstanding; this checkpoint is not full prerequisite acceptance and does not
authorise Rendering. The focused coordinator maintains the shared design and
review documents. The user has now agreed the concrete Host service contract and
handoff and requested its separate implementation chat.

#### Generic Host schema services implementation handoff

On 18 November 2026 the user explicitly requested the dedicated implementation
chat using Astra at Extra-High after accepting the service contract, concrete
messages and ownership handoff. Chat `01a1ef60-9f5f-73a3-9bbc-29a759689977`,
"Implement generic Host schema services", was created with `gpt-6.2-astra` and
`xhigh` reasoning. Work starts in the shared main checkout from
`c818202c319c542f16cd948a2818996f23ff1ac8` plus the coordinator's uncommitted
agreement/handoff documentation. The focused stage-1 design chat remains the
implementation-review coordinator.

Authorised scope is the generic Framework request/result/owner types, Host
admission/source protection/conditioning/publication/acquisition/retirement service,
necessary bounded Foundation composition/merge/bake integration, reusable immutable
schema-document assets, build integration, service documentation and applicable
portable/Host validation. Preserve the separate prepared-schema materialisation
service. Implement the agreed general service, not a Platform-specific worker or
an arbitrary callback/job facility.

The compact Platform filter/layout, conversion of Platform capture to an authored
input handoff, native capture execution placement, outstanding native deadline
qualification and Rendering remain follow-up work. The generic service must be
usable by authenticated non-Platform clients without depending on report startup.
Keep existing report behaviour covered by affected shared-route regressions.

The implementing chat delivers review-ready evidence before claiming independent
acceptance. No commit, push, branch/worktree, extra implementation chat or subagent
is authorised for this new task. The coordinator maintains this design and the
Platform review record; the implementing chat owns its service usage/validation
records and production changes. Substantive contract deviations require discussion;
routine names and implementation mechanics under the agreed contract do not.

#### Generic Host schema services independent review

On 18 November 2026 the focused coordinator completed independent review of the
uncommitted implementation from `c818202`, including the new Host source and both
service documents. No blocking findings were identified; the generic service is
independently accepted within its agreed scope. The
[coordinator review record](../schema/host-schema-processing-review.md) records
ownership/budget assessment, the inspected four-target and real-module evidence,
and the unresolved cause of the earlier incremental Release discrepancy. Current
Release qualification uses the successful clean test and Host rebuilds.

This acceptance does not authorise a commit or push. Compact Platform integration,
native placement/deadline qualification and Rendering remain follow-up work.

### Remaining Rendering decisions

The Rendering workloads, capability coverage, primary selection order, eligibility,
recipes, lifetime rules and acceptance plan are agreed. The complete Rendering
implementation handoff still needs these concrete decisions:

- Concrete save/load interfaces enforcing the agreed baseline format, comparison,
  adoption, identity and failure policies. Keep explicit re-audit separate from
  continuous monitoring and review its boundary to compiled-PSO persistence.
- Rendering's consumption of the Host Platform report and construction of its
  combined GPU/display audit, including the concrete combined-report/persistence
  representation. Keep Host report ownership, snapshot validity and Rendering's
  independent baseline/dependency assessment as agreed above.
- Vulkan header/loader/build/deployment integration, unavailable-runtime behaviour
  and validation-tool setup. The current Rendering build supplies compile/reflection
  dependencies but no native Vulkan loader/device integration.
- Rendering-local request/control/result interfaces and fixture activation: entry
  points for audit, creation-only checks, graphics and compute; window creator/
  controller, context ownership and exact native acceptance commands.
- Remaining Rendering operational defaults: same-preference device tie-breaking,
  Vulkan-side query/retry/capacity limits and finite GPU/acquisition waits/stall
  diagnostics. The Platform capture limits are already settled. Complete the
  detailed submit/present state accounting and synchronisation mapping under the
  agreed retirement/cancellation rules; routine implementation details do not
  require a new feature or general framework.

Record review status and unresolved decisions in this document. These questions do
not require designing the entire RHI or completing general structure binding first.

Catalogue packaging, general structure/data correspondence, semantics, providers,
additional shader stages, PSO persistence and Executive data integration remain
separate work. Record interfaces they will need, but avoid empty extension frameworks
without a consumer. No implementation, build/runtime validation or commit is
authorised by this scope update.

## Follow-up after the initial proof

The user requested a follow-up after increments 1A to 1C are complete, rather than
treating this initial proof as the complete device/display/execution system. Review
the deferred scope and qualification gaps with the user and overarching coordinator,
then select further increments explicitly. This follow-up checkpoint does not
authorise implementing all deferred features or automatically proceeding to them.

The follow-up inventory includes:

- VRR capability/range reporting and detailed HDR/colour capability reporting.
- A separately reviewed retirement path when swapchain-maintenance presentation
  fences are unavailable, including its actual guarantees and qualification.
- Device-loss recovery, surface recreation and any related re-selection policy.
- Continuous display/topology observation and display-disconnection recovery.
- Additional presentation modes, explicit output selection and multiple windows.
- Staging/device-local readback paths and broader resource/binding cases, coordinated
  with stage 2 rather than inferring a general binding system from this fixture.
- Broader native backend/device/driver qualification and native coverage gaps recorded
  during the initial acceptance runs.

Keep these items distinct from unresolved first-pass contracts above. In particular,
omitting device-loss recovery does not omit safe first-pass failure reporting and
cleanup, and deferring VRR detail does not erase separately reported current virtual/
physical refresh facts where queried. General catalogue, semantics, providers and
persistence work retain their existing separate stage boundaries.
