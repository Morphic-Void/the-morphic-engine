
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    types_wrappers.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Schema-role document ownership, guarded editing and client bindings.

#include "schema/types_wrappers.hpp"
#include "schema/baked_data_block.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include "data_model/document_translation.hpp"
#include "debug/macros.hpp"

namespace schema
{

namespace detail
{

void STypesBindingState::invalidate() noexcept
{
    CTypesBinding* binding = head;
    while (binding != nullptr)
    {
        CTypesBinding* const next = binding->m_next;
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

CTypesBinding::CTypesBinding(CTypesBinding&& source) noexcept
{
    take_link(source);
}

CTypesBinding& CTypesBinding::operator=(CTypesBinding&& source) noexcept
{
    if (this != &source)
    {
        release();
        take_link(source);
    }
    return *this;
}

CTypesBinding::~CTypesBinding() noexcept
{
    release();
}

bool CTypesBinding::bind(CBakedTypes& schema) noexcept
{
    return bind_to(schema.m_bindings);
}

bool CTypesBinding::bind(CLiveTypes& schema) noexcept
{
    return bind_to(schema.m_bindings);
}

bool CTypesBinding::bind(const CTypesBinding& source) noexcept
{
    return source.m_owner && bind_to(*source.m_owner);
}

bool CTypesBinding::bind_to(detail::STypesBindingState& target) noexcept
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

void CTypesBinding::release() noexcept
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

void CTypesBinding::take_link(CTypesBinding& source) noexcept
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

const CResolvedTypes* CTypesBinding::resolved() const noexcept
{
    return ((m_owner != nullptr) && (m_owner->resolution != nullptr) && m_owner->resolution->is_ready()) ?
        m_owner->resolution : nullptr;
}

CTypesDocumentQuery CTypesBinding::document_query() const noexcept
{
    return (m_owner != nullptr) ? m_owner->query : CTypesDocumentQuery{};
}

//==============================================================================
//  Baked schema view
//==============================================================================

CBakedTypes::CBakedTypes() noexcept
{
    m_bindings.resolution = &m_resolution;
}

CBakedTypes::~CBakedTypes() noexcept
{
    m_bindings.invalidate();
}

bool CBakedTypes::can_replace() const noexcept
{
    if (m_bindings.count != 0u)
    {
        MV_ASSERT_REPORT("A referenced baked schema cannot be replaced.");
        return false;
    }
    return true;
}

bool CBakedTypes::set_block(const CBakedDataBlock& block) noexcept
{
    return block.loaded_ready() && block.has_types() && set_document(block.document());
}

bool CBakedTypes::set_document(const CBakedDocument& document) noexcept
{
    if (!can_replace() || document_ready() || !document.is_ready())
    {
        return false;
    }
    m_document = document;
    m_bindings.query = CTypesDocumentQuery{ m_document };
    return true;
}

bool CBakedTypes::try_take_from(CBakedTypes&& source) noexcept
{
    if ((this == &source) || !can_replace() || !source.can_replace() || document_ready() || !source.document_ready())
    {
        return false;
    }
    m_document = source.m_document;
    m_resolution = std::move(source.m_resolution);
    m_bindings.query = CTypesDocumentQuery{ m_document };
    source.m_document = {};
    source.m_bindings.query = {};
    return true;
}

bool CBakedTypes::resolve(SDiagnostic& diagnostic) noexcept
{
    return m_resolution.resolve(m_document, diagnostic);
}

bool CBakedTypes::clear_resolution() noexcept
{
    if (!can_replace())
    {
        return false;
    }
    m_resolution.clear();
    return true;
}

bool CBakedTypes::clear() noexcept
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

bool CBakedTypes::promote(CLiveTypes& destination, SDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!document_ready() || !destination.document_empty() || (destination.reference_count() != 0u))
    {
        diagnostic.reason = EReason::invalid_input;
        return false;
    }

    CLiveDocument document;
    if (!document_translation::promote_root_member(m_document, CStringView{ "types" }, document))
    {
        diagnostic.reason = EReason::translation_failed;
        return false;
    }

    CLiveTypes staged;
    if (!staged.try_adopt(std::move(document)))
    {
        diagnostic.reason = EReason::translation_failed;
        return false;
    }
    if (!staged.resolve(diagnostic))
    {
        diagnostic.occurrence = {};
        diagnostic.enclosing_type = {};
        diagnostic.enclosing_member = {};
        diagnostic.related = {};
        diagnostic.ranges_available = false;
        diagnostic.range_begin = 0u;
        diagnostic.range_end = 0u;
        diagnostic.related_begin = 0u;
        diagnostic.related_end = 0u;
        return false;
    }
    if (!destination.try_take_from(std::move(staged)))
    {
        diagnostic.reason = EReason::invalid_input;
        return false;
    }
    return true;
}

CTypesDocumentQuery CBakedTypes::document_query() const noexcept
{
    return CTypesDocumentQuery{ m_document };
}

CTypesHandle CBakedTypes::types_root() const noexcept
{
    const CTypesDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "types" });
}

const CResolvedTypes* CBakedTypes::resolved() const noexcept
{
    return resolved_ready() ? &m_resolution : nullptr;
}

//==============================================================================
//  Live schema owner
//==============================================================================

CLiveTypes::CLiveTypes() noexcept
{
    m_bindings.resolution = &m_resolution;
}

CLiveTypes::~CLiveTypes() noexcept
{
    m_bindings.invalidate();
}

bool CLiveTypes::can_replace() const noexcept
{
    if (m_bindings.count != 0u)
    {
        MV_ASSERT_REPORT("A referenced live schema cannot be replaced.");
        return false;
    }
    return true;
}

bool CLiveTypes::can_edit() const noexcept
{
    if (!can_replace())
    {
        return false;
    }
    return document_ready();
}

bool CLiveTypes::document_empty() const noexcept
{
    return !m_document.root().is_valid() &&
        (m_document.memory_attribution().source_state == memory::EMemorySourceState::empty);
}

bool CLiveTypes::initialise(const std::size_t initial_node_capacity) noexcept
{
    if (!can_replace() || !document_empty() || !m_document.initialise(initial_node_capacity))
    {
        return false;
    }
    m_bindings.query = CTypesDocumentQuery{ m_document };
    return true;
}

bool CLiveTypes::reset(const std::size_t initial_node_capacity) noexcept
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
    m_bindings.query = CTypesDocumentQuery{ m_document };
    return true;
}

bool CLiveTypes::try_adopt(CLiveDocument&& document) noexcept
{
    if (!can_replace() || !document_empty() || !document.is_ready())
    {
        return false;
    }
    m_document = std::move(document);
    m_bindings.query = CTypesDocumentQuery{ m_document };
    return true;
}

bool CLiveTypes::try_take_from(CLiveTypes&& source) noexcept
{
    if ((this == &source) || !can_replace() || !source.can_replace() || !document_empty() || !source.document_ready())
    {
        return false;
    }
    m_document = std::move(source.m_document);
    m_resolution = std::move(source.m_resolution);
    m_resolution.rebind_live_document(m_document);
    m_bindings.query = CTypesDocumentQuery{ m_document };
    source.m_bindings.query = {};
    return true;
}

bool CLiveTypes::resolve(SDiagnostic& diagnostic) noexcept
{
    return m_resolution.resolve(m_document, diagnostic);
}

bool CLiveTypes::clear_resolution() noexcept
{
    if (!can_replace())
    {
        return false;
    }
    m_resolution.clear();
    return true;
}

bool CLiveTypes::clear() noexcept
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

bool CLiveTypes::bake(CBakedDocumentBlock& destination_block, CBakedTypes& destination_schema) const noexcept
{
    if (!document_ready() ||
        (destination_block.memory_attribution().source_state != memory::EMemorySourceState::empty) ||
        destination_schema.document_ready() || destination_schema.resolved_ready() ||
        (destination_schema.reference_count() != 0u))
    {
        return false;
    }

    CBakedDocumentBlock staged_block;
    if (!document_translation::bake_root_member(m_document, CStringView{ "types" }, staged_block))
    {
        return false;
    }
    CBakedTypes staged_schema;
    if (!staged_schema.set_document(staged_block.document()) ||
        !destination_schema.try_take_from(std::move(staged_schema)))
    {
        return false;
    }
    destination_block = std::move(staged_block);
    return true;
}

bool CLiveTypes::bake(CBakedDataBlock& destination_block, CBakedTypes& destination_schema) const noexcept
{
    if (destination_block.loaded_ready() || destination_schema.document_ready() || destination_schema.reference_count()) return false;
    CBakedDocumentBlock types;
    if (!document_translation::bake_root_member(m_document, CStringView{ "types" }, types)) return false;
    SDataBlockDiagnostic diagnostic;
    if (!destination_block.assemble(types.document(), {}, diagnostic)) return false;
    if (!destination_schema.set_block(destination_block))
    {
        destination_block.clear();
        return false;
    }
    return true;
}

CTypesDocumentQuery CLiveTypes::document_query() const noexcept
{
    return CTypesDocumentQuery{ m_document };
}

CTypesHandle CLiveTypes::types_root() const noexcept
{
    const CTypesDocumentQuery query = document_query();
    return query.object_child(query.root(), CStringView{ "types" });
}

const CResolvedTypes* CLiveTypes::resolved() const noexcept
{
    return resolved_ready() ? &m_resolution : nullptr;
}

bool CLiveTypes::in_types(const CTypesHandle target, const bool include_root) const noexcept
{
    const CTypesDocumentQuery query = document_query();
    const CTypesHandle types = types_root();
    if (!types || !query.contains(target))
    {
        return false;
    }
    for (CTypesHandle current = target; current; current = query.parent(current))
    {
        if (current == types)
        {
            return include_root || (target != types);
        }
    }
    return false;
}

bool CLiveTypes::append_parent(const CTypesHandle parent, const SEditValue& value) const noexcept
{
    if (in_types(parent, true))
    {
        return true;
    }
    const CTypesDocumentQuery query = document_query();
    return (parent == query.root()) && !types_root() && (value.type == ELiveValueType::object) &&
        (value.name.length() == 5u) && (std::memcmp(value.name.string(), "types", 5u) == 0);
}

CNodeKey CLiveTypes::create_value(const SEditValue& value) noexcept
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

CTypesHandle CLiveTypes::append(const CTypesHandle parent, const SEditValue& value) noexcept
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
    const CNodeKey destination = detail::STypesHandleAccess::occurrence(parent).live;
    if (!m_document.append_child(destination, candidate).succeeded())
    {
        (void)m_document.erase(candidate);
        return {};
    }
    return detail::STypesHandleAccess::make(detail::SOccurrence{ candidate });
}

bool CLiveTypes::replace(const CTypesHandle target, const SEditValue& value) noexcept
{
    if (!can_edit() || (!in_types(target, false) &&
        !(target && (target == types_root()) && (value.type == ELiveValueType::object))))
    {
        return false;
    }
    m_resolution.clear();
    const CNodeKey key = detail::STypesHandleAccess::occurrence(target).live;
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
            MV_CRITICAL_EVENT("Schema payload rollback failed.");
        }
        (void)m_document.erase(candidate);
        return false;
    }
    if (old_payload && !m_document.erase(old_payload))
    {
        MV_CRITICAL_EVENT("Old schema payload cleanup failed.");
        return false;
    }
    return true;
}

bool CLiveTypes::set_name(const CTypesHandle target, const CStringView& name) noexcept
{
    if (!can_edit() || !in_types(target, false))
    {
        return false;
    }
    m_resolution.clear();
    return m_document.set_name(detail::STypesHandleAccess::occurrence(target).live, name);
}

bool CLiveTypes::set_newline_escaping_suppressed(const CTypesHandle target, const bool suppressed) noexcept
{
    if (!can_edit() || !in_types(target, false))
    {
        return false;
    }
    m_resolution.clear();
    return m_document.set_newline_escaping_suppressed(detail::STypesHandleAccess::occurrence(target).live, suppressed);
}

bool CLiveTypes::erase(const CTypesHandle target) noexcept
{
    if (!can_edit() || !in_types(target, false))
    {
        return false;
    }
    m_resolution.clear();
    return m_document.erase(detail::STypesHandleAccess::occurrence(target).live);
}

//==============================================================================
//  Borrowed live editor forwarding
//==============================================================================

CTypesHandle CLiveTypes::CEditor::append_empty(const CTypesHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_null(const CTypesHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::null_value;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_boolean(const CTypesHandle parent, const bool boolean, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::boolean;
    value.unsigned_value = boolean ? 1u : 0u;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_signed(const CTypesHandle parent, const std::int64_t integer, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.signed_value = integer;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_unsigned(const CTypesHandle parent, const std::uint64_t integer, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.unsigned_value = integer;
    value.unsigned_integer = true;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_floating(const CTypesHandle parent, const double floating, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::floating_point;
    value.floating_value = floating;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_string(const CTypesHandle parent, const CStringView& text, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::string;
    value.text = text;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_array(const CTypesHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::array;
    value.name = name;
    return m_owner->append(parent, value);
}

CTypesHandle CLiveTypes::CEditor::append_object(const CTypesHandle parent, const CStringView& name) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::object;
    value.name = name;
    return m_owner->append(parent, value);
}

bool CLiveTypes::CEditor::set_name(const CTypesHandle target, const CStringView& name) noexcept
{
    return m_owner->set_name(target, name);
}

bool CLiveTypes::CEditor::set_newline_escaping_suppressed(const CTypesHandle target, const bool suppressed) noexcept
{
    return m_owner->set_newline_escaping_suppressed(target, suppressed);
}

bool CLiveTypes::CEditor::erase(const CTypesHandle target) noexcept
{
    return m_owner->erase(target);
}

bool CLiveTypes::CEditor::replace_null(const CTypesHandle target) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::null_value;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_boolean(const CTypesHandle target, const bool boolean) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::boolean;
    value.unsigned_value = boolean ? 1u : 0u;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_signed(const CTypesHandle target, const std::int64_t integer) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.signed_value = integer;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_unsigned(const CTypesHandle target, const std::uint64_t integer) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::integer;
    value.unsigned_value = integer;
    value.unsigned_integer = true;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_floating(const CTypesHandle target, const double floating) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::floating_point;
    value.floating_value = floating;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_string(const CTypesHandle target, const CStringView& text) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::string;
    value.text = text;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_array(const CTypesHandle target) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::array;
    return m_owner->replace(target, value);
}

bool CLiveTypes::CEditor::replace_object(const CTypesHandle target) noexcept
{
    SEditValue value;
    value.type = ELiveValueType::object;
    return m_owner->replace(target, value);
}

}   // namespace schema
