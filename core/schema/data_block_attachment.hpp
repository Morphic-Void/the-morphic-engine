
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_attachment.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Private shared preparation; callers commit only a fully loaded role.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_ATTACHMENT_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_ATTACHMENT_HPP_INCLUDED

#include "schema/data_block_format.hpp"

#include <type_traits>

namespace schema::detail
{

static inline bool data_block_precondition(SDataBlockDiagnostic& diagnostic, const EDataBlockReason reason) noexcept
{
    diagnostic = {};
    diagnostic.stage = EDataBlockStage::preconditions;
    diagnostic.reason = reason;
    return false;
}

template<class TSchema, class TRole, class TDiagnostic>
[[nodiscard]] static bool prepare_data_block(const CByteConstView bytes, const EDataBlockKind kind, TSchema& schema,
    const SDataBlockLoadOptions& options, TRole& prepared, TDiagnostic& diagnostic) noexcept
{
    SDataBlockView view;
    if (!validate_data_block(bytes.data(), bytes.size(), kind, view, diagnostic.block)) return false;
    if (!schema.resolved_ready())
    {
        diagnostic.block.stage = EDataBlockStage::schema_binding;
        diagnostic.block.reason = EDataBlockReason::schema_unavailable;
        return false;
    }
    if (!prepared.set_document(view.document))
    {
        diagnostic.block.stage = EDataBlockStage::format_validation;
        diagnostic.block.reason = EDataBlockReason::invalid_document;
        return false;
    }
    if (!prepared.bind_schema(schema))
    {
        diagnostic.block.stage = EDataBlockStage::schema_binding;
        diagnostic.block.reason = EDataBlockReason::schema_binding_failed;
        return false;
    }
    if (!prepared.load_supplied(view.payload, options.compare_embedded, diagnostic.role))
    {
        diagnostic.block.stage = EDataBlockStage::specialised_loading;
        diagnostic.block.reason = EDataBlockReason::specialised_failure;
        //  The prepared role borrows the caller's file, so these occurrences
        //  remain valid after its temporary lookup state and binding are destroyed.
        CBakedValueIndex value;
        if constexpr (std::is_same_v<TDiagnostic, SInstancesBlockDiagnostic>)
            value = SInstanceHandleAccess::occurrence(diagnostic.role.occurrence).baked;
        else
            value = SBulkDataHandleAccess::occurrence(diagnostic.role.occurrence).baked;
        if (value)
        {
            diagnostic.block.location.kind = EDataBlockLocationKind::source_baked_value;
            diagnostic.block.location.baked_value = value;
        }
        return false;
    }
    return true;
}

}   // namespace schema::detail

#endif // SCHEMA_DATA_BLOCK_ATTACHMENT_HPP_INCLUDED
