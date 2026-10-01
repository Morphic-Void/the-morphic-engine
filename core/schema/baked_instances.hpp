
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instances.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Non-owning baked instance role with independent specialisation snapshots.

#pragma once

#ifndef SCHEMA_BAKED_INSTANCES_HPP_INCLUDED
#define SCHEMA_BAKED_INSTANCES_HPP_INCLUDED

#include "schema/schema_wrappers.hpp"
#include "containers/ByteBuffers.hpp"

namespace schema
{

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
    allocation_failed
};

struct SInstanceDiagnostic
{
    EInstanceLoadReason reason{ EInstanceLoadReason::none };
    CInstanceHandle occurrence;
};

struct SInstanceEntryView
{
    CSchemaIndex type;
    CInstanceHandle parent;
    CInstanceHandle declaration;
    std::uint32_t offset{};
    std::uint64_t byte_count{};
    const std::uint8_t* bytes{ nullptr }; //  Null for a zero-byte type.
};

class CBakedInstances
{
public:
    CBakedInstances() noexcept = default;
    CBakedInstances(const CBakedInstances&) = delete;
    CBakedInstances& operator=(const CBakedInstances&) = delete;
    CBakedInstances(CBakedInstances&&) = delete;
    CBakedInstances& operator=(CBakedInstances&&) = delete;
    ~CBakedInstances() noexcept = default;

    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool set_document(const CMutableBakedDocument& document) noexcept;
    [[nodiscard]] bool bind_schema(CBakedSchema& schema) noexcept;
    [[nodiscard]] bool bind_schema(CLiveSchema& schema) noexcept;

    //  Supplied payloads are borrowed. Materialisation returns the owner to the
    //  caller and borrows its stable allocation. An unallocated output is required.
    [[nodiscard]] bool load_supplied(const CByteConstView& payload, const bool compare_embedded,
        SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(CByteBuffer& returned_owner, SInstanceDiagnostic& diagnostic) noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
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
    void clear() noexcept;

private:
    static constexpr std::uint32_t k_no_parent = UINT32_MAX;

    struct SRecord
    {
        CBakedValueIndex entry, declaration, offset_node, valid_node;
        CSchemaIndex type;
        std::uint32_t parent{ k_no_parent };
        std::uint32_t offset{}, scratch_offset{}, extent{};
        bool prior_valid{};
    };

    struct SFrame
    {
        CBakedValueIndex next;
        std::uint32_t parent{ k_no_parent };
    };

    [[nodiscard]] bool fail(SInstanceDiagnostic& diagnostic, const EInstanceLoadReason reason,
        const CBakedValueIndex occurrence) const noexcept;
    void clear_loaded() noexcept;
    [[nodiscard]] bool record_index(const CInstanceHandle handle, std::uint32_t& result) const noexcept;
    [[nodiscard]] bool plan(const bool supplied, const std::size_t supplied_size,
        TPodVector<SRecord>& records, std::size_t& payload_size, std::size_t& scratch_size,
        SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool compare_embedded(const TPodVector<SRecord>& records, const CByteConstView& payload,
        const std::size_t scratch_size, SInstanceDiagnostic& diagnostic) const noexcept;

    CBakedDocument m_document;
    CMutableBakedDocument m_mutable;
    CSchemaBinding m_binding;
    CByteConstView m_payload;
    TPodVector<SRecord> m_records;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_BAKED_INSTANCES_HPP_INCLUDED
