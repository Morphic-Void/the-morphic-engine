# Instructions for Codex

## Commit review

When the work has an active coordinator task, require both the user's instruction
to commit and coordinator review of the proposed changes. A user instruction to
commit does not waive that review requirement. Address review findings before
committing. When there is no coordinator task, no coordinator review is required.

## Visual Studio item and filter files

Files matching `*.vcxitems` and `*.filters` are intentionally exceptions to
the repository's general text-file policy. Visual Studio is the primary editor
for these files and writes them with CRLF line endings and may omit the final
newline.

- Preserve CRLF line endings in these files.
- Do not add a final newline solely to normalize them.
- Preserve their existing encoding and byte-order mark state.
- All other text files continue to use the policies in `.gitattributes` and
  are expected to end with a newline unless another explicit exception exists.

`tools/check_line_endings.ps1` enforces the CRLF working-tree form while
allowing the missing final newline for these Visual Studio-managed files.

## Source file opening and header comments

Begin repository-owned `.cpp` and `.hpp` files, including test sources and
headers, with a blank line before the top-of-file comment. This is an intentional
manual-editing convention; preserve it when creating or updating these files.
This leading-blank-line rule applies only to `.cpp` and `.hpp` files.

Use a header made from `//` comments, arranged as follows:

1. Copyright and licence notice.
2. Blank comment line.
3. File name, authorship or implementation/review credits, and date.
4. Blank comment line.
5. Brief explanation of the file's purpose.

For the main codebase, normally use
`Authors: Ritchie Brannan / OpenAI Codex`. Sole authorship is appropriate where
it reflects the actual input; when uncertain, default to crediting both.

For test code, normally use these credits:

```cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
```

Joint authorship remains appropriate for test code where it reflects the actual
input.

`Date:` normally records the file's creation date. Preserve it during routine
edits. Update it only for a particularly significant revision to the file's
content; it is not a last-modified timestamp. Use `DD Mon YY` for new dates,
without requiring existing dates to be reformatted.

Example main-code header, including the leading blank line:

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

## Function parameter const qualification

Use `const` aggressively for function parameters that the function does not
mutate. Apply it at the appropriate level: to a value parameter, to the pointer
itself, and/or to the referenced or pointed-to object.

Keep parameter `const` qualification consistent between declarations and
definitions, including top-level `const` that C++ does not require to match.

For example:

```cpp
void process(const int count, const SInput& input, SOutput* const output);
```

Here, `count` and `input` cannot be modified, and `output` cannot be reassigned,
though the output object remains mutable. Repeat the same qualifiers in the
definition.

For pointer parameters:

- `const T*` prevents modification of the pointed-to object through the pointer.
- `T* const` prevents reassignment of the pointer.
- `const T* const` applies both restrictions.

## Expression parenthesising

Use parentheses to make operator precedence explicit and to separate
expressions visually.

In comma-separated lists and conditions joined by `&&` or `||`, parenthesise
individual items that contain operations. Parenthesise a ternary's condition
when it contains operations, and parenthesise the entire ternary when it
appears as an item in either context.

Avoid redundant parentheses. A single function argument needs no additional
outer parentheses, and neither does the complete right-hand side of a simple
assignment. Parentheses within those expressions should still make precedence
explicit. Do not add a second pair when an existing pair already serves both
precedence and visual separation.

Examples:

```cpp
result = a + (b * c);

process((a + b), (c * d));
process(a + b);

if ((a == b) && (c < d))
{
}

result = (a == b) ? x : y;
process(((a == b) ? x : y), other);
process((a == b) ? x : y);

if (((a == b) ? x : y) && ready)
{
}
```

## Visibility of control flow

Make conditional `break`, `continue`, and `return` statements easy to see.
A short, simple condition may place the flow-control statement on the same
line without braces.

When that line extends beyond roughly column 60, or the condition is compound
or spans multiple lines, expand the statement into a braced block. Put the
opening brace, flow-control statement, and closing brace on separate lines.

Column 60 is a readability guideline, not a strict line-length limit.

Examples:

```cpp
if (done) return;
if (skip) continue;

if ((state == EState::ready) && (remaining == 0))
{
    break;
}

if (request_requires_additional_processing(request))
{
    return;
}

if ((state == EState::ready) &&
    (remaining == 0))
{
    return;
}
```

## Switch statements

Normally give each `case` and `default` body its own braced scope, with the
opening brace, statements, and closing brace on separate lines. Keep the
terminating `break`, `continue`, or `return` inside that scope. Consecutive
labels sharing one body share one scope.

Compact, unbraced cases are acceptable for very simple conversions or direct
mappings, such as an enum-to-string or format-to-size lookup. Prefer the
expanded form when a case performs processing, changes state, or contains
further control flow.

Keep the style consistent throughout each `switch`. If any case requires the
expanded, braced form, use that form for every case body, including `default`.
Use compact cases only when the entire switch suits that style.

Typical expanded form:

```cpp
switch (state)
{
    case EState::ready:
    {
        process();
        break;
    }
    case EState::finished:
    case EState::cancelled:
    {
        release();
        return;
    }
    default:
    {
        break;
    }
}
```

Acceptable compact conversion:

```cpp
switch (format)
{
    case EFormat::gray8: return 1u;
    case EFormat::rgba8: return 4u;
    default: return 0u;
}
```

## Comma-separated operations

Avoid the comma operator for sequencing operations. Put separate operations
in separate statements so each is easy to see. This applies throughout the
code, including the iteration expression of a `for` loop.

Ordinary commas separating function arguments, parameters, or initializer-list
elements are unaffected.

Instead of:

```cpp
for (int a = 1; a < 4; ++a, b = (a + 2))
{
    // Loop body.
}
```

Make the operations explicit:

```cpp
for (int a = 1; a < 4;)
{
    // Loop body.

    ++a;
    b = a + 2;
}
```

When restructuring a loop, preserve its execution order and account for any
`continue` paths so its behaviour remains unchanged.

## Internal linkage and namespaces

In production code and tools, use `static` declarations for file-local functions
and objects that need internal linkage. Do not use anonymous namespaces in
those areas. Use named namespaces when they meaningfully group code, deciding
that independently of whether a declaration needs internal linkage.

Test code under `tests/` is an exception: anonymous namespaces are permitted for
test-local helpers and fixtures.

## Working arrangement

Work directly in the shared main checkout unless the user requests otherwise;
do not create a branch or worktree automatically. Preserve unrelated user/task
changes and coordinate edits when multiple tasks share the checkout.

Create a separate implementing task only when the user asks. Discuss substantive
design choices with the user; completed consolidation plans are not authority
to begin future work. Commit only on explicit user instruction and after any
applicable coordinator review. The user performs pushes; do not push.

Do not repeat passing validation matrices without a new change or unresolved
concern. Preserve the user's manual formatting when editing nearby code.
