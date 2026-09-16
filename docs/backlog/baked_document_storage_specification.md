# Baked-document storage and aligned loading

Updated 16 September 2026. Ritchie has accepted the remaining API defaults.
The initial implementation and subsequent buffer-owned/on-demand-view delta
passed coordinator implementation review on 16 September. Ritchie subsequently
approved the version-4 physical-format revision below; its implementation,
four-configuration validation and coordinator implementation review are complete.
Ritchie completed his review and explicitly authorised the bounded implementation
commit on 16 September. Pushes remain his responsibility and are not authorised.

Task: `01a0a4d6-e506-7912-8cf8-0ace7c96453a`, local.
Coordinator: `01a09fd4-73ad-7ce2-a3e4-efcdadb454dd`, local.
Use the shared main checkout. Preserve unrelated dirty coordination documents.
Prerequisites are committed: diagnostic accounting `d1d804c` and uniform owner
attribution `42a908d`. The [stage record](consolidation_stage_1_specification.md)
preserves discussion; this focused plan defines the remaining implementation.

## Current delta: stored section offsets and version-4 header

Ritchie's further 16 September revision retains explicit counts and adds section
offsets to the baked bytes, without caching layout in the view. This supersedes
the version-3/no-format-change statements in the earlier records below.

- Retain family magic MBD2 and the first 32 header bytes; set version to 4 and
  header size to 64. Append five uint32 offsets at bytes 32 through 48 for values,
  property-name references, string-value references, property-name bytes and
  string-value bytes, respectively. Three explicit reserved uint32 words at
  bytes 52 through 60 must be zero. Assert physical sizes and field offsets.
- Keep canonical contiguous section order. Values begin at byte 64, preserving
  32-byte alignment. Minimum serialized extent becomes 114 bytes and minimum
  rounded owning capacity becomes 128 bytes. Reject earlier versions; no
  migration service is introduced.
- The baker writes its computed offsets. Validation checks header identity,
  extent and reserved words, then checks every stored offset against counts and
  exact adjacent section boundaries using widened arithmetic. The final end must
  equal total_size and the checked extent within the existing 2 GiB limit. All
  section geometry must pass before any section is dereferenced; retain existing
  nonzero-count, string-table, record and topology validation.
- Normal queries use stored offsets and existing count bounds. Remove the view's
  SLayout/derive_layout and unused derived-layout helper. Audit direct node access
  as well as string access. The validated immutable-storage contract remains the
  basis for these accessors.
- Preserve the buffer-only owner, allocation-free block-reference views, checked
  adoption guarantees and aligned loader. Preserve Ritchie's manual style edits.
- Update physical-format fixtures and maintained documentation; test each offset,
  each reserved word, contiguity, widened arithmetic, truncated headers, earlier
  versions and node alignment. Revalidate all four configurations and the smoke
  exercise, then obtain coordinator implementation review.

Coordinator design review passed on 16 September with no blocking findings. This
approval covers the proposed design, not its forthcoming implementation.

### Version-4 implementation and validation

Implemented in baked_document_format.hpp, baked_document.hpp/.cpp and
document_baking.cpp, with both existing baked test suites and the maintained
format, semantic and design-note documents updated. SLayout and view-side
derive_layout are removed; ordinary node/string access reads stored offsets.
validate_layout is validation-only, checks every exact boundary with uint64
arithmetic and zero reserved words before section access. The baker retains its
producer-side layout calculation and writes all five offsets into the header.

Tests cover all five offsets (zero, inside header, over-limit, UINT32_MAX,
misalignment, aligned gaps/overlaps), all three reserved words, counts causing
widened overflow/extent rejection, earlier versions, truncated headers, exact
root-only offsets and actual node alignment. Adoption failures preserve both
owners, including malformed offsets and reserved words. Existing allocation-free
view, padding, lifetime, attribution, parser/writer/promotion and transport tests
remain green. Fixtures now use a 114-byte minimum document and 128-byte capacity;
larger-capacity adoption fixtures retain 192 bytes.

All four complete solution builds and mode-1 test runs exited 0, with zero test
failures. BakedDocument reported 1,711 passing checks and BakedDocumentTransfer
568 in every configuration. The latter count is 64 lower than the version-3 run
because the 32 additional serialized bytes leave fewer spare-tail bytes to check
in the fixed-capacity 64/256-alignment cases; no test cases were removed.

| Configuration | Platform | Log tag | Test process |
| --- | --- | --- | --- |
| Debug | x64 | baked-v4-dbg64 | 87608 |
| Release | x64 | baked-v4-rel64 | 67752 |
| Debug | x86 | baked-v4-dbg32 | 24048 |
| Release | x86 | baked-v4-rel32 | 85744 |

Each command used tools/invoke_sandbox_build.ps1 with the table's Configuration,
Platform and LogTag plus -RunTests. Logs are beneath the corresponding
build/sandbox-test-output platform/configuration directories. Policy validation
reported no errors/warnings and only its existing suppressed negative identity
test. No unexpected accounting/destruction/unload diagnostics were found;
intentional transition diagnostics remain in dedicated accounting-test logs.

The rebuilt Debug x64 engine smoke exited 0 from build/baked-storage-smoke.
Its logs/morphic_debug.baked-v4-smoke.p81936.log records successful TGA/file work
and normal Executive/worker shutdown; the direct log is empty and no error or
accounting incidents occurred. The tracked TGA output remains untouched.
git diff --check and tools/check_line_endings.ps1 passed.

Coordinator implementation review passed on 16 September with no blocking
findings. Review covered the physical layout, validation ordering and widened
arithmetic, all normal node/string access paths, baker emission, malformed-input
and adoption-preservation tests, and maintained documentation. The reported
four-configuration evidence was reviewed and the smoke log sampled; diff and
line-ending checks also passed during review. The transfer-suite check-count
reduction is consistent with its per-byte tail checks and the larger header.
Ritchie subsequently completed his review and explicitly instructed this task to
commit the reviewed changes. The bounded commit includes the implementation,
tests, maintained documentation and this focused record; unrelated broader
backlog documents remain outside its scope. Ritchie performs any push manually.

## Previous delta: buffer-owned block and views constructed on demand

Ritchie's 16 September revision supersedes the token-owned/stored-document
representation in the earlier plan and implementation record below. The checked
extent, format, adoption failure guarantees, rounded baking and aligned loading
contracts remain unchanged.

- CBakedDocumentBlock privately owns only CByteBuffer. It exposes no mutable or
  buffer accessor and stores no document view or separate readiness flag.
- Adoption completes the existing geometry, header and full candidate validation
  before changing either owner. Then set the source's logical size to the validated
  total_size using the existing nonallocating set_size and move the buffer into
  the block. Readiness and bounds already establish that set_size can succeed;
  no fallible work follows successful normalisation. Capacity, address and
  attribution remain unchanged. Rejected adoption preserves both owners.
- Add explicit CBakedDocument(const CBakedDocumentBlock&) noexcept. It builds its
  CMemoryConstView from the block's public byte view without content validation
  or scratch allocation. An empty block produces an empty document view.
- Preserve block.document(), returning CBakedDocument by value through that
  constructor. Both construction routes borrow the validated storage and do not
  extend its lifetime. Audit existing callers for the change from reference return.
- Pointer/extent construction and reset remain fully checked; check_integrity
  remains an explicit full validation. Whole-block moves and deallocation operate
  on the buffer only, preserving empty/moved-from and self-move behaviour.
- bytes() remains an immutable exact-extent CByteConstView. Preserve the established
  actual 32-byte alignment guarantee for document views even when adopted storage
  has a weaker recorded allocation alignment but passes the actual-address check.
- The uniform attribution interface forwards to the private buffer. Update
  maintained documentation to supersede the token-owned/stored-view description
  and preserve Ritchie's manual formatting edits.

Coordinator delta design review passed on 16 September with no blocking findings.
The private validated-buffer invariant supports allocation-free view construction;
normalising logical extent removes the need for a second stored descriptor.
Add tests for both view-construction routes, empty/moved-from blocks and successful
construction under a rejecting allocator, retaining all adoption failure and padded
input coverage. Revalidate the revised implementation and affected callers across
the existing four configurations, then obtain fresh implementation review.

### Delta implementation and validation

The delta is implemented in baked_document.hpp/.cpp and the existing baked suite,
with maintained format, semantic, design-note and memory documentation updated.
The block has one private buffer, defaulted moves and a by-value document accessor;
the explicit block-reference constructor uses public immutable bytes and no
friendship. bytes() retains the actual 32-byte alignment guarantee rather than
merely forwarding potentially weaker allocation metadata. Adoption validates
before nonallocating logical-size normalisation and buffer move.

The caller audit found no required adaptations. Existing direct consumers copy
views, pass them into const-reference operations, or bind local const references
whose temporary lifetimes extend through their scopes. Neither construction
route extends the underlying allocation's lifetime. The new rejecting allocator
counts zero requests from block-reference construction and document(), including
empty/moved/deallocated states; arbitrary-byte construction and check_integrity
still request scratch and fail under that allocator. Existing padded adoption,
replacement, self-move, attribution and byte-exact file regressions still pass.

All four fresh complete solution builds and mode-1 test runs exited 0. Each
reported BakedDocument 1,457 passed / 0 failed and BakedDocumentTransfer 632 passed
/ 0 failed, with every other suite and the unordered smoke checks passing.

| Configuration | Platform | Log tag | Test process |
| --- | --- | --- | --- |
| Debug | x64 | baked-view-dbg64 | 75756 |
| Release | x64 | baked-view-rel64 | 82564 |
| Debug | x86 | baked-view-dbg32 | 81576 |
| Release | x86 | baked-view-rel32 | 73036 |

Commands used tools/invoke_sandbox_build.ps1 with the table's Configuration,
Platform, LogTag and -RunTests; logs remain under the corresponding
build/sandbox-test-output platform/configuration directories. Policy validation
has zero errors/warnings and the existing suppressed negative identity test.
No unexpected accounting, context-destruction or safe-unload imbalance reports
were found; intentional injected transitions remain in dedicated accounting logs.
Diff and line-ending checks passed.

The rebuilt Debug x64 engine also passed its isolated smoke run (exit 0), using
build/baked-storage-smoke and tag baked-view-smoke. Event log
logs/morphic_debug.baked-view-smoke.p69816.log records successful TGA/file work
and normal Executive/worker exits; its direct log is empty and no error/accounting
incidents occurred. The tracked TGA fixture/output remain unchanged.

Coordinator implementation delta review passed on 16 September with no blocking
findings after inspection of ownership/view paths, rejecting-allocator tests,
caller usage and maintained documentation. The reported four-configuration
results were reviewed and the engine smoke log sampled; diff and line-ending
checks passed. This approval precedes the version-4 delta above and does not
cover its implementation. Ritchie's review and explicit commit instruction remain
separate gates; nothing has been committed or pushed. The earlier token-owned
implementation record below is historical only.

## Earlier scope and agreed contract

- CBakedDocument uses CMemoryConstView, with exact document extent and a 2 GiB
  maximum, including borrowed documents. Preserve full checked validation and
  existing public observation behaviour. No serialized-format/version change.
- CBakedDocumentBlock owns CMemoryToken directly alongside its checked document.
  Add `[[nodiscard]] bool adopt(CByteBuffer&& source) noexcept`. No separate tag,
  extent argument, raw-token adoption overload or storage-extraction API.
- Failure preserves source and destination. Success empties the source, preserves
  its allocation address/attribution, and replaces/releases old destination storage.
  Whole-block moves and immutable access remain; no mutable content API is added.
- Base addresses must be 32-byte aligned and allocation capacity divisible by 32.
  Allocation size and actual serialized extent remain distinct. Producers zero
  spare storage, but adoption does not scan or reinterpret the spare tail.
- Baking explicitly reallocates rounded capacity before emission and clears the
  allocation. This is mandatory; failure is an ordinary bake failure. Loading
  reads into its final aligned allocation, without an alignment-copy step.
- Append `const std::size_t alignment = 16u` after loadFile's existing pad argument.
  Keep the existing memory alignment conditioner and 16-byte floor; reject raw
  requests above the representational cap before conditioning. Round capacity to
  the effective alignment, with checked arithmetic. Preserve existing padding,
  empty-file and failure semantics.
- Add an alignment field defaulting to 16 to FileLoadRequest and forward it through
  the existing worker. No new service or unrelated load options.

## 1. Checked view and format limits

Change CBakedDocument's raw pointer and separate byte count to one
memory::CMemoryConstView with byte stride 1 and storage alignment 32. Internal
record access derives its pointer/extent from that view; public API consumers
need not change. clear() resets the view. Preserve the current reset() failure
behaviour (the receiving borrowed view becomes empty); block adoption uses a
separate candidate view to protect an existing destination.

Reject a supplied extent above memory::k_byte_size_ceiling before header/content
inspection, and retain explicit pointer-alignment checks: CMemoryConstView's
alignment normalisation alone does not reject a misaligned input. Run structural
and semantic validation against the exact extent and set the memory view only
on success. Preserve check_integrity and existing section/root/string rules.

Derive format minimums from sizeof the header/records: 32-byte header, one 32-byte
root record, two 8-byte sentinel string references and two one-byte string tables
give 82 serialized bytes, or 96 allocated bytes at 32-byte capacity rounding.
Expose narrowly named constexpr format bounds if needed by adoption/tests; avoid
duplicating unexplained literals. Keep the version-3 header and section layout.

## 2. Token-owned block and adoption

Replace the block's CByteBuffer member with a byte-stride CMemoryToken. Keep one
CBakedDocument member for the exact checked extent; no duplicate length/capacity
field. Retain the uniform two-method infrastructure section and identical common
comments. memory_attribution() uses memory::observe_memory_attribution(token);
unchecked replacement forwards to the primitive token's replacement hook.

Implement adoption in this order, without consuming source during validation:

1. Check source readiness and geometry, actual data-address alignment to 32,
   capacity divisible by 32 and capacity at least the rounded format minimum.
   Logical size must contain the header before any content is read. Buffer
   metadata checks also establish logical size <= capacity and the memory ceiling.
2. Read the header; validate magic, version and header size. Obtain total_size
   from the header and require format minimum <= total_size <= source.size(),
   and total_size <= the 2 GiB limit. Larger logical size is permitted, e.g. for
   explicit file-load padding; reserved capacity alone cannot hide truncated input.
3. Construct a temporary checked CBakedDocument over exactly total_size bytes.
   Preserve full validation. Its existing scratch-allocation failure is an adoption
   failure too; do not consume source or replace destination in that case.
4. Obtain the token through source.disown(meta), then publish token plus candidate
   view using only non-fallible ownership operations. No reallocation or automatic
   reattribution at this point. No friendship with CByteBuffer is required (the
   prerequisite has already removed the old friendship).

Keep moved-from/deallocated blocks coherent and empty, with no remaining document
view. Whole-block replacement preserves exact pointer/extent and source attribution,
including when replacing storage previously attributed elsewhere. Token destruction
releases through its own context. bytes() returns only the document extent, never
the token's capacity. Stronger source allocation alignment is acceptable; adoption
requires actual 32-byte address alignment, not exactly 32 in allocation metadata.

## 3. Baking and publication

Keep the existing layout calculation and exact header.total_size, but enforce the
2 GiB bound before allocation. In allocate_output, compute rounded capacity with
memory::condition_bytes(32, total_size), reject zero/failure and explicitly call
CByteBuffer::reallocate(total_size, capacity, 32). Clear all capacity bytes, not
only the logical extent, before emitting the header and sections. Keep preparation
under the baker's intended ambient context; no post-publication reallocation.

Use the new adoption operation for final validation/publication without a duplicate
validation pass. The proposed internal shape is a staged CBakedDocumentBlock in
the baker in place of its separate candidate CBakedDocument: after emission,
adopt(std::move(m_bytes)) into that staged block; publish by whole-block move only
after build succeeds. Remove the now-unneeded baker friendship if publication uses
only public operations. No representation or ownership access needs to be exposed.

This preserves the existing bake contract that destination is unchanged on any
build/validation failure. It performs one final document validation, no additional
byte copy and no unnecessary second allocation for rounded capacity.

## 4. File loading and request forwarding

Use the signature `loadFile(path, pad = 0, alignment = 16u) noexcept`, returning
CByteBuffer as before. For input alignment <= 2^31, effective alignment is
max(16, memory::condition_alignment(alignment)). Zero therefore yields 16; 24 and
48 also yield 16, while 32 remains 32. Reject larger raw alignment rather than
letting conditioning reduce it to something acceptable. This cap matches the token
and view alignment representation and existing memory byte ceiling; do not alter
general memory policy merely to provide the loader's 16-byte floor.

Retain getFileSize's padding overflow guard. Apply memory::condition_bytes to
file length plus pad for checked rounded capacity and ceiling enforcement. Allocate
once, read only actual file bytes, zero everything after them through capacity,
and retain logical size = file length + pad. Preserve close/read/allocation failure
cleanup and rejection of empty files. Invalid alignment fails with an empty buffer
before file I/O; no filename/path-content validation is introduced.

Extend the existing POD FileLoadRequest with defaulted alignment and pass it as
the third loader argument from the Host worker, with pad remaining zero there.
Existing TGA callers keep their 16-byte default. Audit the request's existing
transport/call sites and compile-time shape requirements; preserve registration,
identities and result types. No binary-document Host service is added.

## 5. Expected files and exclusions

Production: core/data_model/baked_document.hpp/.cpp, baked_document_format.hpp,
document_baking.cpp, core/platform/filesystem/file.hpp/.cpp,
core/system/transported_types.hpp and host/runtime/host_worker_thread.cpp, plus
only demonstrably necessary existing caller adjustments.

Tests: extend existing BakedDocument and BakedDocumentTransfer suites for adoption,
storage and file round trips, reusing allocation fixtures and isolated test-output
paths. Avoid new project items if existing suites suffice. Update maintained
data-model/memory documentation where its representation descriptions change.

Do not redo container/live-document reattribution, diagnostic accounting, carrier
tables, or registration. No generic wrappers, image work, Host asset/TGA ownership
redesign, new JSON service, module-unload/reload changes, telemetry, extraction API
or generic buffer-resize facility. If implementation reveals a material additional
design choice, discuss it with Ritchie before broadening the plan.

## 6. Validation and acceptance

View/adoption tests: existing valid/malformed document coverage; empty buffers;
misaligned address; nonmultiple capacity; below-minimum allocation; insufficient
logical header/extent despite spare capacity; invalid magic/version/header/total;
malformed sections; over-limit extent rejected before out-of-bounds access; and
validator allocation failure. Verify source bytes/address/metadata/attribution and
existing destination remain unchanged on failure. Use small fixtures for bounds
tests rather than multi-gigabyte allocations. Do not fabricate invalid owning
tokens merely to make a negative fixture.

Success/lifetime tests: exact extent from header, including logical padding and
larger valid capacity; unchanged source allocation address in destination; source
empty; replacement releases old destination exactly once; move construction,
move assignment/self-move, explicit deallocation and destruction; immutable views
and byte output exact; one owned allocation, uniform attribution, incompatible
allocator rejection on explicit reattribution, and existing erased-owner transfer.
Adoption itself preserves source attribution rather than reattributing to ambient.

Baking tests: minimum root-only document, unaligned serialized lengths, capacity
rounded to 32, zeroed tail, no extra alignment copy, exact/deterministic serialized
bytes, preparation allocation failure and failed publication preserving destination.
Inspect known producer allocation bounds via test allocator instrumentation or a
saved pre-adoption buffer pointer, not a new mutable block API for testing.

File tests: default/zero/below-floor/non-power-of-two/32/larger alignment; raw
above-cap rejection; checked padding/rounding overflow; empty/missing file; explicit
padding and zeroed spare tail; successful close/read and existing failure cleanup.
Save a baked block's bytes, verify actual file length equals serialized extent,
load with 32-byte alignment, adopt with no copy and compare with directly baked
content. Isolate test files under unique test-output paths and clean up only files
created by the fixture. Exercise existing Host/TGA flow with the defaulted request.

Run complete solution builds/core tests with tools/invoke_sandbox_build.ps1 for
Debug and Release on x64 and x86, using distinct log tags. Retain parser, writer,
promotion, live-document, container, erased-owner and transport coverage. Run the
existing Debug x64 Host/engine smoke path to cover worker request forwarding.
Inspect failure counts and attribution diagnostics rather than relying only on exit
status. No counter reset may conceal a real imbalance. Run repository line-ending
validation and git diff --check; preserve CRLF/encoding conventions for Visual
Studio-managed items if any such edit becomes necessary.

## Review gates

Coordinator design review passed on 16 September with no blocking findings.
Checked the proposed bounds and validation order against the current baked format,
checked view, buffer disown/reallocation, memory alignment/rounding policy, file
size/padding handling and worker request transport. Candidate validation before
consumption preserves both owners on failure; staged block publication supports
one final validation with no alignment copy. The format minimum and exact extent
remain distinct from rounded allocation capacity. This approval does not expand
the plan into new Host document services or change the committed attribution API.

Implement within scope and validate, then obtain fresh coordinator
implementation review. Any commit additionally requires Ritchie's completed review
and explicit commit permission. Ritchie performs all GitHub pushes manually.

## Implementation and validation record

Implemented 16 September in the eight planned production files, the two existing
baked-document test suites, and the maintained baked-format and memory-subsystem
documents. No new project items, registrations, services, accounting policy or
container attribution changes were needed. The block retains the committed
two-method interface while observing its token directly. Publication uses public
adoption and whole-block move, removing the baker friendship.

The new tests cover adoption geometry/identity/extent/structural rejection,
scratch-allocation failure, preservation of both owners, stronger alignment,
nonzero spare bytes, attribution/lifetime/moves, rounded and zeroed baking storage,
preparation allocation failure, and aligned file save/load/adopt round trips.
The minimum fixture has 82 serialized bytes and 96 bytes of capacity. Producer
instrumentation verifies the final allocation is retained without an alignment
copy; file round trips verify only the exact serialized bytes are saved.

All complete solution builds and core test runs below exited 0, with all suites
reporting zero failures. BakedDocument reported 1,432 passing checks and
BakedDocumentTransfer 632 in every configuration. Existing parser, writer,
promotion, live-document, container, owner and transport coverage remained green.

| Configuration | Platform | Log tag | Test process |
| --- | --- | --- | --- |
| Debug | x64 | baked-storage-dbg64-verified | 74072 |
| Release | x64 | baked-storage-rel64 | 21748 |
| Debug | x86 | baked-storage-dbg32 | 73800 |
| Release | x86 | baked-storage-rel32 | 38356 |

Each used `tools/invoke_sandbox_build.ps1 -Configuration <configuration>
-Platform <platform> -RunTests -LogTag <tag>`, with the installed Windows SDK
10.0.26100.0 and default test mode 1. Logs are beneath
`build/sandbox-test-output/<platform>/<configuration>/logs`. Policy validation
reported zero errors/warnings and the existing deliberately suppressed negative
system-identity test. Expected injected accounting transitions remain confined
to accounting-test logs; no context-destruction or safe-unload imbalance was found.
The suite environment checks and allocation fixtures finished with zero attribution.

The Debug x64 engine smoke run exited 0 from `build/baked-storage-smoke`, with a
copied input fixture and a separate output path, leaving the tracked TGA output
untouched. Its event log is
`logs/morphic_debug.baked-storage-smoke.p82528.log` beneath that directory; its
direct log is empty. It records successful worker file loading, TGA decode/encode,
file save and normal Executive/worker exits, with no accounting/error incidents.
An initial run completed before the isolated logs directory was created; the
recorded rerun supplied the inspected log evidence.

`git diff --check` and `tools/check_line_endings.ps1` passed. Shared pre-existing
consolidation backlog edits are not part of this implementation's code/doc diff.
Coordinator implementation review passed on 16 September with no code findings.
Reviewed the actual production/test diff against the plan and the reported
four-configuration evidence, sampled the matching test logs, and inspected the
successful isolated engine-smoke log and empty direct log. Independently reran
diff and line-ending checks. No additional build or test run was needed.

The coordinator corrected two stale maintained-document references in
data_model_design_notes.md and revised_data_model.md: the block now owns a token
and follows the common empty-owner replacement rules. Include those small
documentation corrections in this change's review/commit scope. No production
correction was needed. Ritchie's own review and explicit commit instruction remain
outstanding; no commit or push has been performed.
