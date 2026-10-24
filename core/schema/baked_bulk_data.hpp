
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

#include "schema/schema_wrappers.hpp"
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
    embedded_mismatch,
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
    CSchemaIndex type;
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

    //  Attach only an empty wrapper. Both forms borrow their document bytes;
    //  the mutable form permits reserved locator publication on materialisation.
    void clear() noexcept;

    //  Borrow supplied block bytes, or construct independent working bytes.
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CBakedSchema& schema,
        CByteBuffer& returned_owner, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CLiveSchema& schema,
        CByteBuffer& returned_owner, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;

    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool set_document(const CMutableBakedDocument& document) noexcept;

    //  Attachment publishes a complete association. Overloads distinguish a
    //  supplied (possibly zero-byte) binary from construction of a new binary.
    //  Supplied bytes are borrowed. Construction returns an owner to the caller
    //  and borrows its stable allocation; the output must be unallocated.
    //  Stripped descriptions require the supplied-payload overload.
    [[nodiscard]] bool attach_schema(CBakedSchema& schema, const CByteConstView& payload, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_schema(CBakedSchema& schema, CByteBuffer& returned_owner, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_schema(CLiveSchema& schema, const CByteConstView& payload, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_schema(CLiveSchema& schema, CByteBuffer& returned_owner, SBulkDataDiagnostic& diagnostic) noexcept;

    //  Switch only the schema association; all document and payload identities survive.
    [[nodiscard]] bool switch_schema(CBakedSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool switch_schema(CLiveSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Promotion
    //==============================================================================

    //  The explicit supplied path admits a canonical empty view for a document
    //  containing only zero-byte records. Nonempty views require 128-byte base
    //  alignment. The materialised owner is returned separately and must be
    //  unallocated on entry; neither backing allocation belongs to this wrapper.
    //  Stripped documents require load_supplied; there are no embedded values to compare.
    [[nodiscard]] bool promote(class CLiveBulkData& destination, CBakedSchema& schema, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool promote(class CLiveBulkData& destination, CLiveSchema& schema, SBulkDataDiagnostic& diagnostic) const noexcept;

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

    struct SRecord
    {
        CBakedValueIndex entry, data, offset_node, valid_node;
        CSchemaIndex type;
        std::uint32_t count{}, offset{}, extent{};
        std::uint32_t stride{};
        bool prior_valid{};
    };

    [[nodiscard]] bool fail(SBulkDataDiagnostic& diagnostic, const EBulkDataLoadReason reason, const CBakedValueIndex occurrence) const noexcept;
    void clear_loaded() noexcept;
    void take_from(CBakedBulkData& source) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_block_to(const CBakedDataBlock& block, TSchema& schema,
        CByteBuffer& returned_owner, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_to(TSchema& schema, CByteBuffer* const returned_owner, const CByteConstView& payload,
        const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool switch_to(TSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool plan(const bool supplied, const std::size_t payload_size,
        TPodVector<SRecord>& records, std::size_t& total_size, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool compare_embedded(const TPodVector<SRecord>& records, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool load_supplied(const CByteConstView& payload, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(CByteBuffer& returned_owner, SBulkDataDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Borrowed state and record index
    //==============================================================================

    CBakedDocumentBlock m_working_document;
    CBakedDocument m_document;
    CMutableBakedDocument m_mutable;
    CSchemaBinding m_binding;
    CByteConstView m_payload;
    TPodVector<SRecord> m_records;
    CBakedRecordIndex m_record_index;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED
