# Borrowed-service notifications

Implementation contract for the notification increment following
`9db7a3d`. Native Platform resources, modal maintenance and Host sleep scheduling
are separate increments.

## Ownership and interfaces

One notification registration belongs to one service borrow and its exact consumer
instance. `CServiceBorrows` owns the stable registration state inside the existing
borrow record. This couples registration destruction to the existing two-sided
retirement proof without adding an independent release protocol.

- `threading::CNotification` owns an atomic pending latch and non-owning references
  to the pinned thread predicate or pumped client's atomic pending-work flag.
- `threading::SNotificationEndpoint` is a non-owning producer handle exposing
  `signal()`. It has no callback or owning payload. Producers may copy it to their
  own threads, but must stop and join all signal activity before finishing the
  associated borrow's retirement.
- `threading::SNotificationReader` is a non-owning consumer handle exposing
  `pending()` and `clear_pending()`. Only the designated reader clears the latch.
- `services::SNotificationRegistration` is an opaque Host-issued token.
- `RegisterServiceNotification { route, borrow }` requests a registration through
  the existing SYSTEM transport. `ServiceNotificationRegistered` returns the
  route, borrow, status, token and reader through the request's ordinary reply slot.
- `bind_borrow_notification(context, borrow, registration, endpoint*)` joins the
  current flat pump service table. Host authenticates the invoking provider and
  verifies that both tokens refer to that provider's borrow and exact consumer.
- `CThreadContext::observe_epoch()` exposes the existing predicate's acquire
  observation; existing message, exit and batch-return wakeups remain unchanged.
- `SPumpInput::notifications_pending` supplies a conservative pending-work
  indication for pumped consumers. Host continues its existing polling loop.

Registration requires an already published, open borrow and a live threaded or
pumped consumer. Repeating a request for that borrow returns its existing
registration. No rebinding to another borrow, consumer or replacement instance is
allowed. Borrow tokens and notification tokens are distinct public types; stale
tokens cannot name a successor's registration.

`CServiceBorrowClient` tracks a registration request before transport submission.
It installs the returned token/reader only while the matching borrow remains open.
Closing or retired controls can overtake the ordinary registration reply, whose
reader pointer may then already be expired. The helper discards such a reply
without dereferencing the handle and refuses to forget the acquisition while its
registration response is outstanding. Only caller-proven unsubmitted requests may
roll back that tracking. Closing also refuses new registrations and provider binds.

## Acquisition and retirement ordering

1. Provider prepares the ordinary borrow; Host publishes its descriptor.
2. Consumer requests registration, receives its token/reader, and sends the token
   to the provider using the domain's readiness request.
3. Provider resolves the token through the authenticated callback before publishing
   notifications. Domain readiness can require this step before terminal success.
4. Cancellation/release closes the borrow using the existing protocol. Registration
   and package pins remain intact while either side can still access them.
5. Consumer stops resource reads and latch access, then acknowledges quiescence.
   For abandonment, Host uses the existing execution-ended proof instead.
6. Provider stops producers, joins/drains their final publication and signal calls,
   and finishes borrow retirement. Consumer quiescence alone never releases a pin.
7. Host releases the registration and package pin when erasing the retired borrow
   record. A replacement consumer receives neither the old token nor its wakeups.

Thread-package shutdown refuses teardown while notification pins remain; its
destruction contract requires zero pins. Host's existing route/borrow idleness
checks continue to defer package destruction until final provider retirement.
No destructor waits for Host to make asynchronous progress.

## Publication and waiting

The producer first publishes durable queue/control state using its transport's
release protocol, then exchanges the registration latch to pending. Only a
clear-to-pending transition pokes the existing thread epoch or sets pumped work
pending. The latch exchange uses acquire/release ordering, including coalesced
publications; the consumer clears with an acquire/release exchange.

`signal()` returns failure and records a sticky failure indication if the existing
predicate rejects its epoch update, for example because control is invalid or
released. Producers must handle that failure; readers check the failure indication
before waiting. Clearing pending never clears failure. This inherits the current
wait layer's limit: native wake calls return no status, so arbitrary operating-system
wake errors are not detected by this flag. Pumped signalling records an atomic hint.

The consumer repeats this order:

1. Sample `observe_epoch()`.
2. Drain messages and durable data/control state; process exit requests.
3. Clear each relevant registration latch.
4. Recheck all durable data/control state, messages and exit conditions.
5. If none needs work, wait on the sampled epoch using the existing deadline.

A publication preceding latch clearing is observed by the final durable-state
recheck. A publication after clearing sets the latch and changes the epoch, so a
wait using the earlier sample cannot sleep through it. The consumer never takes a
new epoch sample between its last checks and its wait. Pumped readers use the same
clear/recheck discipline and return to Host instead of sleeping.

Host exchanges the pumped client's aggregate flag to zero immediately before a
pump invocation. A concurrent signal is either included in that observation or
remains set for a later invocation. The flag is only a hint: every invocation must
check durable state and the registration latches, and bounded unfinished work is
reported through the existing progress result. Clearing the aggregate does not
clear registration latches, and Host never gates pumping solely on this flag.

## Validation scope

Deterministic tests cover both sides of latch clearing, publication after the
final recheck but before waiting, coalescing and preservation of message/exit wakes.
Host tests cover exact-instance/token authentication, package pin retention,
pending cancellation, abandonment, replacement and pumped pending-work indication.
Delayed registration replies are covered after closing, retirement and replacement.
Real DLL/SO fixtures exercise registration/readiness, publication, cancellation
and shutdown with a producer completing its final signal before retirement.
