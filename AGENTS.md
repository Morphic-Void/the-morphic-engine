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
