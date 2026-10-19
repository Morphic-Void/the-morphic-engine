
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_data.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Validate baked bulk data locators and bind or materialise external payloads.

#include "schema/baked_bulk_data.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
#include "schema/data_document_validation.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace schema
{

//==============================================================================
//  Planning helpers
//==============================================================================

[[nodiscard]] static bool bulk_data_name_is(const CStringView name, const char* const literal) noexcept
{
    const CStringView expected{ literal };
    return (name.length() == expected.length()) &&
        ((name.length() == 0u) || (std::memcmp(name.string(), expected.string(), name.length()) == 0));
}

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
    m_mutable.clear();
    m_document.clear();
}

void CBakedBulkData::take_from(CBakedBulkData& source) noexcept
{
    m_document = source.m_document;
    m_mutable = source.m_mutable;
    m_binding = std::move(source.m_binding);
    m_payload = source.m_payload;
    m_records = std::move(source.m_records);
    m_record_index = std::move(source.m_record_index);
    m_loaded = source.m_loaded;
    source.m_document.clear();
    source.m_mutable.clear();
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

bool CBakedBulkData::set_document(const CMutableBakedDocument& document) noexcept
{
    if (document_ready() || m_binding.is_attached() || !document.is_ready())
    {
        return false;
    }
    m_mutable = document;
    m_document = document.baked();
    return true;
}

template<class TSchema>
bool CBakedBulkData::attach_to(TSchema& schema, CByteBuffer* const returned_owner, const CByteConstView& payload,
    const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!document_ready() || m_loaded || m_binding.is_attached())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CBakedBulkData prepared;
    prepared.m_document = m_document;
    prepared.m_mutable = m_mutable;
    if (!prepared.m_binding.bind(schema))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    if (returned_owner ? !prepared.materialise(*returned_owner, diagnostic) :
        !prepared.load_supplied(payload, compare_embedded, diagnostic))
    {
        return false;
    }
    take_from(prepared);
    return true;
}

bool CBakedBulkData::attach_schema(CBakedSchema& schema, const CByteConstView& payload,
    const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_to(schema, nullptr, payload, compare_embedded, diagnostic);
}

bool CBakedBulkData::attach_schema(CBakedSchema& schema, CByteBuffer& returned_owner, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_to(schema, &returned_owner, {}, false, diagnostic);
}

bool CBakedBulkData::attach_schema(CLiveSchema& schema, const CByteConstView& payload,
    const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_to(schema, nullptr, payload, compare_embedded, diagnostic);
}

bool CBakedBulkData::attach_schema(CLiveSchema& schema, CByteBuffer& returned_owner, SBulkDataDiagnostic& diagnostic) noexcept
{
    return attach_to(schema, &returned_owner, {}, false, diagnostic);
}

template<class TSchema>
bool CBakedBulkData::switch_to(TSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!m_loaded || ((mode != ESchemaSwitchMode::checked) && (mode != ESchemaSwitchMode::trusted)))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CSchemaBinding proposed;
    if (!proposed.bind(schema))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    const detail::CDocumentRead document{ m_document };
    TPodVector<CSchemaIndex> types;
    detail::SOccurrence failure;
    const auto result = detail::prepare_schema_switch<false>(document,
        document.object_child(document.root(), CStringView{ "data" }), m_binding.resolved(), *proposed.resolved(),
        mode == ESchemaSwitchMode::checked, m_records, types, failure);
    if (result != ETypeMatchResult::match)
    {
        diagnostic.reason = (result == ETypeMatchResult::allocation_failed) ? EBulkDataLoadReason::allocation_failed :
            (result == ETypeMatchResult::invalid_input) ? EBulkDataLoadReason::invalid_input : EBulkDataLoadReason::incompatible_schema;
        diagnostic.occurrence = detail::SBulkDataHandleAccess::make(failure);
        return false;
    }
    const CByteConstView payload = m_payload;
    if ((mode == ESchemaSwitchMode::checked) && !values_stripped())
    {
        detail::SValueDiagnostic error;
        const auto checked = detail::check_bulk_records(*proposed.resolved(), document, m_records, payload.data(),
            [&](const std::size_t i) noexcept
            {
                return types[i];
            }, error);
        if (checked != detail::EValueCheck::match)
        {
            diagnostic.reason = detail::value_failure(error, ((checked == detail::EValueCheck::mismatch) ? EBulkDataLoadReason::embedded_mismatch : EBulkDataLoadReason::incomplete_record));
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(error.occurrence);
            return false;
        }
    }
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        m_records[i].type = types[i];
    }
    m_binding = std::move(proposed);
    return true;
}

bool CBakedBulkData::switch_schema(CBakedSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

bool CBakedBulkData::switch_schema(CLiveSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept
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

bool CBakedBulkData::plan(const bool supplied, const std::size_t payload_size,
    TPodVector<SRecord>& records, std::size_t& total_size, SBulkDataDiagnostic& diagnostic) const noexcept
{
    total_size = 0u;
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!document_ready() || !schema)
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, {});
    }
    const CBakedValueIndex root = m_document.root();
    if (m_document.value_type(root) != EBakedValueType::object)
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, root);
    }
    const CBakedValueIndex marker = m_document.object_child(root, CStringView{ "stripped" });
    bool stripped{};
    if (marker && !m_document.boolean_value(marker, stripped))
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, marker);
    }
    if (stripped && !supplied)
    {
        return fail(diagnostic, EBulkDataLoadReason::binary_required, marker);
    }
    const CBakedValueIndex data_root = m_document.object_child(root, CStringView{ "data" });
    if (!data_root.is_valid())
    {
        return true;
    }
    if (m_document.value_type(data_root) != EBakedValueType::object)
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, data_root);
    }
    std::uint64_t cursor{};
    for (CBakedValueIndex group = m_document.first_child(data_root); group.is_valid(); group = m_document.next_sibling(group))
    {
        if (!m_document.is_object_entry(group) || (m_document.value_type(group) != EBakedValueType::object))
        {
            return fail(diagnostic, EBulkDataLoadReason::invalid_input, group);
        }
        const CSchemaIndex type = schema->find_type(m_document.name(group));
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
            if (embedded.is_valid() && (stripped || (m_document.value_type(embedded) != EBakedValueType::array)))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_input, embedded);
            }
            for (CBakedValueIndex value = m_document.first_child(embedded); value.is_valid(); value = m_document.next_sibling(value))
            {
                if (m_document.is_object_entry(value) && !
                    ((layout.category == ECategory::structure) ? schema->find_member(type, m_document.name(value)) :
                        ((layout.category == ECategory::bit_structure) ? schema->find_field(type, m_document.name(value)) : CSchemaIndex{})))
                {
                    return fail(diagnostic, EBulkDataLoadReason::invalid_input, value);
                }
            }
            if (!supplied && !embedded.is_valid())
            {
                return fail(diagnostic, EBulkDataLoadReason::missing_property, entry);
            }
            for (CBakedValueIndex property = m_document.first_child(locator); property.is_valid(); property = m_document.next_sibling(property))
            {
                const CStringView name = m_document.name(property);
                if (!bulk_data_name_is(name, "offset") && !bulk_data_name_is(name, "valid") &&
                    !bulk_data_name_is(name, "count") && !bulk_data_name_is(name, "size"))
                {
                    return fail(diagnostic, EBulkDataLoadReason::unknown_property, property);
                }
            }
            const CBakedValueIndex offset_node = m_document.object_child(locator, CStringView{ "offset" });
            const CBakedValueIndex valid_node = m_document.object_child(locator, CStringView{ "valid" });
            const CBakedValueIndex count_node = m_document.object_child(locator, CStringView{ "count" });
            const CBakedValueIndex size_node = m_document.object_child(locator, CStringView{ "size" });
            std::uint64_t raw_offset{}, raw_count{}, raw_size{};
            bool prior_valid{};
            if (!offset_node.is_valid() || !valid_node.is_valid() ||
                !m_document.unsigned_integer_value(offset_node, raw_offset) || (raw_offset > UINT32_MAX) ||
                !m_document.boolean_value(valid_node, prior_valid))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_locator, locator);
            }
            if (count_node.is_valid() && (!m_document.unsigned_integer_value(count_node, raw_count) ||
                (raw_count == 0u) || (raw_count > UINT32_MAX)))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_count, count_node);
            }
            if (size_node.is_valid() && (!m_document.unsigned_integer_value(size_node, raw_size) ||
                (raw_size > memory::k_byte_size_ceiling)))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_range, size_node);
            }
            std::uint64_t count{};
            if (embedded.is_valid())
            {
                count = m_document.child_count(embedded);
                if (count == 0u)
                {
                    return fail(diagnostic, EBulkDataLoadReason::invalid_count, embedded);
                }
                if (count_node.is_valid() && (raw_count != count))
                {
                    return fail(diagnostic, EBulkDataLoadReason::invalid_count, count_node);
                }
            }
            else if (count_node.is_valid())
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
            if (supplied && !prior_valid)
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_locator, valid_node);
            }
            if (!supplied && !prior_valid && !m_mutable.is_ready())
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_locator, valid_node);
            }
            std::uint64_t offset = raw_offset;
            if (!prior_valid)
            {
                if (!detail::align_data_offset(cursor, layout.alignment, offset))
                {
                    return fail(diagnostic, EBulkDataLoadReason::invalid_range, locator);
                }
            }
            if ((offset < cursor) && ((extent != 0u) || (offset != 0u)))
            {
                return fail(diagnostic, EBulkDataLoadReason::overlap, locator);
            }
            if ((offset > UINT32_MAX) ||
                (offset > memory::k_byte_size_ceiling) ||
                ((offset & (layout.alignment - 1u)) != 0u) ||
                (extent > (memory::k_byte_size_ceiling - offset)))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_range, locator);
            }
            const std::uint64_t end = offset + extent;
            if (supplied && (end > payload_size))
            {
                return fail(diagnostic, EBulkDataLoadReason::invalid_range, locator);
            }
            SRecord record;
            record.entry = entry;
            record.data = embedded;
            record.offset_node = offset_node;
            record.valid_node = valid_node;
            record.type = type;
            record.count = static_cast<std::uint32_t>(count);
            record.offset = static_cast<std::uint32_t>(offset);
            record.extent = static_cast<std::uint32_t>(extent);
            record.stride = static_cast<std::uint32_t>(layout.size);
            record.prior_valid = prior_valid;
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
//  Embedded-value comparison
//==============================================================================

bool CBakedBulkData::compare_embedded(const TPodVector<SRecord>& records, const CByteConstView& payload, SBulkDataDiagnostic& diagnostic) const noexcept
{
    detail::SValueDiagnostic error;
    const auto checked = detail::check_bulk_records(*m_binding.resolved(), detail::CDocumentRead{ m_document }, records,
        payload.data(), [&](const std::size_t i) noexcept
        {
            return records[i].type;
        }, error);
    if (checked == detail::EValueCheck::match)
    {
        return true;
    }
    return fail(diagnostic, detail::value_failure(error, ((checked == detail::EValueCheck::mismatch) ? EBulkDataLoadReason::embedded_mismatch :
        EBulkDataLoadReason::incomplete_record)), error.occurrence.baked);
}

//==============================================================================
//  Loading and materialisation
//==============================================================================

bool CBakedBulkData::load_supplied(const CByteConstView& payload, const bool compare_embedded, SBulkDataDiagnostic& diagnostic) noexcept
{
    clear_loaded();
    diagnostic = {};
    if (payload.is_ready() && ((reinterpret_cast<std::uintptr_t>(payload.data()) & (k_max_alignment - 1u)) != 0u))
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_range, {});
    }
    TPodVector<SRecord> staged;
    std::size_t total_size{};
    if (!plan(true, payload.size(), staged, total_size, diagnostic) || (total_size != 0u && !payload.is_ready()))
    {
        return diagnostic.reason == EBulkDataLoadReason::none ? fail(diagnostic, EBulkDataLoadReason::invalid_range, {}) : false;
    }
    if (compare_embedded && !values_stripped() && !this->compare_embedded(staged, payload, diagnostic))
    {
        return false;
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

bool CBakedBulkData::materialise(CByteBuffer& returned_owner, SBulkDataDiagnostic& diagnostic) noexcept
{
    clear_loaded();
    diagnostic = {};
    if (returned_owner.is_ready())
    {
        return fail(diagnostic, EBulkDataLoadReason::invalid_input, {});
    }
    TPodVector<SRecord> staged;
    std::size_t total_size{};
    if (!plan(false, 0u, staged, total_size, diagnostic))
    {
        return false;
    }
    CBakedRecordIndex index;
    if (!index.build(staged))
    {
        return fail(diagnostic, EBulkDataLoadReason::allocation_failed, {});
    }
    CByteBuffer payload;
    if ((total_size != 0u) && (!payload.allocate(total_size, k_max_alignment) || !payload.set_size(total_size)))
    {
        return fail(diagnostic, EBulkDataLoadReason::allocation_failed, {});
    }
    const CResolvedSchema* const schema = m_binding.resolved();
    detail::CDocumentRead document{ m_document };
    std::size_t constructed_end{};
    for (std::size_t ordinal = 0u; ordinal < staged.size(); ++ordinal)
    {
        const SRecord& record = staged[ordinal];
        if (record.extent != 0u)
        {   //  This payload is new; supplied payloads never pass through construction.
            if (record.offset > constructed_end)
            {
                std::memset((payload.data() + constructed_end), 0, (record.offset - constructed_end));
            }
            constructed_end = static_cast<std::size_t>(record.offset) + record.extent;
        }
        CBakedValueIndex source = m_document.first_child(record.data);
        for (std::uint32_t index = 0u; index < record.count; ++index)
        {
            detail::SValueDiagnostic value_error;
            const std::uint64_t record_offset = static_cast<std::uint64_t>(record.offset) +
                (static_cast<std::uint64_t>(record.stride) * index);
            std::uint8_t* const target = (record.stride == 0u) ? nullptr : (payload.data() + record_offset);
            if (!detail::construct_value(*schema, document, record.type, detail::SOccurrence{ source },
                target, record.stride, detail::EConstructionMode::complete_bulk_data, value_error, m_document.is_object_entry(source)))
            {
                return fail(diagnostic, detail::value_failure(value_error, EBulkDataLoadReason::incomplete_record), value_error.occurrence.is_baked() ?
                    value_error.occurrence.baked : source);
            }
            source = m_document.next_sibling(source);
        }
    }

    //  Every allocation and conversion has succeeded. Only reserved scalar
    //  nodes are changed; a failed setter leaves this role unready.
    for (std::size_t ordinal = 0u; ordinal < staged.size(); ++ordinal)
    {
        const SRecord& record = staged[ordinal];
        if (!record.prior_valid &&
            (!m_mutable.set_unsigned_integer_value(record.offset_node, record.offset) ||
             !m_mutable.set_boolean_value(record.valid_node, true)))
        {
            return fail(diagnostic, EBulkDataLoadReason::invalid_locator, record.entry);
        }
    }
    returned_owner = std::move(payload);
    m_payload = returned_owner.const_view();
    m_records = std::move(staged);
    m_record_index = std::move(index);
    m_loaded = true;
    return true;
}

}   // namespace schema
