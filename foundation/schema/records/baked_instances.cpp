
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instances.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Attach complete baked snapshots and validate their selection inheritance.

#include "schema/baked_instances.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/types/type_compatibility.hpp"
#include "schema/values/value_codec.hpp"
#include "schema/values/instance_selection.hpp"

#include <cstring>
#include <utility>

namespace schema
{

//==============================================================================
//  Lifetime, binding and diagnostics
//==============================================================================

bool CBakedInstances::fail(SInstanceDiagnostic& diagnostic, const EInstanceLoadReason reason, const CBakedValueIndex occurrence) const noexcept
{
    if (diagnostic.reason == EInstanceLoadReason::none)
    {
        diagnostic = { reason, detail::SInstanceHandleAccess::make(detail::SOccurrence{ occurrence }) };
    }
    return false;
}

void CBakedInstances::clear_loaded() noexcept
{
    m_loaded = false;
    m_payload.reset();
    m_records.deallocate();
    m_record_index.clear();
}

void CBakedInstances::clear() noexcept
{
    clear_loaded();
    m_binding.release();
    m_document.clear();
}

void CBakedInstances::take_from(CBakedInstances& source) noexcept
{
    m_document = source.m_document;
    m_binding = std::move(source.m_binding);
    m_payload = source.m_payload;
    m_records = std::move(source.m_records);
    m_record_index = std::move(source.m_record_index);
    m_loaded = source.m_loaded;
    source.m_document.clear();
    source.m_payload = {};
    source.m_loaded = false;
}

bool CBakedInstances::set_document(const CBakedDocument& document) noexcept
{
    if (document_ready() || m_binding.is_attached() || !document.is_ready())
    {
        return false;
    }
    m_document = document;
    return true;
}

template<class TSchema>
bool CBakedInstances::attach_block_to(const CBakedDataBlock& block, TSchema& types, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (document_ready() || !block.loaded_ready() || (block.has_instances() && !block.instance_binary_available()))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CBakedInstances staged;
    if (!staged.set_document(block.document()) || !staged.attach_types(types, block.instance_payload_view(), diagnostic)) return false;
    take_from(staged);
    return true;
}

bool CBakedInstances::attach_block(const CBakedDataBlock& block, CBakedTypes& types, SInstanceDiagnostic& diagnostic) noexcept
{
    return attach_block_to(block, types, diagnostic);
}

bool CBakedInstances::attach_block(const CBakedDataBlock& block, CLiveTypes& types, SInstanceDiagnostic& diagnostic) noexcept
{
    return attach_block_to(block, types, diagnostic);
}

template<class TSchema>
bool CBakedInstances::attach_to(TSchema& types, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!document_ready() || m_loaded || m_binding.is_attached() || !types.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CBakedInstances prepared;
    prepared.m_document = m_document;
    CBakedTypes embedded;
    if (m_document.object_child(m_document.root(), CStringView{ "types" }))
    {
        SDiagnostic error;
        if (!embedded.set_document(m_document) || !embedded.resolve(error))
        {
            diagnostic.reason = error.reason == EReason::allocation_failed ? EInstanceLoadReason::allocation_failed : EInstanceLoadReason::invalid_input;
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::STypesHandleAccess::occurrence(error.occurrence));
            return false;
        }
        if (!prepared.m_binding.bind(embedded) || !prepared.load_supplied(payload, diagnostic) ||
            !prepared.switch_to(types, ETypeBindMode::checked, diagnostic, nullptr, true)) return false;
    }
    else if (!prepared.m_binding.bind(types) || !prepared.load_supplied(payload, diagnostic)) return false;
    take_from(prepared);
    return true;
}

bool CBakedInstances::attach_types(CBakedTypes& types, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept
{
    return attach_to(types, payload, diagnostic);
}

bool CBakedInstances::attach_types(CLiveTypes& types, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept
{
    return attach_to(types, payload, diagnostic);
}

template<class TSchema>
bool CBakedInstances::switch_to(TSchema& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic,
    CByteBuffer* replacement, const bool source_validated) noexcept
{
    diagnostic = {};
    if (!m_loaded || (replacement && replacement->capacity()) ||
        ((mode != ETypeBindMode::checked) && (mode != ETypeBindMode::asserted_recovery)))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CTypesBinding proposed;
    if (!proposed.bind(schema))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const detail::CDocumentRead document{ m_document };
    const auto* prior = m_binding.resolved();
    if ((mode == ETypeBindMode::asserted_recovery) == static_cast<bool>(prior))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    detail::SValueDiagnostic error;
    //  Only initial attachment can reuse the source check performed by load_supplied.
    //  Public rebinding rechecks borrowed bytes, even when the owner is unchanged.
    if (prior && !source_validated)
    {
        const auto checked = detail::check_selected_records(*prior, document, m_records, m_payload.data(),
            [&](const std::size_t i) noexcept { return m_records[i].type; }, error);
        if (checked != detail::EValueCheck::match)
        {
            diagnostic.reason = detail::value_failure(error, checked == detail::EValueCheck::mismatch ?
                EInstanceLoadReason::inheritance_mismatch : EInstanceLoadReason::invalid_declaration);
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
            return false;
        }
    }
    TPodVector<CTypesIndex> types;
    detail::SOccurrence failure;
    const auto result = detail::prepare_types_binding<true>(document,
        document.object_child(document.root(), CStringView{ "instances" }), m_binding.resolved(), *proposed.resolved(),
        mode == ETypeBindMode::checked, m_records, types, failure);
    if (result != ETypeMatchResult::match)
    {
        diagnostic.reason = (result == ETypeMatchResult::allocation_failed) ? EInstanceLoadReason::allocation_failed :
            (result == ETypeMatchResult::invalid_input) ? EInstanceLoadReason::invalid_input : EInstanceLoadReason::incompatible_schema;
        diagnostic.occurrence = detail::SInstanceHandleAccess::make(failure);
        return false;
    }
    CByteBuffer staged;
    if (replacement)
    {
        if (m_payload.size() && (!staged.allocate(m_payload.size(), k_max_alignment) || !staged.set_size(m_payload.size())))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        if (m_payload.size()) std::memcpy(staged.data(), m_payload.data(), m_payload.size());
        if (!detail::rebuild_inherited_records(*proposed.resolved(), document, m_records, staged.data(), staged.size(),
            [&](const std::size_t i) noexcept { return types[i]; }, error))
        {
            diagnostic.reason = detail::value_failure(error, EInstanceLoadReason::invalid_declaration);
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
            return false;
        }
    }
    else
    {
        const auto checked = detail::check_selected_records(*proposed.resolved(), document, m_records, m_payload.data(),
            [&](const std::size_t i) noexcept
            {
                return types[i];
            }, error);
        if (checked != detail::EValueCheck::match)
        {
            diagnostic.reason = detail::value_failure(error, ((checked == detail::EValueCheck::mismatch) ? EInstanceLoadReason::inheritance_mismatch : EInstanceLoadReason::invalid_declaration));
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
            return false;
        }
    }
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        m_records[i].type = types[i];
    }
    m_binding = std::move(proposed);
    if (replacement)
    {
        *replacement = std::move(staged);
        m_payload = replacement->const_view();
    }
    return true;
}

bool CBakedInstances::rebind_types(CBakedTypes& types, CByteBuffer& replacement,
    const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept
{
    return switch_to(types, mode, diagnostic, &replacement);
}

bool CBakedInstances::rebind_types(CLiveTypes& types, CByteBuffer& replacement,
    const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept
{
    return switch_to(types, mode, diagnostic, &replacement);
}

bool CBakedInstances::rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

bool CBakedInstances::rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SInstanceDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

//==============================================================================
//  Queries and payload access
//==============================================================================

bool CBakedInstances::values_stripped() const noexcept
{
    bool stripped{};
    return document_ready() && m_document.boolean_value(
        m_document.object_child(m_document.root(), CStringView{ "stripped" }), stripped) && stripped;
}

CInstanceDocumentQuery CBakedInstances::document_query() const noexcept
{
    return CInstanceDocumentQuery{ m_document };
}

CInstanceHandle CBakedInstances::instances_root() const noexcept
{
    const CInstanceDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "instances" });
}

bool CBakedInstances::record_index(const CInstanceHandle handle, std::uint32_t& result) const noexcept
{
    if (!loaded_ready() || !handle.is_valid())
    {
        return false;
    }
    const detail::SOccurrence occurrence = detail::SInstanceHandleAccess::occurrence(handle);
    if (!occurrence.is_baked())
    {
        return false;
    }
    return m_record_index.find(m_records, occurrence.baked, result);
}

CInstanceHandle CBakedInstances::find_base(const CStringView& type, const CStringView& name) const noexcept
{
    if (!loaded_ready())
    {
        return {};
    }
    const CInstanceDocumentQuery query = document_query();
    const CInstanceHandle group = query.object_child(instances_root(), type);
    const CInstanceHandle found = query.object_child(group, name);
    std::uint32_t index{};
    return record_index(found, index) && (m_records[index].parent == k_no_parent) ? found : CInstanceHandle{};
}

CInstanceHandle CBakedInstances::find_specialisation(const CInstanceHandle parent, const CStringView& name) const noexcept
{
    std::uint32_t parent_index{};
    if (!record_index(parent, parent_index))
    {
        return {};
    }
    const CInstanceDocumentQuery query = document_query();
    const CInstanceHandle container = query.object_child(parent, CStringView{ "specialisation" });
    const CInstanceHandle found = query.object_child(container, name);
    std::uint32_t index{};
    return record_index(found, index) && (m_records[index].parent == parent_index) ? found : CInstanceHandle{};
}

CInstanceHandle CBakedInstances::first_specialisation(const CInstanceHandle parent) const noexcept
{
    std::uint32_t parent_index{};
    if (!record_index(parent, parent_index))
    {
        return {};
    }
    const CInstanceDocumentQuery query = document_query();
    const CInstanceHandle found = query.first_child(query.object_child(parent, CStringView{ "specialisation" }));
    std::uint32_t index{};
    return record_index(found, index) && (m_records[index].parent == parent_index) ? found : CInstanceHandle{};
}

CInstanceHandle CBakedInstances::next_specialisation(const CInstanceHandle current) const noexcept
{
    std::uint32_t current_index{};
    if (!record_index(current, current_index) || (m_records[current_index].parent == k_no_parent))
    {
        return {};
    }
    const CInstanceHandle found = document_query().next_sibling(current);
    std::uint32_t index{};
    return record_index(found, index) && (m_records[index].parent == m_records[current_index].parent) ?
        found : CInstanceHandle{};
}

CInstanceHandle CBakedInstances::parent_instance(const CInstanceHandle instance) const noexcept
{
    std::uint32_t index{};
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent))
    {
        return {};
    }
    return detail::SInstanceHandleAccess::make(detail::SOccurrence{ m_records[m_records[index].parent].entry });
}

bool CBakedInstances::entry(const CInstanceHandle handle, SInstanceEntryView& result) const noexcept
{
    std::uint32_t index{};
    if (!record_index(handle, index))
    {
        return false;
    }
    const detail::SBakedInstanceRecord& record = m_records[index];
    SInstanceEntryView value;
    value.type = record.type;
    value.parent = (record.parent == k_no_parent) ? CInstanceHandle{} :
        detail::SInstanceHandleAccess::make(detail::SOccurrence{ m_records[record.parent].entry });
    value.declaration = detail::SInstanceHandleAccess::make(detail::SOccurrence{ record.declaration });
    value.offset = record.offset;
    value.byte_count = record.extent;
    if (record.extent != 0u)
    {
        if (!m_payload.is_ready() || (record.offset > m_payload.size()) ||
            (record.extent > (m_payload.size() - record.offset)))
        {
            return false;
        }
        value.bytes = m_payload.data() + record.offset;
    }
    result = value;
    return true;
}

//==============================================================================
//  Load planning
//==============================================================================

bool CBakedInstances::plan(const std::size_t supplied_size,
    TPodVector<detail::SBakedInstanceRecord>& records, std::size_t& payload_size,
    SInstanceDiagnostic& diagnostic) const noexcept
{
    const CResolvedTypes* const schema = m_binding.resolved();
    if (!schema)
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_input, {});
    }
    detail::SBakedInstancePlan staged;
    detail::SBakedRecordPlanDiagnostic error;
    if (!detail::plan_baked_instances(m_document, *schema, supplied_size, staged, error))
    {
        const EInstanceLoadReason reason = (error.reason == detail::EBakedRecordPlanReason::invalid_declaration) ?
            EInstanceLoadReason::invalid_declaration : detail::baked_record_plan_reason<EInstanceLoadReason>(error.reason);
        return fail(diagnostic, reason, error.occurrence);
    }
    records = std::move(staged.records);
    payload_size = staged.payload_size;
    return true;
}

//==============================================================================
//  Inherited-value consistency
//==============================================================================

bool CBakedInstances::check_inheritance(const TPodVector<detail::SBakedInstanceRecord>& records, const CByteConstView& payload, SInstanceDiagnostic& diagnostic) const noexcept
{
    detail::SValueDiagnostic error;
    const auto checked = detail::check_selected_records(*m_binding.resolved(), detail::CDocumentRead{ m_document }, records,
        payload.data(), [&](const std::size_t i) noexcept
        {
            return records[i].type;
        }, error);
    if (checked == detail::EValueCheck::match)
    {
        return true;
    }
    return fail(diagnostic, detail::value_failure(error, ((checked == detail::EValueCheck::mismatch) ? EInstanceLoadReason::inheritance_mismatch :
        EInstanceLoadReason::invalid_declaration)), error.occurrence.baked);
}

//==============================================================================
//  Loading supplied storage
//==============================================================================

bool CBakedInstances::load_supplied(const CByteConstView& payload, SInstanceDiagnostic& diagnostic) noexcept
{
    clear_loaded();
    diagnostic = {};
    if (payload.is_ready() && ((reinterpret_cast<std::uintptr_t>(payload.data()) & (k_max_alignment - 1u)) != 0u))
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_range, {});
    }
    TPodVector<detail::SBakedInstanceRecord> staged;
    std::size_t payload_size{};
    if (!plan(payload.size(), staged, payload_size, diagnostic) || ((payload_size != 0u) && !payload.is_ready()))
    {
        return (diagnostic.reason == EInstanceLoadReason::none) ?
            fail(diagnostic, EInstanceLoadReason::invalid_range, {}) : false;
    }
    for (std::size_t ordinal = 0u; ordinal < staged.size(); ++ordinal)
    {
        SType layout;
        if (!m_binding.resolved()->type(staged[ordinal].type, layout) || layout.contains_references)
        {
            return fail(diagnostic, EInstanceLoadReason::reference_context_required, staged[ordinal].entry);
        }
    }
    if (!this->check_inheritance(staged, payload, diagnostic))
    {
        return false;
    }
    CBakedRecordIndex index;
    if (!index.build(staged))
    {
        return fail(diagnostic, EInstanceLoadReason::allocation_failed, {});
    }
    m_records = std::move(staged);
    m_record_index = std::move(index);
    m_payload = payload;
    m_loaded = true;
    return true;
}

}   // namespace schema
