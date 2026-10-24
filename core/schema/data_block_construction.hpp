
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_construction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Private completed-file assembly, live baking, materialisation and stripping.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED

#include "schema/data_block_format.hpp"
#include "schema/baked_data_block.hpp"

#include <type_traits>
#include <utility>

namespace schema::detail
{

//  Allocate the final file after preparing its description. The caller fills
//  the returned payload directly, then adopts the complete storage.
[[nodiscard]] bool prepare_data_block_storage(CLiveDocument& description, const std::size_t payload_size,
    const EDataDomain domain, CByteBuffer& storage, std::uint8_t*& payload, SDataBlockDiagnostic& diagnostic) noexcept;

//  Consumes no payload ownership. Mutates only the private prepared description.
//  Output must be empty and is published only after complete assembly.
[[nodiscard]] bool assemble_data_block(CLiveDocument& description, const CByteConstView* instances,
    const CByteConstView* bulk_data, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_materialisation(const CLiveDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool prepare_materialisation(const CBakedDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_data_block_locators(CLiveDocument& document, const bool bulk, SDataBlockDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_missing_data_document(const CBakedDocument& source, const bool bulk,
    CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

//  A loaded bulk data role supplies counts when stripping complete payloads.
//  Materialised descriptions instead carry the complete embedded arrays.
[[nodiscard]] bool prepare_data_block_output(const CBakedDocument& source, const EDataDomain domain,
    const EDataOutputForm form, const CBakedBulkData* const bulk_data, CLiveDocument& output, SDataBlockDiagnostic& diagnostic) noexcept;

//  Preparation preserves tree shape. Map a temporary baked occurrence by its
//  child ordinals, without allocating diagnostic scratch or assuming live IDs.
template<class TDocument>
[[nodiscard]] static auto materialisation_source_value(const TDocument& source, const CBakedDocument& prepared,
    const CBakedValueIndex value, const unsigned depth = 0u) noexcept -> decltype(source.root())
{
    if (!prepared.contains(value) || (depth >= document_limits::k_max_recursion_depth)) return {};
    if (value == prepared.root()) return source.root();
    const auto parent = materialisation_source_value(source, prepared, prepared.parent(value), depth + 1u);
    if (!parent) return {};
    auto original = source.first_child(parent);
    auto copied = prepared.first_child(prepared.parent(value));
    while (copied && original && (copied != value))
    {
        copied = prepared.next_sibling(copied);
        original = source.next_sibling(original);
    }
    return (copied == value) ? original : decltype(source.root()){};
}

template<class TSource, class TSchema, class TDiagnostic, class TPrepare>
[[nodiscard]] static bool bake_data_block(const TSource& source, CBakedDataBlock& destination, TSchema& schema,
    const EDataOutputForm form, TDiagnostic& diagnostic, const TPrepare prepare) noexcept
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
    if ((form != EDataOutputForm::embedded) && (form != EDataOutputForm::external) && (form != EDataOutputForm::stripped))
    {
        diagnostic.block.reason = EDataBlockReason::invalid_option;
        return false;
    }
    if (!schema.resolved_ready())
    {
        diagnostic.block.stage = EDataBlockStage::schema_binding;
        diagnostic.block.reason = EDataBlockReason::schema_unavailable;
        return false;
    }
    CLiveDocument description;
    CByteBuffer storage;
    if (!prepare(description, storage))
    {
        if (diagnostic.block.reason != EDataBlockReason::none)
        {
            return false;
        }
        diagnostic.block.stage = EDataBlockStage::description_preparation;
        diagnostic.block.reason = EDataBlockReason::specialised_failure;

        //  prepare_output() reports occurrences in the source document.
        //  Retain only valid source live locations.
        CNodeKey node;
        if constexpr (std::is_same_v<TDiagnostic, SInstancesBlockDiagnostic>)
        {
            node = SInstanceHandleAccess::occurrence(diagnostic.role.occurrence).live;
        }
        else
        {
            node = SBulkDataHandleAccess::occurrence(diagnostic.role.occurrence).live;
        }
        if (node.is_valid() && source.document_query().contains(diagnostic.role.occurrence))
        {
            diagnostic.block.location.kind = EDataBlockLocationKind::source_live_node;
            diagnostic.block.location.live_node = node;
        }
        else
        {
            diagnostic.role.occurrence = {};
        }
        return false;
    }
    CBakedDocumentBlock complete;
    if (!complete.adopt_buffer(std::move(storage)))
    {
        diagnostic.block.stage = EDataBlockStage::assembly;
        diagnostic.block.reason = EDataBlockReason::assembly_failed;
        return false;
    }
    if (!destination.adopt_local(std::move(complete), diagnostic.block))
    {
        //  Final validation sees temporary output, not the caller's source.
        diagnostic.block.location = {};
        diagnostic.role.occurrence = {};
        return false;
    }
    return true;
}

}   // namespace schema::detail

#endif // SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED
