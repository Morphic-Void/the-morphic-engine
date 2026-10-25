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

The [schema delivery record](../project/completed_milestones.md#schema-delivery-status)
records validation checkpoints, including the subsequent buffer correction and
its all-configuration regression runs. Earlier milestones retain their historical
counts; local build logs are evidence paths, not checked-in test dependencies.

## Document model coverage

SchemaModel exercises binary-free authoring, independent handles, all section
combinations, deferred validation and required contained types. Overlapping named
definitions are indivisible, with layout proofs limited to relevant dependencies;
unrelated incompleteness and absent intrinsic fields have distinct merge behavior.
Types/instance/bulk imports contain only their own sections. Failed validation,
composition and construction preserve destination content and source locations.

The main Schema suite and SchemaModel cover retained equal-value pins, empty
containers, scalar shorthand, positional null holes, sparse interior edits and
nested array/bitfield paths. Complete overwrite and component edits exercise both
preserve and propagate policies, immediate-parent inheritance, repeated ancestor
changes, scope-limited removal, meaningful versus padding-only differences and
raw encodings without literal representations. Import/bake/materialise/promote
round trips preserve sparse explicit empty selections after adjacent removal.

Scoped-edit tests check blocked typed access/output/rebinding while pending,
explicit completion, invalid/stale tokens (including owner address reuse), failure
followed by retry, unchanged masks/descendants on failed completion and preserved
target edits. Source buffers/names borrowed from the same live role are exercised
through buffer growth and descendant-sourced copies.

Rebinding covers live/baked roles and live/baked type owners, changed defaults,
explicit pins, nested arrays, bitfields, signed zero, reordered indices, empty used
groups, deep hierarchy, lifetime loss and asserted recovery. Baked replacement
ownership is independent of the original block; read-only changes needing new bytes
reject. Recovery cannot bypass checking with an available old owner.

DocumentBoundary checks all live/baked source combinations, destination aliasing,
section-only imports, types-free save versus failed runtime construction, independent
live materialisation, malformed runtime input, import-depth bounds, precise source
diagnostics and per-allocation failures. Types import includes failures during copying
and final structural admission, for both live and baked source owners.

SchemaMerge and DataMerge preserve existing scalar/presentation, named-order,
conflict-location, aliasing and scaling coverage under CSchema. Additional cases
cover sparse selection-array merge, deferred semantic validation, local layout proof,
missing intrinsic fields and strict whole-definition comparison. Admission includes
incomplete/opaque values in the authoring depth bound. Iterative definition comparison
avoids exponential revisiting and stack-heavy recursive projections.

## Runtime storage and publication coverage

SchemaDataBlock constructs independent raw fixtures for version-2 envelope checking.
It covers all eight section combinations against every supplied-payload combination,
required zero-byte binaries, invalid version/stripped markers, exact lengths, zero
container padding, both alignment gaps, offset/range arithmetic and the inclusive
2 GiB ceiling without large allocations. Present sections cannot omit their binaries.

Raw assembly compares live/baked results byte-for-byte, including deep opaque trees,
detached storage, string remapping, metadata/presentation, complete payload gaps and
tails. Missing-domain binary errors precede allocation and retain source locations.
Local release/adoption, asset borrowing, independent owners and storage readiness
without types bindings remain covered. Types and data roles enforce envelopes when
one is declared, including direct description attachment.

Live role bake/prepare-output tests cover active-entry packing, zeroed new gaps,
record padding, raw booleans/enums, zero-size records, count-zero rejection, source
independence and occupied outputs. Bulk authoring retains capture/replacement,
self-aliasing through growth, rename/erase, unpopulated arrays, complete literals,
UINT32_MAX zero-size counts and failed replacement preserving identity/content.
Remapping uses bulk mutable views or explicit instance edits, preserving unmatched
bytes and completing with a chosen descendant policy. Unused-storage tests retain
padding, unused-bit, gap, abandoned-storage and capacity-tail boundaries.

Allocation sweeps reject successive allocations through composition, merging,
materialisation, promotion, editing, rebind, output, assembly and import. Tests check
transactional publication, cleanup, binding counts and source lifetime. Generic
document failures retain their available operation-level reasons; more specific
typed reasons are asserted where supplied. No general timing promise is inferred
from bounded traversal/scaling tests.

## Association and interface coverage

`Schema_test_suite.cpp` exercises general declaration observations independently
of physical types, forward/shared references, targetless associations, nested
interfaces, maximum fixed counts, decorations and source mappings. Invalid grammar,
name collisions, missing targets and association/interface cycles are rejected.
Physical queries, defaults, instance/bulk preparation, remapping and storage clearing
reject non-storage declarations while empty physical types remain valid.

Declaration comparisons cover identity, multiplicity, metadata, physical dependencies
and failed-traversal cache reuse. Merge cases check incompatible relationships,
metadata conflicts, original locations, omitted/explicit unit counts, union and
source aliasing. Structurally admitted incomplete inputs may merge; explicit
validation checks the completed catalogue. Conflicting or unestablished duplicate
definitions still fail during merging. Lifecycle cases
cover move/clear, promotion, baking, canonical output and composition. Allocation
sweeps check resolver cleanup, output, comments, graph comparison/retry and occupied
merge destinations. Generated fixtures include mixed and non-storage-only schemas.

## Type-schema decoration coverage

`Schema_test_suite.cpp` covers decorations on structures, enums, bit structures,
structure members, bit fields and both levels of nested arrays. Cases distinguish
missing keys from empty strings, empty from omitted maps, and exact key/value
contents from declaration identifier rules. Invalid objects, non-string values,
empty/duplicate keys and unsupported attachment points are rejected. Ordinary
types, fields and enum labels named `decorations` remain legal.

Allocation-free lookup/enumeration, unchanged outputs on query failure, no
inheritance, strict definition comparison, order-independent maps and unchanged
generated C++ are checked. Lifecycle coverage includes schema promotion/baking,
canonical output, composition and whole-document baking. Data-only persistence,
explicit value import, checked types rebinding and exact remapping retain their existing
scope and ignore decoration differences. Merge tests cover union, empty/absent
maps, source aliasing, conflicts at every attachment point and original locations.
Allocation-failure sweeps exercise resolution, canonical output, occupied merge
destinations and comparison, checking failure atomicity and attribution cleanup.

## Running the checks

Build and run MorphicTests using the repository's
[build instructions](../project/building_and_testing.md). The ordinary -t1 run
includes SchemaModel, Schema, their merge/block/boundary cases and the related
data-model and Host asset suites. For example:

~~~powershell
.\build\bin\x64\Debug\MorphicTests.exe -t1 --log-tag=schema-check
~~~

BundleRequests retains .bsd live-bake/admit/save/reload and authored aggregate
construction workflows through shared and dedicated workers, including combined
types/instances/bulk blocks. Host saving retains complete bytes, while typed
attachment remains explicit. Generic document, bundle and asset tests cover
borrowed lifetime and buffer transfer independently of schema roles.

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

The `schema_array_layout` fixture covers independent stride and alignment,
count-one and empty arrays, nested wrappers, inherited increased alignment,
slot and additional array tail padding, and helper/payload name collisions.
Compiler assertions follow `array` and `value` payloads to check element stride,
payload offset zero and each nested extent/alignment. Runtime checks cover
canonical output, live/baked round trips, omitted/explicit metadata merging,
invalid descriptors, ceiling arithmetic and allocation-failure cleanup. Storage
checks exercise construction/defaults/inheritance, capture/scoped completion,
import/persistence, checked rebinding, exact remapping and padding clearing,
including a 32-byte array with stride 12 that is not divisible by its stride.

Use Debug/Release and 32/64-bit configurations when changes affect representation,
lifetime, arithmetic or allocation. Do not repeat passing matrices without new
changes or an unresolved concern. Source/project changes use the repository policy
validator. Documentation updates require link, diff and line-ending checks; sample
changes also require their runtime coverage.

## Aggregate construction

Aggregate construction regressions compare direct baking with materialisation
followed by assembly, including payload bytes and authored type presentation.
They also check all type diagnostic locations against the original aggregate,
instance-before-bulk semantic error ordering, source replacement after ownership
extraction, empty groups/specialisations and domain-specific zero-size offsets.
Allocation sweeps require failure to publish no owners and expose no locations
from private staging. The ordinary run reports allocation counts/requested bytes
for direct baking and the equivalent workflow through live owners; these are
observations rather than fixed allocator or timing thresholds.

## Authoring sample

test_schema_sample parses schema-example.json as a binary-free aggregate, validates
its contained types, generates compiler layout fixtures and constructs both data
domains. It exercises promotion, complete runtime baking and typed reload. The
[sample notes](schema-example-notes.md) record expected layouts and values. All
runtime locators are generated; none are reserved in the source authoring file.

## Validation history

The [document model milestone](../project/completed_milestones.md#schema-document-model-and-runtime-construction)
records current delivery results; the [earlier schema milestones](../project/completed_milestones.md#schema-delivery-status)
retain historical checkpoints. Their counts and superseded APIs are historical
evidence, not the current contract. Local tagged build/test output supplies the
detailed validation logs.
