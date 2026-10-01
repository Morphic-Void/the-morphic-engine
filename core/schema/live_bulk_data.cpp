
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_bulk_data.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Live bulk construction, capture and non-consuming baked promotion.

#include "schema/live_bulk_data.hpp"
#include "schema/value_codec.hpp"
#include "memory/memory_policies.hpp"
#include "debug/macros.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace schema
{

struct SCompatiblePair
{
    CSchemaIndex source, destination;
};

[[nodiscard]] static bool add_pair(TPodVector<SCompatiblePair>& pending, const SCompatiblePair pair) noexcept
{
    for (std::size_t index = 0u; index < pending.size(); ++index)
    {
        if ((pending[index].source == pair.source) && (pending[index].destination == pair.destination))
        {
            return true;
        }
    }
    return pending.push_back(pair);
}

[[nodiscard]] static bool same_scalar(const SScalar& lhs, const SScalar& rhs) noexcept
{
    if (lhs.kind != rhs.kind)
    {
        return false;
    }
    if (lhs.kind == EScalar::signed_integer)
    {
        return lhs.value.signed_value == rhs.value.signed_value;
    }
    if (lhs.kind == EScalar::floating_point)
    {
        std::uint64_t left{}, right{};
        std::memcpy(&left, &lhs.value.floating_value, sizeof(left));
        std::memcpy(&right, &rhs.value.floating_value, sizeof(right));
        return left == right;
    }
    return lhs.value.unsigned_value == rhs.value.unsigned_value;
}

[[nodiscard]] static bool same_type_representation(const CResolvedSchema& source, const CResolvedSchema& destination,
    const CSchemaIndex source_type, const CSchemaIndex destination_type, bool& allocation_failed) noexcept
{
    TPodVector<SCompatiblePair> pending;
    if (!add_pair(pending, { source_type, destination_type }))
    {
        allocation_failed = true;
        return false;
    }
    for (std::size_t next = 0u; next < pending.size(); ++next)
    {
        const SCompatiblePair pair = pending[next];
        SType left, right;
        if (!source.type(pair.source, left) || !destination.type(pair.destination, right) ||
            (left.category != right.category) || (left.primitive != right.primitive) ||
            (left.size != right.size) || (left.alignment != right.alignment) ||
            (left.stride != right.stride) || (left.count != right.count) ||
            (left.gaps != right.gaps) ||
            (left.named_components != right.named_components) ||
            !(source.name(left.name) == destination.name(right.name)))
        {
            return false;
        }
        if (left.element_or_storage.is_valid() != right.element_or_storage.is_valid())
        {
            return false;
        }
        if (left.element_or_storage.is_valid() && !add_pair(pending, { left.element_or_storage, right.element_or_storage }))
        {
            allocation_failed = true;
            return false;
        }
        const std::uint32_t child_count = (left.category == ECategory::structure ||
            left.category == ECategory::enumeration || left.category == ECategory::bit_structure) ? left.count : 0u;
        for (std::uint32_t index = 0u; index < child_count; ++index)
        {
            const CSchemaIndex left_member = source.member_at(pair.source, index);
            const CSchemaIndex right_member = destination.member_at(pair.destination, index);
            const CSchemaIndex left_label = source.label_at(pair.source, index);
            const CSchemaIndex right_label = destination.label_at(pair.destination, index);
            const CSchemaIndex left_field = source.field_at(pair.source, index);
            const CSchemaIndex right_field = destination.field_at(pair.destination, index);
            if ((left_member.is_valid() != right_member.is_valid()) ||
                (left_label.is_valid() != right_label.is_valid()) ||
                (left_field.is_valid() != right_field.is_valid()))
            {
                return false;
            }
            if (left_member.is_valid())
            {
                SMember a, b;
                if (!source.member(left_member, a) || !destination.member(right_member, b) ||
                    !(source.name(a.name) == destination.name(b.name)) ||
                    (a.offset != b.offset) || (a.size != b.size))
                {
                    return false;
                }
                if (!add_pair(pending, { a.type, b.type }))
                {
                    allocation_failed = true;
                    return false;
                }
            }
            if (left_label.is_valid())
            {
                SLabel a, b;
                if (!source.label(left_label, a) || !destination.label(right_label, b) ||
                    !(source.name(a.name) == destination.name(b.name)) || !same_scalar(a.value, b.value))
                {
                    return false;
                }
            }
            if (left_field.is_valid())
            {
                SField a, b;
                if (!source.field(left_field, a) || !destination.field(right_field, b) ||
                    !(source.name(a.name) == destination.name(b.name)) || (a.mask != b.mask) ||
                    (a.shift != b.shift) || (a.width != b.width) ||
                    (a.signed_value != b.signed_value) || (a.interpretation != b.interpretation) ||
                    (a.primitive != b.primitive))
                {
                    return false;
                }
                if (!add_pair(pending, { a.type, b.type }))
                {
                    allocation_failed = true;
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] static bool copy_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept
{
    if (source.empty())
    {
        return false;
    }
    if (source.length() == 0u)
    {
        copied = CStringView{ "" };
        return true;
    }
    if (!storage.allocate(source.length(), 1u) || !storage.set_size(source.length()))
    {
        return false;
    }
    std::memcpy(storage.data(), source.string(), source.length());
    copied = CStringView{ storage.data(), source.length() };
    return true;
}

[[nodiscard]] static bool append_node(CLiveDocument& document, const CNodeKey parent, const CNodeKey child) noexcept
{
    if (!child.is_valid())
    {
        return false;
    }
    if (document.append_child(parent, child).succeeded())
    {
        return true;
    }
    (void)document.erase(child);
    return false;
}

struct SDetachedBulkNodes
{
    CLiveDocument& document;
    CNodeKey group, entry, locator;

    ~SDetachedBulkNodes() noexcept
    {
        if (entry.is_valid() && document.contains(entry) && document.is_detached(entry))
        {
            (void)document.erase(entry);
        }
        if (group.is_valid() && document.contains(group) && document.is_detached(group))
        {
            (void)document.erase(group);
        }
        if (locator.is_valid() && document.contains(locator) && document.is_detached(locator))
        {
            (void)document.erase(locator);
        }
    }
};

[[nodiscard]] static CNodeKey make_locator(CLiveDocument& document, const std::uint32_t offset,
    const std::uint32_t count, const std::uint64_t extent) noexcept
{
    const CNodeKey locator = document.create_object(CStringView{ "locator" });
    if (!locator.is_valid() ||
        !append_node(document, locator, document.create_unsigned_integer(offset, CStringView{ "offset" })) ||
        !append_node(document, locator, document.create_boolean(true, CStringView{ "valid" })) ||
        !append_node(document, locator, document.create_unsigned_integer(count, CStringView{ "count" })) ||
        !append_node(document, locator, document.create_unsigned_integer(extent, CStringView{ "size" })))
    {
        if (locator.is_valid())
        {
            (void)document.erase(locator);
        }
        return {};
    }
    return locator;
}

[[nodiscard]] static bool extent_for(const SType& layout, const std::uint32_t count, std::uint32_t& extent) noexcept
{
    if ((layout.alignment == 0u) || ((layout.alignment & (layout.alignment - 1u)) != 0u) ||
        (layout.alignment > 128u) || (layout.size > UINT32_MAX) ||
        ((layout.size != 0u) && (count > (UINT32_MAX / layout.size))) ||
        ((layout.size != 0u) && (count > (memory::k_byte_size_ceiling / layout.size))))
    {
        return false;
    }
    extent = static_cast<std::uint32_t>(layout.size * count);
    return true;
}

bool CLiveBulkData::initialise_document(const std::size_t initial_node_capacity) noexcept
{
    if (!m_document.initialise(initial_node_capacity) || !m_document.set_root_type(ELiveValueType::object))
    {
        return false;
    }
    return append_node(m_document, m_document.root(), m_document.create_object(CStringView{ "data" }));
}

bool CLiveBulkData::initialise(CBakedSchema& schema, const std::size_t initial_node_capacity) noexcept
{
    if (document_ready() || m_binding.is_attached() || !schema.resolved_ready())
    {
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise_document(initial_node_capacity) || !staged.m_binding.bind(schema))
    {
        return false;
    }
    m_document = std::move(staged.m_document);
    m_binding = std::move(staged.m_binding);
    m_loaded = true;
    return true;
}

bool CLiveBulkData::initialise(CLiveSchema& schema, const std::size_t initial_node_capacity) noexcept
{
    if (document_ready() || m_binding.is_attached() || !schema.resolved_ready())
    {
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise_document(initial_node_capacity) || !staged.m_binding.bind(schema))
    {
        return false;
    }
    m_document = std::move(staged.m_document);
    m_binding = std::move(staged.m_binding);
    m_loaded = true;
    return true;
}

void CLiveBulkData::clear() noexcept
{
    m_loaded = false;
    m_records.deallocate();
    m_payload.deallocate();
    m_binding.release();
    m_document.deallocate();
}

void CLiveBulkData::disable() noexcept
{
    m_loaded = false;
    MV_CRITICAL_EVENT("CLiveBulkData partial mutation");
}

CBulkDocumentQuery CLiveBulkData::document_query() const noexcept
{
    return CBulkDocumentQuery{ m_document };
}

CBulkHandle CLiveBulkData::data_root() const noexcept
{
    const CBulkDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "data" });
}

bool CLiveBulkData::record_index(const CBulkHandle handle, std::size_t& index) const noexcept
{
    const detail::SOccurrence occurrence = detail::SBulkHandleAccess::occurrence(handle);
    if (!loaded_ready() || !occurrence.is_live())
    {
        return false;
    }
    for (std::size_t ordinal = 0u; ordinal < m_records.size(); ++ordinal)
    {
        if (m_records[ordinal].entry == occurrence.live)
        {
            index = ordinal;
            return true;
        }
    }
    return false;
}

CBulkHandle CLiveBulkData::find_entry(const CStringView& type, const CStringView& name) const noexcept
{
    if (!loaded_ready())
    {
        return {};
    }
    const CBulkDocumentQuery query = document_query();
    const CBulkHandle found = query.object_child(query.object_child(data_root(), type), name);
    std::size_t index{};
    return record_index(found, index) ? found : CBulkHandle{};
}

bool CLiveBulkData::observe(const SRecord& record, SBulkEntryView& result) const noexcept
{
    const CResolvedSchema* const schema = m_binding.resolved();
    const CSchemaIndex type = schema->find_type(m_document.name(record.group));
    SType layout;
    if (!type.is_valid() || !schema->type(type, layout) ||
        (layout.size * record.count != record.extent) ||
        ((record.extent != 0u) && (!m_payload.is_ready() ||
            (record.offset > m_payload.size()) || (record.extent > (m_payload.size() - record.offset)))))
    {
        return false;
    }
    SBulkEntryView value;
    value.type = type;
    value.count = record.count;
    value.offset = record.offset;
    value.stride = layout.size;
    value.byte_count = record.extent;
    value.bytes = (record.extent != 0u) ? (m_payload.data() + record.offset) : nullptr;
    result = value;
    return true;
}

bool CLiveBulkData::entry(const CBulkHandle handle, SBulkEntryView& result) const noexcept
{
    std::size_t index{};
    return record_index(handle, index) && observe(m_records[index], result);
}

bool CLiveBulkData::mutable_entry(const CBulkHandle handle, SMutableBulkEntryView& result) noexcept
{
    SBulkEntryView value;
    if (!entry(handle, value))
    {
        return false;
    }
    SMutableBulkEntryView mutable_value;
    mutable_value.type = value.type;
    mutable_value.count = value.count;
    mutable_value.offset = value.offset;
    mutable_value.stride = value.stride;
    mutable_value.byte_count = value.byte_count;
    if (value.byte_count != 0u)
    {
        mutable_value.bytes = m_payload.view().subview(value.offset, static_cast<std::size_t>(value.byte_count));
    }
    result = mutable_value;
    return true;
}

CBulkHandle CLiveBulkData::store(const CStringView& type, const CStringView& name,
    const CByteConstView& source, const std::uint32_t count, const bool unpopulated,
    const bool replace, SBulkDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || type.empty() || name.empty() || !schema)
    {
        diagnostic.reason = EBulkLoadReason::invalid_input;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(type);
    SType layout;
    std::uint32_t extent{};
    if (!type_index.is_valid() || !schema->type(type_index, layout))
    {
        diagnostic.reason = EBulkLoadReason::unknown_type;
        return {};
    }
    if (!extent_for(layout, count, extent))
    {
        diagnostic.reason = EBulkLoadReason::invalid_range;
        return {};
    }
    if (!unpopulated && (extent != 0u) && (!source.is_ready() || (source.size() < extent) ||
        ((reinterpret_cast<std::uintptr_t>(source.data()) & (layout.alignment - 1u)) != 0u)))
    {
        diagnostic.reason = EBulkLoadReason::invalid_range;
        return {};
    }
    CByteBuffer type_storage, name_storage;
    CStringView stable_type, stable_name;
    if (!copy_name(type, type_storage, stable_type) || !copy_name(name, name_storage, stable_name))
    {
        diagnostic.reason = EBulkLoadReason::allocation_failed;
        return {};
    }
    const CBulkHandle existing = find_entry(stable_type, stable_name);
    if (existing.is_valid() && !replace)
    {
        diagnostic.reason = EBulkLoadReason::invalid_input;
        return {};
    }
    const CNodeKey data = detail::SBulkHandleAccess::occurrence(data_root()).live;
    CNodeKey group = m_document.object_child(data, stable_type);
    const bool new_group = !group.is_valid();
    std::size_t record_ordinal{};
    const bool replacing = existing.is_valid() && record_index(existing, record_ordinal);
    const SRecord prior = replacing ? m_records[record_ordinal] : SRecord{};
    const bool reuse = replacing && (extent <= prior.extent) && (extent != 0u);
    std::uint64_t offset = 0u;
    if (extent != 0u)
    {
        if (reuse)
        {
            offset = prior.offset;
        }
        else
        {
            const std::uint64_t position = m_payload.size();
            offset = (position + layout.alignment - 1u) & ~(layout.alignment - 1u);
            if ((offset > UINT32_MAX) || (extent > (UINT32_MAX - offset)) ||
                (offset > memory::k_byte_size_ceiling) || (extent > (memory::k_byte_size_ceiling - offset)))
            {
                diagnostic.reason = EBulkLoadReason::invalid_range;
                return {};
            }
        }
    }
    if (new_group)
    {
        group = m_document.create_object(stable_type);
        if (!group.is_valid())
        {
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return {};
        }
    }
    CNodeKey entry_node = replacing ? prior.entry : m_document.create_object(stable_name);
    CNodeKey locator = make_locator(m_document, static_cast<std::uint32_t>(offset), count, extent);
    SDetachedBulkNodes cleanup{ m_document, new_group ? group : CNodeKey{}, (replacing ? CNodeKey{} : entry_node), locator };
    if (!entry_node.is_valid() || !locator.is_valid() ||
        (!replacing && (!append_node(m_document, entry_node, locator) || !m_records.reserve(m_records.size() + 1u))))
    {
        diagnostic.reason = EBulkLoadReason::allocation_failed;
        return {};
    }
    CByteBuffer staged;
    if (!unpopulated && (extent != 0u))
    {
        if (!staged.allocate(extent, 128u) || !staged.set_size(extent))
        {
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return {};
        }
        std::memcpy(staged.data(), source.data(), extent);
    }
    if ((extent != 0u) && !reuse)
    {
        const std::size_t required = static_cast<std::size_t>(offset) + extent;
        if (!m_payload.reserve(required, 128u) || !m_payload.set_size(required))
        {
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return {};
        }
    }
    if (replacing)
    {
        const CNodeKey old_locator = m_document.object_child(entry_node, CStringView{ "locator" });
        const CNodeKey old_payload = m_document.detach_payload(old_locator);
        if (!old_payload.is_valid())
        {
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return {};
        }
        if (!m_document.set_name(locator, CStringView{}) ||
            !m_document.attach_payload(old_locator, locator).is_valid())
        {
            disable();
            diagnostic.reason = EBulkLoadReason::invalid_locator;
            return {};
        }
        (void)m_document.erase(old_payload);
        m_records[record_ordinal] = { entry_node, group, count, static_cast<std::uint32_t>(offset), extent };
    }
    else
    {
        if ((new_group && !append_node(m_document, data, group)) ||
            !append_node(m_document, group, entry_node))
        {
            if (new_group && m_document.contains(group) && !m_document.is_detached(group))
            {
                (void)m_document.erase(group);
            }
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return {};
        }
        if (!m_records.push_back({ entry_node, group, count, static_cast<std::uint32_t>(offset), extent }))
        {
            disable();
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return {};
        }
    }
    if (extent != 0u && !unpopulated)
    {
        std::memcpy(m_payload.data() + offset, staged.data(), extent);
    }
    cleanup.group = {};
    cleanup.entry = {};
    cleanup.locator = {};
    return detail::SBulkHandleAccess::make(detail::SOccurrence{ entry_node });
}

CBulkHandle CLiveBulkData::capture(const CStringView& type, const CStringView& name,
    const CByteConstView& source, const std::uint32_t count, SBulkDiagnostic& diagnostic) noexcept
{
    return store(type, name, source, count, false, true, diagnostic);
}

CBulkHandle CLiveBulkData::create_unpopulated(const CStringView& type, const CStringView& name,
    const std::uint32_t count, SBulkDiagnostic& diagnostic) noexcept
{
    return store(type, name, {}, count, true, false, diagnostic);
}

CBulkHandle CLiveBulkData::create_records(const CStringView& type, const CStringView& name,
    const CBulkDocumentQuery& source_query, const CBulkHandle records_array, SBulkDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const detail::SOccurrence array = detail::SBulkHandleAccess::occurrence(records_array);
    const detail::CDocumentRead& document = source_query.m_query;
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || !document.contains(array) ||
        (document.value_kind(array) != EDocumentValueKind::array))
    {
        diagnostic.reason = EBulkLoadReason::invalid_input;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(type);
    SType layout;
    const std::uint32_t count = document.child_count(array);
    std::uint32_t extent{};
    if (!type_index.is_valid() || !schema->type(type_index, layout))
    {
        diagnostic.reason = EBulkLoadReason::unknown_type;
        return {};
    }
    if (!extent_for(layout, count, extent))
    {
        diagnostic.reason = EBulkLoadReason::invalid_range;
        return {};
    }
    CByteBuffer bytes;
    if (extent != 0u && (!bytes.allocate(extent, 128u) || !bytes.set_size(extent)))
    {
        diagnostic.reason = EBulkLoadReason::allocation_failed;
        return {};
    }
    detail::SOccurrence item = document.first_child(array);
    for (std::uint32_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        if (document.is_object_entry(item))
        {
            diagnostic.reason = EBulkLoadReason::invalid_input;
            diagnostic.occurrence = detail::SBulkHandleAccess::make(item);
            return {};
        }
        detail::SValueDiagnostic value_error;
        if (!detail::construct_value(*schema, document, type_index, item,
            (layout.size == 0u) ? nullptr : (bytes.data() + (layout.size * ordinal)),
            static_cast<std::size_t>(layout.size), detail::EConstructionMode::complete_bulk, value_error))
        {
            diagnostic.reason = EBulkLoadReason::incomplete_record;
            diagnostic.occurrence = detail::SBulkHandleAccess::make(value_error.occurrence.is_valid() ? value_error.occurrence : item);
            return {};
        }
        item = document.next_sibling(item);
    }
    return store(type, name, bytes.const_view(), count, false, false, diagnostic);
}

bool CLiveBulkData::rename_entry(const CBulkHandle handle, const CStringView& name) noexcept
{
    std::size_t index{};
    return !name.empty() && record_index(handle, index) && m_document.set_name(m_records[index].entry, name);
}

bool CLiveBulkData::erase_entry(const CBulkHandle handle) noexcept
{
    std::size_t index{};
    if (!record_index(handle, index))
    {
        return false;
    }
    if (!m_document.erase(m_records[index].entry))
    {
        disable();
        return false;
    }
    return m_records.erase(index);
}

bool CLiveBulkData::promote_from(const CBakedBulkData& source, SBulkDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const original = source.m_binding.resolved();
    const CResolvedSchema* const target = m_binding.resolved();
    if (!source.loaded_ready() || !loaded_ready() || !original || !target)
    {
        diagnostic.reason = EBulkLoadReason::invalid_input;
        return false;
    }
    if (!m_records.reserve(source.m_records.size()))
    {
        diagnostic.reason = EBulkLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size() != 0u)
    {
        if (!m_payload.allocate(source.m_payload.size(), 128u) || !m_payload.set_size(source.m_payload.size()))
        {
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return false;
        }
        std::memcpy(m_payload.data(), source.m_payload.data(), source.m_payload.size());
    }
    const CBakedDocument& baked = source.m_document;
    const CBakedValueIndex source_data = baked.object_child(baked.root(), CStringView{ "data" });
    const CNodeKey destination_data = detail::SBulkHandleAccess::occurrence(data_root()).live;
    for (CBakedValueIndex group = baked.first_child(source_data); group.is_valid(); group = baked.next_sibling(group))
    {
        const CStringView type_name = baked.name(group);
        const CSchemaIndex source_type = original->find_type(type_name);
        const CSchemaIndex target_type = target->find_type(type_name);
        bool allocation_failed{};
        if (!source_type.is_valid() || !target_type.is_valid() ||
            !same_type_representation(*original, *target, source_type, target_type, allocation_failed))
        {
            diagnostic.reason = allocation_failed ? EBulkLoadReason::allocation_failed : EBulkLoadReason::incompatible_schema;
            return false;
        }
        const CNodeKey live_group = m_document.create_object(type_name);
        if (!append_node(m_document, destination_data, live_group))
        {
            diagnostic.reason = EBulkLoadReason::allocation_failed;
            return false;
        }
        for (CBakedValueIndex baked_entry = baked.first_child(group); baked_entry.is_valid(); baked_entry = baked.next_sibling(baked_entry))
        {
            const CBakedBulkData::SRecord* record = nullptr;
            for (std::size_t ordinal = 0u; ordinal < source.m_records.size(); ++ordinal)
            {
                if (source.m_records[ordinal].entry == baked_entry)
                {
                    record = &source.m_records[ordinal];
                    break;
                }
            }
            if (record == nullptr)
            {
                diagnostic.reason = EBulkLoadReason::invalid_input;
                return false;
            }
            const CNodeKey live_entry = m_document.create_object(baked.name(baked_entry));
            const CNodeKey locator = make_locator(m_document, record->offset, record->count, record->extent);
            if (!live_entry.is_valid() || !append_node(m_document, live_entry, locator) ||
                !append_node(m_document, live_group, live_entry) ||
                !m_records.push_back({ live_entry, live_group, record->count, record->offset, record->extent }))
            {
                diagnostic.reason = EBulkLoadReason::allocation_failed;
                return false;
            }
        }
    }
    return true;
}

void CLiveBulkData::take_from(CLiveBulkData& source) noexcept
{
    m_document = std::move(source.m_document);
    m_payload = std::move(source.m_payload);
    m_records = std::move(source.m_records);
    m_binding = std::move(source.m_binding);
    m_loaded = true;
    source.m_loaded = false;
}

bool CBakedBulkData::promote(CLiveBulkData& destination, CBakedSchema& schema, SBulkDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EBulkLoadReason::invalid_input;
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EBulkLoadReason::allocation_failed;
        return false;
    }
    if (!staged.promote_from(*this, diagnostic))
    {
        diagnostic.occurrence = {};
        return false;
    }
    destination.take_from(staged);
    return true;
}

bool CBakedBulkData::promote(CLiveBulkData& destination, CLiveSchema& schema, SBulkDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EBulkLoadReason::invalid_input;
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EBulkLoadReason::allocation_failed;
        return false;
    }
    if (!staged.promote_from(*this, diagnostic))
    {
        diagnostic.occurrence = {};
        return false;
    }
    destination.take_from(staged);
    return true;
}

}   // namespace schema
