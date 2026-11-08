# Instructions for Codex

## Working arrangement and commits

Work directly in the shared main checkout; create a branch or worktree only if
requested. Preserve unrelated changes, coordinate shared-checkout edits, and
preserve the user's manual formatting near edits.

Apply style rules to new or substantively changed code. Do not perform unrelated
style cleanup or rewrite existing attribution merely because a file is touched.

Create a separate implementing task only when asked. Discuss substantive design
choices with the user; completed consolidation plans do not authorise future work.
Do not repeat passing validation matrices without a new change or unresolved concern.

Commit only on explicit user instruction. If this work has an active coordinator,
also obtain coordinator review and address findings before committing; permission
to commit does not waive review. No coordinator means no coordinator review is
required. The user performs pushes; do not push.

## Text files

Follow `.gitattributes` and end text files with a newline unless explicitly exempt.
Visual Studio primarily edits `*.sln`, `*.vcxitems`, and `*.filters`: preserve
their CRLF, encoding, and BOM state. Solution files require a final newline;
do not add one to item/filter files solely for normalisation.
`tools/check_line_endings.ps1` checks line endings and allows only item/filter
files to lack a final newline.

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
column. Implementation/review credit lines do not determine that column.

`Date:` normally records creation. Preserve it during routine edits; update only
for a particularly significant content revision, not as a last-modified timestamp.
Use `DD Mon YY` for new dates; existing dates need not be reformatted.

## Parameter const qualification

Aggressively apply `const` wherever a parameter is not mutated: the value,
pointer itself, and/or referenced or pointed-to object. Match qualification in
declarations and definitions, including top-level `const` that C++ need not match.
For pointers, `const T*` protects the pointee through that pointer, `T* const`
prevents reassignment, and `const T* const` does both.

Preserve required interface types and behaviour; do not introduce casts or
unrelated interface changes solely to add `const`.

## Explicit types and auto

Prefer the explicit type when it is readily identifiable and short enough to read
clearly (e.g. `std::uint64_t`), including existing type aliases. `auto` is acceptable
for long or unwieldy types and where deduction is needed, such as generic code or
unnamed types. Do not introduce casts or new aliases solely to avoid `auto`.
Preserve const/reference qualifiers and behaviour when choosing between them.

## Expression parentheses

Make operator precedence explicit. In comma-separated expression lists and
conditions joined by `&&` or `||`, parenthesise items whose outer expression is
a binary arithmetic, comparison, bitwise/logical operation, or ternary expression.
Parenthesise a ternary's condition when it has such an operation. Operations
nested inside an item do not automatically require wrapping the enclosing item.

Calls, indexing, casts, `sizeof`, increment/decrement, address-of (`&value`),
dereferencing (`*ptr`), simple logical negation (`!ready`), and pointer/member
access do not by themselves trigger extra parentheses. Neither do pointer/reference
declarators. Retain necessary grouping, e.g. `(*ptr).member` or `!(a && b)`;
arithmetic and comparisons involving pointers still follow the rule.

Apply this rule only where parentheses preserve meaning; do not mechanically
parenthesise declarations, types, template arguments, or braced initializers.
Macro arguments require the same care about syntax and meaning.

Avoid redundant pairs: precedence and visual separation can share parentheses.
A single function argument or the complete right-hand side of a simple assignment
needs no additional outer pair; still make precedence explicit within it. Examples:

```cpp
result = a + (b * c);
process((a + b), (c * d));
process(a + b);
process(&value, *ptr, !ready, ptr, ptr->member);
process(get_value(), values[index]);
process(get_value(a + b), other);
result = (a == b) ? x : y;
process(((a == b) ? x : y), other);
process((a == b) ? x : y);
bool ready = (a == b) && (c < d);
bool active = ((a == b) ? x : y) && ready;
```

## Power-of-two integer arithmetic

Prefer explicit shifts for integer multiplication and division by known powers
of two. For a power-of-two size, use `value & (size - 1)` instead of `value % size`.
Use named shift counts and masks for recurring sizes so the relationship stays
visible. Preserve types, signedness, rounding and overflow behaviour; apply these
forms only when equivalent over the supported input range. Keep floating-point
arithmetic and operations on sizes not known to be powers of two unchanged.

## Control flow and switches

Make conditional `break`, `continue`, and `return` easy to see. Short, simple
conditions may use an unbraced statement on the same line, e.g. `if (done) return;`.
If the complete line exceeds roughly column 60, or the condition combines tests
with `&&` or `||` or spans multiple lines, use an expanded block: condition,
opening brace, flow-control statement, and closing brace on separate lines.
A simple comparison alone does not require expansion. Column 60 is a readability
guide, not a limit.

Indent `case` and `default` labels one level inside the `switch`. Align each
case body's braces with its labels and indent its statements one further level.
Use the same label indentation for compact switches. Follow the surrounding
code's indentation style and width.

```cpp
switch (state)
{
    case EState::ready:
    {
        process();
        break;
    }
}
```

Normally give each `case`/`default` body a braced scope, with braces and statements
on separate lines and any existing or required terminating `break`, `continue`,
or `return` inside. Preserve intentional fallthrough and do not add a `default`
solely for formatting.
Consecutive labels sharing a body share one scope. Compact unbraced cases are
acceptable only when the whole switch is a very simple conversion/direct mapping
(e.g. enum-to-string or format-to-size). Prefer expansion for processing, state
changes, or further control flow. If any case uses expanded form, all case bodies,
including `default`, must use it. This consistency rule applies throughout the
switch being substantively changed; leave unrelated switches untouched.

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
