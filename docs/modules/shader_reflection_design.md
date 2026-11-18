Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   shader_reflection_design.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   26 Oct 2026

# Shader system design

## Status and boundaries

This combines decisions agreed through 15 November 2026 with a completed
[review draft](#review-draft-completion-contracts) prepared under the user's direction
to cover the remaining inventory. On 16 November the user's review identified
missing detail, ambiguity and over/under-specification. This document now serves as
the overall system reference under [staged development](../backlog/shader_system_stages.md).
Earlier agreed policies remain the baseline; completion choices are draft proposals,
not implementation-ready contracts. Focused stages refine them with explicit review
of changed decisions. No implementation is authorised here. The specific
transform-operation catalogue remains deliberately deferred at the user's request.

| Area | Status |
| --- | --- |
| [DXC compilation](shader_compilation_design.md), individual reflection, renderer-local compile/reflect job/report | Implemented within the checkpoint below |
| Generic schema composition and version-1 combined persistence | Implemented Foundation mechanisms |
| Production combined groups, component-library assembly | Unimplemented; completion contracts specified for review |
| Semantic contracts/mappings, PSO construction, providers and binding preparation | Agreed direction and policies; unimplemented |
| [Host-owned opaque bulk distribution](direct_bulk_connections.md) | Implemented fixed-capacity latest-state endpoints under direct-connection quiescence |
| Generic Host observation leases and schema-aware/independently leased bulk exchange | Agreed direction and policies; unimplemented extensions |

Rendering owns typed shader contracts, compiler/reflection integration and
intermediates. Host supplies generic assets, backing and lifetime services; explicit
exports may use Host asset retention. Foundation/Framework/Host/Executive contain
no DXC, SPIRV-Reflect or Basis Universal implementation. Public interfaces use
engine-owned data. Reflection consumes engine-produced artifacts; external binaries
would need a separate validation boundary. SPIRV-Reflect is not a SPIR-V validator.

All six conventional stages are supported by the implemented checkpoint. Neither
reflection nor aggregation proves device support, vertex compatibility, stage
linkage or PSO validity. Specialization constants and stream output are not planned;
CPU readback, DirectX and other deferred compilation/pipeline features remain outside
this scope. The exact version-1 reflection vocabulary below is distinct from the
new extensible application-semantic grammar.

### Scope and specification planning

Keep this overall reference available while refining one focused stage at a time.
The [stage plan](../backlog/shader_system_stages.md) records seven work areas and the
system-coordinator, stage-design and implementation-review workflow. Stage 1's
[scope proposal](pso_creation_stage1.md) now covers device/display audit, a presented
triangle and a verified compute job as separate increments for focused design.
It introduces Platform display/window cooperation and proposes a narrow explicit
compute buffer/readback case; general structure binding remains separate. This
extends the earlier creation-only proposal, not the implemented checkpoint above.
Host file access does not require Executive data integration.
Library assembly, semantic/PSO/provider work and generic Host/Framework support remain
distinct work areas, not one mandatory prerequisite chain. Runtime provider APIs,
closure observation and schema-aware bulk extensions are not implicit prerequisites
for library assembly. Each stage must state the validation it actually supplies.
See the [scope inventory](#scope-inventory-and-remaining-specification-work) for
settled decisions, completion choices and verification boundaries.

### Ownership and lifecycle glossary

Host already owns opaque bulk-connection backing; observation records and independent
bulk acquisition leases below are planned extensions. Rendering reports, schema
objects and vendor wrappers continue owning their local storage.

| Term | Meaning |
| --- | --- |
| Storage owner | Allocates and reclaims storage: Host for shared records/bulk backing, Rendering for local results. Allocation ownership alone does not prevent slot overwrite/reuse. |
| Decision authority | Serialises and commits transitions: the relevant coordinator for request/closure outcomes, Host for generic establishment/lease transitions, recipient for bulk acquisition versus withdrawal. |
| Coordinator / operational lease | Controller progressing work, with protection for state needed through publication/drain; it need not own the shared allocation. |
| Observation lease | Independently acquired protection for closure data/control state; grants no mutation authority and retains no closed requester resources. |
| Bulk acquisition | Granted protection for an exact publication/slot and schema backing, covering lifetime and prevention of overwrite/reuse until relinquishment. |
| Lease holder / cleanup custodian | Holder obliged/authorised to release a claim; custody handoff is explicit, preserves identity/protection and grants no successor-endpoint authority. |
| Owning result | Owns bytes or explicitly protects every borrowed dependency, including allocator/code support. |
| Independent copy | Owns copied data and required referents independently of the source acquisition; continued use no longer depends on source bytes/protection. |
| Borrowed view | Access without a new lifetime claim; copying a view or identity acquires nothing. |
| Availability / residency / readiness | Registered capability; backing currently present; selected version/dependencies protected and suitable for intended consumption. These are separate facts. |
| Quiescence | No further relevant access, including callbacks/publishers and applicable device use. Cancellation, delivered results or thread termination alone are insufficient. |
| Retirement | Deliberate provider/context withdrawal: close admission, drain the named scope, then dispose. Refresh/redundancy/unused-backing reclamation are ordinary operation. |
| Release / reclamation | Relinquish one protection claim / free or reuse storage after all relevant claims and accesses permit it. Payload and terminal-bookkeeping reclamation are separate. |

Moving a buffer does not acquire its borrowed memory context's lifetime. Copying
data independently may permit source release; transferring custody preserves the
source acquisition until the new custodian discharges it. Keeping an allocation
alive cannot justify reusing its slot while another user reads the old publication.

### Initial adapter checkpoint

The renderer-private API is `rendering/runtime/shader_reflection.hpp`.
`SRequest` borrows an existing `CShaderCompileReport` and a nonempty opaque UTF-8
component identity. `reflect` requires an empty `CResult`; an occupied result is
preserved and the call returns `invalid_input`. Compilation status, artifact and
diagnostics remain unchanged. `CReflectJob` supplies the existing batch callback
shape without adding Host messages or SYSTEM identities. Its `prepare()` must
succeed on the rendering/requester thread before dispatch. Each job owns a
nonmovable `CReflection` instance containing the private module and operation
allocator; the callback exclusively borrows that prepared instance. Preparation
failure leaves the result `not_executed`. Direct synchronous `reflect()` calls
instead own a temporary module on their calling thread.

This checkpoint accepts conventional entries in all six stages, with canonical
non-storage stage interfaces and checked literal execution modes. Resources
currently include separate samplers, sampled/storage
images and uniform/storage texel buffers, with fixed descriptor arrays. Actual
variable/type decoration ownership is preserved, including access flags; flags
are not propagated through associations. The bundled compiler currently rejects
multidimensional descriptor arrays during SPIR-V legalization; real-artifact
fixtures cover one-dimensional arrays, including extent one.
It emits the three ordered root groups and entry-point metadata, then validates
the complete types-only `CSchema` before publication. Uniform and storage buffers
now import 32-bit scalar/vector/structure storage, nested fixed arrays and matrices.
Storage-buffer runtime tails use buffer-layout interfaces and stride-bearing
sequence associations. Runtime matrix and array elements have private, one-member
physical structures so associations can target named declarations. The member
`storage` begins at offset zero; its inline descriptor retains dimensions,
strides and matrix interpretation, while the wrapper retains exact complete
size/alignment and has no fabricated source metadata. Runtime stride remains
independent. Raw/structured/typed interpretation follows available compiler
user-type metadata. Fixed-prefix runtime blocks, other resource categories and
execution modes outside the checked table fail explicitly. All
specialization constants reject, with identifiable shape/execution reasons where
available. Compile-time variants are the baseline; specialization constants are
currently unsupported and support is not planned, as recorded below.

Stage values use primitive leaves and separate vector, matrix, fixed-array and
structure interfaces. Matrices retain their compiled SPIR-V basis and have no
storage-major claim. Ordinary arrays, including extent one and inner arrays
inside a vertex wrapper, retain every dimension. Actual variable/member locations,
components, indices, built-ins, HLSL semantics and qualifier flags remain on their
owners; groups and referenced shapes do not acquire copies. Applicable nonempty
value groups follow the fixed order below. Conventional built-ins are classified
by stage/direction and their own facts, not the library's merged convenience flags.
For example, DXC's domain-stage `PrimitiveId` keeps its actual `Patch` decoration
on the occurrence while remaining in the system group.
Tessellation-control vertex inputs/outputs and tessellation-evaluation/geometry
vertex inputs use outer vertex-array associations; patch and system arrays do not.

Homogeneous reflected aggregates remain intact. An aggregate whose members need
different value domains currently rejects with `unsupported_interface`, including
type/member coordinates where available. This is an initial importer restriction,
not a declaration that the SPIR-V is invalid or a permanent design exclusion.
An aggregate carrying `Patch` on its containing occurrence currently also passes
the user-varying direction guard before member built-ins are classified: it is
admitted only for control-stage output or evaluation-stage input. Other directions
reject with `unsupported_interface`, even if a homogeneous built-in aggregate
could be legal there. The scalar `PrimitiveId` fixtures do not establish general
support for such Patch-decorated built-in aggregates.
No hierarchy is reconstructed after compiler flattening. Private shape, group
and wrapper identities remain deterministic and document-local; source names are
decorations. Resources, shapes, wrappers and groups share declaration/member limits.

The literal execution table admits compute `LocalSize`; tessellation spacing,
winding, topology, point mode and `OutputVertices`; geometry input/output topology,
`Invocations` and `OutputVertices`; and fragment `OriginUpperLeft`,
`EarlyFragmentTests`, `DepthReplacing`, `DepthGreater`, `DepthLess` and
`DepthUnchanged`. Stage masks, operand counts, positive literal counts, duplicates
and exclusive mode families are checked. Required local compute/fragment/geometry
and control-stage facts are checked, while tessellation facts permitted to reside
in another stage remain separate from future cross-stage/PSO validation. Unknown
modes, symbolic operands and unsupported correctness-relevant annotations reject.

Maintained fixtures cover DXC output from all six stages, scalar logical Boolean
inputs, patch versus vertex wrappers and inner arrays, qualifier combinations,
explicit component/index coordinates, homogeneous aggregate ownership, non-square
matrices, nested arrays of one, mixed-domain rejection, conflicting/unknown modes,
compatible qualifier combinations and rejection of mutually exclusive qualifiers,
exact shared count limits and allocation-failure sweeps. Controlled SPIR-V fixtures
exercise shapes the compiler can flatten; they do not broaden the production input
contract to unvalidated external binaries. Fixed-prefix runtime buffers remain
unsupported alongside this stage-interface support.

Physical declarations are cached by raw type, buffer layout context and matrix
storage interpretation. A shared SPIR-V type can require distinct UBO and storage
representations. Scalar/vector placement remains scalar-aligned for the selected
relaxed layout; vector members cannot straddle a 16-byte boundary. Aggregate
alignment follows the selected std140 extended or std430 base requirements.
Structures round their complete reusable extent to that derived alignment.
Array/matrix slots and explicit strides remain authoritative; overlapping owned
tails reject with `unrepresentable_layout` rather than clipping padding.

| Declared structure | UBO alignment/size | Storage alignment/size | UBO/storage array stride in fixtures |
| --- | --- | --- | --- |
| One `float` | 16/16 | 4/4 | 16/4 |
| One `float3` | 16/16 | 16/16 | 16/16 |

These are complete CPU construction/reuse representations, not inferred original
HLSL/C++ ABIs. The declared content of the two examples spans 4/12 bytes before
tail padding; that is neither instruction/liveness-based accessed range nor a
minimum descriptor-range claim. Device binding-offset alignment and descriptor
range policy remain outside reflection. Scalar-field vector representations do
not force every vector to own 16 bytes; e.g. `float3` plus a scalar can occupy
offsets 0/12. Runtime sequence stride is recorded separately even when it equals
the complete element schema size.

`CResult` owns the exact identity, diagnostic text and successful schema.
This checkpoint uses document-local declaration names; public qualification follows
the review-draft authoring contract and is not implemented here.
Schema access is const. Borrowed result views expire when their owner is cleared,
replaced or destroyed; the compilation report may be disposed after reflection
finishes. Ordinary engine allocator context lifetimes apply. Dispose results
before unloading the rendering module. Failures publish no schema. Diagnostics
retain scalar reasons/stages and optional SPIR-V coordinates, never discarded
staging handles.

Admissible identity is retained exactly on blocked compilation and ordinary
failure, including early context/admission rejection, and included in retained
storage accounting. Invalid or unbounded identity, allocation failure while
copying it, or the retained-result limit may leave it absent. Secondary identity
or diagnostic retention failure preserves an existing primary failure reason.
The diagnostic truncation flag also indicates text omitted because allocation
failed or the retained-result limit discarded the payload; the fixed structured
diagnostic remains available without allocation.

Service defaults are 16 MiB artifact, 262,144 ID bound, 1,048,576 instructions,
1,024 resources, 4,096 top-level stage values, 4,096 generated declarations,
32,768 generated members, 1,024 members per aggregate, eight array dimensions,
32 dependency levels and 262,144 document nodes. Dependency traversal has a
32-level implementation ceiling; service policy may lower it. Preflight covers
type dependency expansion and static function-call graphs before library
processing, including repeated paths through shared dependencies.

Retained names/metadata have a 4 KiB individual limit and 4 MiB aggregate limit;
diagnostics have a 64 KiB limit and truncation flag. Embedded source/debug blobs
use the artifact limit, not the retained-name limit. The retained-result default
is 16 MiB. There is no aggregate working-memory estimate or limit. Explicit
artifact, count, depth, expansion and retained-result limits remain in force,
as does the engine-wide 2 GiB per-allocation ceiling. Preflight checks the
conditioned size of each index allocation before attempting it.

The private `spirv_reflect_adapter.cpp` compiles unmodified vendored source.
Each nonmovable module owns a `CHybridAllocator32`: a chained buddy allocator
with 64 KiB payload backing per manager, a `std::max_align_t` minimum slot,
and a 16 KiB rounded-extent threshold. Larger requests use individually owned
memory tokens indexed by exact start address. This configuration favors fewer
underlying allocations over the smaller 16 KiB / 4 KiB alternative. The
[memory findings](../allocators/reflection_memory_findings.md) summarize the
request sizes and configuration trade-offs.

The module captures its allocation context at construction. The first buddy
manager and chain directory are prepared before vendor entry; failure is
recoverable and releases partial setup. Further managers and tracked storage
are allocated lazily. Vendor `malloc` and `calloc` requests have native
`std::max_align_t` alignment. Zero-size requests allocate at least one byte;
`calloc` checks multiplication before allocation and zeroes the requested bytes.
Backing bases also carry the generic buddy allocator's 128-byte alignment.

Vendor `free(nullptr)` is inert. Other frees must match a currently live start
address. Unknown, interior and already-freed pointers leave ownership unchanged
and increment a sticky audit counter. Valid frees reclaim their allocation;
buddy space may be reused immediately and empty managers remain until sweep.
After normal extraction, vendor destruction runs, then outstanding allocations
are counted before all backing, tracked payloads and metadata are independently
released. Any invalid free or outstanding allocation makes the result
`reflection_failed` / `allocation_audit`, overriding an extraction result and
preventing schema publication. Statistics retain the audit evidence after sweep.
Ordinary vendor creation errors also audit and sweep, without blindly calling
a destructor on partially initialized vendor state. The noexcept C++ destructor
only releases ownership and never calls vendor code.

This protects allocation bookkeeping and catches observed cleanup regressions.
It cannot prevent arbitrary vendor reads/writes, detect a stale pointer whose
address has been reallocated, or make corrupted vendor traversal safe. Guards,
poisoning and delayed reuse are not part of this integration.

Hybrid ownership grows as needed; allocation and cleanup auditing remain
independent of shader size predictions.

A bounded nonallocating scan validates instruction boundaries, counts exact
preflight index capacities and checks explicit service limits. Source/debug
strings must terminate within their instruction, and continuation records need
an earlier embedded source. These are parser-safety checks, not sizing heuristics.
After index allocation, DAG traversal bounds type and static call-graph expansion,
and a separate root pass validates referenced types and entry-point functions.
All of these checks precede vendor entry. There is no aggregate memory cap.

Allocation failure after vendor entry still invokes `MV_PANIC`: upstream
allocation/partial-cleanup paths have not been established as null-safe. The
best-effort panic records identity (bounded to 512 bytes plus its full length),
input size, request dimensions, manager and tracked counts, live extents and
deep owned bytes. Disposable subprocess tests cover chain growth, tracked index
and payload failure, oversized requests and multiplication overflow. Ordinary
failure sweeps cover recoverable preparation and engine-side allocations.

Direct create/destroy entry and completion records bypass informational macro
filtering. Audit diagnostics use the same immediate reporting path. Reporting
uses fixed formatting storage but CRT formatting/output and lock contention
mean it is neither guaranteed durable nor guaranteed allocation-free. Scoped
thread-local routing restores the previous owner after nested calls; concurrent
jobs have independent owners. Storage remains alive through extraction and
vendor destruction. Worker completion releases vendor storage before publication;
the requester retains the small prepared module object until drain/destruction.
Copied results never borrow vendor storage.

`SStorageStatistics` reports storage and audit evidence:

- `preflight_bytes` records actual index storage, including partial allocations
  on failure; `reflection_owner_bytes` records prepared module storage and
  `retained_bytes` records result storage. Preflight size rejection records zero
  allocated index bytes.
- `vendor_peak_live_bytes` counts peak live usable extents: rounded buddy blocks
  and requested tracked sizes, not original request traffic.
- `vendor_peak_owned_bytes` includes payload backing and all allocator metadata.
  Both peaks sample completed allocator operations; transient old/new directory
  overlap during growth is excluded. Setup failure leaves them zero.
- `vendor_invalid_frees`, `vendor_outstanding_allocations` and
  `vendor_outstanding_bytes` retain the audit before the independent sweep.

Regression coverage includes null/zero requests, alignment and zeroing, mixed
buddy/tracked storage, chain growth, interior/foreign/double frees, cleanup
omissions, failed publication, captured-context isolation, nested/concurrent
routing and fallback reclamation. Isolated fatal tests cover allocation failures
after vendor entry. Sparse IDs, nested/shared types, source replacement and
continuations, and generated large shader workloads remain covered. Oversized
preflight storage and invalid root references are rejected before vendor entry.

### Renderer-local composed conditioning

`rendering/runtime/shader_conditioning.hpp` provides the nonmovable
`CCompileReflectJob` and move-only `CCompileReflectReport`. Requester-side
preparation copies the bounded opaque identity and service policies, constructs
the job's private reflection instance and initialises the DXC factory before
dispatch. A separate preparation status reports setup
failure; both stage reports remain `not_executed` and an unready job is not submitted.
The compile request/member and complete source-bundle backing remain borrowed.

The one-shot callback runs compilation followed by reflection synchronously on
its executing thread. Compilation failure takes reflection's blocked path without
vendor parsing. Reflection failure preserves the compiled artifact and compiler
diagnostics, retains its own diagnostic and publishes no schema. Both independent
operations and their individual job wrappers remain available.

The submitting owner keeps stable jobs and borrowed inputs alive through terminal
response consumption. Multiple jobs and groups can share immutable bundle backing;
its owner must retain it until every borrowing job drains. Batch exit is scoped to
the requester route, not a group. Queued, inline, rejected and discarded outcomes
remain the existing batch client's responsibility and are retained separately from
the two stage statuses. Reports can move out after completion and outlive jobs and
inputs; captured allocator contexts and rendering code must outlive the reports'
final destruction.

Maintained tests exercise the production callbacks through `CBatchClient` with
controlled native workers: overlapping groups, out-of-order collection, independent
routes, exit/context-install discards, queue-saturation inline fallback, stage/setup
failures and retained reports after input/job destruction. These are not live Host
pool or DLL-unload tests. Standalone job tests also verify that allocation hooks
on two workers route to the exact distinct instances prepared by the requester,
that vendor access and allocator reclamation finish before return, and that owner destruction follows
drain while retained reports and their allocation contexts remain alive.
Rendering's compile-only request/result contract is unchanged;
this checkpoint adds no production combined-group service or publication envelope.

## Existing schema foundation

Use Foundation schemas for reflected physical types, checked non-storage
associations/interfaces and preserved decorations; Rendering interprets its
semantic vocabulary. Relevant contracts are [composition/persistence](../schema/persistence-and-aggregation.md),
[runtime APIs](../schema/runtime-api.md), [schema specification](../schema/specification.md)
and [combined storage](../schema/common-binary-block.md).

- `CSchema` is an editable binary-free aggregate of optional types/instances/data.
  Runtime locators, lengths and payloads do not belong in its literal tree.
- `compose_from`, `merge_from` and authoring save admit structural fragments with
  unresolved references/incomplete values. Full validation, materialisation and
  bake require contained types and valid data, with no external types fallback.
- Named definitions are indivisible: merge cannot extend member lists, complete
  missing intrinsic fields from another partial definition or infer identity from
  different names. Compatible duplicates can combine permitted defaults,
  decorations and presentation metadata; conflicts preserve original diagnostics.
  Relevant unresolved dependencies cannot be treated as a compatibility proof.
- Associations/interfaces are checked non-storage references/groups within one
  complete schema. Immediate targets retain identity; chains are not aliases,
  cycles reject and physical construction cannot consume non-storage declarations.
- Live/baked promotion preserves binary values and selection masks. `import_from`
  replaces the selected role's section, imports no linked types implicitly and
  reconstructs literals without a general padding/bit/spelling fidelity guarantee.
- SPIR-V can enter live bulk backing without one literal per word. Edit types only
  while unbound; later changes need independent replacement and checked rebinding.
  Direct instance edits use `begin_edit`/`complete_edit`; instance propagation
  policies and bulk writes remain distinct. Borrowed owners stay stable/alive;
  these APIs supply no concurrent-mutation semantics.

Authoring merge and byte-preserving runtime assembly are distinct. Neither performs
multi-source payload packing or locator relocation automatically. No additional
generic schema feature is needed merely to describe matrix shapes, stage/resource
interfaces or runtime buffer-element sequences.
Admitted incomplete fragments are not successful standalone reflection or publishable
libraries; those results require every dependency for validation.

## Intended PSO and resource-binding system

Build towards reusable PSOs with validated stage connections, supported connection
optimisation and introspection of required bindings, types and usages. Explicit
annotations, registered conventions and user configuration establish meaning; the
engine derives supported bindings/usages from that evidence. Names or layouts alone
cannot identify a transform, material or other application meaning.

The goal is to eliminate arbitrary boilerplate while preserving configuration.
Recognising a material contract does not select a material, variation or buffer
element. Providers may expose singular or selectable supplies; configuration or a
declared convention resolves choices. Ambiguity remains explicit.
Selectable provider items are distinct from compile-time variants; a selection may
separately require a compatible compiled target. Choices affecting compilation or
fixed pipeline state belong in PSO construction, apart from selections when using it.

Maintain three independent checks: semantic compatibility, concrete representation
compatibility, and satisfaction of the configured use. Final PSO introspection
describes the produced pipeline, including optimisation's effects on active
requirements, while retaining the distinction from authored semantic expectations.
Cross-stage/vertex/topology validation and PSO construction are later operations,
not consequences of reflection or schema aggregation.

### Compiled PSO library persistence

Support saving and loading compiled PSOs as a library file, rather than persisting
individual PSOs as separate files. Record the compilation environment alongside
compiled data, including hardware/device identity and driver version, plus backend/
toolchain facts needed to establish compatibility.

On loading, validate the recorded environment against the actual loading system
and establish that the backend can use the saved compiled data. Regenerate the PSO
library for that system when compatibility checks fail or the saved data is unusable.
The persistence design must account for the PSO descriptions, shader components and
effective configuration needed for regeneration. The review draft selects embedded
immutable component dependencies with portable recipes for the initial file.

Keep backend-compiled PSO data distinct from portable shader-component artifacts
and their reflection/semantic descriptions, even if packaged together. Exact file
format, environment keys, compatibility rules, backend payload representation and
regeneration/publication policy are specified in the review-draft persistence section.
This is future PSO work, not a
capability of the implemented DXC-to-SPIR-V or component-reflection path.

### Scope, updates and binding schedule

| Concern | Responsibility |
| --- | --- |
| Selection scope | Contracts/mappings describe meaningful scene/view/entity/material relationships; configuration binds concrete instances |
| Updates | Providers describe supplied scope, availability and change/update guarantees |
| Binding schedule | The binding plan derives a valid schedule from requirements, provider capabilities and configuration |

An entity transform can persist across frames and serve many draws. Scope is not
an update rate. Reflection supplies access representation/resource constraints;
shaders may declare additional requirements. Incompatible constraints fail provider
validation without either side silently overriding the other. Initial mapping
records have no combined frequency field; concrete frequency/scheduling policy
belongs to provider and binding-preparation design.

## Library description, identity and catalogue

### Authoring and component identity

```text
library -> source file -> conceptual label -> stage -> variant -> named compilation target
```

Conceptual labels and variant names have library-wide meaning. Source nesting
organises declarations; a conceptual group may span files. Each target explicitly
specifies entry point and compilation settings/defines. One entry point may produce
several targets. Preserve declaration order in reports and catalogue. Sources/includes
are bundle-relative; retain existing compilation defaults and define precedence.

Within a resolved library, identity is **concept/stage/variant/target**. Target names
are unique in that scope and may recur elsewhere. Duplicate full identities reject
across source files. Source path and entry point are properties; moving declarations
or changing implementation/options need not rename a target. Preserve provenance
separately; existing diagnostic fingerprints are not cryptographic cache keys.

A declared library name is descriptive, not globally unique. Explicit contextual
asset bindings/aliases resolve the intended library before selecting its component;
separate assets may repeat declared names. Runtime `CAssetId` values are repository-
local identities, not persistent references, fingerprints or backing acquisitions.

PSOs explicitly compose reusable components, whose variant labels need not match.
Hull/control and domain/evaluation remain separately selected targets. No inferred
stage selection, fallback, pairing or permutation expansion; future shorthand must
resolve to this model. Unknown properties/conflicting declarations reject.
The authoring format version is separate from the generic runtime block version.

### Catalogue and semantic separation

The catalogue identifies compiled targets and locates code, reflection and build
information. A separate semantic model defines reusable meaning, logical structure
and invariant use. Independent occurrence mappings link them; annotation extraction
produces equivalent records through the same validation path. Contract definitions
are not copied into each component record. Separate structures do not require
separate assets/files. The draft manifest keeps their tables distinct in one library.

Identify duplicate descriptions where declared meaning, structure and use agree,
including across libraries. Reuse descriptions without merging component identities,
resource/provider instances, library resolution or stage interfaces. One logical
meaning can have different validated physical representations. Equal source names,
type spellings or layouts do not establish semantic equivalence.

Explicit references to shared complete contracts are the primary reuse mechanism.
Compare definitions encountered through resolved contexts, including imported
libraries, to identify equivalent descriptions under the strict equivalence rules.
Different identities still require explicit validated equivalence. Reuse does not
require a global registry, repository-wide discovery service or automatic
canonicalisation system; broader discovery tooling remains optional future work.

### Catalogue records

Use a flat ordered collection of component records, preserving declaration order.

| Part | Agreed contents |
| --- | --- |
| Library header | Declared library name and catalogue-format version |
| Component identity | Concept/stage/variant/target tuple |
| Compiled content | References to retained code and validated reflection roots |
| Build provenance | Source, entry point, effective options/defines and diagnostic origins |
| Semantic links | References connecting components to independent mapping records and their resolution contexts |

Reflection schemas remain authoritative for physical layouts; semantic definitions
remain in their separate structure. Catalogue positions can support local lookup
without becoming persistent identities. Failed components remain visible in build
reports, while the production catalogue follows atomic publication. Exact fields,
reference syntax and encoding follow the completion contracts below.

### Catalogue naming and references

The review draft resolves names, explicit variants, structured references and
reversible private qualification in [authoring and identity](#completion-authoring-and-identity).
Original source names remain diagnostic provenance rather than public identity.

Public component identity, qualified declaration identity and compact descriptive
type names remain distinct. Exact encoding/constraints and detailed comparison
rules require agreement; cryptographic cache design is outside this work.

## Semantic contracts and grammar

### Identity, definition and equivalence

Contract identity is **semantic namespace/name/exact revision**, independent of
library names, assets, providers and source files. An explicit semantic context
supplies/imports definitions; aliases resolve exact identities. No latest selection,
revision fallback, first-loaded precedence or mandatory global registry.

For one identity/revision, equivalent definitions share one effective definition
with all origins retained. Missing referenced definitions or incompatible claims
fail library validation. A known contract may await a provider. Different identities
require explicit validated semantic equivalence; matching layouts/prose are not proof.
Revision denotes semantic/logical definition, distinct from physical layout, build,
model encoding and catalogue/common-block versions. Revision adaptation is not implied.

Definitions are complete; mappings/providers may reference selected members without
redeclaring a shortened contract. Strict structured equivalence compares resolved
exact-revision dependencies, logical member identities/shapes/cardinalities/relations,
and all correctness-relevant semantic properties/invariant use constraints. Ordering
matters where logically positional, not merely presentational. Documentation/origins
do not affect equivalence; report differing paths with both origins.
Binding validation requires the members selected by mappings, not necessarily every
member of the complete contract. Layouts, descriptor coordinates, provider instances,
updates and schedules remain in their respective structures.

Missing correctness information is not a wildcard. Only declared vocabulary defaults
apply. No partial-definition composition or implicit cross-contract adaptation in
the initial model. Contract equivalence, representation compatibility and binding
satisfaction are separate; declaration agreement does not prove implementation values.

### Extensible vocabulary and term rules

Use a structural grammar, reserved base definitions and complete application
definitions referencing existing definitions. Vulkan/shader facts keep their
qualified meanings; applications can add defined semantics. Custom terms expressed
through understood grammar can be checked generically; new executable behaviour
requires explicit support. Recognising syntax does not provide a conversion.

| Initial family | Applicability and required information |
| --- | --- |
| Logical shape | Complete resolvable scalar/vector/matrix/aggregate/sequence descriptions using existing schema facilities where suitable |
| Semantic role | Position, direction, normal, transform, colour, quantity or identifier/reference roles permit declared shapes; shape does not imply role |
| Scope | Symbolic scene/view/entity/material relationships where meaningful, independently of concrete selection |
| Units | Measurable values identify quantity/unit; dimensionless differs from unspecified |
| Frames/origins | Declare the frames, reference origins and relationships required by the action; semantic frame and numerical reference origin are distinct |
| Mathematical interpretation | Conventions wherever differing interpretations affect meaning, including matrix action |
| Representation/precision | Explicit supported representation/operation profiles, conventions and operating conditions; no general numerical-error proof system |
| Colour interpretation | Interpretation required by the colour definition, such as transfer/primaries; alpha properties apply only where alpha is represented |
| Value constraints | Applicable, mutually consistent domains/ranges, finite-value and unit-length requirements as distinct predicates |

Every term defines value domain, attachment, dependencies/required properties,
exclusions, scope/propagation and equivalence. Required absence fails; optional
absence has exactly its declared meaning; defaults must be explicit; inapplicable
properties reject unless permitted. There is no generic implicit inheritance or
override. Unknown correctness-relevant claims cannot be ignored or repurpose reserved
terms. Concrete term spellings/namespaces/imports and extension rules are specified
in the review draft; existing version-1 reflection decorations are not
automatically extended.

Use typed symbolic roles/relationships, coherent mapping bindings and known predicate
definitions, not unrestricted expressions or automatic computation of missing data.
Predicates declare argument domains, required information and validation meaning;
separate declaration checks from checks of actual data when available. A predicate
name alone provides neither an implementation nor proof of runtime truth.

Transforms must cover matrices, quaternions, rotors and explicit compound forms.
Each form needs complete shape/action/convention/capability/normalisation requirements;
none is inferred from component count, storage order or a shared frame relationship.
Common role definitions are reusable without asserting structural equivalence or
automatic conversion. This does not enable additional compiler numeric features.

### Action, frames/origins and representation/precision

Keep three declarations distinct:

| Concern | Contract responsibility |
| --- | --- |
| Action | State the operation and what it acts on: rotation, displacement, placement, deformation or an explicitly defined composition |
| Frames/origins | State relevant coordinate frames, reference origins, operand relationships and composition order |
| Representation/precision | State how data encodes the action, numerical requirements and guarantees providers must satisfy |

Translation is a member only where the selected representation requires it.
Rotation-only, displacement-only and combined representations without separate
rotation/translation fields must remain expressible. No universal rigid-pose record,
matrix form, local/world choice or entity/scene relationship is implied.

For offsets, declare their frame and position in any composition: source-frame
displacement before rotation differs from destination-frame displacement afterward.
Origin-relative placement is another explicit choice. Compatible shapes do not
permit the engine to interchange these actions or infer a conversion.

A value can describe placement in a world frame while encoding a small displacement
from a selected anchor. Do not require an intermediate large absolute floating-point
position. Cell-plus-offset and split representations can have their own complete
definitions and precision requirements. Keep logical numerical representations
distinct from reflected physical offsets/strides.

Contracts/uses supply requirements; providers advertise supported profiles and
operating conditions; binding resolution validates their match. Frame choice alone
does not establish suitability. Complete definitions still specify structure, action and
relationships through known definitions/predicates; flexibility does not introduce
loosely interpreted optional fields or an unrestricted expression language.

### Supported transform and operation profiles

Use a finite, explicitly supported catalogue of transform representations and
operations. Profiles identify representation/algebra/coefficient conventions;
spatial action, origins, translation order and rebasing conditions; supported
construction/interpolation and its normalisation/sign/path/continuity conditions;
and runtime versus authoring/storage use with explicit conversion routes.

Quaternion, dual-quaternion, rotor/vector and basis forms are candidates, not a
requirement to implement every form. Combined representations keep their complete
action/coupling conventions. Vector-based rotation and basis construction are
first-class catalogue candidates. Angular/logarithmic forms can serve authoring,
storage or conversion without becoming mandatory dynamic representations.

Use practical, qualified operating envelopes, especially displacement ranges,
reference origins and translation order. Providers advertise profiles; validation
checks compatibility and necessary data conditions such as finiteness,
normalisation and non-degeneracy. Continuity requirements belong to supported
construction/interpolation profiles and their conditions, including segment joins.

The semantic grammar stays extensible, while executable operations require explicit
support. There is no general error-budget propagation, first-principles precision
proof, arbitrary mathematical evaluator or implicit numerical repair. Detailed
profile choices, algorithms and tolerances are deliberately left to later catalogue
design; the [profile record contract](#completion-semantic-records-and-profiles)
defines the integration boundary without selecting that repertoire.

## Semantic mappings and contract uses

### Mapping records

| Part | Agreed content |
| --- | --- |
| Component | Contextual library binding plus concept/stage/variant/target, not file/entry point or catalogue position |
| Shader selection | Explicit backend-qualified resource coordinates, initially Vulkan set/binding; content role (whole resource, fixed contents or repeated element as applicable) and optional member/element-type path |
| Semantic selection | Context-qualified contract use resolving an exact contract, optionally selecting a logical member |
| Provenance | Independent declaration or shader annotation and source location for diagnostics |

Independent records can map unannotated shaders without editing their source.
Member names resolve uniquely; ordinal selection is explicit, never a fallback
after failed name lookup. Paths traverse occurrences/types, not runtime material
indices. Private names, SPIR-V IDs and schema indices are not durable authoring
selectors. The completion contracts define reference/path records. Runtime implementations
may use local indices after resolution.

Both shaders and registered providers can contribute semantic/structural definitions;
neither is universally authoritative. Plain reflection supplies structure, not
missing application meaning. A matching provider cannot establish an unannotated
shader's meaning without an explicit semantic connection/convention. Preserve origins
and validate claims together without silent overwrite or source-order precedence.

### Resolved declaration forms

Resolve every declaration origin into one form per record kind: annotations and
independent mappings produce resolved mapping records; shader- and provider-origin
contract definitions produce resolved contract definitions. Resolve contextual
references/exact revisions and apply only declared defaults through shared rules.
Validation, equivalence, introspection and binding consume the appropriate resolved
records rather than maintain origin-specific interpretations. Retain every original
claim/source location for conflict diagnostics; resolution grants no precedence.

This preserves separate catalogue, mapping, contract, use and provider structures.
It introduces no partial-definition merge, inferred semantic identity or common
record containing every concern. The review draft selects manifest record encodings.

### Contract-use identity and role binding

A complete contract declares typed symbolic roles and relationships. An explicit
contract-use instance is their consistency boundary, not a partial definition.
Authored uses have context-qualified names unique within the mapping/binding context;
imports use explicit bindings/aliases. A use declares its exact contract, role
bindings and outstanding selections. Mappings select the use/member; an optional
expected contract identity must agree. Equal names in different contexts do not
establish sharing. Several components may explicitly reference one use.

Each role has one consistent binding within a use, including independently mapped
members. Distinct uses stay independent; cross-use relationships are explicit.
Repeated `entity`/`world` spellings have no global binding meaning. Bound instances
combine the resolved authored use with concrete selection context; one declaration
can serve different entities/materials without requiring an allocation per draw.

Providers fulfil established roles, never silently redefine subjects/frames.
Registrations advertise contracts/members, representations, scope and singular/
selectable behaviour without knowing every use name. Binding resolution associates
the bound use with providers/items; provider IDs are not embedded in shader mappings.

### Strict validation boundaries

Overlaps must agree: no implicit refinement, most-specific-wins or source-order
override. Contradictory claims fail with component/occurrence, contract paths and
origins reported. Unexplained unmapped binding relationships fail. A known mapped
contract awaiting a provider is valid but visible; missing contracts/mappings cannot
be relabelled as awaiting providers. Detailed structured comparison and concrete
coverage encodings follow the review-draft comparison/coverage contract.

| Boundary | Validation |
| --- | --- |
| Library | Resolve declarations/contracts, check overlap/coverage and reflected occurrence/representation compatibility. Run declaration checks before compilation where possible, annotation/reflection-dependent checks as soon as available. Log known conflicts immediately; independent checks may continue. |
| Provider | Validate supplied contracts/representations, roles, singular/selectable behaviour and configured selections. Required relationships must close before binding use; ambiguity/incompatibility/unsatisfied requirements block admission. Run offline when known or at later registration/configuration. |

Provider registration cannot repair a library conflict or invent missing semantics.
Validated library content with outstanding providers is distinct from a usable bound
configuration. Adaptation must be explicitly supported. Treatment of mappings for
compiled-out declarations follows the typed coverage and inactivity evidence rules
below. Logical reason codes, witness schemas and capability-profile contents are
agreed. The baseline DXC-to-SPIR-V profile below describes current support; completion
contracts define encoding and the boundary for separately qualified evidence extensions.

Retain established facts at their validation boundaries. Library validation establishes
resolved declarations, mapping agreement/coverage and reflected compatibility.
Provider resolution establishes eligible associations and a coherent configured plan.
Ordinary preparation reuses those unchanged facts while checking concrete selections,
versions, dependencies, coherence/freshness, protected readiness and whole-composition
admission; it need not repeat contract equivalence or provider search per draw.

Relevant declaration/reflection changes require affected library checks again.
Changes to provider registrations, capabilities, representations or bound role/
association requirements require affected plan checks again. Ordinary data/version
changes normally refresh prepared units; applicable value constraints still require
checks when actual data is available. Reuse never waives a required check or treats
an outstanding provider as resolved. Concrete caching/invalidation mechanisms remain
open; preparation is ongoing enforcement within the provider boundary, not a third
independent semantic-validation system.

### Mapping coverage and inactivity

Coverage and provider fulfilment are separate facts. A valid mapping to a known
contract establishes meaning while its required provider remains outstanding;
a missing mapping cannot acquire that status.

Every active binding-relevant occurrence needs an explicit explanation: a validated
mapping, a registered convention producing equivalent records, or an applicable
reserved backend meaning. Descriptor coordinates and physical layout alone do not
explain an application binding.

Aggregate mappings cover members only through validated structural correspondence.
Mapping one member does not silently cover its siblings. Selecting a member of a
complete semantic contract does not require supplying the contract's other members.
This imposes no per-field providers or runtime acquisition/update granularity.

### Typed structural coverage

Each coverage obligation belongs to a particular component, resource/content role
and typed structural path. Reusing a type in another occurrence does not transfer
coverage to it.

| Structure | Coverage granularity |
| --- | --- |
| Resource | Account for the binding relationship and selected contents where applicable; one mapping may establish both |
| Scalar, vector or matrix | Treat the declared logical value as a unit and validate its complete shape/interpretation without requiring component-by-component mappings |
| Aggregate | Account for binding-relevant members recursively; validated aggregate correspondence can cover them together |
| Array or sequence | Cover element structure and declared cardinality/selection relationships; homogeneous repetition needs no mapping per runtime element, while position-dependent meanings require explicit correspondence |
| Padding | Physical gaps/padding create layout obligations, not semantic values; apparent non-use alone cannot classify a named member as padding |

### Inactivity evidence requirements

A missing selector fails unless positive conditioning evidence connects the mapping
to a valid declaration eliminated or proved not to participate in that particular
compiled component. Absence from final reflection alone is insufficient. Typos/stale
selectors still fail. Annotation-generated and independent records use the same
evidence standard; physical shared-type selections retain strict resolution.

| Required evidence | Establishes |
| --- | --- |
| Exact conditioning scope | Component, selected entry point, effective compilation inputs/settings and resulting artifact; component identity alone cannot distinguish changed implementations |
| Positive declaration resolution | The selector resolved to a valid declaration and structural path before removal; matching names or nearby annotations are insufficient |
| Justified inactive result | Compiler/conditioning evidence establishes elimination or non-participation, rather than merely an absent reflected result |
| Auditable provenance | Mapping origin, declaration correspondence, evidence producer and reason are retained |

A surviving reflected aggregate retains its coverage obligations unless equally
strong evidence establishes inactivity for a particular occurrence. An eliminated
ancestor can justify inactive descendants only where their paths were positively
resolved beforehand. Inactive semantic mappings do not waive concrete layout,
stride or resource requirements remaining in reflection.

Text excluded by preprocessing before declaration validation does not establish a
valid declaration subsequently removed. Scope mappings to applicable targets rather
than silently classify such exclusions as inactive. Insufficient evidence fails
validation with the unresolved selector or missing proof explained.

### Conditioning evidence package and production

Use a versioned, Rendering-owned evidence package associated with a particular
component build. Share build context across records and retain these record groups:

| Part | Contents |
| --- | --- |
| Build context | Component identity, selected entry point, effective input/build provenance, toolchain/conditioning configuration and association with the resulting artifact |
| Declaration correspondence | Validated declaration identity, source origin, resource/content selector and typed paths, with correspondence to surviving final reflected occurrences |
| Inactivity facts | Supported proof kind, producer/stage, affected declaration/path, reason and supporting facts required by that proof kind |

Internal declaration keys can be package-local; they are not persistent authoring
identities. Records own retained audit data or reference owned result/library storage.
The coverage validator derives decisions from the facts and final reflection;
an author-supplied inactive flag cannot substitute for evidence.

1. A supported compiler integration or conditioning stage establishes positive
   declaration/path correspondence before removal for the mappings needing it.
2. Supported compiler stages or explicit transformations emit inactivity facts under
   defined proof contracts. A pass name or diagnostic string alone is insufficient.
3. Reconcile with the final retained artifact/reflection. Check correspondence and
   supported proof; missing/unsupported evidence leaves a claim unproven and fails
   validation wherever inactivity is needed to excuse a missing occurrence.
4. Retain compact supporting evidence with published inactive mappings. Failed
   attempts retain useful evidence/diagnostics in reports. Subsequent transformations
   affecting binding facts require renewed reconciliation.

Active mappings resolving/validating against final reflection need no inactivity
proof. Do not require every compilation to retain a full AST, intermediate
representation or optimisation trace. A tool path lacking the necessary producer
capability cannot classify unresolved mappings as inactive. Comparing presence/
absence across separate compilations may aid diagnosis but establishes no proof
by itself. The completion contracts define encoding and compiler-integration admission;
the package/flow is a design contract, not an implemented evidence facility.

### Supported inactivity proofs and validation

Use two primary proof kinds and one restricted derivation:

| Proof kind | Required basis and limits |
| --- | --- |
| Validated elimination | A supported compiler/conditioning producer identifies the validated occurrence, transformation and before/after correspondence, establishing elimination of its binding requirement. Renaming, lowering, relocation or replacement instead requires continued correspondence; disappearance of the original declaration is insufficient. |
| Selected-entry non-participation | Supported analysis establishes no binding-relevant participation for the exact selected entry point/build. Conservatively account for relevant accesses, side effects and escaping references; unresolved aliases or dynamic indexing count as possible use unless proved otherwise. |
| Descendant of an inactive ancestor | An accepted ancestor proof and positively resolved structural path establish membership in that inactive occurrence/build. This cannot transfer inactivity to another instance of the type or waive surviving layout requirements. |

Validate each claim against all of these requirements:

1. Recognise its proof kind/version and producer's supported analysis contract.
   That contract defines required facts, supported declaration kinds and granularity;
   whole-resource support does not implicitly certify individual members.
2. Validate exact build/artifact association, selected entry point, declaration
   identity and typed path. Matching names or component identities across builds
   are insufficient.
3. Check the structured facts required by the proof contract. A reason string,
   pass name or author assertion alone establishes no inactivity.
4. Reconcile with final artifact/reflection. Later binding-affecting transformations
   preserve validated correspondence or require renewed evidence. Conflicting
   active-use evidence prevents acceptance.
5. Require accepted ancestor proofs for the same build/occurrence containing the
   resolved descendant path. Dangling or cyclic derivations fail.

Claims concern participation fixed by the conditioned build. Runtime observations,
expected material selections and assumptions about future inputs establish no
inactivity. Unsupported/incomplete evidence leaves a claim unproven and fails
validation where an exemption is required. Ordinary semantic-definition and physical
representation checks still apply. No producer support is implemented or implied
by this proof vocabulary.

### Reason codes, witness schemas and capability profiles

Use these initial backend-independent semantic reason codes and typed witnesses:

| Reason code | Required witness contents |
| --- | --- |
| `binding_requirement_eliminated` | Transformation identity/configuration, input/output conditioning checkpoints, occurrence correspondence and structured facts establishing elimination of the binding requirement |
| `no_selected_entry_binding_use` | Analysis contract/revision, analysed checkpoint and selected entry point, plus coverage and alias/access facts required by that conservative analysis |
| `inactive_ancestor` | Accepted ancestor-proof reference and positively validated descendant-path reference |

The codes initially correspond directly to the three proof contracts; encoding
need not duplicate an independently stored proof-kind field. Backend-specific causes
and pass names are diagnostic detail. Validation failures use separate codes such
as `unsupported_profile`, `scope_mismatch`, `incomplete_witness`,
`final_artifact_conflict` and `invalid_derivation`; none establishes inactivity.

Every witness shares an envelope referencing package/build context, declaration
occurrence/path, proof-contract revision, producer capability profile and provenance.
Shared analysis records may avoid repeated facts. Each schema defines required fields,
reference targets and cross-field constraints. Missing facts, incompatible references
or unsupported schema revisions prevent acceptance; input/output checkpoint references
alone do not establish elimination.

A capability profile describes a concrete conditioning path, not merely a backend
or compiler name. Its contents are:

| Concern | Required declaration |
| --- | --- |
| Identity/configuration | Profile revision, compiler/adapter versions, backend/representation and supported configuration envelope |
| Production and validation | Separate supported proof contracts/revisions, declaration kinds and granularity; whole-resource support does not imply member support |
| Analysis boundaries | Supported shader stages, access forms and alias/indexing cases; cases outside them cannot receive a negative-use conclusion |
| Evidence requirements | Witness schemas and compiler/conditioning sources from which accepted facts can be produced |
| Transformation obligations | Evidence-producing points and subsequent transformations requiring preserved correspondence or renewed reconciliation |

Support is explicit: a package cannot enable an unknown profile by naming it, and
compilation/reflection support does not imply inactivity-proof support. A recognised
producer contract defines the basis of its claim; validation checks required facts,
scope and final consistency. This selects no general proof language or requirement
to retain optimiser traces. The completion contracts define logical wire records and
evidence-extension qualification; these contracts remain unimplemented.

### Baseline DXC-to-SPIR-V capability profile

The baseline describes the current pinned compilation/reflection path with no
inactivity-proof capabilities enabled. It is a specification profile, not an
implemented profile/evidence API. Future proof support requires explicitly qualified
profile revisions, with production and validation declared separately.

| Area | Current support and qualification |
| --- | --- |
| Toolchain | DXC `v1.9.2609`, pinned source revision `01b62ad47db7dee3dd33f90e7339cf9e860b3f93`; acquisition checks archive SHA-256, while the runtime guard checks version 1.9 and the reported revision prefix. Reports retain compiler identity. |
| Input/execution | Bundle-backed HLSL 2021, stage-specific Shader Model 6.0 profiles, all six conventional stages and one selected entry point per compilation |
| Defaults | Vulkan 1.1, optimisation disabled, source-debug information, warnings-as-errors disabled, row-major storage default and vector-relaxed buffer layout; source matrix declarations can override storage major |
| Selectable options | Implemented Vulkan 1.1/1.2/1.3 targets, disabled/O1/O2/O3 optimisation, three debug modes and independent warnings-as-errors. Representative tests do not establish exhaustive qualification of the complete cross-product. |
| Compilation checks | Bounded capability/extension inspection, feature-policy rejection and retained artifact requirements; no device qualification or general external-binary validation claim |
| Reflection | Post-compilation resource/layout/interface/execution facts within the implemented importer limits, followed by schema validation; composed jobs retain separate compilation/reflection outcomes |

The implemented checkpoints and [compilation contract](shader_compilation_design.md)
define the accepted subset and limits. Successful compilation does not promise that
every artifact is representable by the reflector. Ordinary 32-bit data, accepted
resource categories, literal execution modes and supported layouts remain bounded
by those contracts; unsupported input fails visibly.

| Proof contract | Production today | Validation today |
| --- | --- | --- |
| `binding_requirement_eliminated` | None | None |
| `no_selected_entry_binding_use` | None | None |
| `inactive_ancestor` | None | None |

Evidence-package production, semantic mapping validation and profile machinery
remain planned. Generic schema validation supplies none of these proof validators.
The preprocessing action retains diagnostics, not validated pre-removal declaration
correspondence. Source/debug metadata and reflected names are not witnesses; `-Od`
is not proof that no binding-affecting transformation occurred. Reflection success
establishes neither semantic coverage, stage linkage nor PSO validity.

Qualify future support per contract and granularity, initially considering whole
resources. Member support needs separate evidence/validation coverage; ancestor
derivation needs accepted root proofs and validated paths. Host-platform qualification
references its actual checkpoint: the [compile-only validation record](shader_compilation_design.md#integration-checks-before-declaring-support)
does not automatically qualify every combined configuration or GPU execution.

### Illustrative grammar exercises

These are agreed grammar exercises, not production identities or implemented
validator capabilities. Required roles and supported operation/representation
profiles resolve explicitly for a bound use; the examples supply no implicit
defaults or numerical proof obligation.

#### Rotation-only

The action `v_B = rotate(r, v_A)` maps free-vector coordinates between explicitly
declared compatible Euclidean axes conventions. State the direction of application;
no point origin, translation, entity or scene relationship is required.

One complete representation can specify a unit Hamilton quaternion with logical
coefficient order `(w, x, y, z)`. Another can specify a unit rotor with a declared
algebra, basis order, reversal and sandwich action. Shared action/role definitions
do not erase distinct representation contracts. The selected profile states finite-value
and unit-magnitude conditions and any construction/interpolation requirements. A unit rotation
does not require unit-length input vectors. Normalisation, coefficient reordering
and conversion remain explicit supported operations.

#### Framed displacement

A displacement `d_F` is expressed in frame `F` with a declared length unit. Its
application to compatible point coordinates is `p'_F = p_F + d_F`. The displacement
itself needs axes/units, not a reference origin; application agrees with the point's
coordinate convention.

Explicit composition chooses between `rotate(r, p_A + d_A)` and
`rotate(r, p_A) + d_B`: the first displacement uses source axes, the second
destination axes. Any supported conversion must meet the selected profile's conditions;
mathematical correspondence alone does not establish equivalent finite-precision
behaviour. Local mesh adjustment and scene-axis displacement can therefore have
different complete contracts/compositions without a universal pose structure.

#### Origin-relative placement

Let `P` be the represented point, `F` the semantic coordinate axes, `O` a selected
reference origin and `delta` the displacement from `O` to `P` expressed in `F`:
`coordinates_F(P - O) = delta`. This relationship requires no intermediate large
absolute floating-point `P`.

A use can bind a scene/world frame, nearby view anchor and supported placement
profile defining offset range and rebasing conditions. A provider supplies the offset and
establishes its association with the selected origin/version. That association can
be control metadata rather than a shader field, as declared by the representation.
Eligibility checks the advertised placement/conversion profile and operating
conditions, not scalar width alone. No high-precision GPU type is implied.

Rebasing changes the origin context. Dependent offsets and view data remain coherently
associated with that context; old prepared versions stay protected for existing
consumers. Rebasing need not retire providers or unrelated data. Wrong coefficient
conventions, displacement axes, origin versions or inadequate domain/accuracy
guarantees reject at the applicable validation/admission boundary.

#### Other exercises

A particular entity-to-world matrix case can select complete frame/unit definitions,
4x4 real shape, column-vector homogeneous action `p_world = M * p_local` and finite
affine values. Storage major remains separate. This is one explicit action/form,
not a requirement on the transform grammar.

A material exercise has `base_colour` (three-channel reflectance, linear transfer,
explicit primaries/convention, finite components in `[0,1]`) and `opacity`
(dimensionless fractional surface coverage, finite scalar in `[0,1]`). Members share
one material selection and normally arrive as one composite supply unit; member
selection does not require per-field providers. Definitions must resolve even while
providers are outstanding; actual bounds/affine checks require actual data.

## Explicit shared-type mappings

These mappings consolidate selected physical type declarations. They are distinct
from the semantic links above: a semantic contract may have multiple physical
representations, and physical consolidation alone does not establish a provider
binding or validate stage composition.

The library description names a shared physical identity and lists selections
from explicit components. Each selection identifies:

1. A component through its contextual library binding and agreed
   concept/stage/variant/target identity using the draft structured reference grammar.
2. A resource by backend-qualified coordinates, initially Vulkan set/binding.
3. Its content role: fixed buffer contents or a repeated element type.
4. An optional typed structural path selecting nested members or array elements.

For example, a shared `Particle` identity may select the repeated element type
from a compute component's storage buffer and a vertex component's storage buffer,
using each component's own Vulkan set/binding. The component-reference and mapping
syntax follow the draft authoring and mapping records.

A path uses steps such as `{ "member": "lighting" }` and
`{ "element_type": true }`. Element selection traverses a type, not a runtime array
index. Permit exact unique reflected member names and an explicit ordinal
alternative, optionally checked against an expected name. A failed name lookup
must not silently fall back to an ordinal. SPIR-V IDs, schema indices and
generated private names are not persistent authoring selectors.

Initially select named physical structures. Rendering validates complete layouts
and required semantics recursively, establishes corresponding dependencies,
assigns canonical shared identities and rewrites references as part of composition
and merging, then validates the complete aggregate. Merely renaming the outer
structure is insufficient.
Dependency sharing for an explicit mapping stays within the selected definition;
it does not establish semantic equivalence elsewhere from layout alone. The wider
contract-reuse direction above requires its own validated equivalence rules;
the planned physical mapping mechanism alone would not provide cross-library
semantic contract reuse.

All listed sources must resolve, including resources removed by compilation:
an absent selected resource is a diagnostic. A reflected declaration cannot
receive two different shared identities. Overlapping mappings must agree on
dependent identities. Conflicting layouts or semantics fail consolidation;
there is no implicit conversion, precedence or repacking. Keep source-specific
provenance in component reports/catalogue where needed so it does not conflict
on a shared declaration. Retain reports when consolidation fails.

## Provider resolution, compositions and preparation

### Eligibility and explicit composition

Filter providers visible in the binding context by exact contract revision/validated
definition, required member coverage, role/frame/selection consistency, concrete
representation, declared singular/selectable behaviour and lifetime/update guarantees.
Configuration narrows candidates but never waives checks. One eligible association
may be inferred; several require configuration that resolves uniquely. No registration-
order, name or first-available priority. An explicitly selected ineligible provider
fails with its reason and no fallback. No candidate remains visibly outstanding
where permitted and blocks closure of a required binding configuration.

Provider choice and item choice are distinct: a uniquely matched selectable provider
still needs a valid selector; temporarily containing one item does not make it
singular. Keep use identity, candidates and rejection reasons in diagnostics.

Multiple providers require an explicit composition, validated as a coherent whole-use
assignment. One supplying route covers each advertised output, including a whole
aggregate/subtree; competing/overlapping routes reject without override or fallback.
Named compositions may nest only acyclically; apply coverage/role/eligibility checks
through contributors and retain diagnostic chains. Do not assemble undeclared
mixtures, merge partial contracts or compute missing data implicitly. Concrete
composition declarations/references follow the draft provider/plan contract.

Material blocks, transform state and scene/view state are normal atomic supply
units. Introspection/member mapping does not imply per-scalar providers, allocations,
callbacks, transfers or update work. Atomic logical supply does not require hardware
atomic stores, contiguous allocation or a copy; packing/derivation needs supported
operations.

### Coherence, freshness and ordinary resource management

Composite units/uses declare coherence/freshness requirements; providers declare
their guarantees. Separate the reusable declaration, resolved binding plan and
prepared versions. Ordinary data changes refresh affected units; changes to provider
registration, representation or selection capabilities can require revalidation.
Units identify selection, change state, validity and consistency/update guarantees;
concrete version/change reporting follows the draft provider/plan contract.

Publish a prepared version only with validated ready inputs: internally consistent
units, coherent shared roles and any required common scene/frame snapshot. Independent
unchanged material data need not receive a new version every frame. Reuse unchanged
units; update dependent composites at unit/batch granularity. A prepared version can
reference several protected blocks. Preserve previous data after a failed refresh
only while its validity/freshness permits; otherwise block further use.

Registered capability, resident backing and readiness are distinct. Inactive providers
can release unused backing and remain available for later preparation. Refresh and
redundant-version/unused-backing reclamation do not retire providers. Retirement is
deliberate scoped withdrawal: close admission, drain and dispose. Reclaim only after
relevant users finish, without draining unrelated providers/contexts. Protected
versions/backing remain valid through CPU and applicable GPU consumption.
Retirement accounts for dependent plans, supplied data and executable callbacks.

### Protected preparation and admission

Resolve eligible supply, prepare protected versions, then admit dependent work.
Requests identify use, concrete selections, coherence/freshness and consumption
scope. Reuse suitable backing or prepare it. A protected result establishes
selection/version, representation/roles, guarantees, dependencies and backing
protection through handoff/consumption. Readiness is for that selected version,
not a global provider flag. Reclamation cannot exploit a gap between checking
readiness and transferring protection.

Contributors prepare independently; whole-composition admission checks coverage,
roles, snapshot/freshness and carries protection into accepted work. Scheduled
dependencies such as uploads may be enforced before consumption without blocking
the CPU for every device operation. Ready is protected supply, pending is incomplete
accepted preparation, and failed retains its reason. Readiness alone does not admit
work; pending/failure cannot silently substitute providers or use incomplete data.
On failure release unnecessary protections; providers may cache unused backing under
their policy. The completion contracts specify logical operations, capacities and enforcement.

## Preparation requests, completion and requester closure

### Identity, sharing and terminal commitment

Consumer requests, shared operations and prepared supply versions have distinct
identities. Requests use requester-context incarnation plus non-reused serial;
recreated requesters have new incarnations, exhaustion rejects instead of wrapping.
Caller correlations are additional metadata, not identity. Validate full identity
and requester authority; slots, addresses and provider names are not substitutes.

Compatible requests can share actual provider work without sharing outcomes, roles,
admission decisions or protection. Match provider/selected unit/representation and
compatible version/coherence demands, not merely a semantic contract. Each consumer's
freshness requirements remain independent. Schedule explicit dependencies without
blocking workers on each other; independent ready contributors may run concurrently.
Respect provider execution requirements and immediate/deferred completion. No new
thread system is selected.

Reserve request state and minimal terminal-result capacity before work can start,
including immediate completion. Every accepted request has exactly one terminal
outcome: ready with protected supply, failed with reason, or cancelled. Pending is
progress; rejection is a direct submission result. One preparation decision authority
commits terminal state and retains its result for delivery; workers report operation
results rather than decide consumer outcomes.

| Race/event | Rule |
| --- | --- |
| Cancellation processed before terminal commitment | Commit cancelled and detach that consumer's demand |
| Ready/failed committed first | Cancellation is too late, even before notification consumption |
| Operation completes after consumer cancellation | Serve remaining consumers or retain/reclaim under policy; do not revive the cancelled request |
| Duplicate/expired/foreign event | Preserve known outcome or reject; never affect another request |

Cancellation is independent of provider retirement and shared execution. Other
consumers retain their demand. With none left, stop queued work where supported;
running work stops cooperatively or drains with dependencies protected. Cancellation
acknowledgement does not prove drain. Check request/context/version/coherence/freshness
before ready transfer; superseded completions cannot overwrite intended current
state. Result delivery and whole-composition admission remain separate.

### Delivery and closure barrier

Collect owning results explicitly through the requester route. Notifications only
signal availability; durable retained results survive coalescing. Delivery may follow
completion order. Slow consumers constrain new acceptance rather than drop accepted
outcomes. Receipt transfers result/protection and settles delivery; discard releases
it without changing terminal outcome. Delivery retries create no second outcome;
expired request bookkeeping cannot redirect late messages.

Requester closure is `open -> closing -> closed`: seal admission, settle pending
requests under terminal commitment, receive/discard outcomes, drain every requester
access, then destroy requester state/release its resources and leases.

| Barrier obligation | Must establish |
| --- | --- |
| Accepted requests | All terminal outcomes committed |
| Retained outcomes | All received or explicitly discarded |
| Execution access | No queued/running callback, publisher or continuation can access requester state |
| Dependent ownership | No remaining requester-dependent backing, route, module binding or allocator context |

Closure does not revoke transferred results. They block closure only where they
depend on requester-owned resources. Independent shared operations may continue without requester access;
otherwise drain them. Record dependencies before work/references become usable,
release after final access and transfer without gaps. Necessary closing continuations
stay covered by accepted operations and register before becoming runnable. This
prevents a false zero while producers can still create references.

The closure decision authority commits closed only after sealed admission and all
obligations settle. Empty queues, delivered cancellation or zero pending requests
alone are insufficient. Track identities/blocking reasons at unit/operation granularity
with efficient counters and bounded diagnostics. Deadlines report still closing,
never prove safe disposal; accounting inconsistencies are not successful closure.

## Host observation storage and lifetime

### Placement and closure records

| Layer | Responsibility |
| --- | --- |
| Foundation | Reusable memory/containers and neutral mechanisms |
| Framework | Generic cross-component identities and acquire/read/retain/release contracts |
| Host | Stable shared storage, lease accounting, identity lookup and final reclamation |
| Rendering | Typed semantics, providers/preparation and authoritative dependency/closure decisions |

Host interprets no shader/provider meaning and adds no SYSTEM shader contracts.
Use one Host-owned closure record per requester incarnation. Its coordinator holds
an operational lease and progresses closure even without observers; repeated close
requests observe the same retained record. Independent observation leases retain
inspectable data, not requester resources. IDs alone acquire nothing.

Reserve minimal record/summary capacity before requester work is accepted. During
closure expose bounded copied progress snapshots, never borrowed dependency pointers.
After closed and reporting quiescence release live bookkeeping and retain immutable
owned terminal identity/state/diagnostics. Reclaim after closed, operational/coordinator
lease release, reporting/read quiescence and final observer release. Retain history
explicitly as handles or independent summary copies, not permanent records.

### Observation handles and coordinator shutdown

Closure identity names lifecycle-coordinator and requester incarnations. Acquisition
validates full identity and establishes protection in one owner-coordinated operation:
return an owning handle or fail without partial ownership. Closed retained records
remain acquirable; reclaimed IDs expire and are never reconstructed/redirected.
Initiating/observing closure atomically acquires its first handle, avoiding an
immediate-close/reclamation gap.

Handles are move-only; move transfers a claim, explicit retain acquires another,
release consumes it, identifier copying acquires nothing. Each acquisition has a
distinct non-reused identity with checked record association/authority. Duplicate,
expired or mismatched release cannot decrement another claim; exhaustion rejects.
Reads protect the record through creation of a bounded owned snapshot. Notifications
carry identities/change signals, not unprotected pointers; guaranteed later inspection
requires a previously acquired handle. Dropping observers does not cancel closure.

Coordinator shutdown closes operational admission, completes its requester closures,
seals external observation lookup at an explicit cutoff, drains accepted calls/
publishers, finalises independent terminal/control state and closes execution routes.
Existing owning handles continue read/retain/release without that coordinator running.
Bare-ID lookup reports coordinator closed after sealing; new incarnations never
receive predecessor operations. Establish observation of coordinator shutdown before
starting it. Old summaries need no dedicated background thread.

### Storage and destruction support

Own all lifetime dependencies from record creation: summary/control bytes, allocator
context/backend, read/retain/release/destruction code and any affinity-bound cleanup
executor. Current containers borrow context lifetime; moving their bytes cannot
establish this ownership. Arrange independent support at creation, without shutdown
allocation/migration. Returned snapshots have the same requirement in their chosen
ownership context.

Terminal payloads are data-only identities/states/counts/owned diagnostics. Remove
provider-specific destructors, borrowed strings, callbacks and live graphs before
shutdown completes. Generic cleanup must not post to a stopped coordinator. Finish
reads/reporting, destroy contents while code is valid, deallocate while allocator
state is valid, then release remaining support through still-safe machinery.
Observers should not retain a whole provider/Rendering coordinator for history;
any unavoidable module dependency must be protected explicitly.

Use existing tokens/buffers/checked containers and suitable transports under a narrow
generic observation lease protocol. Payload ownership alone supplies neither lease
accounting nor context/code lifetime. Existing live service borrows are not an
automatic implementation of detached summaries. Host teardown accounts for remaining
leases/control calls before releasing support. The draft specifies the concrete
logical operations; it selects no general shader-object ownership or new worker thread.

## Host-owned bulk exchange and ordered POD operations

### Implemented opaque distribution checkpoint

[Direct opaque bulk connections](direct_bulk_connections.md), committed in `2e2a8b4`
on top of `CBulkBufferTransport` (`0a1db44`) and corrected `TBulk` ownership/
acknowledgement semantics (`ea24d44`), provide the current baseline:

- Host owns three fixed-capacity 128-byte-aligned buffers. Views expose exactly the
  requested capacity; exchange allocates/grows no storage and preserves the held slot.
- Either role initiates through the shared direct POD/bulk coordinator. Both exact
  module instances accept capacity, producer role and contract version before
  readiness/activation. One bulk and one independent POD connection can coexist per
  unordered exact-instance pair; the shared limits are 32 overall/eight per participant.
- Endpoints are confined to their designated service threads. Publication replaces
  unacquired staged state without waiting for consumer capacity. No data is initially
  published; no schema, used length, publication identity or POD ordering is supplied.
- Producer views expire on publication. Consumer `discard_and_acquire` relinquishes
  its previous view even when nothing new is available. Copied views acquire no ownership.
- Local close disables new local endpoint operations; the peer stops on its closing
  control. Existing held views serve only accounted work completed before quiescence.
  Host retains backing/descriptors/bindings/notifications until both sides quiesce;
  held views have no independent lifetime after that acknowledgement.
- Publication precedes notification. Wake failure after publication is not failure
  to publish and must not trigger retransmission. The underlying `TBulk::publish`
  boolean concerns acquisition of the previous staged publication, not publish success.

The linked reference owns API details and recorded validation. Schema pinning and
acknowledgement, context-qualified transport names/multiple logical transports per
pair, exact-payload offer controls, compatibility metadata and independently retained
acquisition records remain future work. Current assets/schema views do not supply
those independent claims.

The remaining bulk sections specify those agreed extensions, not implemented endpoint
guarantees. Integrate them deliberately with the existing connection/control and
retirement contract; ordinary opaque exchange does not depend on their completion.

### Establishment, usages and replacement

The planned schema-aware extension lets Executive/Rendering request anonymous
Host-owned SPSC backing interpreted through a shared schema at endpoints. Build on
the existing latest-state buffers and Host lifecycle while adding explicit schema
backing, instance-use and acquisition protection. Allocation ownership, slot access/
reuse, schema lifetime and module execution remain separate concerns; neither current
endpoint views nor asset acquisitions establish the proposed independent grant protocol.

An immutable declaration fixes context-qualified connection, producer/recipient
roles, usage, schema/root/layout, capacity and alignment. Either role may arrive
first; match by declared context rather than schema names. Host pins a complete
immutable schema; both exact module instances validate interpretation/representation
and acknowledge it before activation after backing preparation. Host validates
generic schema/storage, not domain meaning. Shared semantics/name or a copied schema
view do not establish equal bytes or lifetime. Second claimants cannot replace roles.
Schema/capacity/usage changes require a new incarnation.

| Instance usage | Publication rule |
| --- | --- |
| Latest snapshot | Publish/acquire independently with no external per-publication handshake; unacquired staged state may be replaced |
| Handshake-driven | Explicit POD coordination preserves particular offered publications; one unacknowledged offer per instance |

These profiles share establishment, schema acknowledgement, backing and acquisition
protection machinery. Handshake usage adds offer/correlation tracking where required;
it does not require a second complete transport facility. Concrete controls,
metadata placement and synchronisation follow the draft bulk-control contract.

Both require establishment acknowledgement and protect acquired data from overwrite.
Matched, established and data-available are distinct: unwritten storage is never a
publication; writes remain private until complete publication. Pending counterpart,
rejected schema and cancelled establishment have explicit request outcomes.
Activation is the establishment commitment boundary. Cancellation first prevents
activation and requires relinquishment/drain of exposed descriptors; a waiting peer
gets dependency failure.
After activation, withdrawal closes the transport rather than rewriting success.

Replacement closes old publication/acquisition, drains departing accesses/signals,
preserves acquired backing/schema until accepted users finish, and establishes a
fresh incarnation/handshake even with equal names/schema. Survivors may retain valid
old data; replacements inherit no endpoint authority. Device and other dependencies
must finish or transfer protection explicitly before module-dependent ownership ends.

### POD compatibility and exact-payload offers

Existing non-discarding POD pipes normally drain ordered per-direction operation/
query history and simple updates. Posting means queued, not processed. Bulk supplies
larger composite data, not command acknowledgement or a combined channel order.
Preserve the [direct POD lifecycle](direct_pod_connections.md): graceful conversations
finish while active; retirement does not require executing unread messages. Finish
required history boundaries explicitly before closing.

Each publication identifies transport incarnation/serial, relevant control-state
generation and required processed POD prefix. The draft stores these in Host-owned
per-slot publication metadata, outside anonymous payload bytes.
Endpoints check compatibility; a prefix alone is insufficient where later commands
delete/reinterpret referenced objects. Generations account for invalidating changes
without invalidation by unrelated queries. Snapshot adoption can wait for future
prerequisites while POD processing continues; incompatible old generations reject.

For handshake use, publish a complete identified payload before its ordered POD
offer/request. Preserve it until acquisition acknowledgement or confirmed withdrawal;
at most one offer remains unacknowledged. Subsequent private preparation is permitted,
but replacing the offered unacquired payload is not. Keep pending transfers explicit
while draining messages/lifecycle controls and preserving operation order.

| Milestone | Meaning |
| --- | --- |
| Acquired | Recipient validated/protected the exact publication; producer can clear the offer and publish another while the held slot remains protected |
| Application complete | Operation-defined terminal outcome, correlated to its request; no automatic source release or universal CPU/GPU-completion meaning |
| Released | All users relinquished the source acquisition; independently protected data may continue. Custody transfer alone is not release. |

Reports identify transport/publication; application completion also identifies its
request, which has one terminal application outcome. Acquisition acknowledgement
requires actual protection and compatibility,
not receipt/inspection/intent. It is not another application terminal outcome.
Acquisition precedes use/release; application completion and release have no fixed
relative order. Copying may release before completion, direct-reference installation
may complete before release. Messages may combine explicit established facts, never
infer one milestone from another. The initial consumer must finish its held payload's
uses before an acquisition that relinquishes it.

### Cancellation, release and acquisition records

Application outcome and acquisition ownership are independent. One recipient decision
authority serialises grant versus withdrawal: confirm no acquisition can occur, or
record protected acquisition and clean up through release. Producer cancellation
intent is not proof of non-acquisition/reusability. Durable closing/cancellation
prevents late acquisition when controls overtake offers.

Cancellation before application commitment may settle that request without freeing
its acquisition. Stop new local use and drain existing users. Committed application
outcomes remain unchanged; cancellation proves neither drain nor rollback. Release
after actual relinquishment applies the decrement once against the exact acquisition.
Late acquisition acknowledgement after release is historical, never revived protection.
Endpoint closure allows valid predecessor cleanup, not new publication/acquisition
or automatic successor authority. Stronger effect-prevention/rollback belongs to the
operation contract. Withdrawal needs confirmation or endpoint quiescence.

Use bounded Host-owned acquisition records with non-reused service-incarnation/
serial identities, separate from requests/publications. Link exact transport/publication,
recipient/module instance, backing slot/range/schema, state, optional handshake
correlation and cleanup/reporting obligations. Snapshot use requires no application
request/external handshake. Establish record/protection before exposing a view;
failure exposes no grant. Copied tokens/views acquire nothing.

Provision capacity with the transport. Account dependent users beneath the initial
single consumer-held acquisition. Historical independent retention requires a
separate extension.

Release may permit slot reuse before terminal bookkeeping is reclaimed. Retain
compact acknowledgement/report/access state until settled, independently of
application-result bookkeeping. Reclaimed tokens expire without affecting reused
records. Explicit cleanup-custody handoff preserves acquisition identity/protection
and obligations; it does not grant replacement endpoints old authority. Diagnostic
history is explicitly retained summaries, not permanent per-publication records.

### Endpoint operation admission and closure

Host establishes protected control state for each transport incarnation and grants
authority to exact endpoint/module instances. Routine publication, acquisition and
release use that pre-established Host-owned state without requiring a Host-message
round trip per operation. The draft specifies control operations and short mutex protection.

Acquisition commitment and transport closure admission are serialised. If acquisition
commits first, its recorded payload/schema protection survives closure and remains
accounted for through release. If closure wins, acquisition fails without exposing
a usable view. Reserving bookkeeping alone is not commitment; an interrupted
reservation must settle without becoming an untracked acquisition.

Closure stops new publication/acquisition while valid release and cleanup remain
available for existing grants. Exact incarnation/authority checks continue to apply;
cleanup grants no replacement endpoint permission to publish or acquire. This
boundary is independent of application completion and payload release. Concrete
control APIs and locking are specified in the completion contract.

### In-flight operations and interrupted reservations

Register counted operation protection before accessing mutable endpoint control
state or reserving resources. Coordinate registration with closure so admitted
operations remain visible to its drain. Admission before closure does not guarantee
acquisition: commitment must still win against the closure boundary.

Every reservation has an accountable owner and settles once, either becoming a
committed acquisition or rolling back without exposing a usable view. The operation
normally releases its interrupted reservation. A cleanup custodian can take over
through explicit handoff or proven quiescence; cancellation, timeout or apparent
thread inactivity does not prove that the operation can no longer access it.

Retain operation accounting through final access, including publication, signalling
and callbacks after a result becomes available. Necessary continuations register
before becoming runnable and remain covered by the accepted operation. Releasing
or transferring responsibility leaves no unaccounted access/reservation gap.

Endpoint closure drains its in-flight operations, provisional reservations and
reporting accesses. Committed acquisitions may survive only through independently
protected backing/schema and explicit cleanup custody. Endpoint quiescence is
distinct from final backing/control-record reclamation, which still waits for
remaining claims and accesses to settle.

Keep accounting bounded and provision it before admitting operations, at operation/
composite-unit granularity. Counts retain enough supporting identity, ownership and
blocking information for diagnostics; they do not replace reservation ownership or
proof of final access. Safe closure must not first require new bookkeeping allocation.

### Capacity pressure and backpressure

Pre-provision bounded capacity before admission. Distinguish temporary pressure from
requirements incompatible with the declared transport configuration; report the
latter explicitly rather than present them as a condition that retry alone resolves.

| Boundary or resource | Policy |
| --- | --- |
| Before acceptance | Insufficient capacity gives an explicit temporary refusal, with no partial grant. The caller retains responsibility for retrying. |
| Accepted operation | Bookkeeping already suffices to remain pending and eventually settle. Pressure cannot discard its result or create an unbounded waiting queue. |
| Release, rollback and closure | Progress must not depend on obtaining fresh capacity reserved for ordinary work. |
| Protected payload | Pressure never permits overwriting acquired data. Snapshot replacement applies only to publications replaceable under the declared latest-state semantics. |

The completion contract specifies capacities, admission/retry controls and synchronisation.
Temporary refusal is distinct from an accepted pending operation and from an
incompatible request; these are behavioural distinctions, not public enum spellings.

## Reflected schema organisation and vocabulary

Each successful entry-point reflection has a validated type catalogue with a
root interface referring to its resources, inputs and outputs. Authoring fragments
may be incomplete during construction. Empty interfaces are valid. Store entry-point
execution properties on that root. Private declarations remain scoped to the
component even after library aggregation. Pipeline linking is a separate step.

### Version 1 vocabulary rules

Use `rendering.*` for renderer interpretation, `rendering.vulkan.*` and
`rendering.spirv.*` for backend facts, and `rendering.hlsl.*` for HLSL metadata.
Foundation preserves these strings without interpreting them.

- Keys and values are case-sensitive.
- Unsigned numbers use decimal without leading zeros, except zero itself.
- Booleans use `true` and `false`.
- Tokens use lowercase snake case, except exact SPIR-V enumeration names and
  explicitly listed tokens such as `1d`.
- Source names and HLSL semantics retain their exact original spelling.
- Decorations apply only to their owner; there is no inheritance, propagation
  or override through references or groups.
- Missing information is unavailable or inapplicable unless a key explicitly
  defines a default. Missing values must not silently become zero or false.
- Unsupported or unknown correctness-relevant metadata produces a reflection
  diagnostic rather than silently disappearing.

The tables below define exact version 1 spellings and attachment locations.
Schema references, member counts and physical layout fields remain authoritative;
decorations do not duplicate them. Sequence extent and stride below describe
non-storage associations which have no corresponding physical schema fields.

### Component and declaration roles

| Owner | Key | Values |
| --- | --- | --- |
| Component root | `rendering.schema.version` | `1` |
| Component root | `rendering.shader.stage` | `vertex`, `tessellation_control`, `tessellation_evaluation`, `geometry`, `fragment`, `compute` |
| Component root | `rendering.shader.entry_point` | Exact compiled entry-point name |
| Component root | `rendering.shader.target` | `vulkan_1_1`, `vulkan_1_2`, `vulkan_1_3` |
| Interface declaration | `rendering.interface.kind` | `component`, `resources`, `inputs`, `outputs`, `value_group`, `buffer_layout` |
| Value-group interface | `rendering.interface.domain` | `vertex`, `patch`, `fragment`, `target`, `system` |
| Shape declaration | `rendering.shape.kind` | `vector`, `matrix`, `array`, `structure` |
| Appropriate declaration/member | `rendering.source.name` | Original reflected name, when available |
| Interface occurrence | `rendering.hlsl.semantic` | Exact available HLSL semantic |

Every component root contains these three members in this order, each with count
one. Their target interfaces exist even when empty:

```text
resources -> resources interface
inputs    -> inputs interface
outputs   -> outputs interface
```

Library and component identity fields belong in the catalogue; they need not
be repeated on every declaration. Shape interfaces have `rendering.shape.kind`;
they are distinct from interfaces classified by `rendering.interface.kind`.

The checked target supplies type identity; do not duplicate it in a decoration
string. Backend-specific coordinates stay qualified. Do not reconstruct DirectX
register information from Vulkan coordinates. Shader declarations remain
unchanged: reflection neither renumbers locations nor rewrites bindings.

## Resources, structures and buffer layouts

Physical buffer structures preserve member order, types, explicit offsets,
nested structures, array/matrix strides and the complete representable extent.
Import the compiled layout rather than recomputing it from HLSL names. One source
structure may require distinct physical definitions in different layout contexts.

An association describes the resource; an interface member describes a particular
resource occurrence and carries its binding. Uniform/fixed blocks target their
physical layout. Images and samplers may be explicitly targetless. Image sampled
scalar information is not a claim about CPU texel storage or a concrete image
format. Read-only and writable resources may share a physical content type.

| Owner | Key | Meaning / values |
| --- | --- | --- |
| Resource association | `rendering.resource.kind` | `uniform_buffer`, `storage_buffer`, `sampled_image`, `storage_image`, `sampler`, `uniform_texel_buffer`, `storage_texel_buffer` |
| Buffer resource association | `rendering.buffer.interpretation` | `typed`, `structured`, `raw` |
| Resource occurrence | `rendering.vulkan.set` | Descriptor set |
| Resource occurrence | `rendering.vulkan.binding` | Binding |
| Resource occurrence | `rendering.descriptor_array.rank` | Number of fixed descriptor-array dimensions |
| Resource occurrence | `rendering.descriptor_array.extent.N` | Positive dimension extent; `N` is a zero-based decimal index |
| Buffer-region occurrence | `rendering.buffer.offset` | Region byte offset |
| Runtime-sequence association | `rendering.sequence.kind` | `runtime` |
| Runtime-sequence association | `rendering.sequence.stride` | Element byte stride |

Other resource categories require an explicit support decision; do not map them
to an approximately similar category. Preserve access/memory qualifiers at their
actual attachment with `rendering.spirv.decoration.NonWritable`, `NonReadable`,
`Coherent`, `Volatile` and `Restrict` (each suffix uses the same full prefix).
An emitted flag has value `true`; absence means it was not emitted. Declared
restrictions are distinct from observed operations or runtime resource use.

Image associations use:

| Key | Values |
| --- | --- |
| `rendering.image.dimension` | `1d`, `2d`, `3d`, `cube`, `buffer` |
| `rendering.image.arrayed` | `true`, `false` |
| `rendering.image.multisampled` | `true`, `false` |
| `rendering.image.sampled_scalar` | `f32`, `i32`, `u32` |
| `rendering.spirv.image_format` | Exact SPIR-V `ImageFormat` name, including `Unknown` |
| `rendering.spirv.image_depth` | `non_depth`, `depth`, `unknown` |

An explicitly unknown image property is retained, not replaced with a guessed
format or depth interpretation. These keys do not describe CPU texel storage.

A structured buffer uses these conceptual relationships:

```text
Resources member (binding)
  -> storage-buffer resource association
    -> buffer-layout interface
      -> element-region occurrence (byte offset)
        -> runtime-sequence association (element stride)
          -> physical element structure
```

A buffer-layout interface can also describe a fixed prefix. Runtime extent is
explicitly unspecified until storage is supplied. The region declares one
sequence; interface-member count does not stand for its runtime element count.
Raw byte-address resources retain an explicitly raw interpretation.

Distinguish element schema size, sequence byte stride, runtime element count and
descriptor count. Existing bulk operations use physical type size as record
spacing; a sequence with another stride is not automatically a compatible bulk
collection. No variable-sized physical schema or new generic strided-data
operation is implied by reflection.

Fixed descriptor arrays use occurrence count for total descriptors. Emit rank
and contiguous indexed extents only for actual arrays, including arrays of one.
Check the dimension product against occurrence count with overflow detection.
Count does not increment binding coordinates. Runtime descriptor arrays remain
deferred.

Schema arrays own full stride slots and rounded tails. Validate representability
against those rules; do not truncate a slot or let another member occupy its
owned padding. SPIRV-Reflect's top-level storage-buffer size can be zero even
when its element type has a layout; inspect the member/element and stride data.
Do not interpret a reflection library's padded-size field as an independently
proven source-language ABI.

## Matrices and stage value shapes

### Canonical non-storage shapes

Composite stage values use non-storage interfaces, with primitive scalar leaves.
They describe values without inventing CPU offsets, alignment, vertex packing or
an inter-stage memory ABI. Physical buffer structures remain physical schema
types. Vertex formats, strides, offsets and conversions belong to future
component/PSO input descriptions and validation.

| Value | Canonical representation |
| --- | --- |
| Scalar | Reference to the corresponding schema primitive |
| Vector | Interface with `rendering.shape.kind=vector`; one member `element`, count equal to vector width |
| Matrix | Interface with `rendering.shape.kind=matrix`; one member `column`, count equal to column count, targeting a vector shape |
| Fixed array | Interface with `rendering.shape.kind=array`; one member `element`, count equal to array extent |
| Structure | Interface with `rendering.shape.kind=structure`; ordered members matching the reflected aggregate |

Multidimensional arrays nest one declaration per dimension. Arrays of one retain
their array identity. Primitive references identify scalar meaning in this
context; their physical sizes are not a claim about shader interface storage.
In particular, a logical Boolean is not evidence of one-byte buffer storage.

Use lowercase descriptive names. A matrix name such as `float3x4` describes
three rows and four columns in its recorded interpretation basis, not an array
of four vectors inferred from its spelling:

```text
float3                         [vector]
  element: f32, count=3

float3x4                       [matrix: three rows, four columns; f32]
  column: float3, count=4

ordinary array declaration     [shape.kind=array]
  element: float3, count=4
```

A distinct generic schema matrix category and a `Matrix` naming prefix are not
required. Retain matrix classification through `rendering.shape.kind=matrix` and
checked shape relationships. Matrix and ordinary-array classification remain
distinct even when their scalar counts agree. This distinction is descriptive;
stage linkage must apply backend rules to the actual interfaces rather than
automatically reject different source classifications. Half precision remains
outside the initial feature policy; compact terminology does not enable it.

These names sit within the qualified declaration identity scheme. Semantic shapes
and concrete storage representations must not acquire the same identity merely
because they have the same descriptive spelling. Names do not replace collision-
free encoding, and consumers inspect checked shape and decorations rather than
parse names to establish correctness.

Preserve aggregate structure exposed by reflection. Do not reconstruct an assumed
HLSL hierarchy after compiler flattening. Missing names receive deterministic
ordinal names without fabricated `rendering.source.name`. A later traversal can
flatten leaves for matching without storing a second authoritative representation.

### Compact physical-layout terminology

The notation describes faithfully reflected storage for CPU data construction
and validation. A single underscore following a vector means padded to four
scalar slots, not one unused element and not an array separator:

| Name | Meaning |
| --- | --- |
| `float2` | Two meaningful 32-bit float components, with no padding implied by the name |
| `float3` | Three meaningful 32-bit float components, with no padding implied by the name |
| `float2_` | Two meaningful floats in a four-float extent; two slots unused |
| `float3_` | Three meaningful floats in a four-float extent; one slot unused |
| `float3_x4` | Four padded three-component storage elements, each occupying four float slots |
| `float3x4` | Three-row, four-column matrix in its recorded basis; storage packing is separately described |

Use `float2_`, not repeated underscores to count individual unused slots. The
underscore describes storage extent, not alignment. A four-slot extent does not
by itself establish alignment, and alignment does not establish size or stride.
The schema's explicit layout and metadata remain authoritative. Other layouts
need unambiguous generated identities without forcing every property into this
compact notation. These are descriptive spellings, not a separate authoring-reference
grammar; use qualified declaration identity for lookup.

The padded storage notation does not by itself classify the repeated values as
a matrix or ordinary array. Preserve that interpretation separately. It also
does not require a dedicated padded schema primitive: existing physical
structures/arrays and explicit strides can express the representation. Padding
is not a fourth meaningful shader component.

For example, a uniform-buffer `float3` at offset 0 followed by a meaningful
`float` at offset 12 is two fields sharing a 16-byte slot. The vector is not
`float3_`, because the fourth slot belongs to the following field. Likewise two
`float2` fields can occupy offsets 0 and 8. Conventional HLSL constant-buffer
packing and the selected vector-relaxed std140 layout permit these examples;
they do not imply identical layouts for every structure, array or matrix. Reflect
the compiled offsets and strides rather than treating every uniform member as
owning a complete `float4`. Uniform-buffer array spacing and structured-buffer
spacing must be checked in their own layout contexts.

### Matrix interpretation, storage and mathematical conventions

Keep three distinct kinds of information:

| Information | Meaning |
| --- | --- |
| Logical interpretation | Rows/columns in the producing and consuming contexts, including known compiler transformations between them |
| Storage mapping | How those logical elements map to bytes, including major-vector order, strides and padding |
| Mathematical usage | Row-vector/column-vector multiplication and application conventions; these are not inferred from storage order |

Row/column terminology must retain its context. It is not necessarily a storage
description. Preserve facts available from the artifact and compilation context;
application conventions that cannot be recovered must be supplied explicitly.
Equal dimensions and byte layouts alone do not establish consistent binding.
Producer/consumer interpretations must agree or have an explicit validated
mapping/conversion. Reflection records these facts; it does not silently
transpose data, rewrite shader code or establish a multiplication convention.

Matrix interpretation remains in rendering. The initial importer describes the
compiled SPIR-V matrix with `rendering.matrix.basis=spirv`, on the matrix shape
or physical matrix descriptor. Rows and columns in names such as `float3x4`
refer to that basis, not inferred original HLSL dimensions.

A physical matrix descriptor additionally uses
`rendering.matrix.storage_major=row` or `column`. Nested physical arrays describe
stored major vectors in the stated basis. Derive dimensions from their counts
and that storage mapping; do not add redundant dimension or stride decorations.
Stage-value matrix interfaces have no storage-major decoration because they
describe values rather than storage. The `storage_major` key specifically
describes storage; it is not a general producer/consumer convention tag.

DXC transposes HLSL matrix types when emitting SPIR-V and reverses their
row/column-major storage decorations. Do not label compiled dimensions as HLSL
dimensions or reconstruct source matrices lowered to vectors/arrays without
sufficient evidence. Preserve known source-to-compiled interpretation mappings
with their context instead of conflating source and backend row/column labels.
Storage order does not establish multiplication convention, coordinate handedness
or application meaning. Additional conventions use the draft semantic/profile
records attached through occurrence mappings; the
existing basis/storage keys do not purport to encode all of those meanings.

The initial authoring direction permits explicit application interpretation on
selected declarations or occurrences. Missing application conventions mean
unspecified; they are not filled from a storage-major flag. Known compiled facts
and compiler mappings retain their provenance independently. Initially diagnose
incompatible interpretations rather than automatically transpose or convert
data. A known compiler mapping can explain consistency, but introducing a new
conversion is outside reflection.

## Per-vertex, per-patch and system declarations

Within each input/output interface, emit applicable nonempty groups in this
fixed order. Each member has count one and targets a `value_group` interface:

| Member name | `rendering.interface.domain` |
| --- | --- |
| `vertex_values` | `vertex` |
| `patch_values` | `patch` |
| `fragment_values` | `fragment` |
| `target_values` | `target` |
| `system_values` | `system` |

These are declaration groups, not memory regions. They do not impose one common
extent on all their members. Built-in identity is independent of grouping: a
per-control-point position is a vertex value, an invocation identifier belongs
with system values, and tessellation factors belong with patch values.

Preserve a stage-imposed outer vertex array through a non-storage association
targeting the value description for one vertex. Its decorations identify the
wrapper and its declared extent. The interface occurrence retains locations,
built-ins and qualifiers. Interface-member count remains one for this convention.

| Vertex-array association key | Meaning / value |
| --- | --- |
| `rendering.sequence.kind` | `vertex_array` |
| `rendering.sequence.extent` | Positive declared array extent |
| `rendering.sequence.source_dimension` | Zero-based dimension in the reflected declaration before wrapper extraction |

```text
vertex_values:
  position -> vertex_array(extent=3) -> float4
  weights  -> vertex_array(extent=3) -> array[4] of f32

patch_values:
  factors  -> array[4] of f32
```

Only the first two declarations have a stage-imposed wrapper. Their inner value
shape is preserved in full. A patch array is not indexed by vertex merely because
it is an array. Single-vertex declarations have no invented array-of-one wrapper.
Wrapper identification follows stage and declaration rules, not a blanket rule
that removes the first dimension from every array. Retain which reflected level
was classified as the wrapper so the original shape remains explainable.

| Stage | Input value groups | Output value groups |
| --- | --- | --- |
| Vertex | Vertex attributes; system values | Single-vertex values; system values |
| Tessellation control | Vertex arrays; system values | Vertex arrays; patch values; system values |
| Tessellation evaluation | Vertex arrays; patch values; system values | Single-vertex values; system values |
| Geometry | Vertex arrays; system values | Single emitted-vertex values; system values |
| Fragment | Fragment values; system values | Colour targets; system values |
| Compute | System values | Normally empty; resource writes belong under resources |

This table describes conventional user-value cases. Classify built-ins according
to their individual rules and retain backend patch decorations separately from
the renderer's semantic grouping. No qualifier inheritance is implied by groups.

Keep these quantities separate:

- Declared interface-array extent: vertex-wrapper description.
- Control-shader output vertex count: entry-point execution property.
- Input patch size: future pipeline configuration.
- Geometry input topology: execution property.
- Geometry maximum emitted vertices: emission limit, not an output value array.

Unavailable or stage-dependent information must remain explicit, not be guessed
from another count. Later PSO validation compares value shapes and separately
checks topology/patch constraints; it neither indiscriminately equates outer
extents nor ignores them. Preserve relevant tessellation modes, geometry modes,
compute local dimensions and fragment execution properties on the component root.

### Interface coordinates and qualifiers

Attach coordinates and qualifiers to the reflected occurrence, not its reusable
value shape:

| Key | Meaning |
| --- | --- |
| `rendering.vulkan.location` | Declared location/base location |
| `rendering.vulkan.component` | Declared component |
| `rendering.vulkan.index` | Declared fragment-output index, when present |
| `rendering.spirv.builtin` | Exact SPIR-V built-in name, for example `Position` |
| `rendering.hlsl.semantic` | Available original semantic |

Keep each decoration on the aggregate/member where it actually occurs. Later
traversal can calculate effective leaf locations; do not store both aggregate
coordinates and a second authoritative flattened layout. Backend validation
rules govern legal combinations, including built-ins versus locations.

Preserve interface qualifier flags with these exact keys:

```text
rendering.spirv.decoration.Flat
rendering.spirv.decoration.NoPerspective
rendering.spirv.decoration.Centroid
rendering.spirv.decoration.Sample
rendering.spirv.decoration.Patch
rendering.spirv.decoration.Invariant
rendering.spirv.decoration.RelaxedPrecision
```

An emitted flag has value `true`; absence means it was not emitted. Backend
rules interpret effective defaults. Keep combinations of flags rather than
collapse them into a lossy interpolation token. The importer must supplement
SPIRV-Reflect where its convenience flags omit required qualifiers.

### Execution properties

Execution modes belong on the component root under
`rendering.spirv.execution.<ExactModeName>`. Flag modes use `true`, single-literal
modes use unsigned decimal, and `LocalSize` uses named `x`, `y`, `z` operands:

```text
rendering.spirv.execution.LocalSize.x = 8
rendering.spirv.execution.LocalSize.y = 8
rendering.spirv.execution.LocalSize.z = 1
rendering.spirv.execution.OutputVertices = 4
rendering.spirv.execution.Invocations = 1
rendering.spirv.execution.Triangles = true
rendering.spirv.execution.SpacingEqual = true
rendering.spirv.execution.VertexOrderCw = true
rendering.spirv.execution.EarlyFragmentTests = true
```

These are independent spelling examples, not one valid component's combined
mode list. Use a checked table of permitted modes, operand forms and stage
combinations. Unsupported modes or unresolved symbolic operands produce
diagnostics; do not invent literal values. Stage-wrapper extent, tessellation
output count, geometry emission limit and future pipeline patch size remain
distinct.

### Initial feature boundary

The individual-shader reflector rejects all specialization constants
with an identifiable unsupported-feature diagnostic, distinguishing shape and
execution dependencies where possible. This does not change the existing
compile-only path. Do not silently freeze specialization constants at their
default values.

Compile-time variants are the baseline: explicit named compilation targets select
entry points, defines and compilation settings, producing independently compiled
and reflected artifacts. This does not introduce automatic permutation expansion.

Specialization constants are currently unsupported by shader reflection and
conditioning, and support is not planned. The 27 October decision supersedes the
earlier required follow-up: specialization support is not a prerequisite for
completing the agreed shader work and is not scheduled as part of PSO construction.
A concrete future use case may justify reconsidering metadata, value selection,
validation and effects on reflection through a separate design decision. Record
this scope in the [engine backlog](../backlog/engine_backlog.md).

Reject physical layouts that cannot be represented faithfully, including cases
where schema extent/padding ownership would misdescribe the compiled layout.
Neither unsupported semantics nor unrepresentable storage may produce a
successful but incomplete reflection result.

## Combined reports and publication

Support enclosing compile-only and compile-and-reflect operation modes. Jobs
remain independently reusable. One owning group result retains ordered member
reports, request/component identities, selected operation and group summary.
Each member retains source/entry point, effective options/provenance, compilation
status/diagnostics, available SPIR-V, reflection status/diagnostics and successful
reflection data. Do not replace compilation success with reflection failure.

| Outcome | Retention |
| --- | --- |
| Compile failure | Compilation diagnostics; reflection blocked by upstream failure |
| Reflection failure | Successful compiled artifact and diagnostics from both operations |
| Both succeed | Artifact, complete reflection data and both sets of diagnostics |
| Compile-only | Artifact/diagnostics; reflection explicitly not requested |
| Work not started/interrupted | Identifiable per-member reason |
| Storage/consolidation/bake failure | Separate finalisation failure; preserve earlier outcomes and available reports |

Do not expose a partial failed schema as usable. Distinguish not requested,
blocked by prior failure and infrastructure interruption. Reserve basic report
bookkeeping before dispatch. Diagnostics are bounded UTF-8 with truncation flags;
allocation failure can leave a minimal status without optional diagnostic text.
Ordinary member failures do not cancel siblings. Drain accepted jobs at shutdown.

Intermediate artifacts and schemas stay in rendering. Reflection borrows immutable
SPIR-V while its owner remains stable. Compilation may feed reflection before all
other compilations finish. Protect the source bundle for all accepted borrowers;
retained strings/results must survive release of the source bundle and compiler
objects. Do not concurrently mutate schema binding lists or relocate live payloads
while workers borrow them. Bind/resolve locally rather than transport resolver
indices, intrusive bindings or borrowed library pointers.

Return the complete owning result at the enclosing Rendering operation boundary.
The existing compile-only path returns its group to its Rendering caller; there
is no automatic typed shader-group transfer to Host retention. Host can retain
explicitly exported generic bytes/documents without interpreting SPIR-V or
reflection schemas. The production combined-group envelope and library-publication
lifetime follow the review-draft group/packing contract; they are not supplied by
the individual composed job.

Keep service/publication status, group outcome and per-stage/finalisation outcomes
distinct. Even an all-failed group has a useful report. Publication success does
not imply shader success, successful library assembly or pipeline compatibility.

Extend the renderer-owned group report with separate compilation, reflection and
finalisation outcomes while retaining the existing compile-only operation.
The review draft specifies logical request/report/view records, status meanings
and configurable ceilings for counts, nesting, strings, diagnostics and retained/
staging bytes. Account for source
intermediates, packed payloads and final storage that coexist during assembly;
the generic block's 2 GiB ceiling is not an aggregate memory budget.

### Atomic production-library policy

Any required shader compilation or reflection failure poisons the production
library. Sharing, consolidation, packing or final validation failure likewise
prevents publication of a production asset. There is no partial-production mode
in the initial contract.

Continue independent jobs and checks after ordinary failures, including reflection
of successful compilations. Checks dependent on missing results are marked
blocked. Retain ordered reports for successful, failed and unexecuted members,
available artifacts and diagnostics, and a group/finalisation summary. The
completed operation result contains an optional validated production library;
failure leaves that optional artifact absent without discarding the reports.

Successful intermediate shaders in a failed group are diagnostic/conditioning
results, not a partially usable production library. A future diagnostic export
must identify itself separately. A failed rebuild leaves any previously valid
published library intact. Reporting a failed attempt and replacing a production
asset are separate actions.

## Live assembly and component-library output

Prepare reflection type descriptions and binary bulk data per compilation target,
then consolidate into one component library containing types, optional instances
and bulk data. Distinguish the editable `CSchema` aggregate, which contains only
literals, from live runtime roles with stripped descriptions and complete binary
backing. Final runtime output uses the common block after aggregation. Independent
per-member baked snapshots are optional, not the required normal intermediate
representation.

Retain exact SPIR-V as named `u32` bulk collections with binary payload storage;
do not require a document node per instruction word or duplicate the artifact in
the reflection report. Its storage schema/access must remain independent of
reflection success. Artifact finalisation may copy compiler output; no zero-copy
construction guarantee is assumed. Preserve debug/reflection information at this
stage. Later distribution stripping must keep artifact requirements consistent.

The library catalogue links component identity to code and reflection entries;
ordinary instance/bulk data can hold it. Semantic contract definitions occupy their
own structure, with explicit links from shader occurrences through mappings;
packaging them alongside a catalogue does not make them catalogue-owned copies.
Shared-type consolidation does not merge stage interfaces into a pipeline interface.
The future content description can select these components for PSO construction,
which remains outside this phase.

### Implemented combined persistence

The [version-1 common block](../schema/common-binary-block.md) is implemented as
`CBakedDataBlock` and persisted as `.bsd`. It uses the existing MBD2 encoding and
header with unchanged reserved-zero fields. Root metadata identifies
`fileType: "morphic-schema-data"`, `version: 1`, `stripped: true` and required
`retainedText` (empty for reference-free values), with optional
`types`, `instances` and `data` sections. Each present data section requires its
exact `instancePayloadSize` or `bulkPayloadSize` and complete supplied backing,
including explicitly supplied empty storage for a zero-length domain. Missing
backing is an error, not a request to materialise data later.

The file contains the baked description, instance binary and bulk binary in that
order. Its base and every nonempty domain are 128-byte aligned; absent/empty
domains consume no payload bytes or alignment padding. Inter-domain padding is
zero, and no final alignment tail is saved. Locators remain relative to their
own domain. Checked arithmetic, exact extent and an inclusive 2 GiB complete-file
ceiling apply. Payload-internal padding is distinct from container padding and
can be preserved byte-for-byte. Prior schema-data forms and superseded APIs have
no compatibility layer.

No separate rendering-specific binary header is required. The library identity
and library-format version live in the schema-defined catalogue; they are
distinct from the generic block version. Generic storage assembly does not
interpret rendering metadata. Types, instance and bulk typed interfaces remain
separate, borrowing stable backing. Envelope validation checks storage; typed
attachment checks supplied values/layout, followed by renderer validation. A
valid envelope alone does not establish a usable shader library.

### Library assembly routes and remaining packing work

The generic APIs provide two distinct paths:

| Route | Contract and consequence |
| --- | --- |
| `CSchema` composition/merge followed by `bake` | Validates contained types and literals, then constructs complete runtime backing directly in the final block |
| Prepared runtime description and payloads followed by `CBakedDataBlock::assemble` | Copies supplied bytes into independent storage transactionally; performs no typed construction or automatic component relocation |

Importing runtime roles into authoring and then baking remains available for
authoring/tooling, rather than a second required production-library workflow. It
requires literal reconstruction and its fidelity/temporary-memory costs. Types must be
imported separately. It is not the required path for large SPIR-V arrays or for
preserving arbitrary payload bytes. Generic authoring `save` is also distinct
from runtime `bake`: saving an admitted authoring tree does not prove it valid or
create runtime payloads.

The required production-library route is binary-preserving. Rendering prepares
combined runtime descriptions, correctly aligned domain payloads and relocated
locators, then assembles once and performs typed/rendering validation before publication.
Use deterministic payload placement and preserve request order in the catalogue.
Relocate each entry within its own payload domain and copy SPIR-V bytes without
literal reconstruction. Retain intermediates until finalisation succeeds or
fails so reports and available artifacts survive an assembly failure.
Neither authoring merge nor raw assembly concatenates component payloads or
rewrites their locators automatically. The detailed multi-source packing plan,
entry identity mapping and bounded temporary ownership are specified in the draft. The
generic persistence mechanism itself no longer needs a new schema extension.

Both construction and assembly require empty output owners and publish only on
success. Keep source owners and borrowed views valid through the operation.
The library's atomic production policy additionally requires all component,
consolidation and final validation checks to pass before replacing an asset.

## Review-draft completion contracts

The following resolves the former remaining-work inventory for user review. These
are design contracts, not implemented APIs or a task to implement them. They retain
the agreed policies above and propose one bounded initial path. Focused stages
refine or explicitly revise these proposals through the staged review process;
their precision and complexity are not assumed adequate merely because they exist.
Unknown correctness-relevant input fails; optional diagnostic additions cannot
change acceptance. No new worker system, general numerical prover, expression
interpreter or universal resource framework is required.

### Completion: authoring and identity

Use the existing engine JSON document reader for version-1 authoring. No additional
shader-description language/parser is introduced. Roots have `kind`, `version: 1`
and `name`; recognised kinds are `shader_library`, `semantic_module` and
`pso_library`. Unknown fields, duplicate object keys, wrong kinds and unsupported
versions reject. Arrays preserve declaration order; object property order does not
define meaning. Binary runtime output retains the existing version-1 .bsd envelope;
the authoring/library version is a separate field inside its schema-defined data.

Public library, context, concept, variant, target, use, role and logical-member names
use `[A-Za-z_][A-Za-z0-9_.-]*`, are case-sensitive and are never trimmed or folded.
Semantic namespaces are dot-separated identifier segments. Names have a 256-byte
UTF-8 ceiling; paths retain the existing 1,024-byte source-path ceiling. Reflected
source names are preserved verbatim as provenance and may require ordinal selectors;
they need not satisfy this public-name grammar. Empty names and embedded NULs reject.

| Record | Version-1 fields and rules |
| --- | --- |
| Shader library | `kind`, `version`, `name`, `sources`; optional `options`, `defines`, `include_roots`, `libraries`, `semantics`, `shared_types`, `annotation_mode`. Omitted collections are empty. |
| Source | `path`, `concepts`; paths resolve within the retained source bundle using existing checked path/include rules. |
| Concept | `name`, `stages`; repeated conceptual groups across files contribute targets, not separate identity scopes. |
| Stage | `stage`, `variants`; canonical tokens are `vertex`, `tessellation_control`, `tessellation_evaluation`, `geometry`, `fragment`, `compute`. |
| Variant | `name`, `targets`; a variant is mandatory, including an explicitly authored `default` where desired. No omitted/default-variant fallback. |
| Target | `name`, `entry_point`, `options`, `defines`; the last two may be empty. Library options/common defines plus target overrides resolve through the existing compile contract. No additional source/concept/stage inheritance. |
| Library binding | Local `name` and `asset` reference; the declared name inside the referenced library is checked as metadata, never used to discover an asset. |
| Asset reference | `root` (an explicit enclosing logical-root binding), relative `path`, optional `sha256` constraint. Conditioned dependency records always retain the resolved content digest. No persisted `CAssetId`. |
| Component reference | `library`, `concept`, `stage`, `variant`, `target`; `library` selects an explicit binding, including the reserved `self` binding. |

Omitted option fields use frozen version-1 defaults matching the implemented
compiler defaults, not executable Debug/Release settings. Effective values are
always recorded. Common defines and target overrides retain existing precedence;
duplicates within either list and reserved `MV_SHADER_` overrides reject. All full
component identities are checked before dispatch, including duplicates across files.
Production shader/PSO libraries require at least one target/pipeline. A semantic
module may supply definitions only. References never use slash-concatenated names.

Private schema names use an internal reserved prefix followed by length-delimited
hexadecimal UTF-8 encodings of the component tuple, declaration kind and an original
declaration name or deterministic traversal ordinal. A separate shared-type prefix
uses the explicit shared identity and dependent path. Encoding is reversible and
collision-checked; generated names are not authoring references. Source names and
origins remain separate diagnostic data.

Canonical build identity includes resolved authoring, effective options/defines,
compiler/adapter pins and every consumed source/include's content identity. Use
SHA-256 for content/dependency validation, not the existing diagnostic FNV values.
Array order is retained; semantic maps sort keys; typed integers/real values remain
distinct. Digests accelerate lookup but never override structured validation or
establish semantic equivalence. Mutable source paths alone are not build identity.
The initial safe input snapshot is the complete acquired bundle's SHA-256 plus the
resolved request, so unused-file changes may conservatively invalidate a build.
Per-file dependency digests can improve diagnostics/reuse only while retaining exact
snapshot association. No precision-proof or cache-authentication system is introduced.

### Completion: semantic records and profiles

A semantic module contains `namespace`, `imports`, `role_types`, `terms`, `contracts`,
`profiles`, `equivalences` and `uses`. Every definition has a local `name` and positive
`revision`; imports bind an explicit module/asset to a local context name. References are structured
`{context, name, revision}` objects. Definitions in one module may reference each
other after collection; import and definition dependency cycles reject initially.
No global discovery, namespace search or latest-version rule is added.

| Definition | Complete logical fields |
| --- | --- |
| Term | Identity, permitted attachments, value domain, required dependent terms/properties, exclusions, propagation rule, equality rule and diagnostic description. Initial propagation is explicit owner-only; a different rule needs separately supported semantics. |
| Contract | Identity, `shape`, typed `roles`, `relations`, `terms`, `constraints`, exact dependencies and optional descriptive metadata. Shape selects a scalar, vector, matrix, record or homogeneous sequence; named members retain declaration order without making it semantic unless explicitly positional. No inferred missing members. |
| Role | Local name, exact role-type definition and whether a use must supply a concrete binding or may retain an explicit selection requirement. Relationships name declared roles, not application lookups inferred from spelling. |
| Role type | Identity, typed binding domain and recognised relationships. Supplied categories include subject/entity, scene, coordinate frame, reference origin and material item; application types can define further domains without adding implicit lookups. |
| Constraint | Known predicate identity plus typed literal/member/role arguments. No arbitrary expressions or callbacks embedded in documents. |
| Transform/operation profile | Exact identity, action definition, representation/encoding conventions, role requirements, permitted construction/interpolation/conversion operations, operating-condition fields and required data checks. Continuity conditions include any segment-boundary requirements. |
| Use | Context-qualified name, exact contract, role bindings, selected profile references and explicit outstanding selections. A contract-fixed binding/profile cannot be overridden by the use. |

Value domains are typed booleans, integers, reals, text, enums, exact references,
role/member references and bounded records/sequences of those values. Domains state
all required fields; unknown correctness fields reject. A logical structure may
reuse Foundation type definitions but must not acquire GPU descriptor coordinates,
physical offsets, provider IDs or schedules as semantic identity.

Reserve `morphic.semantic`, `morphic.unit`, `morphic.colour` and `morphic.profile`
for supplied definitions; application modules use their own namespaces. Existing
`rendering.*` reflection decorations keep their separate version-1 meanings.
Initial base families provide role, frame/origin, unit, colour interpretation,
selection scope and profile references. Role values include position, displacement,
direction, normal, rotation/transform, colour, coverage and identifier/reference;
the term definition checks applicability instead of recognising a scalar shape.
Base units include metre, radian and explicitly dimensionless values. Further units
need exact definitions and an explicit supported conversion when conversion is used.

The base records are `morphic.semantic/{role,unit,frame,origin,selection_scope,profile}`
and `morphic.colour/interpretation`, each revision 1. Their structured identities,
not this slash notation, are used in documents. Role is an enum of the base roles;
unit/profile are exact references; frame/origin/scope are typed role references.
Colour interpretation is a record of exact primaries/whitepoint references, transfer
(`linear` or the defined sRGB transfer) and alpha (`none`, `straight`, `premultiplied`).
Alpha other than `none` requires represented alpha. Spatial roles require frames;
position/displacement quantities require units; origins and action conventions are
required by the selected representation. Transform roles require action/profile
definitions; colour requires interpretation. Units/profile/frame/origin have no
implicit defaults. Application-specific role terms use separate complete definitions,
not edits to reserved enum meanings.
The supplied colour definitions include `morphic.colour/srgb_primaries@1` and
`morphic.colour/d65@1`, interpreted by the referenced colour standard; they do not
install an executable conversion. Imports are arrays of explicit `{name,asset}`
bindings. A library semantic context has `name` and exactly one `asset` or `inline`
semantic module; the containing module's `self` context is reserved and cannot be rebound.
Supplied role types use distinct `entity_type`, `scene_type`, `frame_type`,
`origin_type` and `material_item_type` identities in `morphic.semantic`, each revision 1.

Shape records use `kind` plus exactly its fields: scalar `type`; vector `scalar`
and `count`; matrix `scalar`, `rows`, `columns`; record `members` and optional
`positional` (default false); sequence
`element` and `count`. A member has `name`, exactly one `shape` or exact `contract`
reference, and optional `terms`/`constraints`/`role_bindings`. Referenced child
contracts explicitly bind required child roles to parent roles or declared bindings;
member names do not create an implicit shared subject. Count is a fixed unsigned count or
an explicit bounded `{minimum,maximum}` cardinality. Scalar types resolve through
the contained Foundation definitions; a backend still rejects unsupported widths.
No count, layout padding or scalar type implies an application role.

All definition kinds share namespace/name/revision identity and are type-checked
at reference sites; a role type and a contract cannot occupy the same identity as
different definitions. A role binding is explicitly a typed literal, a named
selection from the caller's binding context, or an alias to a named role in another
explicit use. Use/role-alias dependency cycles reject. Runtime subject keys carry
their owning context/incarnation, not a guessed source name or persisted pointer.
Required selection keys may be outstanding in library validation but must resolve
before a binding plan is usable. Registered selection/relationship handlers supply
application facts; document declarations do not execute arbitrary application code.

A term application has `term` and typed `value`. A relation/constraint has
`predicate` and ordered `arguments`, each explicitly a literal, role reference or
member path. Term definitions may use finite enum cases to activate required
properties/exclusions; further conditions require known predicates. Arrays of
constraints are conjunctions, never overriding rule priority. Descriptions/origins
are metadata and do not change a definition's equality.

Known initial checks are finite values, closed range, nonzero values, unit-length/
unit-rotation and non-degenerate/orthonormal basis conditions under an explicitly
selected supported profile; typed role equality, membership, expressed-in and
relative-to relationships; and profile operating-condition admission. Declaration
validation checks applicability and references. Actual-value checks run when data
exists. Profile-defined tolerance/check rules are qualified implementation policy,
not inputs to a general error estimator.

Profile declarations do not install executable behaviour. Rendering maintains a
finite registry of qualified profile implementations and their supported consumers,
representations and conversions. Registration pins necessary module/code lifetime.
A declaration may describe a complete data convention through understood grammar;
executing a construction/interpolation/conversion additionally requires registered
support. Exact-format data supply needs no generated transform operation. Unknown
execution profiles are unsupported, not invitations to interpret mathematics.

The specific quaternion, dual-quaternion, rotor/vector, basis and interpolation
repertoire is intentionally not selected here. Initial integration accepts only
explicitly supplied qualified profiles and ordinary exact-representation supply.
Angular/logarithmic storage forms require an explicit qualified conversion before a
runtime use needing another form. This boundary completes integration while leaving
the requested detailed catalogue deferred, without prescribing a matrix or pose.

The material exercise resolves one complete record with `base_colour: f32[3]` and
`opacity: f32`. Its explicit colour convention is linear-light sRGB primaries with
D65 whitepoint; both properties are dimensionless and finite in `[0,1]`. Colour
has no implicit alpha member. This is an example definition, not the only material
model. A selectable provider advertises its selector type; neither this contract
nor shader mappings impose one universal material/variation key. Blend/mask state
remains explicit PSO configuration. The [colour-space definition](https://www.w3.org/TR/css-color-4/#predefined-sRGB-linear)
supplies the convention, not a new engine colour-conversion implementation.

### Completion: mappings, comparison and coverage

Semantic modules are imported into an explicit named context in a library's
`semantics` object. It contains `contexts`, `uses` and ordered `mappings` arrays.
Each mapping has `component`, `shader`, `semantic` and provenance. The shader
selector is a discriminated record:

| Selector | Required identity and typed path |
| --- | --- |
| Resource | `backend: vulkan`, `set`, `binding`, `content` (`resource`, `fixed_contents`, `element_type`) and optional `path` |
| Stage value | Direction (`input`/`output`), reflected value group, and either `builtin` or `location` with explicit/default-zero `component` and `index`; optional path. Explicit stage-wrapper traversal is distinct from ordinary array-element traversal. |
| Path step | Exactly one of `member` (unique exact name), `ordinal` (optionally checked against an expected name), `element_type` or `stage_element`; no runtime index or wildcard expansion. |
| Semantic endpoint | `{context, use}` plus optional logical member path; an optional expected contract reference must match the use. |

Resource selectors resolve resources before traversing contents. `element_type`
does not grant coverage of the enclosing resource-selection relationship unless
the mapping/use explicitly establishes it. A homogeneous sequence's element rule
covers its repeated instances; heterogeneous positional meanings use an explicitly
declared fixed record/positional contract. Runtime cardinality/item selection is
validated against the provider and binding plan, not enumerated into mappings.

Resolve each mapping into occurrence-to-logical-value correspondences. A matching
record shape/member identity permits deterministic aggregate correspondence; any
different names or ordering require explicit correspondence entries using the same
path grammar. No search or implicit field rename. Scalar/vector/matrix values
remain atomic unless an explicit supported subvalue mapping is declared. Padding
and stage wrappers retain their separate physical/structural roles.

For strict comparison, resolve references and declared defaults, then compare the
complete typed structures and correctness properties. Sort maps and named role
sets; retain positional order, member identities, sequence counts and constraints.
Strip only explicitly descriptive metadata. A mismatch reports both origins and
the first differing path; independent checks may report additional conflicts.
Duplicate agreeing claims consolidate but retain their provenance. Different
semantic identities require an explicit equivalence declaration checked by these
same rules; no hash-only or layout-only aliasing.
An `equivalences` entry contains exact `left` and `right` references. Compare their
complete definitions modulo only the root identity, preserving dependencies,
member identities and all correctness properties. Different revisions of the same
namespace/name are not an adaptation path. Valid equivalence classes may share one
immutable definition body with all identities/origins retained; lexical identity
ordering selects a deterministic internal representative. This does not select a
provider, convert data or make distinct executable profiles interchangeable.
For non-positional records compare members by name; an explicitly positional record
also compares declaration order. No implicit member-renaming equivalence is introduced.

Coverage records identify `mapped`, `backend_defined`, `proven_inactive` or `error`
for each binding-relevant occurrence. Provider fulfilment is a separate field,
initially `awaiting_provider` where declared. Backend-defined coverage is restricted
to recognised built-in/interface facts: a descriptor number or arbitrary `float4`
does not explain its application meaning. A partially covered aggregate reports
uncovered logical members. These are static records/diagnostics, not per-scalar
runtime provider objects. PSO reconciliation checks required stage connections and
coherent use identities without rewriting authored expectations.

Independent mappings are the initial required route. `annotation_mode` defaults
to `disabled`. An optional `semantic_v1` extractor may consume HLSL comment blocks
`/* @morphic.mapping.v1 <JSON mapping payload> */`, with component/provenance supplied
by the containing target. Enabling this mode requires a qualified extractor that
associates the payload with validated declarations in that target's actual
preprocessing/conditioning scope. Raw comment proximity is insufficient. Unsupported
enabled extraction fails visibly; it never silently drops annotations or grants
them precedence over external records.

### Completion: evidence and capability records

The baseline DXC path has no enabled inactivity producer or validator. Consequently
it accepts mappings resolving against final reflected facts and rejects unresolved
ones; it cannot manufacture inactive outcomes. This is a complete initial behaviour,
not a requirement to implement a new compiler analysis before any library can ship.

Evidence format 1 has a shared `build` record and `declarations`, `analyses`,
`proofs`, `correspondences` and `coverage` tables. Table references are checked local
unsigned indices; absence is explicit, not index zero. No runtime pointers, compiler
object addresses or public authoring SPIR-V IDs are persisted. Record envelopes
contain build/occurrence/proof-contract/profile/provenance references. Their three
discriminated payloads are exactly the agreed elimination, non-participation and
ancestor witnesses. Each contract revision defines required references and facts;
unknown required versions, truncated facts or cycles fail validation.

Context records retain exact effective inputs/dependencies, tool/adapter pins,
entry point, pipeline configuration and SHA-256 associations with checkpoint/final
artifact bytes. A profile record states its exact identity/revision, production
and validation contracts separately, resource/stage/access/alias/indexing granularity,
configuration predicates and correspondence-preservation obligations. A persisted
profile description must match Rendering's recognised implementation; it cannot
enable itself. Neither source-debug metadata nor the diagnostic preprocessing pass
becomes a validated declaration inventory.

Future whole-resource elimination qualification starts with an explicitly enumerated
slice; non-array uniform-buffer occurrences are the first candidate, not an enabled
capability. It requires real pre-removal semantic correspondence, a supported
producer showing elimination of the whole binding requirement and a complete
validated witness reconciled with final output. Replacement/renaming, surviving
uses, uncertain aliases or unmodelled indexing cannot qualify. Test paired eliminated/
live cases, runtime-dependent uses, replacements, stale builds/entry points and
incomplete/conflicting witnesses for every advertised configuration. Promotion of
production and validation is separate and recorded; only a compatible pair enables
acceptance. Specific producer integration is a separately qualified extension, with
the baseline remaining empty until it exists.

### Completion: library packing, reports and limits

`shared_types` records contain a shared `name`, ordered `selections` using the
component/resource/content/path grammar, and optional explicit dependent-name
assignments. Every selected named structure resolves before consolidation. Compare
complete encodings, layouts, required interpretations and corresponding member
positions; retain source labels as occurrence provenance. Establish a checked
redirect table before editing references. Dependent names derive deterministically
from the shared root and typed path unless explicitly assigned. Conflicting
assignments to one original declaration reject; no outer-name-only merge or implicit
repacking. Revalidate every rewritten reference in the complete aggregate.

Runtime library format 1 uses one existing version-1 `.bsd` block. Its Foundation
types describe reflected layouts and bulk entries. A small typed descriptor instance
identifies `shader_library`, library format 1, the manifest entry and its SHA-256.
The manifest records each other payload's size and SHA-256; it never hashes itself.
These are integrity checks, not authentication. The manifest
is canonical UTF-8 JSON in a `u8` bulk entry, using the authoring record vocabulary
plus resolved component catalogue, effective settings, dependencies, root references,
mapping/coverage records and optional evidence tables. This reuses the document
reader and avoids a second binary semantic/string-table protocol. SPIR-V is stored
as separate named `u32` bulk entries, never JSON instruction literals. Persisted
local table indices are bounds-checked and acquire no cross-file identity.

Pack in this order: manifest, then components in declaration order and their retained
artifact/evidence payloads in declared order. Align SPIR-V entries to 16 bytes,
embedded complete `.bsd` dependencies to 128 bytes and ordinary text/opaque entries
to their declared alignment. All padding created by the packer is zero. Preserve
the bytes inside supplied artifacts. The manifest refers to entry identities, not
final byte offsets, so it can be completed before placement without a size fixpoint.

The finalisation sequence is fixed:

1. Reserve member/status records; resolve authoring and validate identities/contracts.
2. Run independent compilation/reflection jobs. Retain successful artifacts and
   all ordinary failure reports; mark unavailable dependent checks blocked.
3. Resolve occurrence mappings against per-component facts, establish shared redirects,
   consolidate types and rewrite schema/mapping references deliberately.
4. Freeze the manifest and compute every domain extent/alignment with checked arithmetic.
   Assign bulk entry identities and relocate locators within their own domains only.
5. Copy to bounded private staging, assemble once into an empty owner, then attach
   typed roles and run Rendering validation, coverage and dependency checks.
6. Publish an owning result with the optional production library only after all
   required checks pass. Never modify an existing published library on failure.

All input owners remain stable until their borrowers/publishers drain. Final output
owns descriptions, metadata and payloads; it retains no compiler, source-bundle or
staging pointers. Large code arrays stay binary. Source, staging and final storage
that coexist count against their respective configured budgets; there is no zero-copy
promise. Private snapshots of each component are optional diagnostics, not a required
second persistence path.

The logical service operations are `condition_library`, `cancel`, `receive` and
`discard`; the accepted operation returns an incarnation-qualified request identity.
The owning result contains ordered `members`, `summary`, `finalisation`, optional
`library` and diagnostics. Compilation/reflection keep existing statuses. Finalisation
is `not_requested`, `blocked`, `succeeded`, `failed` or `cancelled`, with a failing
stage/path/reason. Group delivery status does not replace any member outcome.
Compile-only mode remains available. Cancellation before final commitment stops new
work and drains accepted work; committed outcomes are received/discarded normally.

Version-1 service defaults are policy values, not new implementation claims:

| Quantity | Default |
| --- | --- |
| Compilation components / PSO recipes per library | 256 / 4,096 |
| Contexts or external library bindings | 64 each |
| Semantic definitions / uses / mapping records | 4,096 / 4,096 / 65,536 |
| Dependency/composition depth / resolved plan nodes | 32 / 4,096 |
| Manifest bytes / total group diagnostic bytes | 32 MiB / 8 MiB |
| Retained group payload / private finalisation staging | 256 MiB / 512 MiB |
| Pending preparation requests per requester / coordinator | 128 / 1,024 |

Existing compiler/reflection limits still apply independently, including their
per-report diagnostic ceilings. Allow explicit service configuration before
admission, never request-driven ceiling increases. Reserve minimal statuses before
optional payloads; diagnostic text may truncate with its flag, correctness records
may not. Limit failure rejects admission or returns the accepted operation's bounded
failure result. Vendor compiler/private allocator consumption is not falsely included
in these engine-owned budgets. The common block's inclusive 2 GiB ceiling remains
a separate hard storage limit.

### Completion: PSO descriptions, layouts and introspection

A `pso_library` authoring root has `name`, `version: 1`, explicit `libraries`, named
`pipelines` and semantic contexts/uses/mappings where required. A pipeline name is
unique within its resolved PSO-library asset. Its identity excludes native handles,
provider instances and particular material selections. References select the library
binding and pipeline name. The canonical recipe/dependency digest identifies a
particular build, separately from that public identity.

| Recipe group | Required behaviour |
| --- | --- |
| Kind/components | `graphics` selects explicit conventional stages; `compute` selects exactly one compute component and no graphics state. Tessellation stages are selected as a pair, never inferred. |
| Vertex input | Explicit bindings, stride/input rate, attributes, formats and offsets. Map attribute meaning to input occurrences; validate format conversion through the backend's recognised format table. |
| Primitive/tessellation | Topology, restart policy and patch control-point count where applicable; validate against selected execution modes and device limits. |
| Raster/viewport | Rasterisation state, culling/front-face convention, viewport/scissor counts and an explicit list of dynamic states. No hidden dynamic-state default. |
| Multisample/depth/stencil | Sample count/mask, depth test/write/compare and stencil state; disabled features have defined inactive fields, not contradictory values. |
| Outputs | Ordered colour formats, write masks and blend state; depth/stencil format and render-pass/subpass compatibility description. Initial Vulkan uses ordinary compatible render passes, not a mandatory dynamic-rendering extension. |
| Expectations | Optional assertions on derived bindings/interfaces; assertions constrain generated results and do not create another layout authority. |

A graphics recipe selects a vertex stage; rasterised colour work selects a fragment
stage. Explicit rasterizer-discard/depth-only recipes are permitted where the backend
profile supports them. Absent geometry/tessellation means those stages are not used.
No ray/mesh/task path, specialization constants or shader fallback is added.

Validate every consumed stage input against its producer using locations/components/
indices or built-ins, backend-facing types, interpolation/patch/per-vertex rules and
topology. Strip only the stage-imposed wrapper appropriate to that connection,
never an arbitrary array dimension. Extra producer values are retained where the
backend permits them. No invented widening, reinterpretation or location reassignment.
Source shape labels alone cannot reject a connection permitted by the backend's
actual interface-matching rules.
Vertex formats may supply only conversions explicitly admitted by the backend format
table and the semantic contract. Unknown formats/features reject. These checks follow
the selected Vulkan version's [shader-interface rules](https://docs.vulkan.org/spec/latest/chapters/interfaces.html#interfaces-iointerfaces);
they are additional to individual reflection, not a claim that it already links stages.

Derive a minimal binding layout from the union of active reflected requirements.
For equal set/binding coordinates across stages, descriptor kind/count and physical
representation must agree and the semantic uses must identify a coherent shared
supply. Mere layout coincidence cannot merge independent subjects. Preserve explicit
set/binding numbers and empty set positions; stage visibility is their union.
The backend checks descriptor limits and creates the corresponding
[pipeline layout](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-pipelinelayout).
Larger stable layouts, bindless indexing and root-signature generation are not
implicit initial features.

Rendering owns a queried device profile: API version, enabled features/extensions,
limits and recognised format capabilities. Compilation targets must be compatible;
tessellation/geometry and other optional uses require actual enabled support.
Offline validation can retain an explicit outstanding device-profile requirement,
but a native PSO cannot become ready until device checks and backend creation pass.
This device profile is distinct from an inactivity-proof capability profile.

Initial connection optimisation shares compatible immutable layout objects,
reuses resolved routes/prepared units and avoids redundant binding updates. It does
not rewrite shader instructions or interfaces. Any future rewrite requires a
qualified transformation with fresh reflection/coverage reconciliation. Introspection
records recipe identity, components, active inputs/outputs/resources/layouts, semantic
use links, outstanding provider/device requirements and profile requirements. Keep
authored inactive expectations separately visible. Runtime native handles are a
device-bound materialisation of this description, never persistent identity.

### Completion: compiled PSO library files and regeneration

Use one `.bsd` PSO-library file containing a canonical manifest/recipes, validated
introspection, an immutable snapshot closure of referenced shader-component library
blocks and one backend cache payload for the library. Embed each dependency digest
once, aligned to 128 bytes; do not persist per-PSO files. PSO libraries do not embed
other PSO libraries. Use the same descriptor/manifest/per-payload integrity scheme
as component libraries. This makes driver-environment regeneration independent of a
mutable source tree or missing external component files. Logical names and dependency
digests remain distinct. File kind/library format are schema data inside the existing
envelope, not a new rendering-specific binary header.

For the initial Vulkan backend, persist the opaque data returned by
[`vkGetPipelineCacheData`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html)
and use it to seed [`vkCreatePipelineCache`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreatePipelineCache.html)
while recreating every required pipeline from the recipes. Pipeline handles are not
serialized; cache contents are reuse opportunities, not a guarantee that every recipe
has a reusable native executable. No pipeline-binary or graphics-pipeline-library
extension is required. Compatible cache misses use normal backend compilation.

The manifest records format/schema version, recipe/dependency SHA-256 digests,
backend/adapter profile, target API and enabled feature profile, vendor/device IDs,
raw driver/API version, pipeline-cache UUID and optional available device/driver
identifiers. Human-readable driver strings are diagnostic. Initially require exact
recorded environment/profile matches before reusing opaque cache bytes; this
conservative engine policy may rebuild more often than the API requires. Check
the cache [header/version/vendor/device/UUID](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html)
against both metadata and the current device before passing the original unmodified
blob to Vulkan. An incompatible initial cache may initialise an empty cache under
the [Vulkan cache contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheCreateInfo.html).

Load in order: validate the envelope/extents and manifest digest; validate dependencies,
recipes and introspection; compare expected recipe identity and current environment;
then create native layouts/pipelines with an accepted cache or a fresh empty cache.
Environment mismatch, a missing cache or a cache-only integrity failure regenerates
from intact embedded inputs. Corrupt recipes/dependencies fail the load instead of
rebuilding from untrusted/incomplete descriptions. Device loss, out-of-memory and
ordinary pipeline errors are reported, not hidden in unlimited retries. A known
cache-related rejection permits at most one fresh-cache attempt.
The cache has an independent payload digest, so a byte-integrity failure can be
isolated after the container and portable inputs validate. Malformed container
structure/extents or a damaged manifest fail the load; do not attempt unsafe salvage.

Create all required PSOs privately; a failed member destroys only newly created
objects after their relevant users drain and leaves the previous library usable.
Commit the complete replacement only after validation/creation succeeds. New work
uses the new library; old device-bound handles survive until their consumers finish.
Provider items are rebound through the new introspection and are not baked into
the saved file as live resources.

Serialise cache extraction against modifications, obtain a complete bounded snapshot
and require successful full export for the engine's saved compiled-library snapshot.
Do not silently publish a truncated cache as a complete save, even where an API
permits partial cache data. Save to a temporary complete file, validate/flush/close
it and atomically replace the destination using the Host persistence boundary.
Failure preserves the old file and the live runtime library. Offline operation
without a suitable device can emit a clearly marked recipe-only library; it cannot
claim a hardware-compiled cache. Loading it follows the same regeneration path.
The destination is an explicit writable Host root/path; read-only bundle roots
reject saving. Do not write a regenerated cache back into a discovered source
asset implicitly. Portable dependency corruption is a load failure, while a cache
checksum/header/environment mismatch uses a fresh cache from intact recipes.

### Completion: providers, plans and prepared supplies

Rendering serialises provider registration, association and outcome commitment on
its existing service/coordinator context. A registration is an immutable advertisement
with a fresh provider incarnation and revision. It contains the visible binding
context, exact contracts/member coverage, profile IDs, supplied schema/layouts and
resource access, typed singular/selectable selector contract, role capabilities,
composite-unit boundaries, coherence/update/freshness guarantees and execution
affinity. It also owns or pins its callback/code/context dependencies. No provider
ID enters shader mappings or persisted PSO files.

Provider item selectors are typed values in the selected provider's context, not
runtime array indices inferred from type traversal. Singular supply uses an explicit
empty selector. Selectable supply requires configuration/convention to resolve one
valid item; ambiguous associations and invalid selections fail before admission.
A material provider may expose variants while delivering one coherent parameter
block. A provider can remain registered without resident backing.
Manual resource choices are expressed as explicitly selected typed supplies under
known contracts, including singular providers for caller-owned resources. There is
no escape flag that converts unexplained shader relationships into unchecked binding.

Composition records have a context-qualified name, exact output contract, advertised
output units, role bindings and ordered routes. Each route names a selected provider
or named composition and explicit input/output member paths. Nested compositions
must resolve acyclically. Compile a resolved route/dependency plan with one source
per output unit, retained contributor provenance and explicit profile/packing
operations. A direct provider is a one-node plan. Preserve shared dependencies;
do not expand a material into per-scalar jobs or duplicate a common provider operation.

Plan identity includes the PSO/introspection build, use/role selections, provider
incarnations/revisions, item selections and required representations/profiles.
Library/layout/profile changes or registration/association changes invalidate the
affected plan. Ordinary unit revisions refresh prepared versions. A provider reports
changes through its coordinator before newly published state becomes current for
admission; old protected versions remain immutable for their admitted users.

Initial validity/freshness conditions are finite and explicit: immutable for the
declared registration/item lifetime, current unit revision at admission, or equality
to named required epoch tags such as view/origin/scene snapshots. Conjunction is
permitted, silent weakening is not. There is no implicit refresh interval, frequency
solver or every-frame version requirement. Unsupported temporal conditions reject.
Provider guarantees describe the same conditions and their applicability; readiness
is still a fact about a selected prepared version, not registration as a whole.

The logical provider operations are `register`, `resolve_selection`, `prepare`,
`report_change`, `cancel_interest` and `retire`. `prepare` receives the resolved
unit/use, typed selection, representation/profile requirements, required epochs and
consumer scope. It may complete inline or report progress through the existing
batch/service machinery. Dependency continuations run only when ready; no service
thread waits synchronously for another batch job or for GPU completion.

Shared operations have coordinator-incarnation/non-reused-serial identities distinct
from consumer requests and prepared versions. Initially share only equal resolved
preparation keys: provider incarnation/registration revision, item/unit revision,
concrete role bindings, required
epochs, layout/profile and consumption requirements. Unequal keys do not share work
unless a separately supported provider rule establishes compatibility. Requests
retain independent outcomes, freshness checks and protection claims.
Reserve required operation/dependency bookkeeping for the resolved plan before
acceptance; a default 1,024 live operations per coordinator bounds this separately
from pending consumer requests. Reuse existing compatible operations without
duplicating their dependency nodes. Capacity refusal leaves the request unaccepted;
completion/release notifications use provisioned durable state, with ordinary
service wakeups as hints. Retry requires caller demand and an observed capacity/
state change, not an unbounded internal retry queue or busy polling.

An owning prepared supply records request/provider/item/unit identities and revisions,
contract/member coverage, bound roles, profile/layout, epoch/validity facts, concrete
CPU/GPU resource descriptors, scheduled dependencies and protection through handoff.
Dependent consumers hold that protection through CPU and applicable device use.
RHI completion tokens are Rendering-owned; Host never interprets a GPU fence or
shader meaning. A release notification is not itself proof of device completion.

Binding preparation initially supports exact-layout aliasing/copy and explicitly
registered format/packing/conversion operations. Compile typed correspondences into
unit/batch copy plans; do not perform a runtime semantic/name search for every field.
The Foundation remapper's existing direct name/type matching is not an implicit
cross-contract conversion service. Normalisation, basis construction, interpolation
or representation conversion must be selected qualified operations. Unsupported
conversion cannot be repaired by guessing a matrix, frame, origin or material.

One whole-composition admission check confirms plan validity, selected revisions,
coherent roles/epochs, freshness, required resources and scheduled dependency
enforcement, then transfers protections to work before making it runnable. Failure
releases only no-longer-needed claims, retains diagnostics and admits no partial draw.
Previous data is usable only under its existing validity conditions. Cancellation
withdraws this consumer's demand; shared operation output is retained/reclaimed
under provider policy after other demand/access is accounted for.

Retirement closes new dependent admission, invalidates future associations and
drains accepted operations, dependent plans/data, callbacks and device users before
releasing code/resources. A significant replacement receives a fresh incarnation.
Refreshing/rebasing and reclaiming redundant backing are ordinary version management,
not automatic provider retirement. These contracts require no additional thread pool.

### Completion: request, observation and storage interfaces

Use typed process-local identities with a nonzero owner/service incarnation and
non-reused 64-bit serial. Exhaustion rejects new identities; failed reservations do
not recycle issued serials. A closure identity identifies coordinator/requester
incarnations, while every owning observation acquisition has a separate identity.
No identity is an object address, persisted asset reference or ownership claim by itself.

Request acceptance reserves its state and minimal terminal/result settlement capacity
before work can run. The coordinator maintains the agreed commitment/receive/discard
rules and four closure obligation classes. Queued/running continuations register
before publication and release after final access; protection transfers have no gap.
The existing batch client's terminal/execution semantics remain unchanged underneath.

Host's generic observation operations are `create_record`, `publish_progress`,
`commit_closed`, `lookup_acquire`, `read_owned`, `retain`, `release` and `seal_lookup`.
Record creation reserves bounded progress/terminal storage and the first-close
observation credit. Rendering's coordinator alone supplies semantic progress and
commits closure; Host manages storage/claims. Beginning closure atomically obtains
the reserved initial handle. Repeated close observes the same record; extra
acquisition can fail for capacity without changing closure already in progress.

Progress contains obligation counts and bounded identity/reason diagnostics. Closed
contains immutable owned identity/state/summary text; release live graphs first.
Reads return an owned snapshot or explicit allocation/limit failure, never a borrowed
record pointer. Retain returns another independently tracked move-only claim; release
consumes exactly one. Duplicate/foreign releases fail without decrementing another
claim. Late raw-ID acquisition returns expired or coordinator-closed as appropriate.
Existing handles retain/read/release after lookup sealing and coordinator shutdown.

Use stable Host control blocks and a short owner mutex for lookup/acquire/release,
snapshot protection and reclamation decisions. Never invoke foreign callbacks,
allocate optional diagnostic text or wait for work/device completion while holding
that mutex. A successful operation establishes protection before exposing state.
No lock-free algorithm or per-frame Host message exchange is required.

The initial detached terminal-storage implementation admits data-only payloads in
a Host-owned allocator/context whose cleanup is safe from any permitted control
caller. All generic code/control support is retained from creation; no stopped
coordinator callback is used for cleanup. Foreign affinity-dependent payloads must
be settled before terminal detachment or record creation rejects unsupported lifetime
requirements. This satisfies the ownership rule without inventing a cleanup thread.

Default capacities are 1,024 closure records and 4,096 additional observation claims
per storage owner, configured before requester creation. First-close credits are
reserved separately. Progress/terminal diagnostics use the existing bounded-text
policy. Capacity pressure affects new records/retains, not existing release or safe
closure. The Host coordinator may stop while terminal control blocks remain; its
storage owner/context remains protected until final claims/calls settle. Final
process/storage-owner teardown reports outstanding leases rather than freeing live
state. Timeouts never establish quiescence.

### Completion: schema-aware bulk controls and synchronisation

Keep the implemented opaque bulk API and its quiescence-limited views unchanged.
The independent-lease extension is an additional generic connection contract over
the same Host coordinator, control pipes, notification and retirement mechanisms.
Its key adds a context-qualified connection name to the exact module-instance pair;
this permits separately declared transports without treating equal schema names as
identity. Continue the default 32 overall/eight per participant connection limits,
counting pending, active and closing incarnations of both kinds.

An immutable declaration contains key/name, producer/recipient roles, snapshot or
handshake usage, contract version, fixed payload capacity/alignment, complete schema
and selected root, and maximum acquisition-record capacity. Initial schema roots are
fixed-size types, optionally containing a bounded array plus an explicitly interpreted
count field; dynamic schema/root changes require a new incarnation. Requested
alignment is a supported power of two of at least 128 bytes. Backing size/overflow
and the complete schema are validated before either endpoint is exposed.

Register bounded declaration/schema bytes through an owning generic Host request;
Host copies/adopts them and returns a declaration token. Subsequent ordinary POD
controls carry tokens/correlations/status, not long names or schemas in 48-byte
payloads. Existing asset IDs or copied schema views are not acquisitions. Host pins
immutable complete schema storage, while endpoints interpret its domain meaning.
Both exact participants explicitly acknowledge the offered declaration after checking
root/layout/capacity and their interpretation. Agreement on capacity/roles alone is
not schema acknowledgement. Activation commits only after both accept and backing
preparation succeeds; rejection/cancellation settles the counterpart explicitly.
The extension's default budgets are 8 MiB declaration/schema bytes per connection,
64 MiB retained schema bytes per owner, 256 MiB combined triple-buffer backing per
connection and 512 MiB backing per owner. Configuration may change these before
admission, subject to the existing allocation ceiling. Old independently retained
incarnations remain charged until reclamation; these new limits do not retroactively
change the implemented opaque connection contract.

Logical controls are declaration registration, join, offer, schema decision, install,
ready, activate, close, quiescent and retired. They follow existing Host control
ordering. Declaration and endpoint operations use typed incarnation-qualified IDs.
New interfaces live in Framework; Host owns the records/backing; domain-specific
payload messages remain in the participants. This is no shader SYSTEM protocol.

Publish metadata is Host-owned beside each slot: publication serial, relevant
control-state generation and required POD-prefix serial. Fixed payload schema and
capacity belong to the transport, not repeated records. Endpoints track ordinary
POD posting/processing serials for the declared direction/domain. Failed posting
consumes no prefix; successful posting followed by wake failure still counts.
Unrelated queries need not change a compatibility generation. Participants define
which operations invalidate their payload interpretation; Host interprets none of it.

The leased data-plane operations are `begin_write`, `commit_publish`,
`inspect_publication`, `acquire_latest`, `acquire_exact` and `release_acquisition`.
Metadata inspection returns an owned copy, never an unprotected payload view.
Acquisition checks an expected publication identity and returns a protected view
or an explicit changed/empty/closing/busy result. An initial recipient holds at most
one acquisition; it must relinquish that claim after all uses finish before acquiring
another. Unlike legacy `discard_and_acquire`, failed new acquisition never silently
releases an existing claim. Historical multiple-retention remains outside this contract.

Pre-provision eight acquisition records per transport by default, plus both endpoint
control records and their completion/reporting state. Record capacity may exceed
simultaneous held payloads because released acquisitions can await reporting cleanup.
New demand returns temporary refusal when capacity is occupied; accepted demand's
completion/rollback storage already exists. Slot/record allocation never occurs on
publish/acquire/release. Cleanup uses the existing claim's provisioned state and
cannot require a fresh ordinary-work credit.

A stable Host control block's short mutex serialises operation admission,
reservations, publication/acquisition commitment, close and release accounting.
Payload writes/read/device use occur outside it under recorded protection. A write
reservation is private; close before its commit aborts publication and rollback waits
for the writer's final access. Acquisition records and slot/schema protection commit
before views escape. Close winning first prevents new grants; earlier grants remain
protected. No foreign callback, blocking wait or payload copy occurs under the mutex.
Service-thread confinement remains for producer/recipient operations; an explicitly
authorised cleanup custodian may release an old grant through surviving control state.

Snapshot use may replace only unacquired staged state and needs no per-publication
external acknowledgement. Handshake use commits an exact offer and freezes that
publication until validated acquisition acknowledgement or confirmed withdrawal.
At most one offer is unacknowledged. The durable acknowledgement identifies an actual
committed grant; notifications/POD reports are hints/correlation, not new ownership.
Application completion and release remain independent. Cancellation/withdrawal and
acquisition commit are serialised by recipient/control state; cancellation intent
does not free a payload. Required POD processing continues while adoption waits.

Close seals data-plane admission, drains in-flight callers/private reservations and
endpoint signals, and accepts valid cleanup of committed grants. An endpoint may
acknowledge quiescence only after its module-dependent users finish or explicitly
transfer safe cleanup custody. Remaining independently protected acquisitions keep
backing/schema/control storage alive after endpoint retirement; this is the extension
absent from today's borrowed views. Payload release can permit slot reuse before
compact report state is reclaimed. All delayed events validate full identities.
Replacement always uses fresh instances/incarnation/schema acknowledgement; identical
names confer no predecessor authority. Failed joins/timeouts do not free live backing.

### Completion: acceptance walkthrough and failure boundaries

The following is a design-level walkthrough, not a claim that its future APIs run:

1. Resolve a shader-library authoring document from an acquired bundle. Its explicit
   `surface/vertex/base/main` and `surface/fragment/base/main` targets compile against
   the pinned path. Their independent mappings reference complete application
   contracts in imported contexts. No source layout or name establishes meaning.
2. A rotation-only use binds explicit axes and an application-supplied qualified
   representation profile. A separate origin-relative placement use binds the view
   anchor/version and offset operating envelope. Their payloads may share a physical
   object block without imposing a universal pose contract. The selected transform
   algorithm/profile is outside this draft's deliberately deferred catalogue.
3. A complete material contract defines base-colour/opacity and a material-item role.
   Configuration selects one provider/item, illustratively labelled `Paint/FadedRed`.
   That label is not a prescribed universal selector encoding or shader variant.
   One aggregate supply covers both fields through validated correspondence.
4. Reflect the compiled targets, resolve all resource/member and vertex-input maps,
   check coverage, consolidate only explicit shared types and pack the shader library.
   The zero-proof baseline fails an unresolved selector rather than invent inactivity.
   Library validation can succeed with explicit outstanding provider requirements.
5. Resolve a named graphics recipe with compatible vertex format, stages, output
   formats/state and derived bindings. Validate the actual device and create all
   required native PSOs privately. Introspection retains both active requirements
   and authored expectations; semantic and concrete layout checks stay separate.
6. Register scene/material providers and resolve a route plan. Prepare the rotation,
   origin-relative placement/view and selected material units. Admit only when
   profile/layout, selections, origin epochs and dependency enforcement agree.
   Reuse unchanged material state across draws without per-field provider objects.
7. If Executive supplies a large composite snapshot, establish the schema-aware
   transport, acknowledge its exact schema on both sides and use snapshot or exact
   offer semantics as declared. Copy/upload can release the source before application
   completion; a direct-reference consumer retains it through actual final use.
   This route is optional for provider supply, not a mandatory PSO prerequisite.
8. Rebase the view origin through a new coherent prepared context. Refresh affected
   offsets; do not combine an old-origin offset with new-origin view data. Existing
   consumers retain their old versions, while persistent providers remain registered.
9. Save one PSO-library file with embedded shader dependencies, recipes, environment
   metadata and shared native cache. Reload on a compatible environment using the
   cache, or regenerate native PSOs with an empty cache when the driver/cache identity
   changes. Unsupported required device features fail explicitly; regeneration cannot
   invent another shader stage, enable unavailable hardware or select a fallback recipe.
10. On deliberate retirement, seal admission, settle accepted outcomes, drain module/
    device users and close endpoints. Keep independent acquisitions and data-only
    terminal observations protected until their final release. No timeout substitutes
    for quiescence and no native/source pointer enters a persistent library.

| Case to verify in the eventual implementation | Required result |
| --- | --- |
| Duplicate full target identity across files; conflicting same-revision contract | Reject before dependent dispatch; report both origins. |
| Equal physical shape with different meaning; inconsistent role or origin epochs | Reject the mapping/plan/admission at the earliest available boundary. |
| Unknown or absent resource selector on the baseline profile | Coverage failure, never inactive by absence. |
| Changed compiler/settings/input/entry with a retained proof | Scope mismatch; no proof reuse by name or diagnostic fingerprint. |
| Multiple eligible providers; invalid selected material item | Require explicit unique configuration or report the invalid selection; no fallback. |
| Ordinary compilation/reflection/finalisation failure | Preserve independent reports/artifacts and the previously published library. |
| Capacity pressure; cancellation racing readiness | Refuse before acceptance or preserve one accepted terminal outcome; no dropped result. |
| Acquisition racing close; interrupted private reservation | Commit protected grant first or reject it; rollback after final access and retain accounting. |
| Wake failure after publication; late duplicate acknowledgement/release | Do not retransmit escaped data, revive ownership or decrement another claim. |
| Coordinator/module replacement with outstanding observation or acquisition | New incarnation gains no old authority; valid protected reads/cleanup continue. |
| PSO cache mismatch, corrupt recipe, unsupported device, failed rebuild/save | Regenerate only from intact compatible recipes; preserve old live library/file on failure. |

### Completion: simplification dispositions and delivery boundaries

The delayed simplification review now has concrete dispositions for this draft:

| Former option | Draft disposition |
| --- | --- |
| 6. Resolve composition nesting into a route/dependency plan | Adopt the bounded plan above. Direct/composed supplies share plan execution; preserve dependency sharing, one route per output unit and diagnostic provenance. No compulsory payload flattening. |
| 7. Reuse Host lease/reclamation mechanisms | Keep observation and bulk acquisition contracts/record states distinct. Reuse neutral identity, allocator and protected-control machinery where it fits; no universal public lease type or shader-object ownership framework is required. |
| 8. Reuse request-lifecycle primitives | Reuse correlation/commit/settlement helpers where compatible. Keep operation-specific result meanings and the existing batch/connection contracts. No universal request state machine or new scheduler is required. |

The following draft work areas are retained as context. The current
[staged approach](../backlog/shader_system_stages.md) refines their delivery boundaries
with the user and allows explicit reviewed changes as development exposes requirements:

| Candidate specification/work area | Dependencies and completion boundary |
| --- | --- |
| Component-library conditioning | Existing compile/reflection/schema mechanisms; authoring, mappings, packing/reports and strict coverage. It may ship with zero inactivity-proof capabilities. |
| Semantic/profile registry and provider preparation | Complete semantic definitions, selected application profiles, PSO requirement interfaces and existing service/batch lifetimes; no mandatory bulk transport. |
| Vulkan PSO creation and library persistence | Validated component inputs, recipe/layout/device contracts and existing generic persistence; provider registration can remain an explicit outstanding binding requirement. |
| Generic observation and independent bulk leases | Host/Framework controls, schema/memory and current direct-connection lifecycle; no shader-domain types. Can be specified/validated independently. |
| Additional proof-capable compiler paths | The evidence interfaces and exact qualification requirements; separate optional capabilities, not a prerequisite for every preceding work area. |

Exact C++ class names, source-file placement, SYSTEM catalogue allocations and
platform mutex implementation are engineering choices inside these contracts, not
remaining semantic decisions. Preserve ABI/ownership and the existing checked
container conventions when later implementation is authorised. No task, branch,
commit, build, deployment or runtime feature is authorised by this design closeout.

## Scope inventory and remaining specification work

### Specification status and planned decomposition

The overall review draft covers the intended system. Following the user's 16 November
review, implementation readiness is established separately for each focused stage.
The [stage plan](../backlog/shader_system_stages.md) owns the proposed progression,
dependencies and chat-review arrangement. Clarify, implement and review one selected
increment, then revise affected earlier stages as integration requires. The draft
does not require delivery of the expanded system in one stage or create an implementing task.

The compile/reflection, generic schema and opaque Host bulk-distribution checkpoints
are delivered. Component-library assembly, semantic/PSO/provider facilities, generic
Host observation and schema-aware/independently leased bulk extensions remain distinct
unimplemented work areas. Runtime provider registration, observation services and
bulk extensions are not automatically prerequisites for component-library assembly.
Identify dependencies of the selected library contract explicitly in its work package.

### Settled decisions

This table summarises earlier agreed policy, including the user's final refinement
to finite transform profiles. New completion choices are review-draft additions;
they do not silently replace those agreements or claim implementation.

| Work area | Settled baseline |
| --- | --- |
| Library authoring and identity | Hierarchical authoring, concept/stage/variant/target component identity, duplicate rejection across source files and contextual library resolution |
| Catalogue and meaning | Flat ordered component catalogue with header, identity, compiled-content, provenance and semantic-link record groups; separate semantic model and independent/equivalent annotation-generated mappings |
| Mapping coverage | Typed structural obligations per component/resource/content/path, validated aggregate correspondence and provider fulfilment separate from coverage; inactive mappings require exact build scope, positive declaration resolution, justified inactivity and auditable provenance |
| Conditioning evidence | Versioned Rendering-owned per-build package of shared context, declaration correspondence and inactivity facts; supported producers establish correspondence before removal, final reconciliation validates claims, and outcomes retain compact evidence |
| Inactivity proofs | Validated elimination, selected-entry non-participation and restricted ancestor derivation; recognised versioned contracts, exact scope, complete structured facts, final consistency and valid acyclic derivation references |
| Proof records and support | Backend-independent reason codes with typed witness envelopes/payloads, separate validation diagnostics, and concrete-path capability profiles declaring production/validation support, analysis boundaries and transformation obligations |
| DXC-to-SPIR-V baseline | Current pinned compilation/reflection subset and option envelope, representative qualification only, and no production/validation support for any inactivity-proof contract |
| Compiled PSO persistence | Save/load compiled PSOs as libraries with compilation-environment metadata, validate compatibility on the loading system and regenerate when incompatible or unusable |
| Semantic contracts | Shared namespace/name/exact-revision identities, complete definitions/member references, strict structured equivalence and an extensible grammar |
| Transform semantics | Separate action, frames/origins and representation/precision; finite supported representation/operation profiles and operating conditions, with no mandatory pose decomposition or general numerical proof machinery |
| Roles and providers | Explicit contract-use instances with context-qualified names, consistent role bindings, strict eligibility/unique resolution and explicit acyclic provider compositions |
| Data preparation | Meaningful composite units, explicit coherence/freshness, declared provider guarantees, protected results and whole-composition admission |
| Request lifecycle | Independent requests over shareable operations, non-reused identities, terminal commitment, receive-or-discard settlement and explicit closure accounting |
| Generic ownership | Host storage ownership and leases, Framework generic contracts, Rendering semantic/closure authority, independent data-only terminal observation state and protected handle lifetimes |
| Module communication | Ordered POD operation/query history with per-instance snapshot/handshake SPSC bulk use, immutable declarations, schema acknowledgement and activation commitment |
| Bulk lifetime | Explicit compatibility metadata, one unacknowledged offer, separate acquisition/application/release milestones, fresh replacement incarnations and bounded Host-owned acquisition records |
| Bulk endpoint admission | Protected Host control state with exact-instance authority, acquisition commitment serialised with closure, bounded pre-provisioned operation/reservation accounting, and valid cleanup of existing grants without mandatory per-operation Host-message round trips |
| Bulk capacity | Explicit temporary refusal before acceptance, retained settlement capacity after acceptance, bounded waiting, cleanup progress independent of fresh ordinary-work capacity, and protection against payload overwrite |
| Simplification boundaries | [Resolved declaration forms](#resolved-declaration-forms), [stable validation responsibilities](#strict-validation-boundaries), [one required production assembly route](#library-assembly-routes-and-remaining-packing-work), [explicit contextual reuse](#catalogue-and-semantic-separation) and [shared bulk machinery](#establishment-usages-and-replacement) |

Use the [ownership glossary](#ownership-and-lifecycle-glossary) to distinguish
storage ownership, decision authority, operational/observation leases, acquisitions
and cleanup custody. Physical type sharing, contract equivalence, representation
compatibility and binding satisfaction remain distinct concerns.

### Closed remaining-work inventory

This table records the 15 November draft coverage of former inventory items. The
user's subsequent review found that coverage does not establish sufficient precision
or simplicity for implementation. Each relevant resolution is revisited in its
focused stage; this historical heading does not mean its design questions are all
settled. Neither user acceptance nor implementation qualification follows from it.

| Former inventory item | Review-draft resolution |
| --- | --- |
| Authoring fields, variants, names and private qualification | [Authoring and identity](#completion-authoring-and-identity): strict versioned JSON, mandatory variants, exact names and deterministic qualification. |
| Catalogue fields, references and storage form | Structured references plus [manifest/common-block packing](#completion-library-packing-reports-and-limits); no new binary grammar. |
| Shared physical types/dependencies and collision handling | Checked explicit selections and redirect/consolidation pass in the packing contract; conflicts preserve both origins. |
| Payload placement, relocation, ownership and budgets | Deterministic per-domain plan, explicit alignments, staged transactional assembly and configured limits. |
| Combined groups, results and publication | Owning ordered reports, distinct stage/finalisation states, optional atomic production output and unchanged compile-only mode. |
| Semantic vocabulary, contexts, role definitions and constraints | [Semantic records/profiles](#completion-semantic-records-and-profiles): complete typed definitions, reserved base terms and supported predicates. |
| Transform flexibility and numerical direction | Finite qualified profile references/operating conditions; no general numerical prover. The detailed transform/interpolation repertoire stays deliberately deferred at the user's request. |
| Mapping selectors, structured equality, overlap and coverage | [Mapping/comparison contract](#completion-mappings-comparison-and-coverage), including stage values, aggregate paths and distinct provider fulfilment. |
| Evidence records, capability profiles and compiler integration | [Evidence contract](#completion-evidence-and-capability-records): concrete record groups/qualification boundary; baseline proof sets remain empty. Positive producer integrations are optional separately qualified extensions. |
| PSO recipes, stage/vertex/resource validation and introspection | [PSO contract](#completion-pso-descriptions-layouts-and-introspection): explicit state, device validation, derived layouts and bounded optimisation. |
| PSO files and regeneration | [Persistence contract](#completion-compiled-pso-library-files-and-regeneration): self-contained recipes/dependencies, shared Vulkan cache and environment-validated atomic rebuild/save. |
| Provider registration, selectors, composition, updates and preparation | [Provider/plan contract](#completion-providers-plans-and-prepared-supplies): typed advertisements, resolved dependency plans, protected versions and whole-use admission. |
| Request/closure identities, observation operations and reclamation | [Request/storage contract](#completion-request-observation-and-storage-interfaces): non-reused identities, provisioned claims, owning snapshots and surviving Host control state. |
| Schema-aware bulk setup, compatibility, handoff and cancellation | [Bulk-control contract](#completion-schema-aware-bulk-controls-and-synchronisation): named connections, schema acknowledgement, per-slot metadata, protected acquisition and explicit cleanup. |
| Protocol integration and independent component boundaries | Shared existing Host controls/batch mechanisms, distinct semantic authorities and [delivery boundaries](#completion-simplification-dispositions-and-delivery-boundaries). |
| Design-level validation and examples | [Acceptance walkthrough](#completion-acceptance-walkthrough-and-failure-boundaries) connects the contracts and enumerates failure boundaries without claiming passing runtime tests. |

### Deferred simplification review

The requested later review of options 6-8 has now been performed in
[completion dispositions](#completion-simplification-dispositions-and-delivery-boundaries).
The draft adopts resolved unit/dependency plans and declines a universal public
lease/request framework. Specific internal helper reuse remains an implementation
choice within explicit protocols, not an unresolved architectural prerequisite.

### Review priorities and deliberate exclusions

Review the mandatory-variant/name grammar, canonical JSON manifest choice, configured
budgets, embedded PSO dependencies/cache-assisted recreation, provider validity modes
and independent-leased bulk contract. These are concrete resolutions selected for
review, not assumptions that the current code implements them.

The detailed transform/animation profile catalogue remains out of this closeout by
explicit user direction. No quaternion convention, interpolator, continuity class,
large-world encoding or numerical tolerance is selected globally. Executable forms
require qualified registration; exact-format data can still be supplied directly.
Native additional graphics backends, unqualified proof producers, bindless/mesh/ray
features, stream output, unrestricted expression/numerical-proof systems and arbitrary
history retention are not prerequisites silently waiting to be implemented.

### Verification and feature boundaries

The individual-reflection checkpoint already validates supported execution modes,
stage rules and layouts. Library work must preserve these guarantees and add
consolidation, binary packing/relocation, typed/rendering validation, retained
failure outcomes and preservation of a previously valid published asset.

Library verification should cover all six stages; nested/fixed arrays; non-square
matrices and context-qualified interpretations; packed/padded buffer layouts;
structured-buffer offsets/stride; descriptor arrays; patch/vertex/built-in/qualifier
facts; missing/conflicting selections; standalone and consolidated resolution;
exact SPIR-V preservation; bundle/result lifetimes; and atomic finalisation failure.
These are validation requirements, not a new passing-test claim.

Semantic/provider and generic lifecycle/transport verification belongs to its
corresponding eventual work packages: strict unmapped/conflict handling, coherent
selections, preparation/admission, cancellation/release races, requester closure,
observation lifetime, endpoint replacement and protected slot reuse. This inventory
does not make those runtime facilities prerequisites for every library test.

Wider Boolean buffer representation still requires an explicit contract; unsupported
or unrepresentable layouts fail visibly. Specialization constants remain unsupported
and not planned, rather than an outstanding completion blocker. Stream output is
not planned; CPU readback, DirectX and broader deferred pipeline features are not
added by this documentation update. No GPU execution/PSO validation claim follows
from the current compile/reflection tests, and no implementation is authorised here.

## Technical references

- [SPIRV-Reflect scope and validation boundary](https://github.com/KhronosGroup/SPIRV-Reflect#non-features).
- [DXC SPIR-V mapping](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst),
  including reflected-name limitations, layout contexts and structured buffers.
- [HLSL constant-buffer packing](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules).
- [SPIR-V physical representation](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_physical_layout_of_a_spir_v_module_and_instruction).
- [Vulkan interfaces](https://docs.vulkan.org/spec/latest/chapters/interfaces.html),
  [shader stages](https://docs.vulkan.org/spec/latest/chapters/shaders.html) and
  [geometry shading](https://docs.vulkan.org/spec/latest/chapters/geometry.html).

These sources inform importer design; the engine's selected feature policy and
schema contracts determine the initially accepted subset.
