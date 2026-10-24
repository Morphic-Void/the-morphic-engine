
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instances.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Non-owning baked instance role with independent specialisation snapshots.

//  Public entry point for borrowed instance data. Includes shared schema/query types.
//  record_index.hpp supplies private storage required by the class definition.

#pragma once

#ifndef SCHEMA_BAKED_INSTANCES_HPP_INCLUDED
#define SCHEMA_BAKED_INSTANCES_HPP_INCLUDED

#include "schema/schema_wrappers.hpp"
#include "schema/record_index.hpp"
#include "containers/ByteBuffers.hpp"

namespace schema
{

//==============================================================================
//  Loading diagnostics
//==============================================================================

enum class EInstanceLoadReason : std::uint8_t
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
    invalid_declaration,
    embedded_mismatch,
    unrepresentable_value,
    allocation_failed,
    incompatible_schema,
    binary_required
};

struct SInstanceDiagnostic
{
    EInstanceLoadReason reason{ EInstanceLoadReason::none };
    CInstanceHandle occurrence;
};

//==============================================================================
//  Borrowed entry view
//==============================================================================

struct SInstanceEntryView
{
    CSchemaIndex type;
    CInstanceHandle parent;
    CInstanceHandle declaration;
    std::uint32_t offset{};
    std::uint64_t byte_count{};
    const std::uint8_t* bytes{ nullptr }; //  Null for a zero-byte type.
};

//==============================================================================
//  Baked instance role
//==============================================================================

class CBakedInstances
{
public:

    //==============================================================================
    //  Lifetime and binding
    //==============================================================================

    CBakedInstances() noexcept = default;
    CBakedInstances(const CBakedInstances&) = delete;
    CBakedInstances& operator=(const CBakedInstances&) = delete;
    CBakedInstances(CBakedInstances&&) = delete;
    CBakedInstances& operator=(CBakedInstances&&) = delete;
    ~CBakedInstances() noexcept = default;

    void clear() noexcept;

    //  Borrow supplied block bytes, or construct independent working bytes.
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CBakedSchema& schema,
        CByteBuffer& returned_owner, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CLiveSchema& schema,
        CByteBuffer& returned_owner, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;

    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool set_document(const CMutableBakedDocument& document) noexcept;

    //  Attachment publishes a complete association. Overloads distinguish a
    //  supplied (possibly zero-byte) binary from construction of a new binary.
    //  Supplied bytes are borrowed. Construction returns an owner to the caller
    //  and borrows its stable allocation; the output must be unallocated.
    //  Stripped descriptions require the supplied-payload overload.
    [[nodiscard]] bool attach_schema(CBakedSchema& schema, const CByteConstView& payload, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_schema(CBakedSchema& schema, CByteBuffer& returned_owner, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_schema(CLiveSchema& schema, const CByteConstView& payload, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_schema(CLiveSchema& schema, CByteBuffer& returned_owner, SInstanceDiagnostic& diagnostic) noexcept;

    //  Switch only the schema association; all document and payload identities survive.
    [[nodiscard]] bool switch_schema(CBakedSchema& schema, const ESchemaSwitchMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool switch_schema(CLiveSchema& schema, const ESchemaSwitchMode mode, SInstanceDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Promotion
    //==============================================================================

    [[nodiscard]] bool promote(class CLiveInstances& destination, CBakedSchema& schema, SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool promote(class CLiveInstances& destination, CLiveSchema& schema, SInstanceDiagnostic& diagnostic) const noexcept;

    //==============================================================================
    //  Queries and payload access
    //==============================================================================

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool values_stripped() const noexcept;
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable(); }
    [[nodiscard]] CInstanceDocumentQuery document_query() const noexcept;
    [[nodiscard]] CInstanceHandle instances_root() const noexcept;
    [[nodiscard]] CInstanceHandle find_base(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle find_specialisation(const CInstanceHandle parent, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle first_specialisation(const CInstanceHandle parent) const noexcept;
    [[nodiscard]] CInstanceHandle next_specialisation(const CInstanceHandle current) const noexcept;
    [[nodiscard]] CInstanceHandle parent_instance(const CInstanceHandle instance) const noexcept;
    [[nodiscard]] bool entry(const CInstanceHandle handle, SInstanceEntryView& result) const noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload : CByteConstView{}; }

private:

    //==============================================================================
    //  Private planning and records
    //==============================================================================

    friend class CLiveInstances;
    static constexpr std::uint32_t k_no_parent = UINT32_MAX;

    struct SRecord
    {
        CBakedValueIndex entry, declaration, offset_node, valid_node;
        CSchemaIndex type;
        std::uint32_t parent{ k_no_parent };
        std::uint32_t offset{}, extent{};
        bool prior_valid{};
    };

    struct SFrame
    {
        CBakedValueIndex next;
        std::uint32_t parent{ k_no_parent };
    };

    [[nodiscard]] bool fail(SInstanceDiagnostic& diagnostic, const EInstanceLoadReason reason, const CBakedValueIndex occurrence) const noexcept;
    void clear_loaded() noexcept;
    void take_from(CBakedInstances& source) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_block_to(const CBakedDataBlock& block, TSchema& schema,
        CByteBuffer& returned_owner, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_to(TSchema& schema, CByteBuffer* const returned_owner, const CByteConstView& payload,
        const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool switch_to(TSchema& schema, const ESchemaSwitchMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool record_index(const CInstanceHandle handle, std::uint32_t& result) const noexcept;
    [[nodiscard]] bool plan(const bool supplied, const std::size_t supplied_size,
        TPodVector<SRecord>& records, std::size_t& payload_size,
        SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool compare_embedded(const TPodVector<SRecord>& records, const CByteConstView& payload,
        SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool load_supplied(const CByteConstView& payload, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(CByteBuffer& returned_owner, SInstanceDiagnostic& diagnostic) noexcept;

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

#endif // SCHEMA_BAKED_INSTANCES_HPP_INCLUDED
