# Diagnostic memory accounting

Updated 16 September 2026. Implementation and validation are complete, with
coordinator approval and Ritchie's code review complete. Ritchie has authorised
committing the reviewed scope. Ritchie performs pushes manually.

Accounting task: `01a0a9d6-bd37-7731-b91a-4282e76bf2bc` (local).
Coordinator: `01a09fd4-73ad-7ce2-a3e4-efcdadb454dd` (local).
Dependent stage task: `01a0a4d6-e506-7912-8cf8-0ace7c96453a` (local).
Work directly in the shared main checkout, sequentially with the dependent task.
Preserve existing documentation and user edits. Ritchie performs pushes manually.

Ritchie accepted the outline plan and authorised completing its technical details
and obtaining coordinator review. That review is now complete. Commits
require coordinator review, Ritchie's own completed review and explicit instruction.

## Contract

Accounting is diagnostic in development and release builds. Counter discrepancies
may originate in an earlier operation. They must not themselves reject allocation,
deallocation or ownership/context transfer, or request application shutdown.
Retain allocator compatibility, pointer/alignment/size validation, coherent source
contexts, payload registration and module identity constraints, and actual allocator
and transport failure handling.

Allocation count remains atomic unsigned 32-bit; bytes remain atomic unsigned
64-bit. For each independently, take the before value returned by its relaxed
fetch_add/fetch_sub and derive after at the same unsigned width. Report precisely
when `((before ^ after) & high_bit_mask) != 0`. Retain the adjustment. Report either
direction, including modular underflow/wrap crossings, and do not repeat reports
for further updates in the same high-bit state. The counters are not one atomic
snapshot. Concurrent clusters are acceptable. This heuristic cannot detect every
wrap for arbitrary adjustment magnitudes; no rate limiter or transaction is added.

## Primitive and caller changes

1. Change private CMemoryContext::add/sub to void operations. Use uint32_t count
   and uint64_t bytes internally. Perform both atomic adjustments, preserve their
   returned values, then report each transition using those saved values. A zero
   adjustment for either counter is valid and does not suppress the other update.
2. Preserve allocation/deallocation ordering around allocator callbacks. An
   allocation reservation is unconditional after request validation; null allocator
   results receive the inverse accounting adjustment. A valid deallocation always
   reaches its allocator; restore its subtraction if the allocator rejects it.
   Misaligned allocation results remain failures: remove their reservation only
   if the allocator successfully releases the returned storage. If cleanup fails,
   retain its accounting and existing real-failure reporting. No accounting result
   is tested as a condition for these operations.
3. Keep memory::reattribute's public bool and size_t-count/uint64_t-byte signature.
   Preserve same-context success and cross-allocator rejection. For compatible
   distinct contexts, narrow the count modulo 2^32, add to target, subtract from
   source, and return true. If size_t can represent a count above UINT32_MAX,
   report that representability loss without rejection; include original and
   narrowed count. Zero count, zero bytes and independently wrapped totals are
   accepted diagnostic adjustments, including an all-zero no-op. The function
   cannot infer ownership validity from counter amounts. Ownership preflight
   remains with callers and allocator compatibility remains in the primitive.
4. Preserve existing container bool interfaces and their preflight. Direct callers
   are CMemoryToken, CStableStrings, TPodOrderedSlots, TPodUnorderedSlots,
   TOrderedCollection, TUnorderedCollection and CErasedOwner. Other primitive
   wrappers, byte/string buffers, instances/FIFOs and the current baked block
   forward through these paths. Their existing failure checks can remain: the
   primitive no longer returns false because of accounting. No child reattribution
   transaction, new whole-aggregate hook, visibility reorganisation or live-document
   transfer is added here.
5. Audit both CErasedOwnerTransport and CErasedOwnerMsgTransport post/read paths.
   Their critical assertions after successful preflight may remain as assertions
   of structural/transport invariants. Document that accounting cannot cause those
   assertions to fail. Retain full/unready rejection, identity admission, allocator
   rejection and failed-post ownership restoration. Prove continued post/read with
   damaged source/transport/recipient totals in regression tests; do not merely
   remove assertions that protect actual failures.

### Aggregate representability disposition

Existing aggregate observers sum uint32_t counts and uint64_t bytes directly;
inspection found no separate aggregate representability rejection gate in the
current container or erased-owner transfer paths. CMemoryToken asserts its computed
physical allocation count fits uint32_t; its storage-capacity limits remain
structural constraints and this assertion is not a mutable context-balance check.
No widening or general redesign of aggregate observers is included here.

The current primitive's add/sub range checks and zero checks can reject diagnostic
totals, so remove that coupling as described above. Narrowing already performed
by a caller cannot be reconstructed by the primitive. This limitation is explicit;
the high-bit heuristic does not promise to detect an earlier aggregate sum wrap.
The dependent task owns checked cumulative aggregation. Its overflow report must
retain modulo totals and proceed with context replacement after successful
ownership preflight; it must not introduce an accounting-only failure result.

## Reporting and the discovered recursion dependency

Use existing structured MV_ERROR events, present in release and carrying
EShutdownReason::none. Include the affected context address and its system ID,
counter identity and operation (in the format literal), adjustment, before and
after. Keep formats within the 128-byte format capacity and parameter count within
the existing event capacity. Do not build allocating strings or perform context
name/registry lookups. Existing breakpoint policy remains applicable.

The structured event queue is an embedded bounded arena and scalar argument
encoding and formatting use fixed storage. However its direct fallback currently
holds m_direct_lock and lazily opens Log through makeNativePath. That allocates
TPodVectors through the ambient memory context. A transition caused by these
allocations can recursively report and reacquire the lock. Merely using MV_ERROR
is therefore insufficient.

Ritchie identified the simpler resolution: explicitly open logs at startup and
remove lazy opening. Inspection confirms CHost::initialise_debug_service already
configures and opens both logs before install_service and start. The initial
bounded-log-opening proposal is withdrawn; no log/path conversion change is needed.

- Make open_logs the explicit startup operation, performed before installing the
  service for reporting and before starting the writer. Retain its existing
  all-or-nothing failure behavior. Its temporary allocations then happen before
  this service is reachable through reporting. This is a provisioning operation,
  not an operation that may run concurrently with reporting.
- Remove lazy open from report_immediate_va, write_direct_record and
  write_direct_event. These paths use the existing direct log only when open;
  otherwise their write fails without opening a file or allocating through the
  engine. Keep existing locking and formatting.
- Require both logs already open in start; the writer also checks its existing
  event log instead of opening it. Keep actual thread/file failure returns and
  existing stop/close behavior. The Host's successful startup sequence needs no
  change. Do not silently reopen files after shutdown or startup failure.
- Revise the service.hpp comment that currently makes open_logs optional. Audit
  all test/service setup paths: ErasedPod diagnostics currently rely on writer
  opening, and DebugService explicitly tests lazy opening. Replace those
  assumptions with explicit startup opening and closed-log rejection coverage.
  Incident-only fixtures that deliberately install an unstarted service need not
  create files; installing a service remains distinct from starting its writer.
- CRT/OS write internals may allocate independently; the requirement is no engine
  allocator/accounting dependency or recursive engine reporting while emitting
  these structured diagnostics. Do not claim globally heap-free I/O.

Additional production changes are limited to debug/service.hpp/.cpp. Keep
platform/filesystem/log and native-path conversion unchanged. No alternate
logger, recursion-suppression mechanism or lifecycle/recovery framework is added.

Without an installed debug service or with an actual log I/O failure, existing
reporting availability/failure behavior applies; no durable retry service is added.

## Regression validation

Use existing CMemoryToken, ErasedOwner and DebugService test suites and their
current registration. Add a narrow SMemoryContextTestAccess friend, following the
existing SDebugServiceTestAccess/SLiveDocumentTestAccess convention. Define access
in test support only. It may seed isolated counters and invoke private adjustments;
there is no production counter reset/setter or conditional class layout.

- Table-driven boundary adjustments: independent count and bytes; add/sub; both
  high-bit directions; same-bit silence; underflow/wrap; zero adjustment and
  arbitrary-magnitude cases showing the documented heuristic limitation. Check
  exact modulo totals and exact incident counts and log contents.
- A small bounded concurrent test around a threshold uses several threads with
  equal unit adjustments. Expect exactly one crossing per counter for a monotone
  phase and exact before/after/delta in its report. Include crossing back in a
  separate phase. Combine this with inspection that production values come only
  from fetch return values, never separate loads. No benchmark/stress framework.
- Real allocator callbacks count allocation/deallocation calls. Inject discrepancy
  before successful allocation, valid deallocation and actual allocation/deallocation
  failure. Check pointer/result, callback execution, accounting inverse only for
  real failure, misalignment rejection and failed-cleanup reservation retention.
- Token and representative aggregate/erased nested-owner transfers preserve data
  addresses and replace all relevant contexts despite discrepancies. Both owner
  transports successfully post and read with discrepant totals. Enable critical
  shutdown in the test debug service and verify no shutdown request. Preserve
  existing incompatible-allocator, mixed-source, identity and full/unready tests.
- Logging tests explicitly open logs before installing the service, then use a
  rejecting/counting ambient allocator: a diagnostic must be recorded with no
  engine allocator callback. Exercise direct fallback for unavailable/full event
  transport and normal queued delivery. Configured but unopened logs must not be
  opened by reporting or start; verify failure without engine allocation or file
  creation. Retain missing-service and explicit startup file-open failure coverage.
- Isolated seeded fixtures explicitly undo their injected offsets after real
  storage cleanup. This is test fixture cleanup, never production correction.
  Ordinary balanced cases must return to zero without seeding/resetting.

Build and run the normal core test mode (-t1) in Debug/Release on x64 and Win32
(solution platform x86); the shared memory header and 64-bit atomic arithmetic
justify all four combinations. Debug x64 is the first implementation feedback
pass. Run repository line-ending checks and diff checks. Include a bounded normal
engine run if the existing launch configuration allows its automated exit; inspect
accounting/destruction/unload findings and report any nonzero quiescent balance.
Do not weaken unload checks or silently clear a balance to pass validation.

## Boundaries and review record

Module binding/install/unload behavior remains unchanged. Reload, accounting-period
resets, force-unload, waits/timeouts, telemetry and recovery machinery are excluded.
Public-section organisation, aggregate hooks, CLiveDocument reattribution,
CBakedDocument/Block refactoring, baker capacity and file-load alignment remain in
the dependent stage task. Host retention/admission, TGA sequencing, generic wrappers,
images, discovery and new load/save services are separate work.

- 16 September: bounded code inspection and initial plan presented to Ritchie.
- 16 September: Ritchie accepted the outline and authorised continuation. Clarified
  that zero/wide diagnostic totals are not structural rejection and traced the
  lazy-log-open allocation dependency. This developed plan includes its resolution.
- 16 September: Ritchie proposed removing lazy opening in favor of explicit startup
  opening. Verified that the Host already does this; revised the reporting plan
  above and withdrew the bounded log/path conversion proposal. Coordinator review
  must use this revised plan.
- Coordinator revised design review, 16 September: approved with no blocking
  findings. This approval supersedes the crossed earlier approval mentioning
  bounded log opening. Inspection confirmed that Host startup already opens both
  logs before installing/starting the service. Approved reporting scope: explicit
  provisioning through open_logs; remove lazy opening from the three direct-report
  paths; require preopened logs in start; have the writer check its existing event
  log; update service documentation and affected tests. No bounded Log API or
  platform log/path conversion edits are approved or needed. Accounting primitive,
  modulo/zero-total and caller semantics remain approved, with true structural
  transport checks retained and module-unload behaviour unchanged. The specified
  four-configuration validation and implementation review are still required.
- Production implementation and validation: complete; see results below.
- Coordinator implementation review, 16 September: approved. Full production/test
  diff and the new test-access header reviewed; four successful build/test command
  results and smoke logs checked. The two maintained-document findings are now
  resolved: debug startup requires preopened logs, and memory documentation separates
  diagnostic counter discrepancies from genuine operation/ownership failures.
  No outstanding findings. Documentation-only closure required no test reruns.
- Ritchie's own code review is complete with no issues, and Ritchie explicitly
  authorised committing the reviewed work. No push is authorised; subsequent code
  changes require review as appropriate.

## Implementation and validation results, 16 September

Production changes match the revised plan:

- core/memory/memory_context.hpp: unconditional modular atomic adjustments and
  transition reporting, preserved real allocator failure handling, compatible
  reattribution with independently zero/wrapped totals. Context addresses are
  encoded as uint64_t numeric values because the existing structured-event encoder
  does not accept pointers. System IDs use its existing typed encoding.
- core/debug/service.hpp and core/debug/service.cpp: explicit startup provisioning
  contract, both-open startup prerequisite, and removal of all three direct-report
  lazy opens and the writer's open operation. Platform log/path code is untouched.
- core/system/erased_owner_transport.cpp and
  core/threading/messages/CErasedOwnerMsgTransport.cpp: explanatory comments at the
  retained structural assertions; no transport failure policy change.

Regression changes are in CMemoryToken_test_suite.cpp, ErasedOwner_test_suite.cpp,
DebugService_test_suite.cpp and ErasedPod_test_suite.cpp, with the new
tests/support/memory_context_test_access.hpp. No new suite registration or Visual
Studio item/filter edit is needed. The concurrent test uses the existing platform
CThread wrapper and checks the four emitted records within its own marked log
section, including exact before/after values. A full debug-event queue exercises
direct reporting with a rejecting/counting ambient allocator; no engine callbacks
occur. Configured but unopened logs reject start/direct writes without opening
files. Existing reporting-after-stop expectations now verify failure and absence
of a new record, rather than lazy reopening.

All four final builds and core tests passed using
`tools/invoke_sandbox_build.ps1 -Configuration <configuration> -Platform <platform>
-RunTests -LogTag <tag>` (default TestMode 1):

| Configuration | Solution platform | Tag / process | CMemoryToken | ErasedOwner | DebugService | Exit |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | x64 | accounting-final-dbg64 / 74524 | 648 passed | 527 passed | 458 passed | 0 |
| Release | x64 | accounting-final-rel64 / 65648 | 648 passed | 527 passed | 456 passed | 0 |
| Debug | x86 (Win32) | accounting-final-dbg32 / 68496 | 645 passed | 527 passed | 458 passed | 0 |
| Release | x86 (Win32) | accounting-final-rel32 / 22812 | 645 passed | 527 passed | 456 passed | 0 |

Every suite in these runs reported zero failures, including existing aggregate,
allocator/identity rejection, container, document and transport coverage. The
three additional x64 memory assertions exercise a size_t count wider than uint32_t.
The Debug/Release DebugService difference is its pre-existing development-only
assertion coverage. Policy checks report zero errors/warnings and the existing
negative-test suppression. Repository line-ending and git diff checks passed;
both new files use LF with a final newline.

Logs are beneath build/sandbox-test-output/{x64,x86}/{Debug,Release}/logs with the
tag and process IDs above. The initial feedback run found an existing after-stop
lazy-open expectation; it was updated before all four final runs. Initial test
compilation also identified forbidden standard-library thread/file includes and
a template-comma assertion macro invocation; those were corrected using existing
platform wrappers and parenthesisation, without changing repository policy.

The Debug x64 engine smoke run exited 0 and completed its normal TGA load/save and
worker/Executive shutdown. It ran from build/accounting-smoke with a copied input
fixture so the tracked test_data/output/files/test_output.tga was not overwritten.
Its event log is logs/morphic_debug.accounting-smoke.p82740.log under that directory;
the corresponding direct log is empty. No accounting/destruction/unload incidents
were observed there. The four suite runs passed their explicit zero-attribution
environment checks and balanced fixtures; their logs contain no context-destruction
or safe-unload imbalance reports. Expected injected transition diagnostics remain
in the dedicated accounting logs. No production counter reset or lifecycle change
was made.

Other uncommitted consolidation documents belong to the pre-existing/shared work
and are not part of this implementation diff. The accounting specification is the
focused handoff record and is included in the authorised, reviewed commit scope.
Pushes remain Ritchie's responsibility.

Coordinator implementation review found no blocking production-code or regression
issues. It requested correction of two maintained contracts before closure:
docs/debug/debug_service_substrate.md and docs/memory/memory_subsystem.md. Both
now describe explicit startup log provisioning/no lazy reopening and diagnostic
counter semantics respectively. Related stale transfer-rollback and aggregate
transaction wording in the same memory document was updated consistently;
genuine ownership/allocator constraints and module unload behavior are preserved.
These two documentation files complete the implementation's file scope.
Documentation-only closure review is approved; no production/test changes or
additional build runs were requested for these corrections.
