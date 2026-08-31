Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   policy_validator.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   19 Aug 26

# Morphic Policy Validator

## Purpose

`MorphicPolicyValidator` is a standalone C++17 development tool. It is an
early-warning policy canary for source and Visual Studio project files, not a
complete C++ parser or a proof of semantic safety.

The production engine projects remain C++17 with exception handling disabled.
The validator may use the C++ standard library and exceptions. SuiteUTF is
deliberately outside the validator's first-version scope.

## Build integration and reports

The Host, Executive, and MorphicTests projects reference the validator project
and run it before compilation. A failed error-level rule fails that build.

The executable can also be run directly:

```text
MorphicPolicyValidator.exe --root <repository> [--project <name>]
    [--configuration <name>] [--platform <name>] [--no-report]
```

Unless `--no-report` is used, reports are written beneath
`logs/policy_validator`. Report names include the validated project,
configuration, and platform where supplied, so concurrent configuration builds
do not intentionally share a report file.

## Central policy

`policy/morphic_policy.cfg` is a versioned, line-oriented policy file. Fields
are separated by `|`; paths are repository-relative and use `/` separators.
It contains:

- source roots and their engine or test scope;
- default-deny direct system-header permissions, with path and ownership scope;
- approved placement-construction and allocation-infrastructure locations;
- literal global/system identity macro surfaces; and
- engine and tooling project classifications.

Routine permissions belong in this file. Existing usage is reviewed input to
the allowlist, not automatic grandfathering.

### Standard library permissions

The engine-wide foundations include `<charconv>`, `<cmath>`, `<climits>`,
`<cfloat>`, `<ratio>`, `<initializer_list>`, and `<tuple>`. These headers need
no local annotation. Permission to include a header is not a guarantee that
every operation or template instantiation is non-throwing.

`<charconv>` provides caller-buffer numeric conversions with explicit error
results. `<climits>`, `<cfloat>`, and `<ratio>` provide arithmetic properties
and compile-time facilities. Initializer-list backing storage must remain
alive while referenced; tuple elements and invoked operations remain subject
to the engine's normal allocation and exception restrictions.

Ordinary `<cmath>` use is permitted. C++17 mathematical special functions
need an implementation review if introduced, because their non-throwing
behaviour is not universally guaranteed across standard libraries.

`<array>` and `<string_view>` retain their existing limited permissions;
prefer the codebase's alternatives for new engine use. `<numeric>` remains
unapproved pending further review. Self-managed temporary library allocations
are not, by themselves, grounds for exclusion, but their failure behaviour
must remain compatible with disabled exceptions. `<algorithm>` retains its
existing permission, but `stable_sort` is prohibited by `LIB001`; use an
engine-compatible alternative with an explicit failure policy.
These decisions do not relax the naked-new or allocation-infrastructure rules.

## Narrow source suppressions

An exceptional diagnostic may be suppressed on exactly the following source
line with one rule and a non-empty reason:

```cpp
// morphic-policy: suppress-next-line GID002 reason="negative test requires an out-of-catalogue binding"
MV_REGISTER_SYSTEM_TYPE(...)
```

Unknown, malformed, unmatched, and end-of-file suppressions are errors. This
keeps local exceptions visible and makes stale suppressions fail rather than
silently accumulate.

## Rules

The principal conclusive rules are:

- `MEM001`: ordinary naked `new` expression;
- `LIB001`: prohibited `stable_sort` identifier in source or a macro definition;
- `GID001` and `GID002`: catalogue or registration macro outside its permitted
  surface;
- `INC001`: direct non-project include outside the reviewed allowlist;
- `INC003`: direct `windows.h` use outside the wrapper;
- `INC004`: unresolved quoted project include;
- `PRJ001`: applicable project configuration does not explicitly select C++17;
- `PRJ002`: engine exception handling is not explicitly disabled, or tooling
  exception policy is not explicit;
- `PRJ003`: contradictory `/EH` or `/std` option;
- `SRC001`: configured source root is missing; and
- `SUP001`: invalid or stale source suppression.

Context-dependent findings are warnings initially:

- `MEM002`: parenthesised `new` requiring placement classification;
- `MEM003`: raw allocation primitive outside approved infrastructure;
- `INC005`: ambiguously resolved quoted include;
- `INC101`: apparently unused `<new>` include;
- `INC102`: macro-expanded include operand not evaluated; and
- `PRJ101`: project condition beyond the limited evaluator.

Errors produce a non-zero exit status. Warnings are reported but do not fail
the build. Invocation or policy-loading failures use a distinct non-zero exit
status from policy violations.

## Deliberate limits

The scanner loads each complete file and tokenises enough C++ lexical structure
to distinguish comments, literals, directives, identifiers, and common
new-expression forms. It does not preprocess or build an abstract syntax tree.

Global-ID enforcement is intentionally macro-oriented. It does not attempt to
discover manual reimplementations of functions or structures hidden behind the
macros. Placement classification is lexical, and project-file evaluation is
limited to the conditions and options needed by the current projects. These
boundaries should yield explicit warnings where feasible rather than imply a
stronger guarantee.

`LIB001` matches the exact identifier, not a resolved C++ function. It catches
qualified and unqualified uses, using-declarations, function references, and
literal macro definitions, including code in inactive branches. Comments,
string/character literals, and longer identifiers are ignored. An unrelated
declaration named `stable_sort` also matches; prefer a different name or use a
justified narrow suppression on an ordinary source line. Macro expansion and
token-pasting are not evaluated. A header permission is not an exemption from
this symbol check; tooling outside the configured scan roots remains exempt.

## Focused regression tests

After building the validator, run
`tools/policy_validator/test_stable_sort.ps1` in PowerShell. It defaults to the
Debug x64 executable; `-ValidatorPath` selects another build. The script creates
and removes an isolated temporary fixture checkout, checks `LIB001` diagnostics
and suppressions, and does not modify production sources or write reports.
