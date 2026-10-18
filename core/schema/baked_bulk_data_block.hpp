
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data_block.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Schema-independent bulk data document and binary storage.

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

    //  Validate the persistence envelope before taking or borrowing storage.
    //  Typed interpretation belongs to a separately attached data role.
    [[nodiscard]] bool adopt_local(CBakedDocumentBlock&& block, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    //  The caller keeps this asset range alive; attachment acquires no reference.
    [[nodiscard]] bool attach_asset(const CAssetId asset, const CByteConstView bytes, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    //  Construct independent complete storage from sufficient description values.
    //  Requires an empty destination; failure preserves source and destination.
    [[nodiscard]] bool materialise(const CLiveDocument& description, CBakedSchema& schema, const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(const CLiveDocument& description, CLiveSchema& schema, const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(const CBakedDocument& description, CBakedSchema& schema, const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(const CBakedDocument& description, CLiveSchema& schema, const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) noexcept;

    //  Remove retained values into an independent block, preserving every payload byte.
    //  Requires a loaded source and empty destination; failure preserves both.
    [[nodiscard]] bool strip_to(CBakedBulkDataBlock& destination, CBakedSchema& schema, SBulkDataBlockDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool strip_to(CBakedBulkDataBlock& destination, CLiveSchema& schema, SBulkDataBlockDiagnostic& diagnostic) const noexcept;

    //  Release/forget backing. All borrowing data roles must be quiescent.
    void clear() noexcept;
    [[nodiscard]] bool release_local(CBakedDocumentBlock& output) noexcept;

    [[nodiscard]] const CBakedDocument& document() const noexcept { return m_document; }
    [[nodiscard]] CByteConstView payload_view() const noexcept { return m_payload; }
    [[nodiscard]] EEntityBacking backing() const noexcept { return m_backing; }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_backing != EEntityBacking::none; }
    [[nodiscard]] CAssetId asset_id() const noexcept { return m_asset; }

    //  The complete persisted range, excluding spare allocation capacity.
    [[nodiscard]] CByteConstView file_bytes() const noexcept { return m_bytes; }

private:
    CBakedDocumentBlock m_local;
    CByteConstView m_bytes;
    CAssetId m_asset;
    EEntityBacking m_backing{ EEntityBacking::none };
    CBakedDocument m_document;
    CByteConstView m_payload;
};

}   // namespace schema

#endif // SCHEMA_BAKED_BULK_DATA_BLOCK_HPP_INCLUDED
