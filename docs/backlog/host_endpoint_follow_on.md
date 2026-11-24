# Host and endpoint follow-on work

Reviewed 24 November 2026. Common Host endpoints and module lifecycle (`ca8770d`),
direct POD endpoints (`84506a7`) and the subsequent module preamble consolidation
are complete. This backlog contains only remaining work, not a requirement to
deliver one larger redesign.

The [module preamble design](../modules/module_preamble_design.md) and
[implementation decisions](../modules/module_preamble_proposal.md), dated
22 November 2026, select common dispatch/routing, Host-private lifecycle channels,
fixed ordinary POD rings and pressure-based draining, now implemented. The current
provisional ordinary ring size is 1,024 messages per direction; lifecycle rings
hold 256 messages and local message FIFOs grow up to 1,024 messages. Deferred
responses prefer existing operation storage, and message loss fails the affected
module. Existing framework-wide escalation is retained for fatal endpoint/module
failures, including active or pending Executive failures. Orderly direct closure
explicitly cancels unread messages. Bounded admission/retry rules and rate-limited
pressure/starvation diagnostics address overload without a general recovery
scheduler. The [qualification ledger](../modules/module_preamble_validation.md)
records completion, the scoped 24 November fixture correction and the explicit
validation limits. These mechanisms are no longer backlog alternatives.

Current behaviour belongs in the [Host foundation](../modules/host_pump_foundation.md),
[direct POD](../modules/direct_pod_connections.md) and
[bulk](../modules/direct_bulk_connections.md) references. The
[completion record](../project/completed_milestones.md#module-execution-preamble-consolidation)
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

## Capacity tuning beyond the delivered pressure policy

**Measurement-driven follow-up only.** Common/direct ordinary POD pipes now use
fixed 1,024-message rings; private lifecycle rings hold 256 records. Self and Host
fallback FIFOs grow only to 1,024 entries. Existing operation records retain
asynchronous results, while immediate replies use dispatch-scoped storage
reservations. Pressure-based draining, starvation protection and diagnostic
episodes are implemented. Direct data can pause while lifecycle, Host completions
and asynchronous servicing remain eligible.

Further sizes, operation limits or domain-specific stale-data policies need a
measured workload or selected consumer. Ordinary accepted requests/results cannot
silently disappear. Fixed rings do not guarantee progress for every workload, and
a larger queue is not a substitute for resolving sustained pressure. No general
outbox, arbitrary cleanup queue or new recovery scheduler is selected.

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
High-rate Debug input performance remains with
[input integration](../modules/input_host_integration.md) and the parked input
backlog. The 24 November unfocused-observation/freeze-cleanup fixture correction
is complete with [scoped evidence](../modules/module_preamble_validation.md);
it is not a pending implementation item. Platform-native backends, rendering/PSO work and report acceptance keep
their own scope and approval boundaries.
