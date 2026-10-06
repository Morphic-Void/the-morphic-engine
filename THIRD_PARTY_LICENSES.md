# Third-party licences

Last reviewed: 2026-10-06.

This is the current dependency and licence record for Morphic Engine. It is
provisional while rendering and texture conditioning are being designed. Keep
the linked-component list aligned with the build, and check the contents of
each source or binary release against this record before distribution.

Morphic Engine's own code is MIT licensed under [LICENSE.txt](LICENSE.txt).
Third-party code retains its own terms; this file does not relicense it. A
description or link here is not a substitute for including required licence
texts and notices with a distribution.

## Linked component

| Component | Use | Pin | Licence in source |
| --- | --- | --- | --- |
| [SuiteUTF](https://github.com/Icabod66/SuiteUTF) | Unicode encoding and decoding in engine targets | Git submodule commit `fc51720c5ec3f1c0fd2face660f5fc6919ae1288` | [MIT](external/SuiteUTF/LICENSE.txt) |

SuiteUTF's licence text is present in its submodule checkout. Include its
copyright and licence notice with a binary distribution containing SuiteUTF.

## Pinned sources and tools awaiting build integration

These are selected and pinned, but are not currently linked into Morphic Engine.
The Git submodule revisions are recorded in the repository's Git tree. The
[Vulkan SDK lock](external/vulkan-sdk.lock.json) records the selected external
SDK version and relevant source revisions; it is not installed or enforced by
the builds yet. Its [release notes](https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html)
identify the source revisions below. Recheck licences and bundled files when
each component is integrated.

| Component | Intended use | Pin | Licence information to retain if distributed |
| --- | --- | --- | --- |
| [Basis Universal](https://github.com/BinomialLLC/basis_universal) | Host KTX2/UASTC LDR encoding and transcoding | Git submodule [tag `v2_50`](https://github.com/BinomialLLC/basis_universal/releases/tag/v2_50), commit `9bebe16726b3a61c8c213eeee3b7cffb462ef34e` | [Apache 2.0](external/basis_universal/LICENSE), [NOTICE](external/basis_universal/NOTICE), and licences for bundled files actually used; the upstream [inventory](external/basis_universal/.reuse/dep5) lists Zstd as BSD 3-Clause. |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Rendering build toolchain on Windows and Linux, including Vulkan headers and shader tools | Version `1.4.363.0`; Vulkan-Headers tag `vulkan-sdk-1.4.363.0`, commit `6802bb4733b63ed5efd3adb308a6c885ef180ea1` | The SDK has multiple component licences. [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers/blob/vulkan-sdk-1.4.363.0/LICENSE.md) identifies Apache 2.0 and MIT. Record and supply notices for SDK components only if they are redistributed. |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) | Shader compilation to SPIR-V during development; release compilation and packaging are undecided | SDK 1.4.363.0 commit `01b62ad47db7dee3dd33f90e7339cf9e860b3f93` | [University of Illinois/NCSA licence](https://github.com/microsoft/DirectXShaderCompiler/blob/01b62ad47db7dee3dd33f90e7339cf9e860b3f93/LICENSE.TXT) and [third-party notices](https://github.com/microsoft/DirectXShaderCompiler/blob/01b62ad47db7dee3dd33f90e7339cf9e860b3f93/ThirdPartyNotices.txt), if its binary or code is redistributed. |
| [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect) | SPIR-V reflection; runtime or conditioning placement is undecided | Git submodule SDK tag `vulkan-sdk-1.4.363.0`, commit `f3e261fda74ca81b44d1941931c6b7b5fe236d0e` | [Apache 2.0](external/SPIRV-Reflect/LICENSE), if its source or compiled code is redistributed. |
| [miniaudio](https://github.com/mackron/miniaudio) | Host playback device abstraction; no capture, decoding, or mixing role selected | Git submodule [tag `0.11.25`](https://github.com/mackron/miniaudio/releases/tag/0.11.25), commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | [Public domain or MIT No Attribution](external/miniaudio/LICENSE); use the MIT No Attribution option when distributed. |

Rendering will use the operating system's Vulkan loader; the present plan does
not bundle it. The SDK version pin does not require DXC in a release binary.
Using an SDK during development alone does not make every SDK component part of
a Morphic binary distribution.

## Not selected for integration

- [KTX Software](https://github.com/KhronosGroup/KTX-Software) validation tools
  may be evaluated separately. No KTX library or tool is pinned or linked.

## Maintaining the record

When a component is integrated or updated, record the exact source revision,
which build targets use it, which optional features and bundled files are
compiled, and whether any upstream files were modified. Preserve upstream
copyright and licence notices in redistributed source. Apache 2.0 also requires
prominent change notices on modified files and a readable copy of applicable
upstream NOTICE attributions when distributing derivative works.

For each binary release, provide the full applicable third-party licence texts
and notices in material shipped with the binaries, and verify the list against
the actual linked and bundled components. In particular, check Basis Universal's
bundled code (including Zstd when enabled), SPIRV-Reflect if compiled into
Rendering, and DXC if a compiler binary is packaged. Keep build-only tools and
system-provided libraries distinct from redistributed components.
