
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instances.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Validate baked instance locators and construct independent snapshots.

#include "schema/baked_instances.hpp"
#include "schema/value_codec.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>
#include <utility>

namespace schema
{

[[nodiscard]] static bool instance_name_is(const CStringView name, const char* const literal) noexcept
{
    const CStringView expected{ literal };
    return (name.length() == expected.length()) &&
        ((name.length() == 0u) || (std::memcmp(name.string(), expected.string(), name.length()) == 0));
}

[[nodiscard]] static bool align_instance_offset(const std::uint64_t position, const std::uint64_t alignment, std::uint64_t& result) noexcept
{
    if ((alignment == 0u) || ((alignment & (alignment - 1u)) != 0u) ||
        (position > (UINT64_MAX - (alignment - 1u))))
    {
        return false;
    }
    result = (position + alignment - 1u) & ~(alignment - 1u);
    return result <= memory::k_byte_size_ceiling;
}

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
    m_mutable.clear();
    m_document.clear();
}

void CBakedInstances::take_from(CBakedInstances& source) noexcept
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

bool CBakedInstances::set_document(const CBakedDocument& document) noexcept
{
    if (document_ready() || m_binding.is_attached() || !document.is_ready())
    {
        return false;
    }
    m_document = document;
    return true;
}

bool CBakedInstances::set_document(const CMutableBakedDocument& document) noexcept
{
    if (document_ready() || m_binding.is_attached() || !document.is_ready())
    {
        return false;
    }
    m_mutable = document;
    m_document = document.baked();
    return true;
}

bool CBakedInstances::bind_schema(CBakedSchema& schema) noexcept
{
    if (!document_ready())
    {
        return false;
    }
    clear_loaded();
    return m_binding.bind(schema);
}

bool CBakedInstances::bind_schema(CLiveSchema& schema) noexcept
{
    if (!document_ready())
    {
        return false;
    }
    clear_loaded();
    return m_binding.bind(schema);
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
    const SRecord& record = m_records[index];
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

bool CBakedInstances::values_stripped() const noexcept
{
    bool stripped{};
    return document_ready() && m_document.boolean_value(
        m_document.object_child(m_document.root(), CStringView{ "stripped" }), stripped) && stripped;
}

bool CBakedInstances::plan(const bool supplied, const std::size_t supplied_size,
    TPodVector<SRecord>& records, std::size_t& payload_size, std::size_t& scratch_size,
    SInstanceDiagnostic& diagnostic) const noexcept
{
    payload_size = 0u;
    scratch_size = 0u;
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!document_ready() || !schema)
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_input, {});
    }
    const CBakedValueIndex root = m_document.root();
    if (m_document.value_type(root) != EBakedValueType::object)
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_input, root);
    }
    const CBakedValueIndex marker = m_document.object_child(root, CStringView{ "stripped" });
    bool stripped{};
    if (marker && !m_document.boolean_value(marker, stripped))
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_input, marker);
    }
    if (stripped && !supplied)
    {
        return fail(diagnostic, EInstanceLoadReason::binary_required, marker);
    }
    const CBakedValueIndex instances = m_document.object_child(root, CStringView{ "instances" });
    if (!instances.is_valid())
    {
        return true;
    }
    if (m_document.value_type(instances) != EBakedValueType::object)
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_input, instances);
    }
    std::uint64_t cursor{}, scratch_cursor{};
    for (CBakedValueIndex group = m_document.first_child(instances); group.is_valid(); group = m_document.next_sibling(group))
    {
        if (!m_document.is_object_entry(group) || (m_document.value_type(group) != EBakedValueType::object))
        {
            return fail(diagnostic, EInstanceLoadReason::invalid_input, group);
        }
        const CSchemaIndex type = schema->find_type(m_document.name(group));
        SType layout;
        if (!type.is_valid() || !schema->type(type, layout))
        {
            return fail(diagnostic, EInstanceLoadReason::unknown_type, group);
        }
        TPodVector<SFrame> frames;
        SFrame frame{ m_document.first_child(group), k_no_parent };
        while (frame.next.is_valid() || (frames.size() != 0u))
        {
            if (!frame.next.is_valid())
            {
                if (!frames.pop_back(frame))
                {
                    return fail(diagnostic, EInstanceLoadReason::invalid_input, group);
                }
                continue;
            }
            const CBakedValueIndex instance = frame.next;
            frame.next = m_document.next_sibling(instance);
            if (!m_document.is_object_entry(instance) || (m_document.value_type(instance) != EBakedValueType::object))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_input, instance);
            }
            for (CBakedValueIndex property = m_document.first_child(instance); property.is_valid(); property = m_document.next_sibling(property))
            {
                const CStringView name = m_document.name(property);
                if (!instance_name_is(name, "locator") && !instance_name_is(name, "declaration") &&
                    !instance_name_is(name, "specialisation"))
                {
                    return fail(diagnostic, EInstanceLoadReason::unknown_property, property);
                }
            }
            const CBakedValueIndex declaration = m_document.object_child(instance, CStringView{ "declaration" });
            const CBakedValueIndex specialisations = m_document.object_child(instance, CStringView{ "specialisation" });
            const CBakedValueIndex locator = m_document.object_child(instance, CStringView{ "locator" });
            if (declaration.is_valid() && (stripped || (m_document.value_type(declaration) == EBakedValueType::null_value)))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_declaration, declaration);
            }
            if (specialisations.is_valid() && (m_document.value_type(specialisations) != EBakedValueType::object))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_input, specialisations);
            }
            if (!locator.is_valid() || (m_document.value_type(locator) != EBakedValueType::object))
            {
                return fail(diagnostic, EInstanceLoadReason::missing_property, instance);
            }
            for (CBakedValueIndex property = m_document.first_child(locator); property.is_valid(); property = m_document.next_sibling(property))
            {
                const CStringView name = m_document.name(property);
                if (!instance_name_is(name, "offset") && !instance_name_is(name, "valid") &&
                    !instance_name_is(name, "count") && !instance_name_is(name, "size"))
                {
                    return fail(diagnostic, EInstanceLoadReason::unknown_property, property);
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
                return fail(diagnostic, EInstanceLoadReason::invalid_locator, locator);
            }
            if (count_node.is_valid() && (!m_document.unsigned_integer_value(count_node, raw_count) || (raw_count != 1u)))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_count, count_node);
            }
            if (size_node.is_valid() && (!m_document.unsigned_integer_value(size_node, raw_size) || (raw_size != layout.size)))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_range, size_node);
            }
            if (supplied && !prior_valid)
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_locator, valid_node);
            }
            if (!supplied && !prior_valid && !m_mutable.is_ready())
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_locator, valid_node);
            }
            std::uint64_t offset = raw_offset;
            if (!prior_valid && !align_instance_offset(cursor, layout.alignment, offset))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_range, locator);
            }
            if (((offset < cursor) && ((layout.size != 0u) || (offset != 0u))) ||
                (offset > UINT32_MAX) || (offset > memory::k_byte_size_ceiling) ||
                ((offset & (layout.alignment - 1u)) != 0u) ||
                (layout.size > (memory::k_byte_size_ceiling - offset)))
            {
                const bool overlap = (offset < cursor) && ((layout.size != 0u) || (offset != 0u));
                return fail(diagnostic, (overlap ? EInstanceLoadReason::overlap : EInstanceLoadReason::invalid_range), locator);
            }
            const std::uint64_t end = offset + layout.size;
            if (supplied && (end > supplied_size))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_range, locator);
            }
            std::uint64_t scratch_offset{};
            if (!align_instance_offset(scratch_cursor, layout.alignment, scratch_offset) ||
                (layout.size > (memory::k_byte_size_ceiling - scratch_offset)))
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_range, instance);
            }
            if (records.size() >= k_no_parent)
            {
                return fail(diagnostic, EInstanceLoadReason::invalid_range, instance);
            }
            SRecord record;
            record.entry = instance;
            record.declaration = declaration;
            record.offset_node = offset_node;
            record.valid_node = valid_node;
            record.type = type;
            record.parent = frame.parent;
            record.offset = static_cast<std::uint32_t>(offset);
            record.scratch_offset = static_cast<std::uint32_t>(scratch_offset);
            record.extent = static_cast<std::uint32_t>(layout.size);
            record.prior_valid = prior_valid;
            if (!records.push_back(record))
            {
                return fail(diagnostic, EInstanceLoadReason::allocation_failed, instance);
            }
            if ((layout.size != 0u) || (offset != 0u))
            {
                cursor = end;
            }
            scratch_cursor = scratch_offset + layout.size;
            const CBakedValueIndex child = m_document.first_child(specialisations);
            if (child.is_valid())
            {
                if (!frames.push_back(frame))
                {
                    return fail(diagnostic, EInstanceLoadReason::allocation_failed, specialisations);
                }
                frame = { child, static_cast<std::uint32_t>(records.size() - 1u) };
            }
        }
    }
    payload_size = static_cast<std::size_t>(cursor);
    scratch_size = static_cast<std::size_t>(scratch_cursor);
    return true;
}

bool CBakedInstances::compare_embedded(const TPodVector<SRecord>& records, const CByteConstView& payload,
    const std::size_t scratch_size, SInstanceDiagnostic& diagnostic) const noexcept
{
    const CResolvedSchema* const schema = m_binding.resolved();
    detail::CDocumentRead document{ m_document };
    CByteBuffer expected;
    if ((scratch_size != 0u) && (!expected.allocate(scratch_size, 128u) || !expected.set_size(scratch_size)))
    {
        return fail(diagnostic, EInstanceLoadReason::allocation_failed, {});
    }
    for (std::size_t ordinal = 0u; ordinal < records.size(); ++ordinal)
    {
        const SRecord& record = records[ordinal];
        std::uint8_t* const target = (record.extent == 0u) ? nullptr : (expected.data() + record.scratch_offset);
        const std::uint8_t* const actual = (record.extent == 0u) ? nullptr : (payload.data() + record.offset);
        const detail::SOccurrence declaration{ record.declaration };
        detail::SValueDiagnostic value_error;
        bool converted{};
        if (record.parent == k_no_parent)
        {
            converted = detail::construct_value(*schema, document, record.type, declaration, target, record.extent,
                detail::EConstructionMode::instance, value_error);
        }
        else
        {
            const SRecord& parent = records[record.parent];
            const std::uint8_t* const base = (parent.extent == 0u) ? nullptr : (expected.data() + parent.scratch_offset);
            converted = detail::construct_alternative(*schema, document, record.type, declaration, base,
                parent.extent, target, record.extent, value_error);
        }
        if (!converted)
        {
            return fail(diagnostic, EInstanceLoadReason::invalid_declaration,
                (value_error.occurrence.is_baked() ? value_error.occurrence.baked : record.entry));
        }
        if (!detail::compare_encoded(*schema, record.type, target, record.extent, actual, record.extent))
        {
            return fail(diagnostic, EInstanceLoadReason::embedded_mismatch, record.entry);
        }
    }
    return true;
}

bool CBakedInstances::load_supplied(const CByteConstView& payload, const bool compare_embedded, SInstanceDiagnostic& diagnostic) noexcept
{
    clear_loaded();
    diagnostic = {};
    if (payload.is_ready() && ((reinterpret_cast<std::uintptr_t>(payload.data()) & 127u) != 0u))
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_range, {});
    }
    TPodVector<SRecord> staged;
    std::size_t payload_size{}, scratch_size{};
    if (!plan(true, payload.size(), staged, payload_size, scratch_size, diagnostic) || ((payload_size != 0u) && !payload.is_ready()))
    {
        return (diagnostic.reason == EInstanceLoadReason::none) ?
            fail(diagnostic, EInstanceLoadReason::invalid_range, {}) : false;
    }
    if (compare_embedded && !values_stripped() && !this->compare_embedded(staged, payload, scratch_size, diagnostic))
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

bool CBakedInstances::materialise(CByteBuffer& returned_owner, SInstanceDiagnostic& diagnostic) noexcept
{
    clear_loaded();
    diagnostic = {};
    if (returned_owner.is_ready())
    {
        return fail(diagnostic, EInstanceLoadReason::invalid_input, {});
    }
    TPodVector<SRecord> staged;
    std::size_t payload_size{}, scratch_size{};
    if (!plan(false, 0u, staged, payload_size, scratch_size, diagnostic))
    {
        return false;
    }
    CBakedRecordIndex index;
    if (!index.build(staged))
    {
        return fail(diagnostic, EInstanceLoadReason::allocation_failed, {});
    }
    CByteBuffer payload;
    if ((payload_size != 0u) && (!payload.allocate(payload_size, 128u) || !payload.set_size(payload_size)))
    {
        return fail(diagnostic, EInstanceLoadReason::allocation_failed, {});
    }
    const CResolvedSchema* const schema = m_binding.resolved();
    detail::CDocumentRead document{ m_document };
    for (std::size_t ordinal = 0u; ordinal < staged.size(); ++ordinal)
    {
        const SRecord& record = staged[ordinal];
        std::uint8_t* const target = (record.extent == 0u) ? nullptr : (payload.data() + record.offset);
        const detail::SOccurrence declaration{ record.declaration };
        detail::SValueDiagnostic value_error;
        bool converted{};
        if (record.parent == k_no_parent)
        {
            converted = detail::construct_value(*schema, document, record.type, declaration, target, record.extent,
                detail::EConstructionMode::instance, value_error);
        }
        else
        {
            const SRecord& parent = staged[record.parent];
            const std::uint8_t* const base = (parent.extent == 0u) ? nullptr : (payload.data() + parent.offset);
            converted = detail::construct_alternative(*schema, document, record.type, declaration, base,
                parent.extent, target, record.extent, value_error);
        }
        if (!converted)
        {
            return fail(diagnostic, EInstanceLoadReason::invalid_declaration,
                (value_error.occurrence.is_baked() ? value_error.occurrence.baked : record.entry));
        }
    }
    //  All conversions have succeeded. Publish only reserved scalar locators.
    for (std::size_t ordinal = 0u; ordinal < staged.size(); ++ordinal)
    {
        const SRecord& record = staged[ordinal];
        if (!record.prior_valid &&
            (!m_mutable.set_unsigned_integer_value(record.offset_node, record.offset) ||
             !m_mutable.set_boolean_value(record.valid_node, true)))
        {
            return fail(diagnostic, EInstanceLoadReason::invalid_locator, record.entry);
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
