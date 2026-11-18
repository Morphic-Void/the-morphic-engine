
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_copy.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Independent contextual runtime copying with explicit entry selection/naming.

#pragma once

#include "schema/data_block_view.hpp"

namespace schema
{

struct SDataCopyRoot
{
    std::uint32_t source{};
    CDataEntryHandle entry;
};

struct SDataCopyName
{
    std::uint32_t source{};
    CDataEntryHandle entry;
    CStringView name;
};

//  Views and their backing remain protected by the caller during this call.
//  Required instance ancestors and complete referenced collections are retained.
[[nodiscard]] bool copy_data(const CDataBlockView* const* const sources, const std::size_t source_count,
    const SDataCopyRoot* const roots, const std::size_t root_count,
    const SDataCopyName* const names, const std::size_t name_count,
    const SDataOperationLimits limits, CBakedDataBlock& output, SDataOperationDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool repack_data(const CDataBlockView& source, const SDataOperationLimits limits,
    CBakedDataBlock& output, SDataOperationDiagnostic& diagnostic) noexcept;

//  Complete authoring document for generic inspection or CSchema::copy_from.
//  Reconstructs logical values and selections, including all named collections
//  and types. Raw scalar encodings without a logical representation reject.
[[nodiscard]] bool export_data(const CDataBlockView& source, const SDataOperationLimits limits,
    CBakedDocumentBlock& output, SDataOperationDiagnostic& diagnostic) noexcept;

}   // namespace schema
