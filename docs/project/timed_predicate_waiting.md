Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    timed_predicate_waiting.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    31 Oct 2026

# Timed predicate waiting

## Scope and API

The framework provides finite waits over its existing process-local 32-bit
predicate words. This adds no timer thread, scheduler, message source, or Audio
implementation. Existing indefinite overloads, semaphores and parking gates
retain their behaviour.

`platform::threading::CWaitDeadline`, declared in
[`wait_deadline.hpp`](../../core/platform/threading/wait_deadline.hpp), is an
opaque, copyable deadline. Its implementation lives with the native waits in
[`wait_word.cpp`](../../core/platform/threading/wait_word.cpp).

- `set_after_nanoseconds(uint64_t)` and `set_after_milliseconds(uint64_t)` create
  a deadline relative to one clock observation. They return `WaitError` with
  `code` (`none`, `invalid_argument`, `unsupported`, `native_failure`) and a
  platform-specific `native_error` when applicable.
- `remaining_nanoseconds(uint64_t&) const` returns the same error form and sets
  the output to zero on expiry or failure. Check the error before treating zero
  as expiry. The deadline is never updated by this query.
- Default construction is invalid. Failed construction invalidates an earlier
  deadline and retains the error for subsequent queries/waits. Copies preserve
  the exact deadline; concurrent readers are permitted, concurrent mutation is
  not. Raw timestamps cannot be supplied, persisted, or mixed with performance
  counter values.
- Zero duration is an immediate check. No finite duration or timestamp denotes
  infinity. Durations above `INT64_MAX` nanoseconds and overflowing native-clock
  conversions/additions are rejected. Millisecond multiplication is checked
  before evaluation.

`CWaitPredicate` adds overloads of `wait_until_equal(ticket, value, deadline)`
and `wait_until_not_equal(ticket, value, deadline)` returning `WaitResult`:

| `EWaitStatus` | Meaning |
| --- | --- |
| `satisfied` | An acquire load observed the requested equality or inequality. |
| `timed_out` | The deadline expired and the final observation did not satisfy the predicate or observe control release. |
| `control_released` | The reserved control-release sentinel was observed. |
| `invalid_argument` | Invalid predicate/value/deadline or unrepresentable duration. |
| `unsupported` | The required timed backend or clock is unavailable. |
| `native_failure` | A clock/wait operation failed; `native_error` identifies the native error where supplied. |

`observed` contains the acquired word, including the sentinel on control
release. It is a snapshot, not a promise that the value remains current.
The sentinel remains an invalid requested value. Timed inequality distinguishes
release from ordinary change; the original indefinite inequality still returns
the raw changed word, including the sentinel.

The low-level `platform::threading` equal/not-equal helpers have matching timed
overloads without control-release interpretation. A timed `wait_while_equal`
is only one compare-and-park attempt. Its `WaitWordResult` distinguishes
`awoken_or_changed`, `timed_out`, `invalid_argument`, `unsupported` and
`native_failure`; success may be spurious or interrupted. Callers must recheck
their predicate. The predicate helpers do this automatically.
The primitive checks expiry before entering the native compare-and-park, so an
expired deadline can produce `timed_out` even if the word differs. The higher
predicate helpers perform the final observation and give satisfaction precedence.

## Service use

`CThreadContext::wait_for_new_epoch(epoch, deadline)` returns `WaitResult` and
maintains the existing waiting/running state transitions on every return.
The old overload remains the indefinite idle wait. Message posting and exit
requests use the same epoch publication and wake operations as before.

```cpp
threading::CWaitDeadline check;
const auto error = check.set_after_milliseconds(20u);
if (error.code != platform::threading::EWaitError::none)
{
    // Handle unsupported timing or failure explicitly.
}
else
{
    const auto result = context.wait_for_new_epoch(epoch, check);
    // Interpret result.status; on satisfaction, use result.observed as the epoch.
    // Recheck messages, exit, and callback-published state as appropriate.
}
```

Capture the epoch before draining work/checking exit, as existing service loops
do. Updating the epoch by rereading it after draining messages could consume a
notification for work that has not been processed. A timeout does not advance
the epoch. A service must handle errors explicitly rather than retrying them in
a tight loop or substituting an indefinite wait.

Create one deadline for one logical waiting budget. Reuse it across retries and
ordinary wakeups. A new periodic progress-check deadline can be created for the
next check, but overall and no-progress deadlines remain separate. A predicate
result describes the epoch word, not the callback's independent atomic state.

## Clock, conversions and suspend

The deadline clock measures awake elapsed time. Thread descheduling consumes
the budget; system suspend/hibernation does not. Resume continues the remaining
budget. No wake from system sleep is requested. Wall-clock adjustments cannot
move a deadline. Linux's monotonic clock may be frequency-adjusted by the OS;
this is deliberately different from the RAW profiling clock.

| Platform | Clock and native wait |
| --- | --- |
| Windows | Dynamically resolved `QueryUnbiasedInterruptTimePrecise` (100 ns ticks), with finite `WaitOnAddress` millisecond intervals. Lookup checks KernelBase and Kernel32, since Kernel32 does not always forward the export. Windows 10 or newer is required for timed support. A missing clock returns `unsupported`; indefinite support keeps its existing requirements. |
| Linux/Android | `CLOCK_MONOTONIC` nanoseconds and absolute `FUTEX_WAIT_BITSET_PRIVATE` deadlines with `FUTEX_BITSET_MATCH_ANY`. Existing private futex wakes remain compatible. |
| macOS | `mach_absolute_time` ticks and `os_sync_wait_on_address_with_deadline` using `OS_CLOCK_MACH_ABSOLUTE_TIME`. Requires the existing macOS 14.4 deployment minimum and an SDK declaring these APIs. |

Positive duration conversions round upward using integer quotient/remainder
arithmetic. This avoids overflowing intermediate multiplication even when the
final scaled value fits. Windows rounds remaining nanoseconds up to milliseconds
and caps each native call at `0xfffffffe`, never `INFINITE`. After a native
timeout, the deadline is checked again; an expired finite chunk does not end a
longer budget. Scheduling and native timer granularity can delay return. There
is no hard maximum response latency or exact wake-time guarantee.

On 32-bit Linux/Android, the implementation uses `SYS_futex_time64` when exposed
by the target headers. If unavailable in the running kernel, it falls back only
when the absolute seconds fit the legacy kernel `long` layout. It does not pass
a possibly time64 libc `timespec` to a time32 syscall. An unrepresentable legacy
deadline returns `unsupported`, never an indefinite wait. A failed clock query
returns `native_failure`.

Linux/Android also reject absolute timestamps beyond `INT64_MAX` nanoseconds,
the kernel's signed time representation. Thus the largest constructible budget
depends on current uptime; a duration that fits by itself can still be rejected.

The non-native `CWaitPredicate` fallback can perform immediate observations,
but returns `unsupported` when a timed blocking operation is needed. Its
`CParkingGate` uses untimed locks and is not entered for timed waits.

## Races, errors and lifetime

For valid arguments, predicate satisfaction or control release observed by the
final acquire load wins over timeout. Expired/zero deadlines still permit those
immediate outcomes. A publication after that final load can legitimately race
with a timeout result; this API cannot reconstruct producer timestamps.

Wake-only notifications are not remembered. Producers publish state before
waking; the native compare-and-park closes the notification-before-wait gap.
Transient values and an epoch that wraps back to the expected value retain the
existing ABA limitation. Release/acquire word operations provide publication;
a native wake alone does not publish unrelated memory.

Spurious wakes, futex mismatches and `EINTR` cause rechecking against the same
deadline. Other native errors are returned with their captured error code.
macOS `ENOMEM`/`EFAULT` are reported rather than retried indefinitely. All native
returns lead back through the controlling predicate observation before a timed
predicate reports its terminal outcome.

Keep the word, predicate and ticket storage alive until all waiters and producers
have finished. `release_control()` wakes waiters; it does not join them. Quiesce
users before destruction or a new control lifetime. This work does not make
control acquisition/release concurrent with producer methods safe. The existing
thread package owns shutdown/join and publisher quiescence.

## Validation

Validated locally on 31 October 2026 against the framework working tree based
on `8cccb9d`. Windows used Windows 11 build 26200 and
the Visual Studio v143 toolset. Linux used Ubuntu 24.04 x86-64 in WSL, GCC 13.3
and Clang 18.1.3. These are local results, not hosted CI results.

| Platform/compiler | Configuration | Build and ordinary suites | Additional integration |
| --- | --- | --- | --- |
| Windows MSVC x64 | Debug | Passed; TimedWait 215/215 | Host worker harness passed |
| Windows MSVC Win32 | Debug | Passed; TimedWait 215/215 | Host worker harness passed |
| Windows MSVC x64 | Release | Passed; TimedWait 215/215 | Host worker harness passed |
| Windows MSVC Win32 | Release | Passed; TimedWait 215/215 | Host worker harness passed |
| Linux GCC x64 | Debug | Passed; TimedWait 223/223 | All 5 CTest entries passed |
| Linux GCC x64 | Release | Passed; TimedWait 223/223 | All 5 CTest entries passed |
| Linux Clang x64 | Debug | Passed; TimedWait 223/223 | All 5 CTest entries passed |
| Linux Clang x64 | Release | Passed; TimedWait 223/223 | All 5 CTest entries passed |

The ordinary `-t1` suites include the new
[`TimedWait` suite](../../tests/test_suites/TimedWait_test_suite.cpp). It covers
immediate equality/change, zero and expired deadlines, notification before and
during waiting, real expiry, repeated wake-only notifications and word changes,
deadline reuse, release during equality/inequality waits, unnotified completion,
timeout/completion races, and unchanged indefinite waiting. A real thread package
tests timeout state restoration plus Host message and exit epoch wakeups.
The shared internal loop has deterministic native-return-boundary tests for
completion/release racing with timeout and error/unsupported propagation. These
do not depend on landing a thread in a particular scheduling window.

Integer tests cover upward rounding, intermediate overflow avoidance, invalid
durations and the Windows finite timeout ceiling. Linux additionally tests the
kernel absolute-time range and repeated delivered `SIGUSR1` interruptions without
extending the deadline. Synchronisation probes use bounded waits with scheduling
tolerance; they do not require exact wall-clock wake times.

Each Linux configuration passed `MorphicPolicyIncludes`, `MorphicOrdinary`,
`MorphicReflectionArenaFatal`, `MorphicHostAcceptance1Worker` and
`MorphicHostAcceptance2Worker`. The Windows Host worker harness covered bootstrap
validation, default/shared/dedicated workers, reduction of excessive requests,
asset acceptance and shutdown. The separate full Windows DLL lifecycle fixture
matrix was not rerun for these additive wait overloads.

All seven CMake source-extraction tests passed. Policy checks reported zero
errors and warnings (with the existing identity negative-test suppression).
Whitespace and line-ending checks passed, including checks of the new files.
The changed Visual Studio item/filter files retain their BOM and final-newline
state and use CRLF. Windows compilation emitted warnings in unchanged Basis
Universal sources (encoding and Win32 narrowing); no timed-wait source warnings
were reported. Linux build logs contain no compiler warnings.

Subsequent manual formatting and bracing changes were reviewed without finding
behaviour changes, and edited source headers were updated to shared authorship.
Focused Clang syntax checks of the wait implementation, predicate, thread package
and timed-wait suite passed, as did policy, whitespace and line-ending checks.
The passing matrices were not repeated for these formatting and comment changes.
The focused syntax log is `build/timed-wait/formatting-syntax.log`.

Build, test, Host and source-extraction logs are retained under
[`build/timed-wait/`](../../build/timed-wait/). Windows logs use
`windows-{debug,release}-{x64,x86}-build.log` and `-tests.log`; Host logs use
`windows-{Debug,Release}-{x64,Win32}-host.log`. Linux logs use
`linux-{gcc,clang}-{debug,release}-build.log` and `-tests.log`. Detailed ordinary
suite results remain in each reused build directory's
`Testing/Temporary/LastTest.log`. The build directories are `linux-identity`,
`linux-identity-release`, `linux-bundle-clang-debug` and
`linux-texture-clang-release`, respectively. Validation driver scripts are local
build artifacts; the checked-in ordinary suite and existing harnesses reproduce
the coverage.

### Limits and Audio handback

- Android API 32+ and macOS 14.4+ timed backends are implemented but were not
  compiled or run on those targets. Android retains its experimental designation.
- Linux validation is x86-64 WSL; the 32-bit futex/time64 ABI paths and the
  non-native parking fallback were not exercised on a target here.
- Windows validation is Windows 11 on x64 and Win32. The Windows 10 minimum and
  older-system `unsupported` path follow API availability; they were not tested
  on separate Windows 10/8 machines.
- Physical suspend/resume was not exercised. The suspend-excluding policy is
  based on the selected native clocks; scheduling latency remains unbounded.
- Native-error result propagation is tested with controlled wait returns.
  Actual kernel resource failures and clock failures were not induced.

The Audio chat can now review this contract before implementing its service.
The callback still only publishes atomic state; active service waits can expire
to observe it, while idle waits remain indefinite. The 20 ms check interval is
still a proposal. Review it separately from the 15-second overall and 2-second
no-progress deadlines, including how those Audio deadlines should treat system
suspend. No Audio implementation, Platform pumping, Host event multiplexer or
timer scheduler is included. No push was performed.

## Native references

- [Windows address waits](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress)
  and [precise unbiased time](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryunbiasedinterrupttimeprecise).
- [Linux futex bitset waits](https://man7.org/linux/man-pages/man2/FUTEX_WAIT_BITSET.2const.html)
  and [monotonic clock semantics](https://man7.org/linux/man-pages/man2/clock_gettime.2.html).
- [Apple public address-wait declarations](https://github.com/apple-oss-distributions/libplatform/blob/main/include/os/os_sync_wait_on_address.h)
  and [Mach clock identifier](https://developer.apple.com/documentation/os/os_clockid_t/os_clock_mach_absolute_time).
