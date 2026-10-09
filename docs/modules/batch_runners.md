Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Module batch runners

The Host owns a pool of threads for coarse work defined by a requesting engine
module. The pool is independent of Host file I/O and conditioning workers.
Requesting module threads publish directly to one shared 128-slot MPMC work
channel. Every requester has its own 128-slot MPSC return channel. A work
descriptor contains a static function pointer, opaque object pointer, captured
memory context, requester route and requester-local correlation ID.

`CBatchClient::submit` returns `queued`, `completed_inline` or `rejected`.
The requester owns the work object and buffers. For queued work, the runner has
exclusive access until the requester consumes the terminal response. Duplicate
object pointers and correlation IDs are rejected while outstanding, including
after execution but before response consumption. Correlation IDs let the
requester retain associated data locally without transporting it to a runner.
The function returns `void`; operation-specific output stays in the object.

Each requester has at most 128 outstanding asynchronous submissions. This
guarantees space for every terminal response. When the shared work channel or
requester credit is unavailable, submission executes inline using the current
requesting thread's TLS and publishes no response. With zero runners, all valid
submissions execute inline. Exit and duplicate checks precede both paths.
Arena publication and recycling retain the slot through transient ring-cell
contention. Consuming a response restores its credit after recycling completes.

After acquiring work, a runner checks the requester's exit control state. Work
already executing finishes; work acquired after exit is requested is discarded.
A failed DLL context installation also discards the work. Both cases return a
terminal response distinct from `executed`, so the requester knows whether the
object contains a new result. The requester consumes every outstanding response
and cleans up borrowed resources before exiting. The Host checks for outstanding
work before releasing an Executive or rendering thread's module binding.

For execution, the runner installs its physical thread identity and the captured
memory context in the destination DLL through its binding. The binding already
installs module identity and the shared debug service. Work objects are not
engine thread objects and receive no module-thread provisioning. A runner may
subsequently execute work from another DLL; each invocation installs that DLL's
own thread-local context.

The command-line option `--batch-runners=<nonnegative integer>` requests a
pool size; its default is eight. The effective maximum is 32 and also respects
`min(reported_hardware_threads, 64) - (4 + provisioned_module_threads +
configured_host_workers)`, floored at zero. Today two module threads are
provisioned, making the fixed Morphic and OS allowance six. On eight reported
hardware threads with two Host workers, the result is zero runners.

Batch runners require the platform's native wait-word support. Where it is not
available, the effective runner count is zero. Requesters atomically advance
the shared work epoch and wake one parked runner after publication. Runners
publish to the requester's return channel and wake its existing wait predicate.
The Host keeps all routes, exit controls, contexts and bindings alive through
draining. It also waits for a runner to finish publishing and waking after the
requester consumes its final response. Each requester is joined before its
return route and wait predicate are released. The Host stops the pool before
destroying any remaining routes.
Package cleanup also waits for return publishers before releasing the wait
predicate during failure cleanup, including failed startup.
