
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data_block.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Transactional attachment and ordered release of completed data blocks.

#include "schema/baked_bulk_data_block.hpp"
#include "schema/data_block_attachment.hpp"
#include "schema/data_block_construction.hpp"

#include <utility>

namespace schema
{

bool CBakedBulkDataBlock::adopt_local(CBakedDocumentBlock&& block, SBulkDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_backing != EEntityBacking::none)
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::destination_not_empty);
    }
    const CByteConstView bytes = static_cast<const CBakedDocumentBlock&>(block).buffer_bytes();
    detail::SDataBlockView view;
    if (!detail::validate_data_block(bytes.data(), bytes.size(), detail::EDataBlockKind::bulk_data, view, diagnostic.block))
    {
        return false;
    }
    m_local = std::move(block);
    m_document = view.document;
    m_payload = view.payload;
    m_bytes = bytes;
    m_backing = EEntityBacking::local;
    return true;
}

bool CBakedBulkDataBlock::attach_asset(const CAssetId asset, const CByteConstView bytes, SBulkDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_backing != EEntityBacking::none)
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::destination_not_empty);
    }
    if (!asset.is_valid())
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::invalid_input);
    }
    detail::SDataBlockView view;
    if (!detail::validate_data_block(bytes.data(), bytes.size(), detail::EDataBlockKind::bulk_data, view, diagnostic.block))
    {
        return false;
    }
    m_document = view.document;
    m_payload = view.payload;
    m_bytes = bytes;
    m_asset = asset;
    m_backing = EEntityBacking::asset;
    return true;
}

bool CBakedBulkDataBlock::materialise(const CLiveDocument& description, CBakedSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::bulk_data, diagnostic);
}

bool CBakedBulkDataBlock::materialise(const CLiveDocument& description, CLiveSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::bulk_data, diagnostic);
}

bool CBakedBulkDataBlock::materialise(const CBakedDocument& description, CBakedSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::bulk_data, diagnostic);
}

bool CBakedBulkDataBlock::materialise(const CBakedDocument& description, CLiveSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::bulk_data, diagnostic);
}

bool CBakedBulkDataBlock::strip_to(CBakedBulkDataBlock& destination, CBakedSchema& schema,
    SBulkDataBlockDiagnostic& diagnostic) const noexcept
{
    return detail::strip_data_block(*this, destination, schema, detail::EDataBlockKind::bulk_data, diagnostic);
}

bool CBakedBulkDataBlock::strip_to(CBakedBulkDataBlock& destination, CLiveSchema& schema,
    SBulkDataBlockDiagnostic& diagnostic) const noexcept
{
    return detail::strip_data_block(*this, destination, schema, detail::EDataBlockKind::bulk_data, diagnostic);
}

void CBakedBulkDataBlock::clear() noexcept
{
    m_document.clear();
    m_payload = {};
    m_local.deallocate();
    m_bytes = {};
    m_asset = {};
    m_backing = EEntityBacking::none;
}

bool CBakedBulkDataBlock::release_local(CBakedDocumentBlock& output) noexcept
{
    if ((m_backing != EEntityBacking::local) || (output.buffer_capacity() != 0u)) return false;
    m_document.clear();
    m_payload = {};
    output = std::move(m_local);
    m_bytes = {};
    m_asset = {};
    m_backing = EEntityBacking::none;
    return true;
}

}   // namespace schema
