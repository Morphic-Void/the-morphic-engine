# Current scope

Updated 12 September 2026. Consolidation precedes schema work.

The first document pipeline is implemented. Its parser/reporting design and
the surrounding Host contracts need further work before schema consumers are
built. Completed implementation does not mean these interfaces are final.

## Active work

1. Refactor the parser and its reporting in the current parser task using the
   settled observations, caller policy, grammar and document-model requirements.
   The [linter/parser specification](parser_refactoring_specification.md) is
   the reviewed behavioural contract. Stage 1, covering the linter and shared
   diagnostics, is implemented, validated and reviewed.
   Stage 2 is authorized and in progress. Its first model infrastructure slice
   implements native empty names, root kinds, newline metadata and collision
   extension and is reviewed and validated. Shared findings, singular failure
   reasons and policy definitions are reviewed and committed.
   Scanner/structural findings, partial-report composition, separate capacity
   estimates and construction coverage are reviewed and committed. Shared
   unquoted-token grammar and contextual value findings are reviewed and committed.
   Parser support for native empty names, structural newline-name rejection and
   source-derived per-string newline suppression is reviewed and committed.
   Explicit array roots and shared inference of object/array bodies are
   reviewed and committed. Shared terminal stage/reason reporting is reviewed
   and committed. Caller options and late policy acceptance before publication
   are implemented for review. Protocol retirement and the remaining parser
   report simplification remain.
   After the main implementation, consider a separate const pass: identify
   parameters that functions do not mutate, add appropriate const qualification,
   and align parameter const decoration between declarations and definitions.
   The user plans a light manual style and beautification pass after that const
   pass.
2. Develop the remaining Host consolidation in a separate task: module
   lifecycle, asset identity and lifetime, asynchronous operations, aligned
   loading, conditioning and filesystem resolution.
3. Exercise the resulting Host services with document persistence and an
   Executive-controlled functional run, then begin a schema vertical slice.

[Consolidation plan](consolidation_pass.md) owns detailed scope, open questions
and dependencies. The parser refactor can proceed alongside Host design; the
integration run depends on both. Pause before commits for review.

## Documentation map

- [Engine backlog](engine_backlog.md): remaining broader and deferred work.
- [Data-model specification](../data_model/revised_data_model.md),
  [baked format](../data_model/baked_document_format.md) and
  [design rationale](../data_model/data_model_design_notes.md): current
  implemented baseline, including stage-1 linter/shared diagnostics, with the
  remaining parser/model migration and ownership areas still open.
- [Completed milestones](../project/completed_milestones.md): implementation
  outcomes and validation, including the first document pipeline.
- [Future work notes](../project/future_work_notes.md): supporting cross-task
  context, without a second priority list.

Historical stage-by-stage plans remain in Git history. They do not override
the consolidation direction or require the former Executive test sequence
to be implemented unchanged.
