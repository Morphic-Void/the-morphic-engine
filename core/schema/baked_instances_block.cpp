
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instances_block.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Transactional attachment and ordered release of completed data blocks.

#include "schema/baked_instances_block.hpp"
#include "schema/data_block_attachment.hpp"
#include "schema/data_block_construction.hpp"

#include <utility>

namespace schema
{

bool CBakedInstancesBlock::adopt_local(CBakedDocumentBlock&& block, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_backing != EEntityBacking::none)
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::destination_not_empty);
    }
    const CByteConstView bytes = static_cast<const CBakedDocumentBlock&>(block).buffer_bytes();
    detail::SDataBlockView view;
    if (!detail::validate_data_block(bytes.data(), bytes.size(), detail::EDataBlockKind::instances, view, diagnostic.block))
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

bool CBakedInstancesBlock::attach_asset(const CAssetId asset, const CByteConstView bytes, SInstancesBlockDiagnostic& diagnostic) noexcept
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
    if (!detail::validate_data_block(bytes.data(), bytes.size(), detail::EDataBlockKind::instances, view, diagnostic.block))
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

bool CBakedInstancesBlock::materialise(const CLiveDocument& description, CBakedSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::instances, diagnostic);
}

bool CBakedInstancesBlock::materialise(const CLiveDocument& description, CLiveSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::instances, diagnostic);
}

bool CBakedInstancesBlock::materialise(const CBakedDocument& description, CBakedSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::instances, diagnostic);
}

bool CBakedInstancesBlock::materialise(const CBakedDocument& description, CLiveSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return detail::materialise_data_block(description, *this, schema, form, detail::EDataBlockKind::instances, diagnostic);
}

bool CBakedInstancesBlock::strip_to(CBakedInstancesBlock& destination, CBakedSchema& schema,
    SInstancesBlockDiagnostic& diagnostic) const noexcept
{
    return detail::strip_data_block(*this, destination, schema, detail::EDataBlockKind::instances, diagnostic);
}

bool CBakedInstancesBlock::strip_to(CBakedInstancesBlock& destination, CLiveSchema& schema,
    SInstancesBlockDiagnostic& diagnostic) const noexcept
{
    return detail::strip_data_block(*this, destination, schema, detail::EDataBlockKind::instances, diagnostic);
}

void CBakedInstancesBlock::clear() noexcept
{
    m_document.clear();
    m_payload = {};
    m_local.deallocate();
    m_bytes = {};
    m_asset = {};
    m_backing = EEntityBacking::none;
}

bool CBakedInstancesBlock::release_local(CBakedDocumentBlock& output) noexcept
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
