Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   parser_refactoring_specification.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   11 Sep 2026

# Linter, structural check and parser refactoring specification

Status: reviewed specification; stage 1 (linter and shared diagnostics) is
implemented, validated and reviewed as of 11 September 2026. Stage 2 (the
remaining parser/model/writer and acceptance-policy
migration) has not begun and awaits explicit progression instruction. Section 9
records the completed scope and remaining review boundaries. Pause before
commits for review.

## 1. Purpose and scope

Give callers actionable information about the text they supplied, its structural
integrity and the features interpreted when constructing a document. A caller
must be able to accept, discard, filter or log a result using explicit findings
and policy rather than inferring meaning from counters or a generic failure.

This work covers the three existing stages:

1. Lint source bytes into bounded UTF-8.
2. Check the structural integrity of that text.
3. Parse it into a live document.

It includes shared lexical support, report interfaces, acceptance policy,
diagnostic locations and corresponding tests/call-site changes. It does not
implement Host operations, cancellation, asset lifetimes, filesystem services,
the Executive functional exercise or schema work. Those remain in the
[consolidation plan](consolidation_pass.md).

The [current semantic specification](../data_model/revised_data_model.md)
describes implemented behaviour, including the completed linter and shared
location contracts. Remaining reporting and acceptance changes are specified
here for stage 2. The object-or-array root direction expands
the affected scope to live root construction/clearing, baking, baked validation,
promotion and writing where they currently assume an object root. Per-string
metadata controlling newline escaping also expands the live/baked model,
baking, promotion and writer scope. Review compatibility for both changes.
Live name-table initialization must also prepopulate canonical identifier strings
with fixed name IDs. Replace recovered arrays with ordinary arrays through a
separate public collision-extension operation. Retire the old recovery protocol
and its compatibility handling; it has no consumers beyond the existing tests.
Other baked format and writer behaviour remains outside scope except for
regression checks.

## 2. Agreed requirements

- The caller specifies accepted source forms and deviations from standard JSON,
  using a bitmask or equivalent flag-based interface. Relevant choices include
  ASCII, UTF-8, Java-modified UTF-8, CP1252, relaxed syntax and Morphic extensions.
- Feature-acceptance policy is applied only after linting, structural checking
  and parsing have otherwise succeeded. Disallowed features are recorded during
  processing, not used to reject early. Structural and construction failures
  must not be hidden by policy rejection.
- An undefined byte encountered while decoding CP1252 is a linter failure.
  It must not be silently replaced or admitted by an acceptance-policy option.
- At the linter boundary, accept exact Java-modified NUL and valid CESU-8
  surrogate-pair spellings of supplementary characters and normalize them.
  Use SuiteUTF for decoding; emit standard UTF-8 supplementary scalars.
  CESU byte encodings are not accepted for direct document entry or output.
  Preserve logical NUL promotion to exact `C0 80` in live/baked
  strings; surrogate representations must never be interpreted as modified NUL.
  CP1252 remains a separately identified source-decoding path.
- Default acceptance is conservative: permit standard JSON and the Morphic
  hexadecimal, binary and explicit-positive-sign numeric forms; exclude relaxed
  syntax. Also accept CP1252 and the supported modified-UTF-8 exception by default;
  undefined CP1252 bytes remain failures. Do not enable all Morphic features
  merely through their grouping.
- Identify individual features, including explicit positive numeric signs,
  hexadecimal and binary numbers, unquoted names, unquoted string values,
  embedded NULs and embedded overlong/modified NULs.
- Replace parser statistics with presence flags, preserving their findings.
  Retain the linter's aggregate statistics; the statistics-to-flags replacement
  applies to parser reporting, not to the linter's statistical report.
- Allocate related flag values contiguously by category: Morphic extensions,
  relaxed syntax, structural errors, linter issues and other coherent groups.
- All three stages use the linter's output as their diagnostic coordinate
  space. Public diagnostics use line and code-point position within the line,
  not byte offsets into the original input or linted output.
- Use the same location representation for linter and parser diagnostics. The
  parser report has separate structure-start and failure-point locations. If
  linting fails, its error location becomes the parser report's failure point;
  the structure-start location is unavailable because structure was not examined.
- Positions are 1-based: the first line is line 1 and its first code point is
  column 1. Document linting recognizes every linter-supported line-break form
  and normalizes it to LF without knowledge of strings or JSON structure.
- Escaping belongs to the parser and writer within strings and names, including
  unquoted strings/names. JSON structural characters outside those contexts
  remain literal syntax and must not be escaped. Raw line breaks within quoted
  strings are supported content, including multiline shader source.
- The writer escapes newlines by default. Each string can carry document
  metadata suppressing newline escaping for that string. The parser sets this
  metadata automatically when the source string contains at least one literal
  line break. Escaped line endings alone do not trigger suppression. Preserve
  this metadata through live/baked document transformations.
- Writer suppression covers every newline form. Normalize every written newline
  to LF, emitting literal LF with suppression or its escape spelling without it.
  Strict JSON output overrides suppression and emits `\n`; it does not modify
  the stored string metadata.
- A structural failure identifies the start of the immediately malformed
  element, such as an unterminated string's opening quote or the opening brace
  of an object closed by an array delimiter, and the point where it failed.
- A linter failure before producing any code point has an explicit no-output
  indication. After producing code points it retains a usable position even
  if the output buffer is discarded.
- Names cannot contain newlines, whether quoted or unquoted. Apply this to
  decoded name content as well as literal spelling; JSON's ordinary string
  spelling rules otherwise govern names alongside the agreed syntax extensions.
- Programmatic document construction defaults to an object root. Changing root
  type in either direction is permitted only while the root is empty.
  Empty/whitespace-only input produces an empty object root. Comment-only input
  can also produce an empty object root, subject to comment policy. Root erasure
  preserves its type; document reset restores an object root.
- Prepopulate the live document's name string table with the canonical identifier
  strings in a deterministic order, giving each a fixed name ID across live
  documents. `$morphic-empty` is the canonical replacement for a quoted empty
  member name. This is the only required prepopulated identifier. It always exists
  in a live document but need not appear in baked output if unreferenced.
- Normal insertion continues to reject name collisions. Provide a separate public
  operation for array extension on a name collision, usable by both the parser
  and ordinary live-document callers. The direction is ordinary arrays without
  a special recovered-array value kind or collision-origin distinction.
- Parser collision extension is a relaxed feature, reported when encountered
  and rejected by the default policy only after parsing otherwise succeeds.
  Process collisions in encounter order. Two colliding arrays become elements
  of a new array; incoming non-array values append to the existing top-level array.
- Remove the recovered-array type, text wrapper and reserved-name escaping, and
  retire their compatibility handling and corresponding legacy tests.
- The structural pass checks well-formed syntax and parseable token spelling.
  It assumes numbers are representable, name collisions can be handled and
  subsequent construction allocations succeed. It does not guarantee parsing
  success. Failure of its own scratch allocation is a resource failure.

The following sections define the agreed behaviour and its implementation
constraints. Section 10 records the decisions and the remaining design work.

## 3. Acceptance and processing outcomes

### 3.1 Separate observations, permissions and failures

Use an encountered-findings mask and a caller-allowed-features mask. Related
acceptance bits should share identities with their report findings where that
makes membership tests direct. Provide named masks for each category and useful
presets. Exact C++ names and bit numbers are implementation details; category
members must form contiguous ranges and must not overlap.

Do not compare the complete report mask against the allowed mask. Structural
errors, resource failures and informational transformations are not permissions.
Extract only acceptance-relevant findings before determining disallowed features.
For those features the basic test is:

`disallowed = encountered_acceptance_features & ~effective_allowed_features`

Reject or explicitly diagnose unknown policy bits; do not silently grant new
permissions. Keep one shared definition of category masks and policy closure
so stages cannot disagree about the same feature.

Encoding and acceptance rules:

- ASCII is accepted whenever ordinary UTF-8 is allowed.
- Ordinary UTF-8 is accepted whenever a Java-modified UTF-8 preset is selected.
- Final acceptance of CP1252 source requires permission. Conversion itself
  proceeds on the supported decoding path so later structural/parser errors
  remain observable even when CP1252 is disallowed by the acceptance mask.
  Producing UTF-8 must not disguise the source form as originally compliant UTF-8.
- Non-ASCII UTF-8 permission concerns the source encoding, not the characters
  obtained by decoding JSON escapes. An ASCII source containing `\u00e9`
  remains ASCII source.
- Encoding presets do not implicitly allow unrelated syntax extensions or raw
  control characters. Preserve the existing embedded-NUL admission and storage
  policy rather than introducing a new NUL-permission dependency.

The conservative default is not an unmodified JSON validator. In addition to its
explicit encoding/numeric allowances, apply the agreed document rules: empty
input produces an object, scalar roots adapt to arrays, quoted empty names are
replaced, and newline-bearing names are prohibited. Duplicate-name extension is
explicitly classified as relaxed even though JSON grammar can express duplicate
members. Document these behaviours independently of the syntax-feature mask.

The agreed default allows ordinary UTF-8 (including ASCII), CP1252 and the
supported modified-UTF-8 exception, plus hexadecimal, binary and explicit-positive
numeric forms. Relaxed features, including collision extension, are excluded.
The supported compatibility input includes valid CESU-8 supplementary pairs;
record that source form separately from modified NUL even when a grouped
permission enables both. This normalization is available only through linting.
Undefined CP1252 bytes always fail. Other Morphic features are not automatically
enabled by their grouping. Valid JSON `\u0000` and its stored NUL form are unchanged.
Choose auxiliary preset names during API review; source-encoding defaults are
settled. Keep internal `C0 80` storage distinct from source spelling findings.

Feature detection must distinguish evidence from an adopted decoding path.
Successful CP1252 recovery can report the failed UTF-8 attempt as evidence;
that attempt must not make the final result a terminal UTF-8 processing failure.
CP1252 inference remains an inference, not proof of the author's encoding.

### 3.2 Stage and final results

The report must distinguish successful processing from final policy acceptance.
Represent at least: accepted, policy rejected, linter failure, structural
failure, parser/construction failure, and resource/internal failure, with the
responsible stage and specific reason. A caller rejecting hexadecimal numbers
must not receive a mismatched-delimiter or malformed-number diagnosis for `0x10`.

Process supported input forms and collect findings irrespective of whether the
acceptance mask permits those features. A disallowed feature is not a reason
to stop a stage. Only after parsing has otherwise succeeded may acceptance
policy reject the result.

The outcome order is linter success, structural success, parser/construction
success, then policy acceptance. A structural error takes precedence over an
otherwise applicable feature-policy rejection. Numeric conversion, construction
and resource failures also remain processing failures rather than being relabeled
as policy rejection. This ordering does not require continuing through undecodable
input or another fatal processing failure to search for hypothetical later errors.

Record which stages completed. An uncompleted structural check means structural
integrity is unknown, not valid or invalid. Findings collected before a real
processing failure remain useful, but absent flags in a partial scan are not
proof of absence in the file. No separate strict parser, optional early feature-
policy rejection mode or exhaustive-error-recovery mode is required.

Maintain destination-preservation semantics: stage construction privately;
publish only when processing and acceptance succeed. Rejection or failure
leaves the caller's existing destination unchanged. Retain the report when
discarding temporary output. A later caller can also apply a stricter policy
to a complete report and dispose of a previously accepted result.

No automatic logging, document filtering, or deletion of individual nodes is
implied. The report supplies information for caller-directed actions. Optional
retention of rejected diagnostic assets belongs to later Host policy.

## 4. Findings inventory and grouping

The following inventory uses descriptive names rather than declarations of a
finalized enum. Presence bits accumulate by OR; repeated
occurrences do not allocate records or increase parser counters. The linter
continues to accumulate its existing statistics alongside presence findings.

| Contiguous group | Findings to represent |
| --- | --- |
| Source encoding and linter observations | Non-ASCII UTF-8 used; modified-NUL form used; CESU-8 supplementary pair normalized; CP1252 decoding adopted; literal source NUL present; leading BOM present/stripped; terminal source zeros stripped; encountered and normalized line-ending forms; undefined CP1252 byte failure; existing CP1252 supporting/counter-evidence and relevant UTF decoder findings. |
| Relaxed syntax | Comments; unquoted names; unquoted string values; trailing commas; raw line breaks in quoted strings; other raw quoted controls apart from the established logical-NUL case; name-collision extension; non-empty implicit/unbraced root object; implicit array-body syntax where the source contains more than one top-level value. |
| Morphic extensions | Single-quoted strings/names; explicit positive numeric sign; hexadecimal notation; binary notation; alternate `#` hexadecimal prefix. Keep all Morphic feature bits together. |
| Semantic observations | Implicit array root constructed, including scalar-root adaptation; quoted empty name replaced; redundant singleton normalization; logical NUL in decoded names/values. Keep these distinct from source spelling and structural corruption. |
| Structural errors | Unexpected character; unterminated comment/string; invalid escape/surrogate pair; forbidden newline in a name; missing name/colon/value/separator; mismatched delimiter; unexpected end; trailing content. Malformed numeric-looking candidates fall back to strings rather than producing a numeric syntax error. |
| Construction failures | Numeric range failure; invalid root value; other construction rejection. Quoted empty names use the replacement rule below rather than the former empty-name rejection. |
| Resource and invocation failures | Invalid input view; allocation failure; storage/input limits; internal error. |

Specific statuses or bounded auxiliary evidence may accompany masks when that
preserves a useful distinction more clearly than additional bits. Parser
occurrence-count structures are replaced; linter statistics remain. Choose
sufficient mask storage during interface design rather than compressing unrelated
findings together.

Normal JSON numeric kinds/signs are not Morphic deviations. A minus sign,
decimal integer, exponent or ordinary floating-point literal must not require
the explicit-positive-sign permission. The existing signed/unsigned numeric
intent can be retained independently of those syntax permissions.

Remove recovery-wrapper and reserved-name-escaping findings and associated
interpretation failures. Former protocol-looking names and objects are ordinary
data under the new grammar; do not decode, migrate or silently unescape them.

### 4.1 NUL provenance

Preserve these distinctions:

- A literal payload NUL in source bytes.
- An accepted overlong/Java-modified NUL spelling, specifically `C0 80` in
  the currently implemented path.
- A NUL obtained by interpreting a JSON escape such as `\u0000`.
- A physical buffer terminator or stripped source terminator.

Converting `C0 80` into U+0000 must preserve the modified-source finding. It
must not invent a literal-source-NUL finding. Decoding `\u0000` must not claim
that the source contained a raw control or an overlong encoding. A decoded-NUL
finding permits a separate content filter without declaring the valid JSON
escape structurally defective. Physical terminators are excluded from content.

Preserve the established NUL admission rule: literal or normalized logical NUL
does not acquire a new generic raw-control relaxation requirement. Otherwise
accepting modified-NUL source by default would be defeated by the NUL that
linting emits. Keep literal source NUL, modified source NUL and decoded value NUL
observable separately, without labeling one source spelling as another. This
exception does not extend to other raw control characters.

Keep the established representation boundaries: the linter may emit logical
U+0000 in its bounded output; live string admission promotes logical NUL to exact
`C0 80`, preserves an existing exact `C0 80`, and otherwise requires strict UTF-8.
This applies equally to literal source NUL, modified source NUL and JSON-escaped
NUL. The writer uses `\u0000`. No other overlong spelling or surrogate sequence
may stand in for modified NUL. Valid paired JSON Unicode escapes for supplementary
characters remain separate from this NUL rule.

### 4.2 CESU compatibility input and canonical document encoding

Java's supplementary-character representation uses CESU-8's encoding of a UTF-16
surrogate pair as two three-byte sequences. Accept valid pairs at the linter
boundary using SuiteUTF, combine each pair into one supplementary scalar and
emit its standard four-byte UTF-8 representation. The linter already uses
SuiteUTF; its current `JUTF8st` decoder, rejection mask and source-byte-copying
output path need coordinated revision. SuiteUTF supplies `CESU8st` and
`JCESU8st`, the latter combining CESU input with modified NUL handling.
[SuiteUTF variants](../../external/SuiteUTF/docs/utf/variants_utf_sub_type.md)
[Java DataInput: modified UTF-8](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/io/DataInput.html#modified-utf-8)
[Unicode CESU-8 definition](https://www.unicode.org/reports/tr26/)

Do not merely relax a
diagnostic mask and copy the original six bytes: normalization must produce
canonical UTF-8. Retain a distinct CESU source/normalization finding and the
linter's aggregate statistics. Each combined pair counts as one code point in
the emitted diagnostic coordinate space, independently of its six input bytes.
Compatibility permission does not admit isolated, reversed or truncated
surrogates, overlong surrogate units, arbitrary overlong UTF-8 or extended forms.

CESU normalization is not part of direct entry into any document type. Live
name/value admission, baked validation, copying and promotion must not admit or
preserve CESU byte sequences. Baked bytes and direct document text use standard
UTF-8 supplementary scalars; the established exact `C0 80` logical-NUL storage
exception remains unchanged. Lower-level parser entry points consuming linted
text must not introduce a second compatibility decoder.

The writer must never emit CESU byte sequences. It emits standard UTF-8, or
ordinary JSON Unicode escape pairs where output options require them. JSON
`\uXXXX` surrogate-pair escapes are textual syntax, not CESU-encoded bytes, and
remain valid. No CESU decoding or output encoding belongs in document mutation
APIs.

### 4.3 Retention and removal of counts

Retain findings already established before a later failure; do not clear them
merely because a result buffer/document is discarded. Do not publish an
interpretation as completed before it is actually recognized/performed. Use
stage completion to distinguish partial observations from a complete report.

Encoding fallback must not union contradictory interpretations from abandoned
attempts. Keep useful attempt-failure evidence, but publish source-feature and
position information consistently with the adopted interpretation.

Retain the linter's aggregate statistics, including its line/code-point/byte
metrics and existing occurrence/size information. Presence findings used by
acceptance policy coexist with that statistical report.
Output-relative diagnostic positions do not require removal of source/output
aggregate measurements; measurements are distinct from error locations.

Replace parser interpretation statistics with presence findings. Preserve the
observations about supported collisions and normalization without
counting repeated occurrences. Do not expose document-construction capacity
estimates in the public parser diagnostic report, including through its composed
structural report. Retain estimates internally or in construction-specific data
if they usefully support live-document construction. Their usefulness is an
implementation assessment, not a reason to retain diagnostic-report counters.

Logical buffer length and bounds remain available for embedded NUL safety.
Line and code-point coordinates remain numeric positions. Existing source,
encoding, transformation and failure findings survive the refactor except for
the explicitly retired recovery protocol.
Other writer reporting is outside this statistics change; obsolete recovery and
reserved-name counters are removed with the retired features.

## 5. Shared diagnostic coordinates

### 5.1 Coordinate definition

Lines and code-point columns are 1-based: the first line is line 1 and the
first code point on any line is column 1. Use an explicit unavailable state
instead of using a valid coordinate as a failure sentinel. All locations refer
to the UTF-8 text emitted by the linter, excluding its physical terminal zero.

Use one shared location representation, including the same availability state,
line field and code-point column field, in the linter, structural and parser
reports. The parser report contains two locations of that form: structure start
and failure point. A linter error needs one location of the same form, not a
separate coordinate layout or an artificial pair of structural locations.

A column counts Unicode code points, not UTF-8 bytes, UTF-16 code units,
grapheme clusters, tab-expanded display columns or characters in the parsed
string value. A supplementary scalar counts once; a combining scalar counts
separately; a tab counts once. The six source characters of `\u0000` occupy
six source code-point positions, although parsing produces one NUL value.

Document linting recognizes all forms currently provided by ETextLineEnding:

| Source form | Code points |
| --- | --- |
| LF | U+000A |
| CR | U+000D |
| CRLF | U+000D U+000A |
| LFCR | U+000A U+000D |
| VT | U+000B |
| FF | U+000C |
| NEL | U+0085 |
| LS | U+2028 |
| PS | U+2029 |

Normalize each recognized source line break to one LF in document-linter output.
Recognize compound CRLF/LFCR before their individual constituents. Downstream
line tracking therefore advances once for each emitted LF and resets the next
code-point column to 1. The linter retains encountered/normalized form findings
and aggregate statistics; output-relative locations do not expose source byte
widths or count a compound break as two output lines.

The linter normalizes line breaks without interpreting JSON, quotes, comments
or escapes. A literal line break within a quoted string therefore reaches
the structural pass as LF and is accepted as string content. This supports
multiline text such as shader source. The parsed value contains the normalized
LF, not the original source line-ending spelling. Callers can also use
the agreed escape repertoire, for example `\n`, `\r`, `\f`, `\u000B`,
`\u0085`, `\u2028` or `\u2029`. An escaped CRLF value uses two escaped code
points, preserving both when the parser later decodes the string. Escape
spellings remain ordinary characters in linted text and do not advance its line
number; they become embedded line-break values only during escape decoding.

A physical break terminates an unquoted candidate rather than embedding a
break within it. All source line-break forms become LF before the parser's
JSON-whitespace boundary rules are applied. Breaks in comments remain legal
and advance coordinates. Escape spellings in comments have no escape semantics;
decoded string content is never interpreted again as document structure.

There is no requirement to reconstruct original-input positions. Stripped BOMs
and transcoding cannot add invisible source columns to parser diagnostics.

Private byte offsets remain permissible for scanning, slices and allocation.
Remove byte-offset fields from public linter/structural/parser locations.
Position tracking must be iterative and avoid rescanning the complete prefix
at every token. Retain element-start positions alongside necessary nesting
state, rather than constructing a full token tree just for diagnostics.

### 5.2 Linter failure

- If no complete code point has been emitted, report the specific cause and
  an explicit failure-before-output indication. For failure to decode the first
  code point, the prospective location is line 1, column 1; this describes the
  attempted output position, not an emitted character. Non-decoding failures
  without a meaningful text position may use the unavailable state.
- If at least one complete code point has been emitted, retain a valid cursor
  in that output coordinate space even when the buffer is discarded. For decoder
  failure, it is the line and prospective column of the undecodable code point;
  for output allocation/size failure, it is where emission could not continue.
- Producing a physical terminator alone does not constitute output code points.
  Present empty input can succeed; it is not a failure-before-output case.
- Retain position independently of output ownership. Reset tracking for a new
  decoding attempt; never attach abandoned input coordinates to a later output.
- Allocation/size failures after an emitted prefix follow the same rule. If
  publication fails after complete text emission, its cursor is the output EOF.

### 5.3 Structural and parser failure

The required primary location is the start of the immediately malformed
element. Also report where the structure failed, including the EOF cursor when
applicable. Both locations are required for structural errors.

| Input or failure | Primary location | Detection location |
| --- | --- | --- |
| `{"s":"abc` | Opening quote of `"abc` | EOF |
| `{"a":[1}` | Opening `[` | Unexpected `}` |
| `{"a":{"b":1]}` | Opening brace of the inner object | Unexpected `]` |
| `{` followed by EOF | Opening `{` | EOF |
| `{"a" 1}` | Start of the member name `"a"` | `1`, where a colon was required |
| `{"a":1e+}` | No structural failure: `1e+` is an unquoted string candidate | Acceptance depends on unquoted-string permission |
| A valid numeric token outside the supported range | Start of that token; parser failure | Conversion point if useful |

For unexpected top-level input with no containing element, use the offending
token. For a missing token in a member, use that member's beginning; for a
container-level separator or close failure, use that container's beginning.
An implicit root has a defined virtual start at line 1, column 1. Never walk
out to an unrelated enclosing element when the immediate element is known.

Stop at the first fatal structural error in this refactor. Multiple possible
error bits describe the reason, not a promise of whole-file error recovery.
Later tokens are unexamined and must not contribute speculative feature flags.

### 5.4 Propagating a linter failure into the parser report

When linting fails in document ingestion, publish a parser report identifying
the linter as the failed stage and copy the linter's error location directly
into the parser report's failure-point field. Preserve its availability state
and prospective output-relative line/column unchanged, even if the linted buffer
was discarded. This is the same field used for the point of a structural failure;
do not require callers to find the location only in a nested linter report.

Leave the parser report's structure-start location unavailable. The linter has
no JSON awareness, so it cannot identify a failed structure's beginning. Do not
copy the linter location into both fields or manufacture a root/opening location.
Structural completion remains unexamined. Retain the linter cause, findings and
failure-before-output indication alongside the location.

| Failure stage | Parser structure-start location | Parser failure-point location |
| --- | --- | --- |
| Linter decoding failure before any output | Unavailable | Linter's prospective (1, 1), with failure-before-output recorded |
| Linter failure after output | Unavailable | Linter's retained error location |
| Linter failure without a meaningful text position | Unavailable | Unavailable, matching the linter report |
| Structural failure | Beginning of the immediately failed structure | Point where completion failed, including EOF when applicable |

Reset both location fields for every new operation so that a linter failure
cannot retain a structure-start position from an earlier parse.

## 6. Stage responsibilities

### Linter

Apply the supported encoding/conversion rules, produce bounded valid UTF-8 and
record source and transformation findings. Track output-relative positions
while emitting. Preserve useful decoder evidence without exposing original
byte offsets. Distinguish inability to decode from policy rejection of a form
that the implementation could otherwise handle.

The linter is independent of JSON syntax. It has no string, name, quote, comment
or structural context and does not validate, decode or generate escapes. The
document call path selects normalization of all supported line-break forms to
LF; the linter applies that transformation uniformly throughout the text.
Backslashes and escape spellings remain ordinary text at this stage.

No malformed or unsupported byte sequence becomes acceptable simply because
a broad feature group is allowed. An undefined byte encountered on the CP1252
decoding path is a hard linter failure; do not substitute a replacement character.
There is no policy option to permit this substitution. This rule concerns bytes
being interpreted as CP1252, not a blanket prohibition of those byte values
within otherwise valid multibyte UTF-8 sequences. Source-encoding acceptance is
checked after successful parsing, not used to disable a supported decoding path.

### Structural check and shared lexer

The parser's shared lexer and structural pass validate escape spelling within
strings and names, including their unquoted forms. The parsing stage decodes
those escapes, and the writer generates appropriate escapes for string/name
content. Outside strings/names, JSON structural characters must be literal;
escape spellings cannot stand in for delimiters or separators. For example,
`\u007B` cannot open an object, though it can represent a brace in string content.
Comments do not interpret escapes either.

Accept raw LF as content within both double-quoted and single-quoted strings.
This includes every source line-break form normalized by linting. Continue
tracking output-relative line/column positions across those breaks while
retaining the opening quote location for an eventual unterminated-string error.
Record a presence finding for raw quoted line breaks; they are not structural
failures. Any caller policy exclusion is evaluated only after successful parsing.
Escaped line-break content remains valid and is distinguished from raw source
breaks. Physical breaks still terminate unquoted candidates. Member names cannot
contain line breaks, including line endings obtained from escape decoding.
The structural pass checks this name rule while validating escape spellings,
without allocating a decoded name. It reports the name's start and the literal
break or escape that produces a forbidden line ending. This inspection does not
reinterpret decoded characters as delimiters or reclassify numeric candidates.

Validate the supported superset grammar independently of numeric range and
document-construction feasibility. Identify lexical/relaxed/Morphic numeric
features and retain current and element-start positions. Continue across supported
features regardless of caller exclusions; retain findings for the later policy
decision. Report malformed structure before any feature-policy rejection.

The lexer and parser must share the grammar, feature identities and coordinate
rules. Implement the following unquoted-value rules consistently in both stages;
the existing identifier-only scanner is not the target grammar.

#### Agreed unquoted-string boundary rule

An unquoted candidate is delimited by an unescaped JSON structural character
or JSON whitespace, rather than restricted to the former ASCII identifier
grammar. JSON structural punctuation is `{`, `}`, `[`, `]`, `:` and `,`;
an unescaped double quote also terminates the candidate. JSON whitespace is
space, tab, carriage return and line feed.

An escape within the candidate protects the escaped character from acting as
a terminator. An escaped quote is accepted within the unquoted string; the
same boundary principle applies to escaped structural punctuation and escaped
whitespace. Escape recognition must precede delimiter recognition. Only the
standard JSON escape repertoire is accepted: `\"`, `\\`, `\/`, `\b`, `\f`,
`\n`, `\r`, `\t` and `\uXXXX`, with paired Unicode escapes for supplementary
characters. Hexadecimal character escapes mean `\uXXXX`, not `\xNN`.
For punctuation without a short escape, use its Unicode escape: for example,
`\u002C` for comma and `\u0020` for space. `\,` and `\ ` are not added.
An apostrophe is ordinary content in an already-started unquoted candidate
and does not need escaping. The single-quoted mode described below exchanges
the quote roles and admits `\'` within that mode.
Invalid or incomplete escapes are structural errors. Decoded punctuation is
string content and must not be reinterpreted as a structural delimiter.

Leave the terminating character for the structural grammar to process. Ending
a candidate does not establish that its terminator is legal in that context.
For example, a comma may validly separate values, whereas an adjacent unescaped
double quote ends the unquoted candidate and produces a structural failure:
the quote is outside the candidate and is processed as JSON structure. Adjacent
string tokens are not concatenated or accepted without the required separator.
Do not treat that quote as ordinary unquoted content.

The unquoted domain is more limited than the quoted domain: delimiters which
can occur directly within a quoted string require escaping in an unquoted
candidate. Escape decoding never joins two candidates separated in the linted
text.

#### Value classification and names

In value position, consider an unquoted candidate for keyword and numeric
meaning before interpreting it as a string. Complete `true`, `false` and `null`
keywords keep their JSON meanings; valid numeric spellings keep their numeric
meaning and applicable feature flags. A recognized numeric spelling must not
become a string merely because the caller disallows its feature or conversion
exceeds the available range.

Malformed numeric-looking candidates are strings. For example, `1e+` and
`123abc` fall back to unquoted strings and are subject to the unquoted-string
permission; they are not structural numeric errors. Classify the complete
candidate rather than accepting a numeric prefix and treating the remainder
as an adjacent token. Do not publish numeric-feature findings for an abandoned
numeric interpretation of a string. A complete, well-spelled number outside
the available range remains a numeric conversion failure, not string fallback.
Keyword and numeric classification examines the complete source spelling before
escape decoding. If the candidate is classified as a string, decode its escapes
without subsequently reclassifying the decoded content. Thus `tr\u0075e` is
the string `true`, and `\u0031` is the string `1`, not a boolean or number.

In name position, the result is always a string. JSON string restrictions,
rather than an identifier naming convention, govern names. Unquoted syntax is
a relaxed feature; single-quoted syntax is a Morphic feature. Names which
resemble keywords or numbers are not converted to those value types.

JSON has no separate identifier alphabet for member names: they use string
syntax. In double-quoted JSON names, double quotes, backslashes and U+0000 through
U+001F require escaping; spaces, punctuation and Unicode are otherwise available.
JSON permits escaped line endings in names. The agreed Morphic prohibition on
newlines in names is therefore an additional restriction on decoded content,
not a restriction imposed by JSON. Apply it to quoted and unquoted names alike;
an escape must not bypass it. Newline-suppression metadata applies to string
values, not names. Retain the agreed unquoted-token boundaries, single-quote
mode and logical-NUL storage rules. Here, a forbidden newline is any scalar
line-break form listed in section 5.1, including escaped VT, FF, NEL, LS and PS.
This is a name-content rule across the document set: direct live name admission
and checked baked validation must enforce it as well as the parser. Ordinary
non-newline control values admitted through valid JSON escapes remain subject
to the existing canonical string admission rules.
[RFC 8259, sections 4 and 7](https://www.rfc-editor.org/rfc/rfc8259#section-7)

An unquoted empty name is a missing name and is a structural error, for example
`{:1}`. A quoted empty name is recognized and sets an empty-name-replaced
presence flag. Represent it using the canonical non-empty name `$morphic-empty`.
This applies to both permitted quote modes. Do not add native empty-name storage
to the live model solely for this input case.

Prepopulate each initialized live name table with `$morphic-empty` at a fixed
nonzero name ID. Preserve
ID zero's existing anonymous/empty-name meaning; `$morphic-empty` is a distinct
non-empty name with a fixed nonzero ID. This name-table requirement does not
prepopulate the separate string-value domain or turn identifiers into object
members. Root conversion requires an empty root; prepopulated names and detached
nodes are not root contents and do not make it non-empty. Initialization must
establish both the root and required name entry or fail without publishing a
partially initialized document.

Initialization, reset/reinitialization, copying and promotion must establish
the same fixed live IDs before admitting arbitrary names. `$morphic-empty` is
the only required canonical identifier; do not retain historical recovery
protocol names as prepopulated identifiers. Baked documents can omit it when
unreferenced and need not preserve its live ID. Promotion restores the fixed
live-table entry even when it was absent from the baked source.

The replacement participates in ordinary object-member collision handling.
Multiple empty names, and an explicit name equal to the replacement string,
collide in the same way as any other repeated name, preserving the established
encounter order and the collision-extension behaviour below. Do not synthesize
unique suffixes or
give the replacement privileged collision semantics. A replaced empty name is
a semantic observation, not a structural defect or proof of a non-JSON source
feature. Replacement does not by itself provide reversible original-name
metadata for later writing.

#### Ordinary arrays and explicit collision extension

The direction is to replace the recovered-array representation with ordinary
arrays and a separate public operation that supports array extension on name
collision. Both the parser and ordinary document users can select this operation.
Normal insertion continues to reject collisions. Do not silently change existing
insertion into accumulation or make this a parser-only facility.

The retired baseline used a distinct recovered value kind and a `$morphic`
wrapper. Neither survives in the target contract; this includes empty, singleton
and nested recovered arrays as well as collision-created ones.

An existing ordinary array can be extended regardless of how it was created.
The operation therefore does not need to distinguish an authored array from
one formed after a collision, or retain collision-origin metadata through
baking, promotion or text round trips. Apply the following rules to each
collision as it occurs; do not regroup the entire member set afterwards.

Documented operation semantics:

- Without a matching member name, insert the incoming member normally.
- With a matching non-array value, retain the member's name and position and
  replace its payload with an ordinary array containing the old payload followed
  by the incoming payload. If the incoming value is an array, it remains one
  complete element: existing `1` and incoming `[2,3]` produce `[1,[2,3]]`.
- With a matching array and an incoming non-array, append the incoming payload
  to that top-level array.
- When both payloads are arrays, create a new ordinary array whose two elements
  are the entire existing array and the entire incoming array, in that order.
  Do not splice either array's children. Further incoming non-array values append
  to this new top-level array; a further array collision wraps again.

Thus `{"x":[1,2],"x":3,"x":[4,5],"x":6}` becomes
`{"x":[[1,2,3],[4,5],6]}`. By comparison, `{"x":1,"x":2,"x":3}` becomes
`{"x":[1,2,3]}`. The receiving array is a normal array throughout subsequent
document operations and serialization.

This interpretation of duplicate names is a relaxed parser feature. Set its
presence flag when a collision occurs, including after canonical empty-name
replacement. The default acceptance mask excludes it. Process the entire document
and report later structural failures before rejecting on this feature policy.
The explicitly selected public live operation remains usable independently of
parser policy. The behaviour is intentionally encounter-order dependent; document
it rather than adding strict collision-shape restrictions or provenance tracking.

Implementation requirements across the document set:

- Design the operation's name, accepted inputs and result/ownership contract
  using existing live-document conventions. Ordinary insertion, renaming and
  reparenting do not gain implicit collision extension; a caller must explicitly
  select the new operation.
- Preserve receiving member position and input order, and require failed
  extension to leave both inputs unchanged.
- Remove the recovered value kind, its anonymous-only child rule, text wrapper,
  reserved-name escaping and recovery-specific statuses. There are no consumers
  beyond existing tests, so retain no legacy reader or compatibility conversion.
- Replace tests for old recovery cardinalities and wrappers with ordinary-array
  collision tests. Do not reinterpret old baked recovered-array type tags as a
  different current type; use the appropriate format validation/version boundary.
- Treat former `$morphic` wrapper shapes and dollar-prefixed names as ordinary
  data, without protocol interpretation or name unescaping.

Replace recovery-related findings, parser statuses, APIs, writer behaviour and
tests together. Verify that normal insertion rejects the same
duplicate accepted by explicit extension and that the parser uses the public
operation. No implementation or commit is authorized by this specification work.

#### Root shape: explicit containers or inferred container bodies

Standard JSON permits any single value at the root: object, array, string,
number, boolean or null. The root itself has no member name. The existing
Morphic document model instead has an implicit object root. These are different
scope choices; JSON does not require that root to be an object.
[RFC 8259, sections 2 and 3](https://www.rfc-editor.org/rfc/rfc8259#section-2)

The document root is an anonymous object or an anonymous array. Select the
container from the source as follows, after skipping whitespace and supported
comments. The order is significant:

If no token remains, create the empty object described below. Otherwise:

1. `{` begins an explicit object root.
2. `[` begins an explicit array root.
3. Otherwise, if the first item is named, treat the entire document as the
   body of an implicit object.
4. Otherwise, treat the entire document as the body of an implicit array.

Determine whether the first item is named by its member syntax: a name token
followed by a structural colon, allowing intervening trivia. Do not infer this
from identifier-like spelling, quotedness, or keyword/numeric appearance alone.
For example, `123: true` has a string name, whereas `123` supplies an array value.
Escaped colons within a token are content, not member separators.

| Source | Resulting root interpretation |
| --- | --- |
| `{"a":1}` | Explicit object |
| `[1,true]` | Explicit array |
| `"a":1,"b":2` | Implicit object containing both members |
| `42` | Implicit array containing the number 42 |
| `true` or `false` | Implicit array containing one boolean |
| `null` | Implicit array containing one null value |
| `"hello"` | Implicit array containing one string |
| `1,true,"hello"` | Implicit array containing three values |

Apply the selected container's ordinary structural rules to the entire body;
inference does not waive required separators or permit adjacent values. Once
an explicit root is selected, trailing content does not retroactively turn it
into the first item of an implicit array. The explicit container must comprise
the complete document, apart from surrounding trivia.

Present empty or whitespace-only input produces an empty object root. Comment-only
input also produces an empty object, subject to comment acceptance. Handle these
cases before otherwise-array inference; comments still set their finding and
default policy rejects them after otherwise-successful parsing. An absent input
view still fails. Do not label the no-content case as an implicit array.

Record inferred roots without confusing representation with source permission.
A valid single JSON scalar wrapped in the internal array is an informational
representation adaptation and does not require relaxed permission. Multiple
top-level values and non-empty unbraced object bodies are relaxed syntax. Empty
or whitespace-only input does not acquire an unbraced-object relaxation merely
because its agreed result is an empty object. Comment-only input still requires
comment permission. Exact flag names remain an interface-design detail.

Ordinary object/array semantics apply at the root, including anonymous array
elements, named object members and the existing named-value normalization rules.
A root array is standard JSON syntax and must not require a Morphic or relaxed
feature bit merely because the former model accepted only object roots.

Singleton-object normalization applies only to an anonymous object directly
inside an ordinary array: after its members are constructed, unwrap a single
named member with its complete payload and metadata. Empty and multi-member
objects remain objects. Never unwrap the document root, including a singleton
explicit object root. With recovered arrays removed, there is no special
competitor context exempt from this ordinary rule.

This is a document-model change as well as a parser change. Root kind is retained
through baking, validation, promotion and writing. Keep the root anonymous and
parentless. Programmatic construction defaults to an empty object. Object-to-array
and array-to-object conversion are permitted only while the root is empty.
The parser selects root kind before constructing its contents.
Root erasure removes its contents while preserving root kind. Document reset
restores the default object root and the prepopulated live name entry. Do not
conflate these operations. A request to change a non-empty root's type fails
without changing the document.

The current baked format requires an object root and supports the retired
recovered kind. Revise its validation and format version as needed for array
roots, per-value string metadata and recovery removal. No legacy recovery
compatibility is required. Do not silently reinterpret incompatible old records;
exact layout/version choices belong to implementation review.

#### Single-quoted strings and comment boundaries

Single-quoted strings are supported alongside double-quoted strings. In this
mode the roles of single and double quotes are exchanged: an unescaped single
quote closes the string, `\'` embeds a single quote, and a double quote is
ordinary content requiring no escape. The non-quote JSON escapes, including
Unicode escapes, remain available; the delimiter escape changes from `\"` to `\'`.
Encountering single-quoted syntax sets a Morphic-extension presence flag,
including when it supplies a name. It is not grouped with relaxed syntax.

Within an already-started unquoted candidate, an apostrophe is ordinary content:
`don't` does not need quoting merely because it contains an apostrophe. A single
quote at the start of a quoted token still selects single-quoted mode.
An unescaped double quote within an unquoted candidate terminates it, remains
outside its content and causes a structural failure as described above. This
does not change the ordinary-content treatment of an apostrophe inside that
candidate or the exchanged delimiters of a single-quoted string.

Comments are supported as an extension: `//` and `;` introduce a comment through
the next line break. Block comments use the C-style delimiters `/*` and `*/`.
Comments are recognized at a token boundary before starting an unquoted
candidate; once that candidate has started, their markers are content.

Block-comment markers are recognized only outside strings and names; inside
them they are content. Thus a block marker does not interrupt an already-started
unquoted string/name. Quoted content likewise cannot open or close a comment.
Outside strings/names, recognize and discard the block comment as syntax trivia;
do not add its contents to document values. Preserve the comment-presence finding
and advance diagnostic coordinates through the discarded text. Discarding comment
content during parsing does not rewrite the linter-output coordinate space.
The same outside-content rule applies to `//` and `;`: within an already-started
unquoted string they are content, not comment introducers. For example,
`alpha//beta` and `alpha;beta` are complete string candidates. Names likewise
remain strings; comment recognition does not interrupt their content.
Quotes within recognized comment text do not start strings. Diagnostic line
recognition and comment termination follow the same line-break rules.

The standard baseline for escapes, string-valued names and JSON grammar is
[RFC 8259, sections 2, 4 and 7](https://www.rfc-editor.org/rfc/rfc8259).

### Parser

Construct privately through existing live-document operations. Perform numeric
conversion and semantic interpretation, report relevant findings and construction
failures, and combine earlier-stage observations without losing provenance.
Apply remaining acceptance requirements before publication. Use the public
collision-extension operation, record its relaxed-feature finding and retain
ordinary singleton normalization. Remove recovery-wrapper interpretation and
reserved-name unescaping.

Provide a coherent way to carry linter findings into the final parse result;
bare UTF-8 alone cannot reveal the source encoding or overlong-NUL provenance.
Retain separately usable low-level stages without requiring an entire lint
result or source-to-output map to remain alive for diagnostics. The exact
options/report composition and convenience entry point are design details to
review; avoid needless repeated structural scans.

### Per-string newline writing metadata

The writer escapes embedded newlines by default. A metadata option on each
string suppresses that newline escaping, allowing literal multiline output for
content such as shader source. The option controls output spelling, not the
stored string value, and does not suppress other required string escaping.
It is independent of the parser's source-feature acceptance mask.

The option belongs to each string value occurrence, not to shared interned text.
Two values with identical decoded bytes must be able to retain different
suppression settings when one came from literal source breaks and the other
from escapes. Deduplicating the text must not merge those settings. Preserve
the setting when moving/copying the complete value or transferring its payload.

The parser automatically sets newline-escaping suppression when a source string
contains at least one literal line break, observed in linted text before escape
decoding. A string containing only escaped line endings retains newline escaping,
even though its decoded value contains line endings. A mix of literal and escaped
line endings triggers suppression for the string as a whole. No explicit caller
action is required. Preserve the per-string observation during parsing rather
than inferring suppression from the decoded value or a document-wide finding.
Keep writing metadata separate from the report's raw-source-line-break presence
flag. Escaped line endings alone set neither suppression nor that raw-break flag.
A string containing literal backslash and `n` characters likewise does not
acquire suppression on that basis.

Represent the option in live and baked documents and preserve it through baking,
baked validation, promotion and document copying. Include its storage in the
baked compatibility review. Strings without the option retain default escaped
output; strings with different settings can coexist in one document.

Member names cannot contain newlines and do not use this option. Suppression
covers all newline forms. The writer normalizes every newline to LF, including
embedded values originally produced by escape decoding and formatting newlines.
Use the same complete line-break repertoire as the linter, recognizing CRLF/LFCR
as compound breaks before their constituents. Emit normalized LF literally when
suppression applies and as `\n` otherwise. Other escapes retain their normal rules.
Do not mutate the stored document while writing. Text round trips preserve
newline-normalized content rather than original CR/CRLF or other break spelling.
Strict JSON output overrides newline-escaping suppression. JSON requires LF
(U+000A) and CR (U+000D), among U+0000 through U+001F, to be escaped within strings.
Because all writer newline forms first normalize to LF, strict output always
uses `\n` for embedded breaks, including when suppression metadata is set.
Morphic output honors suppression and may emit literal normalized LF. Neither
mode changes stored metadata. JSON permits some other Unicode separators
literally, but that does not change this rule after LF normalization.
[RFC 8259, section 7](https://www.rfc-editor.org/rfc/rfc8259#section-7)
Parsing derives the option from literal line breaks in the source spelling
rather than recovering an independently serialized metadata value. Ordinary
document transformations must preserve it.

Consequently, writing strict JSON and reparsing it produces strings with
suppression unset: the new source contains escaped-only newlines. That does not
change metadata in the original document. More generally, text round trips
reconstruct source-derived settings rather than promising arbitrary metadata
identity; a suppression bit on a string without line breaks has no text spelling
to preserve it. Compare normalized content and the settings implied by the
emitted source. Binary baking/promotion retains metadata exactly.

All layout newlines, including pretty-print and trailing newlines, are literal
LF outside strings. Retire the writer's current selectable CRLF formatting mode;
it cannot override the LF-only requirement. Preserve the options that control
whether layout newlines are emitted.

## 7. Mapping from the current interfaces

| Current surface | Intended change |
| --- | --- |
| `CTextLintLocation` | The same shared location representation used for parser structure-start and failure-point fields, including availability; remove its three byte-offset fields and source-coordinate meaning. |
| `CTextLintReport` / `CTextLineMetrics` | Preserve aggregate statistics and bounded output length; add or reorganize findings and explicit outcome/progress as required. |
| `CTextLintFailure` | Preserve specific decoder/failure evidence and output-relative location; identify failure before any emitted code point. |
| `ERelaxation` / `ENumericExtension` | Shared grouped feature identities and masks; add agreed missing features rather than duplicating stage enums. |
| `CDocumentStructureReport` | Structural reason, required element-start and detection positions, retained findings and completion; remove public byte offset. Keep useful construction estimates separate from public diagnostic reporting. |
| `CDocumentParseInterpretations` | Presence findings for supported interpretations, including relaxed collision extension and singleton normalization; retire recovery protocol findings. |
| `CDocumentParseReport` | Composed findings, processing/acceptance distinction and separate structure-start/failure-point locations. A linter failure copies its location into failure point and leaves structure start unavailable. Preserve destination on failure/rejection. |
| Live/baked string representation and writer | Per-string metadata to suppress newline escaping; preserve it through baking, validation, promotion and copying, with compatibility review. |
| `CDocumentWriteOptions` / `CDocumentWriteReport` | Retire CRLF selection and obsolete recovery/name-escaping counters; keep remaining writer options and statistics. Strict JSON overrides newline suppression; every emitted break normalizes to LF. |

Audit all production and test consumers of removed fields, including generic
linter callers. Do not silently change their newline or encoding defaults to
achieve a document-specific policy. Framework allocation, no-exception behaviour,
bounded NUL handling and existing codebase style continue to apply.

## 8. Validation requirements

- A strict-ASCII policy accepts ordinary ASCII JSON and escaped Unicode, but
  rejects literal non-ASCII source under the agreed encoding policy.
- UTF-8, modified UTF-8 and CP1252 cases produce consistent permitted/rejected
  outcomes and accurate source findings, independent of output encoding.
- Each individual relaxed/Morphic feature sets its bit. Repeating it does not
  change parser findings into counts. Group masks contain exactly their category.
- Preserve regression checks for linter aggregate statistics, including repeated
  occurrences and line metrics. Retaining those measurements must not affect the
  presence-only parser findings or the common diagnostic-coordinate rules.
- Rejecting a well-formed extension is policy rejection, not structural failure.
  Structural acceptance of an out-of-range number remains possible.
- Combine a disallowed feature with a later structural error and require the
  structural failure, not early policy rejection. Combine disallowed features
  with conversion/construction/resource failures and preserve those outcomes.
  Only otherwise-successful parsing may produce policy rejection.
- Undefined CP1252 bytes fail without replacement even under the broadest
  acceptance mask. Valid UTF-8 containing the same byte values as continuation
  bytes is not rejected by a CP1252-specific rule. Disallowed but decodable
  CP1252 source must still reach structural checking and parsing.
- Verify the conservative default accepts the agreed Morphic numeric forms but
  rejects relaxed syntax only after otherwise-successful parsing. Comment-only
  input constructs an empty object internally and is accepted only when its
  encountered features are allowed.
- Test literal NUL, modified NUL, escaped NUL and physical terminators separately;
  normalization and fallback must preserve the correct provenance.
- Verify CP1252 and exact modified-NUL source are accepted by default while
  undefined CP1252 still fails and unrelated relaxed syntax remains excluded.
- Verify SuiteUTF-based CESU normalization for supplementary boundary scalars
  and mixed ordinary UTF-8/CESU/modified-NUL input. Require standard four-byte
  UTF-8 output per valid pair, separate source findings and one output column
  per supplementary scalar. Test truncated, isolated, reversed and overlong
  surrogate spellings without admitting them as CESU compatibility input.
- Verify direct live entry and checked baked validation reject CESU bytes, and
  that baking, promotion and writing never produce them. The linted equivalent
  must enter documents successfully. Retain ordinary JSON surrogate-escape tests
  and the separate canonical modified-NUL storage policy.
- Exercise collisions in encounter order: array/array wrapping, subsequent
  non-array append, repeated wrapping, non-array promotion and duplicate empty
  names. Verify the parser uses the same public extension operation as callers;
  normal insertion still rejects duplicates. Collision policy rejection follows
  otherwise-successful parsing and cannot mask structural failures.
- Verify legacy recovery wrappers and reserved-name spellings receive no special
  interpretation. Remove recovery-kind round-trip expectations and ensure invalid
  old baked types cannot be silently treated as a different current type.
- Exercise every unquoted delimiter and JSON whitespace character, both escaped
  and unescaped. Verify that escaped quotes/punctuation remain within the
  candidate and that an unescaped terminator is subsequently checked in context.
- Verify single-quoted mode with plain double quotes, escaped single quotes
  and its Morphic flag; an apostrophe inside an unquoted candidate remains
  content. Exercise both line-comment introducers and the agreed block markers
  inside and outside strings/names, without changing quoted content.
- Verify matched line/column results across all stages for ASCII, multibyte
  UTF-8, supplementary scalars, combining marks, tabs, every supported line-break
  form, BOM stripping and CP1252 conversion. Test rewriting against the shared
  LF output space, including compound breaks and adjacent distinct breaks.
- Verify that lint normalization is independent of quotes, comments and JSON
  structure, and leaves escape spellings uninterpreted. The structural pass
  accepts raw line breaks in both quoted string modes after normalization and
  records their presence; escaped spellings remain valid. Escape decoding must
  reproduce the intended value without changing diagnostic coordinates. Breaks
  within comments or between tokens remain legal. Escaped punctuation is string/name content and
  must never act as a structural delimiter or separator outside those contexts.
- Exercise multiline shader-source strings containing quotes, comment markers
  and structural punctuation as content. Verify normalized LF values and
  diagnostic positions after embedded breaks, including an unterminated string.
  Policy exclusion of raw quoted breaks must not hide later structural errors.
- Verify default newline escaping and per-string suppression in one document;
  preserve other required escapes and the underlying string value. Verify
  metadata preservation through live copying, baking, validation and promotion.
  Verify automatic suppression for literal and mixed literal/escaped line
  endings, and no suppression for escaped-only line endings or literal
  backslash-`n` content. Cover parse/write/parse round trips and strings with
  identical decoded values but different source spelling and suppression metadata.
  Ensure escaped-only breaks do not set the raw-break finding and one string's
  literal breaks do not affect another string's metadata.
  Require these independent settings even when both values share interned bytes.
  Verify strict JSON overrides suppression, emits `\n` after normalization and
  leaves metadata intact; subsequent Morphic output must still honor suppression.
  Reparse strict output and require suppression unset in the new document while
  preserving normalized content and the original document's metadata.
- Verify opening-element and detection locations for nested mismatches,
  unterminated strings/comments, malformed escapes, missing separators
  and EOF. Ensure an inner error does not point to the outer root.
- Verify the composed parser report copies a linter failure's location and
  availability unchanged into failure point and leaves structure start unavailable.
  Cover first-code-point failure, normalized output prefixes, discarded output,
  and locationless failures. Reuse a report after a structural failure to ensure
  a later linter failure cannot expose a stale structure-start position.
- Verify complete-candidate numeric recognition and malformed-number fallback
  to strings under both accepting and rejecting unquoted-string policies.
  Well-spelled out-of-range numbers must not take that fallback, and failed
  numeric interpretations must not set numeric-feature bits for string content.
- Verify classification before escape decoding: escaped spellings that decode
  to keyword or numeric text remain strings and do not acquire keyword/numeric
  meaning or numeric-extension findings through a second classification.
- Verify that a missing unquoted name is structural failure, while quoted empty
  names set the replacement flag and use the fixed name. Cover repeated empty
  names, an explicit matching replacement name and both collision orders.
- Verify canonical identifiers, including `$morphic-empty`, have fixed live name
  IDs regardless of user-name insertion order and after initialization, reset,
  copying and promotion. Preserve anonymous ID zero and check baked-to-live name
  remapping; fixed live IDs are not a fixed baked-index requirement.
  Verify unreferenced `$morphic-empty` can be omitted during baking and is restored
  on promotion with its fixed live ID.
- Verify names reject literal and escape-decoded line endings in every supported
  name syntax. Cover JSON-valid spaces, punctuation, escaped quotes/backslashes
  and non-ASCII names without imposing an identifier alphabet.
  Check the same no-newline invariant for direct live name entry and baked
  validation. Other valid escaped name characters and established NUL admission
  remain supported.
- Verify object-root construction by default and conversion in both directions
  when the root is empty. Reject either conversion for a non-empty root without
  changing the document.
  Detached nodes and prepopulated strings do not make an empty root non-empty.
- Cover empty/non-empty explicit object and array roots, implicit object members,
  implicit array bodies, every singleton scalar kind, trivia-aware first-member
  detection, present empty input and the agreed root initialisation/clear behaviour.
  Verify that bake, checked validation, promotion and writing preserve root kind,
  with no named wrapper and no extension flag solely for an explicit array root.
  Reject missing separators and trailing content after an explicit root rather
  than reinterpreting the document under a different inferred root kind.
  Keep explicit singleton object roots as objects; apply singleton unwrapping
  only to the specified anonymous object elements of arrays.
- Verify empty and whitespace-only input yield an object. Erasing an array root
  preserves array kind; resetting the document restores an object root.
- Exercise all writer newline forms in escaped and suppressed strings and in
  formatting. Require LF normalization, including compound CRLF/LFCR, without
  changing the stored document. Compare text round trips against normalized values.
- A first-code-point decoding failure reports prospective position (1, 1) and
  failure-before-output. Failures after a prefix retain prospective positions
  despite buffer disposal. Preserve meaningful resource-failure cursors as well.
- Findings survive later failure, while stage coverage accurately describes
  what was examined. An abandoned decoding attempt cannot corrupt final flags.
- Accepted results publish once; rejected/failed results preserve the previous
  destination and release temporary allocations. Preserve source-alias safety.
- Retain applicable semantic round-trip, deep-nesting and resource tests;
  replace retired recovery tests with ordinary collision-array coverage and
  parser counter assertions with behaviour and presence checks.
  Run affected suites and ordinary Debug/Release x64/Win32 validation, plus
  policy and line-ending checks, after implementation.

## 9. Implementation progress and remaining sequence

### 9.1 Completed stage 1: linter and shared diagnostics

Implemented and reviewed on 11 September 2026.

- Added shared `CTextLocation` with explicit availability and 1-based line and
  code-point column in emitted UTF-8. Migrated linter, lexer, structural and
  parser diagnostics to these coordinates; public diagnostic byte offsets are
  removed. Structural/parser reports retain separate `structure_start` and
  `failure_point` locations under the existing grammar.
- Added grouped `ETextSourceFinding` identities for source encoding, raw
  observations and decoder issues. Retained linter aggregate statistics and
  line-ending masks, with a separate CESU-pair count. Parser presence findings
  and caller acceptance masks remain stage 2.
- Used SuiteUTF to normalize exact modified NUL and valid CESU-8 pairs to
  canonical UTF-8. Malformed CESU forms are rejected on that decoding path;
  eligible unmarked input may still select the separately reported CP1252 path.
  Undefined CP1252 bytes fail without replacement. Abandoned UTF-8 evidence is
  retained separately from terminal failure and the adopted path's statistics.
- Added document ingestion with every supported line-break form normalized to
  LF, independently of quotes, comments and escapes. Generic lint callers retain
  their existing default newline mask.
- Preserved prospective terminal-failure locations through output disposal and
  resource failures, including explicit failure-before-output. Composed ingestion
  copies the linter failure location unchanged to the parser's failure point,
  leaves structure start unavailable and marks structure `unexamined`.
- Added direct byte-view and string-view entry paths for linting and ingestion.
  Null views report absent input; a present zero-length `CStringView` succeeds.
  No byte-to-string view conversion is required. Destination preservation and
  source-alias safety remain covered.
- Consolidated linter parsing state in an internal class and buffer position in
  a source cursor, reducing `decode_attempt` to its decoding-mode argument.
  Grouped static non-member helpers before member definitions in the linter
  and parser, incorporating the final style review.
- Updated the implemented semantic contract and regression tests. Debug and
  Release solution builds and ordinary tests passed for x64 and Win32: 561
  TextLinter, 1,247 DocumentStructure and 15,150 DocumentParser checks per build,
  with the other ordinary suites also passing. Policy validation reported zero
  errors and warnings with the existing negative-test suppression. The final
  helper-only reorder also passed a Debug x64 build; diff and line-ending checks
  passed.

The existing parser grammar, interpretation counters, structural estimates,
recovery protocol and live/baked/writer model remain the baseline. Completion
of stage 1 does not imply implementation of the full requirements or validation
matrix above. The [semantic specification](../data_model/revised_data_model.md)
describes the current behaviour; the [milestone record](../project/completed_milestones.md)
records this completed slice separately from the first pipeline.

### 9.2 Remaining stage 2, after progression instruction

1. Prepare the remaining public report/options and document API shapes. Complete
   grouped parser findings and caller policy definitions, preserving the source
   findings and shared locations already implemented. Update semantic
   documentation alongside each implemented replacement contract.
2. Implement the agreed object-or-array root contract across the live and baked
   model, translation and writing, together with per-string newline-escaping
   metadata, the fixed live name entry and the public collision-extension operation.
   Include format review, root-kind, newline and metadata-preservation tests.
   Review before committing this infrastructure slice.
3. Refactor the shared lexer and structural check for grouped findings,
   separate capacity estimates and the agreed superset grammar. Extend the
   implemented location rules to the new grammar. Review before committing.
4. Refactor parser interpretations, report composition and acceptance/publication;
   use public collision extension and retire recovered-array kinds, protocol
   handling and compatibility across all affected code and tests together.
   Complete end-to-end tests and remove obsolete parser counts. Preserve the
   implemented shared locations and linter aggregate statistics.
   Review before committing.

Keep intermediate changes buildable. A small coordinated migration is preferable
to retaining permanent duplicate report APIs. This sequence proposes review
boundaries for the remaining work, not permission to begin stage 2.

## 10. Decision record and implementation review

The seven original requirements questions and the subsequent collision discussion
are settled. This table points to the final contracts; superseded discussion
proposals are not alternative implementation requirements.

| Original question | Agreed contract |
| --- | --- |
| 1. String grammar | Shared unquoted boundaries; classification before escape decoding; exchanged quote roles in single-quoted strings; comments recognized only outside already-started names/strings. Section 6 defines these rules. |
| 2. Modified UTF-8 | SuiteUTF normalizes exact modified NUL and valid CESU pairs only at linting. Direct document admission and output reject CESU bytes. Canonical logical-NUL storage remains exact `C0 80`. Sections 4.1 and 4.2 define the boundaries. |
| 3. Undefined CP1252 | Hard decoding failure; no replacement or acceptance override. Sections 3 and 6 define policy timing and decoding. |
| 4. Coordinates, names and newlines | 1-based output-relative coordinates; both structural locations; no JSON awareness in the linter; no newlines in names; per-value suppression derived only from literal source breaks. Writer normalization is LF-only and strict JSON overrides suppression. Sections 5 and 6 define the details. |
| 5. Acceptance | Late rejection only. Default accepts ordinary UTF-8, modified NUL, CESU, CP1252 and the specified Morphic numeric forms; excludes relaxed features including collision extension. Scalar-root adaptation is informational. Section 3 and the findings inventory define this separation. |
| 6. Statistics | Retain linter aggregates; replace parser occurrence counts with presence findings. Useful construction estimates remain separate from public diagnostics. Section 4.3 defines retention. |
| 7. Names and roots | Fixed live `$morphic-empty` entry, optional unreferenced baked entry; root kind changes only when empty; erasure preserves kind and reset restores an object. Empty input produces an object. Section 6 defines names and root selection. |

Collision extension uses ordinary arrays through a separate public operation;
normal insertion still rejects duplicates. Process collisions in encounter order
with the case rules and examples in section 6. Retire all recovery-array
compatibility, protocol handling and associated tests.

The consistency review makes these consequences explicit:

- Linter and parser locations share one representation. A linter failure uses
  the parser report's failure-point field, with structure start unavailable.
- Empty roots can change type even when detached nodes or prepopulated strings
  exist; those are not root contents.
- Shared interned bytes do not imply shared suppression metadata. Binary document
  transformations preserve each value's setting; text reparsing derives a new
  setting from the emitted spelling.
- Strict JSON writing preserves the original document's metadata but reparsing
  its escaped-only output does not restore suppression.
- Modified-NUL normalization must not introduce a new raw-control permission
  requirement that contradicts the unchanged NUL policy.
- LF-only output retires selectable CRLF formatting. Removing recovery handling
  also removes its writer counters despite retaining other writer statistics.
- An explicit object root is never removed by singleton-object normalization.

Remaining stage-2 design covers parser/document API names and ownership/results,
parser feature-bit assignments and policy preset names, placement of per-value
metadata, baked layout/version changes, and useful internal capacity estimates.
Stage 1 has settled the implemented source findings and location APIs. The
remaining choices must implement the contracts above and remain reviewable;
they are not unresolved user-facing behaviour questions.

Stage 1 implementation and review are complete. Await Ritchie's explicit
instruction before beginning stage 2. Pause before each subsequent commit
for review.
