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
