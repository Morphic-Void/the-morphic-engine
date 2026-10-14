
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_construction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Private completed-file assembly, live baking and description materialisation.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_CONSTRUCTION_HPP_INCLUDED

#include "schema/data_block_format.hpp"
#include "schema/data_output.hpp"

#include <type_traits>
#include <utility>

namespace schema::detail
{

//  Consumes no payload ownership. Mutates only the private prepared description.
//  Output must be empty and is published only after complete assembly.
[[nodiscard]] bool assemble_data_block(CLiveDocument& description, CByteConstView payload,
    EDataBlockKind kind, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_materialisation(const CLiveDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool prepare_materialisation(const CBakedDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool prepare_materialised_output(const CBakedDocument& source, EDataBlockKind kind,
    EDataOutputForm form, CLiveDocument& output, SDataBlockDiagnostic& diagnostic) noexcept;

//  Preparation preserves tree shape. Map a temporary baked occurrence by its
//  child ordinals, without allocating diagnostic scratch or assuming live IDs.
template<class TDocument>
[[nodiscard]] static auto materialisation_source_value(const TDocument& source, const CBakedDocument& prepared,
    const CBakedValueIndex value, const unsigned depth = 0u) noexcept -> decltype(source.root())
{
    if (!prepared.contains(value) || (depth >= 256u)) return {};
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
    if (!role.bind_schema(schema))
    {
        diagnostic.block.stage = EDataBlockStage::schema_binding;
        diagnostic.block.reason = EDataBlockReason::schema_binding_failed;
        return false;
    }
    if (!role.materialise(payload, diagnostic.role))
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
    if (!prepare_materialised_output(prepared.document(), kind, form, description, diagnostic.block)) return false;
    prepared.deallocate();
    CBakedDocumentBlock complete;
    if (!assemble_data_block(description, payload.const_view(), kind, complete, diagnostic.block)) return false;
    if (!destination.adopt_local(std::move(complete), schema, {}, diagnostic))
    {
        diagnostic.block.location = {};
        diagnostic.role.occurrence = {};
        return false;
    }
    return true;
}

template<class TSource, class TBlock, class TSchema, class TDiagnostic>
[[nodiscard]] static bool bake_data_block(const TSource& source, TBlock& destination, TSchema& schema,
    const EDataOutputForm form, const EDataBlockKind kind, TDiagnostic& diagnostic) noexcept
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
    if (!source.prepare_output(description, payload, schema, form, diagnostic.role))
    {
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
    if (!assemble_data_block(description, payload.const_view(), kind, complete, diagnostic.block))
    {
        return false;
    }
    if (!destination.adopt_local(std::move(complete), schema, {}, diagnostic))
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
