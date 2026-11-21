# Common endpoints and module lifecycle

Design and delivery status reconciled 21 November 2026. The
[first pass](common_endpoint_first_pass.md) and consolidation are complete in
`ca8770d`; the [direct POD endpoint increment](direct_endpoint_increment.md) is
complete in `84506a7`. No implementation item remains open in either increment.

This broader record also retains agreed but unimplemented contracts. The
[Host and endpoint backlog](../backlog/host_endpoint_follow_on.md) is the single
entry point for remaining work; no next increment is selected. Current APIs and
validation belong to the [Host foundation](host_pump_foundation.md) and
[direct POD reference](direct_pod_connections.md).

| Area | Delivery status |
| --- | --- |
| Common Host queues, module pumps/requested threads, lifecycle, sealing and failure policy | Implemented in `ca8770d`. |
| System-ID direct addressing, acknowledged readiness, common receiving, explicit consent/install/activation and guarded retirement | Implemented in `84506a7`. |
| Revision-qualified availability queries and atomic topology observation | Agreed design; deferred implementation. |
| Receipt-free cleanup of undelivered newly created asset IDs | Agreed policy; deferred implementation. |
| Generic provider registration, long-running work, finite-capacity alternatives and richer authority/recovery | Separate backlog/design choices. |

Every module has a pump and can request zero or more service threads. Pumps and
service threads use the same Host communication contract. Host provides common
services, execution resources, routing and lifetime enforcement; modules retain
their own operation semantics and cleanup orchestration.

This direction separates module identity, execution, communication and authority.
Platform input and report concerns motivated the review, but their detailed
migration and publication contracts remain separate design work.

## First pass and longer term scope

The first pass establishes common Host endpoints, one pump per module, requested
service threads, module-owned creation outcomes, and safe shutdown and retirement.
It standardises on current service-thread transport behaviour and retains current
asset policy. Unrecoverable
communication or lifecycle failures may lead to logged application shutdown and,
if safe orderly shutdown cannot progress, process termination. Failure never
permits reclaiming storage or unloading code still in use.

The sections on identity, common Host connections, submission, pump execution,
thread lifecycle, draining, sealing and final deactivation provide the behavioural
foundation for that pass. Its common API, service mapping, closure mechanisms and
failure handling are implemented. Direct-connection controls and the common
receiver were delivered in the second increment. Topology queries and the new
asset-result cleanup policy remain deferred.

The generic exit and abandonment facts remain necessary now. The new policy for
disposing of undelivered asset IDs, its request flag and result metadata are
separate work. Internal endpoint identity and direct endpoint addressing/readiness
are implemented; public availability queries remain separate. Existing connection
and resource lifetime protections continue to apply.

Bounded transport progress, generic HID/report registration and richer recovery
are longer-term concerns. In particular, no additional control pipe or physical
reply-slot reservation has been selected. A secondary lifecycle pipe remains an
option only for a closed message set with a proven maximum outstanding count;
arbitrary thread or connection notifications do not acquire that bound merely by
being called lifecycle messages. See the first-pass document for completion
criteria and the selected decisions for that subset.

## Identity and lifecycle authority

A module instance owns its pump, requested service threads, service registrations
and module state. Each pump and service thread owns one communication endpoint.
An endpoint identifies an exact execution participant, independently of native
thread IDs, diagnostic names and module roles. Replacement never inherits an old
instance's requests, threads, endpoints or resources.

The pump coordinates module lifecycle. It is the normal designated destination
for module lifecycle notifications, but execution mode does not itself grant or
restrict authority. Host knows execution mode for scheduling and waking; Host
authenticates operations through endpoint identity and module-instance ownership.

Any endpoint may request a service thread for its own module instance. The new
thread belongs to that instance, not to the requesting thread. There is no implied
parent-child thread hierarchy. Any endpoint may request that its own module stop.
No endpoint can bypass the checks required for safe retirement and unloading.
Authority to affect other module instances remains a separate policy question.

The lifecycle coordinator orders thread stops and module cleanup. Host treats its
notification destination as a registered endpoint rather than granting lifecycle
authority by testing whether the destination is pumped.

## Common Host connections

Every pump and every service thread has its own connection to Host. Common means
shared interface and implementation, not queues shared between module contexts.

| Direction | Transport | Producer and consumer |
| --- | --- | --- |
| Endpoint to Host | POD SPSC | The owning pump or thread produces; Host consumes. |
| Host to endpoint | POD SPSC | Host produces; the owning pump or thread consumes. |
| Endpoint to Host | Owning-message SPSC | The owning pump or thread produces; Host consumes. |

Ownership transfers only to Host, and only on successful submission. A rejected
submission leaves ownership with its sender. Host replies may carry handles or
borrowed views under explicit lifetime contracts, not owning messages.

Host owns endpoint storage and lifetime. Module code receives a restricted view
for its own submissions and incoming consumption. A thread cannot use its pump's
endpoint or another thread's endpoint. Host workers return results through Host;
they do not become extra producers on a module endpoint's incoming queue.

Pump connections retain distinct producer and consumer roles even though both
execute on the host thread at different times. Thread connections use the same
roles concurrently. The first pass uses only these three service-thread-style
transports. Any later additional control lanes would remain private SPSC lanes
for that endpoint and require a separately justified capacity contract.

## Submission and dispatch

Both execution modes submit outgoing messages and consume incoming messages
through the same interface. Incoming service commands and Host replies use the
common inbox rather than a separate pump-only delivery interface. The pump
invocation supplies execution information, such as budget and lifecycle state.

Submitting from a pump does not invoke a service inline or reenter another module.
Host dispatches the request after the invocation returns. Thread requests use the
same asynchronous semantics. Existing pump-only ordinary service operations,
including borrow preparation, must use that common exchange.

Submission, admission and completion are distinct:

1. Submission transfers a message into its outbound transport. Successful owning
   submission transfers ownership to Host.
2. Host admission records responsibility and the capacity needed for the outcome.
   A submitted request can still be rejected. Delivery to a module provider does
   not establish that provider's semantic acceptance of the operation.
3. Completion publishes a correlated outcome to the exact requesting endpoint.
   If that endpoint can no longer receive it, Host explicitly settles the
   undeliverable obligation rather than redirecting it to a successor.

Every successfully submitted request requiring a response must reach a correlated
outcome or explicit retirement of that delivery obligation. A separate admission
acknowledgement is protocol-specific, not mandatory for every operation.

Request identifiers are scoped to their originating endpoint. Service
registrations identify their module instance and handling endpoint. Provider
commands and provider responses use the same endpoint interface whether the
handler is a pump or a service thread.

FIFO ordering applies within each ordinary directional lane. There is no implied
ordering across endpoints, across directions, or between POD and owning queues.
Priority control lanes may overtake ordinary traffic; protocols requiring an
ordering relationship must establish it explicitly.

Submission is nonblocking with an explicit retry outcome. Pumps retain pending
work and yield when capacity is unavailable. A thread may wait, but its wait must
remain responsive to stop and incoming work. Capacity becoming available must
also support progress notification. The first-pass specification maps poisoning
and uncertain publication to runtime failure, not retryable busy.

## Pump execution and notifications

Pump endpoints persist between invocations. Host schedules bounded pump work for
incoming messages, notifications, lifecycle progress, due deadlines or reported
runnable work. An idle pump need not run continuously. A pump cannot synchronously
wait for Host or a service thread.

Publication makes work visible before notification. Queue and lifecycle state
are durable; wake signals are hints. Waiting and rechecking must prevent lost
notifications, including notifications of newly available capacity.

A service-thread endpoint targets its thread's wait mechanism. A pumped endpoint
becomes runnable and wakes Host when notification originates elsewhere. A pump
does not require a private sleeping thread or native wake object.

Native modal loops remain an explicit execution concern: a cooperative budget
cannot guarantee return from an OS-controlled loop. Authenticated restriction
accounting and bounded Host maintenance can remain synchronous execution
facilities. They do not justify synchronous ordinary service access.

## Thread creation and lifecycle outcomes

Creation uses one asynchronous create-and-start request. The caller supplies a
module-owned entry function and context, requested execution properties and a
request correlation. Host establishes the endpoint, all transports, notification
state and thread execution environment before module code begins running.

Successful submission makes the creation attempt a module-owned obligation.
Requester exit does not implicitly cancel it, even before admission. Host drains
the accepted request and either creates the thread or rejects it under current
admission rules. Explicit cancellation is separate.

The entry context must survive the requester. Submission does not transfer its
ownership to Host. The module retains it until creation definitively fails or the
created thread has finished using it; other published references may require a
longer lifetime.

Successful launch is not module-specific readiness. The thread can initialize and
request Host services through its endpoint; the module decides when it is ready.
The thread may execute or exit before the requester consumes the launch result.
There is no separate provision-then-start round trip.

Each successfully submitted creation attempt produces one authoritative outcome
with two deliveries:

| Destination | Delivery |
| --- | --- |
| Requesting endpoint | Correlated creation result, or explicit settlement if undeliverable. |
| Module lifecycle endpoint | Creation outcome, including rejection, originating endpoint/request and thread identity on success. |

The lifecycle stream preserves creation success, execution ended, then endpoint
retired ordering for each successful thread. Failed creation produces a
definitive failure after partial setup is resolved. There is no delivery ordering
guarantee between the request-result stream and the lifecycle stream. If the
lifecycle endpoint is itself the requester, both logical deliveries remain.

Host establishes bookkeeping for creation and its outcomes before creating execution.
In the first pass these are retained lifecycle records, not reserved pipe slots.
Lifecycle obligations can be retained in durable records until queue publication
is possible; unread or temporarily unpublishable notifications are not lost.

Requester retirement depends on settling its creation request, not on stopping
the newly created thread. The new thread has an independent lifecycle record.
The module coordinator remains informed even when the requester never sees its
result.

## Thread stopping and endpoint retirement

A stop request names the exact thread instance. Host records a durable stop
condition and signals the thread. Recording stop does not depend on inbox space;
repeated requests do not restart shutdown.

The thread cooperatively finishes work, processes results and relinquishes its
resource use before returning. Host joins it and reports execution ended. Joining
proves that thread execution has ended, not that every published resource is
unused or that every accepted message has been processed.

Host retains endpoint storage while it drains accepted outbound messages, settles
requests and undeliverable replies, and resolves resource and notification
obligations. Accepted owning messages belong to Host even if the sender exits.
Module-specific cleanup after thread exit is coordinated through the lifecycle
endpoint using explicitly authorised operations; it does not impersonate the
dead endpoint or take over its SPSC roles.

Retirement requires joined execution, settled communication, resolved resource
access and quiescent notification users. Host then releases endpoint storage and
reports retirement to the lifecycle coordinator. Identities cannot be reused to
receive stale work. Unexpected exit follows the same lifetime protections.

## Abandonment and undelivered asset results

Execution-ending and endpoint retirement facts are implemented. The newly created
asset-ID cleanup policy in this section remains deferred; see the
[asset-result backlog](../backlog/host_endpoint_follow_on.md#undelivered-newly-created-asset-ids).

Execution ending can bypass cooperative draining and sealing. Host explicitly
marks the exact endpoint as no longer able to receive results or produce new
responses. A stop request alone does not establish that fact. The endpoint record
and resources remain retained until execution and access obligations are resolved.

Host drains messages successfully submitted before exit, including owning
messages, completions and releases. A provider completion queued immediately
before exit still counts. After accounting for those submissions, commands that
cannot receive a provider response are resolved with an appropriate provider-ended
failure rather than waiting indefinitely. Results addressed to the departed
endpoint are settled as undeliverable, never redirected to its replacement.

Accepted work is cancelled where supported or allowed to finish safely where
workers still use its inputs. Abandonment does not destroy storage out from under
I/O, conditioning, batch work or other legitimate users. Joined execution and
resource quiescence remain distinct from closing delivery.

Normal asset disposal remains the requester's responsibility: it requests loading
or creation, receives the asset ID, and uses that ID to request disposal. This
design does not introduce a general endpoint-scoped asset lifetime or automatic
disposal of every asset when the requesting endpoint later exits.

The narrower abandonment rule applies only to asynchronous loading or creation
that produces a new asset ID for the requester, when that requester exits before
receiving the ID and can no longer arrange disposal:

| Request behaviour | New asset whose ID cannot be received |
| --- | --- |
| Default | Host arranges disposal once existing operations and protected uses allow it. |
| Explicit `retain_if_undeliverable` request flag | Host retains the asset despite losing the recipient, under the otherwise applicable asset disposal rules. |

The flag is part of the load/create request and is consulted only for this lost
result case. It does not alter normal disposal after successful receipt or create
a general common-resource ownership, discovery or management scheme. A cache
created for general use is an example of the exceptional retention case. It does
not imply a registry or an automatic transfer of the result to the module pump.

The rule does not apply to assets that already have an ID. Saving, acquiring or
otherwise operating on an existing asset must not dispose of that source merely
because an operation's result cannot be delivered. A load satisfied by an existing
cached asset must distinguish that existing identity from a newly created result.

Host storage ownership is unchanged. Disposal must respect active workers,
independent acquisitions and other protected users. Releasing an abandoned
endpoint-local acquisition does not invalidate another user's protection. The
retention flag cannot bypass code, allocator or module dependency requirements.

Receipt means successful consumption from the endpoint's incoming queue, not
publication into that queue or completion of the application handler. Consumption
transfers normal disposal responsibility to the endpoint. If it subsequently
exits before its handler finishes, this lost-result rule does not automatically
dispose of the consumed asset. The receiving framework must not silently discard
consumed results while treating them as never received. Host-side staging or
movement between Host-owned buffers does not count as endpoint consumption.

No asset-receipt acknowledgement exchange is required. For default disposal, the
unread result identifies the newly created asset and carries cleanup metadata.
The metadata represents an abandonment obligation, not a transfer of storage
ownership through POD. Once publication succeeds, the queued result can represent
that obligation; a separate tentative-asset collection is not required by the
contract. Before publication, the operation's existing bookkeeping retains it.

| Result state | Disposal responsibility |
| --- | --- |
| New ID not yet published | Host handles the undeliverable result under the request policy. |
| Published new ID remains unread when the endpoint exits | Host arranges safe disposal by default. |
| Endpoint consumed the new ID | Normal requester-managed disposal applies; no consumption receipt is sent. |
| `retain_if_undeliverable` was selected | Retain regardless of delivery; no tentative delivery status, cleanup-on-unread obligation or handshake is needed. |

After execution ends, Host drains both directions. It first accounts for all
endpoint-to-Host submissions, including final acknowledgements from other
protocols, disposal requests, provider completions and owning messages. Once the
original consumer can no longer access the incoming queue, Host takes over
draining unread Host-to-endpoint messages and applies their cleanup obligations.
This is an exclusive teardown takeover, never simultaneous SPSC consumption by
Host and the endpoint. Disposal must tolerate an already disposed asset without
affecting any later identity.

Closing delivery does not require outstanding worker operations to finish at that
instant. Any new result completed later follows the unpublished-result rule;
draining the abandoned inbox once cannot lose responsibility for later outputs.
Host still waits for protected uses before actual reclamation. The retention flag
has no effect on normal requester-managed disposal after receipt.

The cooperative path closes admission, finishes work, seals, settles and exits.
The abandonment path establishes execution ended, drains prior submissions,
cancels impossible deliveries, resolves unanswered commands and cleans up
abandoned acquisitions and undelivered new asset results as defined above.
The lifecycle coordinator receives the appropriate outcomes but does not
automatically inherit the departed endpoint's requests or
resources. Explicitly requested service threads retain their previously defined
module ownership; they are not endpoint-scoped results of the requesting thread.

The current implementation already cancels terminal-client replies, releases
abandoned acquisitions and cleans up unpublished schema-processing outputs.
Ordinary retained assets have a broader policy: the implemented
[asset retention contract](../assets/asynchronous_asset_services.md) permits
retention until explicit disposal, dependent-module cleanup or application exit.
Dropping an ordinary load reply does not itself dispose of its repository asset.
Default disposal of an undelivered new asset result is therefore a design change,
not a claim about current ordinary-load behaviour. Representation retention such
as source or baked is separate from the proposed lost-result retention flag.

## Module startup and shutdown

Startup establishes the pump endpoint, activates module state, and advances
initialization through subsequent pump invocations. Activation may request
threads and services but does not wait synchronously. Failure still requires
cleanup of anything already created. Executive uses the same execution model;
Host may retain the policy of choosing and initially loading it.

Shutdown begins with sticky module-wide stopping state, a lifecycle notification
and observation by the module's endpoints. Module stopping and a particular
thread's stop request are distinct. The coordinator orders individual thread
stops according to module dependencies; Host need not understand those dependencies.

Host rejects unadmitted thread-creation requests once module stopping begins.
Previously admitted creation remains tracked and its outcome remains required.
Any resulting thread is part of the stopping module. The thread inventory is not
settled until pending creation attempts are resolved.

The pump continues running while service threads stop and retire. Host continues
dispatching and providing cleanup services; it cannot block its loop waiting for
a thread that may need Host to make progress.

The designated lifecycle coordinator declares module cleanup complete. This does
not override Host's checks or bypass earlier messages on other lanes. Finalization
requires resolved creation attempts, joined and retired service threads, drained
accepted messages, settled operations, quiescent published resources/notifications
and no active invocation of module code. Cleanup requiring module execution must
finish before final deactivation; Host-only settlement does not require a departed
endpoint to receive a confirmation.

Final deactivation destroys remaining local runtime state without creating
asynchronous work. The pump endpoint retires last, and Host releases the binding
only after execution and resource references permit it. A departing requester
cannot be required to consume the final result of its own module's termination.

Failed joins, failed cleanup and timeouts report unresolved shutdown; they do not
prove quiescence or authorise unloading live code. Whole-engine shutdown must keep
common services and provider cleanup paths available while consumers need them.

## Admission while draining

Host understands generic lifecycle operations, not module-specific cleanup
semantics. During module draining it rejects new thread creation and service
publication, but permits stopping threads and settling established operations,
acquisitions, borrows and notification lifetimes.

A draining module can still initiate service requests needed for cleanup through
its endpoints, subject to route, destination and capacity rules. Host does not
require a caller-supplied cleanup flag or interpret an opaque opcode as proof.
The coordinator is responsible for bringing this work to quiescence.

| Provider state | Admission |
| --- | --- |
| Accepting | New routes and ordinary requests are permitted. |
| Draining | New routes are refused; existing routes support outstanding work and provider-controlled shutdown requests. |
| Closed | New service requests are not delivered; existing completion and retirement obligations continue. |

In the draining state, the provider interprets a new request on an existing route
and accepts it or returns closing. For example, the provider can distinguish
destroying an existing window from creating a new one without Host learning
either operation. Delivery and rejection obligations remain bounded.

Generic cancellation, release, completion and retirement messages identify
existing obligations and remain available independently of ordinary service
admission. An old identifier does not authorise arbitrary new work.

Dependencies needed during shutdown should be established before draining. A
cleanup procedure requiring a newly opened dependency is refused in the first
pass. Any future exception requires a separate policy.

## Draining accepted provider commands before sealing

Closing new provider-command admission is an explicit step before sealing. It
does not close the incoming mailbox: outstanding results, cancellation, resource
controls and lifecycle messages must continue to arrive. This is stronger than a
provider's draining mode, in which it may still accept domain-specific cleanup
requests on existing routes.

Host establishes an incoming-work boundary accounting for every command admitted
before closure. Delivered commands remain obligations; commands pending delivery
must be delivered or explicitly cancelled with their request obligations settled.
None may appear behind the established boundary. A shared FIFO can establish the
ordering; different lanes require publication boundaries accounted for before the
receiving helper reports closure complete. Cancellation does not itself prove
that provider execution has stopped.

With admission closed, the endpoint remains draining and can issue ordinary Host
requests needed to finish accepted work, subject to existing service and shutdown
rules. It completes or cancels its commands and consumes results whose handling
could require another independent request. Host need not understand the dependency
between a provider command and a later Host operation.

Only then does the endpoint request sealing. Remaining obligations must be
expressible through the defined settlement protocols. A completion or owning
result already queued to Host is included in the pre-seal boundary; the provider
need not wait for Host to deliver it to its eventual recipient if no additional
provider action is required. Work needing a fresh independent request cannot be
relabeled as settlement after sealing.

For direct POD input, module handlers stop accepting new application work and
finish or reject conversations while their pipes remain active. Host does not
interpret their messages. Sealing subsequently revokes the direct pipes; any
unfinished conversation is interrupted rather than silently routed through Host.
Abrupt exit uses the abandonment path instead of requiring the endpoint to finish
these cooperative steps.

## Sealing the Host connection

Sealing constrains remaining work; it is not immediate queue closure or proof of
retirement. The endpoint state progression is open, sealing, sealed, execution
ended, then retired. Abrupt execution failure still requires Host cleanup even
if the normal seal handshake never completed.

The owner begins sealing by refusing new independent submissions locally. It
captures the last successful publication sequence for every outbound lane: POD,
owning messages and any additional control lane. Failed submissions do not advance
these sequences and remain caller-owned. Sequence representation and exhaustion
handling remain interface details; ring positions alone are not the boundaries.

The seal request contains those boundaries and a seal identifier, outside its own
captured prefixes. If publication is busy, the owner retries that same request
and boundaries while remaining in sealing state. Permitted settlement traffic
can continue after the boundaries.

Receiving the POD seal request does not prove the owning queue has been drained.
Host acknowledges only when every captured prefix is accounted for: each message
has been rejected with its result obligation retained, admitted with its
obligations recorded, or accepted into durable Host-managed ownership/control
state. Untracked temporary dequeue is insufficient. Earlier owning submissions
retain pre-seal status even when Host sees the POD seal first; other admission
rules, including module stopping, still apply.

Accepting the seal also stops Host assigning new independent work to that
endpoint. Previously admitted or dispatched commands remain obligations. The
sealed acknowledgement means both the outgoing prefixes are accounted for and
no new independent incoming work will be assigned. It does not mean existing
work has completed. Direct connections impose the additional publication-stop
condition described below; Host queue boundaries do not fence direct queues.

After the boundary, explicit Host-mediated settlement operations may complete or cancel
existing requests, transfer an owning result required by an existing protocol,
release resources and acknowledge quiescence/retirement. Host validates the
identified obligation and permitted transition. Fresh independent service
requests must be submitted before sealing. A generic cleanup flag or arbitrary
reuse of a request ID cannot bypass this restriction.

The captured sequences are not final queue tails. Host must also account for
post-seal settlement messages. Joined thread execution, or final pump deactivation
with no possible later invocation, establishes that its producer cannot publish
again. Resource and notification quiescence are separate retirement requirements.

## Final settlement and deactivation

Stopping execution and retiring endpoint resources are separate decisions. An
endpoint finishes work requiring its code; Host can continue accounting, delivery
cancellation and resource cleanup afterward. No additional permission-to-exit
handshake is required for a service thread.

After sealing, only explicit operations that settle existing work are legal:

| Message category | Behaviour after sealing |
| --- | --- |
| Results or failures of previously submitted requests | Receive and handle while execution continues. |
| Completion or cancellation of existing obligations | Send or receive under the established protocol. |
| Disposal or release of an existing asset, acquisition or borrow | Send or receive. |
| Connection closing, quiescence and retirement controls | Send or receive. |
| Existing thread/module lifecycle cleanup | Send or receive. |
| New loads, processing requests, connections, threads, registrations or availability queries | Reject. |

There is no generic cleanup flag permitting arbitrary new work. An asset ID
arriving after sealing may be disposed of, but cannot initiate a new processing
operation. If that processing is necessary, the endpoint must remain draining.

The endpoint can stop when it has finished or cancelled its application work,
relinquished direct and borrowed-resource access, submitted required final
completions/releases/disposals, and ensured no local callback or delegated user
still needs state it will destroy. It need not wait solely for confirmation of
cleanup Host can complete independently. An admitted disposal continues even if
its requesting endpoint exits before receiving the result.

A service thread returns from its entry function. Host observes execution ended
and joins it, proving that the thread cannot publish or consume further messages.
Host retains its record while draining final submissions in both directions,
applying abandonment rules, finishing or cancelling outstanding operations, and
waiting for connection, resource and notification obligations. Only then is the
endpoint retired. Execution must be able to end before resource retirement where
the retirement proof itself requires joined execution.

A pump reports through its return status that it has finished executing and needs
no further pump invocations. Host drains its final submissions and treats later
results as undeliverable under their own cleanup policies. The pump remains in
its stopping phase if further module execution is still required.

Final deactivation is a separate Host call. Host invokes it only after all module
invocations have unwound, the module's service threads have completed the required
shutdown, no remaining user needs the runtime state being destroyed, and cleanup
does not require another pump invocation. It performs final local destruction
without submitting asynchronous work. Binding release additionally requires the
remaining code and resource references to permit it.

Host checks the execution and tracked lifetime facts; it does not interpret
arbitrary module cleanup semantics. Endpoint code is responsible for relinquishing
its local use correctly. An endpoint exiting with an unanswered provider command
uses the abandonment path rather than causing Host to wait for impossible
cooperation. No acknowledgement is required merely to state that a thread has
returned.

## System ID addressing

The initial addressing mechanism is the existing system ID combining module and
thread identity. Host resolves that address using its own endpoint records during
explicit connection establishment. No publicly shared registry or enumeration
service is required; establishment does not require a preceding discovery query.

| Identity | Meaning |
| --- | --- |
| System ID | Reusable address identifying a module and thread identity. |
| Endpoint instance identity | One exact lifetime of the endpoint occupying that address. |
| Connection token | One establishment between two exact endpoint instances. |

Each module's pump uses its module ID combined with `thread_ids::host`, matching
the existing ambient thread identity for pumped execution. That thread identity
is reserved for the pump within the module; a requested service thread cannot
claim it. The convention conveys an address, not additional service authority.

A service-thread creation request supplies its intended thread identity. Host
derives the module component from the authenticated requester. Multiple service
threads in one module require distinct thread identities; different modules may
use the same thread identity because their combined system IDs differ.

Only one endpoint may occupy a system ID, including while its previous endpoint
is retiring. Duplicate creation fails. After retirement the address can be reused
with a fresh endpoint instance identity. Diagnostic names and native thread IDs
do not determine this identity.

Host resolves both establishment participants once when processing the request.
An absent or not-ready participant returns `unavailable`; Host does not wait for it to
appear, load its module or subscribe the requester to future availability. Other
admission checks, including permission and closure, still apply. Once selected,
each exact participant instance is fixed for that establishment. If either disappears,
the handshake fails rather than substituting a replacement at the same address.
A later explicit request may resolve the address again.

Readiness to participate in connections is endpoint-specific and distinct from
native thread launch or completion of all module initialization. The receiving
and connection-control machinery must be installed before the endpoint is offered
a connection. Module startup may depend on connections becoming available earlier
than overall application readiness. The declaration protocol is defined below.

A direct send by system ID consults the participant's existing local pipe mapping;
it does not re-resolve the destination through Host. That mapping remains bound
to the selected endpoint instance. Replacement closes the old pipe; sends return
`no_pipe` until a fresh explicit establishment succeeds. Deferred work additionally
validates its saved connection token where it belongs to a particular lifetime.
The receiving context makes the sender's system ID, exact endpoint identity and
connection token available without relying on payload claims.

If further identification is needed later, established communication can carry
thread identities. No additional identity-exchange protocol is required or
specified at this stage.

## Endpoint connection readiness

Every endpoint starts unavailable for direct establishment. Host reserves its
system ID and provisions its Host communication resources before module execution
begins, but native thread launch or pump activation does not establish readiness
to accept direct connections.

The endpoint can already use ordinary Host services while preparing to accept
connections. A pump may prepare over activation and several invocations; a service
thread may prepare during initialization in its entry function.

The endpoint owner explicitly declares readiness through its own Host connection.
Before submitting that declaration, it installs its Host receiver, direct control
handler, connection bookkeeping and application message handler. The application
handler need not support every eventual operation yet, but must handle incoming
messages safely, including rejecting work requiring unfinished initialization.
Host accepts the module's assertion; it does not inspect or certify its handlers.

The conceptual request is `EnableDirectConnections`; final message names and
layouts remain unspecified. Host authenticates the source endpoint and does not
allow it to enable another endpoint. It checks lifecycle and admission state,
records readiness and returns a correlated acknowledgement. The acknowledgement
precedes resulting incoming offers in that endpoint's ordered Host control stream.
Handlers already exist before submission, covering the interval before the owner
consumes its acknowledgement. A pump's declaration is processed after its
invocation returns, using the same asynchronous service path as a thread.

Both endpoints must declare readiness before establishment begins. Readiness
permits participation in the offer, installation, activation and closure protocol;
it does not accept every peer or make a particular pipe active. Each offered
connection still requires consent, and each installed connection still requires
its separate readiness and activation handshake.

The straightforward startup sequence is to install handlers, declare readiness,
consume acknowledgement, explicitly request required connections, complete their
handshakes and continue initialization. Waiting means continuing to receive and
perform appropriate work, never blocking a pump. An unavailable peer causes an
immediate establishment outcome rather than a retained wait-for-peer request.

Repeated readiness declarations can be idempotent while the endpoint remains
eligible. They never clear a prohibition, undo sealing or reopen a retiring
endpoint. If shutdown precedes processing, Host rejects the declaration; if
readiness precedes shutdown, subsequent lifecycle restrictions still close
admission and affected connections.

Thread-creation success continues to mean launched, not ready for connections.
Endpoint readiness also does not declare the whole module ready. The module
lifecycle coordinator decides when its required threads, connections and services
have completed the necessary initialization. Connection readiness is one milestone
in that process, not a prerequisite that waits for overall module readiness.

## Availability queries and topology notifications

This section records agreed design, not an implemented discovery API. Delivery is
tracked in the [availability backlog](../backlog/host_endpoint_follow_on.md#availability-queries-and-topology-observation).

Availability observation is separate from connection establishment. A request to
establish a connection either begins against an eligible current endpoint or
returns an immediate outcome such as `unavailable`; Host does not retain it as a
dependency wait, retry it automatically or load the missing module.

Availability queries are conditional on the caller's expected topology revision.
The shared revision provides the initial observation; the asynchronous queries
remain available for Host-serialized checks:

| Query | Result |
| --- | --- |
| Current topology revision | Host's current revision when it processes the request. |
| Module availability at revision R | If R is current: existence, exact instance identity and lifecycle state, labelled with R. Otherwise: `stale_revision` and the current revision, without an availability answer. |
| Endpoint availability by system ID at revision R | If R is current: address resolution, exact endpoint instance and connection readiness, labelled with R. Otherwise: `stale_revision` and the current revision, without an availability answer. |

Host checks the revision and reads availability in one serialized operation. It
does not retain historical snapshots or reconstruct an old revision. Unavailable
at a matching revision is a valid observation, distinct from a stale query.
Several dependencies can be checked at the same revision; a stale result tells
the caller to refresh the relevant checks if it still needs a current view.

A module can be starting while some endpoints already accept connections.
Availability does not reserve an instance or promise later establishment:
disappearance, permission checks and peer consent still apply. Callers wanting a
connection may request it directly without a preceding query.
The expected-revision precondition applies to availability queries, not connection
establishment. Establishment resolves against current Host state and binds once
to the selected exact endpoint, as defined above.

Host exposes the current topology revision through shared atomic state. Each
interested endpoint keeps a local last-observed revision and compares it while
servicing its endpoint. A difference indicates that previous observations may be
stale; the endpoint queries the particular module or system IDs it depends on.
There is no queued topology-change message, per-target subscription, public
registry or required full local copy of Host's records. Observing change does not
establish connections or satisfy startup dependencies by itself.

A topology revision identifies changes to the availability view. Relevant changes include
module appearance and lifecycle transitions, endpoint appearance and declared
readiness, loss of endpoint availability during stopping/sealing, and retirement
or replacement. Loss of availability is observable before final storage
retirement; observers must not wait for retirement to learn that an endpoint
cannot accept connections.

Host updates its availability records and then publishes the new atomic revision
as part of its serialized topology transition. Reading that revision does not
grant access to Host-owned records or provide a snapshot; revision-qualified
queries still perform the authoritative check.

Host does not track a last-posted revision per endpoint, retry topology messages
or wait for acknowledgements. Intermediate revisions coalesce naturally in the
shared value. This removes messaging only from advisory topology notification;
readiness, sealing and resource-quiescence handshakes retain their own meanings.

Pumps inspect the revision on subsequent invocations. Host signals the existing
wake mechanism for eligible service threads when topology changes. The signal is
a prompt to inspect durable state, not an event that must be counted. The
check-before-sleep protocol must cover a change between inspection and waiting.
Atomic storage and wake targets remain valid until all endpoint users are
quiescent; exact memory ordering and wait integration remain specification work.

Coalescing provides notice of change, not a complete event history. A transient
endpoint may appear and retire between observations. Existing direct connections
retain their explicit closing and retirement controls; advisory topology updates
do not replace those lifetime obligations.

A new endpoint reads the current revision and queries its known dependencies, or
attempts the desired connection directly. It does not rely on historical events.
Atomic observations and stale-query results supply newer revisions for subsequent
checks. Sealing ends the need for advisory topology wakeups; outstanding request
results and connection controls retain their settlement rules.

The local revision records observation, not completion of resulting work. If a
query cannot yet be submitted, the endpoint retains that pending work locally;
updating its observed revision must not lose the need to retry.

Asynchronous replies may already be old when consumed. A receiver never moves
its known revision backwards: if it knows revision 41, a successful query result
labelled 40 is not current and can be discarded or retried. A notification does
not itself refresh any dependency's availability, and a query about one target
does not refresh observations about others. If a newer revision is not yet known,
its later observation invalidates the earlier observations. No observation
reserves the destination against subsequent change.

While an endpoint remains eligible and continues executing, revision inspection
and the wake protocol must allow it to observe a change without another change
being required. Ordinary queue pressure cannot prevent publication of the shared
revision. Host does not guarantee receiver action or observation of every
transient intermediate state. No topology acknowledgement exchange is required.

Startup coordination remains module-owned. An endpoint installs its handlers and
declares its own connection readiness before waiting for peers, queries or tries
its dependencies, retains unresolved dependencies locally, and rechecks them on
topology change. It explicitly requests establishment when appropriate and reports
module-specific progress through the module's chosen coordination. Host does not
maintain a dependency graph. Declaring communication readiness before waiting
avoids two endpoints each withholding readiness while waiting for the other.

## Direct POD addressing and use

Direct POD connections are explicitly established through Host on demand. Host
does not provision every possible pair, and attempting to send does not initiate
establishment. This extends the communication model to any two distinct
participating endpoints: pump to pump, pump to service thread, or service thread
to service thread, within one module instance or between module instances.

There is at most one connection record per unordered pair of exact endpoints,
including offered, installing, active and closing records. Each establishment
receives a fresh nonreused connection token. A new establishment between the same
pair must wait until the previous record has fully retired.

An establishment request names two participant system IDs, which Host resolves
once to exact endpoints as described above. Any endpoint may request the pair;
the requester need not be either participant. Host authenticates the requester
through its Host connection, independently of the named participants. Naming a
participant does not impersonate it or grant authority to act on its behalf.
Connecting an endpoint to itself is invalid. Address resolution and permission
to connect are separate concerns.

Host owns two SPSC POD queues per connection. Each participant receives its own
outbound producer and inbound consumer access. No additional application-level
producer/consumer roles or per-pair conversation contract are required. Transport
and message representation must be compatible; message semantics and acceptance
belong to the receiving module. Owning transfers remain exclusively to Host.

Incoming messages from Host and all direct inbound queues reach the endpoint's
common receiving mechanism. The queues remain separate SPSC transports, with one
record per nonblocking receive call, FIFO per queue and no total cross-queue order.
The direct sender identity comes from the established descriptor, not a payload
claim. Host-routed commands distinguish their immediate transport source from
any authenticated original requester carried by the command.

The [common receiving contract](direct_endpoint_increment.md#common-receiving-contract)
specifies persistent round-robin selection, authenticated source metadata and
bounded mechanical handling of Host controls before returning the original record.
The caller budgets all records and retains application dispatch and consent.
Direct messages cannot invoke Host control handling merely by claiming its type.

Outgoing direct messages specify the recipient system ID. For normal sends, the
participant helper locates the established connection bound to that address's
selected exact endpoint instance. Deferred work belonging to one connection
lifetime must also validate its saved token so that an old operation cannot
accidentally publish into a later establishment.

| Pipe availability | Direct send behaviour |
| --- | --- |
| Active locally and permitted by the shared admission gate | Attempt to enqueue. |
| Absent, establishing, closing, retired or denied | Return `no_pipe`; do not establish, reopen or retain the message. |

Active-queue capacity and transport failures have separate outcomes. Successfully
queued means queued, not processed. A notification failure after publication must
not be reported as if the message had never escaped and were safe to retransmit.
The implemented result enumeration and failure distinctions are documented in
the [direct POD reference](direct_pod_connections.md#receiving-and-sending).

Thread recipients wake their thread. Pump recipients become runnable and wake
Host when needed; publication never invokes a receiving pump inline. Consumer
progress also supports notification of available capacity. Connection controls
use the participants' ordinary Host connections, independently of direct queue
capacity.

## Explicit direct connection establishment

Request/result traffic is separate from participant installation and lifecycle
traffic. There is one pending establishment per unordered exact endpoint pair,
regardless of who requests it. Reciprocal requests cannot create a second pipe.

The agreed handshake separates consent, installed readiness and permission to
use the data queues. The requester need not be a participant, and submitting a
request does not supply participant consent.

1. R requests establishment between A's and B's system IDs. R may be A, B or a
   third endpoint. Host resolves both exact endpoints and checks participation,
   admission, uniqueness and resource availability. The submitted request remains
   pending and direct sends return `no_pipe`.
2. Host assigns a fresh connection token and offers the exact connection to both
   participants. Both explicitly accept or reject through their own Host
   connections, even if they requested establishment. No queue descriptors are
   exposed yet. Rejection, disappearance or an establishment
   deadline resolves the request with an appropriate failure.
3. After both participants consent, Host provisions both queues, shared atomic
   admission state and notification facilities. It retains lifetime bookkeeping
   before publishing either descriptor. Each descriptor identifies the token,
   exact local/peer identities and that side's queue and notification access.
4. Each side's common helper validates and installs its descriptor, integrates
   its inbound queue with receiving, and acknowledges readiness. These are generic
   mechanics, not a second module consent decision. Ready means installation is
   complete and activation/closure can be handled; direct data access is still
   disabled. Installation failure initiates closure.
5. Once both sides are ready, Host rechecks admission, including revocation and
   sealing. It activates the shared connection state and publishes activation to
   both endpoints. Each side becomes locally available only after processing its
   activation notification. Each data operation also checks current shared
   admission through the access guard described below.

An offer identifies the connection token and exact peer. The helper records the
pending offer and exposes it to module code. Module policy chooses accept or reject;
the helper validates that the reply belongs to the still-pending offer and sends
the response through that participant's ordinary Host endpoint. There is no wait
inside receive and no implicit acceptance. Rejection by either side fails the
attempt for all attached requesters and withdraws the other side's pending state.
Ignoring an offer is bounded by the existing establishment deadline.

Acceptance permits installation; it does not make the pipe available. The helper
handles installation and readiness publication, retains any unsent control using
the existing deferred-control mechanism, and reports installation notices through
the common receiver. Host cannot activate until it has received both readiness
responses. A failed or withdrawn installation follows the existing closure path;
it does not require module code to undo generic queue registration. Late controls
and replies must match the exact token and expected state and cannot reopen a
closing connection. Module-specific cleanup still observes lifecycle notices.

When a requester is also a participant, its successful establishment result follows
its activation notification in the same ordered Host stream. A third-party
requester receives only its correlated result, not descriptors or installation
controls. Success records activation by Host after both installations; it does
not prove that both participants have consumed activation or processed data.
A can send after processing activation even if B has not yet processed its own
activation: B's queue is already installed and
holds the data until B activates and consumes it.

Concurrent explicit requests share one pending establishment and retain a request
correlation per accepted requester, independently of the two participant records.
Joining a pending attempt sends no duplicate offers and supplies no implicit
consent. Keep correlation storage bounded; additional requests return busy when
that bound is reached. The direct POD increment selects eight correlations per
pending establishment. Redundant pending requests
from the same requester return busy. An active pair returns already connected
with its existing token, not a second ownership claim. A closing
pair returns closing; Host does not queue automatic re-establishment after
retirement.

Activation notifications cannot override later shared closing state. A delayed
success result records successful establishment, not a guarantee of continued
availability when handled. Current shared admission always governs data access.

Before descriptor exposure, failure resolves requests and releases unexposed
resources. Once either descriptor is exposed, failure requires closure and
quiescence of exposed participants. A third-party requester's exit settles its
undeliverable result without cancelling the participants' attempt or connection.
Departure of either participant closes the connection, including when that
participant is also a requester. Requesting a connection does not confer ownership,
an extra lifetime reference, or authority to close or revoke it on others' behalf.

## Closing and permission to connect

Normal connection close and lifecycle revocation are implemented. Arbitrary
non-lifecycle pair prohibitions/removal below describe a future policy boundary;
their authority and API remain in the [backlog](../backlog/host_endpoint_follow_on.md#optional-authority-readiness-and-transport-extensions).

Closing one connection and prohibiting future establishment are distinct:

| Operation | Effect |
| --- | --- |
| Close a connection token | End that establishment and retire it; do not by itself prohibit a later explicit request. |
| Prohibit an exact endpoint pair | Reject establishment between that pair and close affected pending or active connections. |
| Prohibit an endpoint | Reject new direct connections involving it and close affected pending or active connections. |

Module-level and global admission restrictions can independently apply. Host
records prohibitions separately from connection storage so they survive its
retirement. Revocation records the admission restriction before closing affected
records. Pending installations cannot expose or activate behind that restriction.

Removing a prohibition merely makes a later explicit request eligible. It does
not reopen the old pipe, retry a message or establishment, override another
restriction, or bypass unfinished retirement. Endpoint sealing and retirement
permanently prohibit new connections for that exact endpoint identity. Broader
policy governing a replacement is separate.

Connection tokens protect each establishment: a delayed close, readiness or
quiescence message for an old token cannot affect a new connection. Detailed
authority for issuing and removing non-lifecycle prohibitions remains to be
specified; the lifecycle and permission distinction is fixed.

## Direct revocation and quiescence

Host-owned atomic state gates direct operations, rather than providing only an
advisory active flag. A load of active followed by an uncoordinated publication
would allow Host to mistake revocation for proof that publication had stopped.

The direct helper acquires an operation guard only while admission permits it.
Host atomically closes admission; previously admitted operations finish and
release their guards. Admission checking and acquisition must be coordinated,
not a separate unchecked load followed by an increment. The guard covers queue
access and subsequent notification signalling. Exact atomic representation,
memory ordering and counter limits are specified by the implemented
[admission contract](direct_pod_connections.md#closing-admission-and-lifetime).

Revocation applies to the entire bidirectional connection. Host closes admission,
signals both participants and sends closing notifications with the reason through
their Host connections. New direct sends return `no_pipe`, and closed queues are
ineligible for common receiving; operations admitted before closure may finish.
Once admitted publications finish, no further
messages can appear. Notification and descriptor storage remain retained.

Graceful application conversations finish while active. Closure cancels further
direct exchange and does not require processing unread data. Unread POD records
can be discarded once reclamation is safe. Receiving handlers resolve their
application-level expectations as interrupted or cancelled; Host does not inspect
their semantics.

Zero active operations alone is not quiescence: local handlers or deferred work
may still hold access. Each exposed participant relinquishes its queue roles,
descriptor access, notification-reader access and outgoing signals, then sends a
quiescence acknowledgement through its Host connection. Deferred work should
retain tokens and revalidate them through the helper rather than hold raw queue
pointers.

Host retains descriptors, queues, atomic status and notification targets until
both sides have discharged access obligations. For ended execution it may use
the established execution-quiescence proof instead of an acknowledgement, but
that proof must cover all users. A join does not discharge another participant's
access. Never-exposed sides require no descriptor relinquishment. Timeouts do not
authorise reclamation.

The progression is active, closing with admission shut, in-flight operations
finished, both sides quiescent, then retired. Participants receive retirement
notification where deliverable. Normal close permits a later explicit request
after retirement, subject to all remaining admission restrictions.

The agreed [helper/module responsibilities](direct_endpoint_increment.md#helper-and-module-responsibilities)
retain an explicit `quiesce(token)` call after module-dependent users have finished.
The helper applies closing state before exposing the notice, detaches transport
and notification access, and handles acknowledgement publication without blocking.
Modules cancel direct conversation expectations but need not finish unrelated Host
work or discard independent copied data. Neither side waits for peer quiescence
before relinquishing its own access. Request-result correlations settle independently
of connection retirement.

## Direct connections during sealing and shutdown

The Host-queue seal boundaries do not account for direct queues. Direct access
therefore adds a separate publication-stop condition to sealing:

1. Beginning sealing locally refuses direct publication and new explicit
   connection requests as well as independent Host-service submissions.
2. Host processes the seal, permanently closes connection admission for that
   endpoint, and revokes its pending and existing direct connections. A request
   preceding the seal cannot later install or activate behind that boundary.
3. Before publishing the sealed acknowledgement, Host accounts for the captured
   Host-queue prefixes and establishes that no admitted direct publication can
   still complete on the affected connections.
4. Closing and quiescence controls continue through the Host connection as
   settlement traffic. The sealed acknowledgement need not wait for complete
   peer quiescence; exposed storage remains retained while access is relinquished.
5. Direct connections must retire before the participant endpoint can retire.

Module draining may preserve existing direct pipes long enough for graceful
completion. Sealing or Host revocation enforces the stopping boundary. A cleanup
procedure requiring a final direct exchange must finish it before sealing; after
revocation it is interrupted, not silently rerouted through Host. Host-mediated
settlement can continue under the previously defined sealing rules.

## Open design work

Remaining work is consolidated in the [Host and endpoint backlog](../backlog/host_endpoint_follow_on.md).
It separates agreed contracts awaiting implementation from optional future policy:

- Generic provider registration, with separate input/HID and Platform report migrations.
- Revision-qualified availability queries and coalesced atomic topology observation.
- Receipt-free disposal of undeliverable newly created asset IDs and the explicit
  retain-if-undeliverable exception.
- Long-running pump/Host work, including schema/report placement, and separately
  selected recovery or watchdog requirements.
- Backpressure alternatives beyond current growable transports and bounded records.
- Optional pair-prohibition authority, lifecycle delegation, service readiness,
  bulk extensions and relaxation of seal acknowledgement timing.

Common/direct message layouts, exact endpoint identities, direct admission guards,
receive fairness, closure integration and migration of existing consumers are
implemented. Their completed validation is recorded in the
[first pass](common_endpoint_first_pass.md#completion-record) and
[direct increment](direct_endpoint_increment.md#implementation-and-validation-record);
they are not outstanding design or migration tasks.

Current sealing waits for direct connection retirement. The earlier publication-stop
acknowledgement described as permissible above is a future relaxation, not current
behaviour. Arbitrary non-lifecycle pair prohibitions and their removal also require
separate authority/API design; ordinary close and lifecycle revocation are delivered.

A public topology registry, per-target subscriptions, automatic dependency graph and
identity-exchange protocol are not required for the agreed starting point. The
Platform report handoff remains paused until separately resumed against the now
implemented communication interfaces. No backlog item is authorised by this record.

Any selected future contract change must follow AGENTS.md, including removal of
replaced paths, explicit version checks, final pre-release version-1 contracts,
affected consumer/data regeneration and validation within that task's scope.

## Existing implementation references

- [Host pump foundation](host_pump_foundation.md) describes common endpoint traffic,
  execution-specific scheduling and current lifecycle records.
- [Asynchronous module lifecycle](asynchronous_module_lifecycle.md) describes
  current loading, startup, replacement and shutdown.
- [Borrowed services](service_borrow_lifetimes.md) and
  [notifications](service_notifications.md) describe existing lifetime protection.
- [Host restriction and maintenance](host_restriction_maintenance.md) and
  [timed waiting](host_timed_wait.md) describe native-loop and wake constraints.
- [Platform report](platform_report.md) records the implemented report checkpoint
  and its paused handoff; the Host communication refactor is complete, but report
  redesign has not been resumed.
