
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
#include "schema/data_output.hpp"

#include <type_traits>
#include <utility>

namespace schema::detail
{

//  Allocate the final file after preparing its description. The caller fills
//  the returned payload directly, then adopts the complete storage.
[[nodiscard]] bool prepare_data_block_storage(CLiveDocument& description, const std::size_t payload_size,
    const EDataBlockKind kind, CByteBuffer& storage, std::uint8_t*& payload, SDataBlockDiagnostic& diagnostic) noexcept;

//  Consumes no payload ownership. Mutates only the private prepared description.
//  Output must be empty and is published only after complete assembly.
[[nodiscard]] bool assemble_data_block(CLiveDocument& description, const CByteConstView payload,
    const EDataBlockKind kind, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_materialisation(const CLiveDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool prepare_materialisation(const CBakedDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

//  A loaded bulk data role supplies counts when stripping complete payloads.
//  Materialised descriptions instead carry the complete embedded arrays.
[[nodiscard]] bool prepare_data_block_output(const CBakedDocument& source, const EDataBlockKind kind,
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

template<class TDocument, class TBlock, class TSchema, class TDiagnostic>
[[nodiscard]] static bool materialise_data_block(const TDocument& source, TBlock& destination, TSchema& schema,
    const EDataOutputForm form, const EDataBlockKind kind, TDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    diagnostic.block.stage = EDataBlockStage::preconditions;
    if (destination.backing() != EEntityBacking::none)
    {
        diagnostic.block.reason = EDataBlockReason::destination_not_empty;
        return false;
    }
    if (!source.is_ready())
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
    CBakedDocumentBlock prepared;
    if (!prepare_materialisation(source, prepared, diagnostic.block)) return false;
    CByteBuffer payload;
    using TRole = std::conditional_t<std::is_same_v<TDiagnostic, SInstancesBlockDiagnostic>, CBakedInstances, CBakedBulkData>;
    TRole role;
    if (!role.set_document(CMutableBakedDocument{ prepared }))
    {
        diagnostic.block.stage = EDataBlockStage::description_preparation;
        diagnostic.block.reason = EDataBlockReason::invalid_document;
        return false;
    }
    if (!role.attach_schema(schema, payload, diagnostic.role))
    {
        diagnostic.block.stage = EDataBlockStage::materialisation;
        diagnostic.block.reason = EDataBlockReason::specialised_failure;
        using TAccess = std::conditional_t<std::is_same_v<TDiagnostic, SInstancesBlockDiagnostic>,
            SInstanceHandleAccess, SBulkDataHandleAccess>;
        const auto original = materialisation_source_value(source, prepared.document(),
            TAccess::occurrence(diagnostic.role.occurrence).baked);
        diagnostic.role.occurrence = original ? TAccess::make(SOccurrence{ original }) : decltype(diagnostic.role.occurrence){};
        diagnostic.block.location = {};
        if (original)
        {
            if constexpr (std::is_same_v<TDocument, CLiveDocument>)
            {
                diagnostic.block.location.kind = EDataBlockLocationKind::source_live_node;
                diagnostic.block.location.live_node = original;
            }
            else
            {
                diagnostic.block.location.kind = EDataBlockLocationKind::source_baked_value;
                diagnostic.block.location.baked_value = original;
            }
        }
        return false;
    }
    role.clear();
    CLiveDocument description;
    if (!prepare_data_block_output(prepared.document(), kind, form, nullptr, description, diagnostic.block))
    {
        return false;
    }
    prepared.deallocate();
    CBakedDocumentBlock complete;
    if (!assemble_data_block(description, payload.const_view(), kind, complete, diagnostic.block))
    {
        return false;
    }
    if (!destination.adopt_local(std::move(complete), diagnostic))
    {
        diagnostic.block.location = {};
        diagnostic.role.occurrence = {};
        return false;
    }
    return true;
}

template<class TBlock, class TSchema, class TDiagnostic>
[[nodiscard]] static bool strip_data_block(const TBlock& source, TBlock& destination,
    TSchema& schema, const EDataBlockKind kind, TDiagnostic& diagnostic) noexcept
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
    if (!schema.resolved_ready())
    {
        diagnostic.block.stage = EDataBlockStage::schema_binding;
        diagnostic.block.reason = EDataBlockReason::schema_unavailable;
        return false;
    }
    const CByteConstView bytes = source.file_bytes();
    SDataBlockView view;
    if (!validate_data_block(bytes.data(), bytes.size(), kind, view, diagnostic.block))
    {
        return false;
    }
    using TRole = std::conditional_t<std::is_same_v<TDiagnostic, SInstancesBlockDiagnostic>, CBakedInstances, CBakedBulkData>;
    TRole role;
    if (!role.set_document(view.document) || !role.attach_schema(schema, view.payload, false, diagnostic.role))
    {
        diagnostic.block.stage = EDataBlockStage::specialised_loading;
        diagnostic.block.reason = EDataBlockReason::specialised_failure;
        return false;
    }
    const CBakedBulkData* bulk_data = nullptr;
    if constexpr (std::is_same_v<TRole, CBakedBulkData>)
    {
        bulk_data = &role;
    }
    CLiveDocument description;
    if (!prepare_data_block_output(view.document, kind, EDataOutputForm::stripped, bulk_data, description, diagnostic.block))
    {
        return false;
    }
    CBakedDocumentBlock complete;
    if (!assemble_data_block(description, view.payload, kind, complete, diagnostic.block))
    {
        return false;
    }
    if (!destination.adopt_local(std::move(complete), diagnostic))
    {
        //  Attachment observes the new private document, whose indices can differ.
        diagnostic.block.location = {};
        diagnostic.role.occurrence = {};
        return false;
    }
    return true;
}

template<class TSource, class TBlock, class TSchema, class TDiagnostic, class TPrepare>
[[nodiscard]] static bool bake_data_block(const TSource& source, TBlock& destination, TSchema& schema,
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
    CByteBuffer payload;
    if (!prepare(description, payload))
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
    if (!complete.adopt_buffer(std::move(payload)))
    {
        diagnostic.block.stage = EDataBlockStage::assembly;
        diagnostic.block.reason = EDataBlockReason::assembly_failed;
        return false;
    }
    if (!destination.adopt_local(std::move(complete), diagnostic))
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
