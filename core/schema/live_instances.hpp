
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Owned live instance hierarchy and independent complete snapshots.

//  Public entry point for owned instances and selection editing. The baked-role
//  include supplies shared instance views and diagnostics, used by both roles.

#pragma once

#ifndef SCHEMA_LIVE_INSTANCES_HPP_INCLUDED
#define SCHEMA_LIVE_INSTANCES_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/unused_storage.hpp"

namespace schema
{

namespace detail { class CSchemaConstruction; }

enum class EInstanceDescendantPolicy : std::uint8_t { preserve = 0u, propagate };

class CInstanceEditToken
{
public:
    CInstanceEditToken() noexcept = default;
private:
    friend class CLiveInstances;
    const void* m_owner{};
    std::uint64_t m_generation{};
};

class CBakedDataBlock;
struct SInstancesBlockDiagnostic;
struct SDataBlockDiagnostic;

//==============================================================================
//  Selection paths and mutable views
//==============================================================================

enum class EInstanceSelectionStepKind : std::uint8_t { member = 0u, element };

//  Member steps name structure members or bit fields; element steps use zero-based
//  fixed-array indices. An empty path addresses the whole instance. Binary field
//  input is the unshifted logical scalar, with valid range and sign extension.
struct SInstanceSelectionStep
{
    EInstanceSelectionStepKind kind{ EInstanceSelectionStepKind::member };
    CStringView member;
    std::uint32_t element{};
};

struct SMutableInstanceEntryView
{
    CTypesIndex type;
    CInstanceHandle parent;
    CInstanceHandle declaration;
    std::uint32_t offset{};
    std::uint64_t byte_count{};
    CByteView bytes; // Canonical empty view for a zero-byte snapshot.
};

//==============================================================================
//  Live instance role
//==============================================================================

class CLiveInstances
{
public:
    //==============================================================================
    //  Lifetime and initialisation
    //==============================================================================

    CLiveInstances() noexcept = default;
    CLiveInstances(const CLiveInstances&) = delete;
    CLiveInstances& operator=(const CLiveInstances&) = delete;
    CLiveInstances(CLiveInstances&&) = delete;
    CLiveInstances& operator=(CLiveInstances&&) = delete;
    ~CLiveInstances() noexcept = default;

    [[nodiscard]] bool initialise(CBakedTypes& schema, const std::size_t capacity = 0u) noexcept;
    [[nodiscard]] bool initialise(CLiveTypes& schema, const std::size_t capacity = 0u) noexcept;

    //  Preserve handles and allocation; rebuild inherited values from target defaults.
    [[nodiscard]] bool rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    void clear() noexcept;

    //==============================================================================
    //  Queries and payload access
    //==============================================================================

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_loaded && m_binding.is_usable() && !pending_edit(); }
    [[nodiscard]] bool pending_edit() const noexcept { return m_pending != k_no_parent; }
    [[nodiscard]] CInstanceDocumentQuery document_query() const noexcept;
    [[nodiscard]] CInstanceHandle instances_root() const noexcept;
    [[nodiscard]] CInstanceHandle find_base(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle find_specialisation(const CInstanceHandle parent, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle first_specialisation(const CInstanceHandle parent) const noexcept;
    [[nodiscard]] CInstanceHandle next_specialisation(const CInstanceHandle current) const noexcept;
    [[nodiscard]] CInstanceHandle parent_instance(const CInstanceHandle instance) const noexcept;
    [[nodiscard]] bool entry(const CInstanceHandle instance, SInstanceEntryView& result) const noexcept;
    [[nodiscard]] CByteConstView payload_view() const noexcept { return loaded_ready() ? m_payload.const_view() : CByteConstView{}; }
    [[nodiscard]] bool clear_unused_storage(const EUnusedBits bits = EUnusedBits::preserve) noexcept;

    //==============================================================================
    //  Creation and editing
    //==============================================================================

    [[nodiscard]] CInstanceHandle create_base(const CStringView& type, const CStringView& name,
        const CInstanceDocumentQuery& source, const CInstanceHandle declaration, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CInstanceHandle create_specialisation(const CInstanceHandle parent, const CStringView& name,
        const CInstanceDocumentQuery& source, const CInstanceHandle declaration, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CInstanceHandle capture_base(const CStringView& type, const CStringView& name,
        const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept;
    //  Complete writes retain selections and add differences; component writes pin
    //  only supplied paths. Each update explicitly preserves or propagates descendants.
    [[nodiscard]] bool overwrite(const CInstanceHandle instance, const CByteConstView& complete,
        const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool specialise(const CInstanceHandle instance, const SInstanceSelectionStep* steps,
        const std::size_t step_count, const CInstanceDocumentQuery& source, const CInstanceHandle value,
        const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool specialise(const CInstanceHandle instance, const SInstanceSelectionStep* steps,
        const std::size_t step_count, const CByteConstView& complete_component,
        const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool remove_selection(const CInstanceHandle instance, const SInstanceSelectionStep* steps,
        const std::size_t step_count, const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept;
    //  One complete-instance edit may be pending. Typed access and other mutation
    //  are blocked until explicit completion; token destruction has no side effects.
    //  Failed completion retains edited bytes for retry, without publishing selections
    //  or descendant changes. Clear the role to abandon it; no byte backup is retained.
    //  Successful completion ends writable access even if the address is unchanged.
    //  Reacquire handles into replaced selection subtrees after a successful edit.
    [[nodiscard]] bool begin_edit(const CInstanceHandle instance, CInstanceEditToken& token,
        SMutableInstanceEntryView& value, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool complete_edit(const CInstanceEditToken& token, const EInstanceDescendantPolicy policy,
        SInstanceDiagnostic& diagnostic) noexcept;

    //==============================================================================
    //  Output preparation and baking
    //==============================================================================

    //  Complete contiguous persistence output; destination must be empty.
    [[nodiscard]] bool prepare_output(CLiveDocument& document, CByteBuffer& payload, SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool bake(CBakedDataBlock& destination, SInstancesBlockDiagnostic& diagnostic) const noexcept;

private:

    //==============================================================================
    //  Private records and operations
    //==============================================================================

    friend class CBakedInstances;
    friend class CSchema;
    friend class detail::CSchemaConstruction;
    static constexpr std::uint32_t k_no_parent = UINT32_MAX;
    struct SRecord
    {
        CNodeKey entry, declaration;
        CTypesIndex type;
        std::uint32_t parent{ k_no_parent };
        std::uint32_t offset{}, extent{};
    };

    //  Document construction shared by creation and translation.
    [[nodiscard]] static bool append_node(CLiveDocument& document, const CNodeKey parent, const CNodeKey child) noexcept;
    [[nodiscard]] static CNodeKey make_locator(CLiveDocument& document, const std::uint32_t offset, const std::uint32_t extent) noexcept;

    [[nodiscard]] bool initialise_document(const std::size_t capacity) noexcept;
    template<class TSchema>
    [[nodiscard]] bool switch_to(TSchema& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept;
    void disable() noexcept;
    [[nodiscard]] bool mutable_entry(const CInstanceHandle instance, SMutableInstanceEntryView& result) noexcept;
    void take_from(CLiveInstances& source) noexcept;
    [[nodiscard]] bool record_index(const CInstanceHandle instance, std::uint32_t& index) const noexcept;
    [[nodiscard]] CInstanceHandle append_instance(const CTypesIndex type, const std::uint32_t parent,
        const CStringView& type_name, const CStringView& name, const detail::CDocumentRead& source,
        const detail::SOccurrence declaration, const CByteConstView& complete, SInstanceDiagnostic& diagnostic,
        const bool stripped = false) noexcept;
    [[nodiscard]] bool edit_existing(const std::uint32_t index, const CNodeKey selection,
        const CByteConstView& complete, const CNodeKey scope, const bool full_scope,
        const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool overwrite_record(const std::uint32_t index, const CByteConstView& complete,
        const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool baseline(const std::uint32_t index, CByteBuffer& defaults,
        const std::uint8_t*& bytes, SInstanceDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] CNodeKey output_declaration(CLiveDocument& target, const SRecord& record,
        const CResolvedTypes& types,
        EInstanceLoadReason& reason) const noexcept;
    [[nodiscard]] bool promote_from(const CBakedInstances& source, SInstanceDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
        SInstanceDiagnostic& diagnostic, SDataBlockDiagnostic* const block_diagnostic = nullptr) const noexcept;

    //==============================================================================
    //  Owned state
    //==============================================================================

    CLiveDocument m_document;
    CByteBuffer m_payload;
    CTypesBinding m_binding;
    //  Strictly increasing entry keys; see find_live_record().
    TPodVector<SRecord> m_records;
    bool m_loaded{};
    std::uint32_t m_pending{ k_no_parent };
    std::uint64_t m_edit_generation{};
};

}   // namespace schema

#endif // SCHEMA_LIVE_INSTANCES_HPP_INCLUDED
