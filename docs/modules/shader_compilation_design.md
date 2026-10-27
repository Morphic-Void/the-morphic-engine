Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   shader_compilation_design.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   25 Oct 2026

# Shader compilation direction

This is the agreed direction and remaining design work for shader conditioning.
Request/result structure choices, compilation defaults, feature restrictions and
configurable service limits were agreed on 21 October 2026. The initial
compile-only path is now implemented; the implementation and validation record
below distinguishes it from deferred reflection and pipeline work.

The agreed next phase is described in [reflection and component libraries](shader_reflection_design.md):
schema-backed resource and stage interfaces, explicit shared-type mappings,
renderer-owned compile/reflect reports and live library assembly before baking.
The complete reflection/library conditioning path remains unimplemented; the
reflection document describes its narrower initial adapter checkpoint and remaining
decisions. Generic schema composition and version-2 persistence are implemented. The
compile-only contracts below describe current behaviour, not the final library
representation or publication envelope.

## Initial implementation

Core owns the public shader data and cross-component message contracts. Shared
data types use namespace `shader`; rendering's compiler adapter, jobs and
`SLimits` service policy remain in `rendering::shader_compiler`. Core, Host and
Executive consume the Core contracts without including rendering's conditioning
implementation. The request, borrowed-work and owning-result exchanges retain
SYSTEM identities because they cross component boundaries. Compiler objects and
batch job state remain private to rendering.

Core must not include or compile DXC or Basis Universal source or headers,
directly or transitively. Shared stage, target, format and option values are
engine-defined abstractions; rendering translates them to compiler/codec values
and flags. Shared artifact owners contain engine buffers and descriptions, with
no compiler interfaces or codec objects in their public types.

The public contracts are in `core/shader/shader_data.hpp` and
`shader_messages.hpp`. A `ShaderCompileRequest` owns its `CCompileRequest` and
names one bundle asset. Initialise its fixed ordered member arrays, then fill
the include roots, common definitions and per-entry fields. The Host protects
the complete bundle backing with an operation lease and sends the immutable
view to rendering. That lease lasts through every accepted callback and its
terminal response, including shutdown, until the group reply returns.

`CShaderRequests` coordinates stable `CCompileJob` objects through the existing
batch client. Shader correlations occupy the high-bit range, distinct from
texture correlations. The coordinator accepts a rendering-private `SLimits` policy at
construction; the standard rendering entry uses its documented defaults.
Individual requests cannot raise those ceilings. Budget accounting reserves
member records first, then accounts owned report allocations during collection.
Limit failure can leave a member status without optional report payload.

The compile-only operation publishes one `ShaderCompileResult` owner as a Host
asset. The client's ordinary `AssetResult` carries service status, asset identity
and `shader_group_view()`. A successful publication can contain failed shaders;
inspect the group summary and each report. Views remain valid until disposal of
that group asset. Compiler jobs and report classes have no Host asset dependency.

`shader_compiler.cpp` uses checked engine containers for temporary storage and
explicit status returns, with no per-file exception allowance. Request validation
is allocation-free. UTF conversion rejects malformed encodings and embedded NULs;
argument pointers are materialised after their backing storage stops growing.
Include callbacks retain allocation failures even if DXC tries another candidate.
The DXC adapter explicitly loads the compiler beside the executable, pins it on
Windows or uses Linux `RTLD_NODELETE`, and balances
load references so renderer replacement does not accumulate references. DXC
objects are created and destroyed on each executing callback's thread.

The package pin is DXC `v1.9.2609`, source revision `01b62ad47db7dee3dd33f90e7339cf9e860b3f93`.
Acquisition checks the upstream archive SHA-256; jobs check the compiler's
reported version/revision. Reports retain effective definitions, compiler
identity, dependency paths, byte lengths and FNV-1a content identifiers. The
content identifiers support diagnosis; they are not cryptographic cache keys.

`rendering/shaders/morphic_shader.hlsli` is the version-1 support include. Bundle
it with the sources and use `MV_SHADER_RESOURCE(kind, group, binding)` and
`MV_SHADER_LOCATION(index)`. The adapter reserves the `MV_SHADER_` definition
prefix. Binding allocation, array-range conventions and collision validation
remain pipeline/reflection work; the macros translate explicit caller values.
The initial buffer layout is fixed to vector-relaxed std140/std430.

See [building and testing](../project/building_and_testing.md#shader-compiler-dependency)
for dependency acquisition. Validation coverage includes the real rendering DLL,
all six stages, nested/missing/escaping includes, disallowed numeric capabilities,
inline and queued work, reverse completion order, concurrent groups, shutdown
draining, and report lifetime after bundle disposal. This establishes compilation
and transport behaviour, not GPU execution or tool-specific debugging support.

## Agreed scope

HLSL compilation will use DXC and initially produce Vulkan SPIR-V. Support all
six conventional stages: vertex, hull (tessellation control), domain
(tessellation evaluation), geometry, pixel (fragment) and compute. Compiling a
stage does not establish that a particular device supports executing it.

The rendering module coordinates compilation groups through the existing
[batch runners](batch_runners.md), following the ownership and completion
patterns already used for [texture conditioning](texture_codec.md). The renderer
owns intermediate compilation results. The Host supplies asset services and
retains output only when the enclosing operation publishes it. Compilation and
reflection remain separate operations; compiling a group does not link or
validate a pipeline.

Compilation runs in-process, with DXC converting its internal C++ exceptions
into HRESULTs and result diagnostics. The engine adapter uses explicit error
returns and is compiled without exception handling. A compiler helper process is
not part of this scope.

The default buffer layout is vector-relaxed std140 for uniform buffers and
vector-relaxed std430 for storage buffers. CPU and shader representations may
differ where needed. Direct sharing requires verified layout compatibility;
the default does not promise identical C++/HLSL bytes or select a schema
conversion implementation.

Adopt a small, versioned shader support include for binding/location translation
and compiler-controlled target defines. A richer binding description and its
generation/validation belong to later reflection and pipeline conditioning.

Each compilation has one report, including unsuccessful compilations, and the
renderer-owned group retains its reports and summary. An ordinary shader error
allows other jobs to continue. Successful outputs remain available when another
shader fails. An explicit compile-only operation publishes its completed group
to the Host; later conditioning operations may consume the reports within the
renderer and publish a finished package instead.

CPU readback, pipeline assembly and validation, DirectX output, mesh/task shaders
and ray tracing are outside the initial scope. Stream output/transform feedback
is no longer planned; its legacy status and the preferred compute direction are
recorded below. All six compilation stages remain in scope.

## Source bundles and includes

The renderer obtains and holds one complete-bundle reference across the group.
Each job borrows the immutable view and serves source and include bytes directly
from it. Include processing makes no further Host requests and performs no
filesystem reads. This permits compilation on any assigned batch runner,
including the existing inline fallback on the rendering thread.

The [bundle contract](../assets/bundle_specification.md) remains authoritative.
A copied view does not retain its backing allocation. The bundle owner must
coordinate closure with the group: the renderer releases its reference only
after all accepted jobs and their terminal batch responses have drained.
Cancellation does not allow earlier release while a callback can still run.
Compiled reports own their bytes and remain usable after source release.

The include adapter should use `IDxcIncludeHandler`, with the entry source's
bundle path supplied as its compiler filename. DXC supplies candidate filenames
to `LoadSource`; the callback has no separate parent-file argument. It must
normalise those candidates into bundle-relative lookup paths. The exact
current-directory and ordered include-root rules are exercised against the pinned
DXC build, including nested includes and `../` within the bundle.
Reject escape above the bundle root and external absolute/logical-root paths.
Use the bundle's existing case and path rules consistently. Missing content
becomes a diagnostic, with no fallback to disk. Do not implement a second HLSL
preprocessor to discover includes.

Use explicit UTF-8 source encoding. `IDxcUtils::CreateBlobFromPinned` can wrap
include bytes without copying; the group must outlive those wrappers. Its size
argument is 32-bit, so check narrowing even with the existing bundle size bound.
Bundle construction currently omits empty files; an empty header therefore
needs a non-empty representation, such as a comment, under the current format.
The callback and blob contracts are in the
[DXC API header](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/include/dxc/dxcapi.h).

## Shader support include and binding direction

The source bundle will contain a small shader support include providing resource
binding macros and stage-location macros. Target-selection defines are supplied
by the compiler adapter; caller definitions must not override that reserved
configuration. The support include and effective defines are compilation inputs,
so their versions/content must be accounted for in future caching and provenance.
Workers resolve this include from the retained bundle using the same mechanism
as other source files, without Host callbacks or a separately loaded disk header.

Keep ordinary HLSL resource types, array sizes and semantics visible in shader
declarations. Resource macros translate group/binding arguments into HLSL register
declarations; location macros emit Vulkan annotations for SPIR-V and omit them
for future DirectX compilation. The initial resource translation can be the
same for both targets. Exact macro names, include path and version spelling are
implementation details to make concrete with the interface proposal.

The proposed initial convention uses distinct resource binding numbers across
register classes within each group, mapping HLSL spaces to Vulkan sets. Shared
pipeline declarations should give a resource a stable identity across stages.
Application/pipeline code supplies group meanings. Descriptor arrays require a
consistent range reservation rule for future DirectX use. Binding-number policy
remains separate from adopting the translation macros themselves.

Macros provide syntax translation. They do not allocate bindings, establish CPU
layout compatibility, validate collisions or construct backend pipeline layouts.
A later binding description may generate shader declarations and drive Vulkan
pipeline layouts or DirectX root signatures, with reflection checking agreement.
That description should be authoritative for the intended contract even when
optimisation removes unused resources from individual compiled stages. Its
schema, generator and pipeline integration are deferred; the initial support
include does not depend on implementing them.

## Requests, reports and group completion

The agreed group request contains one bundle asset ID, ordered include roots,
common definitions and an owned array of compilation entries. Each entry holds
its source path, entry point, shader stage, complete typed options and local
definitions. Local definitions override common definitions. Duplicate names in
either list and overrides of reserved compiler definitions are rejected.
An entry's request-array index is its member identity; results preserve request
order regardless of execution order. A one-entry group handles single shaders.
Variable-size strings and arrays belong in owned request storage; transport
messages carry bounded metadata and ownership rather than inline source trees.
DXC interfaces and compiler-specific pointers remain inside the adapter.

The owning result objects have no dependency on Host asset IDs. Their
roles are:

- `CCompiledShader`: aligned SPIR-V bytes, stage/target and emitted requirements,
  with an immutable byte view. The enclosing report retains entry-point metadata.
- `CShaderCompileReport`: compilation outcome, diagnostics and an optional
  compiled shader.
- A compilation-group result: reports and their summary under one collective
  lifetime, with member identities local to the group.

The declarations are in `shader_data.hpp`. Each completed compilation report owns:

- A stable request/member identity and compilation status.
- Diagnostics, including warnings on success and errors on failure.
- Compiled bytes on success, with format, stage, entry point and target metadata.
- Effective options and compiler identity sufficient to explain how it was built.
- Any source/dependency identifiers needed after the bundle is released.

Diagnostics are UTF-8 text with an explicit truncation flag. Identical common
metadata may be stored once in the group. Public views remain independent of
the original request, source bundle and compiler interfaces.

Report storage contains engine-owned bytes and strings, so no DXC object or
borrowed source string survives in it. The rendering coordinator may consume
these objects directly for reflection, validation, further conditioning and
package construction. Compiler jobs do not publish assets or require Host
round trips between those operations.

The group summary accounts for every member, including failures, cancellations
and work that could not start. Counts and an overall outcome permit quick
inspection; member identities preserve correlation when jobs complete out of
order. Members identify reports within `CCompileGroup`, rather than containing
independent report asset IDs. `SSummary` carries counts and `EOutcome`.

Keep service status, group outcome and member status distinct. Successful
publication can contain a group with failed compilations. The service status
describes request execution/publication; the group outcome distinguishes all
succeeded, completed with member failures and infrastructure interruption.
Member status describes success, invalid input, compilation failure, allocation
failure or work not executed. Limit failures also need an identifiable outcome.

Publication belongs to the enclosing rendering operation. Initially an explicit
compile-only request transfers its completed group, including failure reports,
to the Host for client access. The intended public lifetime is one retained
group asset with borrowed views into its reports and compiled bytes. Disposal
invalidates all of those views. Separate report assets and extraction/independent
retention are not required initially. A published group does not imply that all
members compiled successfully. The texture policy of discarding unsuccessful
results must not be copied into this shader path.

A later condition-and-package operation can retain intermediate groups entirely
within rendering and publish only its final output and appropriate diagnostics.
Both operations reuse the same compiler jobs and owning result classes. The
initial compile-only publication provides an observable end-to-end path without
making Host ownership a prerequisite for subsequent renderer work.

Keep infrastructure/admission failure distinct from shader failure. In particular,
failure to allocate a report may leave only a member status and no report body.
Ordinary errors do not cancel siblings. Shutdown and cooperative cancellation
must drain accepted work; they cannot forcibly interrupt a synchronous DXC call.
Catastrophic compiler failure has the separate constraints described below.

The agreed publication path sends one terminal completion for a compile-only
group. Its bounded message carries correlation, service status, group asset ID
and a borrowed group view. Reserve member-result bookkeeping before dispatch so
failure can still be recorded without a full report body. Failed publication
releases unpublished output. The agreed compilation options and configurable
service limits are specified below.

## DXC integration findings

### Distribution and platforms

The current stable release examined on 21 October 2026 is
[v1.9.2609](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609).
It is now the adopted compiler package. Inspection of the
official `dxc_2026_09_29.zip` directory confirmed compiler DLLs, executables and
import libraries for **x86, x64 and ARM64**, plus development headers and licences.
Thus Windows Win32 and x64 integration can use official binaries.

**Linux does not inherently require a source build.** The
[release assets](https://github.com/microsoft/DirectXShaderCompiler/releases/expanded_assets/v1.9.2609)
include `linux_dxc_2026_09_28.x86_x64.tar.gz` for Linux x86-64. Prefer that binary
route for the current Linux build, subject to checking the shared library's
glibc/C++ runtime dependencies and loading it on our Ubuntu 24.04 CI image.
Other architectures, a different libc or an older deployment baseline may need
a compatible distribution or a source build. The archive name does not establish
support for 32-bit Linux.

Prefer a reproducibly acquired, version-pinned shared-library package with
matching headers, checksums and retained licence files. The engine should not
silently select whichever compiler happens to be installed. The
[upstream README](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/README.md)
lists GitHub/NuGet distributions and distinguishes the Windows SDK's DXIL-only
compiler from SPIR-V-capable distributions. Verify SPIR-V support in the chosen
artifact with a compilation smoke test.

SPIR-V compilation does not require creating a Vulkan device or a DirectX device.
`dxil.dll` provides DXIL validation/signing functionality; it is not the SPIR-V
validator. Keep DXIL packaging separate from the initial SPIR-V requirement.
The exact deployed file set, OS/runtime imports and Linux C++ runtime baseline
must be verified when the dependency is adopted. Preserve the package's licence
notices rather than applying the engine's MIT header to upstream files.

Building DXC from source is a fallback if a required platform or package feature
is missing. It introduces a separate LLVM-based build and dependencies; its
[build guide](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/docs/BuildingAndTestingDXC.rst)
documents CMake, compiler, SDK and submodule requirements. It need not be built
with the engine's exception settings.

**macOS has a binary-distribution option, but not in that Microsoft release.**
The [LunarG macOS Vulkan SDK](https://vulkan.lunarg.com/doc/view/latest/mac/getting_started.html)
includes DXC for HLSL-to-SPIR-V work. It is a candidate source of prebuilt tools;
adopting its compiler library requires checking the selected SDK's shared-library
contents, architecture slices, minimum macOS version, dependencies, redistribution
and application signing/loading. SDK availability alone does not establish that
its DXC version matches our pin or that its library is ready for our in-process
adapter. A pinned source build is the fallback where those requirements differ.
DXC has an existing macOS build route; see the upstream
[macOS build discussion](https://github.com/microsoft/DirectXShaderCompiler/issues/5762)
and the build guide above. No macOS artifact has been validated for this project.

Hosting DXC on macOS is separate from executing shaders there. A future port
still needs a Vulkan-over-Metal implementation or a native Metal backend and its
shader conversion path. Desktop-produced assets can also avoid shipping a
compiler in a runtime that only consumes precompiled shaders. No Apple rendering
backend or on-device compilation requirement is selected here.

Compiling shaders on a desktop for Quest does not require running DXC on Quest.
The examined release has no Android package. On-device compilation would require
a separate toolchain/deployment investigation, beyond the future Quest rendering
target itself.

### API and module lifetime

Use `DxcCreateInstance`, `IDxcCompiler3` and `IDxcUtils` behind a narrow rendering
adapter. DXC uses reference-counted COM-style interfaces, but its factory does
not require COM initialisation. Explicit loading through the platform module
facilities would allow a missing/incompatible compiler to become a service
availability report. The initial implementation uses explicit loading.

Only the adapter should include DXC headers. The compiler's wide-string ABI uses
UTF-16 on Windows and UTF-32 on Linux; convert from engine UTF-8 locally. Supply
arguments as separate strings. Copy outputs into engine-owned storage before
releasing compiler objects. DXC blobs must be released through their interfaces,
not freed by the engine allocator. These boundaries follow the
[versioned API declarations](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/include/dxc/dxcapi.h).

DXC recommends retaining its library for the process lifetime. Its factory is
thread-safe, but compiler objects must be created and used on the same thread.
The initial job therefore creates, uses and releases its DXC objects
inside its executing batch callback, including inline execution. Do not create a
compiler on rendering and then transfer it to a worker. Independent instances
allow parallel jobs without a shared compiler lock. See
[upstream library guidance](https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll).

Account for process-lifetime loading explicitly when rendering modules are
replaced. All job callbacks and interface releases must finish before their
owning rendering code unloads. Compiler handles must not accumulate on each
replacement; the implemented process-lifetime pin is described above. Reuse the
existing batch completion routing and draining contract, with stable job storage
and correlation that distinguishes shader jobs from texture jobs.

### Exceptions, allocation and failure

DXC uses C++ exceptions internally. Its compiler implementation catches
`std::bad_alloc` and returns `E_OUTOFMEMORY`; other catches produce HRESULTs or
diagnostic results. Internal exceptions are acceptable: the requirement is that
they are converted before entering exception-free engine code. Use DXC's own
conversion where provided. If an API path needs an additional catch boundary,
place it in a small adapter translation unit built with exception handling,
using a compatible C++ ABI/runtime, and return engine status/diagnostics through
a non-throwing interface. Catch within that adapter before unwinding could cross
engine frames. The rest of the engine retains its exception-free build policy;
engine callbacks must not throw. Whether an extra shim is needed depends on the
actual API paths used and their verified boundary behaviour.
The current adapter uses DXC's existing conversion boundaries and has no extra
catch shim. Its own include callbacks are non-throwing, and its scratch storage
uses checked engine allocation. Allocation-failure tests sweep the adapter's
successful allocation path, including include callbacks and retained reports;
these tests do not inject failures into DXC's private allocator.
`DxcCreateInstance2` offers a custom `IMalloc`, but allocator replacement is not
needed for the initial adapter and does not prove complete allocation accounting
or recoverability. See the
[compiler implementation](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/tools/clang/tools/dxcompiler/dxcompilerobj.cpp)
and [allocator entry point](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/tools/clang/tools/dxcompiler/dxcapi.cpp).

There are two status levels: the `Compile` call's HRESULT and the status in its
returned result. A successful call can contain an unsuccessful shader result.
Read diagnostics on both success and shader failure; inspect every API result
before using its output. The ordinary-error continuation policy applies to a
valid returned compilation result.

Upstream's [advanced error guidance](https://github.com/microsoft/DirectXShaderCompiler/wiki/Advanced-Error-Handling)
warns that a failing `Compile` HRESULT can leave compiler/allocator state unsafe
and recommends process termination. Unrecoverable faults can also escape as
Windows structured exceptions. Although that guidance predates the examined
release, the current catches do not establish a general recovery guarantee.
Treating OOM or an internal compiler fault as an ordinary shader error would
therefore be unjustified. Catching a fault is not proof that continued execution
is safe.

The selected approach is in-process compilation with exception conversion at
the boundary. Log before and after compiler work and bound accepted input; these
measures do not guarantee recovery after OOM, memory corruption or an
unrecoverable compiler fault. An ordinary C++ catch cannot make corrupted state
safe or portably recover from access violations/signals. Process isolation is
outside the selected scope. Neither continuing siblings after catastrophic
corruption nor guaranteeing a retained failure report in OOM is promised by the
group behaviour.

## Compilation options and remaining decisions

Use engine types for stage, output format and configuration; keep DXC/Vulkan
translation local. Prefer a defined option set to unrestricted command strings.
Always record effective values rather than relying on changing compiler defaults.

| Decision | Status or direction to discuss |
| --- | --- |
| Language and shader model | Agreed defaults: HLSL 2021 and stage-specific Shader Model 6.0 profiles. |
| Vulkan target | Agreed default: Vulkan 1.1. Higher targets can be explicit options; the future Quest device/runtime profile still needs capability verification. |
| Buffer layout | Vector-relaxed std140/std430; CPU and shader representations may differ. The initial interface exposes this layout only. |
| Matrices | Agreed default: row-major storage, with explicit shader declarations able to override it. Mathematical multiplication conventions remain a separate concern. |
| Optimisation/debugging | Agreed defaults: no optimisation, source-debug information, warnings retained without warnings-as-errors, and compiler validation enabled. The selectable modes are specified below. Optimised final output and stricter warnings in future development builds remain the intended direction. |
| Features | Agreed initial feature restrictions and metadata policy are specified below. |
| Bindings/interfaces | Agreed: small bundled support include for resource/location macros and controlled target defines. Final numbering/range conventions remain to be settled; richer binding descriptions are deferred to reflection/pipeline conditioning. |
| Metadata | Preserve information needed for later reflection; strip distribution artifacts only after reflection has extracted it. |

DXC's default SPIR-V buffer layout is relaxed std140/std430; the GL, DX and scalar
layout switches select different rules. A DX-compatible layout is not automatic
proof of shared CPU/HLSL representation. CPU offsets, array/matrix strides,
padding and scalar widths require validation. Future DirectX compatibility must
be tested against the corresponding DXIL layout. The
[SPIR-V mapping manual](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/docs/SPIR-V.rst)
also documents reflection decorations, target environments and extension controls.

Vulkan version alone does not establish support for optional layout or shader
features. Check and enable required device features, including scalar block
layout where selected, tessellation and geometry. A future Quest profile needs
an actual device/runtime capability check; no particular Quest scalar-layout or
geometry/tessellation guarantee has been established here. See the
[Vulkan layout guide](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)
and [device feature definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html).
Schema-generated shader declarations and representation conversion remain
[separate future work](../backlog/schema_follow_on.md).

### Agreed development options and feature policy

The initial development settings are explicit request values, independent of the
engine executable's Debug/Release configuration. The agreed options are optimisation
disabled or levels 1/2/3, debug information none/source-and-lines/source-debug,
and warnings-as-errors as an independent boolean. Start with `-Od`, source-debug
information and warnings-as-errors disabled. Keep compiler validation enabled
and retain reflection metadata. Future distribution conditioning can select
optimisation level 3 and strip debug/reflection information after extraction;
optimised development builds can retain debug information.

The pinned compiler can drop successful preprocessor warnings when embedding
source in either debug mode. The adapter therefore also invokes DXC's own
preprocessing action and retains its diagnostics before compilation for those
modes. This incurs extra compiler work, including for the default source-debug
mode; selecting no debug information avoids that extra action.

For source-debug information use `-fspv-debug=vulkan-with-source`, which embeds
source and non-semantic debug instructions. Both
[RenderDoc](https://github.com/baldurk/renderdoc/blob/v1.x/docs/how/how_debug_shader.rst)
and [Nsight Graphics](https://docs.nvidia.com/nsight-graphics/UserGuide/configure-application.html)
document this route. GPU tool support varies by stage and feature; RenderDoc
currently excludes geometry/tessellation shader debugging. Embedded information
does not guarantee that every stage can be debugged in every tool.

The enriched artifact retains the Vulkan 1.1 target but additionally requires
[VK_KHR_shader_non_semantic_info](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_shader_non_semantic_info.html)
when loaded on a device below Vulkan 1.3. Record this requirement. A separately
selected source-and-lines mode or later stripped artifact can serve deployments
without it; do not silently drop requested debug information. `-Od` controls
DXC optimisation, not the device driver's later compilation.

The agreed initial execution scope covers all six stages, ordinary 32-bit shader
data, conventional resources, fixed-size descriptor arrays and compute storage
access/atomics supported by the chosen target profile. Geometry/tessellation and
other device-optional uses still require their applicable device features; a
successful compile is not a device capability check. Defer native 16-bit storage
and arithmetic, 64-bit numerical operations, subgroup/wave operations, bindless
descriptor indexing, buffer device addresses, mesh/ray/cooperative-matrix
features and vendor-specific execution extensions. Push-constant and hidden
counter-buffer binding conventions also remain deferred.

Allow compiler-required debug/reflection metadata extensions explicitly,
including those emitted by `-fspv-reflect`. They are part of the artifact's
requirements rather than unrestricted execution extensions; see
[VK_GOOGLE_hlsl_functionality1](https://docs.vulkan.org/refpages/latest/refpages/source/VK_GOOGLE_hlsl_functionality1.html)
and [VK_GOOGLE_user_type](https://docs.vulkan.org/refpages/latest/refpages/source/VK_GOOGLE_user_type.html).
Loading the unstripped artifact requires its declared extensions to be supported
and enabled. Later stripping must remove unsupported metadata and its associated
extension requirements consistently.

Use a typed feature policy translated into DXC's extension allowlist. Optional
core SPIR-V capabilities also require checking in the emitted artifact: an
extension allowlist alone cannot prohibit a core capability such as Float64.
Record the emitted requirements and reject features outside the selected policy
with a diagnostic. This is a bounded capability check; resource reflection and
pipeline validation remain separate work.

### Agreed configurable service limits

These are the agreed service defaults enforced by the initial implementation:

| Quantity | Default limit |
| --- | --- |
| Compilation entries per group | 256 |
| Include roots per group | 32 |
| Common definitions / local definitions per entry | 128 / 128 |
| Aggregate request metadata, including all strings and entries | 4 MiB |
| Bundle-relative path / entry-point or definition name | 1,024 / 256 UTF-8 bytes |
| One definition value | 4 KiB |
| One source or include file supplied to DXC | 4 MiB |
| Distinct source/include content supplied per compilation | 16 MiB |
| Retained diagnostic text per compilation | 256 KiB |
| Compiled artifact per compilation, including embedded debug information | 16 MiB |
| Aggregate retained group result payload, including metadata and diagnostics | 256 MiB |

Source-content accounting covers files actually supplied to a compilation; the
complete bundle retains its existing format limit and may contain unrelated
assets. These limits do not bound preprocessor expansion or compiler heap usage.
Repeated includes can revisit a file; distinct-byte accounting does not provide
preprocessor include-depth protection.

Reject oversized request metadata before dispatch. Report source/output policy
limits as failures with no usable truncated shader artifact. Diagnostic text may
be truncated at a valid UTF-8 boundary with its flag set, without changing the
compiler's success/failure status. Retained output limits apply as results are
adopted into the group; temporary DXC blobs and concurrent jobs require additional
memory. Enforce group accounting during collection, not only at publication.
Reserve summary/member-status storage before optional result payloads consume
the group budget, so exceeding a limit can still be reported.
These per-request limits do not introduce a renderer-wide single-admission rule.

Keep numeric service ceilings in one policy object so build-pipeline deployments
can tune them without changing request/result layouts. Requests must not silently
raise service ceilings. The tests exercise extension/capability rejection and
request, source, artifact and diagnostic ceilings.

## Reflection direction and deferred pipeline validation

Reflection will consume retained compiled artifacts separately under the agreed
[reflection and component-library design](shader_reflection_design.md). It uses
physical schemas, non-storage associations/interfaces and renderer-interpreted
decorations; intermediate results stay in rendering and diagnostics survive
ordinary compilation or reflection failure. Semantic stage/value interpretation
is distinct from concrete buffer layouts used to construct or validate CPU data.
Compact names such as `float3x4`, `float3_` and `float3_x4` describe matrices and
padded storage under that contract; checked structure/layout and context-qualified
metadata remain authoritative. Matrix interpretation, byte storage and
mathematical usage conventions are recorded separately.

The implemented schema model permits authoring-fragment composition before final
contained-type validation. Per-shader runtime results can retain binary SPIR-V
without literal expansion. Version-2 `.bsd` persistence supplies combined storage;
library assembly still needs explicit multi-source payload packing and locator
relocation before raw assembly and typed/rendering validation. Authoring
`CSchema::bake` and byte-preserving `CBakedDataBlock::assemble` are distinct routes.
The reflection design records their constraints and the remaining contract choices.
Its agreed authoring starting point uses an ordered explicit shader/variant list,
existing compilation defaults and define precedence, and separate public,
declaration and descriptive identities. Deterministic packing preserves binary
artifacts. Compile-time variants are the baseline: explicit named compilation
targets select entry points, defines and compilation settings, with no automatic
permutation expansion. The individual-shader reflector rejects all specialization
constants with diagnostics; compile-only behaviour is unchanged. Specialization
constants are currently unsupported by reflection and conditioning, and support
is not planned. They are not a requirement for completing the agreed shader work
or a scheduled part of PSO construction. A concrete future use case may justify
reconsideration through a separate design decision. Exact identity encoding and
public conditioning APIs remain open.

Later pipeline conditioning should validate vertex-buffer construction and stage
connections, including tessellation patch interfaces and topology. The library
content description will eventually select components for PSO generation;
reflection or schema merging alone does not validate those combinations.
The PoC's intended sequence
of extracting reflection before stripping distribution shaders remains useful.
Its reconstructed [fixtures](../../tests/shader/fixtures/poc/ORIGIN.txt) provide
test material, not a production shader/JSON specification or evidence that the
original system completed every intended validation or optimisation.

## Stream output is not planned

Stream output (DirectX terminology), or transform feedback in Vulkan, captures
pre-rasterisation shader output into buffers for subsequent GPU processing or
eventual CPU readback. It is an output facility associated with graphics stages,
not a seventh stage.

Vulkan's [VK_EXT_transform_feedback](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_transform_feedback.html)
is primarily a compatibility facility for translating older APIs. Khronos states
that it is not forward-looking, is not expected to become core Vulkan or a KHR
extension, and recommends alternatives for new applications. This supports
treating it as a legacy approach for this engine. It has not been universally
removed or formally deprecated: Microsoft still documents
[Direct3D 12 stream output](https://learn.microsoft.com/en-us/windows/win32/direct3d12/stream-output-counters),
and the Vulkan extension remains available where supported.

Remove stream capture from the planned work, rather than carrying it as a
deferred feature. Prefer compute and explicit storage-buffer output for future
GPU data-generation work. The earlier possibility of avoiding graphics/compute
pipeline changes, synchronisation or duplicated work is insufficient reason to
design around the legacy facility. A demonstrated future workload could justify
reconsideration, but no stream-output options, reflection metadata or backend
support are currently planned. Geometry and tessellation compilation remain
independent of this decision.

CPU readback remains separate deferred work and can consume compute-generated
buffers; it needs its own transfer, synchronisation and ownership design.

## Integration checks before declaring support

Validate the pinned library on Windows Win32/x64
and the supported Linux environment, with exception-free engine compilation.
Exercise a minimal shader for each stage and representative PoC sources, then
nested and missing includes without disk fallback. Check concurrent groups,
mixed success/failure diagnostics, inline fallback and shutdown with work in
flight. Verify reports remain valid after bundle release and compiler-interface
destruction. A successful compiler call is not a device execution test.

On 22 October 2026, ordinary suites (`MorphicTests -t1`) passed with the pinned
library on Windows x64 Debug, Windows Win32 Debug, Windows x64 Release and
Ubuntu 24.04 GCC Debug. The TextureBatch suite includes the shader cases: all
six stages, unmodified PoC simple vertex/pixel shaders, include roots and nested
includes, diagnostics and warnings-as-errors, target/debug/optimisation options,
definition overrides, service limits, disallowed features, concurrent groups,
discarded work and source disposal while jobs remain in flight. The six build
manifest tests, policy validation and repository text checks also passed.

The Windows checks required a clean rebuild after switching from sandbox builds
with file tracking disabled to normal incremental builds: stale objects otherwise
retained an earlier report layout. The clean build and subsequent final tests
passed. Final parenthesisation-only edits were compile-checked on Windows and
Linux without repeating the passing test matrix.

macOS, Quest execution, reflection, GPU debugging and pipeline compatibility
remain outside this validation. Compiler success is not a device execution test.

After relocating the shared texture/shader contracts into Core on 22 October,
Windows x64 Debug and Ubuntu Debug ordinary suites passed again. Core's SYSTEM
owner operations also passed an isolated compilation without the repository-wide
include path; their compiler-generated dependency list contained no rendering,
Basis Universal or DXC headers. Core and Executive have no rendering header paths
or namespace references. Visual Studio item/filter XML, CRLF, BOM and existing
final-newline states were verified after the relocation.
