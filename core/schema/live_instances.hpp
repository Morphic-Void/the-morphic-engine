
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Owned live instance hierarchy and independent complete snapshots.

#pragma once

#ifndef SCHEMA_LIVE_INSTANCES_HPP_INCLUDED
#define SCHEMA_LIVE_INSTANCES_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/data_output.hpp"
#include "schema/unused_storage.hpp"

namespace schema
{

enum class EInstanceSelectionStepKind : std::uint8_t { member, element };

struct SInstanceSelectionStep
{
    EInstanceSelectionStepKind kind{ EInstanceSelectionStepKind::member };
    CStringView member;
    std::uint32_t element{};
};

struct SMutableInstanceEntryView
{
    CSchemaIndex type;
    CInstanceHandle parent;
    CInstanceHandle declaration;
    std::uint32_t offset{};
    std::uint64_t byte_count{};
    CByteView bytes; // Canonical empty view for a zero-byte snapshot.
};

class CLiveInstances
{
public:
    CLiveInstances() noexcept = default;
    CLiveInstances(const CLiveInstances&) = delete;
    CLiveInstances& operator=(const CLiveInstances&) = delete;
    CLiveInstances(CLiveInstances&&) = delete;
    CLiveInstances& operator=(CLiveInstances&&) = delete;
    ~CLiveInstances() noexcept = default;

    [[nodiscard]] bool initialise(CBakedSchema& schema, const std::size_t capacity = 0u) noexcept;
    [[nodiscard]] bool initialise(CLiveSchema& schema, const std::size_t capacity = 0u) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable(); }
    [[nodiscard]] CInstanceDocumentQuery document_query() const noexcept;
    [[nodiscard]] CInstanceHandle instances_root() const noexcept;
    [[nodiscard]] CInstanceHandle find_base(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle find_specialisation(const CInstanceHandle parent, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle first_specialisation(const CInstanceHandle parent) const noexcept;
    [[nodiscard]] CInstanceHandle next_specialisation(const CInstanceHandle current) const noexcept;
    [[nodiscard]] CInstanceHandle parent_instance(const CInstanceHandle instance) const noexcept;
    [[nodiscard]] bool entry(const CInstanceHandle instance, SInstanceEntryView& result) const noexcept;
    [[nodiscard]] bool mutable_entry(const CInstanceHandle instance, SMutableInstanceEntryView& result) noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload.const_view() : CByteConstView{}; }
    [[nodiscard]] bool clear_unused_storage(const EUnusedBits bits = EUnusedBits::preserve) noexcept;

    //  Rebuild declarations from snapshots; success invalidates document handles, not payload views.
    [[nodiscard]] bool reconcile(SInstanceDiagnostic& diagnostic) noexcept;

    [[nodiscard]] CInstanceHandle create_base(const CStringView& type, const CStringView& name,
        const CInstanceDocumentQuery& source, const CInstanceHandle declaration, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CInstanceHandle create_specialisation(const CInstanceHandle parent, const CStringView& name,
        const CInstanceDocumentQuery& source, const CInstanceHandle declaration, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CInstanceHandle capture_base(const CStringView& type, const CStringView& name,
        const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool capture_specialisation(const CInstanceHandle instance,
        const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool set_selection(const CInstanceHandle instance, const SInstanceSelectionStep* const steps,
        const std::size_t step_count, const CInstanceDocumentQuery& source, const CInstanceHandle value,
        SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool remove_selection(const CInstanceHandle instance, const SInstanceSelectionStep* const steps,
        const std::size_t step_count, SInstanceDiagnostic& diagnostic) noexcept;

    [[nodiscard]] bool prepare_output(CLiveDocument& document, CByteBuffer& payload,
        CBakedSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool prepare_output(CLiveDocument& document, CByteBuffer& payload,
        CLiveSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool demote(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
        CBakedSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool demote(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
        CLiveSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept;

private:
    friend class CBakedInstances;
    static constexpr std::uint32_t k_no_parent = UINT32_MAX;
    struct SRecord
    {
        CNodeKey entry, declaration, group;
        CSchemaIndex type;
        std::uint32_t parent{ k_no_parent };
        std::uint32_t offset{}, extent{};
    };

    [[nodiscard]] bool initialise_document(const std::size_t capacity) noexcept;
    [[nodiscard]] bool record_index(const CInstanceHandle instance, std::uint32_t& index) const noexcept;
    [[nodiscard]] bool promote_from(const CBakedInstances& source, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CInstanceHandle append_instance(const CSchemaIndex type, const std::uint32_t parent,
        const CStringView& type_name, const CStringView& name, const detail::CDocumentRead& source,
        const detail::SOccurrence declaration, const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool edit_existing(const std::uint32_t index, const CNodeKey selection,
        const CByteConstView& complete, const bool full_declaration, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CNodeKey output_declaration(CLiveDocument& target, const SRecord& record,
        const CResolvedSchema& schema, const CSchemaIndex destination_type,
        EInstanceLoadReason& reason) const noexcept;
    [[nodiscard]] bool prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
        const CResolvedSchema& destination_schema, const EDataOutputForm form,
        SInstanceDiagnostic& diagnostic) const noexcept;
    template <class TSchema>
    [[nodiscard]] bool demote_to(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
        TSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept;
    void take_from(CLiveInstances& source) noexcept;
    void disable() noexcept;

    CLiveDocument m_document;
    CByteBuffer m_payload;
    CSchemaBinding m_binding;
    //  Strictly increasing entry keys; see find_live_record().
    TPodVector<SRecord> m_records;
    bool m_loaded{};
};

}   // namespace schema

#endif // SCHEMA_LIVE_INSTANCES_HPP_INCLUDED
