
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_wrappers.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Schema-role document ownership, guarded editing and client bindings.

#include "schema/schema_wrappers.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include "debug/macros.hpp"

namespace schema
{

namespace detail
{

void SSchemaBindingState::invalidate() noexcept
{
    CSchemaBinding* binding = head;
    while (binding != nullptr)
    {
        CSchemaBinding* const next = binding->m_next;
        binding->m_owner = nullptr;
        binding->m_previous = nullptr;
        binding->m_next = nullptr;
        binding = next;
    }
    head = nullptr;
    count = 0u;
    resolution = nullptr;
    query = {};
}

}   // namespace detail

//==============================================================================
//  Client-held schema binding
//==============================================================================

CSchemaBinding::CSchemaBinding(CSchemaBinding&& source) noexcept
{
    take_link(source);
}

CSchemaBinding& CSchemaBinding::operator=(CSchemaBinding&& source) noexcept
{
    if (this != &source)
    {
        release();
        take_link(source);
    }
    return *this;
}

CSchemaBinding::~CSchemaBinding() noexcept
{
    release();
}

bool CSchemaBinding::bind(CBakedSchema& schema) noexcept
{
    return bind_to(schema.m_bindings);
}

bool CSchemaBinding::bind(CLiveSchema& schema) noexcept
{
    return bind_to(schema.m_bindings);
}

bool CSchemaBinding::bind_to(detail::SSchemaBindingState& target) noexcept
{
    if ((target.resolution == nullptr) || !target.resolution->is_ready())
    {
        return false;
    }
    if (m_owner == &target)
    {
        return true;
    }
    if (target.count == std::numeric_limits<std::size_t>::max())
    {
        return false;
    }
    release();
    m_owner = &target;
    m_next = target.head;
    if (m_next != nullptr)
    {
        m_next->m_previous = this;
    }
    target.head = this;
    ++target.count;
    return true;
}

void CSchemaBinding::release() noexcept
{
    if (m_owner == nullptr)
    {
        return;
    }
    if (m_previous != nullptr)
    {
        m_previous->m_next = m_next;
    }
    else
    {
        m_owner->head = m_next;
    }
    if (m_next != nullptr)
    {
        m_next->m_previous = m_previous;
    }
    --m_owner->count;
    m_owner = nullptr;
    m_previous = nullptr;
    m_next = nullptr;
}

void CSchemaBinding::take_link(CSchemaBinding& source) noexcept
{
    m_owner = source.m_owner;
    m_previous = source.m_previous;
    m_next = source.m_next;
    if (m_owner != nullptr)
    {
        if (m_previous != nullptr)
        {
            m_previous->m_next = this;
        }
        else
        {
            m_owner->head = this;
        }
        if (m_next != nullptr)
        {
            m_next->m_previous = this;
        }
    }
    source.m_owner = nullptr;
    source.m_previous = nullptr;
    source.m_next = nullptr;
}

const CResolvedSchema* CSchemaBinding::resolved() const noexcept
{
    return ((m_owner != nullptr) && (m_owner->resolution != nullptr) && m_owner->resolution->is_ready()) ?
        m_owner->resolution : nullptr;
}

CSchemaDocumentQuery CSchemaBinding::document_query() const noexcept
{
    return (m_owner != nullptr) ? m_owner->query : CSchemaDocumentQuery{};
}

//==============================================================================
//  Baked schema view
//==============================================================================

CBakedSchema::CBakedSchema() noexcept
{
    m_bindings.resolution = &m_resolution;
}

CBakedSchema::~CBakedSchema() noexcept
{
    m_bindings.invalidate();
}

bool CBakedSchema::can_replace() const noexcept
{
    if (m_bindings.count != 0u)
    {
        MV_ASSERT_MSG(false, "A referenced baked schema cannot be replaced.");
        return false;
    }
    return true;
}

bool CBakedSchema::set_document(const CBakedDocument& document) noexcept
{
    if (!can_replace() || document_ready() || !document.is_ready())
    {
        return false;
    }
    m_document = document;
    m_bindings.query = CSchemaDocumentQuery{ m_document };
    return true;
}

bool CBakedSchema::try_take_from(CBakedSchema&& source) noexcept
{
    if ((this == &source) || !can_replace() || !source.can_replace() || document_ready() || !source.document_ready())
    {
        return false;
    }
    m_document = source.m_document;
    m_resolution = std::move(source.m_resolution);
    m_bindings.query = CSchemaDocumentQuery{ m_document };
    source.m_document = {};
    source.m_bindings.query = {};
    return true;
}

bool CBakedSchema::resolve(SDiagnostic& diagnostic) noexcept
{
    return m_resolution.resolve(m_document, diagnostic);
}

bool CBakedSchema::clear_resolution() noexcept
{
    if (!can_replace())
    {
        return false;
    }
    m_resolution.clear();
    return true;
}

bool CBakedSchema::clear() noexcept
{
    if (!can_replace())
    {
        return false;
    }
    m_resolution.clear();
    m_document = {};
    m_bindings.query = {};
    return true;
}

CSchemaDocumentQuery CBakedSchema::document_query() const noexcept
{
    return CSchemaDocumentQuery{ m_document };
}

CSchemaHandle CBakedSchema::types_root() const noexcept
{
    const CSchemaDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "types" });
}

const CResolvedSchema* CBakedSchema::resolved() const noexcept
{
    return resolved_ready() ? &m_resolution : nullptr;
}

//==============================================================================
//  Live schema owner
//==============================================================================

CLiveSchema::CLiveSchema() noexcept
{
    m_bindings.resolution = &m_resolution;
}

CLiveSchema::~CLiveSchema() noexcept
{
    m_bindings.invalidate();
}

bool CLiveSchema::can_replace() const noexcept
{
    if (m_bindings.count != 0u)
    {
        MV_ASSERT_MSG(false, "A referenced live schema cannot be replaced.");
        return false;
    }
    return true;
}

bool CLiveSchema::can_edit() const noexcept
{
    if (!can_replace())
    {
        return false;
    }
    return document_ready();
}

bool CLiveSchema::document_empty() const noexcept
{
    return !m_document.root().is_valid() &&
        (m_document.memory_attribution().source_state == memory::EMemorySourceState::empty);
}

bool CLiveSchema::initialise(const std::size_t initial_node_capacity) noexcept
{
    if (!can_replace() || !document_empty() || !m_document.initialise(initial_node_capacity))
    {
        return false;
    }
    m_bindings.query = CSchemaDocumentQuery{ m_document };
    return true;
}

bool CLiveSchema::reset(const std::size_t initial_node_capacity) noexcept
{
    if (!can_replace())
    {
        return false;
    }
    CLiveDocument fresh;
    if (!fresh.initialise(initial_node_capacity))
    {
        return false;
    }
    m_resolution.clear();
    m_document = std::move(fresh);
    m_bindings.query = CSchemaDocumentQuery{ m_document };
    return true;
}

bool CLiveSchema::try_adopt(CLiveDocument&& document) noexcept
{
    if (!can_replace() || !document_empty() || !document.is_ready())
    {
        return false;
    }
    m_document = std::move(document);
    m_bindings.query = CSchemaDocumentQuery{ m_document };
    return true;
}

bool CLiveSchema::try_take_from(CLiveSchema&& source) noexcept
{
    if ((this == &source) || !can_replace() || !source.can_replace() || !document_empty() || !source.document_ready())
    {
        return false;
    }
    m_document = std::move(source.m_document);
    m_resolution = std::move(source.m_resolution);
    m_resolution.rebind_live_document(m_document);
    m_bindings.query = CSchemaDocumentQuery{ m_document };
    source.m_bindings.query = {};
    return true;
}

bool CLiveSchema::resolve(SDiagnostic& diagnostic) noexcept
{
    return m_resolution.resolve(m_document, diagnostic);
}

bool CLiveSchema::clear_resolution() noexcept
{
    if (!can_replace())
    {
        return false;
    }
    m_resolution.clear();
    return true;
}

bool CLiveSchema::clear() noexcept
{
    if (!can_replace())
    {
        return false;
    }
    m_resolution.clear();
    m_document.deallocate();
    m_bindings.query = {};
    return true;
}

CSchemaDocumentQuery CLiveSchema::document_query() const noexcept
{
    return CSchemaDocumentQuery{ m_document };
}

CSchemaHandle CLiveSchema::types_root() const noexcept
{
    const CSchemaDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "types" });
}

const CResolvedSchema* CLiveSchema::resolved() const noexcept
{
    return resolved_ready() ? &m_resolution : nullptr;
}

bool CLiveSchema::in_types(const CSchemaHandle target, const bool include_root) const noexcept
{
    const CSchemaDocumentQuery query = document_query();
    const CSchemaHandle types = types_root();
    if (!types || !query.contains(target))
    {
        return false;
    }
    for (CSchemaHandle current = target; current; current = query.parent(current))
    {
        if (current == types)
        {
            return include_root || (target != types);
        }
    }
    return false;
}

bool CLiveSchema::append_parent(const CSchemaHandle parent, const SEditValue& value) const noexcept
{
    if (in_types(parent, true))
    {
        return true;
    }
    const CSchemaDocumentQuery query = document_query();
    return (parent == query.root()) && !types_root() && (value.type == ELiveValueType::object) &&
        (value.name.length() == 5u) && (std::memcmp(value.name.string(), "types", 5u) == 0);
}

CNodeKey CLiveSchema::create_value(const SEditValue& value) noexcept
{
    switch (value.type)
    {
        case ELiveValueType::empty: return m_document.create_empty(value.name);
        case ELiveValueType::null_value: return m_document.create_null(value.name);
        case ELiveValueType::boolean: return m_document.create_boolean(value.unsigned_value != 0u, value.name);
        case ELiveValueType::integer:
            return value.unsigned_integer ? m_document.create_unsigned_integer(value.unsigned_value, value.name) :
                m_document.create_signed_integer(value.signed_value, value.name);
        case ELiveValueType::floating_point: return m_document.create_floating_point(value.floating_value, value.name);
        case ELiveValueType::string: return m_document.create_string(value.text, value.name);
        case ELiveValueType::array: return m_document.create_array(value.name);
        case ELiveValueType::object: return m_document.create_object(value.name);
        default: return {};
    }
}

CSchemaHandle CLiveSchema::append(const CSchemaHandle parent, const SEditValue& value) noexcept
{
    if (!can_edit() || !append_parent(parent, value))
    {
        return {};
    }
    m_resolution.clear();
    const CNodeKey candidate = create_value(value);
    if (!candidate)
    {
        return {};
    }
    const CNodeKey destination = detail::SSchemaHandleAccess::occurrence(parent).live;
    if (!m_document.append_child(destination, candidate).succeeded())
    {
        (void)m_document.erase(candidate);
        return {};
    }
    return detail::SSchemaHandleAccess::make(detail::SOccurrence{ candidate });
}

bool CLiveSchema::replace(const CSchemaHandle target, const SEditValue& value) noexcept
{
    if (!can_edit() || (!in_types(target, false) &&
        !(target && (target == types_root()) && (value.type == ELiveValueType::object))))
    {
        return false;
    }
    m_resolution.clear();
    const CNodeKey key = detail::SSchemaHandleAccess::occurrence(target).live;
    const CNodeKey candidate = create_value(value);
    if (!candidate)
    {
        return false;
    }
    const bool old_empty = m_document.value_type(key) == ELiveValueType::empty;
    const CNodeKey old_payload = old_empty ? CNodeKey{} : m_document.detach_payload(key);
    if (!old_empty && !old_payload)
    {
        (void)m_document.erase(candidate);
        return false;
    }
    if (!m_document.attach_payload(key, candidate))
    {
        if (old_payload && !m_document.attach_payload(key, old_payload))
        {
            MV_ASSERT_MSG(false, "Schema payload rollback failed.");
        }
        (void)m_document.erase(candidate);
        return false;
    }
    if (old_payload && !m_document.erase(old_payload))
    {
        MV_ASSERT_MSG(false, "Old schema payload cleanup failed.");
        return false;
    }
    return true;
}

bool CLiveSchema::set_name(const CSchemaHandle target, const CStringView& name) noexcept
{
    if (!can_edit() || !in_types(target, false))
    {
        return false;
    }
    m_resolution.clear();
    return m_document.set_name(detail::SSchemaHandleAccess::occurrence(target).live, name);
}

bool CLiveSchema::set_newline_escaping_suppressed(const CSchemaHandle target, const bool suppressed) noexcept
{
    if (!can_edit() || !in_types(target, false))
    {
        return false;
    }
    m_resolution.clear();
    return m_document.set_newline_escaping_suppressed(detail::SSchemaHandleAccess::occurrence(target).live, suppressed);
}

bool CLiveSchema::erase(const CSchemaHandle target) noexcept
{
    if (!can_edit() || !in_types(target, false))
    {
        return false;
    }
    m_resolution.clear();
    return m_document.erase(detail::SSchemaHandleAccess::occurrence(target).live);
}

//==============================================================================
//  Borrowed live editor forwarding
//==============================================================================

CSchemaHandle CLiveSchema::CEditor::append_empty(const CSchemaHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_null(const CSchemaHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::null_value;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_boolean(const CSchemaHandle parent, const bool boolean, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::boolean;
    value.unsigned_value = boolean ? 1u : 0u;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_signed(const CSchemaHandle parent, const std::int64_t integer, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.signed_value = integer;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_unsigned(const CSchemaHandle parent, const std::uint64_t integer, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.unsigned_value = integer;
    value.unsigned_integer = true;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_floating(const CSchemaHandle parent, const double floating, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::floating_point;
    value.floating_value = floating;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_string(const CSchemaHandle parent, const CStringView& text, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::string;
    value.text = text;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_array(const CSchemaHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::array;
    value.name = name;
    return m_owner->append(parent, value);
}

CSchemaHandle CLiveSchema::CEditor::append_object(const CSchemaHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::object;
    value.name = name;
    return m_owner->append(parent, value);
}

bool CLiveSchema::CEditor::set_name(const CSchemaHandle target, const CStringView& name) noexcept
{
    return m_owner->set_name(target, name);
}

bool CLiveSchema::CEditor::set_newline_escaping_suppressed(const CSchemaHandle target, const bool suppressed) noexcept
{
    return m_owner->set_newline_escaping_suppressed(target, suppressed);
}

bool CLiveSchema::CEditor::erase(const CSchemaHandle target) noexcept
{
    return m_owner->erase(target);
}

bool CLiveSchema::CEditor::replace_null(const CSchemaHandle target) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::null_value;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_boolean(const CSchemaHandle target, const bool boolean) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::boolean;
    value.unsigned_value = boolean ? 1u : 0u;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_signed(const CSchemaHandle target, const std::int64_t integer) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.signed_value = integer;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_unsigned(const CSchemaHandle target, const std::uint64_t integer) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.unsigned_value = integer;
    value.unsigned_integer = true;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_floating(const CSchemaHandle target, const double floating) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::floating_point;
    value.floating_value = floating;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_string(const CSchemaHandle target, const CStringView& text) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::string;
    value.text = text;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_array(const CSchemaHandle target) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::array;
    return m_owner->replace(target, value);
}

bool CLiveSchema::CEditor::replace_object(const CSchemaHandle target) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::object;
    return m_owner->replace(target, value);
}

}   // namespace schema
