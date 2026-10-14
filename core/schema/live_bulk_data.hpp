
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
#include "schema/data_output.hpp"
#include "schema/unused_storage.hpp"

namespace schema
{

class CBakedBulkDataBlock;
struct SBulkDataBlockDiagnostic;

//==============================================================================
//  Mutable entry view
//==============================================================================

struct SMutableBulkDataEntryView
{
    CSchemaIndex type;
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

    [[nodiscard]] bool initialise(CBakedSchema& schema, const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool initialise(CLiveSchema& schema, const std::size_t initial_node_capacity = 0u) noexcept;
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
    //  Binary reconciliation
    //==============================================================================

    //  Rebuild complete records; success invalidates document handles, not payload views.
    [[nodiscard]] bool reconcile(SBulkDataDiagnostic& diagnostic) noexcept;

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

    [[nodiscard]] bool prepare_output(CLiveDocument& document, CByteBuffer& payload,
        CBakedSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool prepare_output(CLiveDocument& document, CByteBuffer& payload,
        CLiveSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedBulkData& role,
        CBakedSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedBulkData& role,
        CLiveSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept;

    //  Complete contiguous persistence output; destination must be empty.
    [[nodiscard]] bool bake(CBakedBulkDataBlock& destination, CBakedSchema& schema,
        const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool bake(CBakedBulkDataBlock& destination, CLiveSchema& schema,
        const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) const noexcept;

private:
    //==============================================================================
    //  Private records and operations
    //==============================================================================

    friend class CBakedBulkData;

    struct SRecord
    {
        CNodeKey entry, group;
        CSchemaIndex type;
        std::uint32_t count{}, offset{}, extent{};
    };

    [[nodiscard]] bool initialise_document(const std::size_t initial_node_capacity) noexcept;
    [[nodiscard]] bool record_index(const CBulkDataHandle handle, std::size_t& index) const noexcept;
    [[nodiscard]] bool observe(const SRecord& record, SBulkDataEntryView& result) const noexcept;
    [[nodiscard]] CBulkDataHandle store(const CStringView& type, const CStringView& name,
        const CByteConstView& source, const std::uint32_t count, const bool unpopulated,
        const bool replace, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool promote_from(const CBakedBulkData& source, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
        const CResolvedSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept;
    template<class TSchema>
    [[nodiscard]] bool bake_to(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedBulkData& role,
        TSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept;
    void take_from(CLiveBulkData& source) noexcept;
    void disable() noexcept;

    //==============================================================================
    //  Owned state
    //==============================================================================

    CLiveDocument m_document;
    CByteBuffer m_payload;
    CSchemaBinding m_binding;
    //  Strictly increasing entry keys; see find_live_record().
    TPodVector<SRecord> m_records;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_LIVE_BULK_DATA_HPP_INCLUDED
