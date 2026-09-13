# Document style review

Updated 13 September 2026. Follow-up to the parameter const pass committed as
`8d5ca1d`. The user's manual formatting edits are retained in the working tree.
The changes below are uncommitted and await user and coordinator review.

Validation: Debug and Release builds and all ordinary test suites pass on x64
and Win32. Each configuration passes 18,079 parser, 1,247 structure, 10,539
writer, 1,216 baked-document and 213 baked-transfer checks. Live-document checks
pass at 4,426 in Debug and 4,436 in Release, including the expanded signed-width
boundary cases. Policy validation, whitespace and line-ending checks pass.
The relocated collision function's body was compared with its pre-move text
and is unchanged.

## Addressed observations

Original note numbers are retained for reference.

- **Item 1:** `live_signed_integer_smallest_width()` now expresses its bounds in hexadecimal,
   casting each magnitude to `std::int64_t` before applying a negative sign.
   Tests check explicit expected widths at and immediately outside each boundary.
- **Item 5:** `CToken` member comments are on the corresponding declaration lines.
- **Item 7:** `extend_object_child()` has its own group after structural mutation, with its
   definition moved to match. The user's formatting inside the body is preserved.
- **Item 9:** `CParser::integer()` declares `max_signed_integer` as a `constexpr` at the
   top of the function.

## Verified observations

- **Item 6:** `trailing_line_ending` controls one final LF after the complete document.
   `pretty_print` independently controls layout line breaks and indentation.
   The writer header now explains formatting and string-newline behaviour in
   its top-of-file documentation, with a short reference beside the member.
   Comments across the document model and shared text utilities use British
   English; existing code identifiers keep their spelling.
- **Item 8:** Separate name/value scratch buffers prevent decoding a value from overwriting
   its decoded name. They now form a group with a comment explaining both.
- **Item 10:** Private lexical helpers already have static linkage inside `lex_util`.
    `read_escape`, `is_name_token` and `select_root` are shared operations used
    by the parser and/or structure checker. They remain in `document_text`.

## Wider ownership refactoring

Item 2 is deferred to the wider ownership refactoring in the
[consolidation plan](consolidation_pass.md). It is not part of the current
parser/report review.

- **Item 2:** Consider whether `CBakedDocumentBlock` should adopt a `CByteBuffer` to simplify
   building and ownership transfer. Reassess whether a distinct block class is
   needed, or whether a general "interpret this byte buffer as..." abstraction
   would be more appropriate.

## Parser and structural report discussion

Items 3 and 4 are being evaluated together. They remain design questions,
not approved API changes.

The agreed direction is to separate concerns and represent each consistently:
one shared processing-state vocabulary, one accumulated findings value, one
terminal diagnosis with locations, and an independent policy result. Structural
checking should contribute to the pipeline report without retaining duplicate
diagnoses or findings inside an embedded report. Reuse representations where
distinct types provide no useful type strictness. The numerical values and
signedness of processing states remain undecided; the negative/zero/positive
example was illustrative, not a specification.

The current style changes form a proposed commit checkpoint before experiments
with report composition and public entry points. No such API changes are
included in this checkpoint.

- **Item 3:** Improve `document_parser.hpp` readability: move extensive comments into the
   file header or external documentation; reconsider report naming, whitespace
   between concerns and conceptual processing order; make `structure_start`
   understandable without recent context. Reassess ingestion entry points:
   their necessity, public visibility, placement and documentation.
- **Item 4:** Review `document_structure.hpp` with the same reporting concerns. The parser
   report already contains a structural report; examine duplicated summary
   fields and how composition should work. Move the `string_source_byte_size`
   and `maximum_depth` comments inline. The implementation provides some
   justification for a separate header, but whether to retain that boundary
   remains open.
