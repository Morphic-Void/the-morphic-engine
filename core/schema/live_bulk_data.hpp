
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_bulk_data.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Owned live bulk document and aligned working payload.

#pragma once

#ifndef SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED
#define SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED

#include "schema/baked_bulk_data.hpp"

namespace schema
{

struct SMutableBulkEntryView
{
    CSchemaIndex type;
    std::uint32_t count{}, offset{};
    std::uint64_t stride{}, byte_count{};
    CByteView bytes; // Canonical empty view for a zero-byte extent.
};

class CLiveBulkData
{
public:
    CLiveBulkData() noexcept = default;
    CLiveBulkData(const CLiveBulkData&) = delete;
    CLiveBulkData& operator=(const CLiveBulkData&) = delete;
    CLiveBulkData(CLiveBulkData&&) = delete;
    CLiveBulkData& operator=(CLiveBulkData&&) = delete;
    ~CLiveBulkData() noexcept = default;

    [[nodiscard]] bool initialise(CBakedSchema& schema, const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool initialise(CLiveSchema& schema, const std::size_t initial_node_capacity = 0u) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable(); }
    [[nodiscard]] CBulkDocumentQuery document_query() const noexcept;
    [[nodiscard]] CBulkHandle data_root() const noexcept;
    [[nodiscard]] CBulkHandle find_entry(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] bool entry(const CBulkHandle handle, SBulkEntryView& result) const noexcept;
    [[nodiscard]] bool mutable_entry(const CBulkHandle handle, SMutableBulkEntryView& result) noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload.const_view() : CByteConstView{}; }

    [[nodiscard]] CBulkHandle create_records(const CStringView& type, const CStringView& name,
        const CBulkDocumentQuery& source_query, const CBulkHandle records_array, SBulkDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CBulkHandle capture(const CStringView& type, const CStringView& name,
        const CByteConstView& source, const std::uint32_t count, SBulkDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CBulkHandle create_unpopulated(const CStringView& type, const CStringView& name,
        const std::uint32_t count, SBulkDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rename_entry(const CBulkHandle handle, const CStringView& name) noexcept;
    [[nodiscard]] bool erase_entry(const CBulkHandle handle) noexcept;

private:
    friend class CBakedBulkData;

    struct SRecord
    {
        CNodeKey entry, group;
        std::uint32_t count{}, offset{}, extent{};
    };

    [[nodiscard]] bool initialise_document(const std::size_t initial_node_capacity) noexcept;
    [[nodiscard]] bool record_index(const CBulkHandle handle, std::size_t& index) const noexcept;
    [[nodiscard]] bool observe(const SRecord& record, SBulkEntryView& result) const noexcept;
    [[nodiscard]] CBulkHandle store(const CStringView& type, const CStringView& name,
        const CByteConstView& source, const std::uint32_t count, const bool unpopulated,
        const bool replace, SBulkDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool promote_from(const CBakedBulkData& source, SBulkDiagnostic& diagnostic) noexcept;
    void take_from(CLiveBulkData& source) noexcept;
    void disable() noexcept;

    CLiveDocument m_document;
    CByteBuffer m_payload;
    CSchemaBinding m_binding;
    TPodVector<SRecord> m_records;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED
