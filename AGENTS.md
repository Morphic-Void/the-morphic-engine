# Instructions for Codex

## Working arrangement and commits

Work directly in the shared main checkout; create a branch or worktree only if
requested. Preserve unrelated changes, coordinate shared-checkout edits, and
preserve the user's manual formatting near edits.

Create a separate implementing task only when asked. Discuss substantive design
choices with the user; completed consolidation plans do not authorise future work.
Do not repeat passing validation matrices without a new change or unresolved concern.

Commit only on explicit user instruction. If this work has an active coordinator,
also obtain coordinator review and address findings before committing; permission
to commit does not waive review. No coordinator means no coordinator review is
required. The user performs pushes; do not push.

## Text files

Follow `.gitattributes` and end text files with a newline unless explicitly exempt.
Visual Studio primarily edits `*.vcxitems` and `*.filters`: preserve their CRLF,
encoding, and BOM state, and do not add a final newline solely for normalisation.
`tools/check_line_endings.ps1` checks line endings and allows these files to lack
a final newline.

## Source file headers

Repository-owned `.cpp` and `.hpp` files, including tests, begin with a blank line
before the `//` header. Preserve this manual-editing convention when creating or
updating files; the leading-blank-line rule applies only to these extensions.

Use this header order and layout: copyright/licence, blank comment line,
file/credits/date, blank comment line, brief purpose:

```cpp

//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    example.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Brief explanation of the file's purpose.
```

Main code normally credits both authors; use sole authorship where it reflects
actual input, and credit both when uncertain. Tests normally replace `Authors:`
with two credit lines: `Primary implementation: OpenAI Codex` and
`Reviewed and accepted by: Ritchie Brannan`; joint authorship is appropriate
where it reflects actual input.

Use `Author:` for one author and `Authors:` for multiple authors. Adjust spacing
after `File:`, `Author:`/`Authors:`, and `Date:` so their values start in the same
column.

`Date:` normally records creation. Preserve it during routine edits; update only
for a particularly significant content revision, not as a last-modified timestamp.
Use `DD Mon YY` for new dates; existing dates need not be reformatted.

## Parameter const qualification

Aggressively apply `const` wherever a parameter is not mutated: the value,
pointer itself, and/or referenced or pointed-to object. Match qualification in
declarations and definitions, including top-level `const` that C++ need not match.
For pointers, `const T*` protects the pointee through that pointer, `T* const`
prevents reassignment, and `const T* const` does both.

## Expression parentheses

Make operator precedence explicit. In comma-separated lists and conditions
joined by `&&` or `||`, parenthesise each item containing operations. Parenthesise
a ternary's condition if it contains operations, and the entire ternary when it
is an item in either context.

Simple address-of (`&value`), dereferencing (`*ptr`), logical negation (`!ready`),
and pointer values/member access (`ptr`, `ptr->member`) do not count as operations
requiring extra parentheses here. Neither do pointer/reference declarators.
Retain parentheses needed for correct grouping, such as `(*ptr).member` or
`!(a && b)`; arithmetic and comparisons involving pointers still follow the rule.

Avoid redundant pairs: precedence and visual separation can share parentheses.
A single function argument or the complete right-hand side of a simple assignment
needs no additional outer pair; still make precedence explicit within it. Examples:

```cpp
result = a + (b * c);
process((a + b), (c * d));
process(a + b);
process(&value, *ptr, !ready, ptr, ptr->member);
result = (a == b) ? x : y;
process(((a == b) ? x : y), other);
process((a == b) ? x : y);
bool ready = (a == b) && (c < d);
bool active = ((a == b) ? x : y) && ready;
```

## Control flow and switches

Make conditional `break`, `continue`, and `return` easy to see. Short, simple
conditions may use an unbraced statement on the same line, e.g. `if (done) return;`.
If that line exceeds roughly column 60, or the condition is compound or multiline,
use an expanded block: condition, opening brace, flow-control statement, and
closing brace on separate lines. Column 60 is a readability guide, not a limit.

Normally give each `case`/`default` body a braced scope, with braces and statements
on separate lines and the terminating `break`, `continue`, or `return` inside.
Consecutive labels sharing a body share one scope. Compact unbraced cases are
acceptable only when the whole switch is a very simple conversion/direct mapping
(e.g. enum-to-string or format-to-size). Prefer expansion for processing, state
changes, or further control flow. If any case uses expanded form, all case bodies,
including `default`, must use it.

## Comma-separated operations

Avoid the comma operator for sequencing operations, including in a `for` loop's
iteration expression. Use separate statements. When restructuring loops, preserve
execution order and behaviour, including `continue` paths. Ordinary separators
between arguments, parameters, or initializer-list elements are unaffected.

## Internal linkage and namespaces

In production code and tools, use `static` for file-local functions and objects
requiring internal linkage; do not use anonymous namespaces. Choose meaningful
named namespaces independently of linkage needs. Tests under `tests/` may use
anonymous namespaces for test-local helpers and fixtures.
