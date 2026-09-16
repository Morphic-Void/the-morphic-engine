# Container and live-document reattribution

Updated 16 September 2026. Ritchie has requested and authorised replacing the
previously reviewed interface with the smaller uniform contract below. Revised
design review passed on 16 September. The revised implementation and fresh
four-configuration matrix are complete and passed coordinator implementation
review on 16 September. Ritchie completed his manual styling and review, found
no issues, and authorised committing after the final check and coordinator
confirmation. Both checks passed and final coordinator confirmation was granted
for the exact 33-file staged scope, including this specification. The final
manual edits preserve behaviour; the four-configuration matrix remains applicable.
The earlier implementation and its passing matrix remain historical evidence,
not approval of this delta.

Task: `01a0aa63-8583-7b91-862e-b56199f9e039`, local.
Coordinator: `01a09fd4-73ad-7ce2-a3e4-efcdadb454dd`, local.
Dependent baked-document/loading task: `01a0a4d6-e506-7912-8cf8-0ace7c96453a`.
Use the shared main checkout. Preserve existing dirty documentation and user edits.
Commit is authorised for the confirmed scope; Ritchie pushes manually.
Broader consolidation documents remain outside this commit.
Earlier approval-state notes below describe their
historical review boundaries and are superseded by this status.

The [design order](consolidation_design_order.md),
[consolidation plan](consolidation_pass.md), and current inventory and boundary in
the [stage 1 record](consolidation_stage_1_specification.md) supply the broader
context. The diagnostic-accounting prerequisite is complete in `d1d804c`; preserve
its [contract and validation](diagnostic_memory_accounting_specification.md).
Historical alternatives in the stage record are not additional requirements here.

## Revised implementation and validation

The uniform contract is implemented across all 20 participating classes, with
identical public declaration sections and no legacy wrappers. Shared operations
observe once, preflight freshly and retain empty/same-context replacement. The
primitive adapter leaves CMemoryToken unchanged; typed CErasedOwner callbacks
consume records without changing registration or module checks. Baked-block
changes only forward its existing byte buffer and remove hook-only friendship.
Document structural emptiness checks now use source state rather than totals.

All four full solution builds and core test runs passed on 16 September 2026:

| Configuration | Log tag | Process | Result |
| --- | --- | --- | --- |
| Debug x64 | uniform-attribution-final-dbg64 | 73088 | All tests passed |
| Release x64 | uniform-attribution-final-rel64 | 86536 | All tests passed |
| Debug Win32 | uniform-attribution-final-dbg32 | 86116 | All tests passed |
| Release Win32 | uniform-attribution-final-rel32 | 80976 | All tests passed |

Commands used `tools/invoke_sandbox_build.ps1 -Configuration <Debug/Release>
-Platform <x64/x86> -RunTests -LogTag <tag>`. Counts include CMemoryToken
1038 on x64 / 1035 on Win32, LiveDocument 4610 Debug / 4620 Release,
StringBuffers 184, TOrderedCollection 1775, TUnorderedCollection 990,
ErasedOwner 527, BakedDocumentTransfer 213 and BakedDocument 1216, all zero
failures. The record matrix, nested wrapping sums, complete mixed totals,
one-observation/fresh-preflight checks, empty/same-context replacement and
composed FIFO/instance owner are covered. Contract diagnostics also run with
a full event queue and rejecting allocator; no allocation or shutdown occurs.
Line-ending and diff checks pass; final logs contain no context-destruction
reports of live allocations.

An initial compile identified a test variable named empty_block whose type is
CErasedOwner; its member call was restored before the passing matrix. Only
ownership-boundary comments and documentation changed during/after that matrix.
Fresh coordinator implementation review passed on 16 September with no findings.
Reviewed the shared helpers, all participating ownership compositions, primitive
adapter, erased-owner adaptations, baked-block forwarding, receiver-specific
migration, tests and maintained documentation. Confirmed the four recorded command
outputs report all tests passed with exit code zero; independently checked final
logs for live-allocation destruction reports and reran line-ending/diff checks.
No additional test run was needed. This approval covers the uniform revision;
Ritchie's own review and explicit commit instruction remain required. No commit
or push is authorised, and the dependent task awaits the agreed handoff.

## Current revision: uniform observation and replacement

Ritchie identified inconsistent method sets, visibility and comments in the first
implementation. He accepted the following proposal and explicitly requested its
implementation. This section supersedes the interface shape in the earlier record;
ownership boundaries, structural rejection, diagnostic-only accounting and review
rules remain unchanged. No commit or push is authorised.

Place the following value and shared helpers in memory_context.hpp, alongside the
checked-add helpers, without new files or project registration:

```cpp
enum class EMemorySourceState { empty, coherent, mixed };

struct SMemoryAttribution
{
    EMemorySourceState source_state{ EMemorySourceState::empty };
    CMemoryContext* source{ nullptr };
    std::uint32_t token_count{ 0u };
    std::uint32_t allocation_count{ 0u };
    std::uint64_t allocation_size{ 0u };
};

[[nodiscard]] SMemoryAttribution combine_memory_attribution(
    const SMemoryAttribution& left, const SMemoryAttribution& right) noexcept;

template<typename TOwner>
[[nodiscard]] bool can_reattribute_to(
    const TOwner& owner, CMemoryContext* target = nullptr) noexcept;

template<typename TOwner>
[[nodiscard]] bool reattribute(
    TOwner& owner, CMemoryContext* target = nullptr) noexcept;
```

All participating container classes, including the four payload backing classes,
the two metadata bases, TPodFifo and TInstance, plus CLiveDocument and the current
CBakedDocumentBlock expose exactly this infrastructure section with identical
method order and comments:

```cpp
//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;
```

Remove their separate token/count/size/source observers and checked transfer members.
Do not keep legacy wrappers. All infrastructure methods in this set are public;
unrelated private/protected content remains so. Document ownership boundaries next
to the class description; common section comments remain identical. Metadata bases
still describe metadata only, while each outer describes complete backing storage.
No ownership of arbitrary user-value allocations is introduced.

Observation is a value describing the current exclusively accessed owner, not a
reservation or persistent transfer plan. Empty means no allocated backing,
coherent means all allocated backing uses its nonnull source, mixed means sources
conflict. Empty and mixed results have null source. Accounting totals are available
in every state and never determine that state. The combiner treats empty as the
source identity and mixed as absorbing, retains modulo totals and reports their
overflow through the two already agreed helpers. Token counts sum the bounded
backing-token inventory. Every aggregate observes each child once and combines
those values. It must continue collecting totals after source incoherence is known.

Shared can_reattribute_to resolves ambient target, takes one observation and checks
source state/allocator compatibility. Shared reattribute independently does the
same single observation, performs one primitive adjustment for a distinct coherent
source, then always invokes complete replacement, including empty/same-context
cases. Mixed sources, absent destination and incompatible allocators reject before
replacement. No rollback or accounting failure gate is added. Shared inspection
necessarily obtains totals even for a same-context query; this changes which
observer query may emit a sum diagnostic, not counter movement or acceptance.

CMemoryToken is the underlying primitive and retains its existing public operations
and visibility. Add one narrow free adapter in memory_token.hpp:
`SMemoryAttribution observe_memory_attribution(const CMemoryToken&) noexcept`.
It uses the existing owns_storage/context/count/size observers to construct the
record; leaf containers forward to it. This avoids duplicating record construction
and does not redesign token or transport APIs. Existing valid-owner invariants and
exclusive access remain prerequisites; the record is not a general corruption audit.

Minimal caller adaptation:

- CErasedOwner keeps its public checked operations, private hooks, registration
  callbacks and component-identity checks. Its typed nested adapters read the new
  record fields and call the shared checked query. Source validation explicitly
  rejects mixed records. Keep the operation-table shape and registrations intact.
  Its existing shell-plus-payload sums use the checked-add helpers as appropriate.
  The one-observation guarantee applies to each shared checked operation, not to
  an entire carrier operation through its unchanged separate callbacks. Those
  callbacks may independently observe the payload and repeat sum diagnostics;
  no cached record or operation-table redesign is required here.
- CBakedDocumentBlock forwards the new two-method interface through its current
  byte buffer. Remove its now-unneeded CErasedOwner hook friendship; retain baker
  access. No representation, adoption, extent, baking or loading change belongs
  in this delta. The dependent task will change its forwarding during refactoring.
- Migrate document internals, tests and maintained documentation to records and
  shared calls. Primitive-token and carrier callers continue using their existing
  APIs. Identify receiver types rather than blindly replacing every matching name.

Validation will retain and migrate the complete existing regression coverage. Add
direct source-state combination cases (empty/coherent/mixed, matching/different
sources, mixed totals retained), nested checked-overflow records, source states
independent of zero/modulo totals, and a synthetic owner proving one observation
and one replacement per successful shared operation, fresh preflight after a query,
and no replacement on rejection. Exercise newly composable FIFO/instance wrappers
and complete backing interfaces. Repeat Debug/Release x64/Win32 full solution/core
tests, line-ending and diff checks, then return the revised diff for coordinator
implementation review. Old validation and approval do not cover the new interface.

Coordinator delta design review passed on 16 September with no blocking findings.
Reviewed the primitive adapter, container ownership boundaries, baked-block
forwarding and typed erased-owner callbacks against their current implementations.
The source-state/totals separation and fresh observation on each checked transfer
preserve structural rejection independently of diagnostic arithmetic. The dependent
baked-document task remains design/read-only until this revision's implementation
is reviewed and the shared-checkout handoff is agreed with Ritchie.

## Earlier reviewed interface and implementation

The following sections record the preceding implementation. Their public API
shape is superseded by the current revision above.

### Ownership and public interface

Add the exact separate section immediately before infrastructure declarations:

```cpp
//  Interface for memory accounting and ownership-transfer infrastructure.
public:
```

Keep existing observer signatures, default arguments and ordinary moves. Single
storage wrappers retain `CMemoryContext* memory_source_context() const noexcept`,
returning null when they own no allocation. Their existing replacement hook becomes
public. No memory primitive, carrier or transport visibility redesign is included.

The complete aggregate interface on TPodOrderedSlots, TPodUnorderedSlots,
TOrderedCollection, TUnorderedCollection, CStableStrings and CLiveDocument is:

```cpp
[[nodiscard]] std::uint32_t memory_token_count() const noexcept;
[[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
[[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
[[nodiscard]] bool memory_source_context(memory::CMemoryContext*& source) const noexcept;
[[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* context = nullptr) const noexcept;
[[nodiscard]] bool reattribute(memory::CMemoryContext* context = nullptr) noexcept;
void unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept;
```

Each operation covers the same complete owned backing storage. Slot/collection
outers explicitly combine payload backing and metadata instead of inheriting a
metadata-only query or replacement. General collections do not recursively own
arbitrary allocations within their user values. CLiveDocument combines m_nodes,
m_property_names and m_string_values; caller analysis scratch and transient
parser/baker scratch are excluded. There is no live-document erased registration.

Source discovery merges allocated source contexts into the incoming pointer:

- Null input means no source discovered yet; the first allocated child supplies it.
- Empty children leave it alone, irrespective of their configured context.
- Every allocated child must match the discovered/incoming source exactly, even
  where different contexts share an allocator.
- On mismatch, return false and preserve the caller's incoming pointer. Use local
  candidate pointers during multi-child discovery, publishing only on success.
- Single-storage pointer queries are composed deliberately by aggregate helpers;
  no overload assumption or pointer-to-bool substitution is used.

Reattribution requires exclusive ownership/mutation access throughout discovery,
preflight, accounting and replacement. Unrelated owners may update counters
concurrently. Resolve a null target through the ambient context; a still-null
target rejects. Discover coherent allocated sources and verify compatibility
before any member changes. A same-source destination needs no adjustment; an
empty aggregate has no allocator constraint from unallocated tokens. For a
distinct allocated source, obtain each complete accounting total once and call
memory::reattribute once. Then invoke the complete unsafe replacement hook,
recursing without child accounting. Include every unallocated token. No allocation,
content movement, key change, or fallible sequential child reattribution occurs.

The unsafe hook retains its existing precondition: the caller has established
coherence and compatibility, performed the accounting adjustment where needed,
and maintained exclusive access. It is not an alternative checked transfer API.

## Checked cumulative accounting

Add two small inline helpers to memory_context.hpp alongside the existing
reattribution primitive, without changing its interface or private access:

```cpp
[[nodiscard]] std::uint32_t add_accounting_counts(
    std::uint32_t left, std::uint32_t right) noexcept;
[[nodiscard]] std::uint64_t add_accounting_bytes(
    std::uint64_t left, std::uint64_t right) noexcept;
```

Each checks `right > max - left`, returns the unsigned modulo sum, and emits an
MV_ERROR on overflow with the counter kind, operands and result. Reporting uses
existing scalar structured events, stays active in release, and neither rejects
nor requests shutdown. No allocating message construction or lazy log provisioning.
These helpers do not update memory contexts or promise exact unbounded totals.

Use checked addition at every cumulative allocation-count/byte observation in
this container/live-document hierarchy, including backing helpers. A child reports
its own lost representability before returning a narrowed total; its parent checks
the addition of that modulo result. This preserves composition without a new
visitor/accumulator hook or a second accounting interface. Explicit observer calls
can therefore report overflow; repeated observations can report it again. A
same-context/empty transfer does not need to observe totals. Compatibility/source
preflight does not depend on these totals. Token-count observers describe the
fixed number of backing tokens and retain their existing bounded arithmetic.

The atomic-context high-bit-transition heuristic is unchanged. Cumulative overflow
is a separate diagnostic of the sum, with no recovery, reservation, reset, or
transaction machinery. Synthetic arithmetic inputs exercise limits without huge
allocations or invalid physical storage. Ritchie selected these two checked-add
helpers, including reporting from aggregate observations.

## Class and friendship inventory

| File / classes | Planned change |
| --- | --- |
| ByteBuffers.hpp / CByteBuffer, CByteRectBuffer | Group observers and transfer methods; expose existing two hooks. |
| StringBuffers.hpp / CSimpleString, CStringBuffer | Same grouping and hook exposure; retain pointer source queries. |
| StringBuffers.hpp / CStableStrings | Group complete interface; fix source merging; checked cumulative observers. |
| TPodVector.hpp | Group/expose hooks; keep raw_data, constants and token private. |
| TPodFifo.hpp, TInstance.hpp | Group existing observers and transfer methods only; no unused new hooks. |
| TPodOrderedSlots.hpp, TPodUnorderedSlots.hpp | Complete outer payload+metadata hooks and standalone use; checked sums. |
| TOrderedCollection.hpp, TUnorderedCollection.hpp | Complete outer storage+pointer/key+metadata hooks; checked sums. |
| slots/TOrderedSlots.hpp, slots/TUnorderedSlots.hpp | Public infrastructure section for metadata-only hooks/observers; preserve protected domain interface. |
| live_document.hpp/.cpp | Complete public aggregate interface over its three owned components; checked sums. |

Backing storage classes keep unrelated data protected. Their cooperating source
and replacement hooks get the agreed public section; existing backing accounting
can stay protected. The metadata bases remain explicitly metadata-only tools.

Inspection found these eleven container friendship declarations serve only the hooks
being exposed and can be removed:

- CByteBuffer: CStringBuffer, CErasedOwner, CBakedDocumentBlock.
- CByteRectBuffer: CErasedOwner.
- CSimpleString: CErasedOwner.
- CStringBuffer: CStableStrings.
- TPodVector: the four slot/collection backing class templates and CStableStrings.

Validate each removal against actual callers and the full build. Keep live-document
baker/promoter/test friendships and all primitive/carrier friendships. The existing
baked-block byte-buffer forwarding continues to compile through public hooks; no
intermediate baked-block representation change is needed.

## Implementation and validation sequence

1. Settle helper/reporting details with Ritchie and submit this complete plan to
   the coordinator. Production implementation begins after design review passes.
2. Implement grouping, verified friendship removal, complete source/replacement
   composition and checked observers. Add CLiveDocument forwarding using only
   child interfaces. Update maintained API documentation where it contradicts
   the resulting contract; preserve unrelated documentation edits.
3. Extend existing suites without new project registrations where practical.
   Exercise all four slot/collection aggregates, stable strings and live-document
   nesting. Derive test fixtures from slot outers to exercise protected payload
   backing and public metadata separately; use existing live-document test access
   for child isolation. Public API tests prove external access without friendship.
4. Verify full payload/metadata deltas against context counters; pointer/key
   stability; same-context, empty, partially allocated and moved states; ordinary
   moves preserving attribution; growth under destination ambient context; and
   zero context balances after cleanup. Include retained excess backing after
   allocation failure. Test mixed sources, including compatible but distinct
   contexts, and incompatible allocators without any member context mutation.
   Explicitly verify empty-member replacement, including on same-context transfer.
5. Synthetic checked-add boundaries cover exact maxima, overflow to zero/nonzero,
   independent count/byte totals and nested loss before the parent addition.
   Check incident counts/contents and absence of shutdown with the existing debug
   service fixture and provisioned logs. Feed resulting modulo totals through
   compatible primitive transfer, then exercise aggregate transfer despite seeded
   context discrepancies. Retain existing erased/document regressions.
6. Build and run core tests (-t1) in Debug/Release x64 and x86 (Win32) with
   tools/invoke_sandbox_build.ps1. Start with Debug x64 for feedback; final matrix
   uses the finished source. Run line-ending and git diff checks. No huge objects,
   broad fuzz/benchmark expansion, or unrelated lifecycle work.
7. Return exact signatures, semantics, reviewed inventory and command/log evidence
   for coordinator implementation review. Wait for Ritchie's own review and
   explicit commit instruction before committing. No pushes.

## Boundaries and progress

The dependent task retains baked-document/view/block representation and adoption,
the 2 GiB bound, mandatory baker reallocation/zeroed rounded tail, and primitive
file-load alignment. It applies this contract to the baked block later. No generic
wrapper migration, image/Host/TGA service changes, erased registration, module
unload/reload policy, telemetry, or recovery machinery belongs here.

- 16 September: verified baseline dirty files; read governing specifications and
  inspected actual container, primitive, erased-owner and live-document code.
- Confirmed incomplete outer hooks and CStableStrings overwriting incoming source.
- Proposed merge semantics and the checked-add API directly to Ritchie. This
  document is the focused design/progress record; production code remains untouched.
- Ritchie selected the two checked-add helpers; complete plan submitted for
  coordinator review before implementation.
- Coordinator design review passed on 16 September with no blocking findings.
  Checked the plan against the existing payload/metadata composition, source hooks,
  live-document ownership and baked-block forwarding. Approval covers this plan;
  completed production changes and validation still require implementation review.

## Implementation record

The implemented public signatures and ownership boundaries match the plan above.
All eleven listed friendship declarations were removed; actual callers continue
through the public hooks. Raw storage and unrelated private details remain private
or protected. No memory-token, erased-carrier, baked-block, transport, registration,
Visual Studio item/filter, debug-service or module-lifecycle changes were required.

The four slot/collection outers now explicitly combine backing and metadata for
source discovery and replacement. Source outputs are committed only after all
children agree. CStableStrings merges rather than overwriting the incoming source.
CLiveDocument uses these complete interfaces for its three owned components and
performs one accounting transfer before recursive replacement. Checked addition is
used at every cumulative count/byte observer in the changed hierarchy. Same-context
and empty transfers still recurse through replacement but skip accounting totals.

One necessary validity correction was found during implementation: the two general
collection storage_is_valid functions required an unconfigured token's context to
be null, although their existing reattribution hook can bind that empty token to a
destination. Remove only that null-context requirement; the no-storage, zero-size,
zero-stride/alignment/capacity constraints remain. Empty and moved-from collection
tests confirm validity and the unallocated stable-token destination context.

Coverage added to existing suites (no new project registrations):

- TOrderedCollection: a bounded fixture instantiates all four slot/collection
  variants and validates complete public aggregate composition, payload/metadata
  sources, distinct compatible-source rejection, allocator rejection, counter
  deltas, address/key stability, same-context transfer, moves, empty states and
  subsequent growth under the destination context. An owned TInstance value proves
  backing transfer does not recursively reattribute user values.
- TOrderedCollection and TUnorderedCollection failure sweeps: transfer retained
  backing after failed growth, then resume growth/packing under the destination;
  check both contexts return to zero.
- StringBuffers: merge semantics, empty/same-context transfer, and mixed-source
  rejection created through ordinary byte-buffer growth under a different context.
- LiveDocument: nested node/string composition; each child independently moved to
  create incoherence; pointer, key and string-ID stability; scratch exclusion;
  empty string domains rebound during same-context transfer; moves and later growth.
- CMemoryToken diagnostic fixture: exact-max/overflow/zero/independent count and
  byte sums, nested child loss, modulo transfer success, exact incidents and log
  contents, no shutdown, and full-document transfer with seeded accounting offsets.
  Repeat the synthetic sum cases with full logging queue and rejecting ambient
  allocator, proving no engine allocator callbacks while reporting.

First Debug x64 feedback built successfully and found two fixture assertions:
analysis scratch had been constructed under the source context before entering its
intended scope. Moving its construction inside that scope fixed both. The next run
passed LiveDocument and found the new mixed-string fixture had not exceeded the
4 KiB minimum growth capacity. The fixture now explicitly reserves beyond that
capacity; this was a test setup correction, not a container behavior change.
The final matrix below will record only successful runs of the finished sources.

## Final validation evidence

All four commands completed with exit code 0 on 16 September:

```powershell
.\tools\invoke_sandbox_build.ps1 -Configuration Debug -Platform x64 -RunTests -LogTag aggregate-final2-dbg64
.\tools\invoke_sandbox_build.ps1 -Configuration Release -Platform x64 -RunTests -LogTag aggregate-final-rel64
.\tools\invoke_sandbox_build.ps1 -Configuration Debug -Platform x86 -RunTests -LogTag aggregate-final-dbg32
.\tools\invoke_sandbox_build.ps1 -Configuration Release -Platform x86 -RunTests -LogTag aggregate-final-rel32
```

These are complete solution builds and core test mode 1 runs. Every suite reported
zero failures, including baked document, parser/writer, erased-owner and transport
regressions. The ordered-collection suite includes the new four-variant aggregate
fixture. Build policy validation reported zero errors/warnings and its existing
negative identity-test suppression.

| Configuration | Platform | Process | CMemoryToken | LiveDocument | StringBuffers | TOrderedCollection | TUnorderedCollection |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Debug | x64 | 72400 | 797 | 4605 | 180 | 1746 | 990 |
| Release | x64 | 66352 | 797 | 4615 | 180 | 1746 | 990 |
| Debug | x86 / Win32 | 81400 | 794 | 4605 | 180 | 1746 | 990 |
| Release | x86 / Win32 | 86492 | 794 | 4615 | 180 | 1746 | 990 |

Cells contain passing assertion counts. ErasedOwner retained 527 passes in every
run; DebugService had 458 in Debug and 456 in Release. The three x64-only memory
assertions are the existing size_t narrowing coverage. Suite assertion counts can
differ by configuration; there were no failed assertions in any final run.

Diagnostic logs are under build/sandbox-test-output/{x64,x86}/{Debug,Release}/logs,
with the command tags and process IDs above. In particular, accounting_events and
accounting_direct contain the checked-sum operands/results, for example count
4294967295 + 3 -> 2 and bytes 18446744073709551615 + 17 -> 16. Tests verify both
queued and direct-fallback records, exact incidents and absence of shutdown.
Final-tag log inspection found no context-destruction live-allocation reports.
Balanced fixtures and final test-environment checks pass without counter seeding;
only deliberately discrepant arithmetic fixtures reset their injected offsets.

Repository line-ending validation and `git diff --check` pass. No .vcxitems or
.filters files changed. The focused specification is a new LF text file with a
final newline. Existing dirty consolidation documents were preserved; coordinator
updates to those shared records are separate from this implementation diff.

The implementation, exact API/semantics above, friendship inventory and this
evidence have been returned to the coordinator for implementation review. Ritchie's
own review and explicit commit instruction remain required after that review.

Coordinator implementation review passed on 16 September with no findings.
Reviewed the production and test diffs and maintained memory documentation;
confirmed the four successful build/test command results and sampled the final
overflow logs. Independently reran line-ending validation and git diff --check.
The empty collection validity correction is consistent with binding unallocated
tokens and preserves the other geometry checks. No additional test run was needed.
This approval covers the reviewed implementation, not a commit or the dependent
baked-document changes. Ritchie's own review and explicit commit instruction are
still required; no push is authorised.
