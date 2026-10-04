Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   validation.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   4 Oct 2026

# Schema validation

The [specification](specification.md) defines behaviour and the
[runtime guide](runtime-api.md) documents its C++ interfaces. Validation covers
observable results, failure boundaries and generated layout fidelity. Private
record organisation is not a public ABI or a substitute for those checks.

## Running the checks

Build and run `MorphicTests` using the repository's
[build instructions](../project/building_and_testing.md). The ordinary `-t1` run
includes [Schema_test_suite.cpp](../../tests/test_suites/Schema_test_suite.cpp)
and the related document-model suites. For example, after building Debug x64:

```powershell
.\build\bin\x64\Debug\MorphicTests.exe -t1 --log-tag=schema-check
```

Logs and generated files include the tag and process ID. The runner prints their
paths; default output is beneath `development/logical-roots/test-output` and
logs beneath `development/logical-roots/test-logs`.

The schema suite generates C++ headers and separate assertion translation units.
Running the suite alone does not compile those generated files. For layout or
generator changes, pass each printed source path to the maintained helper:

```powershell
.\tools\validate_schema_layout.ps1 -Source '<printed validation source path>'
```

[validate_schema_layout.ps1](../../tools/validate_schema_layout.ps1) compiles with
C++17 and warnings as errors for x86 and x64 by default. Its `-Platforms` argument
can select either target. The fixtures cover natural and explicit layout, empty
types, the authoring sample, representation spelling and generated-name collisions.
Assertions check size, alignment, member offsets and extents, array shape, enum
storage/values, mask types/values, standard layout and trivial copyability,
including `fp16data_t`. Generated data declarations themselves contain no test
assertions or operational code. Support for a compiler/ABI requires fidelity to
the resolved layout; packing changes or approximate output must not hide a mismatch.

Use the appropriate Debug/Release and 32/64-bit configurations when code changes
affect representation, lifetime, arithmetic or allocation. Do not repeat a passing
matrix without a new change or unresolved concern. Documentation-only changes
need link, diff and line-ending checks; edits to the sample also warrant its
existing runtime coverage. Source/project changes use the repository's
[policy validator](../project/policy_validator.md).

## Coverage to preserve

| Area | Observable contracts |
| --- | --- |
| Resolution and queries | Live/baked parity, forward references, cycle/duplicate rejection, exact property sets, named/ordinal agreement, occurrence mapping, clear/move/re-resolution lifetimes. |
| Types and layout | Empty type identities and zero-byte members; nested and count-one arrays; natural and explicit layout; increased alignment; overlap/bounds diagnostic order; recursive gaps; the inclusive 2 GiB extent ceiling. |
| Values and defaults | Integer boundaries and signedness; floating rounding, overflow, signed zero and special strings; existing fp16 conversion; enum aliases and implicit first-label defaults; UNORM/SNORM raw codes and floating inputs. |
| Input structure | Complete bulk records; instance defaults and inherited positional tails; named singleton normalisation and scalar shorthand; invalid nulls, excess values and unsupported aggregate defaults. |
| Ownership and failure | Non-owning baked views, owned live payloads, schema binding invalidation, non-destructive translation, allocation-failure cleanup and the publication rules of each operation. |
| Capture and editing | Independent snapshots, selected-value preservation, parent-before-child propagation, repair of discarded unrepresentable selections, and bulk replacement after reconciliation/promotion. |
| Reconciliation and output | Binary authority, changed defaults, immediate-parent comparison, shortest necessary array prefixes, embedded/external reload, encoded fidelity and source preservation. |
| Stripped output | Required payloads, navigation/binary access, schema/default retention, empty types, malformed markers, contradictory declarations, promotion reconstruction and exact raw encodings. |
| Integer presentation | Declared-type policies, full-width masks, nested defaults, structural thresholds, live/baked sources, strict JSON, repeated preparation and inferred structure details. |
| Remapping | Exact-type direct-member matching, default-sensitive compound compatibility, partial/no-match plans, destination claims, range coalescing, all copy paths, exact-stride preflight and untouched unmatched bytes. |
| Unused storage | Nested padding and optional unused-bit clearing, preservation of every addressable field, complete fixed-array views, document gaps, idempotence and failure before writes. |
| Access cost | Baked sorted indexes, ordered live records, cached bulk type bindings, compatibility caching, retained physical-member order and allocation-free access/clearing. |

The data-model suites separately cover integer metadata, string flags, subtree
copying, promotion/baking and their allocation failures. Schema-specific rules
reuse those facilities rather than introducing a second document implementation.
Allocation and complexity tests establish their measured properties; they do not
promise general timing improvements or that all name lookup is indexed.

## Authoring sample

`test_schema_sample` reads [schema-example.json](schema-example.json) through the
engine parser, bakes and resolves its schema, checks expected layouts, generates
compiler fixtures, materialises instances and bulk records, promotes both roles,
and checks embedded/external baking and reload. Its eleven type definitions,
eleven instance declarations and three bulk records exercise more than resolution.
See the [sample notes](schema-example-notes.md) for expected values and layouts.

## Validation history

The [schema milestone](../project/completed_milestones.md#schema-system-and-refinement)
retains representative completion checkpoints. Detailed historical logs and
per-change matrices belong to their commits and local test output, not the
current behavioural specification.
