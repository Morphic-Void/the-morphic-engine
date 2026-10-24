Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   common-binary-block.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   24 Oct 2026

# Common binary schema data block

## Agreed contract and implementation scope

Recorded before implementation on 24 October 2026. Replace the existing binary
type, instance and bulk persistence forms outright with one `.bsd` format and
`CBakedDataBlock` owner. There is no legacy-reader compatibility requirement.
Keep the three specialised interfaces (`CBakedSchema`, `CBakedInstances`,
`CBakedBulkData`), external schema attachment and separate working binary owners.
The existing baked-document encoding/header, including reserved-zero words,
does not change.

The root is an object with required `fileType: "morphic-schema-data"` and unsigned
`version: 1`. Optional object sections are `types`, `instances` and `data`, in any
combination, including none. Optional unsigned `instancePayloadSize` and
`bulkPayloadSize` describe exact supplied binary lengths. Omission means unavailable;
zero means supplied and genuinely empty. A size requires its corresponding section;
an empty section cannot claim nonzero bytes. Optional Boolean `stripped` retains
its existing meaning for both data domains. Reject unknown root properties,
incorrect metadata types and unsupported versions. Accept any root-property order;
writers use consistent ordering and preserve meaningful section ordering.

The complete file consists of the baked document, optional instance binary and
optional bulk binary, in that order. The base and each nonempty binary are
128-byte aligned. Align the cursor only before a nonempty binary and require all
container padding to be zero. Absent and supplied-zero-length binaries consume no
bytes or padding. No final alignment tail is saved. Locators remain relative to
their respective binary. Preserve payload-internal padding under existing rules.
Validate checked arithmetic, exact file extent and the existing 2 GiB complete
storage ceiling. Extra bytes and truncation are errors.

Storage attachment validates the envelope, not typed semantics. The owner retains
local storage or borrows an asset range without acquiring an asset reference. It
exposes the document, independent presence/availability observations, both payload
views and the complete file range. It retains no schema resolution or data-role
bindings. Borrowers must finish before backing storage is replaced or released.
Existing schema-binding lifetime guards remain applicable.

Specialised interfaces can attach directly to the common representation. Existing
lower-level document and payload paths remain available. Creating missing binary
returns separate working ownership and never mutates or relocates the common
block. Explicit assembly publishes independent replacement storage; no implicit
rebinding occurs. A missing binary is not treated as supplied empty storage.

Assembly accepts a document and explicitly optional supplied binaries. It preserves
availability, writes fresh metadata and builds an envelope without materialising
values. This operation is schema-independent and does not promise typed validity.
Materialising bake instead resolves embedded types (the authority when present),
or uses an explicitly supplied external schema when types are absent. It validates
supplied binaries and constructs missing binaries for described domains. The
aggregate document produced by `compose_document` is a direct input: omitted
domains stay absent; explicit empty domains produce supplied-zero-length binaries.
There is no implicit merge of embedded and external definitions.

Both operations stage privately, require an empty destination and preserve sources
and destination on failure. Retain embedded/external/stripped output forms with one
choice for both domains initially. These control descriptions, independently of
binary availability. Stripping cannot discard the only constructible description:
construct a missing binary first or fail. Stripped/insufficient descriptions with
no supplied binary fail materialisation rather than inventing values.

Recognise this exact envelope in schema resolution and data preparation. Document
grammar validation alone does not validate external byte ranges. Preparation and
promotion select the correct domain/payload; merging and `compose_document` retain
their existing prepared-document contracts. Whole-payload reassembly preserves
locators, while existing domain packing assigns locators when repacking entries.
Reuse live bulk capture/packing so binary collections need no per-value nodes.

Implementation covers the common validator/layout/owner; migration of specialised
storage owners and callers; all three baked interfaces; aggregate materialisation
and explicit assembly; stripping, preparation, promotion and diagnostics; project
item files, examples and documentation. Test section and availability combinations,
aggregate baking, external schemas, zero-size types, binary preservation, lifetime
rules, malformed files, checked boundaries and allocation-failure transactionality.

Shader catalogue semantics, publication/failure policy, general multi-source binary
merging, compression, checksums, external references, selective type extraction and
zero-copy construction guarantees are outside this refactor. Work is authorised;
committing is not.

## Implementation and validation

Implemented on 24 October 2026. `CBakedDataBlock` replaces the separate instance
and bulk storage owners. The three specialised interfaces attach to common storage;
missing binaries remain separately owned until explicit replacement assembly.
Aggregate materialisation, supplied-byte preservation, stripped/retained conversion
and asset/bundle loading use the common representation. The persistence, runtime API
and asset documentation describe `.bsd`; no legacy format reader is retained.

The initial implementation builds and runs passed in Debug and Release for x64 and
Win32. Each run reported 94,491 schema checks and 1,396 bundle-request checks passed,
with no failures; all other suites passed. Coverage includes combined-block
save/load through both worker modes, the section/availability matrix, both alignment
gaps, checked extents and allocation-failure publication rules. Repository policy,
diff-whitespace and line-ending checks also passed. Changes remained uncommitted
pending user review at this checkpoint.

Review follow-up preserved preparation's `invalid_range` diagnostics for malformed
payload lengths and schema resolution's `missing_property` diagnostics for
incomplete envelopes, and corrected the stripping API documentation. Focused
regressions cover both live/baked representations, both size fields and data roles,
source locations and failure transactionality. The Debug x64 build and core run
passed with 95,253 schema checks and 1,396 bundle-request checks; all suites passed.
Policy, diff-whitespace and line-ending checks also passed. The earlier full
configuration matrix was not repeated for these diagnostic-only code changes.
