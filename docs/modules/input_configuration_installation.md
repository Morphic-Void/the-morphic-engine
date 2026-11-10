# Platform input configuration installation

Approved 6 November 2026. The portable implementation combines the nominated
[Host schema conditioner](../schema/prepared-schema.md) with incremental Platform
validation and installation. It does not execute Platform jobs on a Host worker.
The [portable mapper](input_mapping.md), implemented 8 November 2026, adds resolved
evaluation tables and tighter threshold-source validation to installation.
[Desktop acquisition](desktop_input.md) and [controller acquisition](controller_input.md)
now supply Windows native input to that mapper.

## Commands and ownership

Input service contract 2 now uses version 4 through the controller increment;
configuration format remains version 1. It retains opcode 1 (`SInstallConfiguration`)
and opcode 2 (`SRetireConfiguration`). Installation carries a retained raw asset ID,
format version 1 and reserved zero. Its terminal payload is `SInstallationResult`
(24 bytes): installation token or copied error/category/definition/field detail.
An error has no usable installation token. Retirement carries an installation
token. All requests retain the existing routed command identity and reply credit.
Diagnostic category ordinals 0–11 follow the structure order in the v1 schema
table; 12 denotes a failure outside a particular table. Definition/field identify
the offending ID and field where available. A schema error instead uses those
two fields for source line/column, with zero meaning unavailable.

The submitting client owns the source asset's lifetime, keeping it immutable and
live through installation completion. Platform never adopts its storage or reuses
the client's acquisition. Host takes its own source protection before conditioning.
Platform receives a separate acquired intermediate, copies everything it needs,
then ends all intermediate reads and releases it before committing success.
Final records, names, indices and profiles belong to Platform and have no source
or prepared-schema pointers.

An installed configuration is a Platform resource independent of the submitting
client's subsequent lifetime. Any connected caller holding its token may retire
it. Tokens are nonzero and never reused during the Platform runtime. Replacement
is a new installation; existing connections continue using their old immutable
installation until released. Failure leaves existing installations unchanged.

## Admission and stages

At most eight configurations may occupy storage, including candidates and
retiring resources; at most one preparation runs at a time. Admission reserves a
completion before creating candidate state. Capacity pressure returns `busy`.

1. Submit the nominated Host request, retaining correlation through backpressure.
2. Check prepared descriptor version and readiness, exact v1 enum meanings,
   structure fields, offsets, sizes, strides and alignments.
3. Inventory records and relationships against limits before copying definitions.
4. Copy fixed records and names into small owned pages.
5. Build ID and relationship indices with resumable merge sorting; reject
   duplicate IDs/relationship keys.
6. Validate references, memberships, source/control/direction compatibility,
   activation/action combinations, connection kinds, reserved values, modifier
   rules, finite analog parameters and hysteresis, and native-repeat eligibility.
7. Build binding order by profile/context/action/ID and reject ambiguous absolute
   scalar/vector contributors. Incrementally resolve modifier control indices,
   binding instructions and default/explicit parameters into owned immutable
   evaluation pages. Prepare per-profile ranges, contexts and subject tables.
8. Detach all intermediate views, release the Host acquisition, and wait for the
   release reply. Then commit the resource and publish its token.

The record layouts and control catalogue remain in
[configuration schema v1](platform_contracts_v1.md#configuration-schema-v1).
Control code 72 is spelled `key_delete`, because the schema rejects the C++
keyword `delete` as an identifier; its numeric meaning is unchanged.
Defaults are 1,024 actions, 128 contexts, 32 profiles, 8,192 bindings, 8,192
sources, 8,192 analog parameter records, 32 window policies, 8,192 shortcuts and
32,768 relationships in total. Definition names are at most 127 UTF-8 bytes.
IDs are nonzero and unique within each definition category. Relationships use
bulk records; unknown record categories and wrong instance/bulk roles are rejected.

Regular and chord activations produce digital held state from digital sources;
chords require an all-of modifier set. Start/release activations produce pulses
from digital sources and may require modifiers. Threshold activation produces
digital state from a trigger or projected stick direction; pointer and wheel
thresholds are rejected. Direct activation
produces scalar/vector state or relative wheel deltas with a compatible source.
Explicit analog values are finite, `0 <= dead_zone < 1`,
`0 <= neutral_enter < neutral_leave <= 1`, and `0 <= release < press <= 1`;
scale is finite. A zero parameter reference selects the source default.
Window policy/shortcut data is validated and retained; attaching policies to
native windows and executing reserved shortcuts remain part of window/mode integration.

## Cadence, cancellation and durable completion

Installation has one five-second eligible awake-time budget starting at Platform
admission. Host submission retries, conditioning, copy/validation, release and
commit all share it. Restrictions pause eligible time and ordinary preparation;
cleanup remains permitted. No retry resets the deadline.

Each pump gives configuration work at most 500 microseconds within the remaining
shared two-millisecond pump budget and 256 conservative check credits. Copy and
page growth remain below 32 KiB per turn. Sorting and reference checks consume
credits too. Page-wise retirement bounds cleanup. Time limits are cooperative:
one already-entered bounded step can finish after its clock deadline. Runnable
local work requests another pump; waiting solely for Host replies permits sleep.

`SHostPumpServices` version 2 adds `commit_resource`. It authenticates the exact
provider and operation, requires confirmed command consumption, rejects prior
cancellation/recipient closure and stores terminal success in the already
reserved operation/reply capacity. It returns `busy` before command consumption.
Once accepted, cancellation cannot turn that committed success into failure.
Platform makes the candidate visible in that same invocation without another
allocation or failure point. Ordinary `complete` semantics remain unchanged.

Before commit, cancellation/timeout prevents visibility and may report failure
before cleanup finishes. Platform detaches views, preserves outstanding Host
correlation, releases any late successful result exactly once and discards its
candidate incrementally. It keeps the admission slot until that cleanup and its
terminal completion are resolved. Worker execution is never forcibly interrupted.

Retirement returns `busy` while any connection still references the installation,
including pending acquisition or closing connections whose borrow has not retired.
An accepted retirement is rechecked at commit. Successful commit makes its token
unavailable immediately; storage is reclaimed incrementally afterward. Shutdown
cancels candidates and drains connections, acquisitions, replies and retirement
before Platform reports stopped. Future window-policy users must participate in
the same installation reference check before those operations are added.

## Synthetic verification

Coverage includes source-independent prepared views, type directories,
preparation limits and zero-sized expansion; exact layouts, IDs and references;
incremental sorting and independent profiles; real one/two-worker Host result
release and failure cleanup; cancellation across preparation/release/commit;
reply pressure; deadline expiry; and busy/stale installation retirement. These
tests exercise the portable contracts without requiring a native device backend.
The 8 November resolved-plan, default-parameter, modifier-index and threshold-source
tests are included in [portable mapper validation](input_mapping.md#synthetic-acceptance).
