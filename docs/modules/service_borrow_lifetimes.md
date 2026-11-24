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

`PrepareServiceBorrow` authenticates the sending provider endpoint, exact route
and accepted request; its result is asynchronous. It reserves record storage and an additional prepared-reply
credit before returning success. A rejected offer has not escaped. An accepted
offer must remain valid until provider retirement completes, even if no terminal
acquisition result has yet been produced. Repeated offers for one request fail.

Host emits a `ServiceReply` with notice `prepared` and `SPreparedServiceBorrow` in
its 24-byte payload. The envelope remains 48 bytes. The descriptor pointer is
same-process, same-architecture data, never a serialised or cross-bitness address.
The contract/opcode identifies its provider-defined type; consumers validate its
size, version and domain fields before using it. Host does not dereference it.

A command descriptor remains in its Host operation record until authenticated
completion or provider quiescence. Prepared publication precedes the terminal
result in the common incoming FIFO. Publication distinguishes queued, cancelled
before exposure and backpressure. Uncertain publication retains the view until
consumer relinquishment or final execution drain; it never proves never-published.

Domain readiness remains separate. An input consumer can install readers and
perform its readiness/boundary exchange before acquisition completes. The terminal
result releases the request record, not the borrow. Domain identities belong in
the descriptor or terminal result rather than replacing the Host borrow token.

## Retirement

`ReleaseServiceBorrow` requests cooperative retirement. Providers can initiate the
same transition through `begin_borrow_retirement`. Host records closing durably
and sends `ServiceBorrowControl` notifications through the common incoming FIFO.
Temporary queue pressure leaves transitions pending in the existing borrow record.
Closing cannot overtake a previously queued prepared view.

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
The consumer helper waits for receipt acknowledgement before posting cancellation;
request and cancellation use the same outgoing FIFO. The consumer helper
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

Release, quiescence and cancellation are typed settlement submissions with no reply obligation.
They are idempotent for the exact tracked identity; stale or foreign identities
cannot affect another record. The sender retries definite temporary pressure, never an uncertain publication.

## Execution facade and protocol boundary

`CCommunication` submits POD and owning requests for both execution modes.
Providers use `CProviderClient` for asynchronous completion, resource commit,
borrow preparation/retirement and notification binding. Native restriction and
maintenance callbacks remain in the authenticated `SHostExecutionContext`.
Host and modules require matching version-1 bootstrap/execution layouts and a coordinated
rebuild, including SYSTEM catalogue changes. Unsupported layouts are rejected.

Generic settlement messages share each endpoint's ordinary FIFO. There is no
recursive provider dispatch. Existing advertised providers bind to the module
pump; consumers may be pumps or requested threads.

## Verification

`ServiceBorrow_test_cases.cpp` exercises common-FIFO publication, exact-instance
authentication, retained borrows after terminal success, cancellation tombstones,
late prepared replies, independent consumers, bounded admission, saturated control
queues, terminal-before-drain proof and completion precedence.

The real DLL/SO fixture cases exercise provider-owned `TInstance` storage through
normal retirement, pending-acquisition cancellation, abandoned consumer cleanup,
release after an exit request and cooperative provider failure. Provider storage
remains present through simulated publisher-drain steps after consumer quiescence.
These are protocol/lifetime fixtures, not evidence of native window or GPU cleanup.
