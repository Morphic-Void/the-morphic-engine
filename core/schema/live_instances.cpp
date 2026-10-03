
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Owned live instance documents and independent complete snapshots.

#include "schema/live_instances.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_copy.hpp"
#include "data_model/document_translation.hpp"
#include "memory/memory_policies.hpp"
#include "debug/macros.hpp"

#include <cstring>
#include <utility>

namespace schema
{

[[nodiscard]] static bool append(CLiveDocument& document, const CNodeKey parent, const CNodeKey child) noexcept
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

[[nodiscard]] static CNodeKey make_locator(CLiveDocument& document, const std::uint32_t offset, const std::uint32_t extent) noexcept
{
    const CNodeKey locator = document.create_object(CStringView{ "locator" });
    if (!locator)
    {
        return {};
    }
    if (!append(document, locator, document.create_unsigned_integer(offset, CStringView{ "offset" })) ||
        !append(document, locator, document.create_boolean(true, CStringView{ "valid" })) ||
        !append(document, locator, document.create_unsigned_integer(extent, CStringView{ "size" })))
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

bool CLiveInstances::initialise_document(const std::size_t capacity) noexcept
{
    return m_document.initialise(capacity) && m_document.set_root_type(ELiveValueType::object) &&
        append(m_document, m_document.root(), m_document.create_object(CStringView{ "instances" }));
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
    if (!append(m_document, instance, locator))
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
        if (!append(m_document, instance, copied_declaration))
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
    if (new_group && !append(m_document, destination, group))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    if (!append(m_document, group, instance))
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

void CLiveInstances::take_from(CLiveInstances& source) noexcept
{
    m_document = std::move(source.m_document);
    m_payload = std::move(source.m_payload);
    m_records = std::move(source.m_records);
    m_binding = std::move(source.m_binding);
    m_loaded = source.m_loaded;
    source.m_loaded = false;
}

bool CLiveInstances::promote_from(const CBakedInstances& source, SInstanceDiagnostic& diagnostic) noexcept
{
    const CResolvedSchema* const original = source.m_binding.resolved();
    const CResolvedSchema* const target = m_binding.resolved();
    if (!source.loaded_ready() || !loaded_ready() || !original || !target)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CTypeCompatibility compatibility{ *original, *target, ETypeMatch::representation };
    const CBakedValueIndex instances = source.m_document.object_child(source.m_document.root(), CStringView{ "instances" });
    const CNodeKey live_instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    for (CBakedValueIndex group = source.m_document.first_child(instances); group; group = source.m_document.next_sibling(group))
    {
        const CStringView name = source.m_document.name(group);
        const CSchemaIndex source_type = original->find_type(name);
        const CSchemaIndex destination_type = target->find_type(name);
        const auto match = compatibility.compare(source_type, destination_type);
        if (match != ETypeMatchResult::match)
        {
            diagnostic.reason = (match == ETypeMatchResult::allocation_failed) ?
                EInstanceLoadReason::allocation_failed : EInstanceLoadReason::incompatible_schema;
            return false;
        }
        if (!append(m_document, live_instances, m_document.create_object(name)))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    if (!m_records.reserve(source.m_records.size()))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size() &&
        (!m_payload.allocate(source.m_payload.size(), 128u) || !m_payload.set_size(source.m_payload.size())))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size())
    {
        std::memcpy(m_payload.data(), source.m_payload.data(), source.m_payload.size());
    }
    for (std::size_t i = 0u; i < source.m_records.size(); ++i)
    {
        const CBakedInstances::SRecord& record = source.m_records[i];
        const CBakedValueIndex baked_group = source.m_document.parent((record.parent == CBakedInstances::k_no_parent) ?
            record.entry : source.m_records[record.parent].entry);
        CNodeKey group{};
        if (record.parent == CBakedInstances::k_no_parent)
        {
            group = m_document.object_child(live_instances, source.m_document.name(baked_group));
        }
        else
        {
            group = m_document.object_child(m_records[record.parent].entry, CStringView{ "specialisation" });
            if (!group)
            {
                group = m_document.create_object(CStringView{ "specialisation" });
                if (!append(m_document, m_records[record.parent].entry, group))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
            }
        }
        const CSchemaIndex type = (record.parent == CBakedInstances::k_no_parent) ?
            target->find_type(source.m_document.name(baked_group)) : m_records[record.parent].type;
        const CNodeKey entry = m_document.create_object(source.m_document.name(record.entry));
        const CNodeKey locator = make_locator(m_document, record.offset, record.extent);
        SRecord promoted{ entry, {}, group, type, record.parent, record.offset, record.extent };
        promoted.declaration = output_declaration(m_document, promoted, *target, type, diagnostic.reason);
        if (diagnostic.reason != EInstanceLoadReason::none)
        {
            return false;
        }
        if (!entry || !append(m_document, entry, locator) ||
            (promoted.declaration && !append(m_document, entry, promoted.declaration)) ||
            !append(m_document, group, entry) || !m_records.push_back(promoted))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    return true;
}

bool CLiveInstances::reconcile(SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveDocument document;
    TPodVector<SRecord> records;
    if (!document.initialise() || !document.set_root_type(ELiveValueType::object) ||
        !append(document, document.root(), document.create_object(CStringView{ "instances" })) ||
        !records.reserve(m_records.size()))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey root = document.object_child(document.root(), CStringView{ "instances" });
    const CNodeKey old_root = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    for (CNodeKey group = m_document.first_child(old_root); group; group = m_document.next_sibling(group))
    {
        if (!append(document, root, document.create_object(m_document.name(group))))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        const SRecord& source = m_records[i];
        CNodeKey group;
        if (source.parent == k_no_parent)
        {
            group = document.object_child(root, m_document.name(source.group));
        }
        else
        {
            const CNodeKey parent = records[source.parent].entry;
            group = document.object_child(parent, CStringView{ "specialisation" });
            if (!group)
            {
                group = document.create_object(CStringView{ "specialisation" });
                if (!append(document, parent, group))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
            }
        }
        const CNodeKey entry = document.create_object(m_document.name(source.entry));
        const CNodeKey locator = make_locator(document, source.offset, source.extent);
        const CNodeKey declaration = output_declaration(document, source, *schema, source.type, diagnostic.reason);
        if (diagnostic.reason != EInstanceLoadReason::none)
        {
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source.entry });
            return false;
        }
        if (!entry || !append(document, entry, locator) || (declaration && !append(document, entry, declaration)) ||
            !append(document, group, entry) ||
            !records.push_back({ entry, declaration, group, source.type, source.parent, source.offset, source.extent }))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    m_document = std::move(document);
    m_records = std::move(records);
    return true;
}

bool CBakedInstances::promote(CLiveInstances& destination, CBakedSchema& schema, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
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

bool CBakedInstances::promote(CLiveInstances& destination, CLiveSchema& schema, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
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

struct SInstanceOutputRecord
{
    std::uint32_t source{}, offset{}, extent{};
};

struct SInstanceOutputFrame
{
    CNodeKey next, group;
    std::uint32_t parent{ UINT32_MAX };
};

bool CLiveInstances::prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
    const CResolvedSchema& destination_schema, const EDataOutputForm form,
    SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    const CResolvedSchema* const source_schema = m_binding.resolved();
    if (!loaded_ready() || !source_schema || document.is_ready() || payload.is_ready() ||
        ((form != EDataOutputForm::embedded) && (form != EDataOutputForm::external) && (form != EDataOutputForm::stripped)))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveDocument staged_document;
    if (!staged_document.initialise() || !staged_document.set_root_type(ELiveValueType::object) ||
        !append(staged_document, staged_document.root(), staged_document.create_object(CStringView{ "instances" })))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey output_instances = staged_document.object_child(staged_document.root(), CStringView{ "instances" });
    if ((form == EDataOutputForm::stripped) &&
        !append(staged_document, staged_document.root(), staged_document.create_boolean(true, CStringView{ "stripped" })))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey source_instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    CTypeCompatibility compatibility{ *source_schema, destination_schema, ETypeMatch::representation };
    TPodVector<SInstanceOutputRecord> output_records;
    std::uint32_t cursor{};
    for (CNodeKey source_group = m_document.first_child(source_instances); source_group; source_group = m_document.next_sibling(source_group))
    {
        const CStringView type_name = m_document.name(source_group);
        const CSchemaIndex source_type = source_schema->find_type(type_name);
        const CSchemaIndex output_type = destination_schema.find_type(type_name);
        const auto match = compatibility.compare(source_type, output_type);
        if (match != ETypeMatchResult::match)
        {
            diagnostic.reason = (match == ETypeMatchResult::allocation_failed) ?
                EInstanceLoadReason::allocation_failed : EInstanceLoadReason::incompatible_schema;
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_group });
            return false;
        }
        const CNodeKey output_group = staged_document.create_object(type_name);
        if (!append(staged_document, output_instances, output_group))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        TPodVector<SInstanceOutputFrame> frames;
        SInstanceOutputFrame frame{ m_document.first_child(source_group), output_group, k_no_parent };
        while (frame.next || (frames.size() != 0u))
        {
            if (!frame.next)
            {
                if (!frames.pop_back(frame))
                {
                    diagnostic.reason = EInstanceLoadReason::invalid_input;
                    return false;
                }
                continue;
            }
            const CNodeKey source_entry = frame.next;
            frame.next = m_document.next_sibling(source_entry);
            const SRecord* record = nullptr;
            std::uint32_t record_index{};
            if (find_live_record(m_records, source_entry, record_index))
            {
                record = &m_records[record_index];
            }
            if (!record || (record->type != source_type) ||
                ((frame.parent == k_no_parent) ? (record->parent != k_no_parent) :
                    (record->parent != output_records[frame.parent].source)))
            {
                diagnostic.reason = EInstanceLoadReason::invalid_input;
                diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                return false;
            }
            SType layout;
            if (!destination_schema.type(output_type, layout) || (layout.size != record->extent) ||
                (layout.alignment == 0u) || (layout.alignment > 128u))
            {
                diagnostic.reason = EInstanceLoadReason::incompatible_schema;
                diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                return false;
            }
            std::uint32_t offset{};
            if (record->extent != 0u)
            {
                const auto alignment = static_cast<std::uint32_t>(layout.alignment);
                offset = (cursor + alignment - 1u) & ~(alignment - 1u);
                if ((offset > memory::k_byte_size_ceiling) ||
                    (record->extent > (memory::k_byte_size_ceiling - offset)))
                {
                    diagnostic.reason = EInstanceLoadReason::invalid_range;
                    diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                    return false;
                }
                cursor = offset + record->extent;
            }
            const CNodeKey output_entry = staged_document.create_object(m_document.name(source_entry));
            const CNodeKey locator = make_locator(staged_document, offset, record->extent);
            if (!output_entry || !append(staged_document, output_entry, locator))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            const CNodeKey declaration = (form == EDataOutputForm::stripped) ? CNodeKey{} :
                output_declaration(staged_document, *record, destination_schema, output_type, diagnostic.reason);
            if (diagnostic.reason != EInstanceLoadReason::none)
            {
                diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                return false;
            }
            if (declaration && !append(staged_document, output_entry, declaration))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            if (!append(staged_document, frame.group, output_entry) ||
                !output_records.push_back({ record_index, offset, record->extent }))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            const CNodeKey source_children = m_document.object_child(source_entry, CStringView{ "specialisation" });
            if (source_children && m_document.first_child(source_children))
            {
                const CNodeKey output_children = staged_document.create_object(CStringView{ "specialisation" });
                if (!append(staged_document, output_entry, output_children) || !frames.push_back(frame))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
                frame = { m_document.first_child(source_children), output_children,
                    static_cast<std::uint32_t>(output_records.size() - 1u) };
            }
        }
    }
    CByteBuffer staged_payload;
    if ((cursor != 0u) && (!staged_payload.allocate(static_cast<std::size_t>(cursor), 128u) ||
        !staged_payload.set_size(static_cast<std::size_t>(cursor))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    for (std::size_t i = 0u; i < output_records.size(); ++i)
    {
        const SInstanceOutputRecord& record = output_records[i];
        if (record.extent != 0u)
        {
            const SRecord& original = m_records[record.source];
            std::memcpy((staged_payload.data() + record.offset), (m_payload.data() + original.offset), record.extent);
        }
    }
    document = std::move(staged_document);
    payload = std::move(staged_payload);
    return true;
}

bool CLiveInstances::prepare_output(CLiveDocument& document, CByteBuffer& payload,
    CBakedSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    if (!destination_schema.resolved_ready())
    {
        diagnostic = { EInstanceLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, *destination_schema.resolved(), form, diagnostic);
}

bool CLiveInstances::prepare_output(CLiveDocument& document, CByteBuffer& payload,
    CLiveSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    if (!destination_schema.resolved_ready())
    {
        diagnostic = { EInstanceLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, *destination_schema.resolved(), form, diagnostic);
}

template <class TSchema>
bool CLiveInstances::bake_to(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
    TSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (block.is_ready() || payload.is_ready() || role.document_ready() ||
        role.m_binding.is_attached() || !destination_schema.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
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
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    CBakedInstances prepared_role;
    if (!prepared_role.set_document(prepared_block.document()) ||
        !prepared_role.bind_schema(destination_schema) ||
        !prepared_role.load_supplied(prepared_payload.const_view(), false, diagnostic))
    {
        if (diagnostic.reason == EInstanceLoadReason::none)
        {
            diagnostic.reason = EInstanceLoadReason::invalid_input;
        }
        diagnostic.occurrence = {};
        return false;
    }
    block = std::move(prepared_block);
    payload = std::move(prepared_payload);
    role.take_from(prepared_role);
    return true;
}

bool CLiveInstances::bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
    CBakedSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    return bake_to(block, payload, role, destination_schema, form, diagnostic);
}

bool CLiveInstances::bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
    CLiveSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    return bake_to(block, payload, role, destination_schema, form, diagnostic);
}

} // namespace schema
