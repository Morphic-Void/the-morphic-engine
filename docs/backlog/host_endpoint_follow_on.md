# Host and endpoint follow-on work

Reviewed 21 November 2026. The selected Host refactor is complete: common Host
endpoints and module lifecycle in `ca8770d`, followed by direct POD endpoint
connections in `84506a7`. No next implementation is selected. This is the backlog
for the remaining work captured during those discussions, not a requirement to
deliver one larger redesign.

Current behaviour belongs in the [Host foundation](../modules/host_pump_foundation.md),
[direct POD](../modules/direct_pod_connections.md) and
[bulk](../modules/direct_bulk_connections.md) references. The
[completion record](../project/completed_milestones.md#common-host-endpoints-and-direct-pod-connections)
records delivered scope. The [broader design](../modules/common_endpoint_lifecycle_design.md)
retains the detailed decisions for the deferred contracts below.

## Generic provider registration for input and reports

**Deferred design and implementation.** Reduce Host knowledge of module-specific
resources and workflows. Platform should register its facilities through generic
Host mechanisms; consumers should request or query those facilities without Host
understanding HID pipe or Platform report semantics. Common endpoint traffic and
direct connections provide communication, but do not implement that registry.

Select a small registration/lifetime contract first: provider identity, the public
identifier used by consumers, publication/withdrawal, supported queries and the
ownership and retirement of exposed resources. Reuse exact endpoint identities,
service routes, borrows and notifications where they already supply the required
guarantees. Decide whether providers beyond the currently bound module pump are
needed for the selected consumer; a general threaded provider adapter is not an
automatic prerequisite.

Migrate input/HID and Platform reports as separate consumer increments. Preserve
input's Platform-owned streams, readiness/generation rules and acquisition
lifetimes. Preserve report backing/acquisition lifetime while reassessing which
orchestration and processing belong to Platform. The
[report handoff](../modules/platform_report.md) remains paused until explicitly
resumed; completion of the communication refactor does not approve that proposal.
Acceptance should demonstrate publication, use, withdrawal, provider replacement
and consumer abandonment without adding domain operations to Host.

## Availability queries and topology observation

**Behaviour agreed; implementation deferred.** Start with system-ID queries, not
a public registry. Host can answer module availability, exact endpoint availability
and the current topology revision. A query carries its expected revision: if stale,
return `stale_revision` and the latest revision without an availability answer.
Host checks revision and availability in one serialized operation; it retains no
historical snapshots. Availability neither reserves an endpoint nor establishes
a connection.

Expose the current topology revision atomically, with a local last-observed copy
per interested endpoint. Changes coalesce; native threads receive wake hints and
pumps inspect during execution. No notification message queue, receipt or
acknowledgement protocol is required. Select revision width/exhaustion rules,
storage lifetime and wake/recheck ordering when implementing. Validate changes
during queries, sleeping, replacement and retirement so a coalesced hint cannot
hide the latest durable revision. See the
[availability design](../modules/common_endpoint_lifecycle_design.md#availability-queries-and-topology-notifications).

Startup dependencies remain module-owned: install handlers and declare direct
readiness before waiting asynchronously for peers, continue receiving, and handle
`unavailable`. No automatic dependency graph, wait-for-peer connection request,
per-target subscription or identity-exchange protocol is selected.

## Undelivered newly created asset IDs

**Cleanup policy agreed; implementation deferred.** Normal disposal remains the
requester's responsibility. Only asynchronous loading/creation that produces a
new asset ID for that requester needs the orphan-result policy; existing IDs and
ordinary cache hits must not acquire a new disposal rule.

After requester execution has ended and all users are quiescent, Host accounts
for both endpoint-to-Host lanes and outstanding work, then safely drains the
Host-to-endpoint queue. Newly created IDs whose results were consumed remain under
normal requester-managed disposal. Dispose of newly created assets whose IDs
cannot be delivered or remain unread, including outcomes still in Host staging
or completed after exit. Reconcile explicit disposal/other final submissions
before deciding cleanup; do not treat uncertain publication as proof of receipt
or nonreceipt.

An explicit `retain_if_undeliverable` request flag retains the exceptional common
or cache-like resource even when its requester cannot receive the ID. It adds no
handshake or tentative status and has no effect after normal receipt. There is no
per-result receipt protocol or mandatory tentative-asset ledger. Select request
flag/result metadata and integrate with current asset/cache accounting as one
bounded increment. Validate consumed, unread, staged, late-completing and uncertain
results, including retained exceptions and unchanged existing-ID behaviour. See
the [receipt-free policy](../modules/common_endpoint_lifecycle_design.md).

## Long-running pump and Host work

**Deferred execution work.** The refactor makes asynchronous service use uniform;
it does not make long module/native calls preemptible. Identify the specific work
that exceeds an execution budget, including Host-managed schema/report processing,
and select an incremental operation or suitable worker placement. Keep Host and
module pumps responsive while their dependencies complete, preserving cancellation,
retained input/output ownership and native-thread constraints. Do not relocate
all processing or introduce a scheduler framework as a prerequisite.

The existing fatal deadline operates while Host can make progress. An independent
watchdog for a pump stuck in native/module code, coordinator recovery or automatic
restart requires a separate decision. Until then, logging, safe retention and
application/process shutdown remain the failure policy.

## Backpressure beyond current transports

**Options retained; no replacement selected.** Current common/direct POD queues
are growable, with bounded operation records, safe retry/refusal and fatal handling
for unrecoverable obligations or uncertain publication. The receive helper can
pause direct data while Host controls and completions continue; this does not
prove progress for every finite-capacity workload.

If measured stalls or a selected consumer justify it, evaluate fixed rings,
explicit outstanding-work limits, local pending output and existing Host-staged
results. Make pressure observable and preserve ownership on safe refusal. Stale
data/drop policy belongs to operations that explicitly permit it. Ordinary Host
requests/results cannot silently disappear.

A secondary lifecycle lane is an option only for a restricted message set with
a proven maximum outstanding count, potentially a reserved-slot transport. No
general cleanup queue or response-slot reservation scheme is agreed. Any selected
change must show progress with both directions under pressure, including stop,
cleanup, capacity wakes and unrecoverable failure; moving an unbounded backlog to
another queue is not a capacity guarantee.

## Optional authority, readiness and transport extensions

These are separate future choices, not unfinished requirements of the delivered
endpoint contract:

- Authority and APIs to impose/remove arbitrary non-lifecycle pair prohibitions.
  Ordinary connection close and lifecycle revocation are already implemented;
  a fresh explicit request is allowed after normal retirement, subject to admission.
- Lifecycle-coordinator delegation/replacement, cross-module thread-management
  authority, individual service publication, richer readiness policy or opening
  new dependencies during shutdown. Current same-module authority and failure
  policy remain the baseline.
- Generalising bulk addressing to arbitrary endpoints, schema-aware bulk contracts
  or independently retained acquisitions. Preserve existing bulk behaviour until
  a consumer selects such work; see the [shader stage plan](shader_system_stages.md).
- Relaxing the current seal acknowledgement, which waits for direct retirement,
  to the broader design's publication-stopped boundary. No current consumer needs
  that relaxation.

## Qualification and adjacent work

The completed increment's [validation record](../modules/direct_endpoint_increment.md#implementation-and-validation-record)
distinguishes portable/native automated evidence from physical qualification.
Further native interactive, hardware/audio and Linux toolchain/configuration
coverage can be selected when relevant; it is not an open implementation step.
High-rate Debug input performance and observation cleanup remain with
[input integration](../modules/input_host_integration.md) and the parked input
backlog. Platform-native backends, rendering/PSO work and report acceptance keep
their own scope and approval boundaries.
