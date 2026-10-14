
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

template<class TSchema>
bool CBakedInstancesBlock::adopt(CBakedDocumentBlock&& block, TSchema& schema,
    const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_backing != EEntityBacking::none)
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::destination_not_empty);
    }
    const CByteConstView bytes = static_cast<const CBakedDocumentBlock&>(block).buffer_bytes();
    CBakedInstances prepared;
    if (!detail::prepare_data_block(bytes, detail::EDataBlockKind::instances, schema, options, prepared, diagnostic))
    {
        return false;
    }

    //  No fallible work remains. Moving storage preserves all prepared addresses.
    m_local = std::move(block);
    m_role.take_from(prepared);
    m_bytes = bytes;
    m_backing = EEntityBacking::local;
    return true;
}

template<class TSchema>
bool CBakedInstancesBlock::attach(const CAssetId asset, const CByteConstView bytes, TSchema& schema,
    const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_backing != EEntityBacking::none)
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::destination_not_empty);
    }
    if (!asset.is_valid()) return detail::data_block_precondition(diagnostic.block, EDataBlockReason::invalid_input);
    CBakedInstances prepared;
    if (!detail::prepare_data_block(bytes, detail::EDataBlockKind::instances, schema, options, prepared, diagnostic))
    {
        return false;
    }

    m_role.take_from(prepared);
    m_bytes = bytes;
    m_asset = asset;
    m_backing = EEntityBacking::asset;
    return true;
}

bool CBakedInstancesBlock::adopt_local(CBakedDocumentBlock&& block, CBakedSchema& schema,
    const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return adopt(std::move(block), schema, options, diagnostic);
}

bool CBakedInstancesBlock::attach_asset(const CAssetId asset, const CByteConstView bytes, CBakedSchema& schema,
    const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return attach(asset, bytes, schema, options, diagnostic);
}

bool CBakedInstancesBlock::adopt_local(CBakedDocumentBlock&& block, CLiveSchema& schema,
    const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return adopt(std::move(block), schema, options, diagnostic);
}

bool CBakedInstancesBlock::attach_asset(const CAssetId asset, const CByteConstView bytes, CLiveSchema& schema,
    const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept
{
    return attach(asset, bytes, schema, options, diagnostic);
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

void CBakedInstancesBlock::clear() noexcept
{
    m_role.clear();
    m_local.deallocate();
    m_bytes = {};
    m_asset = {};
    m_backing = EEntityBacking::none;
}

bool CBakedInstancesBlock::release_local(CBakedDocumentBlock& output) noexcept
{
    if ((m_backing != EEntityBacking::local) || (output.buffer_capacity() != 0u)) return false;
    m_role.clear();
    output = std::move(m_local);
    m_bytes = {};
    m_asset = {};
    m_backing = EEntityBacking::none;
    return true;
}

}   // namespace schema
