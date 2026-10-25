
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_construction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Private completed-file assembly and live runtime baking.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED

#include "schema/data_block_format.hpp"
#include "schema/baked_data_block.hpp"

#include <utility>

namespace schema::detail
{

//  Prepare final storage for either or both domains. Null sizes omit a domain;
//  a pointer to zero retains its empty section. The caller fills payload spans.
[[nodiscard]] bool prepare_common_data_block_storage(CLiveDocument& description, const std::size_t* instance_size,
    const std::size_t* bulk_size, CByteBuffer& storage, SDataBlockLayout& layout, SDataBlockDiagnostic& diagnostic) noexcept;

//  Allocate the final file after preparing its description. The caller fills
//  the returned payload directly, then adopts the complete storage.
[[nodiscard]] bool prepare_data_block_storage(CLiveDocument& description, const std::size_t payload_size,
    const EDataDomain domain, CByteBuffer& storage, std::uint8_t*& payload, SDataBlockDiagnostic& diagnostic) noexcept;

//  Consumes no payload ownership. Mutates only the private prepared description.
//  Output must be empty and is published only after complete assembly.
[[nodiscard]] bool assemble_data_block(CLiveDocument& description, const CByteConstView* instances,
    const CByteConstView* bulk_data, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

template<class TSource, class TDiagnostic, class TPrepare>
[[nodiscard]] static bool bake_runtime_block(const TSource& source, CBakedDataBlock& destination,
    TDiagnostic& diagnostic, const TPrepare prepare) noexcept
{
    diagnostic = {};
    diagnostic.block.stage = EDataBlockStage::preconditions;
    if (destination.backing() != EEntityBacking::none)
    {
        diagnostic.block.reason = EDataBlockReason::destination_not_empty;
        return false;
    }
    if (!source.loaded_ready())
    {
        diagnostic.block.reason = EDataBlockReason::invalid_input;
        return false;
    }
    CLiveDocument description;
    CByteBuffer storage;
    diagnostic.block = {};
    if (!prepare(description, storage))
    {
        if (diagnostic.block.reason == EDataBlockReason::none)
        {
            diagnostic.block.stage = EDataBlockStage::description_preparation;
            diagnostic.block.reason = EDataBlockReason::specialised_failure;
        }
        return false;
    }
    CBakedDocumentBlock complete;
    if (!complete.adopt_buffer(std::move(storage)) || !destination.adopt_local(std::move(complete), diagnostic.block))
    {
        if (diagnostic.block.reason == EDataBlockReason::none)
        {
            diagnostic.block.stage = EDataBlockStage::assembly;
            diagnostic.block.reason = EDataBlockReason::assembly_failed;
        }
        diagnostic.block.location = {};
        return false;
    }
    diagnostic = {};
    return true;
}

}   // namespace schema::detail

#endif // SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED
