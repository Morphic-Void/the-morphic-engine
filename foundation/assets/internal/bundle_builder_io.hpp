
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle_builder_io.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Internal synchronous I/O seam for deterministic builder failure tests.

#pragma once

#ifndef BUNDLE_BUILDER_IO_HPP_INCLUDED
#define BUNDLE_BUILDER_IO_HPP_INCLUDED

#include "assets/bundle_builder.hpp"

namespace bundle::detail
{

struct SBuildIo
{
    void* state{ nullptr };
    bool (*query_size)(void*, const char*, std::size_t&) noexcept{ nullptr };
    bool (*read)(void*, const char*, const CByteView&) noexcept{ nullptr };
};

//  Callbacks have the same exact-size and failure contracts as queryFileSize
//  and loadFileInto. Production build() always supplies the native operations.
[[nodiscard]] SBuildResult build(const CLiveDocument& inventory, CNodeKey content,
    TPodConstView<SBuildSource> sources, CBakedDocumentBlock& destination,
    CBundleView& view, const SBuildIo& io) noexcept;

}   // namespace bundle::detail

#endif // BUNDLE_BUILDER_IO_HPP_INCLUDED
