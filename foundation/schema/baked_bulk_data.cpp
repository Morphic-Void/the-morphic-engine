
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
#include "schema/data_block_format.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
#include "schema/data_document_validation.hpp"
#include "schema/data_storage_validation.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>
#include <limits>
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
    const SRecord& record = m_records[ordinal];
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
    TPodVector<SRecord>& records, std::size_t& total_size, SBulkDataDiagnostic& diagnostic) const noexcept
{
    total_size = 0u;
    const CResolvedTypes* const schema = m_binding.resolved();
    if (!document_ready() || !schema)
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, {});
    }
    detail::SDataBlockRoot metadata;
    SDataBlockDiagnostic root_error;
    const bool envelope = static_cast<bool>(m_document.object_child(m_document.root(), CStringView{ "fileType" }));
    if (!detail::read_data_block_root(detail::CDocumentRead{ m_document }, metadata, root_error, envelope))
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, root_error.location.baked_value);
    }
    if (metadata.bulk_size && (metadata.bulk_bytes != payload_size)) return fail(diagnostic, EBulkDataLoadReason::invalid_range, metadata.bulk_size.baked);
    if (!metadata.stripped)
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, m_document.root());
    }
    const CBakedValueIndex data_root = metadata.bulk_data.baked;
    if (!data_root.is_valid())
    {
        return true;
    }
    std::uint64_t cursor{};
    for (CBakedValueIndex group = m_document.first_child(data_root); group.is_valid(); group = m_document.next_sibling(group))
    {
        if (!m_document.is_object_entry(group) || (m_document.value_type(group) != EBakedValueType::object))
        {
            return fail(diagnostic, EBulkDataLoadReason::invalid_input, group);
        }
        const CTypesIndex type = schema->find_type(m_document.name(group));
        SType layout;
        if (!type.is_valid() || !schema->type(type, layout))
        {
            return fail(diagnostic, EBulkDataLoadReason::unknown_type, group);
        }
        for (CBakedValueIndex entry = m_document.first_child(group); entry.is_valid(); entry = m_document.next_sibling(entry))
        {
            if (!m_document.is_object_entry(entry) || (m_document.value_type(entry) != EBakedValueType::object))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_input, entry);
            }
            for (CBakedValueIndex property = m_document.first_child(entry); property.is_valid(); property = m_document.next_sibling(property))
            {
                const CStringView name = m_document.name(property);
                if (!detail::data_entry_property(name, true, true))
                {
                    return fail(diagnostic, EBulkDataLoadReason::unknown_property, property);
                }
            }
            const CBakedValueIndex locator = m_document.object_child(entry, CStringView{ "locator" });
            const CBakedValueIndex embedded = m_document.object_child(entry, CStringView{ "data" });
            if (!locator.is_valid() || (m_document.value_type(locator) != EBakedValueType::object))
            {
                return fail(diagnostic, EBulkDataLoadReason::missing_property, entry);
            }
            if (embedded.is_valid())
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_input, embedded);
            }
            detail::SDataLocator parsed;
            detail::SOccurrence failure;
            EBulkDataLoadReason reason{};
            if (!detail::read_data_locator(detail::CDocumentRead{ m_document }, detail::SOccurrence{ locator },
                true, layout.size, parsed, reason, failure))
            {
                return fail(diagnostic, reason, failure.baked);
            }
            const CBakedValueIndex valid_node = parsed.valid_node.baked;
            const CBakedValueIndex count_node = parsed.count_node.baked;
            const CBakedValueIndex size_node = parsed.size_node.baked;
            const std::uint64_t raw_offset = parsed.offset;
            const std::uint64_t raw_count = parsed.count, raw_size = parsed.size;
            const bool prior_valid = parsed.valid;
            std::uint64_t count{};
            if (count_node.is_valid())
            {
                count = raw_count;
            }
            else if (size_node.is_valid() && (layout.size != 0u) && ((raw_size % layout.size) == 0u))
            {
                count = raw_size / layout.size;
            }
            std::uint64_t extent{};
            if (!detail::data_extent(layout, count, extent))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_count, locator);
            }
            if (size_node.is_valid() && (raw_size != extent))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_range, size_node);
            }
            if (!prior_valid)
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_locator, valid_node);
            }
            std::uint64_t offset = raw_offset;
            if (!detail::check_data_range(offset, extent, layout.alignment, cursor, payload_size, reason))
            {
                return fail(diagnostic, reason, locator);
            }
            const std::uint64_t end = offset + extent;
            SRecord record;
            record.entry = entry;
            record.type = type;
            record.count = static_cast<std::uint32_t>(count);
            record.offset = static_cast<std::uint32_t>(offset);
            record.extent = static_cast<std::uint32_t>(extent);
            record.stride = static_cast<std::uint32_t>(layout.size);
            if (!records.push_back(record))
            {
                return fail(diagnostic, EBulkDataLoadReason::allocation_failed, entry);
            }
            if ((extent != 0u) || (offset != 0u))
            {
                cursor = end;
            }
        }
    }
    total_size = static_cast<std::size_t>(cursor);
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
    TPodVector<SRecord> staged;
    std::size_t total_size{};
    if (!plan(payload.size(), staged, total_size, diagnostic) || (total_size != 0u && !payload.is_ready()))
    {
        return diagnostic.reason == EBulkDataLoadReason::none ? fail(diagnostic, EBulkDataLoadReason::invalid_range, {}) : false;
    }
    CBakedRecordIndex index;
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
