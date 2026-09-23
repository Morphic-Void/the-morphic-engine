# Development filesystem roots

`development/logical-roots` is the first directory-backed implementation of the
runtime logical-root model.  It deliberately makes every currently supported
development root visible in one place while the engine loads ordinary Windows
or Linux files.

The root names are logical-root identifiers, not a proposal for the shipping
filesystem layout.  The later platform resolver will bind the same identifiers
to installed content, per-user data, Android assets or other appropriate
backends.

The image implementation lives in `core/filesystem/filesystem_image.hpp/.cpp`,
above the native operations in `core/platform/filesystem/`. The Host owns the
live image and its scheduling; ownership does not make the reusable image code
part of the platform adapter.

## Roots

| Directory | Logical root | Initial purpose | Discovery | Git policy |
| --- | --- | --- | --- | --- |
| `package/` | `package:` | Files that model immutable core packaged content | startup/on demand | tracked |
| `dev-source/` | `dev-source:` | Development source material eligible for the ordinary development content pipeline | startup/on demand | tracked |
| `dev-workspace/` | `dev-workspace:` | Editable source assets and projects not yet promoted to packaged content | startup/on demand | tracked |
| `config/` | `config:` | Local development settings | startup/on demand | ignored |
| `saves/` | `save:` | Save slots and their metadata | startup/on demand | ignored |
| `state/` | `state:` | Crash reports and machine-specific runtime state | startup/on demand | ignored |
| `logs/` | `logs:` | Runtime and development-tool diagnostics | no content inventory | ignored |
| `test-logs/` | `test-logs:` | Test-infrastructure diagnostic output | no content inventory | ignored |
| `test-output/` | `test-output:` | Generated test files, manifests and other non-log test artefacts | startup/on demand | ignored |
| `cache/` | `cache:` | Rebuildable local data | startup/on demand | ignored |
| `ugc-work/` | `ugc-work:` | Editable local UGC projects | startup/on demand | ignored |
| `ugc-inbox/` | `ugc-inbox:` | Imported/downloaded UGC awaiting validation | startup/on demand | ignored |
| `ugc-installed/` | `ugc-installed:` | Validated local UGC available for selection on a later launch | startup/on demand | ignored |

`ugc-provider:` is intentionally absent from this local shape.  It will be a
read-only provider adapter rather than a directory owned by this repository.
Android does not bind any `ugc-*` root until Android UGC support is explicitly
introduced.

## First-pass catalogue

The Host file I/O worker reads `development/root-manifest.json` and scans the
inventoried roots before Executive bootstrap. Launch from the repository
directory for this development profile. Schema version 2 uses a `roots` object
keyed by logical-root name. Each root has a `source` relative to the manifest's
directory and a `writable` permission. Inventory defaults to enabled;
`inventory: false` retains the binding but skips directory queries entirely.
An initial inventory scan failure prevents bootstrap; an empty directory is
successful. `.gitkeep` is ignored. Discovery never follows symlinks/reparse points.

The resulting `CLiveDocument` is transferred to the Host and thereafter accessed
only by the Host. It mirrors the directory hierarchy using named objects:

```json
{
  "roots": {
    "dev-source:": {
      "source": "development/logical-roots/dev-source",
      "writable": true,
      "content": {
        "textures": { "content": { "stone.tga": {} } },
        "test_input.tga": { "asset": 42 }
      }
    },
    "logs:": {
      "source": "development/logical-roots/logs",
      "writable": true,
      "inventory": false
    }
  }
}
```

An uncached file is `{}`; a directory always has `content`, even when empty.
Other filesystem objects have `kind: "other"` and cannot be loaded or written
through this resolver. Filesystem names occur only as keys within `content`,
so names such as `source`, `asset` and `content` cannot collide with metadata.
Bindings and write permissions are inherited from the nearest ancestor, with
explicit overrides only where needed. All stored physical paths use forward
slashes, including the resolved executable directory. Native conversion remains
at the existing platform boundary.

`writable` describes configured permission, not a guarantee that the OS will
accept a write. `asset`, present only for a cached file, is a process-local
retained asset ID. Format, JSON parser findings and write/refresh serials are
private Host runtime bookkeeping, not repeated on every discovered file.

Requests use names such as `dev-source:/test_input.tga`, `save:/slot.json` and
`test-output:/result.bin`. Logical names are case-sensitive and use forward
slashes. Absolute paths, backslashes, empty components, `.`/`..`, wildcards and
alternate-stream colons are rejected. Discovery rejects ASCII case aliases
and redirect collisions, rather than choosing an arbitrary file. This is a
development resolver, not a security sandbox or full native-path canonicalizer.

Loads must resolve an inventoried file before any I/O. Writes within inventoried
roots require an existing discovered writable parent directory; a new filename
is allowed, but implicit directory creation is not. Non-inventoried roots are
write-only through this interface: validated relative paths resolve beneath the
configured source, and native I/O checks whether their parent exists. Successful
writes never add entries to those roots; refresh acknowledges the binding without
scanning its contents. `test-output:` remains inventoried, including ordinary
readback and cache associations. Native load/save primitives remain available
for low-level tests and bootstrap diagnostics.

## Build-output redirect

The checked-in manifest places the `bin` binding under `package:`'s `content`,
with `source: "executable-directory"`, `extensions: ["dll"]` and
`writable: false`. The image resolves that source once at the directory level;
DLL entries do not repeat its path. No build-generated path or binary copy is
needed. The worker queries the actual running process image location, so Debug/Release,
x64/Win32 and isolated lifecycle-fixture directories select their own adjacent
DLLs. All adjacent DLLs, including test-only DLLs, are discovered; EXEs, PDBs
and import libraries are not. This discovery does not load the DLLs.
The executable-location query uses 1 KiB of typed stack storage, accepting a
full executable filename of at most 511 UTF-16 code units on Windows or 1,023
bytes on Linux. Truncation fails the query; there is no growth fallback. This
bound does not expand or replace the existing `makeNativePath()` restrictions.
Existing physical `package/bin` content can coexist if names do not collide;
those exceptional branches retain explicit physical source overrides beneath
the read-only `bin` binding.

`ModuleRequest` and `--executive=` use logical DLL names, for example
`package:/bin/MorphicExecutive.dll`. The Host resolves these into stable native
filenames before worker binding. Unknown modules fail without a native load
attempt. A replacement still unloads the old binding under the existing module
lifecycle contract. Module binding ownership remains in the module service,
separate from ordinary retained asset IDs. The DLL filter is deliberately the
current Windows profile; Linux shared-library deployment is not introduced here.

## Refresh, writes and cached resources

The Executive sends an owning `FilesystemRefreshRequest` whose `root` is one
logical root, including the colon (for example `dev-source:`). The Host queues
up to 16 requests, including the active request, in FIFO order without coalescing.
It dispatches one scan at a time. A correlated POD `FilesystemRefreshResult`
reports success only after integration, or invalid root, queue full, scan failure,
integration failure or delivery failure. Successful queued refreshes retain order;
an immediately rejected full-queue request may reply before earlier work completes.

Each scan uses immutable Host-owned backing, borrowed by the worker. Each file
operation likewise owns its resolved filename in address-stable Host storage
until completion. Workers never borrow strings or descriptions from the mutable
image, and no owning file-description copy is sent to the worker.

The worker builds its observation privately. The Host integrates it into a
private replacement document and publishes that document only on success.
This first pass uses bake/promote to clone the current image; lookup and merging
are linear and have not been optimized for large content libraries. Failed or
partial scans leave the current image unchanged. Removed files disappear from
the image, while their retained assets remain alive until disposal. Write
serials preserve successful writes completed after scan dispatch, and prevent an
older in-flight load from replacing a newer write's cache association.

Successful loads reuse a compatible cached ID and views. JSON remains baked;
its recorded findings are checked against the new caller's parse policy. The
existing format distinction is retained privately, including raw/mismatched-format
test requests; this is not a general multi-variant cache. TGA loading options are
deliberately not cache keys: subsequent compatible requests reuse the first
cached representation. Concurrent uncached loads are not deduplicated and can
produce separate IDs; this is not a first-request ordering guarantee. External
content changes do not invalidate an already cached representation during refresh;
dispose and reload to reread it.

Ownership-only transfers do not add a file association. Only a successful write
updates an inventoried destination. Discard saves clear that file's cached
association without disposing an older retained asset. Live-source saves do not advertise the live
document as the baked file cache. A JSON save records the source association but
is reparsed on its first subsequent load, since serialization can change the
representation and source-policy evidence. Disposal clears all matching file
associations by removing `asset`; it does not remove the discovered file.
Shared-consumer ownership and cache-variant lifetimes remain deferred.

Test logs belong beneath `test-logs/`; all other test artefacts belong beneath
`test-output/`.  The test runner uses process-ID and optional tag-qualified
filenames in these roots. Its existing `--output-directory` override puts
non-log files directly in that directory and logs in its `logs` child.
The Executive acceptance exercise and DLL lifecycle fixtures currently use
fixed non-log output filenames and must run serially.

The shared input fixture lives at `dev-source/test_input.tga`. Host diagnostics
use `logs/`, policy-validator reports use `logs/policy_validator/`, and the DLL
lifecycle harness redirects Host diagnostics into `test-logs/`. Diagnostic sinks
and test-only fixture setup still use physical paths. External writes to
inventoried roots become visible at the next explicit root refresh; there is no
watcher. Neither diagnostic root ever contributes content entries. Runtime asset
requests use the logical resolver and update inventoried destinations on success.

The core filesystem-image suite generates a process/tag-qualified
`filesystem-image.*.json` beneath `test-output/` for inspection. It exercises
discovery, DLL filtering/collisions, write permissions, refresh reconciliation,
failed scans and cache metadata. The Executive exercise adds queued refreshes,
concurrent access, cache reuse, disposal/reload and read-only rejection checks.
This stage has been validated on Windows Debug x64, not Linux or Android and
not a new full Debug/Release x64/Win32 matrix. The directory query has a basic
POSIX branch, at the same development scope as the existing file loading/saving.

## Deliberate limitations

The current cache does not provide image-option variants, explicit hot replacement,
automatic invalidation on external byte changes, shared-consumer retention or
automatic eviction. Root refresh reconciles inventory; it does not replace
existing borrowed asset views with newly loaded content. Watching, source-version
tracking and general editor/bulk-query APIs are not implemented.

Final deployment bindings, Android storage and UGC providers remain separate
future work. The native-path policy is unchanged and is not a security sandbox.
See [filesystem limitations and future considerations](../docs/backlog/filesystem_asset_mapping.md)
for the useful remaining cases. The old deferred filesystem design is superseded,
not a second implementation plan to complete.
