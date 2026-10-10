Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Basis Universal texture conditioning

The rendering thread owns Basis Universal initialization and texture job
dispatch. It initializes Basis before accepting work, submits encoding and
transcoding through its batch client, and deinitializes Basis after accepted
requests and batch completions have drained. With no available batch runners
or queue capacity, the batch client executes the job inline on rendering.
The Host admits at most one encode or decode operation across clients. A second
request completes with `EAssetStatus::busy`; callers may retry later. Capture
producers must also serialize capture against these operations when added.

An Executive sends an owning `TextureEncodeRequest` with a top-down
`CByteRectBuffer`, input format, transfer function and `CEncodeOptions`. The
Host retains that request while rendering borrows it. Input formats are gray8,
RGBA8, RGBA16F and RGBA32F. HDR encoding accepts linear RGB from the float
formats, rejects negative or non-finite RGB, and discards alpha. LDR accepts
gray8 or RGBA8. Profiles select Basis quality/effort values 80/1 for
`runtime_fast` and 100/10 for `build_quality` in LDR. HDR has no RDO quality
knob, so both profiles use quality 100 and differ by effort 1 or 10. Mip
generation is optional.

The result is a KTX2 container with UASTC LDR 4x4 or UASTC HDR 4x4 data in
one 16-byte-aligned byte buffer. `CEncodedDescription` records dimensions,
levels, container, encoding, transfer function and the HDR scale read from
`KTXmapRange`. The Host retains this object under an asset ID; clients receive
a borrowed `CEncodedView`. A retained encoded asset can be saved with an
`AssetSaveRequest` using the raw file format.

A `TextureDecodeRequest` names a retained encoded texture or raw KTX2 asset,
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

The rendering dispatcher keeps a job at a stable address and retains its result
message while it is outstanding. A queued completion publishes the job's output
before rendering reads it; inline completion can be handled immediately. The
rendering thread then moves the output into the owning Host reply. Batch runners
install the rendering module and memory context before invoking a job.

Rejected or discarded jobs produce `ETextureStatus::codec_failed`, which the
Host maps to `EAssetStatus::conditioning_failed`. Job/result allocation failure
uses the existing allocation-failure path. Every completed request releases
the Host's single-operation admission and source borrow. During exit, rendering
drains outstanding jobs and returns their results before Basis shutdown; module
unload also waits for batch return publishers. The Host continues to admit only
one texture operation across clients; parallel image admission is separate work.

## Acceptance coverage

The Executive's 71 sequential asset cases include 23 texture cases: RGBA8 and
RGBA16F round trips, gray8 and RGBA32F with build-quality mip generation,
all five output formats, and rejection of pixel-budget overflow, malformed
rows, negative/non-finite HDR RGB, incompatible modes/transfers/profiles,
invalid mip levels, incompatible decode targets, malformed containers and
invalid asset identities. The checks include byte alignment, mip dimensions,
row pitch, grayscale expansion and HDR values above 1.0 after applying scale.

The ordinary `TextureService` suite holds renderer completions explicitly.
It checks busy admission for encode/encode, encode/decode and decode/decode,
retention of borrowed input during disposal, refusal of new borrowers once
disposal is pending, completion of deferred disposal and recovery after a
failed codec operation. This does not depend on codec duration or scheduling.

The ordinary `TextureBatch` suite loads the real rendering module and controls
its batch queue. It checks inline and queued encode/decode, recovery after
context-install discard, both job types discarded during exit, an executed
decode completing after exit was requested, correlation, transferred output
and empty rendering memory attribution after draining. Process harnesses also
verify codec execution is attributed to rendering for inline work and to a
rendering-module batch runner for queued work.

Linux CTest also runs the real Host/Executive/Rendering acceptance flow using
`tests/texture/run_tests.py`; its isolated filesystem and logs are retained
under the selected output directory. The Windows worker and lifecycle
harnesses check the same 71-case completion summary.
