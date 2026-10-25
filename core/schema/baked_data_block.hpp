
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_data_block.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    24 Oct 26
//
//  Common .bsd storage, independent of typed schema/data bindings.

#pragma once

#ifndef SCHEMA_BAKED_DATA_BLOCK_HPP_INCLUDED
#define SCHEMA_BAKED_DATA_BLOCK_HPP_INCLUDED

#include "schema/data_block.hpp"
#include "assets/asset_repository.hpp"

namespace schema
{

//  Null omits a domain; each present section requires its matching pointer.
//  A pointer to an empty view explicitly supplies zero bytes.
//  The views and their bytes are borrowed for the duration of the operation.
struct SDataBlockPayloads
{
    const CByteConstView* instances{ nullptr };
    const CByteConstView* bulk_data{ nullptr };
};

class CBakedDataBlock
{
public:
    CBakedDataBlock() noexcept = default;
    CBakedDataBlock(const CBakedDataBlock&) = delete;
    CBakedDataBlock& operator=(const CBakedDataBlock&) = delete;
    CBakedDataBlock(CBakedDataBlock&&) = delete;
    CBakedDataBlock& operator=(CBakedDataBlock&&) = delete;
    ~CBakedDataBlock() noexcept { clear(); }

    [[nodiscard]] bool adopt_local(CBakedDocumentBlock&& block, SDataBlockDiagnostic& diagnostic) noexcept;

    //  Acquires no asset reference; the caller preserves the borrowed range.
    [[nodiscard]] bool attach_asset(const CAssetId asset, const CByteConstView bytes, SDataBlockDiagnostic& diagnostic) noexcept;

    //  Schema-independent assembly. Empty destination; failure preserves inputs.
    [[nodiscard]] bool assemble(const CLiveDocument& description, const SDataBlockPayloads& payloads, SDataBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool assemble(const CBakedDocument& description, const SDataBlockPayloads& payloads, SDataBlockDiagnostic& diagnostic) noexcept;

    void clear() noexcept;
    [[nodiscard]] bool release_local(CBakedDocumentBlock& output) noexcept;

    [[nodiscard]] const CBakedDocument& document() const noexcept { return m_document; }
    [[nodiscard]] bool has_types() const noexcept;
    [[nodiscard]] bool has_instances() const noexcept;
    [[nodiscard]] bool has_bulk_data() const noexcept;
    [[nodiscard]] bool instance_binary_available() const noexcept { return m_has_instances; }
    [[nodiscard]] bool bulk_binary_available() const noexcept { return m_has_bulk_data; }
    [[nodiscard]] CByteConstView instance_payload_view() const noexcept { return m_instances; }
    [[nodiscard]] CByteConstView bulk_payload_view() const noexcept { return m_bulk_data; }
    [[nodiscard]] EEntityBacking backing() const noexcept { return m_backing; }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_backing != EEntityBacking::none; }
    [[nodiscard]] CAssetId asset_id() const noexcept { return m_asset; }
    [[nodiscard]] CByteConstView file_bytes() const noexcept { return m_bytes; }

private:
    CBakedDocumentBlock m_local;
    CByteConstView m_bytes, m_instances, m_bulk_data;
    CAssetId m_asset;
    EEntityBacking m_backing{ EEntityBacking::none };
    CBakedDocument m_document;
    bool m_has_instances{}, m_has_bulk_data{};
};

}   // namespace schema

#endif // SCHEMA_BAKED_DATA_BLOCK_HPP_INCLUDED

