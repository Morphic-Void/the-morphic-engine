
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle_builder.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Synchronous bundle construction from a stable inventory and resolved paths.

#pragma once

#ifndef BUNDLE_BUILDER_HPP_INCLUDED
#define BUNDLE_BUILDER_HPP_INCLUDED

#include "assets/bundle.hpp"
#include "containers/TPodVector.hpp"
#include "data_model/live_document.hpp"

namespace bundle
{

//  Match the framework typed-view alignment even for standalone stack records.
struct alignas(16) SBuildSource
{
    CNodeKey entry;
    const char* physical_path{ nullptr };
};

static_assert(alignof(SBuildSource) >= TPodConstView<SBuildSource>::k_align);

enum class EBuildStatus : std::uint8_t
{
    success = 0u, invalid_input, allocation_failed, size_query_failed,
    read_failed, no_content, too_large, bake_failed, validation_failed
};

struct SBuildResult
{
    EBuildStatus status{ EBuildStatus::success };
    //  Input inventory identity when available, never a temporary document key.
    CNodeKey entry;
    EValidationStatus validation{ EValidationStatus::success };
    [[nodiscard]] bool succeeded() const noexcept { return status == EBuildStatus::success; }
};

//  content is the selected directory's content object. Sources identify exactly
//  its regular files, including empty files, in any order; each path is UTF-8 and
//  zero terminated. Inventory, paths and source content remain unchanged until
//  return. The Host supplies private captured input, never mutable live-image
//  nodes, when dispatching this function to its I/O worker.
//
//  Builds in private storage, bakes once and reads directly into final ranges.
//  Failure preserves destination and view. Success replaces both; callers must
//  quiesce any borrowers of the old destination first. No binding is published.
[[nodiscard]] SBuildResult build(const CLiveDocument& inventory, CNodeKey content,
    TPodConstView<SBuildSource> sources, CBakedDocumentBlock& destination, CBundleView& view) noexcept;

}   // namespace bundle

#endif // BUNDLE_BUILDER_HPP_INCLUDED
