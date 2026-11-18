
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Validate baked bulk data locators and bind or materialise external payloads.

#include "schema/baked_bulk_data.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/types/type_compatibility.hpp"
#include "memory/memory_policies.hpp"

#include <utility>

namespace schema
{

//==============================================================================
//  Lifetime, binding and diagnostics
//==============================================================================

bool CBakedBulkData::fail(SBulkDataDiagnostic& diagnostic, const EBulkDataLoadReason reason, const CBakedValueIndex occurrence) const noexcept
{
    if (diagnostic.reason == EBulkDataLoadReason::none)
    {
        diagnostic = { reason, CBulkDataHandle{ detail::SOccurrence{ occurrence } } };
    }
    return false;
}

void CBakedBulkData::clear_loaded() noexcept
{
    m_loaded = false;
    m_payload.reset();
    m_records.deallocate();
    m_record_index.clear();
}

void CBakedBulkData::clear() noexcept
{
    clear_loaded();
    m_binding.release();
    m_document.clear();
}

void CBakedBulkData::take_from(CBakedBulkData& source) noexcept
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

bool CBakedBulkData::set_document(const CBakedDocument& document) noexcept
{
    if (document_ready() || m_binding.is_attached() || !document.is_ready())
    {
        return false;
    }
    m_document = document;
    return true;
}

template<class TSchema>
bool CBakedBulkData::attach_block_to(const CBakedDataBlock& block, TSchema& types, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (document_ready() || !block.loaded_ready() || (block.has_bulk_data() && !block.bulk_binary_available()))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CBakedBulkData staged;
    if (!staged.set_document(block.document()) || !staged.attach_types(types, block.bulk_payload_view(), diagnostic)) return false;
    take_from(staged);
    return true;
}

bool CBakedBulkData::attach_block(const CBakedDataBlock& block, CBakedTypes& types, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_block_to(block, types, diagnostic);
}

bool CBakedBulkData::attach_block(const CBakedDataBlock& block, CLiveTypes& types, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_block_to(block, types, diagnostic);
}

template<class TSchema>
bool CBakedBulkData::attach_to(TSchema& types, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!document_ready() || m_loaded || m_binding.is_attached() || !types.resolved_ready())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CBakedBulkData prepared;
    prepared.m_document = m_document;
    CBakedTypes embedded;
    if (m_document.object_child(m_document.root(), CStringView{ "types" }))
    {
        SDiagnostic error;
        if (!embedded.set_document(m_document) || !embedded.resolve(error))
        {
            diagnostic.reason = error.reason == EReason::allocation_failed ? EBulkDataLoadReason::allocation_failed : EBulkDataLoadReason::invalid_input;
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::STypesHandleAccess::occurrence(error.occurrence));
            return false;
        }
        if (!prepared.m_binding.bind(embedded) || !prepared.load_supplied(payload, diagnostic) ||
            !prepared.switch_to(types, ETypeBindMode::checked, diagnostic)) return false;
    }
    else if (!prepared.m_binding.bind(types) || !prepared.load_supplied(payload, diagnostic)) return false;
    take_from(prepared);
    return true;
}

bool CBakedBulkData::attach_types(CBakedTypes& types, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_to(types, payload, diagnostic);
}

bool CBakedBulkData::attach_types(CLiveTypes& types, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_to(types, payload, diagnostic);
}

template<class TSchema>
bool CBakedBulkData::switch_to(TSchema& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!m_loaded || ((mode != ETypeBindMode::checked) && (mode != ETypeBindMode::asserted_recovery)))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CTypesBinding proposed;
    if (!proposed.bind(schema))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    const detail::CDocumentRead document{ m_document };
    if ((mode == ETypeBindMode::asserted_recovery) == static_cast<bool>(m_binding.resolved()))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    TPodVector<CTypesIndex> types;
    detail::SOccurrence failure;
    const auto result = detail::prepare_types_binding<false>(document,
        document.object_child(document.root(), CStringView{ "data" }), m_binding.resolved(), *proposed.resolved(),
        mode == ETypeBindMode::checked, m_records, types, failure);
    if (result != ETypeMatchResult::match)
    {
        diagnostic.reason = (result == ETypeMatchResult::allocation_failed) ? EBulkDataLoadReason::allocation_failed :
            (result == ETypeMatchResult::invalid_input) ? EBulkDataLoadReason::invalid_input : EBulkDataLoadReason::incompatible_schema;
        diagnostic.occurrence = detail::SBulkDataHandleAccess::make(failure);
        return false;
    }
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        m_records[i].type = types[i];
    }
    m_binding = std::move(proposed);
    return true;
}

bool CBakedBulkData::rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

bool CBakedBulkData::rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

//==============================================================================
//  Queries and payload access
//==============================================================================

bool CBakedBulkData::values_stripped() const noexcept
{
    bool stripped{};
    return document_ready() && m_document.boolean_value(
        m_document.object_child(m_document.root(), CStringView{ "stripped" }), stripped) && stripped;
}

CBulkDataDocumentQuery CBakedBulkData::document_query() const noexcept
{
    return CBulkDataDocumentQuery{ m_document };
}

CBulkDataHandle CBakedBulkData::data_root() const noexcept
{
    const CBulkDataDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "data" });
}

CBulkDataHandle CBakedBulkData::find_entry(const CStringView& type, const CStringView& name) const noexcept
{
    if (!loaded_ready())
    {
        return {};
    }
    const CBulkDataDocumentQuery query = document_query();
    const CBulkDataHandle group = query.object_child(data_root(), type);
    const CBulkDataHandle found = query.object_child(group, name);
    SBulkDataEntryView value;
    return entry(found, value) ? found : CBulkDataHandle{};
}

bool CBakedBulkData::entry(const CBulkDataHandle handle, SBulkDataEntryView& result) const noexcept
{
    if (!loaded_ready() || !handle.is_valid())
    {
        return false;
    }
    const detail::SOccurrence occurrence = detail::SBulkDataHandleAccess::occurrence(handle);
    if (!occurrence.is_baked())
    {
        return false;
    }
    std::uint32_t ordinal{};
    if (!m_record_index.find(m_records, occurrence.baked, ordinal))
    {
        return false;
    }
    const detail::SBakedBulkRecord& record = m_records[ordinal];
    SBulkDataEntryView value;
    value.type = record.type;
    value.count = record.count;
    value.offset = record.offset;
    value.stride = record.stride;
    value.byte_count = record.extent;
    if (record.extent != 0u)
    {
        if (!m_payload.is_ready() ||
            (record.offset > m_payload.size()) ||
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

bool CBakedBulkData::plan(const std::size_t payload_size,
    TPodVector<detail::SBakedBulkRecord>& records, std::size_t& total_size, SBulkDataDiagnostic& diagnostic) const noexcept
{
    const CResolvedTypes* const types = m_binding.resolved();
    if (!types)
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, {});
    }
    detail::SBakedBulkPlan staged;
    detail::SBakedRecordPlanDiagnostic error;
    if (!detail::plan_baked_bulk_data(m_document, *types, payload_size, staged, error))
    {
        return fail(diagnostic, detail::baked_record_plan_reason<EBulkDataLoadReason>(error.reason), error.occurrence);
    }
    records = std::move(staged.records);
    total_size = staged.payload_size;
    return true;
}

//==============================================================================
//  Loading supplied storage
//==============================================================================

bool CBakedBulkData::load_supplied(const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) noexcept
{
    clear_loaded();
    diagnostic = {};
    if (payload.is_ready() && ((reinterpret_cast<std::uintptr_t>(payload.data()) & (k_max_alignment - 1u)) != 0u))
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_range, {});
    }
    TPodVector<detail::SBakedBulkRecord> staged;
    std::size_t total_size{};
    if (!plan(payload.size(), staged, total_size, diagnostic) || (total_size != 0u && !payload.is_ready()))
    {
        return diagnostic.reason == EBulkDataLoadReason::none ? fail(diagnostic, EBulkDataLoadReason::invalid_range, {}) : false;
    }
    CBakedRecordIndex index;
    for (std::size_t ordinal = 0u; ordinal < staged.size(); ++ordinal)
    {
        SType layout;
        if (!m_binding.resolved()->type(staged[ordinal].type, layout) || layout.contains_references)
        {
            return fail(diagnostic, EBulkDataLoadReason::reference_context_required, staged[ordinal].entry);
        }
    }
    if (!index.build(staged))
    {
        return fail(diagnostic, EBulkDataLoadReason::allocation_failed, {});
    }
    m_records = std::move(staged);
    m_record_index = std::move(index);
    m_payload = payload;
    m_loaded = true;
    return true;
}

}   // namespace schema
