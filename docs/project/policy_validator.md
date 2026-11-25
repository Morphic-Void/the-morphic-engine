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

The engine and fixture projects reference the validator project and run it before
compilation. A failed error-level rule fails that build. Every invocation checks
all configured sources and projects; the project/configuration/platform arguments
label reports rather than restrict validation.

The executable can also be run directly:

```text
MorphicPolicyValidator.exe --root <repository> [--project <name>]
    [--configuration <name>] [--platform <name>] [--no-report]
```

Unless `--no-report` is used, reports are written beneath
`development/logical-roots/logs/policy_validator`. Report names include the validated project,
configuration, and platform where supplied, so concurrent configuration builds
do not intentionally share a report file.

## Central policy

`policy/morphic_policy.cfg` is a versioned, line-oriented policy file. Fields
are separated by `|`; paths are repository-relative and use `/` separators.
It contains:

- source roots and their engine or test scope;
- default-deny direct system-header permissions, with path and ownership scope;
- approved placement-construction and allocation-infrastructure locations;
- exact test-file permissions for direct memory-token use;
- literal global/system identity macro surfaces; and
- engine and tooling project classifications.

Routine permissions belong in this file. Existing usage is reviewed input to
the allowlist, not automatic grandfathering.

Configuration records require non-empty fields, recognized scopes and match kinds,
and one supported version record. Paths must be normalized repository-relative
paths with `/` separators: `prefix` paths end in `/`, other paths do not, and
`any` uses `*`. Duplicate records (including include permissions differing only
in their reason), conflicting project classifications and conflicting diagnostic
rules for one macro are policy-loading errors. The format remains version 1.
Different paths classifying the same physical project are rejected with `CFG001`,
including case aliases and links; they cannot supply conflicting engine/tool scopes.

Configured targets must exist inside the repository. Source roots cannot overlap,
and narrow source permissions must address scanned source files/directories in
the correct scope. Invalid targets fail with `CFG001` (`SRC001` for source roots).
Unused exact/prefix include and macro permissions, and unused placement/allocation
permissions, produce `CFG101`. A permission fully covered by another permission
produces `CFG102`. Usage includes inactive preprocessor branches. `any` permissions
are standing policy and do not need a current use; classification labels explain
permissions but do not change their enforcement.

`memory-token|tests/path.cpp|reason` permits direct `CMemoryToken` use in one
scanned test file. The reason is required; duplicate paths, including records
with different reasons, are rejected. These permissions cannot target engine
scope, unscanned files or directories, and unused permissions produce `CFG101`.

Project coverage is checked against `.vcxproj` files in the repository root and
under `visual_studio/`, plus project references in `MorphicEngine.sln`. Unclassified
projects fail with `PRJ004`. Solution projects under `external/` remain outside
this inventory. Invalid targets or incomplete project coverage stop the source
scan, avoiding misleading unused-permission reports from an incomplete audit.

Foundation uses the `foundation/` directory and `MorphicFoundation` shared items.
Its project includes are restricted to Foundation and SuiteUTF. Framework's contracts
and common implementation may include Foundation, Framework and SuiteUTF, with explicit
`framework/...` includes resolved through the repository root. Neither layer may
include Host or module implementations. There are no migration exceptions.
Configured catalogue macro permissions name the Framework files; generic
registration machinery retains its Foundation permission. This separation does not
widen the exact-file permissions for SYSTEM registration macros.

Rendering source and project configuration use the same engine checks as Host
and Executive. The two Basis Universal headers are permitted only in
`rendering/runtime/basis_codec.cpp`; upstream Basis sources remain outside the
scanner's source roots. Rendering's function registration is permitted only in
its binding header.

Resolved vendor headers and implementation files under `external/` require an
adapter permission for their repository-relative destination, with quoted and
angle-bracket includes treated identically. Relative paths, backslashes, absolute
paths and symbolic links cannot bypass this check. SuiteUTF retains its explicit
exception. Resolved includes must remain inside the repository, and Foundation/
Framework layer restrictions still take precedence over vendor permissions.

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
- `MEM004`: direct `CMemoryToken` identifier outside `foundation/`, `framework/`
  or an explicitly permitted test file;
- `LIB001`: prohibited `stable_sort` identifier in source or a macro definition;
- `GID001` and `GID002`: catalogue or registration macro outside its permitted
  surface;
- `INC001`: direct non-project include outside the reviewed allowlist;
- `INC002`: Foundation project include outside Foundation or SuiteUTF, or a Foundation include
  whose macro-expanded operand cannot be checked;
- `INC003`: direct `windows.h` use outside the wrapper;
- `INC004`: unresolved quoted project include;
- `INC006`: Framework project include outside Foundation, Framework or SuiteUTF, or a
  Framework include whose macro-expanded operand cannot be checked;
- `PRJ001`: applicable project configuration does not explicitly select C++17;
- `PRJ002`: engine exception handling is not explicitly disabled, or tooling
  exception policy is not explicit;
- `PRJ003`: contradictory `/EH` or `/std` option;
- `PRJ004`: repository-owned project missing a policy classification;
- `CFG001`: invalid permission/project target or overlapping source roots;
- `SRC001`: configured source root is missing; and
- `SUP001`: invalid or stale source suppression.

Context-dependent findings are warnings initially:

- `MEM002`: parenthesised `new` requiring placement classification;
- `MEM003`: raw allocation primitive outside approved infrastructure;
- `INC005`: ambiguously resolved quoted include;
- `INC101`: apparently unused `<new>` include;
- `INC102`: macro-expanded include operand outside Foundation/Framework not evaluated;
- `PRJ101`: project condition beyond the limited evaluator;
- `CFG101`: unused narrow permission; and
- `CFG102`: redundant include or macro permission.

Quoted project includes are checked relative to the including file's directory,
the repository root, and Foundation. Files under Host, Executive, and Rendering also
use their own module root, including when those sources are compiled into tests.
Other modules remain explicitly qualified; their roots are not added as fallback
candidates. Multiple distinct matching files still produce `INC005`.

Foundation project includes may resolve only within `foundation/` or `external/SuiteUTF/`;
Framework includes may additionally resolve within `framework/`.
SuiteUTF is an explicit exception because it is controlled by the engine's
author and intended for low-level integration. Other engine modules, tests,
tools, and vendor libraries are outside this boundary. The check normalises
path separators and resolves parent segments and symbolic links before
checking the destination; a matching forbidden candidate fails the check even
if another candidate is allowed. It applies to quoted and angle-bracket
operands, and system-header permissions cannot override it. Approved standard
and platform headers retain their existing permissions. Both layers must use
literal include operands so the checker can verify their boundaries;
macro-expanded operands are errors there and remain warnings elsewhere.

Errors produce a non-zero exit status. Warnings are reported but do not fail
the build. Invocation or policy-loading failures use a distinct non-zero exit
status from policy violations.

## Deliberate limits

The scanner loads each complete file and tokenises enough C++ lexical structure
to distinguish comments, literals, directives, identifiers, and common
new-expression forms. It performs physical line splicing (LF and CRLF), preserving
raw literal contents and original diagnostic locations, but does not expand macros
or build an abstract syntax tree. Comments may separate directive tokens or precede
the directive marker. Multiline comments and raw literals remain intact when
collecting a directive. Comments and string literals do not count as uses of
configured identity macro permissions.

Memory checks also examine macro replacement bodies, including inactive and
continued definitions. The macro name and parameters are excluded, so allocator
redirections such as `#define malloc engine_allocate` do not count as raw
allocation. Comments and literals are ignored. Macro expansion, token pasting,
and allocations introduced only through those mechanisms remain outside the
scanner's guarantees. `MEM002` and `MEM003` retain their warning severity.

`MEM004` also examines literal macro replacement bodies, including inactive and
continued definitions. It matches the identifier in declarations, aliases,
parameters and template arguments without resolving C++ types. Comments,
literals and longer identifiers do not match. A local alias declaration using
`CMemoryToken` is checked; uses of aliases provided elsewhere, inferred types,
macro expansion and token pasting are outside the rule's guarantees. Foundation
and Framework remain the implementation layers for raw token ownership. Other
engine code should use their owners and containers; the Foundation allocators
remain the intended interface for controlled external-system allocations. This
rule does not restrict direct context allocation or change placement-new rules.

Project override checks examine each compiler option, including mixed `/std`
selections and MSVC's `-` option spelling; a permitted C++17 option cannot conceal
another conflicting standard selection.
Suppressions can target the following directive as well as ordinary source;
the diagnostic must be on that target line. Configuration audit findings cannot
be suppressed in source files.

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

Run `python tools/tests/test_policy_auditing.py --binary <validator-path>` for
configuration validation and usage auditing, project coverage, vendor confinement,
and macro allocation/suppression regression fixtures. Linux CI runs these through
`MorphicPolicyAuditing` in CTest. Fixtures use isolated temporary checkouts and
`--no-report`; symbolic-link cases are skipped on Windows without the necessary
privilege and run normally on Linux.

Run `python tools/tests/test_policy_includes.py --binary <validator-path>` to
check include resolution and the Foundation boundary in an isolated fixture checkout.
Linux CI runs these cases through `MorphicPolicyIncludes` in CTest. They cover
module and vendor dependencies, SuiteUTF, relative and absolute paths,
backslashes, ambiguous candidates, angle-bracket permissions, macro operands,
and symbolic-link escapes. Windows skips the symbolic-link cases when the
account lacks the required privilege; Linux runs them normally.

After building the validator, run
`tools/policy_validator/test_stable_sort.ps1` in PowerShell. It defaults to the
Debug x64 executable; `-ValidatorPath` selects another build. The script creates
and removes an isolated temporary fixture checkout, checks `LIB001` diagnostics
and suppressions, and does not modify production sources or write reports.
