
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_construction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Private completed-file assembly and live bake orchestration.

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
