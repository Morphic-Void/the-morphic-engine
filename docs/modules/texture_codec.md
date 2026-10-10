Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Basis Universal texture conditioning

The rendering thread owns Basis Universal initialization, encoding and
transcoding. It initializes Basis before accepting work, processes one texture
request at a time, and deinitializes it after its request queue is drained.
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
