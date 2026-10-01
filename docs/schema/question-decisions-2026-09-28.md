# Schema question decisions — 28 September 2026

Working record of the user answers following the
[pre-refactoring review, now consolidated](implementation-contract.md). This records decisions for
subsequent reconciliation with [design.md](design.md), the
[implementation contract](implementation-contract.md), and the
[runtime API](runtime-api.md). It does not authorise implementation or a commit.

## Wrapper and schema lifetimes

- Schema, instance, and bulk data are separate logical documents. Failure to
  load an instance or bulk document is fatal to that logical document, not to
  its resolved schema, even when views share a physical baked block.
- Adding new data is an isolated operation. If an append fails, existing entries
  remain usable. Bytes already appended may remain unreferenced, provided no
  locator for the failed entry is published.
- A schema is supplied explicitly when an instance or bulk document is created,
  promoted, or demoted. Conversion does not inherit the source's schema
  reference. Normal schema-matching rules determine success or failure during
  the operation; there is no separate pre-validation pass.
- Documents reference their schema through inter-document reference counts.
  Referencing documents do not own or extend the schema wrapper's lifetime.
  Attempting to mutate a referenced live schema fails without changes and
  triggers an assertion. A referenced schema cannot be moved. Attempting to
  destroy one raises a critical error and must be stopped before its backing
  data is released; the system should immediately attempt shutdown.
- Re-resolution of unchanged definitions is permitted while documents refer to
  the schema. Successful re-resolution gives the same data and preserves the
  meanings of existing unversioned handles. A failed re-resolution clears the
  resolution; preserving its old tables through allocation failure is not
  required. Access is single-threaded even within a multi-threaded system.
- Demotion returns the baked wrapper and its owned baked document block
  separately. The caller manages their joint lifetime and may transfer block
  ownership. Demoting a schema does not resolve the new baked wrapper; the
  caller may only want to save its block.

## Queries, names, and diagnostics

- Public document handles have three role types: schema, instance, and bulk.
  The live and baked wrapper for a role use the same public handle type, though
  its internal interpretation may differ. Handles carry no individual document
  identity. Using one with another document, including the other representation,
  is caller error.
- Handles in a live wrapper survive mutation when the underlying live
  data-model handle survives it. Borrowed name, string, and other document
  views are invalid after any mutation of their document. Binary views across
  buffer mutation or growth are the caller's responsibility; no held-view
  tracking is added.
- Resolved-schema names are borrowed from the schema document, never copied
  into the resolution. A live schema wrapper owns its underlying live document.
  A baked schema borrows string storage whose lifetime its user manages.
- A live schema may temporarily contain an empty type during construction.
  Empty types have no resolution, are skipped without failing document-level
  resolution, and cannot be serialised. Complete types in the same schema can
  resolve. Construction should prevent a complete type from referring to an
  empty type; if such a reference nevertheless occurs, the empty reference is
  ignored rather than failing resolution.
- Malformed schema validation or resolution fails with useful information for
  human review, comparable in purpose to a data-model parsing report and
  suitable for `MV_REPORT` or similar logging. A node handle may be used
  internally to build a name or path; no direct recovery through a public
  diagnostic handle is required. Hand-authored baked input is the main expected
  source of these failures because live schema editing is mediated.

## Locators, loading, and binary buffers

- Instance and specialisation objects gain a sibling `locator` object beside
  their existing contents. Each named bulk entry becomes an object with
  `locator` and `data`; `data` holds its record array when present.
- A locator has a zero-based unsigned 32-bit `offset` into the one buffer
  associated with its logical instance or bulk document, and a separate
  Boolean `valid` flag, for example
  `"locator": { "offset": 0, "valid": true }`.
- `locator.size` is optional for both roles. When present it equals resolved
  type size, including alignment padding, multiplied by one for an instance or
  by record count for bulk. Record stride equals resolved type size.
- `locator.count` is optional for instances, implies one when absent, and is
  stripped from live instance documents. A baked instance locator may contain
  it only with value one. Bulk count is optional to support manual editing; if
  the embedded `data` array is absent, at least one of `count` or `size` is
  required. Present count and size values are checked against the resolved type
  and, when available, the embedded record array. Clarified on 1 October: a
  zero-byte resolved type requires an explicit positive `count` when embedded
  records are absent; `size: 0` alone is insufficient. An embedded array supplies
  its record count even when the records occupy no bytes.
- The baked format must reserve the appropriate locator nodes and validity
  flag so loading need not insert fields. With a supplied byte buffer, baked
  offsets must already be valid; the baked block may be immutable. Without a
  supplied buffer, embedded values must be present. The loader creates the
  binary buffer using existing valid offsets, or updates reserved offsets
  through the mutable baked interface if permitted. Existing offsets are
  validated against the embedded values. A block that needs offset updates
  but is not writable fails to load.
- Adding new instance or bulk data appends to the working buffer. Each new
  extent begins at an offset aligned for its resolved type. A larger bulk
  replacement appends a new extent and leaves the old bytes unreferenced;
  same-size or smaller replacements may retain their offset. The working
  buffer does not compact.
- Live buffer offsets need not follow document order. Baking creates a new
  buffer in document traversal order and writes matching baked locators; a
  baked document's buffer extents follow document order.
- Valid locator extents within one document's buffer must not overlap.
- A named bulk entry with zero records may exist temporarily in a live
  document while it is being populated, but is invalid in a baked document.
- Associated instance and bulk binary buffers are allocated with 128-byte alignment.
  A caller-supplied binary buffer whose base is not 128-byte aligned is
  rejected rather than copied into aligned storage.
  Their stored byte order is fixed little-endian. The caller supplies the
  associated binary file when creating/loading the document; schema/layout
  identity in the file is beyond this work. File matching is manual for now.

## Capture, specialisations, and output

- Clarified by the user on 30 September: a specialisation is an independent
  alternative, not an operation replacing or modifying its base. Positional
  declarations select their supplied prefix and omitted positions inherit.
  Thus base `[2, 1, 4, 7]` with alternative declaration `[2, 1, 4]` produces
  `[2, 1, 4, 7]` for the alternative as well. This supersedes the earlier
  positional-replacement/default-tail rule for specialisations. The rule
  applies recursively, like named selection; base construction still uses
  defaults for omitted input and embedded bulk records must still be complete.
  The base is the immediate parent's fully realised description, incorporating
  the entire chain of earlier specialisations; inherited values do not revert
  to the original instance or schema defaults.
- A specialisation is an independently selectable alternative instance. Its
  document declaration retains its parent relationship and only its selected
  modifications; its binary image is complete.
- Capturing a complete binary value for an existing specialisation takes only
  the parts selected by its retained modification declaration. The resulting
  complete binary image changes those parts, and descendant specialisations
  are updated recursively. Editing a base instance likewise updates all
  descendant specialisations in the document tree, parent before child. The
  recursion may use an internal helper but is part of the user-facing edit
  operation, not a separate caller step.
- Removing a selected specialisation member rebuilds that image from its base
  and updates descendants. Adding a selected member supplies a replacement
  value and also updates descendants. Callers access underlying live documents
  only through the wrappers so these updates remain coordinated.
- Instance and specialisation output retain both declaration information and
  complete saved values, including distribution output. Bulk embedded `data`
  arrays may be retained for review or omitted when binary backing exists.
  Either form remains fully editable after promotion to a live wrapper.
- Text output with embedded instance or bulk values still includes locator
  objects. Baking a document loaded from that text must have the reserved
  locator nodes available for offsets created while loading embedded values.
- When both embedded and binary representations are loaded, comparison is an
  optional validation path for both instance and bulk documents. The
  comparison uses destination-encoded field values, ignoring padding and unused
  bits. Clarified on 1 October: encoded values compare exactly (including signed
  zero and distinct SNORM codes), except that all NaN encodings compare equal;
  document text cannot express NaN payloads. No numeric tolerance is applied.
  Distributed data can skip it for speed. With validation disabled, existing binary
  snapshots are authoritative until an edit; load does not rebuild them from
  declarations.

## Value codecs, explicit layouts, and remapping

- Revised by the user on 30 September: the original `floor(f * 2^n)` UNORM
  quantisation is superseded by nearest rounding, ties away from zero, of
  `clamp(f, 0, 1) * (2^n - 1)`. Decoding remains `code / (2^n - 1)`.
- The same revision adds SNORM: encode floating input as nearest, ties away
  from zero, of `clamp(f, -1, 1) * (2^(n-1) - 1)`. Decode using
  `max(code / (2^(n-1) - 1), -1)`. Require a signed integer primitive and at
  least two mask bits. Preserve the most-negative raw signed code even though
  floating -1.0 encodes to the negative of the positive maximum.
- Normalised input and schema defaults accept raw integer codes or floating
  values. Store/output unshifted unsigned codes for UNORM and signed codes for
  SNORM; both must fit the logical type and mask width. Integer `1` means raw
  code one; `1.0` means normalised maximum. NaN becomes zero, and infinities
  clamp to their endpoint. Guard conversion and rounding at 64-bit boundaries.
  Public setters for raw codes and normalised floats remain separate operations.
- Explicit structure alignment is capped at 128 bytes. Accepted explicit
  layouts generate faithful C++ declarations using padding members under the
  selected compiler settings. The legacy review-only declaration path is
  removed; layouts that cannot meet the faithful contract are rejected.
- In direct-member remapping, a compound containing a differing enum is wholly
  unmatched because it is not the same exact compound type.
- Remapping uses one record count, supplied explicitly or defaulted to the
  minimum of source and destination counts. Execution takes bounded current
  source and destination views and checks ranges before writing. Public view
  calls and handle-based instance/bulk calls both exist; handle calls extract
  views and share the underlying executor without duplicating validation.

## Deferred or remaining questions

- Source ingestion is a separate follow-up task. Selected files, supported
  declarations, and target-layout evidence remain for that discussion.
- Exact public C++ signatures, record layouts, and the mechanics of reference
  counting and critical shutdown remain implementation design work.
- The baked document format and existing schema documents must be reconciled
  with reserved mutable locators, the revised live edit/reference rules,
  automatic descendant updates, empty live types, and faithful explicit-layout
  output before implementation.
