
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

#include "schema/types_wrappers.hpp"
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
    inheritance_mismatch,
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
    CTypesIndex type;
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

    //  Runtime descriptions and binaries are complete and immutable. A supplied
    //  zero-byte payload is explicit. Embedded types validate the original bytes.
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CBakedTypes& types, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_block(const class CBakedDataBlock& block, CLiveTypes& types, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool attach_types(CBakedTypes& types, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_types(CLiveTypes& types, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept;

    //  Without replacement storage, inherited values must already match target defaults.
    //  Replacement overloads require an empty owner, publish independently rebuilt
    //  bytes and borrow that owner afterwards. The source description is unchanged.
    [[nodiscard]] bool rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rebind_types(CBakedTypes& types, CByteBuffer& replacement,
        const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rebind_types(CLiveTypes& types, CByteBuffer& replacement,
        const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Promotion
    //==============================================================================

    [[nodiscard]] bool promote(class CLiveInstances& destination, SInstanceDiagnostic& diagnostic) const noexcept;

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
    friend class CSchema;
    static constexpr std::uint32_t k_no_parent = UINT32_MAX;

    struct SRecord
    {
        CBakedValueIndex entry, declaration;
        CTypesIndex type;
        std::uint32_t parent{ k_no_parent };
        std::uint32_t offset{}, extent{};
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
    [[nodiscard]] bool attach_block_to(const CBakedDataBlock& block, TSchema& types, SInstanceDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach_to(TSchema& types, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool switch_to(TSchema& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic,
        CByteBuffer* replacement = nullptr, const bool source_validated = false) noexcept;
    [[nodiscard]] bool record_index(const CInstanceHandle handle, std::uint32_t& result) const noexcept;
    [[nodiscard]] bool plan(const std::size_t supplied_size,
        TPodVector<SRecord>& records, std::size_t& payload_size,
        SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool check_inheritance(const TPodVector<SRecord>& records, const CByteConstView& payload,
        SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool load_supplied(const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept;

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

#endif // SCHEMA_BAKED_INSTANCES_HPP_INCLUDED
