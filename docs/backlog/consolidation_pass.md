# Consolidation before schema work

Updated 11 September 2026. Active direction; detailed interfaces remain under
discussion. This is the consolidated plan for parser refactoring and the Host
prerequisites to schema work.

The first data-model implementation supplies live construction, immutable
baking, promotion, writing, parsing and an initial baked-block ownership
bridge. It exposed gaps in Host authority, lifetime, asynchronous operations
and parser reporting. Resolve those contracts before adding schema consumers.
The current code is a baseline to refactor, not an approved final interface.

## Work and review boundaries

- Refactor the parser/reporting in the existing parser task. Agree the revised
  contract and examples before implementation, then keep changes reviewable.
- Carry most other consolidation into a separate task, using this document as
  the handoff. Establish Host authority and lifetime before dependent services.
- Add direct persistence checks and the full Executive exercise against those
  services. The former test-specific message sequence is superseded as an
  implementation plan; retain the coverage goals below.
- Begin a schema vertical slice only when these prerequisites are usable.
  Pause before each commit for review.

## Parser observations and caller policy

The [linter/parser refactoring specification](parser_refactoring_specification.md)
owns the settled requirements and implementation-review details. Its consistency
review is complete; execution requires Ritchie's explicit instruction.

The agreed direction is caller-selected feature permissions, grouped parser
presence flags instead of statistics, retained linter aggregate statistics,
construction capacity estimates retained separately where useful, distinct
processing and policy outcomes, and shared 1-based line/code-point locations in
the linter's output. All supported
line-break forms are normalized to LF. The linter has no JSON or string awareness;
escaping belongs to the parser/writer within strings and names. Raw quoted line
breaks are accepted string content and reported as a feature. The writer escapes
newlines by default, with per-string metadata to suppress that escaping. The
parser sets this metadata automatically on strings containing at least one literal
source line break; escaped-only line endings do not trigger suppression.
Preserve the metadata through live/baked document transformations. It belongs to
each value occurrence, independently of shared interned text. Suppression covers
all newline forms, and
the writer normalizes every newline to LF before emitting literal or escaped
output. Strict JSON overrides suppression and escapes embedded LF as `\n`,
without modifying the document's metadata. Reparsing strict output derives
suppression as unset from its escaped-only spelling. Structural punctuation
outside strings/names must not be escaped. Names cannot contain newlines in text,
direct live entry or checked baked storage.
Structural errors identify both the beginning of the immediately malformed
element and its failure point. Linter errors identify the prospective line and
code-point column that could not be decoded. Linter failures
retain a location after producing code points, even if output is discarded;
failure before any output is identified explicitly. Both stages use the same
location representation. On linter failure, the parser report copies that
location into its failure-point field and leaves structure start unavailable.
Feature-policy rejection
occurs only after parsing has otherwise succeeded, so structural and other
processing failures take precedence. Undefined CP1252 bytes cause linter failure
without replacement.

Preserve the established logical-NUL storage form, exact Java-style `C0 80`,
without surrogate substitutes. The linter uses SuiteUTF to normalize valid
CESU-8 supplementary pairs to standard UTF-8, retaining source findings. CESU
bytes are not supported for direct document entry or output. Preserve NUL
admission without adding a generic raw-control permission requirement. Default
acceptance is conservative, allowing
Morphic hexadecimal, binary and explicit-positive numeric forms but excluding
relaxed syntax. CP1252 and the supported modified-UTF-8 exception are accepted by
default; undefined CP1252 still fails. Programmatic documents default to an object
root; changing root type in either direction requires an empty root. Detached
nodes and prepopulated strings do not count as root contents. Comment-only input can
produce an empty object, subject to comment acceptance policy. Empty/whitespace-only
input produces an empty object. Root erasure preserves its type; reset restores
an object root.

Prepopulate live name tables with `$morphic-empty`, the only required canonical
identifier, at a fixed live name ID. Baking may omit it when unreferenced.
Replace the distinct recovered-array representation with ordinary arrays and a separate
public collision-extension operation available to the parser and ordinary users.
Normal insertion still rejects collisions. Array/array collisions create a new
array containing both arrays; incoming non-array values append to the top-level
array. Parser collision extension is a relaxed feature. Retire all old recovery
compatibility and replace its tests; no external consumers depend on it.

The current baseline remains under
[docs/data_model](../data_model/revised_data_model.md). Host cancellation and
optional retention of rejected diagnostic assets belong to the asynchronous
operation contract. They are not prerequisites to drafting or implementing the
low-level parser/report refactor, and no automatic logging is implied.

## Host worker authority and module lifecycle

- Move DLL loading, binding, initialisation and unloading to the Host worker.
- Define Executive requests for module operations. Executive threads must not
  directly mutate module bindings or Host collections.
- Make the Host worker the authority performing state changes; settle how the
  main Host coordinates requests and receives results.
- Define safe unload: reject new module work, cancel or drain associated
  operations, reclaim module-scoped assets, unregister bindings/types, then
  unload the DLL.

## Asset identity, lifetime and type erasure

- Distinguish Host-local storage IDs from SYSTEM-level identities usable
  across requests, reports, caches and remapping. Decide the identity returned
  for worker-to-Host and module-to-Host publication; the current monotonic
  CAssetId storage handle alone does not settle that contract.
- Define temporary, module-scoped, Host-durable and cache-backed/evictable
  lifetimes and the metadata accompanying publication and retention intent.
- Define how concrete assets enter, are retrieved from and are reclaimed by
  type-erased Host collections, including permissions and outstanding users.
  Settle permission revocation, retiring-resource handling, long-term shared
  backing and transport ownership as part of that authority boundary.
- An asset surviving its originating module must retain no dependency on that
  module's code, type metadata, destructors or allocation context.
- Revisit the public/private split and friendship in memory attribution.
  Keep transfer mechanics private, expose semantic lifetime intent publicly,
  and give narrowly scoped access to erased carriers and necessary Host internals.
  The initial BakedDocumentAsset bridge is implemented; its API is under review.
- Keep mounting-point hazards separate from identity, ownership, permission
  and provisioning metadata.

## Asynchronous operations, loading and conditioning

Define request and operation IDs, states, cancellation, completion outcomes,
optional diagnostic reports/statistics and ownership of published results.
Include dispatch rejection, worker failure and shutdown cleanup.

File loading must carry caller-supplied alignment through the asynchronous
request and platform::filesystem::loadFile, retaining a 16-byte floor. Use
the effective alignment for allocation and capacity rounding, preserve existing
padding semantics, and cover larger alignments and failure handling. Baked
binary views currently require a 32-byte-aligned base.

JSON parsing and baking run in CHostWorkerThread using
thread_ids::bg_conditioning. Keep the live document and scratch on that
thread and retain the loaded text until conditioning completes. Publish only
the completed owned result accepted by the operation's policy. File I/O remains
a separate workload from conditioning.

Resolve the save/load API before encoding it in the functional test:

- Can saving name an existing Host asset, transfer a new asset, or support both?
- What lifetime is requested for a newly supplied block after saving, including
  failure or cancellation: disposal, temporary retention or durable publication?
- How are binary output, JSON output and JSON writer options specified?
- Are multiple outputs serial operations or one combined instruction, and how
  are partial success and result ownership reported?
- Where does JSON writing execute under the service contract? The earlier
  proposal put writing in the Executive; do not treat that proposed test
  arrangement as a settled restriction on the consolidated save service.

## File association, discovery and resolution

- Associate an externally supplied source key or path with an identified asset.
  Keep file identity distinct from a loaded buffer or immutable content version.
  Do not assume all source keys were discovered by a scan.
- Add platform-agnostic directory scanning backed by platform implementations.
- Define configured storage roots, startup scans and filename/source-key indexes,
  plus explicit or incremental rescans.
- Define relative-to-requester resolution, canonical logical keys, search
  precedence and duplicate/shadowing behaviour across roots.
- Permit direct user-specified locations to bypass discovery while still using
  the normal Host identity, lifetime and reporting rules after loading.

Schemas and their C/C++ source/header inputs require hierarchy-aware dependency
resolution, so this service precedes schema work. A complete cache/remapping
implementation is deferred beyond the resolution contract needed initially.

## Persistence and Executive validation

The Executive exercise should validate the intended Host and worker services.
Settle the operation contracts first, then define messages and reviewable test
slices. Earlier plans assumed fixed save/load messages and repository IDs;
those mechanics must be reconciled with the consolidation decisions above.

Retain these coverage goals:

- Construct a live fixture with every supported node/payload type, repeating
  types in named, anonymous, array and object contexts as needed.
  Include empty placeholders, numeric intent and boundaries, Unicode/NULs,
  `$morphic-empty`, singleton objects and collision-extension array shapes.
- Bake twice independently and compare complete bytes before transferring
  ownership of one copy. Retain the other as the Executive's reference.
- Save and reload binary through the Host, returning the agreed identity and a
  checked borrowed view. Compare complete loaded bytes against the reference.
- Exercise JSON saving, loading, conditioning and policy acceptance; compare
  against the reference using the agreed normalized semantics, including
  collision-array shape/order, singleton normalization and numeric intent/width.
  Account explicitly for strict-output numeric normalization where exercised.
- Cover direct file round trips before relying on the full asynchronous run.
  A loaded binary can remain owned by LoadedFile with a checked baked view;
  an additional baked-block adoption API is not inherently required.
- Correlate each completion and advance only when the preceding operation's
  result permits it. Exercise failures without losing the original failure.
- The Executive relinquishes borrowed views and requests disposal of every
  asset from the full flow that remains Host-owned: transferred originals,
  loaded copies, output text and retained requests/intermediates, including
  internally tracked assets whose IDs were never returned to the Executive.
- Wait for outstanding I/O and view users before disposal, acknowledge cleanup,
  and verify stale IDs no longer resolve and test allocations are released.
  Require explicit cleanup before success; shutdown cleanup is a fallback.
  Release the Executive reference before DLL shutdown and verify any surviving
  SYSTEM payload can be destroyed without Executive code.

## Order beyond this task

Host authority and module lifecycle enable asset identity/lifetime and standard
asynchronous operations. Parser observations and policy can be developed
alongside those contracts; their integration enables accepted-result
publication. Aligned loading, conditioning and filesystem resolution then
support meaningful persistence/integration tests and a first schema consumer.

The first schema slice should resolve a schema/source and dependencies through
the Host filesystem service, parse under a caller's strictness profile, and
publish an accepted result safe for its requested lifetime. Broader C/C++
parsing, generation, serialisation and remapping follow that slice.
