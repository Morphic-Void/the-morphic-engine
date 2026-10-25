
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_bulk_data.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Live bulk data ownership, capture, promotion and baked output.

#include "schema/live_bulk_data.hpp"
#include "data_model/document_copy.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
#include "schema/data_reconstruction.hpp"
#include "schema/integer_notation.hpp"
#include "data_model/document_copy.hpp"
#include "data_model/document_translation.hpp"
#include "memory/memory_policies.hpp"
#include "debug/macros.hpp"

#include <cstring>
#include <utility>

namespace schema
{

//==============================================================================
//  Shared document construction
//==============================================================================

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

struct SDetachedBulkDataNodes
{
    CLiveDocument& document;
    CNodeKey group, entry, locator;

    ~SDetachedBulkDataNodes() noexcept
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

CNodeKey CLiveBulkData::make_locator(CLiveDocument& document, const std::uint32_t offset,
    const std::uint32_t count, const std::uint32_t extent) noexcept
{
    const CNodeKey locator = document.create_object(CStringView{ "locator" });
    if (!locator.is_valid() ||
        !append_node(document, locator, create_structural_integer(document, offset, CStringView{ "offset" })) ||
        !append_node(document, locator, document.create_boolean(true, CStringView{ "valid" })) ||
        !append_node(document, locator, create_structural_integer(document, count, CStringView{ "count" })) ||
        !append_node(document, locator, create_structural_integer(document, extent, CStringView{ "size" })))
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
        (layout.alignment > k_max_alignment) || (layout.size > UINT32_MAX) ||
        ((layout.size != 0u) && (count > (UINT32_MAX / layout.size))) ||
        ((layout.size != 0u) && (count > (memory::k_byte_size_ceiling / layout.size))))
    {
        return false;
    }
    extent = static_cast<std::uint32_t>(layout.size * count);
    return true;
}

//==============================================================================
//  Lifetime and binding
//==============================================================================

bool CLiveBulkData::initialise_document(const std::size_t initial_node_capacity) noexcept
{
    if (!m_document.initialise(initial_node_capacity) || !m_document.set_root_type(ELiveValueType::object))
    {
        return false;
    }
    return append_node(m_document, m_document.root(), m_document.create_object(CStringView{ "data" }));
}

bool CLiveBulkData::initialise(CBakedTypes& schema, const std::size_t initial_node_capacity) noexcept
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

bool CLiveBulkData::initialise(CLiveTypes& schema, const std::size_t initial_node_capacity) noexcept
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

template<class TSchema>
bool CLiveBulkData::switch_to(TSchema& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept
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

bool CLiveBulkData::rebind_types(CBakedTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

bool CLiveBulkData::rebind_types(CLiveTypes& schema, const ETypeBindMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
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

//==============================================================================
//  Queries and payload access
//==============================================================================

CBulkDataDocumentQuery CLiveBulkData::document_query() const noexcept
{
    return CBulkDataDocumentQuery{ m_document };
}

CBulkDataHandle CLiveBulkData::data_root() const noexcept
{
    const CBulkDataDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "data" });
}

bool CLiveBulkData::record_index(const CBulkDataHandle handle, std::size_t& index) const noexcept
{
    const detail::SOccurrence occurrence = detail::SBulkDataHandleAccess::occurrence(handle);
    if (!loaded_ready() || !occurrence.is_live())
    {
        return false;
    }
    return find_live_record(m_records, occurrence.live, index);
}

CBulkDataHandle CLiveBulkData::find_entry(const CStringView& type, const CStringView& name) const noexcept
{
    if (!loaded_ready())
    {
        return {};
    }
    const CBulkDataDocumentQuery query = document_query();
    const CBulkDataHandle found = query.object_child(query.object_child(data_root(), type), name);
    std::size_t index{};
    return record_index(found, index) ? found : CBulkDataHandle{};
}

bool CLiveBulkData::observe(const SRecord& record, SBulkDataEntryView& result) const noexcept
{
    const CResolvedTypes* const schema = m_binding.resolved();
    const CTypesIndex type = record.type;
    SType layout;
    if (!type.is_valid() || !schema->type(type, layout) ||
        (layout.size * record.count != record.extent) ||
        ((record.extent != 0u) && (!m_payload.is_ready() ||
            (record.offset > m_payload.size()) || (record.extent > (m_payload.size() - record.offset)))))
    {
        return false;
    }
    SBulkDataEntryView value;
    value.type = type;
    value.count = record.count;
    value.offset = record.offset;
    value.stride = layout.size;
    value.byte_count = record.extent;
    value.bytes = (record.extent != 0u) ? (m_payload.data() + record.offset) : nullptr;
    result = value;
    return true;
}

bool CLiveBulkData::entry(const CBulkDataHandle handle, SBulkDataEntryView& result) const noexcept
{
    std::size_t index{};
    return record_index(handle, index) && observe(m_records[index], result);
}

bool CLiveBulkData::mutable_entry(const CBulkDataHandle handle, SMutableBulkDataEntryView& result) noexcept
{
    SBulkDataEntryView value;
    if (!entry(handle, value))
    {
        return false;
    }
    SMutableBulkDataEntryView mutable_value;
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

bool CLiveBulkData::clear_unused_storage(const EUnusedBits bits) noexcept
{
    const CResolvedTypes* const resolved = m_binding.resolved();
    return loaded_ready() && resolved && schema::clear_unused_storage(*resolved, document_query(), m_payload.view(), bits);
}

//==============================================================================
//  Capture and entry editing
//==============================================================================

CBulkDataHandle CLiveBulkData::store(const CStringView& type, const CStringView& name,
    const CByteConstView& source, const std::uint32_t count, const bool unpopulated,
    const bool replace, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedTypes* const schema = m_binding.resolved();
    if (!loaded_ready() || type.empty() || name.empty() || !schema)
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return {};
    }
    const CTypesIndex type_index = schema->find_type(type);
    SType layout;
    std::uint32_t extent{};
    if (!type_index.is_valid() || !schema->type(type_index, layout))
    {
        diagnostic.reason = EBulkDataLoadReason::unknown_type;
        return {};
    }
    if (!extent_for(layout, count, extent))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_range;
        return {};
    }
    if (!unpopulated && (extent != 0u) && (!source.is_ready() || (source.size() < extent) ||
        ((reinterpret_cast<std::uintptr_t>(source.data()) & (layout.alignment - 1u)) != 0u)))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_range;
        return {};
    }
    CByteBuffer type_storage, name_storage;
    CStringView stable_type, stable_name;
    if (!document_translation::stabilise_document_name(type, type_storage, stable_type) ||
        !document_translation::stabilise_document_name(name, name_storage, stable_name))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return {};
    }
    const CBulkDataHandle existing = find_entry(stable_type, stable_name);
    if (existing.is_valid() && !replace)
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return {};
    }
    const CNodeKey data = detail::SBulkDataHandleAccess::occurrence(data_root()).live;
    CNodeKey group = m_document.object_child(data, stable_type);
    const bool new_group = !group.is_valid();
    std::size_t record_ordinal{};
    const bool replacing = existing.is_valid() && record_index(existing, record_ordinal);
    const SRecord prior = replacing ? m_records[record_ordinal] : SRecord{};
    const bool reuse = replacing && (extent <= prior.extent) && (extent != 0u);
    std::uint32_t offset = 0u;
    if (extent != 0u)
    {
        if (reuse)
        {
            offset = prior.offset;
        }
        else
        {
            const auto position = static_cast<std::uint32_t>(m_payload.size());
            const auto alignment = static_cast<std::uint32_t>(layout.alignment);
            offset = (position + alignment - 1u) & ~(alignment - 1u);
            if ((offset > memory::k_byte_size_ceiling) || (extent > (memory::k_byte_size_ceiling - offset)))
            {
                diagnostic.reason = EBulkDataLoadReason::invalid_range;
                return {};
            }
        }
    }
    if (new_group)
    {
        group = m_document.create_object(stable_type);
        if (!group.is_valid())
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return {};
        }
    }
    CNodeKey entry_node = replacing ? prior.entry : m_document.create_object(stable_name);
    CNodeKey locator = make_locator(m_document, offset, count, extent);
    SDetachedBulkDataNodes cleanup{ m_document, new_group ? group : CNodeKey{}, (replacing ? CNodeKey{} : entry_node), locator };
    if (!entry_node.is_valid() || !locator.is_valid() ||
        (!replacing && (!append_node(m_document, entry_node, locator) || !m_records.reserve(m_records.size() + 1u))))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return {};
    }
    CByteBuffer staged;
    if (!unpopulated && (extent != 0u))
    {
        if (!staged.allocate(extent, k_max_alignment) || !staged.set_size(extent))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return {};
        }
        std::memcpy(staged.data(), source.data(), extent);
    }
    if ((extent != 0u) && !reuse)
    {
        const std::size_t previous_size = m_payload.size();
        const std::size_t required = static_cast<std::size_t>(offset) + extent;
        if (!m_payload.reserve(required, k_max_alignment) || !m_payload.set_size(required))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return {};
        }
        if (offset > previous_size) std::memset(m_payload.data() + previous_size, 0, offset - previous_size);
    }
    if (replacing)
    {
        const CNodeKey old_locator = m_document.object_child(entry_node, CStringView{ "locator" });
        const CNodeKey old_payload = m_document.detach_payload(old_locator);
        if (!old_payload.is_valid())
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return {};
        }
        if (!m_document.set_name(locator, CStringView{}) ||
            !m_document.attach_payload(old_locator, locator).is_valid())
        {
            disable();
            diagnostic.reason = EBulkDataLoadReason::invalid_locator;
            return {};
        }
        (void)m_document.erase(old_payload);
        m_records[record_ordinal] = { entry_node, type_index, count, offset, extent };
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
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return {};
        }
        if (!m_records.push_back({ entry_node, type_index, count, offset, extent }))
        {
            disable();
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
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
    return detail::SBulkDataHandleAccess::make(detail::SOccurrence{ entry_node });
}

CBulkDataHandle CLiveBulkData::create_records(const CStringView& type, const CStringView& name,
    const CBulkDataDocumentQuery& source_query, const CBulkDataHandle records_array, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const detail::SOccurrence array = detail::SBulkDataHandleAccess::occurrence(records_array);
    const detail::CDocumentRead& document = source_query.m_query;
    const CResolvedTypes* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || !document.contains(array) ||
        (document.value_kind(array) != EDocumentValueKind::array))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return {};
    }
    const CTypesIndex type_index = schema->find_type(type);
    SType layout;
    const std::uint32_t count = document.child_count(array);
    std::uint32_t extent{};
    if (!type_index.is_valid() || !schema->type(type_index, layout))
    {
        diagnostic.reason = EBulkDataLoadReason::unknown_type;
        return {};
    }
    if (!extent_for(layout, count, extent))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_range;
        return {};
    }
    CByteBuffer bytes;
    if (extent != 0u && (!bytes.allocate(extent, k_max_alignment) || !bytes.set_size(extent)))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return {};
    }
    detail::SOccurrence item = document.first_child(array);
    if (extent) std::memset(bytes.data(), 0, extent);
    for (std::uint32_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        if (document.is_object_entry(item) && !
            ((layout.category == ECategory::structure) ? schema->find_member(type_index, document.name(item)) :
                ((layout.category == ECategory::bit_structure) ? schema->find_field(type_index, document.name(item)) : CTypesIndex{})))
        {
            diagnostic.reason = EBulkDataLoadReason::invalid_input;
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(item);
            return {};
        }
        detail::SValueDiagnostic value_error;
        if (!detail::construct_value(*schema, document, type_index, item,
            (layout.size == 0u) ? nullptr : (bytes.data() + (layout.size * ordinal)),
            static_cast<std::size_t>(layout.size), detail::EConstructionMode::complete_bulk_data, value_error, document.is_object_entry(item)))
        {
            diagnostic.reason = detail::value_failure(value_error, EBulkDataLoadReason::incomplete_record);
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(value_error.occurrence.is_valid() ? value_error.occurrence : item);
            return {};
        }
        item = document.next_sibling(item);
    }
    return store(type, name, bytes.const_view(), count, false, false, diagnostic);
}

CBulkDataHandle CLiveBulkData::capture(const CStringView& type, const CStringView& name,
    const CByteConstView& source, const std::uint32_t count, SBulkDataDiagnostic& diagnostic) noexcept
{
    return store(type, name, source, count, false, true, diagnostic);
}

CBulkDataHandle CLiveBulkData::create_unpopulated(const CStringView& type, const CStringView& name,
    const std::uint32_t count, SBulkDataDiagnostic& diagnostic) noexcept
{
    return store(type, name, {}, count, true, false, diagnostic);
}

bool CLiveBulkData::rename_entry(const CBulkDataHandle handle, const CStringView& name) noexcept
{
    std::size_t index{};
    return !name.empty() && record_index(handle, index) && m_document.set_name(m_records[index].entry, name);
}

bool CLiveBulkData::erase_entry(const CBulkDataHandle handle) noexcept
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
    if (!m_records.erase(index))
    {
        disable();
        return false;
    }
    return true;
}

//==============================================================================
//  Binary-to-document reconstruction
//==============================================================================

//==============================================================================
//  Promotion
//==============================================================================

bool CLiveBulkData::promote_from(const CBakedBulkData& source, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedTypes* const types = m_binding.resolved();
    if (!source.loaded_ready() || !loaded_ready() || !types || source.m_binding.resolved() != types)
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    if (!m_records.reserve(source.m_records.size()))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size() != 0u)
    {
        if (!m_payload.allocate(source.m_payload.size(), k_max_alignment) || !m_payload.set_size(source.m_payload.size()))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return false;
        }
        std::memcpy(m_payload.data(), source.m_payload.data(), source.m_payload.size());
    }
    const CBakedDocument& baked = source.m_document;
    const CBakedValueIndex source_data = baked.object_child(baked.root(), CStringView{ "data" });
    const CNodeKey destination_data = detail::SBulkDataHandleAccess::occurrence(data_root()).live;
    for (CBakedValueIndex group = baked.first_child(source_data); group.is_valid(); group = baked.next_sibling(group))
    {
        const CStringView type_name = baked.name(group);
        const CTypesIndex type = types->find_type(type_name);
        SType layout;
        if (!types->type(type, layout))
        {
            diagnostic.reason = EBulkDataLoadReason::incompatible_schema;
            return false;
        }
        const CNodeKey live_group = m_document.create_object(type_name);
        if (!document_translation::detail::CDocumentConstruction::append(m_document, destination_data, live_group))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return false;
        }
        for (CBakedValueIndex baked_entry = baked.first_child(group); baked_entry.is_valid(); baked_entry = baked.next_sibling(baked_entry))
        {
            const CBakedBulkData::SRecord* record = nullptr;
            std::uint32_t ordinal{};
            if (source.m_record_index.find(source.m_records, baked_entry, ordinal))
            {
                record = &source.m_records[ordinal];
            }
            if (record == nullptr)
            {
                diagnostic.reason = EBulkDataLoadReason::invalid_input;
                return false;
            }
            const CNodeKey live_entry = m_document.create_object(baked.name(baked_entry));
            const CNodeKey locator = make_locator(m_document, record->offset, record->count, record->extent);
            if (!live_entry.is_valid() || !document_translation::detail::CDocumentConstruction::append(m_document, live_entry, locator) ||
                !document_translation::detail::CDocumentConstruction::append(m_document, live_group, live_entry) ||
                !m_records.push_back({ live_entry, type, record->count, record->offset, record->extent }))
            {
                diagnostic.reason = EBulkDataLoadReason::allocation_failed;
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

bool CBakedBulkData::promote(CLiveBulkData& destination, SBulkDataDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise_document(0u) || !staged.m_binding.bind(m_binding))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    staged.m_loaded = true;
    if (!staged.promote_from(*this, diagnostic)) return false;
    destination.take_from(staged);
    return true;
}

//==============================================================================
//  Output preparation and baking
//==============================================================================

struct SBulkDataOutputRecord
{
    const std::uint8_t* bytes{};
    std::uint32_t offset{}, extent{};
};

bool CLiveBulkData::prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
    SBulkDataDiagnostic& diagnostic, SDataBlockDiagnostic* const block_diagnostic) const noexcept
{
    diagnostic = {};
    const CResolvedTypes* const source_schema = m_binding.resolved();
    if (!loaded_ready() || !source_schema || document.is_ready() || payload.is_ready())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CLiveDocument staged_document;
    const CNodeKey staged_root = (staged_document.initialise() &&
        staged_document.set_root_type(ELiveValueType::object)) ? staged_document.root() : CNodeKey{};
    const CNodeKey data = staged_root ? staged_document.create_object(CStringView{ "data" }) : CNodeKey{};
    if (!staged_root || !document_translation::detail::CDocumentConstruction::append(staged_document, staged_root, data))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    if (!document_translation::detail::CDocumentConstruction::append(staged_document, staged_root, staged_document.create_boolean(true, CStringView{ "stripped" })))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    TPodVector<SBulkDataOutputRecord> records;
    std::uint32_t cursor{};
    const CNodeKey source_data = detail::SBulkDataHandleAccess::occurrence(data_root()).live;
    for (CNodeKey group = m_document.first_child(source_data); group; group = m_document.next_sibling(group))
    {
        const CStringView type_name = m_document.name(group);
        const CTypesIndex source_type = source_schema->find_type(type_name);
        SType layout;
        if (!source_schema->type(source_type, layout))
        {
            diagnostic.reason = EBulkDataLoadReason::incompatible_schema;
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ group });
            return false;
        }
        const CNodeKey output_group = staged_document.create_object(type_name);
        if (!document_translation::detail::CDocumentConstruction::append(staged_document, data, output_group))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return false;
        }
        for (CNodeKey entry = m_document.first_child(group); entry; entry = m_document.next_sibling(entry))
        {
            const SRecord* source_record = nullptr;
            std::uint32_t index{};
            if (find_live_record(m_records, entry, index))
            {
                source_record = &m_records[index];
            }
            SBulkDataEntryView value;
            if (!source_record || !observe(*source_record, value) || (value.count == 0u) || (value.byte_count > UINT32_MAX))
            {
                diagnostic.reason = EBulkDataLoadReason::invalid_input;
                diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ entry });
                return false;
            }
            std::uint32_t offset{};
            if (value.byte_count != 0u)
            {
                const auto alignment = static_cast<std::uint32_t>(layout.alignment);
                offset = (cursor + alignment - 1u) & ~(alignment - 1u);
                if ((offset > memory::k_byte_size_ceiling) ||
                    (value.byte_count > (memory::k_byte_size_ceiling - offset)))
                {
                    diagnostic.reason = EBulkDataLoadReason::invalid_range;
                    diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ entry });
                    return false;
                }
                cursor = offset + static_cast<std::uint32_t>(value.byte_count);
            }
            const CNodeKey output_entry = staged_document.create_object(m_document.name(entry));
            const CNodeKey locator = make_locator(staged_document, offset,
                value.count, static_cast<std::uint32_t>(value.byte_count));
            if (!output_entry || !document_translation::detail::CDocumentConstruction::append(staged_document, output_entry, locator) ||
                !document_translation::detail::CDocumentConstruction::append(staged_document, output_group, output_entry) ||
                !records.push_back({ value.bytes, offset, static_cast<std::uint32_t>(value.byte_count) }))
            {
                diagnostic.reason = EBulkDataLoadReason::allocation_failed;
                return false;
            }
        }
    }
    CByteBuffer staged_payload;
    std::uint8_t* payload_bytes{};
    if (block_diagnostic)
    {
        if (!detail::prepare_data_block_storage(staged_document, cursor, detail::EDataDomain::bulk_data, staged_payload, payload_bytes, *block_diagnostic))
        {
            return false;
        }
    }
    else if ((cursor != 0u) && (!staged_payload.allocate(static_cast<std::size_t>(cursor), k_max_alignment) ||
        !staged_payload.set_size(static_cast<std::size_t>(cursor))))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    if (!block_diagnostic) payload_bytes = staged_payload.data();
    std::size_t copied_end{};
    for (std::size_t index = 0u; index < records.size(); ++index)
    {
        const SBulkDataOutputRecord& record = records[index];
        if (record.extent != 0u)
        {   //  Clear only newly created gaps; complete records retain their bytes.
            if (record.offset > copied_end)
            {
                std::memset((payload_bytes + copied_end), 0, (record.offset - copied_end));
            }
            std::memcpy((payload_bytes + record.offset), record.bytes, record.extent);
            copied_end = static_cast<std::size_t>(record.offset) + record.extent;
        }
    }
    document = std::move(staged_document);
    payload = std::move(staged_payload);
    return true;
}

bool CLiveBulkData::prepare_output(CLiveDocument& document, CByteBuffer& payload, SBulkDataDiagnostic& diagnostic) const noexcept
{
    if (!loaded_ready())
    {
        diagnostic = { EBulkDataLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, diagnostic);
}

bool CLiveBulkData::bake(CBakedDataBlock& destination, SBulkDataBlockDiagnostic& diagnostic) const noexcept
{
    return detail::bake_runtime_block(*this, destination, diagnostic,
        [&](CLiveDocument& document, CByteBuffer& storage) noexcept
        {
            return prepare_output_to(document, storage, diagnostic.role, &diagnostic.block);
        });
}

}   // namespace schema
