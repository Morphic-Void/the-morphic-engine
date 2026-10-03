
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Live instance ownership, queries and creation.

#include "schema/live_instances.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_copy.hpp"
#include "memory/memory_policies.hpp"
#include "debug/macros.hpp"

#include <cstring>
#include <utility>

namespace schema
{

//==============================================================================
//  Shared document construction
//==============================================================================

bool CLiveInstances::append_node(CLiveDocument& document, const CNodeKey parent, const CNodeKey child) noexcept
{
    if (!child.is_valid())
    {
        return false;
    }
    if (document.append_child(parent, child).succeeded())
    {
        return true;
    }
    if (document.contains(child) && document.is_detached(child))
    {
        (void)document.erase(child);
    }
    return false;
}

CNodeKey CLiveInstances::make_locator(CLiveDocument& document, const std::uint32_t offset, const std::uint32_t extent) noexcept
{
    const CNodeKey locator = document.create_object(CStringView{ "locator" });
    if (!locator)
    {
        return {};
    }
    if (!append_node(document, locator, document.create_unsigned_integer(offset, CStringView{ "offset" })) ||
        !append_node(document, locator, document.create_boolean(true, CStringView{ "valid" })) ||
        !append_node(document, locator, document.create_unsigned_integer(extent, CStringView{ "size" })))
    {
        (void)document.erase(locator);
        return {};
    }
    return locator;
}

struct SDetachedInstanceNodes
{
    CLiveDocument& document;
    CNodeKey group, entry;
    ~SDetachedInstanceNodes() noexcept
    {
        if (entry && document.contains(entry) && document.is_detached(entry))
        {
            (void)document.erase(entry);
        }
        if (group && document.contains(group) && document.is_detached(group))
        {
            (void)document.erase(group);
        }
    }
};

//==============================================================================
//  Lifetime and binding
//==============================================================================

bool CLiveInstances::initialise_document(const std::size_t capacity) noexcept
{
    return m_document.initialise(capacity) && m_document.set_root_type(ELiveValueType::object) &&
        append_node(m_document, m_document.root(), m_document.create_object(CStringView{ "instances" }));
}

bool CLiveInstances::initialise(CBakedSchema& schema, const std::size_t capacity) noexcept
{
    if (document_ready() || m_binding.is_attached() || !schema.resolved_ready())
    {
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise_document(capacity) || !staged.m_binding.bind(schema))
    {
        return false;
    }
    staged.m_loaded = true;
    take_from(staged);
    return true;
}

bool CLiveInstances::initialise(CLiveSchema& schema, const std::size_t capacity) noexcept
{
    if (document_ready() || m_binding.is_attached() || !schema.resolved_ready())
    {
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise_document(capacity) || !staged.m_binding.bind(schema))
    {
        return false;
    }
    staged.m_loaded = true;
    take_from(staged);
    return true;
}

void CLiveInstances::clear() noexcept
{
    m_loaded = false;
    m_records.deallocate();
    m_payload.deallocate();
    m_binding.release();
    m_document.deallocate();
}

void CLiveInstances::disable() noexcept
{
    m_loaded = false;
    MV_CRITICAL_EVENT("CLiveInstances partial mutation");
}

void CLiveInstances::take_from(CLiveInstances& source) noexcept
{
    m_document = std::move(source.m_document);
    m_payload = std::move(source.m_payload);
    m_records = std::move(source.m_records);
    m_binding = std::move(source.m_binding);
    m_loaded = source.m_loaded;
    source.m_loaded = false;
}

//==============================================================================
//  Queries and payload access
//==============================================================================

CInstanceDocumentQuery CLiveInstances::document_query() const noexcept
{
    return CInstanceDocumentQuery{ m_document };
}

CInstanceHandle CLiveInstances::instances_root() const noexcept
{
    const CInstanceDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "instances" });
}

bool CLiveInstances::record_index(const CInstanceHandle handle, std::uint32_t& result) const noexcept
{
    const detail::SOccurrence occurrence = detail::SInstanceHandleAccess::occurrence(handle);
    if (!loaded_ready() || !occurrence.is_live())
    {
        return false;
    }
    return find_live_record(m_records, occurrence.live, result);
}

CInstanceHandle CLiveInstances::find_base(const CStringView& type, const CStringView& name) const noexcept
{
    if (!loaded_ready())
    {
        return {};
    }
    const CInstanceDocumentQuery query = document_query();
    const CInstanceHandle found = query.object_child(query.object_child(instances_root(), type), name);
    std::uint32_t i{};
    return (record_index(found, i) && (m_records[i].parent == k_no_parent)) ? found : CInstanceHandle{};
}

CInstanceHandle CLiveInstances::find_specialisation(const CInstanceHandle parent, const CStringView& name) const noexcept
{
    std::uint32_t p{};
    if (!record_index(parent, p))
    {
        return {};
    }
    const CInstanceDocumentQuery query = document_query();
    const CInstanceHandle found = query.object_child(query.object_child(parent, CStringView{ "specialisation" }), name);
    std::uint32_t i{};
    return (record_index(found, i) && (m_records[i].parent == p)) ? found : CInstanceHandle{};
}

CInstanceHandle CLiveInstances::first_specialisation(const CInstanceHandle parent) const noexcept
{
    std::uint32_t p{};
    if (!record_index(parent, p))
    {
        return {};
    }
    const CInstanceDocumentQuery query = document_query();
    const CInstanceHandle found = query.first_child(query.object_child(parent, CStringView{ "specialisation" }));
    std::uint32_t i{};
    return (record_index(found, i) && (m_records[i].parent == p)) ? found : CInstanceHandle{};
}

CInstanceHandle CLiveInstances::next_specialisation(const CInstanceHandle current) const noexcept
{
    std::uint32_t c{};
    if (!record_index(current, c) || (m_records[c].parent == k_no_parent))
    {
        return {};
    }
    const CInstanceHandle found = document_query().next_sibling(current);
    std::uint32_t i{};
    return (record_index(found, i) && (m_records[i].parent == m_records[c].parent)) ? found : CInstanceHandle{};
}

CInstanceHandle CLiveInstances::parent_instance(const CInstanceHandle instance) const noexcept
{
    std::uint32_t i{};
    if (!record_index(instance, i) || (m_records[i].parent == k_no_parent))
    {
        return {};
    }
    return detail::SInstanceHandleAccess::make(detail::SOccurrence{ m_records[m_records[i].parent].entry });
}

bool CLiveInstances::entry(const CInstanceHandle handle, SInstanceEntryView& result) const noexcept
{
    std::uint32_t i{};
    if (!record_index(handle, i))
    {
        return false;
    }
    const SRecord& record = m_records[i];
    if (record.extent && (!m_payload.is_ready() || (record.offset > m_payload.size()) ||
        (record.extent > (m_payload.size() - record.offset))))
    {
        return false;
    }
    result = { record.type, ((record.parent == k_no_parent) ? CInstanceHandle{} :
        detail::SInstanceHandleAccess::make(detail::SOccurrence{ m_records[record.parent].entry })),
        detail::SInstanceHandleAccess::make(detail::SOccurrence{ record.declaration }),
        record.offset, record.extent, (record.extent ? (m_payload.data() + record.offset) : nullptr) };
    return true;
}

bool CLiveInstances::mutable_entry(const CInstanceHandle handle, SMutableInstanceEntryView& result) noexcept
{
    SInstanceEntryView value;
    if (!entry(handle, value))
    {
        return false;
    }
    SMutableInstanceEntryView mutable_value;
    mutable_value.type = value.type;
    mutable_value.parent = value.parent;
    mutable_value.declaration = value.declaration;
    mutable_value.offset = value.offset;
    mutable_value.byte_count = value.byte_count;
    if (value.byte_count != 0u)
    {
        mutable_value.bytes = m_payload.view().subview(value.offset, static_cast<std::size_t>(value.byte_count));
    }
    result = mutable_value;
    return true;
}

bool CLiveInstances::clear_unused_storage(const EUnusedBits bits) noexcept
{
    const CResolvedSchema* const resolved = m_binding.resolved();
    return loaded_ready() && resolved &&
        schema::clear_unused_storage(*resolved, document_query(), m_payload.view(), bits);
}

//==============================================================================
//  Instance creation
//==============================================================================

CInstanceHandle CLiveInstances::append_instance(const CSchemaIndex type, const std::uint32_t parent,
    const CStringView& type_name, const CStringView& name, const detail::CDocumentRead& source,
    const detail::SOccurrence declaration, const CByteConstView& complete,
    SInstanceDiagnostic& diagnostic) noexcept
{
    const CResolvedSchema* const schema = m_binding.resolved();
    SType layout;
    if (!loaded_ready() || !schema || !schema->type(type, layout) || name.empty() ||
        ((parent != k_no_parent) && (parent >= m_records.size())) ||
        (layout.alignment == 0u) || (layout.alignment > 128u) ||
        (layout.size > UINT32_MAX) || (m_records.size() >= k_no_parent) ||
        (layout.size && (!complete.is_ready() || (complete.size() < layout.size))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    const auto position = static_cast<std::uint32_t>(m_payload.size());
    const auto alignment = static_cast<std::uint32_t>(layout.alignment);
    const std::uint32_t offset = (position + alignment - 1u) & ~(alignment - 1u);
    if ((offset > memory::k_byte_size_ceiling) ||
        (layout.size > (memory::k_byte_size_ceiling - offset)))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return {};
    }
    CByteBuffer type_storage, name_storage;
    CStringView stable_type, stable_name;
    if (((parent == k_no_parent) && !document_translation::stabilise_document_name(type_name, type_storage, stable_type)) ||
        !document_translation::stabilise_document_name(name, name_storage, stable_name))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const CNodeKey instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    CNodeKey group = parent == k_no_parent ? m_document.object_child(instances, stable_type) :
        m_document.object_child(m_records[parent].entry, CStringView{ "specialisation" });
    const bool new_group = !group;
    if ((parent == k_no_parent) ? find_base(stable_type, stable_name).is_valid() :
        find_specialisation(detail::SInstanceHandleAccess::make(detail::SOccurrence{ m_records[parent].entry }), stable_name).is_valid())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    CByteBuffer stable;
    if (layout.size && (!stable.allocate(static_cast<std::size_t>(layout.size), 128u) ||
        !stable.set_size(static_cast<std::size_t>(layout.size))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    if (layout.size)
    {
        std::memcpy(stable.data(), complete.data(), static_cast<std::size_t>(layout.size));
    }
    if (new_group)
    {
        group = m_document.create_object((parent == k_no_parent) ? stable_type : CStringView{ "specialisation" });
    }
    const CNodeKey instance = m_document.create_object(stable_name);
    SDetachedInstanceNodes cleanup{ m_document, (new_group ? group : CNodeKey{}), instance };
    if (!group || !instance)
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const CNodeKey locator = make_locator(m_document, offset,
        static_cast<std::uint32_t>(layout.size));
    if (!append_node(m_document, instance, locator))
    {
        if (locator && m_document.is_detached(locator))
        {
            (void)m_document.erase(locator);
        }
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    CNodeKey copied_declaration;
    if (declaration.is_valid())
    {
        copied_declaration = document_translation::copy_subtree(m_document, source, declaration, CStringView{ "declaration" });
        if (!append_node(m_document, instance, copied_declaration))
        {
            if (copied_declaration && m_document.is_detached(copied_declaration))
            {
                (void)m_document.erase(copied_declaration);
            }
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return {};
        }
    }
    if (!m_records.reserve(m_records.size() + 1u) ||
        (((offset + layout.size) > position) &&
         (!m_payload.reserve(static_cast<std::size_t>(offset + layout.size), 128u) ||
          !m_payload.set_size(static_cast<std::size_t>(offset + layout.size)))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    if (layout.size)
    {
        std::memcpy((m_payload.data() + offset), stable.data(), static_cast<std::size_t>(layout.size));
    }
    const CNodeKey destination = (parent == k_no_parent) ? instances : m_records[parent].entry;
    if (new_group && !append_node(m_document, destination, group))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    if (!append_node(m_document, group, instance))
    {
        if (new_group)
        {
            (void)m_document.erase(group);
        }
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    cleanup.group = {};
    cleanup.entry = {};
    if (!m_records.push_back({ instance, copied_declaration, group, type, parent,
        offset, static_cast<std::uint32_t>(layout.size) }))
    {
        disable();
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    return detail::SInstanceHandleAccess::make(detail::SOccurrence{ instance });
}

CInstanceHandle CLiveInstances::create_base(const CStringView& type, const CStringView& name,
    const CInstanceDocumentQuery& source, const CInstanceHandle declaration,
    SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || type.empty() || name.empty())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(type);
    SType layout;
    if (!type_index || !schema->type(type_index, layout))
    {
        diagnostic.reason = EInstanceLoadReason::unknown_type;
        return {};
    }
    const CInstanceDocumentQuery fallback = document_query();
    const detail::CDocumentRead& document = declaration ? source.m_query : fallback.m_query;
    const detail::SOccurrence value = detail::SInstanceHandleAccess::occurrence(declaration);
    if (declaration && (!document.is_ready() || !document.contains(value)))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        diagnostic.occurrence = declaration;
        return {};
    }
    CByteBuffer encoded;
    if (layout.size && (!encoded.allocate(static_cast<std::size_t>(layout.size), 128u) ||
        !encoded.set_size(static_cast<std::size_t>(layout.size))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    detail::SValueDiagnostic error;
    if (!detail::construct_value(*schema, document, type_index, value, encoded.data(),
        encoded.size(), detail::EConstructionMode::instance, error))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        diagnostic.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
        return {};
    }
    return append_instance(type_index, k_no_parent, type, name, document, value, encoded.const_view(), diagnostic);
}

CInstanceHandle CLiveInstances::create_specialisation(const CInstanceHandle parent, const CStringView& name,
    const CInstanceDocumentQuery& source, const CInstanceHandle declaration, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t parent_index{};
    if (!record_index(parent, parent_index) || name.empty())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    const SRecord& parent_record = m_records[parent_index];
    const CResolvedSchema* const schema = m_binding.resolved();
    const CInstanceDocumentQuery fallback = document_query();
    const detail::CDocumentRead& document = declaration ? source.m_query : fallback.m_query;
    const detail::SOccurrence value = detail::SInstanceHandleAccess::occurrence(declaration);
    if (declaration && (!document.is_ready() || !document.contains(value)))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        diagnostic.occurrence = declaration;
        return {};
    }
    CByteBuffer encoded;
    if (parent_record.extent && (!encoded.allocate(parent_record.extent, 128u) ||
        !encoded.set_size(parent_record.extent)))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    detail::SValueDiagnostic error;
    if (!detail::construct_alternative(*schema, document, parent_record.type, value,
        (parent_record.extent ? (m_payload.data() + parent_record.offset) : nullptr), parent_record.extent,
        encoded.data(), encoded.size(), error))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        diagnostic.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
        return {};
    }
    return append_instance(parent_record.type, parent_index, {}, name, document, value, encoded.const_view(), diagnostic);
}

} // namespace schema
