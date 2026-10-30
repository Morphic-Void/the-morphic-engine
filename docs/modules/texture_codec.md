Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Basis Universal texture conditioning

Rendering owns the contracts in `rendering/texture/texture_data.hpp` and
`texture_requests.hpp`, the Basis adapter and all typed operations. Core, Host
and Executive have no texture formats, options, metadata or SYSTEM texture
identities. TGA remains a separate core facility available to every component.

`CResourceRequests` owns encode pixels and decode descriptors. Decode obtains a
read-only byte view through generic `AssetAcquireRequest`; the Host does not
interpret the container. Rendering retains that acquisition until the job drains,
then sends `AssetReleaseRequest` and waits for its acknowledgement before
returning the owning result to the Rendering caller. Multiple requests are
independent; disposal rejects new acquisitions and waits for existing ones.

The Rendering thread initializes Basis before accepting work and deinitializes
it after accepted jobs drain. With no available batch runners or queue capacity,
the batch client executes inline. There is no texture-specific admission limit;
existing transport capacities and per-input bounds still apply.

`TextureEncodeRequest` transfers top-down pixels into Rendering. Input formats
are gray8, RGBA8, RGBA16F and RGBA32F. HDR accepts linear float RGB, rejects
negative or non-finite RGB and discards alpha. LDR accepts gray8 or RGBA8.
Profiles select Basis quality/effort 80/1 for `runtime_fast` and 100/10 for
`build_quality` in LDR. HDR uses quality 100 and effort 1 or 10. Mip generation
is optional.

The result is a KTX2 container with UASTC LDR 4x4 or UASTC HDR 4x4 data in
one 16-byte-aligned byte buffer. `CEncodedDescription` records dimensions,
levels, container, encoding, transfer function and the HDR scale read from
`KTXmapRange`. Rendering owns this object and its views. To retain or serialize
the container through Host, move its byte buffer into `RawAssetTransfer` and
use the existing raw save contract. No typed texture owner crosses that boundary.

A `TextureDecodeRequest` names a retained raw KTX2 asset,
a mip level and an engine storage format. LDR may be transcoded to RGBA8 or
BC7 in linear or sRGB form. HDR may be transcoded to RGBA16F or BC6H unsigned
float. One mip is returned in a 16-byte-aligned buffer. The decoded view
provides width, height, row pitch in bytes, storage format, transfer function
and HDR scale. For HDR, consumers apply `hdr_scale` to the decoded values to
recover the encoded scene range; this preserves the above-1 bloom boundary.
The engine storage enum is independent of Vulkan and DirectX enum values.

Inputs are capped at 16,777,216 pixels and the caller may choose a lower
`max_pixels` limit. KTX2 input is capped at 256 MiB, with the same pixel cap
and at most 15 mip levels. The rendering codec logs before Basis initialization,
encoding and transcoding, and reports completion or failure. Basis retains its
upstream allocation behavior, including process termination on some allocation
failures; these limits reduce exposure but cannot make OOM recoverable.

## Rendering batch jobs

`rendering/runtime/texture_jobs.hpp` provides `CTextureEncodeJob` and
`CTextureDecodeJob`. Each copies its input view and configuration and owns its
output and codec status. Inputs remain borrowed and immutable until completion.
The classes cannot be copied or moved while a batch callback may reference them.
Their `run()` methods perform the typed operations; the static
`execute(void*) noexcept` entry points use `MV_STD_ABI_CALL` and match `FBatchWork`.
The classes have no Host-message or submission-policy dependencies.

`CTextureRequests` keeps jobs at stable addresses. Monotonic batch correlations
identify active jobs independently of reusable storage slots; stale or duplicate
completions are rejected. Queued completions publish output before Rendering
reads it, while inline completion is immediate. Batch runners install Rendering's
module and memory context before invoking a job.

`ETextureStatus` is Rendering-local. Unusable output with a success status becomes
`codec_failed`. Rejected or discarded work also produces `codec_failed`, mapped
by Rendering to `EAssetStatus::conditioning_failed`; allocation failure remains
distinct. These service statuses do not expose texture details to Host.

Shutdown drains callbacks before releasing acquired views or destroying request
storage. A failed release delivery is terminal: the source remains protected
until Host reclaims the abandoned acquisition after thread and batch publishers
have drained. Module unload observes the same batch-return lifetime boundary.

## Acceptance coverage

Executive sends a generic `RunTestsRequest` naming the selected Rendering module.
Host routes that request and its correlated `RunTestsResult`, which reports overall
status and passed/failed/skipped counts. Rendering owns test selection, fixture
preparation, sequencing, assertions and cleanup in `texture_flow_tests.cpp`.
Host finds the destination among registered module service threads by module
identity; background I/O/conditioning workers are not test destinations. One
run is active at a time, with both caller and worker protected from teardown.
Terminal failure follows the actual worker's state after draining its final
messages, including when Executive is the worker and Rendering is the caller.
The runner disposes every retained fixture and result before reporting completion.
The runner calls Rendering-local services. Host retains only generic fixture and
encoded-container bytes; decoded textures remain owned within Rendering.

Executive retains 50 general asset cases, including TGA. TGA remains core
functionality available to Host and every module, separate from this texture
infrastructure. Rendering runs the original 23 texture cases: RGBA8 and
RGBA16F round trips, gray8 and RGBA32F with build-quality mip generation,
all five output formats, and rejection of pixel-budget overflow, malformed
rows, negative/non-finite HDR RGB, incompatible modes/transfers/profiles,
invalid mip levels, incompatible decode targets, malformed containers and
invalid asset identities. The checks include byte alignment, mip dimensions,
row pitch, grayscale expansion and HDR values above 1.0 after applying scale.

The ordinary `TextureService` suite connects the production Rendering service
to a real generic Host asset service and controls batch completions. It checks
concurrent LDR/HDR encode/decode jobs, reverse completion, isolated failures,
source disposal, rejection of new borrows during disposal, acquisition release
acknowledgements, stale/duplicate/wrong-phase completions, allocation failure,
delivery failure and shutdown with acquired inputs. These tests use explicit
barriers and completion control rather than codec timing.

`TextureBatch` checks typed job execution inline and queued, context-install
discard, exit draining, output ownership, empty Rendering memory attribution and
128 queued requests plus inline saturation. Shader cases retain compiler/report
coverage and concurrent group lifetime checks against the generic Host boundary.
Typed services run inside the test binary because their contracts are local to
Rendering. A separate real-DLL test drives generic `RunTestsRequest`, including
busy rejection, cancellation, setup failure and cleanup. Host process harnesses
verify real module transport and inline/batch execution attribution.

Linux CTest also runs the real Host/Executive/Rendering acceptance flow using
`tests/texture/run_tests.py`; its isolated filesystem and logs are retained
under the selected output directory. The Windows worker and lifecycle
harnesses check the same split completion summaries and the individual Rendering
cases in `tests/texture/flow_cases.json`, including their execution on the rendering
thread. Existing codec route checks still verify inline and batch execution.
