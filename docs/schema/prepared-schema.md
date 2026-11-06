# Host prepared-schema conditioning

Approved 6 November 2026. This is a nominated, platform-independent Host
conditioning operation. Host workers execute Host/Core code; requests contain
no module function, callback or arbitrary job. Batch runners and their inline
fallback are unchanged.

## Request and result

`SchemaPrepareRequest` names a retained raw UTF-8 `CAssetId`, a copied parser
policy and preparation limits. Host validates admission, protects the source
before dispatch and releases that protection after the worker finishes reading.
The requester keeps the source immutable and live until admission; clients that
do not separately observe admission keep it through their operation's terminal
reply. Disposal already pending rejects new preparation. Disposal admitted later
waits for the source read to finish.

The conditioning worker parses, validates, resolves and materialises schema data,
then builds the result directories. It knows nothing about Platform's input
records or semantic rules. A failed result contains copied status/stage/reason
and available source line/column, with no borrowed diagnostic strings.

Success returns `SchemaPrepareResult`: an already granted acquisition and a
pointer to `SPreparedSchemaV1`. This temporary result has no ordinary asset ID,
lookup-by-name publication or reusable result mode. The descriptor has explicit
size/version and exposes:

- Stable immutable resolved types.
- Materialised named instances, their type, parent ordinal and byte span.
- Named bulk records with element type, count, stride and byte span.
- Ordinal directories and precomputed type ranges.
- Retained storage and materialised payload byte counts.

`CPreparedSchemaView` copies no data and grants no ownership. Ordinal access is
constant time and type ranges use binary search. Reading never resolves, attaches,
allocates or lazily builds an index. Names, types, payloads and directories all
belong to the Host result and remain valid for exactly its acquisition lifetime.
They are independent of the original raw bytes after preparation completes.
Consumers check descriptor size/version before using it.

## Limits and publication

Initial per-result ceilings are 1 MiB source, 64 MiB simultaneous owned storage
(including parser/resolver scratch and result indices), 16 MiB payload and
262,144 document nodes. Callers may tighten these limits. A limit of 4,194,304
recursive value-write visits bounds expanded construction, including zero-sized structures.
Source/storage limits also constrain parsing and resolution; the visit budget is
not a wall-clock guarantee. Host admits at most four outstanding schema results,
including preparation, acquired results and pending reclamation.

Preparation owns one `CValueWorkBudget` and passes it by required reference through
materialisation, aggregate construction and the value codec. Every participating
writer borrows that same object; base instances, specialisations and bulk records
share the allowance. No budget is installed in thread-local state. Each attempted
writer visit consumes one credit; exactly the permitted number may succeed, and
the next visit reports `storage_limit`, mapped to preparation `limit_exceeded`.
An exhausted allowance stays exhausted across calls. Separate budget objects are
independent, and callers must keep each object alive until synchronous use ends;
the mutable counter must not be used concurrently.

The public materialisation compatibility overload and ordinary role operations
explicitly select an unbounded budget. Internal construction/checking entry points
require a budget reference, with no default that could silently drop a caller's
allowance. Structural admission, declaration-only validation, type resolution,
encoded comparison and result indexing do not consume writer credits. The budget
does not provide a timeout, cancellation or incremental resumption facility.

Host retains publication state through reply backpressure. A protected pumped
invocation cannot receive an untracked staged descriptor; publication waits for
ordinary delivery to become possible. Only actual queued success establishes
the recipient's acquisition. Recipient death before publication retires the
unpublished result. Failure never exposes a partial descriptor. A poisoned queue
can publish before reporting failure: Host treats such work/result delivery as
potentially exposed, retains its storage, stops the affected execution and settles
the reply credit once. It never retries or frees an ambiguous publication.

## Release and failure

The recipient stops all reads, drops every copied view, then sends
`AssetReleaseRequest` using the returned acquisition. Host validates the exact
recipient and token. Release acknowledges the end of the borrow; it also schedules
this temporary result for retirement. It does not promise destruction has already
finished. Normal heavy reclamation runs on the nominated conditioning worker,
and Host retains the work record until that worker acknowledges completion.
Pending reclamation counts as Host work and prevents normal worker shutdown.

Cancellation of a requesting operation does not terminate a running worker or
prove readers quiescent. The recipient retains correlation until the result
arrives and releases a late success. Host may clean up for a removed recipient
only after its execution and accepted batch users are proven quiescent.

Exceptional worker failure has a separate rule: after workers publish terminal
state and their output queues are drained, no further schema access is possible.
Host records that condition permanently. Acquired results remain intact until
their readers retire; unborrowed results may then be reclaimed synchronously on
Host. This failure-only path avoids posting retirement to an exited worker. The
parent allocator and Host context outlive all retained results. No timeout alone
permits reclamation of a result with readers.
