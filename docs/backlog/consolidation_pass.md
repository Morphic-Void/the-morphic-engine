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

Revisit what the parser checks, what it reports and which decisions belong to
the caller. This is a refactor of the existing parser, not a return to the
archived v1 model. The current semantic and physical baseline is documented
under [docs/data_model](../data_model/revised_data_model.md).

- Record relaxed syntax used, Morphic extensions encountered, numeric types
  and explicit positive signs, and relevant UTF findings. Include embedded
  NULs and Java-style modified/overlong UTF-8 NULs.
- Preserve the distinction between source observations, ingestion corrections,
  syntax observations and semantic interpretation. Define how information from
  the linter reaches the parser's result without losing source characteristics.
- Keep observation separate from policy. A caller supplies a strictness
  profile; the asynchronous parse operation applies it after parsing.
- Distinguish accepted input, policy rejection, parse failure and cancellation.
  On policy rejection, discard the parsed result unless an explicit diagnostic
  retention policy requests otherwise.
- Make detailed reports optional temporary diagnostic assets. A requester
  need not receive or retain them. Define useful location information,
  including line/column and hierarchy context, and statistics separately.
  Retain ingestion statistics such as line count and maximum line length,
  alongside what was corrected and how non-standard encodings were handled.
- Preserve the structural pass's purpose: determine well-formed syntax and
  parseable token spelling, assuming representable numbers, manageable name
  collisions and successful subsequent allocations. Structural success does
  not guarantee parsing success; its own scratch failure is a resource failure.

Before coding, settle the observation inventory, report shape, strictness
evaluation boundary, failure/partial-report semantics and examples of each
outcome. Define cancellation through the Host operation contract; do not infer
that a low-level parser already implements it. Review recovery and numeric
interpretation against this split without silently changing document meaning.

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
  types in named, anonymous, array, object and recovery contexts as needed.
  Include empty placeholders, numeric intent and boundaries, Unicode/NULs,
  reserved names, singleton objects and all recovery cardinalities/nesting.
- Bake twice independently and compare complete bytes before transferring
  ownership of one copy. Retain the other as the Executive's reference.
- Save and reload binary through the Host, returning the agreed identity and a
  checked borrowed view. Compare complete loaded bytes against the reference.
- Exercise JSON saving, loading, conditioning and policy acceptance; compare
  against the reference using the agreed normalized semantics, including
  recovery identity/order, singleton normalization and numeric intent/width.
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
