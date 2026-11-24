# Module preamble design

Status: design agreed, 22 November 2026. The separately authorised implementation
and automated qualification are complete within the recorded scope, with final
coordinator review accepted on 23 November; see the
[qualification ledger](module_preamble_validation.md) for failed/unrun qualifications
and retained limitations. The separate 24 November physical follow-up confirmed
both operator checklists and ordinary cleanup. Instrumented native cleanup failed;
the fixture follow-up is corrected and review-complete after consolidated
coordinator acceptance on 24 November, with separate affected-consumer validation
and no corrected native process rerun. The ledger preserves both
outcomes separately from the accepted 23 November implementation scope.

Executive watchdog clarification: the user approved treating its acceptance
watchdogs as stall detectors. A bounded restriction-snapshot retry does not pause
ordinary dispatch; a completion can therefore settle an operation without a
coherent observation proving it met the previous eligible deadline. No new
ordinary-input gate is added. New budget anchors are constructed in candidate
state and required before publishing their operations. Timing unavailability is
tracked independently of operation transitions: warn after one second, repeat no
faster than every five seconds, summarize coherent recovery, and use the existing
timing-failure path after thirty continuous seconds measured by the independent
awake clock. Failed clock/reader operations remain failures. Retry-only work uses
a one-millisecond timed wake, with earlier restriction notifications permitted.

The Host-owned preamble consolidates message processing, routing and lifecycle
handling for serviced and pumped module execution contexts. The initial change
preserves functionality, shutdown invariants, thread assignments and SPSC
transport ownership while removing these mechanics from module implementations.

A returned destroy refusal never resumes module callbacks or retries destruction.
For pumped contexts only, the existing whole-process retention policy can complete
shutdown after the Host seal grant, successful outer invocation unwind, final
ingress accounting and batch settlement. This is a separate process-retention
proof, not successful endpoint retirement: keep the object, dependency storage,
notification pins, binding and complete Host graph. Settle pending unload with
`unload_failed`, stop/join Host workers and batch runners, and return failure.
Other retained failures remain ineligible. Serviced destroy refusal also needs
actual native return/join and parent lifecycle proof; this scoped correction does
not relax those gates or invent child retirement, so unresolved serviced cases
retain the conservative fatal-deadline behaviour.

## Agreed design

### Ownership and execution

The preamble is Host code, not module DLL code. Host instantiates it during
context creation; this replaced the former module thread handoff arrangement. It runs on
the service thread for serviced contexts and the calling thread for pumped
contexts. It executes with Host identity, enters the module execution context's
identity when invoking its handler and restores Host identity on return. The
image-local module identity supplies the Host/DLL distinction; the binding installs
the execution thread identity and provisioning in that image's TLS. Preserve any
Host caller TLS state temporarily changed during pumping; no global module-ID
swapping is required.

### Module interface and origin attribution

Each context supplies one message handler receiving the message and originator
identity. A single logical outbound interface accepts a message and routing
information; the preamble chooses the physical transport or local delivery path.
Destinations include infrastructure, external contexts and the context itself.

Each module-defined execution context is a module-owned class holding its local
configuration and state, including access to the installed common communication
facade. Simple inline facade methods hide the fixed DLL-to-Host function table;
module handlers do not access table entries directly. Thin create/handle/destroy
ABI adapters call the object's methods. Pumped and serviced contexts use this same
module-facing model, with Host driving scheduling. No common inheritance hierarchy
or registration mechanism is required; the Host standard coordinator remains
available for modules that do not need a custom coordinator class.

The user confirmed retaining the two-pointer facade: an immutable Host-lifetime
function-table pointer and an opaque per-context preamble handle. The table is
shared by contexts at the DLL boundary; Host-internal calls remain direct. No
module-load global-table redesign is part of this task.

The preamble knows the module context's system ID, comprising module and thread
IDs, and applies it to module submissions without requiring an origin parameter.
This attribution is independent of the Host identity under which routing code
executes. Host-generated notifications originate from Host; requester, connection
and peer identities are separate payload information where needed.

### Private lifecycle channel

Dedicated bidirectional fixed-capacity TRingTransport communication connects
Host-owned lifecycle machinery. Creation, destruction, direct-pipe installation
and standard handshaking are internal protocols. Their messages never cross into
module DLLs. Message IDs are local Host IDs; payloads and definitions belong in
Host-private code and require no system-wide type registration.

The preamble consumes infrastructure messages and delivers distinct module-facing
notifications when appropriate. Modules never see lifecycle transports or the
underlying protocol messages.

Each lifecycle direction has capacity for 256 messages. Expected traffic consists
of fixed transitions, handshakes and a relatively small variable number of direct
pipe installations. Full-queue handling remains required; this sizing assumption
is not a proof that saturation cannot occur.

### Connection requests

Any context can request a connection; the requester need not be an installation
recipient. Host machinery performs provisioning, handshaking and installation.
Affected module contexts receive installation notifications, and the requester
separately receives a correlated success or failure result. Neither notification
substitutes for the other.

Module consent is retained. The preamble translates a private offer into a
module-facing offer notification. The module accepts or rejects it through the
same outbound interface, correlated by the offered connection token. Host keeps
all installation and handshaking mechanics private.

Report establishment success after both preambles have delivered availability and
acknowledged activation. The establishment deadline continues through those
acknowledgements. Timeout or closure fixes the result once; late acknowledgements
cannot reopen the connection.

Orderly connection closure cancels unread direct PODs and requires settlement of
dependent module conversations before quiescence. Successful posting promises
transport acceptance, not handler delivery after cancellation. This explicit
cancellation is distinct from message loss; safe resource retirement is unchanged.

### Ordinary POD transport and adaptive draining

Ordinary POD pipes between preambles, and between preambles and Host, use fixed
TRingTransport rings. The provisional capacity is 1,024 messages per one-way pipe,
or 64 KiB of payload storage with the current 64-byte envelope. Appropriate sizes
will be investigated later. This does not multiply capacity by the old queue's
three backing buffers. Local message FIFO storage may grow on demand but is
initially capped at 1,024 messages per FIFO, including self messages and any
fallback Host reply FIFO.

Lifecycle processing retains absolute priority. Ordinary pipe selection adapts
to observed pressure and may reorder direct and ordinary Host traffic. Selection
is per eligible pipe, with starvation protection that persists across short pump
calls. FIFO within each pipe remains; no global ordering is introduced. Self
messages participate in starvation protection without treating their growable
allocation's occupancy percentage as external pipe pressure.

This replaces the previously approved fixed 16 direct / 4 Host / 1 self scheme.
The precise pressure comparison, batch size and starvation threshold are specified
in the completed implementation decision record.

### Asynchronous servicing

The preamble directly invokes a synthetic service-asynchronous-processes message
after every N ordinary messages and after available-message processing, before
waiting, synchronising or returning. This can occur with no ordinary input.
N can initially be fixed. This invocation is separate from the self-message FIFO.

The common processing operation is independent of waiting or returning. Serviced
contexts continue or wait; pumped contexts return to their caller. Existing
blocking-work requirements, affinity and asynchronous cleanup remain respected.

### Pressure and scope

Submission retains accept-or-decline, ownership and retry behaviour. Full ordinary
pipes require safe refusal or explicitly retained pending work, never silent loss
or blocking the preamble. Already-owed Host results must survive temporary pressure.
The proposal distinguishes retained acceptance from actual publication wherever
resource lifetime depends on that distinction. Local storage can grow where
reasonable within its 1,024-message cap. Prefer keeping deferred messages, such as
pending asynchronous responses, in their existing operation/state-transition
storage until the destination pipe has space, rather than duplicating them into
another FIFO. That state and its required resources remain live until publication
or explicit protocol cancellation.

Message loss is fatal for the affected module. A full-pipe refusal that leaves
the message with its caller, or a deferred response safely retained in operation
state, is not loss. Failure to retain an accepted message/response obligation is
loss and must fail the module; no overwrite, silent drop or false success is
permitted. Explicit protocol cancellation remains distinct from accidental loss.
Smaller ring and local FIFO limits may be used to test backpressure. No comprehensive
admission framework or new end-to-end delivery guarantee is included.

Keep the response to pressure small and bounded. Classify Host ingress before
reserving fallback reply storage, retaining at most one blocked head per lane.
Zero-reply settlement does not itself need reply storage; it can still wait behind
a blocked FIFO head. Pause new fallback-dependent admission while ready
operation-held output needs a chance to publish, honour existing reservations,
and use finite retry turns with rotation across producers and their operations.
After each turn, give pending ingress an admission opportunity when storage permits;
continuous ready responses must not prolong the pause indefinitely. Fallback
storage reservations must materialise or release during the current dispatch;
work awaiting later input/events retains its obligation in operation state.
Runnable retained heads remain eligible even with an empty ring; blocked heads
are ineligible even with a full ring and are re-evaluated when their blocking
condition changes. Do not scan around blocked messages or introduce a general
dependency/recovery scheduler.

Report sustained saturation, starvation, blocked admission and lifecycle stalls
through existing logging, including identities, occupancy, pending work and time
since progress. Count transient refusals, rate-limit persistent warnings and report
recovery. Distinguish intentionally paused/restricted work from eligible starvation.
Diagnostic thresholds do not introduce new failure deadlines: full pipes alone
remain nonfatal; loss remains fatal. Use these signals to investigate excessive
pressure and tune behaviour/capacity, rather than adding elaborate recovery code.

Preserve current framework-wide shutdown escalation for fatal endpoint/module
failures, including non-Executive modules. Isolated module-failure containment and
restart are outside this refactor. Attribution to the affected module remains
useful for diagnostics and safe cleanup.

An active or pending Executive module is required for framework operation. Any
fatal event in an active or pending Executive, including fatal message loss,
initiates shutdown of the entire framework. This applies during startup and
replacement as well as normal execution. A pending replacement permits an orderly
transition; it does not override shutdown caused by an Executive fatal event.
Shutdown retains the existing resource-draining and safe-retirement requirements.

The initial scope includes self-messaging and shared dispatch, routing and lifecycle
mechanics. It excludes new thread assignments, thread aggregation, dynamic topology
switching, a general scheduler, handler registration and capability discovery.
The migration preserved the existing domain-specific cancellation, timing and
native-affinity requirements.

## Implementation and completion

The [implementation decision record](module_preamble_proposal.md) maps this agreement
onto the delivered code, preserves the original migration sequence, and
records the review decisions. Module consent, 256-message lifecycle rings,
1,024-message ordinary rings, local FIFO growth capped initially at 1,024 messages,
pressure-based draining with starvation protection, retention in existing operation
state and module-fatal message loss are agreed. The bounded progress rules,
pressure diagnostics, failure escalation and orderly closure policies above are
also recorded decisions. The implementation and scoped fixture follow-up are
complete; see the [qualification ledger](module_preamble_validation.md) for current
builds, the physical observations and explicit failed/unrun qualifications. Further
capacity tuning is separate work, not unfinished interface design.

## Related architecture

- [Current Host pump foundation](host_pump_foundation.md)
- [Common endpoint lifecycle design](common_endpoint_lifecycle_design.md)
- [Direct POD connections](direct_pod_connections.md)
- [Direct bulk connections](direct_bulk_connections.md)
- [Service borrow lifetimes](service_borrow_lifetimes.md)
- [Host and endpoint backlog](../backlog/host_endpoint_follow_on.md)
