# Consolidation Pass Before Schema Work

Status: proposed prerequisite work for the next data-model stage.

The first stage of the data-model work is substantially complete. The anticipated schema work—representing C-like structures in JSON, then adding C/C++ parsing, code generation, serialisation, and remapping—exposes host ownership, asynchronous-operation, module-lifetime, and filesystem-resolution requirements that need to be established first.

This is an enabling phase, not a diversion from schema work. Its purpose is to make the schema system a consumer of explicit host contracts rather than a source of ad-hoc infrastructure.

## Consolidation Scope

### 1. Host worker ownership and module lifecycle

- Move DLL/module loading, binding, initialisation, and unloading to the host worker thread.
- Define executive-thread requests for module operations; executive threads must not directly mutate module bindings or host collections.
- Define a safe unload sequence: reject new module work, cancel or drain module-associated operations, reclaim module-scoped assets, unregister bindings and types, then unload the DLL.

### 2. Identified host-owned assets and lifetime

- Define the distinction between host-local collection/storage IDs and system IDs that can be named across requests, reports, caches, and remapping.
- Define publication and retention intent, including temporary, module-scoped, host-durable, and cache-backed or evictable assets.
- Ensure that an asset surviving its originating module has no remaining dependency on that module's code, type metadata, destructors, or allocation context.

### 3. Restricted memory-context and ownership transfer

- Keep memory-context transfer mechanics private implementation machinery.
- Expose semantic lifetime intent publicly, not allocation-transfer details.
- Give narrowly scoped access to the erased carrier and required host internals instead of widening public APIs or accumulating broad friendship.

### 4. Type-erased host collection

- Establish how concrete assets enter, live in, are retrieved from, and are reclaimed by host-owned collections.
- Define the limited type and lifecycle contract that remains valid after a module unload.

### 5. Standard asynchronous host operations

- Define request and operation IDs, operation states, cancellation, completion outcomes, and result publication/ownership transfer.
- Standardise optional diagnostic reports and statistics in operation results.
- Keep the host worker as the authority which performs state changes.

### 6. Parser observations and caller strictness policy

- Extend data-model parser reporting to record relaxed syntax that was used, Morphic extensions encountered (including numeric types and explicit positive signs), and UTF findings such as embedded NULs and Java-style modified/overlong UTF-8 NULs.
- Keep observation separate from policy: a caller supplies a strictness profile and the asynchronous parse operation applies it after parsing.
- Distinguish accepted input, policy rejection, parse failure, and cancellation. On policy rejection, discard the parsed result unless a diagnostic-retention policy says otherwise.
- Treat detailed reports as optional temporary diagnostic assets; callers need not receive or retain them by default.

### 7. File-backed asset association

- Permit an externally supplied source key or path to be associated with an identified host asset.
- Keep file identity distinct from a specific loaded buffer or immutable content version.
- Do not yet assume every source key is known to the host or discovered by a scan.

### 8. Filesystem discovery and resolution

Filesystem discovery is a prerequisite for schema work because schemas and the C/C++ source/header inputs they exercise will need hierarchy-aware dependencies and filesystem search.

- Define configured storage roots and startup scanning to populate source-key or filename lookup indexes.
- Support later explicit or incremental rescans.
- Define relative-to-requester resolution, canonical logical keys, search precedence, and duplicate/shadowing behaviour across roots.
- Support directly user-specified locations which bypass ordinary discovery or lookup, while still following the normal host identity, lifetime, and reporting rules after loading.

## Intended Order

```text
host worker and module lifecycle
  -> asset identity, type erasure, lifetime, and async contracts
  -> parser reporting and strictness policy
  -> filesystem discovery and dependency resolution
  -> schema vertical slice
  -> broader C/C++ parsing, generation, serialisation, and remapping
```

The first schema vertical slice should resolve a schema/source and its dependencies through the host filesystem service, parse under a caller-supplied strictness profile, and publish only an accepted result that is safe for the requested lifetime.

## Explicitly Deferred

- A complete filesystem cache/remapping implementation beyond the resolution contract needed by schema work.
- The full C/C++ parser, generator, serialisation, and remapping pipeline.
- A requirement for every parse requester to retain detailed parser reports.

## Ritchie's notes

- I'm not comfortable with the current friend usage and public/private split in the memory attribution.
- We need a more defined transfer of data from modules and threads to the host.
- The file loader needs to be able to specify alignment above 16 bytes.
- We need to add platform agnostic (probably backed by per-platform code) directory scanning.
- The transfer of buffer ownership from the host worker to the host should probably use a SYSTEM level ID
- and be usable for transfers from modules to the host.
- The parser and parser report don't fit my vision of what these should be checking for and reporting.
- We need to add the lifetime and ownership metadata to ownership passed to the host.
- The current plan for the executive driven data model load/save flow and test is inappropiate
- it should become a test of closer to final flows through the host and host worker.
- We need to determine how to handle the loading and saving of assets more clearly, for instance:
- - When saving a baked document, we need to be able to specify either an existing host owned
  - baked block or that we are passing a new one. Whether a new passed block should be discarded
  - after the operationm whether we want to save as JSON and if so with what options and whether
  - we want to save the binary. It's unclear if these should be serial messages or a combined instruction.
