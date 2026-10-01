
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Non-owning baked bulk role with explicit schema and payload associations.

#pragma once

#ifndef SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED
#define SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED

#include "schema/schema_wrappers.hpp"
#include "containers/ByteBuffers.hpp"

namespace schema
{

enum class EBulkLoadReason : std::uint8_t
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
    allocation_failed
};

struct SBulkDiagnostic
{
    EBulkLoadReason reason{ EBulkLoadReason::none };
    CBulkHandle occurrence;
};

struct SBulkEntryView
{
    CSchemaIndex type;
    std::uint32_t count{}, offset{};
    std::uint64_t stride{}, byte_count{};
    const std::uint8_t* bytes{ nullptr }; //  Null for zero-byte records.
};

class CBakedBulkData
{
public:
    CBakedBulkData() noexcept = default;
    CBakedBulkData(const CBakedBulkData&) = delete;
    CBakedBulkData& operator=(const CBakedBulkData&) = delete;
    CBakedBulkData(CBakedBulkData&&) = delete;
    CBakedBulkData& operator=(CBakedBulkData&&) = delete;
    ~CBakedBulkData() noexcept = default;

    //  Attach only an empty wrapper. Both forms borrow their document bytes;
    //  the mutable form permits reserved locator publication on materialisation.
    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool set_document(const CMutableBakedDocument& document) noexcept;
    [[nodiscard]] bool bind_schema(CBakedSchema& schema) noexcept;
    [[nodiscard]] bool bind_schema(CLiveSchema& schema) noexcept;

    //  The explicit supplied path admits a canonical empty view for a document
    //  containing only zero-byte records. Nonempty views require 128-byte base
    //  alignment. The materialised owner is returned separately and must be
    //  unallocated on entry; neither backing allocation belongs to this wrapper.
    [[nodiscard]] bool load_supplied(const CByteConstView& payload, const bool compare_embedded, SBulkDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(CByteBuffer& returned_owner, SBulkDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool promote(class CLiveBulkData& destination, CBakedSchema& schema, SBulkDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool promote(class CLiveBulkData& destination, CLiveSchema& schema, SBulkDiagnostic& diagnostic) const noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable(); }
    [[nodiscard]] CBulkDocumentQuery document_query() const noexcept;
    [[nodiscard]] CBulkHandle data_root() const noexcept;
    [[nodiscard]] CBulkHandle find_entry(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] bool entry(const CBulkHandle handle, SBulkEntryView& result) const noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload : CByteConstView{}; }
    void clear() noexcept;

private:
    friend class CLiveBulkData;
    void take_from(CBakedBulkData& source) noexcept;
    struct SRecord
    {
        CBakedValueIndex entry, data, offset_node, valid_node;
        CSchemaIndex type;
        std::uint32_t count{}, offset{}, extent{};
        std::uint32_t stride{}, alignment{};
        bool prior_valid{};
    };

    [[nodiscard]] bool fail(SBulkDiagnostic& diagnostic, const EBulkLoadReason reason, const CBakedValueIndex occurrence) const noexcept;
    void clear_loaded() noexcept;
    [[nodiscard]] bool plan(const bool supplied, const std::size_t payload_size,
        TPodVector<SRecord>& records, std::size_t& total_size, SBulkDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool compare_embedded(const TPodVector<SRecord>& records, const CByteConstView& payload, SBulkDiagnostic& diagnostic) const noexcept;

    CBakedDocument m_document;
    CMutableBakedDocument m_mutable;
    CSchemaBinding m_binding;
    CByteConstView m_payload;
    TPodVector<SRecord> m_records;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_BAKED_BULK_DATA_HPP_INCLUDED
