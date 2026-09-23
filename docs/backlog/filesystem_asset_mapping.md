# Filesystem image: limitations and future considerations

Updated 23 September 2026. The former deferred filesystem design is superseded
in full by the implemented [development filesystem image](../../development/README.md).
This file retains only limitations and considerations that may justify a later,
explicitly selected iteration. It is not an implementation plan or a requirement
to add extension points now.

## Implemented baseline

The Host owns a live document with named roots and nested `content` objects.
Its I/O worker constructs the initial image and per-root scan observations;
the Host integrates them. Logical paths resolve through configured bindings,
including the running executable's adjacent DLLs. Root permissions constrain
writes. Both log roots retain bindings without content inventory; `test-output:`
remains inventoried.

Loads check the inventory and can reuse retained assets. Successful saves update
inventoried destinations; disposal removes cache associations. Refresh requests
queue in FIFO order, failed scans leave the image unchanged, and reconciliation
preserves successful writes completed after scan dispatch. Workers borrow stable
Host-owned operation paths or scan backing, never nodes in the mutable image.

There is no outstanding requirement for shared immutable catalogues, atomic
image-pointer publication, reader entry/exit accounting or identifiers that retain
old images. Those proposals are retired, not deferred prerequisites. Any future
consumer must be designed against the current contract.

## Cached assets, options and replacement

A compatible cached TGA load reuses the already retained representation even
when the new request asks for different loading options. In that bounded sense,
the first cached representation wins. This concerns the loaded image asset,
not replacement of the filesystem-image document. Concurrent uncached loads are
not coalesced and may produce separate IDs; there is no guaranteed first-request
ordering between them.

A root refresh does not reread or invalidate an already cached representation
merely because the source bytes changed. Dispose and reload to obtain fresh
content. A successful Host save can update the file's cache association; it does
not dispose an older retained asset. JSON policy evidence and format compatibility
continue to follow the [asset contract](../assets/asynchronous_asset_services.md).

If an editor or another real consumer needs image-option variants, explicit
reload/replacement or hot content updates, decide:

- whether replacement is explicit, variant-keyed or versioned;
- how existing asset IDs and borrowed views remain valid or are quiesced;
- how overlapping loads, refreshes and saves select the resulting association;
- whether concurrent equivalent loads should share one in-flight operation.

No automatic cache eviction, reference counting or shared-consumer disposal
protocol is implemented. Current disposal assumes other borrowers are quiesced.
Broader ownership must be selected before treating repeated cache hits as
independent retained uses.

## Discovery and consumer access

The current operation is a queued refresh of one root. There is no filesystem
watcher, content hashing, source-version tracking or stable identity across
renames. Reads are not byte snapshots of discovery: a file can change or disappear
between its scan and the native read, which can then fail normally.

Later game, editor or save-browser consumers may need finer refresh scope,
filtering, metadata extraction or explicit choices about adopting changed content.
Discovery does not validate payloads or choose an active game configuration.
There is no implemented policy restricting every game refresh to saved-game
lists or requiring restart for every content change.

Only the Host accesses the live image. If another thread needs a directory tree
or bulk query, define its response ownership and consistency at that time;
do not expose mutable document pointers by default. Document-local keys are not
durable file identities. General live/baked-document path navigation and
hierarchy-aware dependency resolution remain separate possible consumer work.

Ordinary Host loads require inventory membership. A future explicit-location
import or load would need its own authority and lifetime contract; the availability
of low-level file primitives is not an existing Host bypass.

## Deployment and platform bindings

The checked-in development manifest and directory layout are implemented, not
the final deployment arrangement. Installed/read-only content, per-user writable
storage, packaged assets and platform APIs still need bindings when deployment
requires them. Android, including Quest/Pico-class devices, is not validated;
UGC is not currently planned for Android.

Windows Debug x64 has been exercised for this stage. Linux directory/file access
has a basic implementation but has not been exercised here. Non-Windows module
discovery and final bootstrap placement remain unimplemented. Do not infer
cross-platform validation from a shared API shape.

The existing native-path restrictions remain intentional. The executable query's
1 KiB typed stack buffer is bounded and has no growth fallback; it does not
promise general extended-path support. Logical-path checks are a development
resolver, not a security sandbox. Revisit native containment, aliases and
platform restrictions only with a concrete requirement and targeted validation.

Directory traversal is depth-bounded, lookups are not tuned for large libraries,
and refresh integration clones the current image through bake/promote. Optimise
these only when real content sizes or measured latency justify it.

## UGC, configuration and composition

The local UGC roots provide storage and discovery locations, not provider
integration, validation/promotion workflows or distribution. mod.io is the likely
provider; Steam Workshop and Epic EOS remain possibilities, not selected APIs.
Developer content production and UGC may share local tooling without sharing
remote distribution or provenance.

Final core-content selection may use a distributed configuration. The current
root manifest defines bindings and inventory policy; it does not settle that
content-selection or distribution contract.

Overrides, patches and document layers remain possible later work. Discovery
alone grants neither permission to replace core content nor trust in its payload.
When composition becomes necessary, specify eligibility, conflicts, ordering,
source provenance and cache invalidation together. A layered document's semantics
must not be inferred from parser duplicate-property handling.

Developer responsibility and ordinary payload/module validation remain the
foundation. An autonomous trust scorer, first-use journal or recovery-selection
engine is not required by this filesystem implementation. Any later diagnostic
or recovery feature needs a concrete purpose rather than revival of the retired
filesystem plan.
