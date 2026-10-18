
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_bulk_data.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Live bulk data ownership, capture, promotion, reconciliation and baked output.

#include "schema/live_bulk_data.hpp"
#include "data_model/document_copy.hpp"
#include "schema/baked_bulk_data_block.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
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

[[nodiscard]] static CNodeKey make_locator(CLiveDocument& document, const std::uint32_t offset,
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

template<class TSchema>
bool CLiveBulkData::switch_to(TSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept
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
    const CByteConstView payload = m_payload.const_view();
    if (mode == ESchemaSwitchMode::checked)
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

bool CLiveBulkData::switch_schema(CBakedSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept
{
    return switch_to(schema, mode, diagnostic);
}

bool CLiveBulkData::switch_schema(CLiveSchema& schema, const ESchemaSwitchMode mode, SBulkDataDiagnostic& diagnostic) noexcept
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
    const CResolvedSchema* const schema = m_binding.resolved();
    const CSchemaIndex type = record.type;
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
    const CResolvedSchema* const resolved = m_binding.resolved();
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
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || type.empty() || name.empty() || !schema)
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(type);
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
        const std::size_t required = static_cast<std::size_t>(offset) + extent;
        if (!m_payload.reserve(required, k_max_alignment) || !m_payload.set_size(required))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return {};
        }
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
        const CNodeKey old_data = m_document.object_child(entry_node, CStringView{ "data" });
        if (old_data.is_valid() && !m_document.erase(old_data))
        {
            disable();
            diagnostic.reason = EBulkDataLoadReason::invalid_input;
            return {};
        }
        m_records[record_ordinal] = { entry_node, group, type_index, count, offset, extent };
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
        if (!m_records.push_back({ entry_node, group, type_index, count, offset, extent }))
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
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || !document.contains(array) ||
        (document.value_kind(array) != EDocumentValueKind::array))
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(type);
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
    for (std::uint32_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        if (document.is_object_entry(item) && !
            ((layout.category == ECategory::structure) ? schema->find_member(type_index, document.name(item)) :
                ((layout.category == ECategory::bit_structure) ? schema->find_field(type_index, document.name(item)) : CSchemaIndex{})))
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

//  Output helpers keep all construction within the staged document.

class CBulkDataReconstructor
{
public:
    CBulkDataReconstructor(CLiveDocument& document, const CResolvedSchema& schema, EBulkDataLoadReason& reason) noexcept :
        m_document(document), m_schema(schema), m_reason(reason) {}

    [[nodiscard]] bool records(const CNodeKey entry, const CSchemaIndex type,
        const std::uint8_t* const bytes, const std::uint32_t count) noexcept;

private:
    [[nodiscard]] CNodeKey scalar(const CSchemaIndex type, const std::uint64_t bits,
        const unsigned width, const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey value(const CSchemaIndex type, const std::uint8_t* const bytes,
        const CStringView& name, const bool in_array, const unsigned depth) noexcept;

    CLiveDocument& m_document;
    const CResolvedSchema& m_schema;
    EBulkDataLoadReason& m_reason;
};

CNodeKey CBulkDataReconstructor::scalar(const CSchemaIndex type, const std::uint64_t bits,
    const unsigned width, const CStringView& name) noexcept
{
    EScalarDecodeReason decoded;
    const CNodeKey value = decode_document_scalar(m_document, m_schema, type, bits, width, name, decoded);
    if (!value)
    {
        m_reason = (decoded == EScalarDecodeReason::invalid_type) ? EBulkDataLoadReason::incompatible_schema :
            ((decoded == EScalarDecodeReason::unrepresentable_value) ? EBulkDataLoadReason::unrepresentable_value :
                EBulkDataLoadReason::allocation_failed);
    }
    return value;
}

CNodeKey CBulkDataReconstructor::value(const CSchemaIndex type, const std::uint8_t* const bytes,
    const CStringView& name, const bool in_array, const unsigned depth) noexcept
{
    SType layout;
    if ((depth >= 256u) || !m_schema.type(type, layout))
    {
        m_reason = EBulkDataLoadReason::incompatible_schema;
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        const CNodeKey decoded = scalar(type,
            read_scalar_bits(bytes, static_cast<std::size_t>(layout.size)),
            static_cast<unsigned>(layout.size * 8u), name);
        if (!decoded && (m_reason == EBulkDataLoadReason::none))
        {
            m_reason = EBulkDataLoadReason::allocation_failed;
        }
        return decoded;
    }

    //  Keep the established explicit positional output for anonymous
    //  one-member compounds; input also accepts named and scalar forms.
    const bool positional = in_array && (layout.count == 1u) &&
        ((layout.category == ECategory::structure) || (layout.category == ECategory::bit_structure));
    const bool array = (layout.category == ECategory::array) || positional;
    const CNodeKey result = array ? m_document.create_array(name) : m_document.create_object(name);
    if (!result)
    {
        m_reason = EBulkDataLoadReason::allocation_failed;
        return {};
    }
    for (std::uint32_t index = 0u; index < layout.count; ++index)
    {
        CNodeKey child;
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!m_schema.member(m_schema.member_at(type, index), member))
            {
                m_reason = EBulkDataLoadReason::incompatible_schema;
            }
            else
            {
                child = value(member.type, (bytes ? (bytes + member.offset) : nullptr),
                    (positional ? CStringView{} : m_schema.name(member.name)), positional, (depth + 1u));
            }
        }
        else if (layout.category == ECategory::array)
        {
            child = value(layout.element_or_storage,
                (bytes ? (bytes + (layout.stride * index)) : nullptr), {}, true, (depth + 1u));
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!m_schema.field(m_schema.field_at(type, index), field))
            {
                m_reason = EBulkDataLoadReason::incompatible_schema;
            }
            else
            {
                const std::uint64_t bits = (read_scalar_bits(bytes, static_cast<std::size_t>(layout.size)) & field.mask) >> field.shift;
                child = scalar(field.type, bits, field.width, (positional ? CStringView{} : m_schema.name(field.name)));
            }
        }
        else
        {
            m_reason = EBulkDataLoadReason::incompatible_schema;
        }
        if (!child || !document_translation::detail::CDocumentConstruction::append(m_document, result, child))
        {
            if (m_reason == EBulkDataLoadReason::none)
            {
                m_reason = EBulkDataLoadReason::allocation_failed;
            }
            (void)m_document.erase(result);
            return {};
        }
    }
    return result;
}

bool CBulkDataReconstructor::records(const CNodeKey entry, const CSchemaIndex type,
    const std::uint8_t* const bytes, const std::uint32_t count) noexcept
{
    SType layout;
    if (!m_schema.type(type, layout))
    {
        m_reason = EBulkDataLoadReason::incompatible_schema;
        return false;
    }

    //  Every explicit record needs at least one node; reject impossible expansions before traversal.
    if ((count > static_cast<std::uint32_t>(INT32_MAX)) ||
        (count > (memory::k_byte_size_ceiling / sizeof(CLiveNode))))
    {
        m_reason = EBulkDataLoadReason::invalid_range;
        return false;
    }
    const CNodeKey array = m_document.create_array(CStringView{ "data" });
    if (!document_translation::detail::CDocumentConstruction::append(m_document, entry, array))
    {
        m_reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    for (std::uint32_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        const std::uint8_t* const original = layout.size ? (bytes + (layout.size * ordinal)) : nullptr;
        const CNodeKey item = value(type, original, {}, true, 0u);
        if (!item || !document_translation::detail::CDocumentConstruction::append(m_document, array, item))
        {
            if (m_reason == EBulkDataLoadReason::none)
            {
                m_reason = EBulkDataLoadReason::allocation_failed;
            }
            return false;
        }
        detail::SValueDiagnostic error;
        bool matches{};
        if (!detail::check_value(m_schema, detail::CDocumentRead{ m_document }, type, detail::SOccurrence{ item },
            original, static_cast<std::size_t>(layout.size), detail::EConstructionMode::complete_bulk_data, matches, error) || !matches)
        {
            m_reason = detail::value_failure(error, EBulkDataLoadReason::unrepresentable_value);
            return false;
        }
    }
    return true;
}

//==============================================================================
//  Promotion
//==============================================================================

bool CLiveBulkData::promote_from(const CBakedBulkData& source, SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const original = source.m_binding.resolved();
    const CResolvedSchema* const target = m_binding.resolved();
    if (!source.loaded_ready() || !loaded_ready() || !original || !target)
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
    CBulkDataReconstructor reconstructor{ m_document, *target, diagnostic.reason };
    CTypeCompatibility compatibility{ *original, *target, ETypeMatch::representation };
    const CBakedDocument& baked = source.m_document;
    const bool stripped = source.values_stripped();
    const CBakedValueIndex source_data = baked.object_child(baked.root(), CStringView{ "data" });
    const CNodeKey destination_data = detail::SBulkDataHandleAccess::occurrence(data_root()).live;
    for (CBakedValueIndex group = baked.first_child(source_data); group.is_valid(); group = baked.next_sibling(group))
    {
        const CStringView type_name = baked.name(group);
        const CSchemaIndex source_type = original->find_type(type_name);
        const CSchemaIndex target_type = target->find_type(type_name);
        const auto match = compatibility.compare(source_type, target_type);
        if (match != ETypeMatchResult::match)
        {
            diagnostic.reason = (match == ETypeMatchResult::allocation_failed) ?
                EBulkDataLoadReason::allocation_failed : EBulkDataLoadReason::incompatible_schema;
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
                !m_records.push_back({ live_entry, live_group, target_type, record->count, record->offset, record->extent }))
            {
                diagnostic.reason = EBulkDataLoadReason::allocation_failed;
                return false;
            }
            if ((stripped || baked.object_child(baked_entry, CStringView{ "data" })) &&
                !reconstructor.records(live_entry, target_type,
                    (record->extent ? m_payload.data() + record->offset : nullptr), record->count))
            {
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

bool CBakedBulkData::promote(CLiveBulkData& destination, CBakedSchema& schema, SBulkDataDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
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

bool CBakedBulkData::promote(CLiveBulkData& destination, CLiveSchema& schema, SBulkDataDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CLiveBulkData staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
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

//==============================================================================
//  Binary reconciliation
//==============================================================================

bool CLiveBulkData::reconcile(SBulkDataDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema)
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CLiveDocument document;
    TPodVector<SRecord> records;
    if (!document.initialise() || !document.set_root_type(ELiveValueType::object) ||
        !document_translation::detail::CDocumentConstruction::append(document, document.root(), document.create_object(CStringView{ "data" })) ||
        !records.reserve(m_records.size()))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    CBulkDataReconstructor reconstructor{ document, *schema, diagnostic.reason };
    const CNodeKey root = document.object_child(document.root(), CStringView{ "data" });
    const CNodeKey old_root = detail::SBulkDataHandleAccess::occurrence(data_root()).live;
    struct SGroup { CNodeKey entry, group; };
    TPodVector<SGroup> groups;
    for (CNodeKey group = m_document.first_child(old_root); group; group = m_document.next_sibling(group))
    {
        const CNodeKey translated = document.create_object(m_document.name(group));
        if (!document_translation::detail::CDocumentConstruction::append(document, root, translated) || !groups.push_back({ group, translated }))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return false;
        }
    }
    if (groups.size() > 1u)
    {
        std::sort(groups.data(), (groups.data() + groups.size()),
            [](const SGroup& a, const SGroup& b)
            {
                return a.entry.query_value() < b.entry.query_value();
            });
    }
    std::size_t group_cursor{};
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        const SRecord& source = m_records[i];
        SBulkDataEntryView value;
        if (!observe(source, value))
        {
            diagnostic.reason = EBulkDataLoadReason::invalid_input;
            return false;
        }
        if (!find_near_record(groups, source.group, group_cursor))
        {
            diagnostic.reason = EBulkDataLoadReason::invalid_input;
            return false;
        }
        const CNodeKey group = groups[group_cursor].group;
        const CNodeKey entry = document.create_object(m_document.name(source.entry));
        const CNodeKey locator = make_locator(document, source.offset, source.count, source.extent);
        if (!entry || !document_translation::detail::CDocumentConstruction::append(document, entry, locator) || !document_translation::detail::CDocumentConstruction::append(document, group, entry) ||
            !records.push_back({ entry, group, source.type, source.count, source.offset, source.extent }))
        {
            diagnostic.reason = EBulkDataLoadReason::allocation_failed;
            return false;
        }
        if (!reconstructor.records(entry, value.type, value.bytes, value.count))
        {
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ source.entry });
            return false;
        }
    }
    m_document = std::move(document);
    m_records = std::move(records);
    return true;
}

//==============================================================================
//  Output preparation and baking
//==============================================================================

struct SBulkDataOutputRecord
{
    CNodeKey source, destination;
    CSchemaIndex type;
    const std::uint8_t* bytes{};
    std::uint32_t count{}, offset{}, extent{};
};

bool CLiveBulkData::prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
    const CResolvedSchema& destination_schema, const EDataOutputForm form,
    SBulkDataDiagnostic& diagnostic, SDataBlockDiagnostic* const block_diagnostic) const noexcept
{
    diagnostic = {};
    const CResolvedSchema* const source_schema = m_binding.resolved();
    if (!loaded_ready() || !source_schema || document.is_ready() || payload.is_ready() ||
        ((form != EDataOutputForm::embedded) && (form != EDataOutputForm::external) &&
            (form != EDataOutputForm::stripped)))
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
    if ((form == EDataOutputForm::stripped) &&
        !document_translation::detail::CDocumentConstruction::append(staged_document, staged_root, staged_document.create_boolean(true, CStringView{ "stripped" })))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    CTypeCompatibility compatibility{ *source_schema, destination_schema, ETypeMatch::representation };
    TPodVector<SBulkDataOutputRecord> records;
    std::uint32_t cursor{};
    const CNodeKey source_data = detail::SBulkDataHandleAccess::occurrence(data_root()).live;
    for (CNodeKey group = m_document.first_child(source_data); group; group = m_document.next_sibling(group))
    {
        if (!m_document.first_child(group))
        {
            continue;
        }
        const CStringView type_name = m_document.name(group);
        const CSchemaIndex source_type = source_schema->find_type(type_name);
        const CSchemaIndex destination_type = destination_schema.find_type(type_name);
        const auto match = compatibility.compare(source_type, destination_type);
        if (match != ETypeMatchResult::match)
        {
            diagnostic.reason = (match == ETypeMatchResult::allocation_failed) ?
                EBulkDataLoadReason::allocation_failed : EBulkDataLoadReason::incompatible_schema;
            diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ group });
            return false;
        }
        SType layout;
        if (!destination_schema.type(destination_type, layout))
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
                !records.push_back({ entry, output_entry, destination_type, value.bytes,
                    value.count, offset, static_cast<std::uint32_t>(value.byte_count) }))
            {
                diagnostic.reason = EBulkDataLoadReason::allocation_failed;
                return false;
            }
        }
    }
    if (form == EDataOutputForm::embedded)
    {
        CBulkDataReconstructor reconstructor{ staged_document, destination_schema, diagnostic.reason };
        for (std::size_t index = 0u; index < records.size(); ++index)
        {
            const SBulkDataOutputRecord& record = records[index];
            if (!reconstructor.records(record.destination, record.type, record.bytes, record.count))
            {
                diagnostic.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ record.source });
                return false;
            }
        }
    }
    CByteBuffer staged_payload;
    std::uint8_t* payload_bytes{};
    if (block_diagnostic)
    {
        if (!detail::prepare_data_block_storage(staged_document, cursor, detail::EDataBlockKind::bulk_data,
            staged_payload, payload_bytes, *block_diagnostic))
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

bool CLiveBulkData::prepare_output(CLiveDocument& document, CByteBuffer& payload,
    CBakedSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept
{
    if (!destination_schema.resolved_ready())
    {
        diagnostic = { EBulkDataLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, *destination_schema.resolved(), form, diagnostic);
}

bool CLiveBulkData::prepare_output(CLiveDocument& document, CByteBuffer& payload,
    CLiveSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept
{
    if (!destination_schema.resolved_ready())
    {
        diagnostic = { EBulkDataLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, *destination_schema.resolved(), form, diagnostic);
}

template<class TSchema>
bool CLiveBulkData::bake_to(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedBulkData& role,
    TSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (block.is_ready() || payload.is_ready() || role.document_ready() ||
        role.m_binding.is_attached() || !destination_schema.resolved_ready())
    {
        diagnostic.reason = EBulkDataLoadReason::invalid_input;
        return false;
    }
    CLiveDocument prepared;
    CByteBuffer prepared_payload;
    if (!prepare_output(prepared, prepared_payload, destination_schema, form, diagnostic))
    {
        return false;
    }
    CBakedDocumentBlock prepared_block;
    if (!document_translation::bake(prepared, prepared_block))
    {
        diagnostic.reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    CBakedBulkData prepared_role;
    if (!prepared_role.set_document(prepared_block.document()) ||
        !prepared_role.attach_schema(destination_schema, prepared_payload.const_view(), false, diagnostic))
    {
        if (diagnostic.reason == EBulkDataLoadReason::none)
        {
            diagnostic.reason = EBulkDataLoadReason::invalid_input;
        }
        diagnostic.occurrence = {};
        return false;
    }
    block = std::move(prepared_block);
    payload = std::move(prepared_payload);
    role.take_from(prepared_role);
    return true;
}

template<class TSchema>
bool CLiveBulkData::bake_to(CBakedBulkDataBlock& destination, TSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) const noexcept
{
    return detail::bake_data_block(*this, destination, schema, form, diagnostic,
        [&](CLiveDocument& document, CByteBuffer& storage) noexcept
        {
            return prepare_output_to(document, storage, *schema.resolved(), form, diagnostic.role, &diagnostic.block);
        });
}

bool CLiveBulkData::bake(CBakedBulkDataBlock& destination, CBakedSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) const noexcept
{
    return bake_to(destination, schema, form, diagnostic);
}

bool CLiveBulkData::bake(CBakedBulkDataBlock& destination, CLiveSchema& schema,
    const EDataOutputForm form, SBulkDataBlockDiagnostic& diagnostic) const noexcept
{
    return bake_to(destination, schema, form, diagnostic);
}

bool CLiveBulkData::bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedBulkData& role,
    CBakedSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept
{
    return bake_to(block, payload, role, destination_schema, form, diagnostic);
}

bool CLiveBulkData::bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedBulkData& role,
    CLiveSchema& destination_schema, const EDataOutputForm form, SBulkDataDiagnostic& diagnostic) const noexcept
{
    return bake_to(block, payload, role, destination_schema, form, diagnostic);
}

}   // namespace schema
