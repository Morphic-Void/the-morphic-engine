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
- Clarified on 1 October: one instance wrapper and one associated byte buffer
  cover the entire `instances` section. Branches provide naming scopes within
  it; they are not independently loaded documents. Separate instance documents
  each have their own wrapper and buffer.
- Adding new data is an isolated operation. If an append fails, existing entries
  remain usable. Bytes already appended may remain unreferenced, provided no
  locator for the failed entry is published.
- A schema is supplied explicitly when an instance or bulk document is created,
  promoted, or demoted. Conversion does not inherit the source's schema
  reference. Normal schema-matching rules determine success or failure during
  the operation; there is no separate pre-validation pass.
- Clarified on 1 October for bulk promotion using a different schema: referenced
  types must agree in names, member order, physical layout, primitive types,
  array counts, enum labels/values and bit masks/interpretations. Compare names
  across documents by text. Defaults may differ because existing binary values
  are copied; unrelated catalogue types need not agree. This does not revise
  the separate default-sensitive remapping rule.
- Confirmed on 1 October: instance promotion additionally requires matching
  defaults for referenced types, since retained declaration omissions affect
  later rebuilding. Promotion preserves authoritative binary snapshots without
  rebuilding them or applying defaults.
- Documents reference their schema through inter-document reference counts.
  Referencing documents do not own or extend the schema wrapper's lifetime.
  Attempting to mutate a referenced live schema fails without changes and
  triggers an assertion. A referenced schema cannot be moved. Revised on
  29 September: destruction invalidates and detaches client bindings through
  their intrusive list before releasing schema state. Subsequent dependent
  operations fail safely. A no-return panic is a last resort, not a required
  response; this supersedes the original mandatory shutdown rule.
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
  Revised on 30 September: empty types retain queryable resolved identities and
  zero-byte member records, with size zero and alignment one. References to
  them are valid, and a structure containing only empty members is itself
  empty. Generated C++ omits empty types and members. This supersedes the
  original unresolved/ignored-empty-reference proposal.
- Malformed schema validation or resolution fails with useful information for
  human review, comparable in purpose to a data-model parsing report and
  suitable for `MV_REPORT` or similar logging. A node handle may be used
  internally to build a name or path; no direct recovery through a public
  diagnostic handle is required. Hand-authored baked input is the main expected
  source of these failures because live schema editing is mediated.

## Locators, loading, and binary buffers

- Clarified on 1 October: an omitted instance `declaration` means no selected
  values. A base uses schema defaults; a specialisation inherits its complete
  parent unchanged. Optional comparison reconstructs that same meaning;
  omission never indicates discarded or unknown declaration intent.

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
- Confirmed on 1 October: descendant updates preserve selected binary values
  and synchronise retained declarations to those values, including when saved
  binary disagrees with the original declaration. Unselected values inherit
  from the updated immediate parent; equal-to-parent selections remain explicit.
- Removing a selected specialisation member rebuilds that image from its base
  and updates descendants. Adding a selected member supplies a replacement
  value and also updates descendants. Callers access underlying live documents
  only through the wrappers so these updates remain coordinated.
- Instance and specialisation output retain both declaration information and
  complete saved values, including distribution output. Bulk embedded `data`
  arrays may be retained for review or omitted when binary backing exists.
  Either form remains fully editable after promotion to a live wrapper.
- Confirmed on 1 October for embedded instance output: reject a snapshot that
  disagrees with an inherited value left unselected by its declaration. For
  example, a parent with `x = 1` and an unselected child snapshot with `x = 9`
  cannot be represented by the existing hierarchy without changing selection
  intent. Report a diagnostic rather than adding a selection or changing the
  saved value. Binary-backed output remains available.
- Confirmed on 2 October: the same rule applies to omitted base-instance fields.
  If a base omits `x`, its schema default is `1`, and its authoritative snapshot
  contains `9`, embedded output fails rather than adding a selection. Preserve
  the omission; binary-backed output remains available.
- Confirmed on 2 October: schema handling recognises the text parser's existing
  singleton-object normalisation within arrays. For an element structure with
  members `a` and `b`, `[{b:9}]` still selects only `b` after the parser unwraps
  its object; `a` remains inherited. Preserve that intent through loading,
  promotion, output and later edits, including corresponding bit-structure
  selections. Do not change the text parser or restrict these selections to
  baked output. Named entries for scalar array elements remain invalid.
- Confirmed on 1 October for both data roles: embedded output uses the existing
  encoded-field comparison rule for round-trip fidelity, treating NaNs as equal
  and ignoring padding and unused bits. Reject values that cannot round-trip
  under that rule, including noncanonical Boolean codes and unlabelled enum
  codes. Binary-backed output preserves those encodings.
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
- Clarified by the user on 2 October: direct-member remapping collects semantic
  and exact-type matches for unchanged binary copies. Missing members and all
  type mismatches simply remain unmapped, including same-name primitive, enum,
  array and compound differences. Partial and completely empty plans are valid.
  This supersedes the earlier hard-error rule for non-enum mismatches and the
  special treatment of enum differences. Once a compound is known to differ in
  type, do not inspect its contents to classify the mismatch or find submatches.
  Memory-operation validity is separate from finding matching data; offsets and
  strides may differ between source and destination.
- Simplified by the user during usage review on 2 October: remapping derives
  complete record capacities from bounded current views and stored layouts,
  processing their minimum through one `execute` operation. Callers narrow
  views to restrict the transfer. Remove per-view and requested record counts,
  copied-count output, `execute_min` and execution diagnostics. Execution returns
  success/failure; `initialise` retains its diagnostic. Zero-byte types impose
  no storage limit, and entirely zero-byte transfers are successful no-ops
  without a reported logical count. Handle-based instance/bulk calls will
  extract views and share the executor without duplicating validation.
- On 2 October the user confirmed that remapping into a live base instance or
  specialisation changes only the destination binary snapshot. Declarations,
  selections and descendant snapshots remain unchanged. Mapped fields need not
  already be selected. This is a binary transfer, not a capture or coordinated
  instance edit. Existing embedded-output checks may reject a resulting mismatch
  with omitted/defaulted or inherited values; binary-backed output remains
  available.

- The user subsequently confirmed that fast runtime binary updates are the primary
  remapping use case. A separate explicit refresh is the intended direction for
  creating document forms from remapped results, with scope and conflict handling
  controlled by refresh parameters. Exact refresh policies and hierarchy effects
  remain to be discussed; this does not authorise adding refresh to the binary
  remapping implementation.
- The primary remapping path reads baked or live data and fills application-owned
  buffers outside the schema system. Those destinations require a described
  layout, not a destination document. Writing into live backing within the schema
  system is also supported through the same executor. Both paths leave document
  updates and reconciliation to separate operations.

## Deferred or remaining questions

- Source ingestion is a separate follow-up task. Selected files, supported
  declarations, and target-layout evidence remain for that discussion.
- Exact public C++ signatures and private record layouts remain implementation
  design work for undelivered stages. Schema binding lifetime mechanics are
  implemented; the revised invalidation rule above governs dependent documents.
- The baked document format and existing schema documents must be reconciled
  with reserved mutable locators, the revised live edit/reference rules,
  automatic descendant updates, empty live types, and faithful explicit-layout
  output before implementation.
