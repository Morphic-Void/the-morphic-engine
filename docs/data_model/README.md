Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   README.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   14 Sep 2026

# Data-model documentation

These documents describe the current implementation and its contracts.
They are self-contained references; implementation plans and review history
are not required to interpret them.

| Document | Responsibility |
| --- | --- |
| [Semantic specification](revised_data_model.md) | Logical nodes, names, roots, numeric values, mutation, collision extension, integrity, ownership, baking, promotion and subtree copying |
| [Text format](document_text_format.md) | Source encodings, complete grammar, root inference, escapes, numeric classification, NULs, construction interpretations and text round trips |
| [Parsing and reporting](document_parsing.md) | Byte-view API, optional linter details, processing state, failure locations, findings, permission masks and policy evaluation |
| [Baked format](baked_document_format.md) | Physical layout, version, flags, string tables and checked validation |
| [Design notes](data_model_design_notes.md) | Rationale and implementation tradeoffs, rather than additional requirements |

For parser use, start with [the entry point](document_parsing.md#scope-and-entry-point).
For accepted syntax, read the text format together with
[acceptance policy](document_parsing.md#acceptance-policy): supported syntax is
processed before caller permissions determine whether to publish the document.

## Header entry points

Include the header for the operation being used:

| Header in `core/data_model/` | Responsibility |
| --- | --- |
| `live_document.hpp` | Live document ownership, queries and mutation. |
| `baked_document.hpp` | Checked baked views, fixed-layout editing and block ownership. |
| `document_parser.hpp` | Source-byte parsing and policy-controlled publication to a live document. |
| `document_writer.hpp` | Text output from a checked baked view. |
| `document_structure.hpp` | Syntax-only checking and capacity estimates for already-linted UTF-8. |
| `document_translation.hpp` | Whole-document and selected-root-member baking and promotion. |
| `document_copy.hpp` | Detached subtree copying and name stabilisation. |

`data_model_types.hpp` supplies shared identities and value metadata;
`document_findings.hpp` supplies processing reports and caller policy. Their
consumers include them, but they may also be included directly for those types.

`live_document_node.hpp` and `baked_document_format.hpp` describe storage used by
the document headers. Ordinary callers use the document APIs; format validation
and tooling may need the baked records directly. `document_text_lex.hpp` is
internal scanner support shared by the parser and structural checker.

Parser, writer and translation headers forward-declare document classes. Include
the corresponding live/baked document header when defining an owner or view.
