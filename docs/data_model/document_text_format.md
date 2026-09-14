Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   document_text_format.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   14 Sep 2026

# Document text format

## Scope

This is the normative text contract for Morphic documents: source decoding,
grammar, construction interpretation and text round trips. The
[semantic specification](revised_data_model.md) defines the document itself.
[Parsing and reporting](document_parsing.md) defines the API, diagnostics,
complete findings inventory and acceptance policy. These documents describe
the current implementation without requiring migration or backlog material.

The grammar recognises supported features independently of caller permission.
A supported extension is processed before policy is evaluated. Malformed
structure or failed construction is a processing failure, even if the source
also uses a disallowed feature. The default policy is not a strict JSON
validator: it permits the documented encoding and numeric extensions, adapts
scalar roots, accepts empty input and prohibits newlines in names.

## Source decoding and line endings

The parser first lints source bytes. It attempts UTF-8 with two compatibility
forms: exact modified NUL `C0 80` and valid CESU-8 surrogate pairs. These become
U+0000 and standard four-byte UTF-8 supplementary scalars respectively. Other
overlong encodings, isolated/reversed/truncated surrogate units and extended
UTF forms are not accepted on that path.

If UTF decoding fails and no leading BOM was recognised, the linter retries
the whole payload as CP1252. Defined CP1252 bytes are converted to UTF-8;
undefined bytes fail without replacement. A recognised leading BOM prevents
fallback. The UTF-8 BOM is stripped; BOM recognition does not provide UTF-16
or UTF-32 decoding. Malformed compatibility input may therefore succeed as
unmarked CP1252, with CP1252 findings, rather than as compatibility UTF-8.
CP1252 adoption is an inference, not proof of the author's intended encoding.

Trailing zero bytes in the **source** are stripped before decoding. Successful
**output** contains the decoded payload followed by one appended physical NUL
terminator. Its logical byte size excludes only that appended terminator.
A decoded payload can itself end in U+0000; bounded lengths distinguish content
from termination. Embedded raw source zeros remain payload.

Document linting normalises the following forms to one LF each, recognising
the compound forms before their constituents:

| Source form | Unicode code points |
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

Normalisation applies inside quotes and comments as well as between tokens.
The linter has no JSON awareness. Escape spellings are ordinary source
characters at this stage: `\r\n` is not a physical source break.

Direct live admission and baked validation do not perform CP1252 or CESU
conversion. Names and values use strict UTF-8 with the exact `C0 80` exception
for stored logical NUL. Writers emit standard UTF-8 or JSON Unicode escapes,
never CESU byte sequences.

## Containers and root selection

Objects contain named members separated by commas, with a literal colon
between each name and value. Arrays contain values separated by commas.
Containers may be empty. Nested objects and arrays require braces and brackets.
Names in arrays require object wrappers: `[name:1]` is invalid, while
`[{"name":1}]` is valid.

After leading whitespace and comments, select the root in this order:

| First content | Interpretation |
| --- | --- |
| No token | Empty object |
| `{` | Explicit object root |
| `[` | Explicit array root |
| Name token followed by a literal structural colon, allowing intervening trivia | Implicit object body |
| Anything else | Implicit array body |

The selected container's rules apply to the entire document. An explicit root
must end before any trailing trivia; further content is an error, not a reason
to reinterpret the root as the first element of an implicit array. Escaped
colons are content and cannot select an object body.

| Source | Document shape | Root-related policy requirement |
| --- | --- | --- |
| `{"a":1}` | Object with one member | None |
| `[1,true]` | Array with two values | None |
| `"a":1,"b":2` | Object with two members | `implicit_body` |
| `123:true` | Object with string name `123` | `implicit_body` and `unquoted_names` |
| `42`, `true`, `null` or `"hello"` | Array containing the single value | None |
| `1,true,"hello"` | Array with three values | `implicit_body` |
| Empty or whitespace-only text | Empty object | None |
| Comment-only text | Empty object | `comments` |

A single scalar does not set `implicit_body`. Multiple unbracketed values and
non-empty unbraced objects do. Root type is observable in the document and is
not duplicated in the findings. A trailing comma after a single unbracketed
value requires `trailing_commas` even though there is no implicit-body finding.

One trailing comma is supported after a member or array element, including
at the end of an implicit body. Leading commas, repeated commas, missing
values and adjacent values without separators fail. A comma alone does not
make an empty container.

## Whitespace and comments

Lexical whitespace is space, tab, CR and LF. Source line-break normalisation
has already converted the full repertoire above to LF; arbitrary other Unicode
space characters are not automatically token separators.

`;` and `//` start a line comment through the next line break or EOF.
`/*` starts a non-nesting block comment, closed by the first `*/`.
An unterminated block comment is a structural failure.

Comments begin only at a token boundary. Inside quoted text or an already
started unquoted candidate their markers are ordinary content. Thus
`alpha//beta`, `alpha/*beta*/` and `alpha;beta` are complete unquoted strings.
`1/*note*/` is an unquoted string, whereas `1 /*note*/` is a number followed
by a comment. Comments do not remove the need for a separator between values.

Quotes and escapes inside comments have no string meaning. Comment text does
not become document content, but its physical text still contributes to
diagnostic coordinates and the `comments` finding.

## Strings, names and escapes

Both double-quoted and single-quoted strings are supported. A token beginning
with an apostrophe selects single-quoted mode. Single quotes set `single_quotes`
for names and values alike.

| Mode | Delimiter escape | Other quote |
| --- | --- | --- |
| Double-quoted | `\"` | Apostrophe is ordinary content; `\'` is invalid |
| Single-quoted | `\'` | Double quote is ordinary content; `\"` is invalid |
| Unquoted | `\"` | Apostrophe is ordinary content; `\'` is invalid |

Every mode also admits `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t` and
`\uXXXX`. Each Unicode escape has exactly four hexadecimal digits. A high
surrogate must be followed immediately by a low-surrogate Unicode escape;
the pair produces one supplementary scalar. Isolated or reversed surrogates,
unknown escapes and incomplete escapes are structural failures. There are no
`\xNN`, `\,` or backslash-space escapes; use `\u002C` or `\u0020` instead.

Quoted strings may contain literal controls and line breaks. Raw quoted
line breaks and other raw quoted controls require their respective relaxed
permissions. Logical NUL is the exception: it does not require raw-control
permission. Delimiters and comment markers inside quoted content have no
structural meaning. Adjacent string tokens are never concatenated.

Unquoted candidates end before an unescaped `{`, `}`, `[`, `]`, `:`, `,`,
double quote or lexical whitespace. The terminating character remains for
structural processing; its presence does not guarantee that it is legal there.
For example, `alpha"beta"` fails for a missing separator.
An apostrophe inside an already-started candidate, as in `don't`, is content.

Escapes are recognised before boundaries. Decoded punctuation and whitespace
remain string content and cannot split or join tokens, supply a structural
delimiter, or be interpreted a second time. `\u007B` is a string containing an
opening brace, not an object opener.

Names always become strings, including keyword- and numeric-looking names.
Unquoted names require `unquoted_names`. A quoted empty name is present and
valid; it sets the informational `empty_member_name` finding. There is no
synthetic replacement name.

No name may contain LF, CR, VT, FF, NEL, LS or PS, whether literal or obtained
from escape decoding. This document-model restriction also applies to direct
live name admission and baked validation. Other valid escaped controls are
permitted. Newline-escaping suppression applies to string values, never names.

| Source | Meaning |
| --- | --- |
| `{"":1}` | Present empty name, unsigned integer value |
| `{"name":""}` | Present non-empty name, empty string value |
| `{"":""}` | Present empty name and empty string |
| `{:1}` | Missing-name failure |
| `{"name":}` or `{"":}` | Missing-value failure |

An empty string is distinct from `null` and from a live empty placeholder.

## Value classification and numbers

Classify each complete unquoted source candidate before decoding its escapes.
Exact, case-sensitive `true`, `false` and `null` retain their value meanings.
A complete numeric spelling retains its numeric type. Other candidates become
unquoted strings and require `unquoted_strings`. Names do not undergo value
conversion.

Decimal numbers have an optional leading sign, an integer part of `0` or a
non-zero digit followed by digits, an optional decimal point followed by one
or more digits, and an optional `e`/`E` exponent with optional sign and one or
more digits. A decimal point or exponent selects floating point.

Binary integers use `0b`/`0B` followed by binary digits. Hexadecimal integers
use `0x`/`0X` or `#` followed by hexadecimal digits. An optional sign precedes
the prefix. These forms have no fraction or exponent and require at least one
digit. Leading `+` sets `explicit_plus`; a plus in an exponent does not.
Hexadecimal `#` sets both `hexadecimal` and `alternate_hexadecimal_prefix`.

| Candidate in value position | Classification |
| --- | --- |
| `true` | Boolean |
| `tr\u0075e` | String `true` |
| `\u0031` | String `1` |
| `0`, `-1`, `+1`, `0x10`, `-#FF`, `0b10` | Integer |
| `1.0`, `-0.0`, `1e+2` | Floating point |
| `01`, `1.`, `.5`, `1e+`, `123abc`, `0x`, `0b2` | Unquoted string |
| `NaN`, `Infinity`, `1_000` | Unquoted string |

Abandoned numeric classification contributes no numeric feature findings.
A well-spelled number never falls back to a string because policy disallows
it or because its value is out of range.

Integers without a sign use the unsigned domain; either explicit sign selects
the signed domain. Unsigned values must fit `uint64_t` and signed values must
fit `int64_t`, including its most negative value. Construction retains the base
and alternate-prefix intent and selects the smallest valid width in the domain:
8, 16, 32 or 64 bits.

Floating-point values are finite IEEE-754 binary64, with negative zero retained.
Conversion rounds to binary64. Overflow and non-zero underflow to zero fail
construction. Huge but syntactically valid numbers can therefore pass structural
checking and subsequently fail with `numeric_out_of_range`.

## NUL provenance

The following observations must remain distinct:

| Origin | Linter output/content | Findings |
| --- | --- | --- |
| Raw zero byte within the source payload | U+0000 | `literal_source_nul`; also `logical_nul` if encountered in a name/value |
| Exact source `C0 80` | U+0000 | `modified_nul`; also `logical_nul` if encountered in a name/value |
| Textual escape `\u0000` | Six source characters, later decoded to U+0000 | `logical_nul`, without inventing a raw or modified source finding |
| Trailing raw source zeros | Removed before decoding | `stripped_terminal_zeros` |
| Appended output terminator | Outside the logical text | No content finding |

Source observations can describe bytes in comments; logical content findings
describe names and values. Linting and source normalisation do not add a new
raw-control permission requirement for NUL. Live admission stores each logical
NUL as exact `C0 80`, and writing spells it `\u0000`.

## Collision extension and singleton objects

Duplicate object names use the public `extend_object_child` operation during
construction. Compare decoded names, so `"a"` and `"\u0061"` collide. Repeated
empty names collide with each other. The receiving member retains its name and
position. Apply each collision in encounter order:

| Existing payload | Incoming payload | Resulting payload |
| --- | --- | --- |
| Non-array | Any value | New array containing old payload, then the complete incoming payload |
| Array | Non-array | Append incoming payload to the existing array |
| Array | Array | New array containing both complete arrays, without splicing their children |

Thus `{"x":1,"x":2,"x":3}` becomes `{"x":[1,2,3]}`, and
`{"x":[1,2],"x":3,"x":[4,5],"x":6}` becomes
`{"x":[[1,2,3],[4,5],6]}`. Successful collision extension sets
`name_collision_extension`, which default policy excludes. Normal live
insertion and renaming continue to reject collisions; explicit extension is
also available to direct document users independently of parser policy.

An anonymous object directly inside an array is unwrapped when, after member
construction and collision extension, it contains exactly one named member.
The named child retains its complete payload and metadata, setting
`singleton_normalization`. Empty and multi-member objects remain objects.
Never unwrap the root. This is an informational document interpretation, not
a relaxed permission.

All resulting arrays are ordinary arrays. Dollar-prefixed names, including
`$morphic` and `$$morphic`, and former protocol-shaped objects are ordinary data.
There is no recovery wrapper, reserved-name unescaping or compatibility reader.

## Writing and round trips

The writer consumes a checked baked document. Both modes quote names and
strings and preserve child order. Morphic mode retains integer domain, base and
prefix intent; strict JSON emits decimal integers, including the full unsigned
64-bit range, and omits an explicit positive sign. A minus sign is ordinary
JSON syntax. Floats use shortest-round-trip spelling and retain a decimal point
or exponent and negative zero. Neither mode emits NaN or infinity.

`escape_non_ascii` independently selects JSON Unicode escapes, including
surrogate pairs for supplementary scalars. Otherwise output uses standard UTF-8.
Logical NUL always uses `\u0000`.

A named value inside an array is written with its implied object wrapper:
a named integer writes as `{"n":1}`, and a named object payload as
`{"n":{...}}`. On reparse, singleton normalisation recovers the named value.
Collision arrays use the same ordinary array syntax in either writing mode.

Every written newline is normalised to LF using the full repertoire above.
Within string values the default spelling is `\n`. Morphic mode honours
per-value newline-escaping suppression by emitting literal LF instead.
Strict JSON always emits `\n` within strings. Other required escaping remains
active, and writing changes neither stored content nor metadata.

Parsing sets suppression when a string's linted source contains at least one
literal LF before escape decoding. Escaped-only newlines leave it unset.
Mixed literal and escaped newlines set suppression for the whole string.
The flag belongs to each value occurrence, not the interned string: equal
stored text can have different flags. Baking, promotion, copying and payload
transfer preserve it.

Text round trips preserve normalised semantic content, not source spelling
or every metadata bit:

- A scalar source is written as its array root, and an implicit body is written
  with explicit delimiters.
- Comments, original whitespace, quote choice and escape spelling are lost.
- Singleton wrappers and collisions follow the semantic rules above.
- Strict JSON loses positive signed-integer intent and non-decimal notation.
- All newline spellings normalise to LF.
- Strict JSON reparse derives suppression as false from escaped-only newlines.
  A suppression flag on a string with no newline has no text spelling to retain.
- Source-encoding findings describe the newly parsed bytes, not the previous
  input. Binary baking and promotion preserve document metadata exactly.

For layout, `pretty_print` inserts line breaks and indentation;
`trailing_line_ending` independently appends one LF after the whole document,
even in compact mode. `indent_width` controls pretty indentation.
Defaults are Morphic mode, UTF-8 characters without forced non-ASCII escapes,
pretty printing, indentation width two and a final LF. Successful writer output
has one appended NUL terminator; its logical length includes any final LF but
excludes that terminator. Failure returns no output allocation.

The [semantic specification](revised_data_model.md#serialization-facing-requirements)
defines the remaining document-to-text invariants, including live placeholders
baked as null and preservation of the selected root kind.
