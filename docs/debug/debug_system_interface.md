Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   debug_system_interface.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   27 Jul 2026

# Debug System Public Interface

## Purpose

This document defines the caller-facing contract for the replacement Morphic
Engine debug system.

It covers:

- conditional assertions and diagnostic-only evaluation;
- assertion-level reporting without breakpoint or shutdown effects;
- informational and abnormal event reporting;
- compositional event level and type;
- runtime breakpoint control;
- controlled fatal shutdown;
- last-resort panic behavior;
- bounded transport-facing argument and source-text constraints.

It does not define transport payload layout, queue capacity, log-file format,
thread implementation, optional generated-file format, sink implementation, or
initialisation ownership. Those belong to the technical design.

The pre-redesign implementation and usage inventory are recorded in
`debug_system_audit.md`.

## Design model

The interface keeps these concerns separate:

- **evaluation contract**: whether a condition or expression executes in a
  given build;
- **event level**: info, detail, trace, assert, warning, error, critical, or
  fatal;
- **event type**: condition or event;
- **breakpoint policy**: whether the producer thread enters the debugger;
- **routing policy**: which configured sinks observe an event;
- **control consequence**: continue, request controlled shutdown, or terminate.

Macro names express evaluation contracts, representation and explicit immediate
delivery. The four general logging macros take a fixed level token; assertion
and abnormal-event names fix their classification and control policy. Display
and breakpoint settings remain runtime policy where the selected macro permits them.

Every public macro is statement-like. No debug macro returns a value or may be
used as a condition, operand, assignment value, or return expression. Program
control flow remains explicit in ordinary C++.

This document distinguishes three build-policy tiers:

- **debug development**: includes all development diagnostics, including
  expensive debug-only checks;
- **other development**: includes normal development assertions but omits
  expensive debug-only work;
- **published**: omits development-only incidents while retaining the
  explicitly published assertion-report, warning, error, critical, fatal, and panic contracts.

These are engine diagnostic-policy tiers. They are configured explicitly by
`MV_DEVELOPMENT_BUILD` and `MV_DEBUG_DEVELOPMENT_BUILD`, not inferred from
compiler `_DEBUG` or `NDEBUG` definitions. The initial solution maps Debug to
both tiers, Development to the normal development tier, and Release to neither.

The initial compile-time informational ceilings are trace for Debug, detail for
Development, and info for Release. `MV_COMPILED_INFORMATION_LEVEL` carries this
solution-wide policy.

## Conditional interface

## Legacy Migration Map

The retired legacy surface does not map one-to-one by name.

- `MV_HARD_ASSERT(...)` migrates to `MV_ASSERT(...)` or
  `MV_ASSERT_MSG(...)` when the condition is development-only and the caller
  already owns any fallback or return path.
- `MV_SELF_ASSERT(...)` migrates to `MV_DEBUG_ONLY(...)` when the invoked
  diagnostic reports at a deeper call site, or to `MV_DEBUG_ASSERT(...)` when
  the current usage point should own the assertion incident.
- `MV_FAIL_SAFE_ASSERT(...)` has no direct replacement. Its expression must be
  evaluated in ordinary C++, local control flow must remain explicit, and the
  failure report must be chosen deliberately for the site:
  `MV_ERROR(...)`, `MV_CRITICAL_EVENT(...)`, `MV_FATAL_EVENT(...)`,
  `MV_ASSERT(...)`, or no report when the deeper operation already reported.
- `MV_CRITICAL_ASSERT(...)` migrates directly to
  `MV_CRITICAL_ASSERT(...)` or `MV_CRITICAL_ASSERT_MSG(...)` under the new
  interface.
- `debug_utils::debug_output(...)` migrates to `MV_INFO(...)`,
  `MV_DETAIL(...)`, or `MV_TRACE(...)` when the traffic fits the bounded event
  transport, and to `MV_LOG_RICH(level, ...)` when rich formatting is required.
  Choose an `_IMMEDIATE` form only when synchronous delivery is required.
- `MV_REPORT(...)` and `MV_REPORT_IMMEDIATE(...)` are retired. Select an explicit
  level and use a structured `MV_LOG` form where possible, or a rich form when
  the diagnostic needs it. This migration deliberately changes informational
  reports to obey the information filters.

No migration may rely on a public macro returning a value or participating in
program control flow. Any former condition-participating call must be
restructured explicitly.

### `MV_ASSERT(condition)`

`MV_ASSERT` marks an internal programmer contract, precondition, postcondition,
or invariant intended for development builds.

Contract:

- development builds evaluate `condition` exactly once;
- published builds do not evaluate `condition`;
- failure produces an assertion incident;
- failure may break on the producing thread according to breakpoint policy;
- resuming from a breakpoint permits the caller to continue;
- the macro does not return a value;
- the macro does not own local fallback or return behavior.

An `MV_ASSERT` condition must not contain work required for correctness because
the condition is omitted from published builds.

### `MV_ASSERT_REPORT(format_literal, ...)`

`MV_ASSERT_REPORT` records a failure already detected by ordinary caller code.
It is an assertion-level report, not a development-only assertion check:

```cpp
if (outstanding_references != 0u)
{
    MV_ASSERT_REPORT("Closing with {} outstanding references", outstanding_references);
}
```

Contract:

- present in every build and independent of informational filtering;
- emits structured `assert/condition` metadata at the calling file and line;
- takes the existing bounded format literal and typed `{}` arguments, with
  each argument evaluated once, even if no service is installed;
- accepts no Boolean condition and adds no stringified expression prefix;
- is statement-like and returns no value;
- never invokes breakpoint handling, even when breakpoints are globally enabled;
- never requests debug-system shutdown or changes caller control flow;
- uses ordinary queued submission and its direct fallback, with the same
  best-effort delivery limits as the underlying service.

The caller owns detection, recovery, cleanup and any lifecycle shutdown.
Use this report for convention violations or failure paths which must remain
visible in published builds and continue without debugger intervention. Existing
uses include bundle closure with outstanding complete views, retained-asset
cleanup and module lifecycle failure. Ordinary `MV_ASSERT` and `MV_DEBUG_ASSERT`
retain their existing build gating and breakpoint policy.

### `MV_DEBUG_ASSERT(condition)`

`MV_DEBUG_ASSERT` is the debug-development-only counterpart to `MV_ASSERT`. It
marks an expensive Boolean integrity or diagnostic check which should not run
in every development build.

Contract:

- debug development builds evaluate `condition` exactly once;
- other development and published builds do not evaluate `condition`;
- failure produces an assertion incident at the calling usage point;
- failure may break on the producing thread according to breakpoint policy;
- resuming from a breakpoint permits the caller to continue;
- the macro does not return a value;
- the macro does not own local fallback or return behavior.

An `MV_DEBUG_ASSERT` condition must not contain work required for correctness.

### Required evaluation

No evaluation-preserving or condition-participating assertion macro is
provided. Work required in every build remains explicit ordinary C++, with
diagnosis applied separately:

```cpp
[[maybe_unused]] const bool succeeded = operation();
MV_ASSERT(succeeded);
```

When the result participates in program control flow:

```cpp
const bool succeeded = operation();
MV_ASSERT(succeeded);

if (!succeeded)
{
    return false;
}
```

When a failure requires a published durable report:

```cpp
const bool succeeded = operation();
if (!succeeded)
{
    MV_ERROR("Operation failed");
    return false;
}
```

This separation prevents correctness-critical side effects from being hidden
inside debug macros and prevents reporting or breakpoint policy from becoming
part of program control flow.

### `MV_CRITICAL_ASSERT(condition)`

`MV_CRITICAL_ASSERT` marks an architectural, ownership, lifetime, or accounting
contract whose failure means that trusted engine state has been compromised,
but for which bounded degraded continuation exists.

Contract:

- every build evaluates `condition` exactly once;
- failure is reported in development and published builds;
- durable critical recording cannot be disabled by informational filtering;
- failure may break on the producing thread according to breakpoint policy;
- runtime policy may escalate the incident to controlled shutdown;
- the macro does not return a value;
- without escalation, the caller retains its explicit degraded continuation.

Critical reporting must not hide or replace local recovery code.

### `MV_FATAL_ASSERT(condition)`

`MV_FATAL_ASSERT` marks a condition whose failure means safe application
operation cannot continue, while bounded cleanup and debug draining are still
trusted.

Contract:

- every build evaluates `condition` exactly once;
- failure is reported in development and published builds;
- failure atomically requests controlled shutdown;
- shutdown signalling does not depend on transport or file logging succeeding;
- failure may break on the producing thread according to breakpoint policy;
- the macro does not return a value;
- the call returns after reporting and signalling so the caller can perform
  bounded cleanup or leave its current operation safely.

Breakpoint or sink policy cannot downgrade or suppress the shutdown request.

### `MV_DEBUG_ONLY(expression)`

`MV_DEBUG_ONLY` gates an unconditional expensive diagnostic call or expression
which performs any useful assertion and reporting internally. It is not itself
an incident or breakpoint-capable usage point.

Contract:

- debug development builds evaluate `expression` exactly once;
- other development and published builds do not evaluate `expression`;
- the macro does not inspect or report the expression's result;
- the macro does not allocate incident identity or apply breakpoint policy;
- any incident and breakpoint must be produced by a reporting usage point
  inside the invoked expression.

Use `MV_DEBUG_ONLY` when an expensive diagnostic function reports at a deeper
and more useful point in its own call stack. Use `MV_DEBUG_ASSERT` when the
macro should test the diagnostic function's returned condition and make the
calling usage point the assertion and breakpoint location.

Neither form may contain work required for correctness.

## Message-bearing conditions

Condition text can be captured automatically from the condition expression.
Callers may additionally supply static explanatory text where the expression
does not adequately explain intent.

The public family reserves explicit message-bearing forms:

- `MV_ASSERT_MSG(condition, format_literal, ...)`;
- `MV_DEBUG_ASSERT_MSG(condition, format_literal, ...)`;
- `MV_CRITICAL_ASSERT_MSG(condition, format_literal, ...)`;
- `MV_FATAL_ASSERT_MSG(condition, format_literal, ...)`.

These forms have the same evaluation, reporting, breakpoint, continuation, and
shutdown contracts as their unadorned counterparts.

Message format arguments are evaluated only when the condition is false and an
incident is produced in the current build.

Explanatory text should describe the violated intent or relevant context rather
than merely restating the condition.

## General diagnostic reporting

```cpp
MV_LOG(level, format_literal, ...);
MV_LOG_IMMEDIATE(level, format_literal, ...);
MV_LOG_RICH(level, format_literal, ...);
MV_LOG_RICH_IMMEDIATE(level, format_literal, ...);
```

`level` is one of the fixed tokens `info`, `detail`, `trace`, `warning`, or
`error`, not an expression or a runtime variable. These macros emit `event`
metadata with caller source and ambient identity. They never process a
breakpoint or request shutdown, including when global breakpoint and critical
shutdown policies are enabled. The caller owns recovery and continuation.
Assertion, critical and fatal levels use their dedicated interfaces.

Information levels obey the compile-time ceiling and runtime threshold before
argument evaluation, incident allocation or formatting. Warning and error
reports are present in every build and bypass information filters. Their
arguments are evaluated once even when no service is installed. All delivery
remains best effort when the service or sink is unavailable.

Choose warning for an unexpected condition that still fulfils the operation
contract; choose error for a failed operation or local degraded path that needs
published diagnostics. An expected negative result, such as an optional lookup
miss, need not be an error. Report at the layer that knows the operation context,
avoiding duplicate reports. Deliberately exercising a failure in a test does
not lower its production severity: the test recognises the specific expected
diagnostic and still rejects unexpected failures.

Prefer structured, ordinary delivery. Choose rich representation for content
that needs it, and immediate delivery when write/flush must be attempted before
continuation. These are independent choices.

Runtime call sites distinguish rejected internal misuse from operation failure:
rejected transport identities, invalid control requests and already-detected
structural failures in documents and collections use diagnostic-only assertion
reports. Allocation, codec and module-operation failures use diagnostic-only errors. Ordinary
development invariant checks retain their build gating. Failed ownership
transitions and schema rollback/cleanup retain or use critical reporting because
the operation can no longer establish a coherent result.

Memory-accounting overflow, narrowing and suspicious modular adjustments use
diagnostic-only errors; their reports do not change the accounting operation or
establish an ownership failure. Boolean container integrity checks use
diagnostic-only assertion reports and return failure to the caller.
`CStringBuffer::assert_invariants()` remains breakpoint-capable for explicit
debugger inspection. Vector fallback accessors retain development assertions
with breakpoint processing: they continue with substitute data, rather than
returning a failure result to the caller.

Per-texture batch progress and codec completion are `detail` traffic. The Basis
initialisation, encode, transcode metadata and transcode entry breadcrumbs remain
immediate `info` reports to preserve the last entry point before third-party code
can crash or stall. Returned failures and completion reports use ordinary
delivery. Dynamic paths, parser explanations and reports exceeding the bounded
argument count still justify rich representation; a scalar bitmask does not.

```cpp
MV_LOG(info, "Host: Background workers {}", worker_count);
MV_LOG_IMMEDIATE(info, "Codec: encode begin {}x{}", width, height);
MV_LOG_RICH(error, "File load failed: %s", path);
```

## Informational logging

The following structured conveniences are equivalent to `MV_LOG` with the
corresponding level:

```cpp
MV_INFO(format_literal, ...);
MV_DETAIL(format_literal, ...);
MV_TRACE(format_literal, ...);
```

`format_literal` is static text. The remaining arguments are dynamic values
captured for the event.

The informational levels are:

- `info`: lifecycle and generally useful operational information;
- `detail`: additional state transitions and activity detail;
- `trace`: high-frequency or fine-grained execution traffic.

Informational levels have:

- a compile-time ceiling which may omit a call entirely;
- a runtime threshold controlled by the installed debug-service configuration.

When an informational call is omitted or rejected, its dynamic arguments are
not evaluated. Informational filtering occurs before incident identity is
allocated because no event was attempted.

Warning, error, critical, fatal, and panic events do not become informational
events at different levels.

### Bounded arguments

Normal transport-backed macros use a deliberately small closed set of dynamic
argument types. A variadic function template derives the argument count and
types without parsing `format_literal` on the producer thread.

The transport-facing contract is:

- the technical design defines a small hard maximum argument count;
- every supported argument is captured by value and packed into a bounded
  byte payload;
- compact type tags describe the packed values and allow their offsets to be
  reconstructed;
- any ordering of individually supported types is valid;
- references, pointers, borrowed strings, arbitrary objects, and constructed
  text are not supported;
- unsupported argument types or excessive argument counts are compile-time
  errors and do not implicitly select another reporting path.

The initial supported set covers fixed-width signed and unsigned integers,
`bool`, `float`, `double`, `system_type_id`, `local_type_id`, category-bearing
`type_id`, `system_ids::id_type`, `module_ids::id_type`,
`thread_ids::id_type`, and an owning 16-byte inline text value. Runtime system,
module, and thread identity remains numeric in transport and is resolved by the
consumer through the host registry. Local type identity is normalised on the
producer to its registered short name or an explicit raw failure
representation; generic type identity is normalised according to its category
and has no generic wire tag.
The technical limit is eight arguments, each with a byte-wide type
and a fixed, explicitly aligned 16-byte value slot. Boolean truth is carried by
its type and its value slot remains zero.

The initial format grammar provides sequential `{}` substitutions, `{{` or
`}}` escaped braces, and hexadecimal integer insertions written as `#{}`,
`x{}`, or `X{}` immediately at the insertion site. `#{}` emits lowercase
digits prefixed with `#`; `x{}` emits lowercase digits prefixed with `x`;
`X{}` emits uppercase digits prefixed with `x`. Hexadecimal formatting applies
only to transported 32-bit and 64-bit signed or unsigned integer arguments.
Signed values are formatted as unsigned values of their encoded width so their
two's-complement bit pattern is preserved. Other transported argument types
must reject a hexadecimal insertion explicitly rather than silently falling
back.

Without generated static data, an assertion expression of at most 191 bytes
and a format literal of at most 127 bytes are copied into separate event
fields. The expression is emitted literally and only the format is interpreted
by the writer. Longer typed literals fail compilation; rich or dynamically
constructed formatting belongs on `MV_LOG_RICH`. A possible future generated-data mode may
replace that copy with a static descriptor identity, selected by one
solution-wide build policy. The public call site and argument encoding do not
depend on that optimisation.

## Rich reporting

`MV_LOG_RICH(level, ...)` and `MV_LOG_RICH_IMMEDIATE(level, ...)` use printf
formatting for variable strings, longer explanations and unsupported structured
values. Their level and filtering rules are identical to the structured forms.

- Both format on the caller thread after filtering. Arguments are consumed
  before return; borrowed strings never enter asynchronous storage.
- Ordinary rich reporting formats once into a bounded 4096-byte producer
  buffer. Text of at most 447 bytes is queued when possible.
- Text of 448–4095 bytes, or failed queue submission, takes the synchronous
  direct write/flush path. This size accommodation is independent of an explicit
  requirement for immediate delivery.
- Immediate rich reporting formats under the direct lock into the existing
  service-owned buffer, avoiding the ordinary rich producer-buffer frame.
- Text requiring 4096 bytes or more is a reporting failure. Emit an explicit
  error diagnostic at the original source/incident, without retaining truncated
  content. Empty output or formatting failure emits an invalid-report error.
- Formatting-error diagnostics do not introduce breakpoint or shutdown effects.

Rich reporting is not an allocation-free or re-entrant emergency primitive.

## Explicit immediate delivery

Append `_IMMEDIATE` to any existing assertion or abnormal-event macro, including
`MV_ASSERT_MSG_IMMEDIATE`, `MV_DEBUG_ASSERT_MSG_IMMEDIATE`,
`MV_ASSERT_REPORT_IMMEDIATE`, `MV_CRITICAL_ASSERT_MSG_IMMEDIATE`,
`MV_FATAL_ASSERT_MSG_IMMEDIATE`, `MV_WARNING_IMMEDIATE`, `MV_ERROR_IMMEDIATE`,
`MV_CRITICAL_EVENT_IMMEDIATE` and `MV_FATAL_EVENT_IMMEDIATE`. The condition-only
assertion forms also have immediate counterparts.

The suffix changes delivery only: build gating, condition evaluation, argument
evaluation, classification, breakpoint defaults and shutdown policy stay the
same. Failed conditions alone evaluate their message arguments. Immediate
structured events use the bounded typed arguments and service-owned direct
formatter, without an ordinary rich-format producer buffer.

For an emitted immediate record, the caller attempts direct writing and flushing
before returning or processing its breakpoint. A required shutdown request is
published before potentially blocking output and does not depend on logging
success. Immediate reporting neither drains earlier queued records nor orders
the two logs. It can block on the direct lock and sink; ordinary reporting can
also block when it falls back. Neither path is a nonblocking guarantee.

`MV_PANIC` retains its separate emergency contract. `MV_DEBUG_ONLY` only gates
evaluation and has no delivery variant.

## Abnormal events

Unconditional abnormal events use level-specific macro names:

```cpp
MV_WARNING(format_literal, ...);
MV_ERROR(format_literal, ...);
MV_CRITICAL_EVENT(format_literal, ...);
MV_FATAL_EVENT(format_literal, ...);
```

### Warning

A warning records an unexpected condition which did not fail the current
operation and did not compromise trusted architecture.

Warning incidents:

- are recorded in development and published builds;
- are written to the primary durable log;
- do not request shutdown;
- return control to the caller;
- have breakpoint reaction disabled by default;
- may have their breakpoint enabled by per-usage-point policy;
- may be filtered independently from optional debugger, terminal, and popup
  sinks.

Once emitted, a warning is durable; use `MV_INFO`, `MV_DETAIL`, or `MV_TRACE`
for informational traffic which may be omitted by level policy.

### Error

An error records an operation that failed or entered a local degraded path
which should remain visible in published diagnostics.

Error incidents:

- are recorded in development and published builds;
- are written to the primary durable log;
- do not request shutdown;
- return control to the caller;
- have breakpoint reaction enabled by default;
- may have their breakpoint disabled by per-usage-point policy;
- may be filtered independently from optional debugger, terminal, and popup
  sinks.

The caller owns retry, fallback, failure return, or other local recovery.
`MV_ERROR` is the durable, published-build counterpart to development
assertion reporting, but it is an unconditional event rather than a
condition-participating macro.

### Critical event

A critical event is the unconditional counterpart to
`MV_CRITICAL_ASSERT`.

It is recorded in development and published builds. Durable critical recording
cannot be disabled by informational filtering. Runtime policy may break or
escalate it to controlled shutdown.

### Fatal event

A fatal event is the unconditional counterpart to `MV_FATAL_ASSERT`.

It is recorded in development and published builds and atomically requests
controlled shutdown. It returns so the caller can perform bounded cleanup.

Normal application shutdown is ordinary control flow and should not be
represented as a fatal debug event.

## Panic

Panic is deliberately outside the assertion family:

```cpp
MV_PANIC(format_literal, ...);
```

Panic means that controlled shutdown or the normal debug-reporting
infrastructure cannot be trusted. It is an unconditional, non-returning,
last-resort operation.

Contract:

- panic attempts to capture a usage point and incident identity if those
  mechanisms remain trustworthy;
- panic bypasses the normal MPMC reporting path;
- panic performs best-effort minimal emergency output without allocation;
- panic must not wait indefinitely for emergency-output synchronisation;
- ordinary informational filtering, sink filtering, breakpoint quieting, and
  shutdown policy do not suppress panic;
- panic may enter a debugger when one is attached;
- if execution resumes, panic terminates through the platform fail-fast path;
- complete formatting and durable output cannot be guaranteed.

No `MV_PANIC_ASSERT` form is provided. A conditional panic is written with
ordinary control flow so its exceptional nature remains visible:

```cpp
if (!infrastructure_is_safe)
{
    MV_PANIC("Debug infrastructure cannot continue safely");
}
```

Panic is reserved for fundamental infrastructure failure, unbounded recursive
reporting, or process state in which controlled shutdown itself is unsafe. A
full queue, unavailable ordinary sink, or recoverable emergency-log failure
does not by itself justify panic.

## Breakpoint policy

Breakpoint control is independent of event capture and logging.

The runtime policy provides:

- one process-global atomic breakpoint enable;
- one macro-local static override with `inherit`, `enabled`, and
  `disabled` states at each breakpoint-capable compiled usage point.

Here, per-usage-point is the only meaning of local breakpoint suppression. The
public contract does not define lexical, dynamic-scope, or thread-local
breakpoint suppression.

The local static exists to carry per-usage-point breakpoint policy when the
corresponding control surface is available. In the current implementation it is
consulted only on the producing thread immediately before debugger entry. It is
not exposed as a cross-thread control surface, transported, or retained after
the breakpoint operation.

Template specialisations and separately loaded modules may own separate local
states for the same textual macro expansion. This is acceptable: the initial
contract treats them as distinct compiled usage points. A persistent or
asynchronously addressable source-site policy would require a later registry
or static identity mechanism and is not part of the initial interface.

The default breakpoint reactions are:

- enabled for assertion, debug assertion, error, critical, and fatal incidents;
- disabled for warning incidents;
- not applicable to informational logging;
- not applicable to rich reporting;
- not applicable to `MV_ASSERT_REPORT` (it bypasses breakpoint policy entirely);
- not applicable to `MV_DEBUG_ONLY`;
- special and non-suppressible for panic.

For a breakpoint-capable incident, the producer:

1. captures the incident and usage-point context;
2. attempts publication through the appropriate reporting path;
3. evaluates global and per-site breakpoint policy;
4. enters the debugger on the producing thread when enabled.

Disabling a breakpoint globally or individually does not suppress:

- condition or argument evaluation required by the macro's build contract;
- incident identity allocation;
- transport or fallback reporting;
- durable recording;
- shutdown escalation;
- debugger text output or other sinks unless separately configured.

Breakpoint policy never changes a condition value used by ordinary program
control flow. Public macros do not return such values.

Panic is not governed by ordinary breakpoint quieting.

The implementation may later add break-once, occurrence-count, or other
per-site policies without changing the call-site interface.

## Routing and filtering

Routing policy is distinct from informational production and breakpoint
policy.

Potential sinks include:

- the primary text log;
- the direct text log;
- platform debugger output such as `OutputDebugString`;
- an internal terminal-style display;
- internal popup notifications.

Runtime policy may control presentation to these sinks. Warning and stronger
incidents retain a durable-recording requirement even when optional display
sinks are filtered. Fatal shutdown signalling remains independent of every
sink. General logging may use the event or direct log; an
`_IMMEDIATE` form deliberately uses the direct text log. Representation and
delivery do not change severity.

The originating thread must never be required to perform internal UI work.

## Static call-site contract

Every reporting macro defines one static source usage point.

Call sites follow these constraints:

- informational level is fixed by the convenience name or the general logging level token;
- descriptive text and format text are string literals;
- condition expressions are available for static extraction;
- dynamic format strings are not supported by the public macros;
- source line is captured directly as a fixed-width value in normal events;
- the filename plus extension is extracted from the compiler-provided source
  path using either platform separator and copied into the event;
- a filename longer than 31 bytes is deterministically represented by a
  leading ellipsis and UTF-8-boundary-safe suffix;
- no source-file pointer or disambiguating source hash enters normal transport;
- dynamic arguments are evaluated at most once;
- conditional message arguments are evaluated only when their incident is
  produced;
- filtered informational arguments are not evaluated.

A future pre-build extraction tool remains possible but is not required by the
initial design. If measurement or offline tooling later justifies one,
generated data may replace copied format and source text without changing
public call sites.

## Ambient identity contract

Normal public reporting assumes that valid ambient module, thread, and combined
system identity are always installed.

The surrounding lifetime contract is:

- the executable host thread installs its ambient identity before initialising
  or using the normal debug interface;
- every engine-managed thread installs its thread identity before making any
  normal reporting call;
- a DLL binds to the executable-hosted debug service and establishes its module
  side of ambient identity before exposing code which may report;
- thread and module identity remain valid until their final normal reporting
  opportunity has passed;
- DLL unbinding and thread teardown occur only after code using the normal
  reporting interface has quiesced.

Missing ambient identity is a broken startup, binding, or teardown contract. It
is not represented as an ordinary unknown value in a normal event.

The technical design must provide a bounded infrastructure-failure path for
detecting this contract violation without recursively depending on normal
reporting. That path does not weaken the public assumption that normal callers
always have valid identity.

Ambient memory context is separate from ambient system identity. Debug
reporting must remain available when allocation is unavailable and must not
require a usable ambient memory context.

The executable hosts the debug service. DLL bindings and thread-context
installation provide complete call paths to that service rather than optional
metadata enrichment.

## Behavior summary

The following fixed-width matrices separate exceptional build behavior,
recording, breakpoint defaults, and control consequences. `yes` under durable
recording means that an emitted incident is submitted to the primary or
emergency durable path; physical persistence remains best effort when the
process or reporting infrastructure fails.

### Development-only behavior

Development-only evaluation is isolated to these interfaces:

```text
Interface                Debug development       Other development       Published
-----------------------  ----------------------  ----------------------  ----------------
MV_ASSERT                evaluate/report false   evaluate/report false   omit completely
MV_DEBUG_ASSERT          evaluate/report false   omit completely         omit completely
MV_DEBUG_ONLY            evaluate; no incident   omit completely         omit completely
```

All public macros are statements and return no value.

The matrices include immediate counterparts with the same evaluation and control
policy. `MV_LOG*` denotes the four general forms; immediate forms always use the
direct sink for emitted records.

### Recording behavior

All entries below have the same behavior in development and published builds.

```text
Interface                 Event production                        Durable record
------------------------  --------------------------------------  ----------------
MV_INFO                   build/runtime level policy              policy
MV_DETAIL                 build/runtime level policy              policy
MV_TRACE                  build/runtime level policy              policy
MV_LOG* (information)      build/runtime level policy              policy
MV_LOG* (warning/error)    emit unconditionally                    yes; event/direct
MV_ASSERT_REPORT          emit unconditionally                    yes; queued/direct
MV_WARNING                emit unconditionally                    yes
MV_ERROR                  emit unconditionally                    yes
MV_CRITICAL_ASSERT        evaluate; emit when condition is false  yes
MV_CRITICAL_EVENT         emit unconditionally                    yes
MV_FATAL_ASSERT           evaluate; emit when condition is false  yes
MV_FATAL_EVENT            emit unconditionally                    yes
MV_PANIC                  best-effort emergency emission          best effort
```

### Breakpoint defaults

In the following two matrices, `MV_CRITICAL` groups
`MV_CRITICAL_ASSERT` with `MV_CRITICAL_EVENT`, and `MV_FATAL` groups
`MV_FATAL_ASSERT` with `MV_FATAL_EVENT`. The grouped labels are level/type
shorthand, not additional public macros.

```text
Interface/class           Default reaction             Per-usage-point override
------------------------  ---------------------------  ---------------------------
MV_INFO                   no breakpoint                not breakpoint-capable
MV_DETAIL                 no breakpoint                not breakpoint-capable
MV_TRACE                  no breakpoint                not breakpoint-capable
MV_LOG*                   no breakpoint                not breakpoint-capable
MV_ASSERT_REPORT          no breakpoint                not breakpoint-capable
MV_WARNING                breakpoint disabled          enabled/disabled/inherit
MV_ASSERT                 breakpoint enabled           enabled/disabled/inherit
MV_DEBUG_ASSERT           breakpoint enabled           enabled/disabled/inherit
MV_DEBUG_ONLY             no breakpoint                not breakpoint-capable
MV_ERROR                  breakpoint enabled           enabled/disabled/inherit
MV_CRITICAL               breakpoint enabled           enabled/disabled/inherit
MV_FATAL                  breakpoint enabled           enabled/disabled/inherit
MV_PANIC                  special debugger entry       not suppressible
```

An omitted or non-reporting macro has no breakpoint reaction in that build.
Every ordinary breakpoint remains subject to the process-global breakpoint
enable.

### Control consequences

```text
Interface/class           Consequence
------------------------  ---------------------------------------------------------
MV_ASSERT                 caller continues after optional breakpoint
MV_DEBUG_ASSERT           caller continues after optional breakpoint
MV_DEBUG_ONLY             invoked diagnostic function owns any consequence
MV_LOG*                   caller continues; no breakpoint or shutdown effects
MV_ASSERT_REPORT          caller owns continuation, cleanup and lifecycle policy
MV_WARNING                caller continues; operation contract remains fulfilled
MV_ERROR                  caller owns local recovery from failed operation
MV_CRITICAL               degraded continuation; policy may request shutdown
MV_FATAL                  atomically requests controlled shutdown; cleanup allowed
MV_PANIC                  no return; platform fail-fast termination
```
