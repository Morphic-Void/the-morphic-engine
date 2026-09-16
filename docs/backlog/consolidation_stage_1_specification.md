# Stage 1 discussion: reattribution and baked-document refactor

Updated 16 September 2026. Historical design and discussion record. This file
preserves settled decisions, code evidence and proposals from the stage's design
process; it is not authority to begin additional production implementation.

The resulting work is complete in commits `d1d804c` (diagnostic accounting),
`42a908d` (uniform container/live-document attribution) and `a75962f` (baked
storage and aligned loading), following validation, coordinator review and
Ritchie's review and commit instructions. The focused
[baked-document storage record](baked_document_storage_specification.md) takes
precedence over earlier alternatives and pending-work statements below. In
particular, the final block owns only CByteBuffer and constructs borrowed views
on demand; the version-4 header stores offsets and counts. Direct token ownership
and a retained document view were superseded. Broader Host/TGA/service work and
the Executive acceptance exercise remain separate follow-up work.

Stage task: `01a0a4d6-e506-7912-8cf8-0ace7c96453a`.
Coordinator: `01a09fd4-73ad-7ce2-a3e4-efcdadb454dd` (local).
Work uses the existing main checkout, explicitly authorised in this task.
The [design order](consolidation_design_order.md) and
[consolidation plan](consolidation_pass.md) govern scope. Historical discussion
does not override current decisions.

## Current coding scope: 16 September consolidation

This is the current task inventory, including Ritchie's explicit addition of
live-document reattribution and the organisation of the shared infrastructure
interface. It supersedes the narrower inventory and older wrapper proposals.
Scope is agreed in direction; the interfaces and decisions listed below must
still be resolved into a complete plan before production implementation.
The diagnostic-accounting prerequisite is complete in commit `d1d804c` (currently
unpushed), from task `01a0a9d6-bd37-7731-b91a-4282e76bf2bc`, Diagnostic memory
accounting. Its [specification](diagnostic_memory_accounting_specification.md)
records approved implementation and validation. The earlier accounting investigation
below is historical evidence, not an instruction to redo that work.

At Ritchie's request, the next split is being handed to the coordinator for a new
implementing task: container infrastructure-interface organisation, complete
aggregate reattribution hooks, checked cumulative accounting and live-document
reattribution. This task retains baked-document/view/block representation, storage
preparation and aligned loading, and will apply the established infrastructure
contract to the baked block. The inventory below records accumulated requirements
across that split; it does not assign all of them to this task. At Ritchie's
request, the coordinator has created Container and live-document reattribution,
`01a0aa63-8583-7b91-862e-b56199f9e039` (local), using Astra High directly in the
shared main checkout. The smaller uniform-interface revision is now complete and
committed as `42a908d`, Unify container memory attribution and reattribution.
The coordinator confirms fresh implementation approval, four full Debug/Release
x64/Win32 solution/core runs, Ritchie's manual review and explicit commit instruction.
HEAD and the clean production worktree were verified here; broader coordination
backlog documents remain dirty/untracked and must be preserved. No push was made.
The prerequisite is satisfied. Finalise the remaining plan directly with Ritchie
and obtain coordinator plan review before production changes here; this handoff
does not itself authorise implementation, a commit or a push.

### Current dependency contract: supersedes earlier interface proposals

The authoritative interface is the current revision in
[Container and live-document reattribution](container_reattribution_specification.md).
Participating owners expose only these two infrastructure methods, in the agreed
commented public section:

- memory_attribution() returns memory::SMemoryAttribution: an empty/coherent/mixed
  source state, source pointer, token count, allocation count and allocation size.
- unsafe_replace_memory_context_without_accounting(expected_source, target)
  replaces every owned context, including unallocated members.

Separate owner token/count/size/source observers and checked transfer members are
removed, without legacy wrappers. Checked operations become the shared functions
memory::can_reattribute_to(owner, target) and memory::reattribute(owner, target).
Each takes one fresh owner observation. Source state is independent of modulo
accounting totals; mixed sources, missing target and incompatible allocators reject
before replacement. Successful structural preflight permits one diagnostic
accounting adjustment followed by complete replacement; empty and same-context
cases also perform replacement without counter movement.

Aggregates combine child records with combine_memory_attribution. Empty is the
source identity and mixed is absorbing; all child totals are still collected and
checked/modulo overflow is reported without rejection. An observation is not a
cached transfer plan or reservation. The primitive CMemoryToken retains its API
and receives a narrow free observe_memory_attribution adapter; it is not redesigned.

The container task now includes minimal forwarding through CBakedDocumentBlock's
existing byte buffer and typed erased-owner callback adaptations. The carrier's
operation-table shape, registration and identity checks remain unchanged. This
task will adapt the block's observation/replacement forwarding to direct token
ownership during its later representation refactor. Adoption, extent, baking and
loading remain here. No production overlap is permitted.

This approved revision supersedes earlier references below to exposing
memory_source_context or retaining separate public observers/member transfer
methods, and earlier sequencing that deferred all baked-block interface changes
to this task. Those passages record design history, not a competing current API.

### Accumulated implementation inventory

1. **Organise the reattribution infrastructure interface.** Give cooperating
   accounting, source-context, compatibility and context-replacement operations
   a separate public section in the container set, CLiveDocument and
   CBakedDocumentBlock only. Put a single-line
   explanatory comment before the new public declaration. Preserve the existing
   capabilities and the accounting contract established by the prerequisite work.
   Review friendships made unnecessary by
   this change; do not expose unrelated internals or remove friends indiscriminately.
   Include review and regression validation of any related edits Ritchie makes,
   preserving his work.
2. **Add CLiveDocument reattribution.** Follow the existing aggregate-container
   model across its nodes, property names and string values. Retain its memory
   accounting. Preflight the whole document before transferring accounting and
   replacing member contexts, including empty/unallocated members; failure must
   not leave a partially reattributed document. Caller-owned scratch is excluded.
3. **Refactor CBakedDocument onto CMemoryConstView.** Replace the separate raw
   pointer/byte-count representation with an exact byte-stride memory view.
   Preserve checked construction, structural validation and explicit 32-byte
   address validation. Reduce the baked-document extent limit explicitly to
   memory::k_byte_size_ceiling (2 GiB), including standalone borrowed views;
   retain the existing serialized format.
4. **Refactor CBakedDocumentBlock to own a CMemoryToken.** Keep the checked
   CBakedDocument alongside its owner. Use CByteBuffer::disown for byte-buffer
   handoff, removing the block's need for CByteBuffer friendship. Update baker
   publication, owner moves/replacement, empty state, destruction, views and
   reattribution forwarding. Adoption accepts a byte buffer only; the document
   identifies itself and its exact extent through its existing header. Validate
   storage alignment, capacity rounding and minimum allocation before reading
   content. Complete fallible validation/preparation before
   consuming a source or replacing a valid destination; successful replacement
   destroys its old content. Keep the existing erased payload shell and baked
   asset integration; do not replace them with a generic tagged byte wrapper.
5. **Implement mandatory baking storage preparation.** The baking path must
   explicitly reallocate to 32-byte-rounded capacity and zero unused space before
   token handoff. This is not optional; failure is an ordinary baking allocation
   failure. The file-load route already supplies aligned/rounded final storage.
   An additional public resize API is not required merely to express this policy;
   use existing reallocation where sufficient. Reallocation must precede final
   view construction and preserve the baker's intended attribution.
6. **Add requested alignment to primitive file loading.** Retain the 16-byte
   floor, use the existing memory alignment conditioner within the cap, reject
   requests above the cap, and round capacity to the effective address alignment. Baked loading
   requests 32 and reads directly into its final allocation. Preserve the
   distinction between file content, requested padding and capacity. Review the
   existing load request/caller path and make only the forwarding changes needed
   for the agreed aligned-load interface; a new service is not implied.
7. **Update and extend regression coverage.** Cover the changed views, ownership
   and publication paths, aggregate accounting and rejected transfers, moves and
   empty states, rounded capacity versus serialized extent, and direct aligned
   loading. Retain baking/promotion/parser/writer and existing transport coverage.
   Select the final build/test matrix after the affected class inventory is known.

### Settled constraints

- Existing container transfer flexibility is retained. Making all reattribution
  operations private is not the selected design. The separate public section
  describes the cooperating infrastructure contract, not the class's main features.
  The agreed comment form is:

  ```cpp
  //  Interface for memory accounting and ownership-transfer infrastructure.
  public:
  ```

- Unchecked context replacement requires completed ownership/context preflight.
  Reattribution across different memory allocators remains forbidden. Accounting
  errors must be recorded but must not themselves cause rejection or shutdown:
  the discrepancy may originate in an earlier update, not the current operation.
  Visibility does not make partial aggregate transfer valid, change
  ordinary move semantics or automatically register a type with CErasedOwner.
- Each aggregate presents one reattribution interface covering all of its owned
  backing storage, combining its constituent operations internally. A parent
  should not need knowledge of a child's payload/metadata split. Ritchie's
  selected direction is coherent source contexts, checked cumulative accounting,
  one accounting transfer at the outer operation boundary, and then unsafe
  context replacement through the aggregate hierarchy. This is reattribution,
  not memory reallocation or movement of content.
- The baked-document size limit is 2 GiB, including borrowed documents. This
  deliberately replaces the old UINT32_MAX layout-validation limit with the
  codebase's existing memory limit, without a format/version change.
- Both loaded and directly baked blocks have the same storage convention:
  32-byte base alignment and capacity rounded to 32, with a zeroed spare tail.
  The checked view, header total and saved bytes retain the actual serialized
  length. For example, 150 document bytes occupy 160 allocated bytes. No format
  version change or serialized trailing padding is selected. Larger requested
  alignment entails matching capacity rounding; loaded alignment never falls
  below 16.
- Where capacity reduction is needed, reduce to the rounded capacity, not the
  unrounded logical size. Direct baking/loading should allocate correctly first,
  without an extra alignment/shrink copy on those normal paths.
- Moving into an occupied owner should replace and destroy its previous content,
  following Ritchie's preference. Ritchie has accepted the proposed adoption
  failure guarantee: preserve the source and existing destination on failure.
- Baked-block adoption takes only a CByteBuffer, with no caller-supplied document
  type or extent. Before inspecting content, require ready storage at a
  32-byte-aligned address, allocation capacity a multiple of 32, and allocation
  large enough for a minimum valid baked document. Then derive identity and
  exact utilised extent from the existing header and perform checked validation.
- CLiveDocument receives reattribution capability only, not erased-payload
  registration or a new transfer service, like most existing containers.
- The uniform infrastructure section applies to the container set, live document
  and baked block. Its two methods are memory_attribution and
  unsafe_replace_memory_context_without_accounting; checked transfer/query are
  shared free functions. Preserve friendships needed for unrelated private access.
  Primitive-token and carrier APIs/visibility remain intact, with only the adapter
  and typed callback changes described in the current dependency contract.
- The later JSON service still parses locally, applies policy, bakes, and returns
  a baked block to the durable Host owner. Adding live-document reattribution
  does not change that service result or select a new live-document service.
- Filename expectations remain a producer contract; do not add wrapper-level
  filename/path/content validation merely to duplicate filesystem failure.

### Outstanding design decisions

1. **Consume the reviewed uniform contract.** The record-based owner interface and
   shared checked operations have passed design review in the container task.
   The reviewed implementation is now available in 42a908d. Do not reopen the
   earlier hook choices here. Apply that contract to the token-owned baked block
   in this task's plan and subsequent approved implementation.
2. **Baked-block adoption details.** Byte-buffer-only input and storage-first
   checks are now settled. Finalise the method name/move signature and whether
   a separate extraction operation is needed. The header supplies the extent,
   not the caller or allocation capacity. Complete fallible validation before
   consuming the source or replacing a valid destination. The agreed safe
   read bound and concrete checks are recorded below.
3. **Preparation placement and alignment API details.** The two routes and
   mandatory preparation policy are settled. Specify the baker's explicit
   reallocation/zeroing placement, file-load parameter placement/default and
   request forwarding. Reuse the existing alignment conditioner and checked capacity rounding;
   do not change general memory alignment semantics. CByteBuffer::reallocate uses
   the ambient context, so keep preparation local to the baker's intended context.
   No general optional trim/adoption policy is needed for these two entry routes.

Defaults accepted by Ritchie (not yet implementation authority):

- Add an explicit fallible `bool adopt(CByteBuffer&& source) noexcept` operation.
  An rvalue-reference parameter expresses intended ownership handoff but does not
  consume the buffer before validation. No separate raw-token extraction API is
  proposed without a concrete use; whole-block moves and immutable access remain.
- Append `alignment = 16u` after the existing `pad = 0` argument to loadFile,
  preserving existing calls and padding semantics. Forward an alignment field
  defaulting to 16 through FileLoadRequest and its worker; do not add a new service
  or expose unrelated load options merely for this change.
- Use the baker's existing explicit reallocate call in allocate_output to request
  rounded capacity from the outset, clear the entire allocation, and retain the
  actual logical document size. This satisfies mandatory preparation without a
  second allocation/copy after emission. Final checked view construction/adoption
  follows storage preparation. Share validation where practical rather than
  needlessly validating the same baked output twice.

Raw storage extraction is omitted by agreement. Method spelling, parameter
placement and preparation placement are settled by the accepted defaults above.
The focused baked_document_storage_specification.md supplies the concrete plan;
coordinator design review remains required before implementation.

### Byte-buffer-only baked-block adoption

Ritchie selects CByteBuffer as the sole adoption input. No token-plus-extent
overload, external content tag or separately supplied length is required. The
existing SBakedDocumentHeader already supplies magic, version, header_size and
total_size; no new file-format metadata is needed.

The selected validation order is storage first, then content, then consumption:

1. Before accessing header/content bytes, check ready buffer storage, actual
   32-byte address alignment, capacity divisible by 32 and sufficient allocation
   for the minimum valid document. Existing format evidence gives a minimum
   serialized extent of 82 bytes: 32-byte header, one 32-byte root record, two
   8-byte sentinel string references and two one-byte empty string tables. Thus
   the minimum allocation under the rounded-capacity rule is 96 bytes. Derive
   these bounds from format types/constants rather than unexplained literals.
2. Read the header only after those checks. Check magic/version/header shape and
   obtain total_size. Bound that extent to the format minimum and the agreed
   2 GiB maximum before traversing the document. Agreed read-bound rule: the
   byte buffer's logical size must contain the header and the declared extent;
   spare capacity is not evidence that file/document bytes were supplied. Logical
   size may exceed total_size through explicit file-load padding. No additional
   caller argument is needed for this check.
3. Validate the document against exactly header.total_size bytes using the
   existing semantic/structural checks. Trailing padding and spare allocation
   capacity are not part of the document view. Their content is not used to infer
   the extent. No additional tail-content scan is selected.
4. Only after all fallible work succeeds, disown the buffer's token and publish
   the owner plus checked document view. Preserve source and previous destination
   on failure; on success empty the source and replace the destination's prior
   owned content. No adoption-time alignment copy or optional repair is implied.

Ritchie has accepted the logical-size read-bound proposal as well as storage-first
validation and byte-buffer-only input. This preserves the distinction between
supplied content and reserved memory. Normal baking/loading paths satisfy the rule
while allowing capacity rounding and explicit load padding.

### Whole-aggregate reattribution: direction and implementation considerations

Each aggregate combines source discovery, accounting observations, compatibility
checks and unchecked replacement for its owned components. Both standalone
reattribution and composition into a larger owner should use that same complete
contract, avoiding separate implementations that can disagree on included storage.
It covers owned backing storage, not arbitrary allocations owned by values placed
in general-purpose collections; existing ownership boundaries remain unchanged.

This subsection's original method-level discussion predates the uniform record
revision. Use the current dependency contract and its linked specification for
exact signatures, source-state composition and shared-operation semantics.

The intended sequence is:

1. Resolve the target and establish one source context for all allocated backing
   storage. Check target allocator compatibility. The caller must exclusively
   control the object during preflight and replacement; context accounting may
   still be updated concurrently by unrelated owners.
2. Accumulate allocation counts and accounted bytes with overflow checks before
   narrowing to the accounting primitive's types (32-bit count, 64-bit bytes).
   Do not silently wrap accounting totals. An inability to represent or update
   diagnostic totals must be reported, not confused with allocator incompatibility
   or made a reason to reject an otherwise valid ownership operation. Concrete
   primitive/reporting details remain to specify.
3. Perform one cumulative memory::reattribute call for a nonempty cross-context
   transfer, not a series of fallible child reattribute calls. Same-context and
   no-allocation cases need no counter transfer.
4. After ownership/context preflight succeeds, recurse through the whole-aggregate
   unsafe replacement hooks even if accounting reports a discrepancy. Do not
   repeat accounting in children. Include unallocated member tokens so
   their contexts are updated too; retain the current rule that a token with no
   storage does not contribute an allocated source context to the coherence test.

Current slot/collection aggregates combine payload and metadata only inside
can_reattribute_to/reattribute. Supply complete aggregate hooks rather than exposing
only inherited metadata operations. In particular, CLiveDocument must be able to
treat its TPodOrderedSlots node store as one owner. CStableStrings currently writes
its source out-parameter afresh, whereas slot backing helpers merge an existing
source. Choose consistent externally visible aggregate semantics so callers do not
depend on that implementation difference. Internal helpers may still differ.

Structural incompatibility and mixed allocated source contexts are distinct from
accounting discrepancies. Keep the aggregate checks and accumulation simple and
testable. Existing atomic accounting handles concurrent updates; do not add
reservation, transaction or recovery machinery to protect against speculative
accounting failure.

Ritchie subsequently clarified the purpose of accounting: discrepancies are
high-priority diagnostic signals that something needs investigation and correction,
not inherently immediate application-fatal events. Accounting helps identify
possible leaks and the context worth investigating. It remains present in the
released product, not only development builds. Users may send back logs to help
diagnose gaps exposed by a local setup or combination of patches. Automatic
telemetry is a less likely future possibility, not an approved feature or task.

At module unload, the intended policy is to assess accounting once quiescent and
report any remaining imbalance; an imbalance is evidence to investigate, not by
itself proof of a real leak.
This supersedes the earlier effectively-fatal characterisation without reducing
the priority of accounting defects. It does not change the separate Host
insertion/resource-failure discussion.

Keep ownership, allocator compatibility and context lifetime requirements intact;
their correctness is not merely diagnostic. Accurate accounting and useful tests
remain desirable, but rare counter failures should not dominate the interface
design or create a new consumer-visible error protocol. The latest discussion
explicitly opens consideration of wider simplification; the previous statement
that accounting behaviour was categorically outside discussion is superseded.
No production change has been made or complete wider implementation plan selected.

### Diagnostic accounting: required change and future reload context

Ritchie's clarified requirement is that an accounting error must be recorded but
must not itself cause rejection or immediate application shutdown. The current
update cannot be assumed to have caused a discrepancy; it may reveal a prior
accounting mistake. Reattribution across allocators remains forbidden, independently
of accounting. An accounting discrepancy may delay updates while
a module unloads. Reload should start a fresh zeroed accounting period, so an old
discrepancy does not persist unless the faulty path is exercised again. This is
the desired future lifecycle behaviour. Reload does not exist yet; its absence
is not a defect or implementation gap in an existing reload feature. The current
Host-owned context is evidence to inform that future design, not a reason to add
reload implementation to this task.

Inspection identifies these couplings:

- CErasedOwnerMsgTransport::post/read escalate reattribute failure through
  MV_CRITICAL_ASSERT. That event can request shutdown when critical shutdown is
  enabled. Structural rejection and accounting-update failure currently share a
  boolean result, so an accounting discrepancy can reach this path.
- CMemoryContext::allocate declines to allocate when accounting addition fails.
  CMemoryContext::deallocate returns before invoking the allocator if accounting
  subtraction fails. Thus bookkeeping damage can obstruct real cleanup, rather
  than merely describe a possible leak. The reattribute primitive similarly
  reports failure and attempts accounting rollback.
- CBoundModule::unbind refuses a nonempty accounting balance; it has no bounded
  wait/log/continue policy. Its destructor retains the native module on failure.
  Installation also requires empty attribution. Host shutdown reports a failed
  unload as a critical event.
- The Executive's CMemoryContext is a static object owned by the Host, not the
  Executive DLL. Unloading/reloading that DLL does not reconstruct or zero its
  counters, and no explicit reset operation currently exists.

Agreed semantic direction: separate ownership/allocator/context validity from
diagnostic accounting outcomes. A structurally valid transfer must update the
whole aggregate's contexts while any accounting discrepancy is reported without
turning it into a transport failure or shutdown request. Valid deallocation should
not be skipped solely because its diagnostic counters disagree. This requires
reviewing the memory primitives and their callers together; merely ignoring the
existing boolean result in a container would be incomplete. Earlier descriptions
of context replacement only after accounting success describe the existing
baseline, not the selected contract. The concrete implementation must preserve
rejection of cross-allocator reattribution while removing accounting-only rejection
and shutdown escalation.

#### High-bit-transition accounting diagnostics

Ritchie selects reporting when the high bit changes across an accounting
adjustment. This signals apparent wrap/underflow or entry into a concerningly
large range, without making the diagnostic a rejection or shutdown condition.
Check allocation-count and allocated-byte counters independently, using their
actual unsigned widths (currently 32 and 64 bits respectively).

Concrete implementation direction: use an atomic fetch_add/fetch_sub return value
as the before value and derive the after value using the same adjustment with
unsigned arithmetic at the counter's width. Report when
`((before ^ after) & high_bit_mask) != 0`. Do not compare separate atomic loads
before and after the operation: those could include other threads' adjustments.
The two counters do not require a combined atomic snapshot or transaction.

Report transitions in either direction, not every adjustment while the high bit
is set. Thus ordinary further activity in the same range produces no repeated
incident. Concurrent adjustments, or repeated crossings in either direction, may
produce a short cluster of reports; that is acceptable and needs no additional
rate limiter. This is a diagnostic threshold-transition test, not a claim to
detect every possible arithmetic wrap for arbitrary adjustment magnitudes.

Keep the adjustment despite a diagnostic. Do not clamp/reset counters, roll back
an ownership operation or inhibit a valid deallocation because of this signal.
An inverse adjustment needed to reflect a real allocator failure is a separate
matter from rollback merely because a counter looks wrong. Diagnostics should
identify the affected context and counter and make the adjustment/before/after
values available for investigation, using existing non-shutdown reporting.

Tests should exercise both counters, addition/subtraction, high-bit transitions
in both directions, ordinary same-range changes, modular wrap/underflow, and
concurrent adjustments using their own atomic before/after values. Verify that
an incident neither rejects otherwise valid operations nor requests shutdown,
and that continuing within the same high-bit state does not repeat the report.
Cross-allocator reattribution must still be rejected independently.

When reload is implemented, it needs a quiescent reporting/reset boundary for the affected
accounting context. Logging must retain the discrepancy before counters are reset.
Resetting counters does not free leaked storage or prove unloading is safe: actual
threads, executable-code dependencies, allocator dependencies and any legitimate
late operations against the previous context must be handled by ownership/lifetime
rules independently. A new accounting period must not silently mix with updates
still belonging to the previous one.

The accounting-only nonrejection/nonshutdown rule, high-bit-transition reporting
and cross-allocator prohibition are settled. Outstanding items for this plan are
the concrete primitive/caller changes and implementation allocation. Unload wait/completion
and reporting/reset policy belong to the future reload design, not a required
extension of this task. Do not invent a timeout or
force-unload policy, reset a live context, weaken dependency checks, or implement
telemetry from this discussion. Define the handoff boundary here; under the
proposed split, develop and review the detailed accounting implementation plan
in its dedicated task before implementation.

### Inspection findings supporting the clarified scope

- Public hook exposure is sufficient for the existing private primitive/string/
  baked-block hooks. The aggregate forwarding detail above is composition work
  using those same two hooks, not an additional public mechanism. Existing
  friendships must be checked against remaining uses before removal.
- CMemoryConstView at stride 1, CByteBuffer and CMemoryToken use the 0x80000000
  byte ceiling. The current baked validator instead checks UINT32_MAX; Ritchie
  has explicitly selected reduction to the common 2 GiB ceiling, resolving the
  standalone borrowed-view compatibility question.
- Production erased registrations with nested owning storage are LoadedFile and
  EncodedTga (CByteBuffer), DecodedTga (CByteRectBuffer), TgaLoadRequest and
  TgaSaveRequest (CSimpleString), and BakedDocumentAsset (CBakedDocumentBlock).
  Host adds a no-nested-storage continuation payload; Executive adds none. Test
  registration adds a test continuation. System type IDs also exist for
  CStringBuffer and CStableStrings, but type registration is not erased-owner
  eligibility. Vectors, FIFO/instance, slot/collection containers and CLiveDocument
  are not broadly registered for owning transfer. No new registration is planned.
- Ritchie subsequently selected the existing alignment conditioning instead of
  the initially requested upward rounding. memory::condition_alignment reduces
  non-power-of-two requests using their lowest set bit, with a pointer-alignment
  floor; apply the loader's 16-byte floor after that conditioning. For example,
  24 and 48 both produce effective alignment 16, while 32 remains 32. Zero also
  produces effective alignment 16. This is not rounding to the greatest power
  of two below the request. Check the raw request against the cap before
  conditioning, retaining the above-cap failure rule. memory::condition_bytes
  supplies overflow-checked capacity rounding to the effective alignment.
- Token alignment is a five-bit logarithm (maximum exponent 31); memory views
  also cap that exponent at 31. Thus the representational alignment cap is
  0x80000000, matching the allocation byte ceiling. Reject loader alignment
  requests above it rather than clamping. Representable requests can still fail
  allocation. The installed MSVC aligned-new implementation calls _aligned_malloc;
  the installed UCRT implementation checks power-of-two alignment and allocation
  size/overhead overflow, then heap limits. Inspection found no smaller independent
  fixed alignment cap there. In particular, extreme requests need not succeed
  on Win32 merely because the token can represent them.

### Boundary, sequencing and validation

The low-level accounting change is complete in `d1d804c`. Its approved specification
and test record are in diagnostic_memory_accounting_specification.md. Inspection
here confirms that it preserves container interfaces, permits modular diagnostic
totals without accounting-only rejection, retains cross-allocator rejection, and
deliberately leaves whole-aggregate hook/visibility work to the dependent task.
Its debug-service changes prevent accounting diagnostics from recursively opening
logs; do not undo that provisioned-logging contract. This task inspected relevant
changes for dependency planning, not as a repeat of the completed implementation
review or test run.

Ritchie has requested that the coordinator receive a self-contained handoff for
a separate container reattribution task. Recommended sequence: completed accounting
change, then container/aggregate/live-document reattribution, then this task's baked
document and loading work. Keep production changes sequential in the shared main
checkout. This task may continue design/documentation and read-only inspection.
The handoff has been sent to the coordinator, including the agreed public-section
comment, class/hook inventory, aggregate composition pitfalls, diagnostic-only
accounting and checked-accumulation requirements, live-document ownership boundary,
registration exclusions, validation expectations, retained work here and review
rules. The coordinator confirmed the handoff was sufficient and subsequently
created Container and live-document reattribution,
`01a0aa63-8583-7b91-862e-b56199f9e039` (local), at Ritchie's request, using Astra
High in the shared main checkout. The uniform revision subsequently passed fresh
implementation review and the four-configuration validation matrix, followed by
Ritchie's manual review and explicit commit instruction. The coordinator handed
off committed result 42a908d; it is now the prerequisite baseline for this task.
Use container_reattribution_specification.md and docs/memory/memory_subsystem.md
for the final contract. This task's own plan review and commit permissions are
still required; prerequisite completion is not authority to begin production work.

The coordinator subsequently narrowed its review condition following Ritchie's
clarification: unload/reload checks are tangential to the accounting change. Leave
existing module-unload behaviour unchanged; future unload/reload policy and a
detailed lifecycle plan are not prerequisites. With correct accounting and no
remaining allocations, quiescent totals should be zero. Validate/report any
observed imbalance and investigate the evidence without masking it or automatically
expanding scope. This supersedes the earlier condition requiring reconciliation
of module binding/install/unbind and Host unload escalation within the accounting
plan. The existing-code observations above remain context, not work assignments.
Older Host retention/TGA/new service work remains separate, with later sequencing
to be reconciled against Executive acceptance when developed plans are reviewed.

The new container task should own public infrastructure grouping, whole-aggregate
hooks, coherent source-discovery semantics, checked cumulative observations with
nonfatal modulo overflow reporting, redundant friendship cleanup, and CLiveDocument
reattribution. Include live-document support as a concrete nested aggregate user,
but add no erased-payload registration. Preserve ordinary move semantics, owned
storage boundaries, allocator compatibility and diagnostic-only accounting.

This task retains CBakedDocument's CMemoryConstView/2 GiB refactor, token-owned
CBakedDocumentBlock and adoption/lifetime changes, mandatory 32-byte-rounded baking
storage with zeroed tail, and requested file-load alignment. The container task
will first supply minimal uniform-interface forwarding through the block's current
byte buffer and adapt typed erased-owner callbacks, keeping registration/table
checks intact. This task will change the block's forwarding to token ownership
alongside its representation refactor; baking/loading work stays here. Reload,
module-unload changes, counter-reset lifecycle, telemetry,
image implementation and new services remain excluded.
Each task needs its complete plan reviewed by the coordinator, and implementation
review plus Ritchie's own review and explicit permission before any commit.

The container task should return the agreed public signatures and source-context
semantics, accounting aggregation/reporting behaviour, class/friendship inventory
and validation results as its handoff. Then this task rechecks that contract and
finalises block adoption/storage preparation and loading before submitting its
complete plan for coordinator review. Routine design steps do not need individual
coordinator approval. Ritchie performs all pushes manually.

Tests should establish all-or-nothing reattribution, allocator/source-context
rejection, checked count/byte accumulation, balanced accounting, complete nested
payload/metadata replacement, empty-member context handling and subsequent
growth under the intended target context. Document tests should cover pointer/key
stability, failed adoption preserving existing owners, move/reset/destruction,
validation failures (including the 2 GiB extent limit), deterministic bake/promotion
and exact serialized output. Over-limit extent rejection should be exercised
without requiring allocation of a multi-gigabyte document.
Loading tests should cover the default 16-byte floor, requested 32/larger
alignment, rounded zeroed tails, padding, invalid/overflow requests according to
the chosen API, and round trips without added file bytes. Cross-configuration
coverage should reflect changes to shared headers, including Win32/x64 and
Debug/Release where supported by the existing test setup.

Host lifetime/admission, TGA sequencing, image manipulation and new JSON/filesystem
services remain wider consolidation context, not automatic coding assignments
for this task. The coordinator must reconcile the older broad stage-1 allocation
with this task's developed boundary when reviewing the complete plan; this does
not discard the wider agreed ownership rules. No production implementation has
started, and no commit is authorised.

## Supporting evidence and design history

The current inventory and decision list above take precedence. The sections
below retain code observations and the evolution of earlier proposals; historical
questions are not additional current tasks or unresolved decisions unless carried
forward above.

## Scope reassessment: current direction

15 September 2026, subsequent to the wrapper discussion below: Ritchie's reading
of the existing code changed the premise of the proposed reattribution redesign.
Existing container reattribution provides useful flexibility and is to be
preserved. Do not remove container transfer support, narrow its public surface
or restructure friendship on the earlier premise. Concrete cleanup may remain,
but Ritchie initially considered it more appropriate to address by hand. The
later public-interface organisation and live-document additions are now included
in the current scope above; the original narrowing proposal remains superseded.

On 16 September Ritchie separately reopened access visibility: retain the
reattribution capability but consider making its operational machinery private,
so domain transfer through the erased carrier drives it. That question is distinct
from the superseded removal of container support. His subsequent clarification
favours an explicitly separate public infrastructure interface where appropriate,
rather than increasing friendship to enforce privacy; see the discussion below.

The generic byte-wrapper plan was reopened, including its earlier description as
settled buffer-plus-type storage. The current task now develops the retained
baked-block/token design above. The image representation remains wider design
context and is not selected for implementation here.

The retained CBakedDocumentBlock with direct memory-token ownership is now the
coding direction, with its adoption/preparation API still to settle. Whether image
manipulation should use a non-owning image view without a dedicated owning wrapper
remains a separate question. No production implementation has begun.

Permanent Host ownership, separate ownership/receipt/operation sequencing,
stage-1 TGA migration, aligned primitive loading, accounting and safe exit
destruction remain wider consolidation decisions. See the expected task scope
above for Ritchie's latest implementation focus. Earlier wrapper-dependent proposals below are
preserved as discussion findings, not requirements for implementation. Their
component and test boundaries must be revised after the alternative is developed.

## Working and review arrangement

Ritchie clarified the coordinator's role in this stage task:

- Develop the design and implementation plan with Ritchie here, using and
  updating the shared documentation. Resolve design gaps here as they arise.
- Once the complete plan is ready, send it to the coordinator for review against
  the wider refactor, documentation and context available to that task. Routine
  design decisions do not require individual coordinator approval or updates.
- The coordinator also performs pre-commit review of the implementation. A commit
  requires passed coordinator review, completion of Ritchie's own review and his
  explicit permission to commit. Address review findings before committing.
- Ritchie performs all GitHub pushes manually. This task must not push.

## Continuing consolidation requirements and constraints

- Preserve the existing container reattribution infrastructure. The earlier
  restriction to byte/image transfer wrappers is superseded.
- Define the baked-document and image representations with Ritchie, then derive
  the configuration needed to populate them. Exact representations and move/view
  interfaces are reopened.
- An image wrapper describes owned image content and the metadata needed to use
  its view. It is not itself the configuration for a load/decode operation.
- Keep the erased owner's allocated payload shell. The payload representation
  remains under discussion. A content tag alone does not validate external baked
  data. The earlier buffer-plus-type wrapper is reopened, as detailed below.
- Filename content is expected to be UTF-8 with no embedded NUL and a physical
  trailing NUL excluded from the string-view length. This is a producer contract,
  not a requirement for wrapper validation. Ritchie explicitly rejected adding
  filename content scanning or rejection before the filesystem operation. Do not
  add UTF-8, embedded-NUL, empty-name, terminator-content or path-validity checks
  to filename wrapper admission. Content-kind, storage-state and extent coherence
  remain the wrapper's view responsibilities. Filesystem operations report their
  own failures; malformed filename content is not a fatal admission invariant.
- Where the revised representation supports moving storage out, the emptied
  wrapper must no longer supply a view. Moves must preserve destination coherence;
  precise APIs depend on the revised representation.
- Primitive file loading retains a 16-byte alignment floor, accepts requested
  alignment and can allocate a guaranteed 32-byte base for baked data. Read into
  final storage; distinguish content length, padding and capacity.
- Accepted Host assets remain owned until application exit. Neither operation
  failure nor client completion permits removing them or their storage.
- Failure to deliver an acceptance receipt does not reverse accepted ownership.
  The accepted asset remains Host-owned until exit even if its client disappears
  or never learns the asset ID.
- Asset operations require previously issued Host asset IDs. Ownership admission,
  receipt of the ID and the operation request are separate steps. Failed
  admission leaves ownership with the client; failed operations leave ownership
  with the Host. Loads create Host-owned assets and return IDs.
- The existing TGA production flow and its callers must adopt these rules in
  stage 1. Broader new load/save services belong to stage 3.
- Preserve allocation accounting, allocator compatibility and module dependency
  hazard flags. Retained dependencies remain available through exit destruction.

## Current implementation and affected consumers

`core/system/erased_owner.hpp` and `.cpp` allocate the payload shell, dispatch
registered operations and jointly reattribute shell plus nested storage after
checking their common source context. Moves preserve hazard flags. An empty
nested payload still has an allocated shell and associated accounting.

`core/system/system_erased_owner_payloads.def` registers LoadedFile, EncodedTga,
DecodedTga, BakedDocumentAsset and both TGA requests. The first two contain a
CByteBuffer, DecodedTga contains CByteRectBuffer, BakedDocumentAsset contains
CBakedDocumentBlock, and each TGA request contains a CSimpleString filename.

`core/containers/ByteBuffers.hpp` exposes public reattribution and grants the
erased owner direct access. String containers and the baked block also have
attribution forwarding/hooks. Production filename transport is the concrete
string consumer identified in the earlier inspection. Rich-container reattribution
also has dedicated tests. These findings do not justify removing that flexibility;
existing support is to be preserved under the scope reassessment.

`CBakedDocument::reset` performs full validation and retains a checked borrowed
view. `check_integrity` repeats validation. CBakedDocumentBlock holds both owned
bytes and a cached document; the baker publishes directly into it. Changing this
local baking result versus giving it a buffer extraction bridge remains open.

The only current loadFile callers are `host/runtime/host_worker_thread.cpp` and
`tests/manual/tga_round_trip.cpp`; both use default padding. Its existing contract
rejects empty files and returns an empty buffer on failure. Successful logical
size is file length plus requested padding; capacity is rounded to 16 bytes.
The whole tail after file content, including spare capacity, is zeroed.

`executive/runtime/executive_thread.cpp` sends an owning TgaLoadRequest, receives
the loaded image ID, then sends an owning TgaSaveRequest naming that image ID.
The save already refers to an existing image; its newly transferred storage is
the output filename. The Host stores requests as assets and erases those requests
on completion or failure. Inspected runtime erase calls target request assets;
successfully inserted loaded, encoded and decoded buffers are already retained.

`CAssetRepository` exposes erase, reinitialisation, whole-store deallocation and
mutable payload access. Merely deleting Host erase calls would leave other routes
to replacing or extracting accepted storage. Its public access boundary must
support permanent ownership. This requirement prevents removing or replacing
owned storage; it does not establish immutable content for all future image
operations. Checked baked views separately require immutable backing bytes.
`CASyncStates` is separate operation bookkeeping;
releasing those states need not remove assets.

`CHost::shutdown` currently stops threads, unbinds the Executive, then deallocates
assets and thread packages. The final design must order retained-asset and queued
payload destruction while their required code, metadata and contexts are usable.

## Alternative under consideration: existing owners and specialised views

Ritchie is still settling the design. Existing container reattribution is useful
even where only a small number of container types currently need Host transfer.
He observes that allowing an assigned non-owning user to modify a Host-owned
image makes similar use of other Host-owned containers natural. This observation
does not request a new generic access/assignment framework. Existing erased-owner
eligibility and nested allocation accounting still apply; container reattribution
alone does not automatically register a type as a transferable payload.

An image-manipulating view remains useful. The need for a dedicated owning image
wrapper is now questioned: owned rectangular storage and non-owning image
interpretation/manipulation can have separate roles. Pixel interpretation still
needs format and layout metadata; where that metadata belongs and how it reaches
the view remain open. Do not presume the earlier owning wrapper, its provenance
fields or encode settings are mandatory in the alternative.

Ritchie is inclined to retain CBakedDocumentBlock but replace its contained
CByteBuffer with direct ownership of the memory token, potentially taking that
token from a byte buffer. This keeps the document-specific owner while permitting
it to manage its own allocation through existing memory infrastructure.

Ritchie further identifies this as a more faithful representation of an
effectively immutable baked block: use the public disown facility to receive
the allocation rather than retaining a byte-buffer container inside the block.
The current CByteBuffer friendship for CBakedDocumentBlock is needed for its
private source-context and context-replacement forwarding. Direct token ownership
would remove that dependency because the token exposes the corresponding
facilities. Removing this specific friendship would follow from the representation
change; it is not a return to broad friendship or container-transfer restructuring.
Allocation ownership and replacement remain distinct from exposing mutable baked
document content through the block's public API.

Code findings supporting this possibility:

- CByteBuffer::disown(MetaByteBuffer&) already moves out its CMemoryToken and
  returns the logical-size/capacity metadata. No added friendship is needed to
  use this public handoff, and moving the token does not copy the bytes.
- CMemoryToken already implements allocation, prefix-preserving reallocation,
  accounting and explicit reattribution. These can back the existing block's
  attribution methods directly, preserving the erased shell plus nested-storage
  accounting pattern.
- The baker currently constructs and validates a byte buffer, then moves it into
  the block alongside a checked CBakedDocument. It could hand off the allocation
  token instead; this is an implementation possibility, not yet a chosen API.
- The byte buffer's token count represents capacity, not logical content length.
  Adoption must retain the actual document extent from the buffer metadata or
  an established checked document view; token.bytes() is not automatically the
  baked document length, particularly after padded file loading.
- Moving an allocation preserves its address. Reallocation can change the address
  and content extent, so the block's checked view must become unavailable or be
  rebuilt as appropriate. Previously borrowed views require corresponding usage
  discipline. Whether reallocation means rebuilding/replacing a complete document
  or exposing an intermediate allocation state remains to be specified.

These findings support discussion of the alternative without selecting its
complete layout, adding image manipulation algorithms or changing production code.

### Reattribution access visibility: 16 September discussion

Ritchie asks whether container reattribution machinery can and should become
private, since attribution normally changes as part of thread/module transfer
rather than through direct client calls. The assistant initially recommended
private/protected operational hooks and public accounting observations. That
recommendation preceded Ritchie's fuller explanation below and is superseded
as the preferred approach; it is not an instruction to reorganise visibility.

Code inspection finds production calls at the erased-owner/transport boundary
and within container composition. Direct container calls outside that machinery
are in tests. CErasedOwner's nested operation templates execute as carrier members,
so its friendship permits access to private nested-storage hooks for the current
registered payloads. Restricting those hooks would also prevent a normal caller
from reattributing only the nested buffer and leaving an erased shell attributed
to a different context.

Friendship is not already universal or transitive. CByteBuffer, CByteRectBuffer,
CSimpleString and CBakedDocumentBlock friend CErasedOwner; TPodVector, CStableStrings
and the aggregate collection/slot types do not all do so. Composite owners still
need access to their constituent hooks, and internal storage base classes use
protected hooks. These observations inform the public cooperating-interface
inventory now selected, not a requirement to add private access paths. Preserve
existing aggregate preflight and combined accounting. Payload registration
remains explicit and is not supplied by friendship.

Tests can continue exercising the public infrastructure contract, retaining
compatibility, rejection and accounting coverage. Memory-token/context primitives
and carrier-to-transport access need inspection on their own terms rather than a
blanket visibility change. No production visibility changes or new transport
registrations have been implemented here.

#### Clarified interface organisation preference

Ritchie explains that C++ public/private/protected/friend mechanisms express only
part of the relationships between code. He uses repeated access sections to
express interface groupings the language cannot otherwise represent. Friendship
already establishes a contract or tight coupling; where a cooperating interface
would otherwise be split between public and private members and require many
friends, he is inclined to make it explicitly public in a section of its own.
Friends remain useful where justified, but should be used sparingly.

The main public interface should describe the class's own features. Memory
reattribution is instead an infrastructure feature spanning multiple classes,
although each class supplies its own implementation. Preferred design direction:
group that cooperating contract in a clearly identified public section, distinct
from ordinary container operations. Preserve private implementation details and
use friendship where it meaningfully serves the design; do not infer that every
existing friend or private helper must be removed or exposed.

This changes the emphasis from access prohibition to explicit interface roles.
Source-context discovery, accounting, compatibility checks and context replacement
should form a coherent aggregate/transfer contract. Preconditions on unchecked
hooks and the requirement for complete accounting remain explicit even if those
hooks are public. Public visibility does not alter what an ordinary move does or
permit a partial reattribution to be treated as a valid aggregate transfer.

Ritchie subsequently confirmed a single-line explanatory comment before the new
public section, and explicitly included this formatting/relationship change in
the coding scope. Exact class/member grouping remains to be developed in the
complete plan; this is not a blanket exposure of private implementation details.

### Live-document reattribution: included in current scope

Ritchie initially asked whether CLiveDocument should support reattribution
consistently with aggregate containers, while retaining baked transfer. He has
now explicitly included this capability in the task's coding scope. He has also
confirmed that no production erased-payload registration is to be added here.

Inspection finds three document-owned aggregates: TPodOrderedSlots<CLiveNode,
CNodeKey>, CStableStrings for property names, and CStableStrings for string values.
Each supports reattribution. Nodes refer to document-owned content through slots,
keys and string IDs. Caller-owned analysis scratch is not part of the document
and must not be included in a document transfer.

Implementation direction: support live-document reattribution using the established
aggregate pattern, while retaining baked transfer as a normal workflow choice.
Preflight source-context coherence and allocator compatibility for all owned
storage, transfer aggregate accounting, then replace context pointers without
recounting. Rejection must leave every member unchanged. The component hooks
need appropriate composition access; merely calling three fallible public
reattribute methods in sequence does not establish an all-or-nothing operation.
Cover unallocated member state as well as allocated storage, and verify growth
after transfer under the destination's intended ambient context.

This capability neither selects live documents as the standard service payload
nor automatically registers them with CErasedOwner. It does not make concurrent
access safe: mutation remains single-threaded with an explicit handoff between
users. Ordinary moves continue preserving attribution unless an explicit
reattribution is requested.

Keep the existing accounting observations.
CLiveDocument::initialise already uses memory_allocation_count to reject existing
owned storage, and live-document/parser tests use allocation totals to establish
unchanged storage after failures and separation from analysis scratch. These
methods sum existing member accounting; they do not introduce separate counters
that exist solely to support reattribution.

### JSON loading continues to publish baked blocks

Ritchie prefers to retain the planned JSON-loading workflow even if live-document
reattribution is added: parse into a local CLiveDocument, apply caller policy,
bake the accepted result, and return only the CBakedDocumentBlock to the Host.
The Host becomes the durable owner of that resource; an immutable published
document is the intended result of loading. Live construction and its scratch
remain local to conditioning and can be released once the result is baked.

Reattribution capability does not change that service contract. A client may
later promote a borrowed baked document into its own mutable live document.
Explicit transfer of that live document could be supported as with other
containers if a use arises, subject to the ordinary payload registration and
Host admission contract. No current use case or new service is selected for it.
Promoting a local working copy does not change the retained baked original.

This is the chosen direction for the later JSON service, not an expansion of
this task into implementing that service. It preserves the distinction between
the container's transfer capability and the representation a service publishes.

### Consistent non-owning memory representation

Ritchie proposes that CBakedDocument use CMemoryConstView, following CStringView.
Inspection confirms CStringView stores a CMemoryConstView plus its logical
string length, whereas CBakedDocument currently stores a raw byte pointer and
size directly. Earlier descriptions of their pointer/length semantics remain
conceptually correct but did not capture this implementation difference.

Proposed document representation: one CMemoryConstView bounded to the exact
document extent, with byte stride (1) and the required 32-byte base alignment.
Its data() and bytes() can replace the raw pointer and separate byte count.
Unlike CStringView, a baked view does not need a logical length distinct from
its memory extent. Capacity and file-load padding must not enlarge that extent.

CMemoryConstView provides bounded, non-owning memory description; it does not
validate a document's encoding or structure. Preserve checked document
construction and explicit baked alignment validation. Its alignment handling
can reduce a requested guarantee to match the actual address, so specifying 32
alone does not reject a misaligned pointer. Check the common memory-view size
limits against the baked format's existing accepted range during implementation.

This fits the token-owned block direction: CMemoryToken owns the allocation,
CBakedDocument interprets an exact CMemoryConstView into that allocation, and
the block keeps the owner and checked document together. This is a proposed
refinement of the existing types, not authority to begin implementation.

### Capacity reduction before token extraction

Ritchie initially suggested that the baked-block refactor might justify an
explicit resize operation before token extraction. His subsequent decision is
mandatory explicit reallocation/zeroing in the baking route; the file-load route
already supplies rounded final storage. The observations below explain available
operations, not an unresolved optional-trimming policy.

Current code already distinguishes the relevant operations:

- CByteBuffer::resize changes logical size and may grow capacity; it does not
  shrink an already sufficient allocation.
- CByteBuffer::shrink_to_fit calls reallocate(size, size, align()), making capacity
  equal to logical size while retaining alignment. A capacity change allocates
  and copies; failure leaves the original buffer unchanged. When size already
  equals capacity, this is a no-copy operation.
- The baker already allocates output with logical size and capacity both equal
  to m_total_size, at baked_document_format::k_block_alignment. Its current
  publication path has no excess capacity to remove.
- CByteBuffer::reallocate creates its replacement token using the ambient memory
  context; CMemoryToken::reallocate allocates through its existing context. The
  chosen preparation path must define/preserve attribution intentionally, rather
  than assuming these operations have identical context behaviour.

Use explicit reallocation with rounded capacity in the baking route; add no
redundant generic resize method without a concrete need. Exact shrink_to_fit
would undo the agreed rounding rule below. Complete any actual
reallocation before constructing the final memory/document view; previously
constructed views cannot be carried over blindly if the allocation moves.
Preparation failure is handled like any other allocation failure during baking.
The file-load route must not force an extra alignment copy.

### Aligned loading and matching block storage

Ritchie explicitly adds file-loader alignment to this task and agrees that
directly baked blocks use the same multiple-of-32 capacity as loaded blocks.
Both paths should produce the same usable representation, without origin-specific
handling by document consumers.

Ritchie's rationale: capacity follows address alignment because content is often
accessed in power-of-two blocks, for example through vector registers. This is
the existing file loader's intention at 16-byte alignment, now extended to caller
requests while retaining 16 bytes as the minimum loaded alignment.

Current evidence: the baker allocates an exact unrounded document length at a
32-byte-aligned address. loadFile currently rounds capacity to 16 bytes but keeps
logical size equal to content plus requested padding. The baked format's version-3
validator requires header.total_size to equal both supplied document length and
the exact end of its sections; arbitrary trailing bytes are not currently part
of the document format.

Agreed common storage rule: request a 32-byte-aligned base and round
allocation capacity up to a multiple of 32 on both paths. Keep the document view
bounded to the actual serialized length and zero the spare tail consistently.
For example, 150 document bytes would use a 160-byte allocation on either path,
while the document view and saved file remain 150 bytes. The owning token records
capacity; the CMemoryConstView records document extent. This requires no baked
format change and no alignment copy after loading. Larger caller-requested
alignment uses matching capacity rounding. The loader's effective alignment has
a 16-byte floor; baked loading requests 32 bytes. Ritchie has now selected the
existing memory alignment conditioner, with raw above-cap requests rejected,
as recorded in the current inspection findings above.

Capacity rounding does not add bytes to the serialized document or change its
header total. A format change to include trailing padding is not selected.

Under this common capacity rule, any explicit trimming before token
extraction should trim excess to the rounded block capacity, not blindly call
shrink_to_fit and undo that rounding. Extra file-load padding is separately
requested storage and must not be mistaken for serialized document bytes.

## Earlier wrapper design discussion: reopened

Ritchie requests that wrapper structure and move-in/move-out semantics lead the
design, with loading configuration derived afterwards. The following records the
earlier buffer-plus-type and rect/image discussion. The generic byte-wrapper
choice is now reopened; neither the table nor its detailed consequences below
establish the replacement design. Useful findings remain available for that work.

| Component | Owned storage | Additional description |
| --- | --- | --- |
| Byte wrapper | CByteBuffer | Only content kind; derive view extent from the buffer |
| Rect/image wrapper | CByteRectBuffer | Blind rectangular storage needs no image interpretation; image content needs pixel format, orientation, provenance and encode configuration |
| Load/decode configuration | No result storage | Instructions needed to produce the requested content and layout |

The buffers already carry storage dimensions: the byte buffer knows size,
capacity and base alignment; the rectangle knows active row width in bytes,
row pitch, row count and alignment. Pixel interpretation is additional image
metadata. For the current Gray/RGBA/RGBX formats, pixel width can be derived
from active row bytes and bytes per pixel; height is row count. These need not
be duplicated in the owning wrapper merely to expose them in an image view.

Proposed move-in operation: move storage and its content description together.
Obtain borrowed views afterwards through separate accessors. This keeps ownership
and the description coherent without requiring every view to be constructed on
move-in. Whether to also permit bare-storage adoption followed by a separate
description operation remains open; it would require defining the intermediate
state and which views are available from it.

For byte content, the type specifier supplies blind/filename/baked interpretation.
Blind content needs no additional interpretation view; the existing byte buffer
already supplies storage observations. The wrapper holds no duplicate length
and no cached string/document view. For baked content, buffer.size() must supply
the exact document extent. Allocation capacity and alignment remain separate;
this does not require reallocating to remove spare capacity. Reconcile file-load
padding with that logical extent when specifying loading configuration.

Retain the existing CStringView and CBakedDocument representations, which refer
to memory using a pointer and length. Construct them using the owned buffer's
pointer and derived extent after checking that the requested view matches its
type and storage state. They need not reference the byte buffer object. Filename
length excludes the expected trailing NUL; derive it from stored logical size
without scanning content, and handle zero size without underflow. Empty and
absent string views need no different filename treatment: neither is usable as
a filename. This does not add filename validation to admission.

Moving in byte storage does not construct or cache a view. The type tag alone
does not validate baked content. The existing checked document construction can
validate when creating the borrowed view; users can retain that view outside the
wrapper for repeated access. Exact accessor placement and loading/publication
validation sequencing remain to be finalised. The previous proposal to cache a
validated document inside the simple wrapper is superseded.

For image content, move-in must at least establish how the rectangle's bytes
represent pixels before an image view can be offered. Format and orientation
describe the current pixels. Decode provenance describes their source; encode
configuration describes a possible future output. Those are distinct roles;
provenance cannot be mandatory TGA history for an image constructed locally.
Exact provenance fields and configuration defaults remain to be chosen.

Ritchie distinguishes a blind rectangular transfer wrapper, which is as simple
in principle as blind byte ownership, from an image interpretation of that
rectangle. A rect buffer alone does not supply image semantics. Image use and
future modification need the additional description and appropriate access to
the pixels. Do not make the image design inherit a permanent const-content rule
from the simple byte/baked representation. Initial manipulation algorithms remain
outside stage 1, but the storage/view contract must accommodate their later use.
Whether this is expressed as the image wrapper with a blind state or another
layout choice remains to be finalised; it does not require a new generic framework.

Move-out empties the wrapper and prevents further view extraction. Whole-wrapper
move transfers storage and its description together. Finalise what metadata is
returned or remains queryable after moving only the buffer out; the agreed
failure of subsequent views does not alone settle that metadata policy.

Ritchie prefers replacement when moving into an occupied wrapper. Proposed rule:
release the destination's previous storage, discard its previous description and
cached views, then take the incoming storage and its accompanying description.
This follows the existing buffers' move-assignment behaviour. Whole-wrapper
self-move assignment is a no-op. Previously borrowed views of the replaced
storage become invalid; copied views cannot be revoked automatically. Any
fallible move-in preconditions must be checked before replacing the destination
or consuming the source. Ordinary replacement does not implicitly reattribute
the incoming allocation.

Replacement is a wrapper lifetime operation. The Host repository's permanent
ownership boundary must not expose replacement or extraction of accepted asset
storage during the run.

## Earlier proposals and outstanding discussion

Wrapper-dependent proposals in this section are reopened by the scope reassessment.
The aligned-loading and fatal-insertion directions remain applicable independently
of the eventual payload representation.

1. Filename content expectations and absence of wrapper content validation are
   settled above. Whether existing TGA requests use separately admitted filename
   assets remains a representation proposal, discussed below.
2. Wrapper state: clear content descriptor after extraction or move-from. The
   simple wrapper has no cached view to clear; any image view state must also
   become unavailable. Ordinary moves/extraction preserve attribution; explicit
   erased-owner transfer changes it. Extracted storage accounts independently;
   the emptied shell remains accounted for. Previously copied borrowed views
   depend on the continuing backing lifetime and cannot be revoked automatically.
3. Baked bytes: preserve checked CBakedDocument construction using the buffer's
   exact logical extent and required base alignment. No cached document belongs
   in the simple wrapper. Finalise view accessor placement and when the loading
   service establishes a checked result before publishing it. Preserve backing
   immutability while checked document views are used.
4. File loading: preserve existing logical size and zero-padding behaviour, add
   explicit alignment and expose content/padding metadata. Capacity remains a
   buffer property. Final signature and whether metadata belongs in a small result
   or an output parameter remain routine design details to settle. Check rounding
   and size bounds before allocation. No production caller needs nonzero padding
   compatibility migration today.
5. Image metadata: the existing codec supplies Gray/RGBA/RGBX decoded layout and
   accepts EncodeOptions. It does not currently expose original RLE, palette,
   source pixel depth or header orientation as decode provenance. Agree the useful
   provenance fields before changing the decode result. Preserve the distinction
   between current pixel layout, original encoding and future encode options.
6. Admission failure: Ritchie observes that repository insertion failures are
   likely fatal to the application, making the transport/insertion gap primarily
   theoretical. The design direction is therefore to treat resource exhaustion
   or broken repository invariants after valid transfer as fatal shutdown cases,
   rather than introducing a recoverable asynchronous ownership-return protocol.
   Ordinary admission checks should reject before the owning post consumes
   storage. Specify those checks and the acceptance point in the final flow.
   Successful posting still means storage is in transport; receipt of a valid
   asset ID permits a dependent operation. Failed delivery of that receipt leaves
   an accepted asset Host-owned until exit, including if its client disappears.
   Fatal insertion failure produces no valid ID or dependent operation, and its
   still-owned payload must have safe shutdown cleanup. Application-fatal failure
   does not promise normal rejection and restored client operation.

   Code evidence: CAssetRepository::insert rejects an unready owner or exhausted
   ID sequence; collection insertion can fail to acquire/map storage. Its move
   into CAssetRecord occurs only after those checks succeed. Thus failed insertion
   leaves the received owner intact locally. With a coherent ready wrapper and
   monotonic IDs, the remaining failure paths concern capacity/resources or
   invalid internal state. Existing Host insertion failures already report a
   critical event carrying a shutdown reason, although they also send ordinary
   operation-failure results; final implementation must make the chosen fatal
   path explicit.

## Proposed concrete admission boundary

This earlier proposal makes the agreed failure direction reviewable. Its
restriction to ready byte/image wrappers is reopened; payload eligibility and
checks must follow the revised representation. Existing container transport
support must remain available. The post/insertion/receipt distinctions remain
useful independently of wrapper choice.

Before posting an asset for admission, the admission entry point checks that it
contains a supported, ready byte/image wrapper with coherent state and a usable
correlation slot. Baked validation follows its separate content contract; filename
content is not scanned or validated here. The existing transport checks message/payload
identity eligibility for the destination, queue readiness and available space,
common shell/storage attribution and compatible allocation contexts. These checks
precede consumption; an ordinary failure leaves the caller-owned message and
its storage intact. A shell containing an extracted/empty wrapper is transportable
as a general erased value but is not a ready asset for this admission entry point.

Successful post puts the owner in the queue and ends client ownership/access as
an owner. The queue retains the hazard flags and transport attribution. Reading
moves it into a Host-local receiver and reattributes to the recipient context.
Successful repository insertion is the permanent asset acceptance point. Only
after insertion does the Host issue the correlated asset-ID receipt. The client
requires that receipt before submitting an operation on the asset.

An unexpected invalid payload after this checked entry point is a protocol or
invariant fault, not a recoverable rejection which can claim the client still
holds its buffer. A failed insertion or recipient attribution invariant stops
normal processing and enters shutdown. The Host must exit its processing path
explicitly; logging a critical event and sending an ordinary failure result is
insufficient. The failed-insertion receiver still owns its payload, so cleanup
must keep that owner valid until the relevant shutdown destruction point. Already
accepted assets remain retained. Receipt delivery failure never removes them.
Shutdown must also destroy unread queue owners before their dependencies vanish.

## Proposed TGA flow and implementation boundaries

The filename-as-asset flow below is an earlier proposal dependent on the generic
byte wrapper. It is reopened. Existing filenames need not migrate merely to
support reattribution narrowing. The requirement for the TGA flow to conform to
separate asset admission and operation requests remains in stage 1; its concrete
request representation must be developed with the revised design.

The filename wrapper permits a small migration: admit the input filename, receive
its ID, then request TGA loading by that filename ID. Receive the image ID; admit
the output filename, receive its ID, then request saving by image ID and filename
ID with save options. Requests become ordinary value messages. This is a proposal
for applying the settled sequence to the actual owning request members.

Separate filename admission is not implied for all operation metadata. Its
concrete benefit here is to let the existing owning filenames use the byte
wrapper and permanent backing while TGA requests become value messages. The
cost is an additional receipt before each operation using a new filename.
Resolve that tradeoff and the filename/request representation with Ritchie.
Inline value options such as vertical flip and encode settings do not require
their own asset identities under this proposal.

The existing load/save exercise alone does not test admission of a client-created
image. Add focused admission coverage for client-owned image storage followed by
a separate save request, alongside loaded-image reuse and rejected admission.

Earlier bounded slices, requiring revision before overall plan review:

1. Reopened: generic byte/image wrapper migration and baking ownership bridge.
   Develop the alternative baked-document and image approaches with Ritchie.
2. Superseded: reattribution narrowing and forced registered-payload/filename
   migration. Preserve current support; no replacement cleanup slice is selected.
3. Wider consolidation scope: requested alignment through primitive loading and worker request, with explicit
   content extent. Verify direct allocation and existing padding behaviour.
4. Wider consolidation scope: permanent Host ownership, admission/receipt and existing TGA caller migration;
   exit destruction ordering with preserved dependency checks.

Earlier coverage candidates, to revise for the selected representation: moves
and extraction, kind mismatch and malformed baked content,
shell-only accounting, incompatible allocators, rejected post/admission, alignment
and padding, receipt-before-operation sequencing, invalid IDs and types, failed
operations retaining assets, and exit deallocation. Broader JSON/binary services
and the full data-model Executive exercise remain later stages.

## Progress

Latest status, 16 September: current scope is consolidated at the top of this
document. It includes the public infrastructure-section organisation, adding
live-document reattribution, the memory-view/token-owned baked-document refactor,
storage preparation and aligned loading, and regression validation. Baked capacity
rounding and exact serialized extent are settled. Subsequent clarification fixes
the visibility scope to the container set, live document and baked block, using
the two existing hook families; live transfer registration is excluded. Failed
adoption preserves both owners. Baking reallocation/zeroing is mandatory; loading
uses the existing alignment conditioner, a 16-byte floor and above-cap rejection.
The 2 GiB baked-view limit is now agreed, as is complete aggregate reattribution
through checked preflight, one cumulative accounting update and subsequent unsafe
replacement. Concrete aggregate hook semantics, adoption API shape and parameter
placement still need to be incorporated into the complete plan. The full
plan awaits completion and coordinator review. No production edits or tests have
been performed by this task. Entries below preserve earlier discussion and do
not override the current inventory or outstanding-decision list.

- Initial code inspection complete; no production edits or tests run.
- Four initial questions were presented about filenames, empty state, baked
  validation and padding. Filename content expectations without wrapper validation
  and move-out view failure are now settled; descriptor reset is the proposed
  implementation. Baked validation timing and padding answers remain pending.
- Coordinator corrected TGA migration scope: it belongs in stage 1. This draft
  incorporates that correction and replaces the earlier stage-task suggestion
  that the existing TGA sequencing could wait until stage 3.
- This draft is an inspection/discussion handoff. Final design review and
  implementation review have not yet occurred; no commit is authorised.
- Coordinator read the provisional draft and requested the explicit filename
  tradeoff, separate acceptance/delivery failure cases, and the distinction
  between permanent storage ownership and content immutability now recorded
  above. Filename representation and separate admission remain under discussion.
- Ritchie subsequently identified insertion failure as likely application-fatal.
  The admission discussion now follows that simpler failure model; the final
  specification still needs the concrete pre-transfer checks and acceptance point.
- Ritchie clarified that expected filename content must not become a wrapper
  validation requirement. This supersedes the assistant's proposal to validate
  UTF-8, embedded NUL, termination and nonempty names before admission.
- Ritchie directed the design to begin with wrapper structures, move-in arguments
  and view construction, then derive loading configuration. Image result metadata
  and load/decode configuration must be distinct. The structural proposal above
  records this focus; view timing and move-in details remain under discussion.
- Ritchie prefers destroying existing content when moving into an occupied
  wrapper. The proposed replacement semantics above record that preference.
- Ritchie clarified the simpler byte wrapper's scope: no additional view for
  blind content, minimal baked pointer/extent representation with possible extent
  derivation from an exactly sized buffer. Blind rect transport is similarly
  simple; image interpretation and modification need additional metadata and
  access semantics. The structural proposal now reflects this distinction.
- Ritchie settled the simple wrapper as buffer plus type specifier only. Views
  use the existing memory-pointer/length forms, with extent derived from the
  buffer. Removed the superseded proposal for a separate size and cached baked
  view. Empty versus absent strings needs no special filename distinction.
