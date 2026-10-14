
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data_block.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Completed bulk data file with a contained specialised role.

#pragma once

#ifndef SCHEMA_BAKED_BULK_DATA_BLOCK_HPP_INCLUDED
#define SCHEMA_BAKED_BULK_DATA_BLOCK_HPP_INCLUDED

#include "schema/data_block.hpp"
#include "schema/data_output.hpp"
#include "assets/asset_repository.hpp"

namespace schema
{

class CBakedBulkDataBlock
{
public:
    CBakedBulkDataBlock() noexcept = default;
    CBakedBulkDataBlock(const CBakedBulkDataBlock&) = delete;
    CBakedBulkDataBlock& operator=(const CBakedBulkDataBlock&) = delete;
    CBakedBulkDataBlock(CBakedBulkDataBlock&&) = delete;
    CBakedBulkDataBlock& operator=(CBakedBulkDataBlock&&) = delete;
    ~CBakedBulkDataBlock() noexcept { clear(); }

    //  Validate before taking ownership. Failure preserves source and destination.
    [[nodiscard]] bool adopt_local(CBakedDocumentBlock&& block, CBakedSchema& schema,
        const SDataBlockLoadOptions& options, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool adopt_local(CBakedDocumentBlock&& block, CLiveSchema& schema,
        const SDataBlockLoadOptions& options, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    //  Borrow a retained asset's complete range. The caller protects its lifetime;
    //  this block neither acquires a reference nor sends disposal requests.
    [[nodiscard]] bool attach_asset(CAssetId asset, CByteConstView bytes, CBakedSchema& schema,
        const SDataBlockLoadOptions& options, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_asset(CAssetId asset, CByteConstView bytes, CLiveSchema& schema,
        const SDataBlockLoadOptions& options, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    //  Construct independent complete storage from sufficient description values.
    //  Requires an empty destination; failure preserves source and destination.
    [[nodiscard]] bool materialise(const CLiveDocument& description, CBakedSchema& schema,
        EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(const CLiveDocument& description, CLiveSchema& schema,
        EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(const CBakedDocument& description, CBakedSchema& schema,
        EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(const CBakedDocument& description, CLiveSchema& schema,
        EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    //  Remove retained values into an independent block, preserving every payload byte.
    //  Requires a loaded source and empty destination; failure preserves both.
    [[nodiscard]] bool strip_to(CBakedBulkDataBlock& destination, CBakedSchema& schema,
        SBulkDataBlockDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool strip_to(CBakedBulkDataBlock& destination, CLiveSchema& schema,
        SBulkDataBlockDiagnostic& diagnostic) const noexcept;

    //  Clear the role before releasing/forgetting backing. All users must be quiescent.
    void clear() noexcept;
    [[nodiscard]] bool release_local(CBakedDocumentBlock& output) noexcept;

    [[nodiscard]] const CBakedBulkData& bulk_data() const noexcept { return m_role; }
    [[nodiscard]] EEntityBacking backing() const noexcept { return m_backing; }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_role.loaded_ready(); }
    [[nodiscard]] CAssetId asset_id() const noexcept { return m_asset; }

    //  Remains available after schema invalidation; excludes spare capacity.
    [[nodiscard]] CByteConstView file_bytes() const noexcept { return m_bytes; }

private:
    template<class TSchema>
    [[nodiscard]] bool adopt(CBakedDocumentBlock&& block, TSchema& schema,
        const SDataBlockLoadOptions& options, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach(CAssetId asset, CByteConstView bytes, TSchema& schema,
        const SDataBlockLoadOptions& options, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    CBakedDocumentBlock m_local;
    CByteConstView m_bytes;
    CAssetId m_asset;
    EEntityBacking m_backing{ EEntityBacking::none };
    CBakedBulkData m_role;
};

}   // namespace schema

#endif // SCHEMA_BAKED_BULK_DATA_BLOCK_HPP_INCLUDED
