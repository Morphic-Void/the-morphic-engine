
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Non-owning baked bulk data role with explicit schema and payload associations.

//  Public entry point for borrowed bulk data. Includes shared schema/query types.
//  record_index.hpp supplies private storage required by the class definition.

#pragma once

#ifndef SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED
#define SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED

#include "schema/types_wrappers.hpp"
#include "schema/record_index.hpp"
#include "containers/ByteBuffers.hpp"

namespace schema
{

//==============================================================================
//  Loading diagnostics
//==============================================================================

enum class EBulkDataLoadReason : std::uint8_t
{
    none = 0u,
    invalid_input,
    missing_property,
    unknown_property,
    unknown_type,
    invalid_count,
    invalid_locator,
    invalid_range,
    overlap,
    incomplete_record,
    incompatible_schema,
    unrepresentable_value,
    allocation_failed,
    binary_required
};

struct SBulkDataDiagnostic
{
    EBulkDataLoadReason reason{ EBulkDataLoadReason::none };
    CBulkDataHandle occurrence;
};

//==============================================================================
//  Borrowed entry view
//==============================================================================

struct SBulkDataEntryView
{
    CTypesIndex type;
    std::uint32_t count{}, offset{};
    std::uint64_t stride{}, byte_count{};
    const std::uint8_t* bytes{ nullptr }; //  Null for zero-byte records.
};

//==============================================================================
//  Baked bulk data role
//==============================================================================

class CBakedBulkData
{
public:

    //==============================================================================
    //  Lifetime and binding
    //==============================================================================

    CBakedBulkData() noexcept = default;
    CBakedBulkData(const CBakedBulkData&) = delete;
    CBakedBulkData& operator=(const CBakedBulkData&) = delete;
    CBakedBulkData(CBakedBulkData&&) = delete;
    CBakedBulkData& operator=(CBakedBulkData&&) = delete;
    ~CBakedBulkData() noexcept = default;

    void clear() noexcept;

    //  Runtime descriptions and binaries are complete and immutable. A supplied
    //  zero-byte payload is explicit. Embedded types validate the original bytes.
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CBakedTypes& types, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CLiveTypes& types, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool attach_types(CBakedTypes& types, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_types(CLiveTypes& types, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept;

    //  Switch only the schema association; all document and payload identities survive.
    [[nodiscard]] bool rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Promotion
    //==============================================================================

    [[nodiscard]] bool promote(class CLiveBulkData& destination, SBulkDataDiagnostic& diagnostic) const noexcept;

    //==============================================================================
    //  Queries and payload access
    //==============================================================================

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool values_stripped() const noexcept;
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable(); }
    [[nodiscard]] CBulkDataDocumentQuery document_query() const noexcept;
    [[nodiscard]] CBulkDataHandle data_root() const noexcept;
    [[nodiscard]] CBulkDataHandle find_entry(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] bool entry(const CBulkDataHandle handle, SBulkDataEntryView& result) const noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload : CByteConstView{}; }

private:

    //==============================================================================
    //  Private planning and records
    //==============================================================================

    friend class CLiveBulkData;
    friend class CSchema;

    struct SRecord
    {
        CBakedValueIndex entry;
        CTypesIndex type;
        std::uint32_t count{}, offset{}, extent{};
        std::uint32_t stride{};
    };

    [[nodiscard]] bool fail(SBulkDataDiagnostic& diagnostic, const EBulkDataLoadReason reason, const CBakedValueIndex occurrence) const noexcept;
    void clear_loaded() noexcept;
    void take_from(CBakedBulkData& source) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_block_to(const CBakedDataBlock& block, TSchema& types, SBulkDataDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_to(TSchema& types, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool switch_to(TSchema& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool plan(const std::size_t payload_size,
        TPodVector<SRecord>& records, std::size_t& total_size, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool load_supplied(const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Borrowed state and record index
    //==============================================================================

    CBakedDocument m_document;
    CTypesBinding m_binding;
    CByteConstView m_payload;
    TPodVector<SRecord> m_records;
    CBakedRecordIndex m_record_index;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED
