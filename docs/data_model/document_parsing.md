Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   document_parsing.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   14 Sep 2026

# Document parsing and reporting

## Scope and entry point

This defines the current parser interface, reporting and acceptance contract.
The [text format](document_text_format.md) defines decoding, grammar and text
round trips; the [semantic specification](revised_data_model.md) defines live
and baked document behaviour.

`document_parser.hpp` exposes one public parsing function:

```cpp
[[nodiscard]] CDocumentReport parse(
    const CByteConstView& source,
    CLiveDocument& destination,
    const CDocumentParseOptions& options = {},
    CTextLintReport* const linter_report = nullptr) noexcept;
```

The function is in namespace `document_parser`. It performs no file I/O or
logging. Source bytes must remain immutable and alive until return; they may
refer to the destination's current storage. Parsing uses the ambient framework
allocator.

Every zero-length byte view, including a default/null view, means empty source
text. Successful parsing produces an empty object. Internally a present empty
`CStringView` preserves this meaning, while other internal string views retain
the distinction between absent and empty.

Every call performs, in order:

1. Lint and convert source bytes, normalising all document line endings to LF.
2. Check the successful linter output's structure.
3. Construct a private live document and perform semantic interpretations.
4. Evaluate caller policy against accumulated findings.
5. Move the private document into `destination` only if the report is accepted.

A processing failure stops subsequent stages. Policy exclusions, including
unknown option bits, do not stop processing early. All failure and rejection
outcomes preserve the existing destination. Temporary output is discarded
without losing the report. There is no separate strict parser, public
already-linted parse overload, or public `ingest` entry point.

When supplied, `linter_report` receives the complete linter report on every
outcome, including later structural/construction failure, policy rejection and
invalid options. Each call replaces that output, so old failures cannot survive
a later successful call. Requesting details does not alter parsing or policy.
The standalone linter remains a separate interface for consumers needing its
output text, metrics or decoding evidence.

## Common report and processing state

Structural checking and full parsing return the same `CDocumentReport`.
There is no nested structural or linter report.

| Member | Meaning |
| --- | --- |
| `state` | `EDocumentProcessingState : std::int8_t`: `failure = -1`, `unprocessed = 0`, `success = 1` |
| `failure` | One `CDocumentFailure` grouping stage, reason and locations |
| `findings` | Accumulated presence observations as `std::uint32_t` |
| `policy` | Independent `CDocumentPolicyResult` |

`processing_succeeded()` tests state alone. `accepted()` requires both
processing success and accepted policy. A default report is unprocessed with
no failure, no findings and unexamined policy.

| Operation outcome | State | Failure | Policy | Destination published |
| --- | --- | --- | --- | --- |
| Not processed | Unprocessed | None | Unexamined | No |
| Linter, structural or construction failure | Failure | Responsible stage and first reason | Unexamined | No |
| Construction complete, permitted features | Success | None | Accepted | Yes |
| Construction complete, disallowed features | Success | None | Rejected | No |
| Construction complete, invalid option bits | Success | None | Invalid options | No |
| Successful standalone structural check | Success | None | Unexamined | No destination argument |

Stage completion is implied by processing order and the failing stage, not
stored as duplicated progress flags. Standalone structural success establishes
syntax only. Full parse success establishes construction, even when policy
rejects the result.

Findings are cumulative observations, not errors or occurrence counters.
Linter source observations can describe the whole bounded source even when
later processing fails. Decoding, structural and construction observations
describe only work actually examined or completed. A failed report's missing
bits do not prove absence in the rest of the document. Root lookahead does not
publish speculative observations. Successful CP1252 fallback retains useful
UTF-attempt evidence without unioning abandoned decoding interpretations.

The parser imports source observations into the report returned by structural
checking and continues using that report during construction. It retains one
terminal diagnosis; later failures must not replace the first cause.

## Failure diagnosis and logging

`CDocumentFailure` groups:

| Member | Type and meaning |
| --- | --- |
| `stage` | `EDocumentFailureStage`: `none`, `linter`, `structure` or `parser`; `parser` identifies construction |
| `reason` | One `EDocumentFailureReason`; `none` when no terminal processing failure occurred |
| `location` | Detection or prospective output location |
| `element_start` | Beginning of the affected element when available |

Logging consumers normally record stage, reason and detection location
together, with element start as additional context. Policy rejection has no
terminal failure; log the policy status and disallowed features instead.
The parser does not log, filter nodes or take consumer-directed recovery action.

The complete reason vocabulary is:

| Category | Reasons |
| --- | --- |
| Decoding | `utf8_decode`, `undefined_cp1252_byte`, `cp1252_decode` |
| Structure | `unexpected_character`, `unterminated_comment`, `unterminated_string`, `invalid_escape`, `invalid_surrogate_pair`, `newline_in_name`, `missing_name`, `missing_colon`, `missing_value`, `missing_separator`, `mismatched_delimiter`, `unexpected_end`, `trailing_content` |
| Construction | `numeric_out_of_range`, `construction_failed` |
| Resources and invocation | `invalid_input_view`, `allocation_failed`, `input_limit`, `storage_limit`, `internal_error` |

Resource reasons use the stage to identify where they occurred. Linter
`output_limit` maps to shared `storage_limit`. Known parser scratch failures
retain allocation/storage reasons. A rejected live creation reports
`construction_failed` because that live API does not distinguish all underlying
allocation and storage causes. Not every reason is reachable from every entry
point: for example, a default byte view is empty input to public parsing, but
an absent string view is invalid input to standalone structural checking.

A structural failure takes precedence over policy, and construction failures
also precede policy. For example, disallowing hexadecimal does not turn
`0x10` into a syntax error; it produces rejection after successful construction.
An out-of-range number remains a construction failure even with invalid policy
options. Processing does not continue through a fatal failure to collect
hypothetical later errors.

## Diagnostic coordinates

All stages use `CTextLocation` with `available`, `line_1_based` and
`code_point_column_1_based`. An unavailable position is explicit; it is not
encoded as a valid coordinate.

Positions refer to the linter's UTF-8 output, excluding its physical terminator,
not original byte offsets or decoded string-value positions. Only emitted LF
advances the line and resets the next column to one. A tab counts once, a
combining scalar counts separately, and a supplementary scalar counts once
regardless of its source encoding. The six source characters of `\u0000`
occupy six columns even though construction produces one NUL.

Stripped BOMs and transcoding do not add hidden columns. Discarded comments
remain part of this coordinate space. There is no source-to-output offset map
or promise to reconstruct original input locations.

For linting failure, `location` is the prospective output cursor. Failure to
decode the first scalar is at (1, 1); a failure after emitted content keeps the
cursor where decoding/emission could not continue, even if output is discarded.
Failures without a meaningful text position may leave it unavailable.
`element_start` is unavailable because linting does not recognise document
structure. The optional full linter report supplies `first_failure.before_output`
and decoder evidence. A terminator alone is not an emitted code point; successful
empty input is not a failure-before-output case.

For structural and construction failures, `element_start` identifies the
immediately affected token, member or container where available:

| Source/failure | Element start | Detection |
| --- | --- | --- |
| `{"s":"abc` | Opening quote of the value | EOF |
| `{"a":[1}` | Opening bracket | Unexpected closing brace |
| `{"a":{"b":1]}` | Inner opening brace | Unexpected closing bracket |
| `{` followed by EOF | Opening brace | EOF |
| `{"a" 1}` | Member name's opening quote | `1`, where a colon was required |
| Newline in a name | Name token's start | Literal break or backslash of the offending escape |
| Well-spelled out-of-range number | Numeric token's start | Numeric token's location |

Missing member tokens use the member's beginning; container-level separator or
closing errors use the container's beginning. Unexpected input without a
containing element uses the offending token. An implicit root has a virtual
start at (1, 1). Successful operations and policy-only rejection carry no
failure locations.

## Findings inventory

`EDocumentFinding` has a `std::uint32_t` underlying type. Report storage and
option masks also use `std::uint32_t` directly. Presence bits accumulate by OR.
Named `k_*_start` and `k_*_end` enum aliases delimit inclusive contiguous groups;
`document_findings` provides `k_source`, `k_relaxed`, `k_morphic`, `k_semantic`
and their union `k_all`. The following table records the current bit identities.

| Bit | Finding | Observation | Permission required |
| --- | --- | --- | --- |
| 0 | `non_ascii_utf8` | Non-ASCII ordinary UTF-8 on the adopted decoding path | Yes |
| 1 | `modified_nul` | Exact source `C0 80` decoded | Yes |
| 2 | `cesu8_pair` | Valid source CESU pair normalised | Yes |
| 3 | `cp1252` | CP1252 decoding path adopted, including a subsequently failing attempt | Yes |
| 4 | `literal_source_nul` | Raw payload zero in source bytes | No |
| 5 | `leading_bom` | Leading BOM detected | No |
| 6 | `stripped_utf8_bom` | Leading UTF-8 BOM stripped | No |
| 7 | `stripped_terminal_zeros` | Trailing source zeros stripped | No |
| 8 | `utf8_attempt_failed` | UTF decoding attempt failed, whether terminal or followed by fallback | No |
| 9 | `reserved_undefined_cp1252_byte` | Reserved for the linter's existing error flag; not a shared observation or permission | No |
| 10 | `comments` | Comment syntax | Yes |
| 11 | `unquoted_names` | Unquoted member name | Yes |
| 12 | `unquoted_strings` | Unquoted string value | Yes |
| 13 | `single_quotes` | Single-quoted name or string | Yes |
| 14 | `trailing_commas` | Trailing comma | Yes |
| 15 | `raw_quoted_line_breaks` | Literal line break in quoted text | Yes |
| 16 | `raw_quoted_controls` | Other literal quoted controls, excluding logical NUL | Yes |
| 17 | `name_collision_extension` | Completed duplicate-name extension | Yes |
| 18 | `implicit_body` | Non-empty unbraced object or multiple unbracketed root values | Yes |
| 19 | `explicit_plus` | Leading plus on a complete numeric value | Yes |
| 20 | `binary` | Binary integer value notation | Yes |
| 21 | `hexadecimal` | Hexadecimal integer value notation, either prefix | Yes |
| 22 | `alternate_hexadecimal_prefix` | Additional `#` hexadecimal-prefix observation | Yes |
| 23 | `empty_member_name` | Present empty name | No |
| 24 | `singleton_normalization` | Completed singleton-object unwrapping | No |
| 25 | `logical_nul` | Logical NUL encountered in a name or string value | No |

Bits 0–8 are the source group; 10–18 relaxed syntax; 19–22 Morphic extensions;
23–25 semantic observations. `from_source` imports only the source group,
excluding reserved bit 9. That linter error is translated to a terminal reason.
Detailed line-ending masks, decoder evidence and statistics remain in
`CTextLintReport` rather than gaining duplicated document bits.

`k_encoding_features` contains bits 0–3. `k_acceptance_features` combines those
bits with the relaxed and Morphic groups. All other findings are informational.
Names that resemble numbers do not acquire numeric value findings, and a minus
sign or decimal exponent does not itself require a Morphic permission.
The [text format](document_text_format.md#nul-provenance) distinguishes raw,
modified, escaped and physical NULs.

## Acceptance policy

`CDocumentParseOptions::allowed_features` defaults to `document_policy::k_default`.
Permission bits use the corresponding finding identities, but the complete
findings mask must not be treated as a permission mask.

| Preset in `document_policy` | Allowed features |
| --- | --- |
| `k_ascii` | No feature bits; ordinary ASCII syntax needs none |
| `k_utf8` | Non-ASCII UTF-8 |
| `k_modified_utf8` | Ordinary UTF-8, modified NUL and CESU pairs |
| `k_default` | `k_modified_utf8`, CP1252 and every Morphic numeric extension |
| `k_all_supported` | Every encoding, relaxed and Morphic permission |

Encoding presets alone grant no relaxed or numeric-extension permissions.
ASCII always needs no encoding bit. Selecting either modified NUL or CESU
permission also grants ordinary UTF-8, but does not grant the other
compatibility form. ASCII JSON Unicode escapes do not acquire a non-ASCII source
permission merely because their decoded values are non-ASCII. CP1252 conversion
does not disguise source bytes as originally UTF-8.

The evaluator `document_policy::evaluate(findings, options)`:

1. Computes `unknown_policy_bits` as allowed bits outside
   `k_acceptance_features`. This includes informational and reserved bits.
   If non-zero, returns `invalid_options` with effective and disallowed masks zero.
2. Otherwise copies allowed features and adds ordinary UTF-8 permission when
   modified-NUL or CESU permission is present.
3. Computes
   `disallowed_features = (findings & k_acceptance_features) & ~effective_allowed_features`.
4. Returns `accepted` when that mask is zero, otherwise `rejected`.

The result retains `status`, `effective_allowed_features`,
`disallowed_features` and `unknown_policy_bits`. It remains unexamined if
processing fails. Unknown bits in findings are not granted permissions:
evaluation considers only the recognised acceptance-feature subset.

For example, to retain defaults while admitting comments:

```cpp
const CDocumentParseOptions options{
    document_policy::k_default | EDocumentFinding::comments
};
const CDocumentReport report =
    document_parser::parse(source, destination, options);
```

`#FF` needs both hexadecimal permissions; selecting only the alternate-prefix
bit does not imply the base permission. Repeated names require collision
extension permission even if their spelling is otherwise JSON. Empty names,
logical NUL, empty input, scalar-root adaptation and singleton normalisation
have no additional permission. All supported features can still fail for
structure, numeric range, resource or document-content reasons.

A caller may re-evaluate complete findings under a stricter policy, or apply
independent content filtering externally. The evaluator does not parse text or
prove processing success, and cannot publish a document that was discarded
after an earlier rejection.

## Standalone structural checking and linting

`document_structure::check(const CStringView&, CDocumentStructureEstimates*)`
takes successfully linted UTF-8 with an explicit logical length, excluding the
physical terminator. All source line breaks must already be normalised to LF.
An absent string view fails; a present empty string produces structural success.
The source must remain immutable and alive for the call.

The checker shares lexical rules with construction and uses an iterative
framework frame vector. It builds no document or full token tree. It checks
quotes, escapes, member/value placement, delimiters and numeric spelling, but
does not convert numbers, compare decoded names for collision extension, or
preflight construction allocations. Allocation/storage failure in its own
scratch is a processing failure at the structure stage. There is no arbitrary
grammar depth cap; framework allocation and storage limits still apply.

Optional estimates are reset on entry and published only after structural
success. They are separate from diagnostics:

| Estimate | Meaning |
| --- | --- |
| `value_count` | Syntactic values, including the explicit or inferred root |
| `object_count` / `array_count` | Syntactic containers |
| `named_entry_count` | Member occurrences |
| `string_source_byte_size` | Raw name/string token bytes, including quotes and escapes |
| `maximum_depth` | Maximum container depth, with the root at depth one |

These precede collision extension and singleton normalisation, so they are
neither exact live-node counts nor allocation guarantees. A standalone report
contains lexical/structural findings, not the source-encoding findings held by
a separate linter result.

Standalone `text_linter::lint` accepts byte or string views. Its null views
mean absent input and fail; a present zero-length `CStringView` succeeds.
Its default newline mask covers LF, CR and CRLF; callers can select a different
mask, including zero to preserve source line endings. Diagnostic coordinates
still advance on emitted LF, while aggregate line metrics follow the selected
mask. Document parsing always uses `k_document_text_lint_line_endings`.

The full `CTextLintReport` retains source/output byte sizes, line/content
metrics, literal/modified NUL and CESU counts, terminal-zero counts,
`encountered_line_endings`, `normalised_line_endings`, CP1252 confidence/evidence,
and UTF attempt diagnostics. A CESU pair counts as one decoded scalar, with
six source bytes and four UTF-8 output bytes. Raw-byte observations cover the
bounded payload; decoding statistics describe the examined prefix of the
adopted attempt. The CP1252 replacement count remains zero.

`first_failure` identifies only a terminal failure and preserves its location
after partial output disposal. `utf8_attempt_errors` retains evidence from an
abandoned UTF attempt; successful fallback has no terminal `first_failure`.
Failed output is discarded, its logical size is zero and it claims no ready
output encoding. Successful output is bounded payload plus one appended NUL.
Consumers must use `logical_text_byte_size` rather than a zero-terminated
string-length function.
