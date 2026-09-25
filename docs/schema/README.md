# Schema documentation

Start with [the design](design.md) for schema semantics, then inspect
[the example](schema-example.json) and [its notes](schema-example-notes.md).
The [implementation contract](implementation-contract.md) defines the initial
delivery, runtime observations, checks, and boundaries for subsequent work.

[The header survey](header-survey.md) is supporting research for the later
source-ingestion stage. It does not expand current delivery scope.

The old coherence audit and question agenda have been consolidated into these
documents. Git history retains the audits and superseded discussion; implementing
tasks do not need to read them to recover current requirements.

The sample includes Morphic numeric extensions and later layout features. Its
notes distinguish initial fixtures from the full design example. Standard-JSON
review previews should use the existing document writer option, preserving values
while omitting Morphic-specific numeric presentation such as `+` and hexadecimal.
