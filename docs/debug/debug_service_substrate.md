Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   debug_service_substrate.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   28 Jul 2026

# Debug Service Substrate

## Scope

`debug/service.hpp` and `debug/service.cpp` provide the first bounded
implementation checkpoint for the replacement debug system.

The service substrate now directly underpins the replacement public macros in
`debug/macros.hpp`. The legacy facade and placeholder implementation were
retired once usage migration completed.

This checkpoint establishes:

- executable ownership through `TInstance<CDebugServiceState>`;
- one module-local service pointer with explicit installation and removal;
- cache-isolated shared control words;
- an executable-owned MPMC event transport;
- a dedicated writer thread which services the event log;
- a mutex-guarded synchronous direct log;
- orderly transport close, drain, thread join, and service destruction;
- a narrow provisional host lifecycle integration;
- the replacement statement-only public macro interface;
- independent focused tests.

It does not migrate existing call sites or implement optional sinks, DLL
exports, or generated static usage-point data.

## Ownership

The executable creates one:

```cpp
TInstance<debug_system::CDebugServiceState>
```

The payload owns all state needed by producers and the writer:

- incident, shutdown-request, configuration, wake, and writer-state words;
- the MPMC arena transport;
- direct-log lock, log, and copied path;
- a 4096-byte direct formatting buffer;
- writer thread lifetime;
- event log and copied path;
- a 4096-byte event formatting buffer;
- the event transport.

The payload is immovable after provisioning. Its owner must not be reset,
re-emplaced, moved, or reattributed until producers have quiesced, the writer
has stopped, and every module-local pointer has been removed.

The host configures both paths and calls `open_logs()` before publishing the
service pointer and starting the writer. This explicit provisioning operation
succeeds only when both logs are open. If the direct log cannot be opened, an event log first
opened by the same call is closed again; a log which was already open is left
unchanged.

`start()` requires both logs already open. Provisioning must not run concurrently
with reporting. Reporting and writer startup never open or reopen files;
direct writes fail when the direct log is closed, including after `stop()`.
This prevents low-level diagnostics from recursively allocating through the
engine while opening a log. Actual opening, writing and flushing failures retain
their existing failure results. After startup, the writer thread exclusively writes,
services, flushes, and closes the event log. Any thread may synchronously enter
the separately locked direct path while the service remains live.

Operations on service-owned state are members of `CDebugServiceState`.
Module-pointer provisioning and the global reporting facade remain free
functions because they act on module-local routing rather than an already
selected service object.

## Provisioning

Each linked module contains one pointer in its own `service.cpp`:

```cpp
CDebugServiceState* s_service;
```

The module-local interface is:

```cpp
get_service()
install_service(service)
uninstall_service(expected)
```

Installation is not concurrently replaceable. It occurs before the module
exposes reporting work. Removal occurs after that work and its threads have
quiesced. The pointer is intentionally non-atomic because concurrent
installation, replacement, or removal is outside the lifetime contract.

DLL export names, ABI versioning, executable callback boundaries, and combined
installation of debug, identity, and memory provisioning remain deferred until
module loading is implemented.

## Shared Words

Every independently shared service word uses
`TCacheLineAtomic<std::uint32_t>`:

```text
incident_counter
shutdown_request
configuration
writer_wake_epoch
writer_state
```

This rule does not apply to per-event transport sequence words or future
macro-local breakpoint overrides.

### Incident counter

Allocation uses an atomic increment and ordinary `std::uint32_t` wrapping
semantics. Identities progress from 1 through `UINT32_MAX`, then 0, then 1
again. Zero is therefore valid after wrap. A discontinuity in a log containing
enough events to wrap the counter is sufficient for later ordering
reconstruction.

Identity allocation uses relaxed ordering because uniqueness does not publish
event content.

### Shutdown request

The word contains an `EShutdownReason`. Zero means no request.

Multiple reporting threads may request shutdown. Reasons are ordered by
severity and updates are monotonic: a weaker request cannot erase or downgrade
a stronger one. The host reads the request and owns coordinated executive
shutdown. The word does not represent shutdown progress and is not cleared.

The current ordered reasons are `critical_incident`, `fatal_incident`, and
`panic_incident`. The existence of a reason does not by itself require every
incident of that severity to request shutdown; that remains reporting policy.

### Configuration

The host thread is the sole writer. It publishes complete word snapshots;
client and writer threads perform read-only relaxed loads. The initial defined
bit is `k_breakpoints_enabled`.

Configuration bits are currently self-contained policy. Publishing associated
out-of-word data would require an explicit synchronization or immutable
snapshot design.

## Event Transport

The transported event contains:

```text
64-bit ambient system identity
uint32 incident identity
uint32 source line
uint8 representation
uint8 event level
uint8 event type
uint8 filename size
12-byte representation-specific metadata
32-byte filename buffer
448-byte representation-specific storage
```

`SEvent` is fixed at 512 bytes and explicitly aligned to 64 bytes. Compile-time
size, alignment, standard-layout, trivial-copyability, and member-offset
assertions fix the representation. Full-width identity fields occupy bytes
0-15. Representation, level, event type, and filename size occupy bytes 16-19.
Bytes 20-31 are a metadata union, the filename occupies bytes 32-63, and bytes
64-511 are a storage union.

Structured metadata stores expression size at 20, format size at 21,
structured content type at 22, parameter count at 23, and eight parameter
types at 24-31. Structured storage retains the 192-byte expression at 64-255,
the 128-byte format at 256-383, and eight explicitly aligned 16-byte parameter
slots at 384-511. Structured content type distinguishes literal text from a
format descriptor.

Report metadata stores a naturally aligned `uint16_t` report length at 20-21
and ten contiguous reserved zero bytes at 22-31. Report storage overlays the
entire region from 64-511 with 448 bytes of text storage. Its capacity includes
the terminator, so a transported report contains at most 447 characters.

The previous generic expansion area is deliberately removed. Future argument
representation growth is supplied primarily by the byte-wide type namespace
and the eight generous typed slots rather than an unstructured tail.

Ordinary events have an empty expression. Assertion events copy their fixed
explanatory prefix and stringized condition into the expression buffer; only
the optional message in the format buffer is interpreted by the formatter.
This keeps legal C++ braces in condition expressions from acquiring format
semantics and requires no producer-side parsing or escaping.

Typed submission receives level and type as compile-time template parameters.
The permitted compositions are:

```text
info/detail/trace  + event
assert             + condition
warning/error      + event
critical/fatal     + condition or event
```

Invalid compile-time combinations are rejected by `static_assert`. The writer
also validates transported metadata before reconstruction.

Typed submission captures the producing thread's ambient
`system_ids::id_type`, the compiler-provided source line, and a static source
file literal. Both `/` and `\` are recognised as separators and only the
filename plus extension is retained. A filename of at most 31 bytes is copied
unchanged. A longer filename is represented by `...` followed by up to the
final 28 bytes. If that initial suffix position is a UTF-8 continuation byte,
it advances to the next leading byte so truncation does not create an orphaned
leading continuation byte. Every representation is explicitly terminated.
The source line normally disambiguates the rare collision between equal
truncated filenames.

Missing or invalid ambient system identity rejects normal submission. This
enforces the surrounding startup, thread, DLL-binding, and teardown contract;
the later bounded infrastructure-failure route remains responsible for
diagnosing a violation without recursion.

The argument representation is:

```text
uint8 parameter count
8 byte-wide parameter types
8 explicitly aligned 16-byte value slots
```

There are at most eight parameters. Parameter `i` has type `i` and value slot
`i`; decoding requires no packed-tag extraction or variable-payload traversal.
Every value is copied with `memcpy` into the beginning of its slot. The event,
the parameter array, and every parameter slot have explicit 16-byte alignment.
Slot tails, unused descriptors, unused slots, and unused representation bytes
are zeroed deterministically. Typed submission encodes directly into a reset arena
slot. If no slot is available, the producer encodes an equivalent direct-path
argument helper and formats while holding the direct-log lock.

The implemented argument tags are:

```text
unused
false
true
int32
uint32
int64
uint64
float32
float64
inline_text
system_type_id
local_type_name
local_type_id_failure
system_id
module_id
thread_id
```

Boolean values consume no payload bytes. `CInlineText16` owns exactly 16 bytes,
including a guaranteed terminator, and therefore carries at most 15 text
characters. `system_type_id` consumes four payload bytes and formats as a
registered symbolic name when one exists, otherwise as an explicit
`unregistered-type:0x...` or `invalid-type:0x...` diagnostic. A registered
`local_type_id` is normalised on the producer to its fixed 16-byte short-name
representation. An invalid or unresolved local ID carries its four-byte raw
value under `local_type_id_failure`, formatting as an explicit
`unregistered-local-type:0x...` or `invalid-local-type:0x...` diagnostic.
`type_id` is inspected on the producer and normalised to the corresponding
system or local representation; it has no generic wire tag. System names are
resolved through the installed host-owned registry, while local names are
resolved only through the producing component's installed local registry.
`system_id`, `module_id`, and `thread_id` each retain their eight-byte numeric
representation in transport. The consumer resolves registered values through
the host registry and otherwise emits an explicit `unregistered-...:0x...` or
`invalid-...:0x...` diagnostic. The remainder of each 16-byte slot must be
zero for every numeric ID representation.

Unsupported C++ argument types, more than eight arguments, and any value larger
than one 16-byte slot are compile-time errors. Pointers, references as
transported state, arbitrary strings, and implicit object conversions are not
admitted.

The initial format grammar supports sequential `{}` substitutions, `{{` and
`}}` escaped braces, and hexadecimal integer insertions written as `#{}`,
`x{}`, or `X{}` immediately before the substitution token. Hexadecimal
formatting applies only to transported 32-bit and 64-bit signed or unsigned
integer arguments. Signed values are formatted as unsigned values of their
encoded width so negative values preserve their encoded two's-complement bit
pattern. Malformed formats, descriptor inconsistencies, unsupported
hexadecimal uses, reserved tags, and formatting-buffer exhaustion are explicit
formatter results.

The transport retains a provisional capacity hint of 128, giving its typed
arena a fixed 64 KiB event footprint. Transported expression literals contain
at most 191 bytes plus their terminator and format literals at most 127 bytes
plus their terminator. Oversized typed literals fail compilation. Runtime typed
entry points reject text which does not fit its corresponding field.
Uninterpreted `submit_text()` uses the format field literally and retains its
existing direct fallback for oversized text. `MV_REPORT` and
`MV_REPORT_IMMEDIATE` provide the rich formatting mechanisms.

The free `submit_text()` facade resolves the module-local service and delegates
to `CDebugServiceState::submit_text()`. It remains as a compatibility facade
and submits short text as uninterpreted content, so braces in existing text do
not acquire format semantics. Typed `submit_event()`:

1. allocates an incident identity;
2. reserves an MPMC event slot;
3. copies the bounded expression and format and encodes arguments directly
   into that slot;
4. publishes the completed event and signals the writer wake epoch;
5. formats and writes synchronously under the direct-log lock when no slot is
   immediately available.

Breakpoint- or shutdown-capable macros use `process_event()`. It captures one
incident context, submits that context through the same transport/direct path,
applies any shutdown request independently of submission success, and finally
applies breakpoint policy using that same context. The macro-local breakpoint
override is passed by reference so debugger controls operating on the producing
thread can change the usage-point state without cross-thread synchronisation.

The host-controlled configuration word currently assigns bit 0 to global
breakpoint enable, bits 1-2 to the runtime informational ceiling, and bit 3 to
critical-incident shutdown escalation. Fatal and panic consequences do not
depend on the critical escalation bit.

For structured format events, the writer validates the descriptor, interprets
the format, and constructs final text in its service-owned 4096-byte buffer.
For report events, it validates representation, length, terminator, reserved
metadata, and unused storage before writing the already prepared text. Invalid
transported events are represented by a fixed diagnostic in the direct log
rather than trusting the malformed event further.

The current record envelope renders the transported system identity as a
fixed-width hexadecimal value, renders the compositional level and type, and
adds the source filename and line when they are available:

```text
[decimal incident identity] [hex system identity] [level:type] [filename:line] text
```

The service-owned direct formatting buffer is covered by the same mutex as
direct-log opening, writing, and flushing. Typed-event fallback and
`MV_REPORT_IMMEDIATE` therefore avoid a 4096-byte producer stack buffer. The
ordinary `MV_REPORT` path deliberately owns a separate bounded 4096-byte
producer stack buffer so it can format exactly once before choosing transport
or direct fallback. Event and direct routes use the same record reconstruction.

## Writer Lifetime

The native entry function validates and casts its opaque pointer, then invokes
`writer_thread_main()` through a service reference. The main function reads as
the sequential writer-thread program: it verifies that the event log is already
open, publishes `running`, and then alternates between draining and parking.
Failure of the open-state check fails writer startup; it does not open the file.

The wake protocol loads an epoch before draining. A producer increments that
epoch after publication and wakes one waiter. If publication races the writer
between drain completion and parking, the changed epoch prevents a lost wake.

Orderly shutdown is:

```text
producers quiesce
transport open -> closing
writer wake
writer drains and recycles every outstanding event
transport closing -> closed
writer flushes and closes event log
writer publishes stopped
host joins writer
host closes direct log
module pointers are removed
TInstance is destroyed
```

Forced transport shutdown and recovery from a writer which cannot complete are
not part of this orderly checkpoint.

## Host Integration

`host.cpp` is a thin provisional lifecycle client. It:

- creates the service and configures both stored log paths;
- requires the all-or-nothing log preflight before installing the service pointer;
- installs the service and starts the writer before provisional worker startup;
- checks the shutdown request in the existing host iteration;
- stops the service after worker shutdown;
- removes the executable-module pointer before owner destruction.

Host log names always contain the native process ID. The optional command-line
form `--log-tag=<value>` adds a caller-selected disambiguation tag before that
ID, producing names such as `morphic_debug.manual-a.p12345.log` and
`morphic_debug_direct.manual-a.p12345.log`. Without the option, the names retain
the `p12345` component. Tags are descriptive only; the process ID preserves
concurrent uniqueness when a tag is absent or reused.

`MorphicTests` applies the same naming convention to each suite-owned event and
direct log and prints its absolute path pattern before running tests. This
allows parallel invocations to be reconciled with captured command output
without sharing writable files.

Test output defaults to `tests/data/output`. The optional command-line form
`--output-directory=<path>` selects another output root, with suite logs written
beneath its `logs` child. Relative paths are resolved from the launch directory.

The current TGA processing and worker-management sketch did not influence the
service internals. Future backing-file, multi-step-operation, module-loading,
identity, and memory-provisioning infrastructure should relocate these
lifecycle calls rather than redesign the service.

## Validation

`DebugService_test_suite.cpp` independently covers:

- owner creation;
- installation, duplicate rejection, expected removal, and accessor state;
- incident allocation;
- complete configuration publication;
- monotonic shutdown reasons;
- compile-time supported-type classification;
- fixed 512-byte event shape and explicit 64-byte alignment;
- byte-wide type ordering and zero-payload boolean encoding;
- the eight-argument and 16-byte slot boundaries;
- integer, floating-point, boolean, and inline-text formatting;
- producer-normalised system, local, and generic type identity;
- transported type-id formatting and valid/unregistered/invalid fallback;
- sequential substitutions, escaped braces, and hexadecimal insertions;
- malformed format, mismatched argument, invalid descriptor, unsupported
  hexadecimal use, reserved-tag, and output-capacity results;
- writer startup and state publication;
- MPMC legacy-text and typed-event delivery;
- ambient system identity capture on the producer;
- source-line capture, basename extraction, UTF-8-safe leading ellipsis
  truncation, and deterministic termination;
- raw system identity and source metadata reconstruction;
- compile-time and writer-side level/type validation;
- explicit common-header, metadata-union, filename, and storage-union offsets;
- report representation, length, reserved metadata, termination, and unused-byte validation;
- 447-character transport, 448-character fallback, full-report limits, and immediate routing;
- identical level/type reconstruction on event and direct routes;
- all-or-nothing pre-install opening of both configured log paths;
- rejection of startup without both logs already open;
- no file opening or engine allocation from reporting to closed logs;
- no reopening from reporting after stop;
- oversized direct fallback;
- orderly drain, close, and join;
- event and direct file output.

Implementation of the direct fallback exposed and corrected an existing
`platform::filesystem::Log::flush()` result inversion: `std::fflush()` returns
zero on success.

## Deferred Design

The following remain deliberately unsettled:

- final queue-capacity policy;
- any justified extension of the deliberately minimal format grammar;
- external string and path reference wrappers, strong typing, and lookup;
- eventual migration from `char` storage to the codebase's enforced UTF-8
  representation based on `std::uint8_t`;
- `OutputDebugString` and internal terminal or popup sinks;
- direct-path recursion handling and lowest-level platform fallback;
- the intentional reporting gap for `platform::threading::CThread` and
  `platform::threading::CExclusiveLock`, which remain outside the normal debug
  transport because the service itself depends on them;
- DLL ABI/version validation and executable-hosted direct call boundary;
- final shutdown-reason catalogue and host policy;
- migration of existing usages and retirement of the placeholder utilities.

When the main debug implementation and usage migration are complete, remind
the project owner to perform a manual formatting pass before treating the
subsystem as finished.
