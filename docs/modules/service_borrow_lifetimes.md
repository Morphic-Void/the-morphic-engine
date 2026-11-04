# Service borrow lifetimes

Committed in `9db7a3d`, this extends the [Host foundation](host_pump_foundation.md)
with provider-owned borrowed views. The later
[first Windows window](platform_first_window.md) uses this protocol for native
window uses; resource-specific types and cleanup policy remain in Platform.

## Ownership

The provider owns the descriptor, its backing resource and all resource-specific
dependencies through its existing curated containers. Host owns an independent
borrow record in `CServiceBorrows`, embedded in `CServiceRouter`. A bounded
`TUnorderedCollection` supplies stable record storage. A record identifies the
exact provider and consumer instances, route and acquisition request; it never
contains a resource destructor or interprets the descriptor.

Each accepted borrow gets a nonzero, monotonically issued Host token. Tokens are
not collection indices and are never reused. There are at most 256 live records;
exhaustion rejects publication before the descriptor can escape. Multiple uses
of one resource have distinct records. Host's route/client retirement checks
protect both bindings, client records and memory contexts until retirement.

Consumers own their local protocol state and dependent objects. The Core
`CServiceBorrowClient` helper keeps at most 512 pending/acquired records, covering
the Host's 256 operations and 256 surviving borrows. It never owns or destroys a
provider object, and its destructor cannot substitute for asynchronous release.

## Publication

`services/service_borrow.hpp` defines `SServiceBorrow`, `SServiceView`,
`SServiceBorrowOffer` and `SPreparedServiceBorrow`. One acquisition request can
publish one borrow. A single descriptor may group several related views, such as
desktop text and control endpoints.

`prepare_borrow` authenticates the currently invoked provider, exact route and
accepted request. It reserves record storage and an additional prepared-reply
credit before returning success. A rejected offer has not escaped. An accepted
offer must remain valid until provider retirement completes, even if no terminal
acquisition result has yet been produced. Repeated offers for one request fail.

Host emits a `ServiceReply` with notice `prepared` and `SPreparedServiceBorrow` in
its 24-byte payload. The envelope remains 48 bytes. The descriptor pointer is
same-process, same-architecture data, never a serialised or cross-bitness address.
The contract/opcode identifies its provider-defined type; consumers validate its
size, version and domain fields before using it. Host does not dereference it.

Offers made inside a pump are held until consumed command prefixes are confirmed.
An invalid prefix never authorises publication. Prepared publication precedes the
terminal result in the ordinary reply stream. Publication explicitly distinguishes
queued, cancelled before exposure and backpressure; ordinary reply cancellation
is not mistaken for delivery.

Domain readiness remains separate. An input consumer can install readers and
perform its readiness/boundary exchange before acquisition completes. The terminal
result releases the request record, not the borrow. Domain identities belong in
the descriptor or terminal result rather than replacing the Host borrow token.

## Retirement

`ReleaseServiceBorrow` requests cooperative retirement. Providers can initiate the
same transition through `begin_borrow_retirement`. Host records closing durably
and sends `ServiceBorrowControl` notifications through the independent control
lane. A full ordinary inbox cannot consume cleanup capacity. Full control queues
leave transitions pending in the existing borrow record, requiring no new allocation.

The consumer stops every read and releases dependent consumer-side objects before
posting `ServiceBorrowQuiescent`. This message is an assertion of completed local
cleanup, not a request for Host to perform that cleanup. Merely requesting release
does not establish quiescence. The helper refuses a quiescence acknowledgment until
it has observed the prepared result or proof that publication never occurred.

Closing controls can overtake ordinary prepared replies. The helper records the
withdrawal and subsequently consumes a late prepared result without exposing its
pointer. Callers observe ordinary replies in transport order and retain helper
state until both terminal acquisition and retirement have been observed.

Host reports the provenance of consumer quiescence to the provider:

- `never_published`: Host proved that the view did not escape.
- `relinquished`: the authenticated consumer explicitly discharged its obligations.
- `execution_ended`: Host observed terminal execution before draining final queues,
  and registered batch jobs and return publishers are gone.

The last case proves registered CPU access ended; it cannot prove GPU/native
resource cleanup. The provider must still satisfy its own resource-specific
requirements, drain publication/callback activity and detach any reverse references
into consumer storage. The client record remains protected during this work.

After receiving consumer-quiescent control and completing those requirements, the
provider frees the use/storage and calls `finish_borrow_retirement`. A valid finish
requires no allocation or fresh queue credit. Host retains the record until its
retired notification can be queued, or the recipient is terminal. Consumers retain
their notification-related state until retirement, not just their own quiescence.

Ordinary route close returns busy while any operation or borrow remains. Consumer
replacement never inherits a view or a token. Global stop closes publication and
keeps retirement controls flowing. Provider `stopped` is not permission to discard
borrow records; a premature stopped claim fails the protocol and retains state.
Unproven quiescence or failed cleanup can prevent shutdown from completing. A
deadline never authorises destruction or unbinding.

## Cancellation

`CancelServiceRequest` cancels the client's wait, separately from provider work.
The sender waits for receipt acknowledgment before posting cancellation, so the
cleanup lane cannot overtake the original request's admission. The consumer helper
enforces this prerequisite. `abandon_unsubmitted` removes local preparation only
when the caller proves transport submission never succeeded.
Cancellation before dispatch removes work without invoking the provider. After
dispatch, Host delivers `ServiceRequestCancelled`, resolves the client's wait once
and retains the operation until the provider completes or establishes stopped
quiescence. Late completion drains that operation without sending another terminal
result. A completion already confirmed before cancellation keeps precedence.
Cancellation still withdraws the associated borrow when that successful completion
is already recorded or queued and its operation record has been removed. Thus a
consumer that closes local access while cancelling cannot strand a live borrow
merely because success won the terminal-result race.

An existing borrow enters retirement. A later offer for a cancelled operation is
rejected; provider code retains responsibility for cleaning up its unexposed
candidate. No late success can publish to a replacement client.

Release, quiescence and cancellation submissions use zero-reply cleanup admission.
They are idempotent for the exact tracked identity; stale or foreign identities
cannot affect another record. The sender retries a failed transport submission.

## Pump adapter and protocol boundary

`modules::SHostPumpServices` is one current table containing queued POD and owner
submission, terminal completion, borrow preparation, begin/finish retirement, and
notification binding and restriction/maintenance callbacks. Providers require
the exact current size and version before using the
table. These fields reject mismatched binaries; older layouts are not supported.
Host and modules use matching headers and coordinated rebuilds, including changes
to the SYSTEM catalogue. No native types or Platform opcodes are added to Host.

Generic SYSTEM cleanup messages travel through the existing pump message batch;
there is no recursive provider dispatch or new execution capability. The protocol
also supports threaded consumers. Service providers still use the pumped adapter.

Window uses implement generic borrow retirement and quiescence, with domain
cleanup preceding the appropriate acknowledgment; separate window-release opcodes
are not implemented. The same lifetime boundary is the starting point for input
connections, whose readiness and stream barriers remain unimplemented. Platform
owns close policy and native resource destruction. Notifications
use [borrow-bound registrations](service_notifications.md), retaining their wait
targets through final publisher cleanup. [Modal restriction and maintenance](host_restriction_maintenance.md)
and [native Windows windows](platform_first_window.md) are implemented separately.
Worker preparation and service-thread provider support remain future work. The
next input/direct-POD-connection review must assess common lifetime support and
domain-specific barriers without treating proposed endpoint layouts as implemented.

## Verification

`ServiceBorrow_test_cases.cpp` exercises consumed-prefix publication, exact-instance
authentication, retained borrows after terminal success, cancellation tombstones,
late prepared replies, independent consumers, bounded admission, saturated control
queues, terminal-before-drain proof and completion precedence.

The real DLL/SO fixture cases exercise provider-owned `TInstance` storage through
normal retirement, pending-acquisition cancellation, abandoned consumer cleanup,
release after an exit request and cooperative provider failure. Provider storage
remains present through simulated publisher-drain steps after consumer quiescence.
These are protocol/lifetime fixtures, not evidence of native window or GPU cleanup.
