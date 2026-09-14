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
| [Semantic specification](revised_data_model.md) | Logical nodes, names, roots, numeric values, mutation, collision extension, integrity, ownership, baking and promotion |
| [Text format](document_text_format.md) | Source encodings, complete grammar, root inference, escapes, numeric classification, NULs, construction interpretations and text round trips |
| [Parsing and reporting](document_parsing.md) | Byte-view API, optional linter details, processing state, failure locations, findings, permission masks and policy evaluation |
| [Baked format](baked_document_format.md) | Physical layout, version, flags, string tables and checked validation |
| [Design notes](data_model_design_notes.md) | Rationale and implementation tradeoffs, rather than additional requirements |

For parser use, start with [the entry point](document_parsing.md#scope-and-entry-point).
For accepted syntax, read the text format together with
[acceptance policy](document_parsing.md#acceptance-policy): supported syntax is
processed before caller permissions determine whether to publish the document.
