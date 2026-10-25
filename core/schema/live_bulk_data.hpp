
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_bulk_data.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Owned live bulk data document and aligned working payload.

//  Public entry point for owned bulk data. The baked-role include also supplies
//  shared bulk data views and diagnostics; output and clearing policies are included.

#pragma once

#ifndef SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED
#define SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED

#include "schema/baked_bulk_data.hpp"
#include "schema/unused_storage.hpp"

namespace schema
{

namespace detail { class CSchemaConstruction; }

class CBakedDataBlock;
struct SBulkDataBlockDiagnostic;
struct SDataBlockDiagnostic;

//==============================================================================
//  Mutable entry view
//==============================================================================

struct SMutableBulkDataEntryView
{
    CTypesIndex type;
    std::uint32_t count{}, offset{};
    std::uint64_t stride{}, byte_count{};
    CByteView bytes; // Canonical empty view for a zero-byte extent.
};

//==============================================================================
//  Live bulk data role
//==============================================================================

class CLiveBulkData
{
public:
    //==============================================================================
    //  Lifetime and initialisation
    //==============================================================================

    CLiveBulkData() noexcept = default;
    CLiveBulkData(const CLiveBulkData&) = delete;
    CLiveBulkData& operator=(const CLiveBulkData&) = delete;
    CLiveBulkData(CLiveBulkData&&) = delete;
    CLiveBulkData& operator=(CLiveBulkData&&) = delete;
    ~CLiveBulkData() noexcept = default;

    [[nodiscard]] bool initialise(CBakedTypes& schema, const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool initialise(CLiveTypes& schema, const std::size_t initial_node_capacity = 0u) noexcept;

    //  Switch only the schema association; all document and payload identities survive.
    [[nodiscard]] bool rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    void clear() noexcept;

    //==============================================================================
    //  Queries and payload access
    //==============================================================================

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable(); }
    [[nodiscard]] CBulkDataDocumentQuery document_query() const noexcept;
    [[nodiscard]] CBulkDataHandle data_root() const noexcept;
    [[nodiscard]] CBulkDataHandle find_entry(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] bool entry(const CBulkDataHandle handle, SBulkDataEntryView& result) const noexcept;
    [[nodiscard]] bool mutable_entry(const CBulkDataHandle handle, SMutableBulkDataEntryView& result) noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload.const_view() : CByteConstView{}; }
    [[nodiscard]] bool clear_unused_storage(const EUnusedBits bits = EUnusedBits::preserve) noexcept;

    //==============================================================================
    //  Creation and editing
    //==============================================================================

    [[nodiscard]] CBulkDataHandle create_records(const CStringView& type, const CStringView& name,
        const CBulkDataDocumentQuery& source_query, const CBulkDataHandle records_array, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CBulkDataHandle capture(const CStringView& type, const CStringView& name,
        const CByteConstView& source, const std::uint32_t count, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CBulkDataHandle create_unpopulated(const CStringView& type, const CStringView& name,
        const std::uint32_t count, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rename_entry(const CBulkDataHandle handle, const CStringView& name) noexcept;
    [[nodiscard]] bool erase_entry(const CBulkDataHandle handle) noexcept;

    //==============================================================================
    //  Output preparation and baking
    //==============================================================================

    //  Complete contiguous persistence output; destination must be empty.
    [[nodiscard]] bool prepare_output(CLiveDocument& document, CByteBuffer& payload, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool bake(CBakedDataBlock& destination, SBulkDataBlockDiagnostic& diagnostic) const noexcept;

private:

    //==============================================================================
    //  Private records and operations
    //==============================================================================

    friend class CBakedBulkData;
    friend class CSchema;
    friend class detail::CSchemaConstruction;

    struct SRecord
    {
        CNodeKey entry;
        CTypesIndex type;
        std::uint32_t count{}, offset{}, extent{};
    };

    [[nodiscard]] bool initialise_document(const std::size_t initial_node_capacity) noexcept;
    [[nodiscard]] static CNodeKey make_locator(CLiveDocument& document, const std::uint32_t offset,
        const std::uint32_t count, const std::uint32_t extent) noexcept;
    template<class TSchema>
    [[nodiscard]] bool switch_to(TSchema& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    void disable() noexcept;
    [[nodiscard]] bool record_index(const CBulkDataHandle handle, std::size_t& index) const noexcept;
    [[nodiscard]] bool observe(const SRecord& record, SBulkDataEntryView& result) const noexcept;
    [[nodiscard]] CBulkDataHandle store(const CStringView& type, const CStringView& name,
        const CByteConstView& source, const std::uint32_t count, const bool unpopulated,
        const bool replace, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool promote_from(const CBakedBulkData& source, SBulkDataDiagnostic& diagnostic) noexcept;
    void take_from(CLiveBulkData& source) noexcept;
    [[nodiscard]] bool prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
        SBulkDataDiagnostic& diagnostic, SDataBlockDiagnostic* const block_diagnostic = nullptr) const noexcept;

    //==============================================================================
    //  Owned state
    //==============================================================================

    CLiveDocument m_document;
    CByteBuffer m_payload;
    CTypesBinding m_binding;
    //  Strictly increasing entry keys; see find_live_record().
    TPodVector<SRecord> m_records;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED
