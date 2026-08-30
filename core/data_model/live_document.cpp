
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_document.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Aug 26
//
//  Mutable document implementation and baked-document promotion.

#include <limits>
#include <utility>

#include "data_model/baked_document.hpp"
#include "data_model/live_document.hpp"

//==============================================================================
//  CLiveDocument function bodies
//==============================================================================

bool CLiveDocument::is_empty() const noexcept
{
    return m_nodes.is_empty();
}

CNodeKey CLiveDocument::root() const noexcept
{
    return m_root;
}

bool CLiveDocument::is_array_type(const EJsonNodeType type) noexcept
{
    return (type == EJsonNodeType::array) || (type == EJsonNodeType::recovered_duplicate_array);
}

bool CLiveDocument::is_container_type(const EJsonNodeType type) noexcept
{
    return is_array_type(type) || (type == EJsonNodeType::object);
}

bool CLiveDocument::check_stable_strings(const CStableStrings& strings) noexcept
{
    return (strings.memory_allocation_count() == 0u) || strings.check_integrity();
}

CJsonSlot* CLiveDocument::node_slot(const CNodeKey node) noexcept
{
    return m_nodes.get_slot(node);
}

const CJsonSlot* CLiveDocument::node_slot(const CNodeKey node) const noexcept
{
    return m_nodes.get_slot(node);
}

bool CLiveDocument::initialise(const std::size_t initial_slot_count) noexcept
{
    deallocate();
    return m_nodes.initialise(initial_slot_count);
}

void CLiveDocument::deallocate() noexcept
{
    m_nodes.deallocate();
    m_property_names.deallocate();
    m_string_values.deallocate();
    m_root = CNodeKey{};
    m_next_node_key = 1u;
}

bool CLiveDocument::is_valid() const noexcept
{
    return m_nodes.is_valid() && check_stable_strings(m_property_names) && check_stable_strings(m_string_values);
}

bool CLiveDocument::is_ready() const noexcept { return m_nodes.is_ready(); }

bool CLiveDocument::contains_recovered_duplicate_arrays() const noexcept
{
    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const CJsonSlot* const slot = m_nodes.get_slot(index);
        if ((slot != nullptr) && (slot->type == EJsonNodeType::recovered_duplicate_array)) return true;
    }
    return false;
}

bool CLiveDocument::requires_morphic_json_extensions() const noexcept
{
    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const CJsonSlot* const slot = m_nodes.get_slot(index);
        CJsonIntegerMetadata metadata;
        if ((slot != nullptr) && (slot->type == EJsonNodeType::integer) &&
            json_integer_metadata_from_flags(slot->flags, metadata) &&
            json_integer_requires_morphic_extensions(slot->payload.unsigned_bits, metadata)) return true;
    }
    return false;
}

bool CLiveDocument::is_canonical() const noexcept
{
    return !contains_recovered_duplicate_arrays() && !requires_morphic_json_extensions();
}

bool CLiveDocument::set_root(const CNodeKey node) noexcept
{
    CJsonSlot* const slot = node_slot(node);
    if ((slot == nullptr) || slot->parent.is_valid() || slot->previous_sibling.is_valid() || slot->next_sibling.is_valid()) return false;
    m_root = node;
    return true;
}

CNodeKey CLiveDocument::create_node(const EJsonNodeType type) noexcept
{
    if (!is_ready() || (type == EJsonNodeType::invalid) || (m_next_node_key == 0u))
    {
        return CNodeKey{};
    }
    const CNodeKey key{ m_next_node_key++ };
    CJsonSlot slot{};
    slot.self = key;
    slot.type = type;
    if (is_container_type(type)) slot.payload.children = CChildList{};
    return (m_nodes.insert(key, slot) >= 0) ? key : CNodeKey{};
}

CNodeKey CLiveDocument::create_null() noexcept { return create_node(EJsonNodeType::null_value); }
CNodeKey CLiveDocument::create_array() noexcept { return create_node(EJsonNodeType::array); }
CNodeKey CLiveDocument::create_object() noexcept { return create_node(EJsonNodeType::object); }

CNodeKey CLiveDocument::create_boolean(const bool value) noexcept
{
    const CNodeKey key = create_node(EJsonNodeType::boolean);
    if (CJsonSlot* const slot = node_slot(key)) slot->payload.unsigned_bits = value ? 1u : 0u;
    return key;
}

CNodeKey CLiveDocument::create_integer(const std::int64_t value) noexcept
{
    const CJsonIntegerMetadata metadata{
        (value < 0) ? EJsonIntegerSign::signed_value : EJsonIntegerSign::unsigned_value,
        (value < 0) ? json_signed_integer_smallest_width(value) :
            json_unsigned_integer_smallest_width(static_cast<std::uint64_t>(value)),
        EJsonIntegerNotation::decimal,
        EJsonIntegerPrefix::standard };
    return create_integer(value, metadata);
}

CNodeKey CLiveDocument::create_integer(const std::int64_t value, const CJsonIntegerMetadata& metadata) noexcept
{
    if (!json_integer_metadata_matches_signed_value(value, metadata)) return CNodeKey{};
    const CNodeKey key = create_node(EJsonNodeType::integer);
    if (CJsonSlot* const slot = node_slot(key))
    {
        slot->payload.unsigned_bits = json_integer_bits(value);
        slot->flags = json_integer_flags(metadata);
    }
    return key;
}

CNodeKey CLiveDocument::create_unsigned_integer(const std::uint64_t value, const CJsonIntegerMetadata& metadata) noexcept
{
    if (!json_integer_metadata_matches_unsigned_value(value, metadata)) return CNodeKey{};
    const CNodeKey key = create_node(EJsonNodeType::integer);
    if (CJsonSlot* const slot = node_slot(key))
    {
        slot->payload.unsigned_bits = value;
        slot->flags = json_integer_flags(metadata);
    }
    return key;
}

CNodeKey CLiveDocument::create_floating_point(const double value) noexcept
{
    const CNodeKey key = create_node(EJsonNodeType::floating_point);
    if (CJsonSlot* const slot = node_slot(key))
    {
        slot->payload.floating_value = value;
    }
    return key;
}

CNodeKey CLiveDocument::create_string(const CStringView& value) noexcept
{
    if (value.string() == nullptr) return CNodeKey{};
    const std::size_t id = m_string_values.append(value.string(), value.length());
    if ((id == CStableStrings::k_invalid_id) || (id > std::numeric_limits<std::uint32_t>::max())) return CNodeKey{};
    const CNodeKey key = create_node(EJsonNodeType::string);
    if (CJsonSlot* const slot = node_slot(key)) slot->payload.string_value = CStringValueId{ static_cast<std::uint32_t>(id) };
    return key;
}

CPropertyNameId CLiveDocument::intern_property_name(const CStringView& name) noexcept
{
    if (name.string() == nullptr) return CPropertyNameId{};
    const std::size_t id = m_property_names.append(name.string(), name.length());
    if ((id == CStableStrings::k_invalid_id) || (id > std::numeric_limits<std::uint32_t>::max())) return CPropertyNameId{};
    return CPropertyNameId{ static_cast<std::uint32_t>(id) };
}

CStringView CLiveDocument::property_name(const CPropertyNameId name) const noexcept
{
    return name.is_valid() ? m_property_names.view(name.query_value()) : CStringView{};
}

CStringView CLiveDocument::string_value(const CStringValueId value) const noexcept
{
    return value.is_valid() ? m_string_values.view(value.query_value()) : CStringView{};
}

bool CLiveDocument::can_attach(const CNodeKey parent_key, const CNodeKey child_key) const noexcept
{
    const CJsonSlot* const parent_slot = node_slot(parent_key);
    const CJsonSlot* const child_slot = node_slot(child_key);
    if ((parent_slot == nullptr) || !is_container_type(parent_slot->type) || (child_slot == nullptr) ||
        (child_key == m_root) || child_slot->parent.is_valid() || child_slot->previous_sibling.is_valid() ||
        child_slot->next_sibling.is_valid() ||
        (parent_slot->payload.children.count == std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }
    CNodeKey ancestor = parent_key;
    for (std::uint32_t depth = 0u; ancestor.is_valid(); ++depth)
    {
        if ((ancestor == child_key) || (depth >= m_nodes.occupied_count())) return false;
        const CJsonSlot* const ancestor_slot = node_slot(ancestor);
        if (ancestor_slot == nullptr) return false;
        ancestor = ancestor_slot->parent;
    }
    return true;
}

bool CLiveDocument::attach_before(
    const CNodeKey parent_key,
    const CNodeKey before_key,
    const CNodeKey child_key,
    const CPropertyNameId name) noexcept
{
    if (!can_attach(parent_key, child_key)) return false;
    CJsonSlot* const parent_slot = node_slot(parent_key);
    CJsonSlot* const child_slot = node_slot(child_key);
    CJsonSlot* before_slot = nullptr;
    if (before_key.is_valid())
    {
        before_slot = node_slot(before_key);
        if ((before_slot == nullptr) || (before_slot->parent != parent_key)) return false;
    }
    CChildList& list = parent_slot->payload.children;
    const CNodeKey previous_key = (before_slot != nullptr) ? before_slot->previous_sibling : list.last;
    CJsonSlot* const previous_slot = node_slot(previous_key);
    if (previous_key.is_valid() && ((previous_slot == nullptr) || (previous_slot->parent != parent_key))) return false;

    child_slot->parent = parent_key;
    child_slot->previous_sibling = previous_key;
    child_slot->next_sibling = before_key;
    child_slot->name_in_parent = name;
    if (previous_slot != nullptr) previous_slot->next_sibling = child_key; else list.first = child_key;
    if (before_slot != nullptr) before_slot->previous_sibling = child_key; else list.last = child_key;
    ++list.count;
    ++list.revision;
    return true;
}

bool CLiveDocument::append_array_child(const CNodeKey array, const CNodeKey child) noexcept
{
    const CJsonSlot* const slot = node_slot(array);
    return (slot != nullptr) && is_array_type(slot->type) && attach_before(array, CNodeKey{}, child, CPropertyNameId{});
}

bool CLiveDocument::insert_array_child_before(const CNodeKey array, const CNodeKey before, const CNodeKey child) noexcept
{
    const CJsonSlot* const slot = node_slot(array);
    return (slot != nullptr) && is_array_type(slot->type) && before.is_valid() && attach_before(array, before, child, CPropertyNameId{});
}

bool CLiveDocument::object_has_name(const CNodeKey object, const CPropertyNameId name) const noexcept
{
    for (CNodeKey child = first_child(object); child.is_valid(); child = next_sibling(child))
    {
        const CJsonSlot* const child_slot = node_slot(child);
        if ((child_slot != nullptr) && (child_slot->name_in_parent == name)) return true;
    }
    return false;
}

bool CLiveDocument::add_object_child(const CNodeKey object, const CStringView& name, const CNodeKey child) noexcept
{
    const CJsonSlot* const slot = node_slot(object);
    if ((slot == nullptr) || (slot->type != EJsonNodeType::object)) return false;
    const CPropertyNameId name_id = intern_property_name(name);
    return name_id.is_valid() && !object_has_name(object, name_id) && attach_before(object, CNodeKey{}, child, name_id);
}

bool CLiveDocument::detach(const CNodeKey child_key) noexcept
{
    CJsonSlot* const child_slot = node_slot(child_key);
    if ((child_slot == nullptr) || !child_slot->parent.is_valid() || (child_key == m_root)) return false;
    CJsonSlot* const parent_slot = node_slot(child_slot->parent);
    CJsonSlot* const previous_slot = node_slot(child_slot->previous_sibling);
    CJsonSlot* const next_slot = node_slot(child_slot->next_sibling);
    if ((parent_slot == nullptr) || !is_container_type(parent_slot->type) || (parent_slot->payload.children.count == 0u) ||
        (child_slot->previous_sibling.is_valid() && (previous_slot == nullptr)) ||
        (child_slot->next_sibling.is_valid() && (next_slot == nullptr)))
    {
        return false;
    }
    CChildList& list = parent_slot->payload.children;
    if (previous_slot != nullptr) previous_slot->next_sibling = child_slot->next_sibling; else list.first = child_slot->next_sibling;
    if (next_slot != nullptr) next_slot->previous_sibling = child_slot->previous_sibling; else list.last = child_slot->previous_sibling;
    child_slot->parent = CNodeKey{};
    child_slot->previous_sibling = CNodeKey{};
    child_slot->next_sibling = CNodeKey{};
    child_slot->name_in_parent = CPropertyNameId{};
    --list.count;
    ++list.revision;
    return true;
}

bool CLiveDocument::erase_detached(const CNodeKey node) noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) && (node != m_root) && !slot->parent.is_valid() &&
        !slot->previous_sibling.is_valid() && !slot->next_sibling.is_valid() &&
        (!is_container_type(slot->type) || (slot->payload.children.count == 0u)) &&
        m_nodes.erase(node);
}

EJsonNodeType CLiveDocument::node_type(const CNodeKey node) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) ? slot->type : EJsonNodeType::invalid;
}

bool CLiveDocument::boolean_value(const CNodeKey node, bool& value) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    if ((slot == nullptr) || (slot->type != EJsonNodeType::boolean))
    {
        return false;
    }
    value = slot->payload.unsigned_bits != 0u;
    return true;
}

bool CLiveDocument::integer_value(const CNodeKey node, std::int64_t& value) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    CJsonIntegerMetadata metadata;
    if ((slot == nullptr) || (slot->type != EJsonNodeType::integer) || !json_integer_metadata_from_flags(slot->flags, metadata) ||
        ((metadata.sign == EJsonIntegerSign::unsigned_value) && (slot->payload.unsigned_bits > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))))
    {
        return false;
    }
    value = (metadata.sign == EJsonIntegerSign::signed_value) ?
        json_signed_integer_value(slot->payload.unsigned_bits) : static_cast<std::int64_t>(slot->payload.unsigned_bits);
    return true;
}

bool CLiveDocument::unsigned_integer_value(const CNodeKey node, std::uint64_t& value) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    CJsonIntegerMetadata metadata;
    if ((slot == nullptr) || (slot->type != EJsonNodeType::integer) || !json_integer_metadata_from_flags(slot->flags, metadata) ||
        (metadata.sign == EJsonIntegerSign::signed_value)) return false;
    value = slot->payload.unsigned_bits;
    return true;
}

bool CLiveDocument::integer_metadata(const CNodeKey node, CJsonIntegerMetadata& metadata) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) && (slot->type == EJsonNodeType::integer) && json_integer_metadata_from_flags(slot->flags, metadata);
}

bool CLiveDocument::floating_point_value(const CNodeKey node, double& value) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    if ((slot == nullptr) || (slot->type != EJsonNodeType::floating_point))
    {
        return false;
    }
    value = slot->payload.floating_value;
    return true;
}

CStringView CLiveDocument::string_value(const CNodeKey node) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return ((slot != nullptr) && (slot->type == EJsonNodeType::string)) ? string_value(slot->payload.string_value) : CStringView{};
}

CNodeKey CLiveDocument::parent(const CNodeKey node) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) ? slot->parent : CNodeKey{};
}

CPropertyNameId CLiveDocument::name_in_parent(const CNodeKey node) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) ? slot->name_in_parent : CPropertyNameId{};
}

CNodeKey CLiveDocument::previous_sibling(const CNodeKey node) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) ? slot->previous_sibling : CNodeKey{};
}

CNodeKey CLiveDocument::next_sibling(const CNodeKey node) const noexcept
{
    const CJsonSlot* const slot = node_slot(node);
    return (slot != nullptr) ? slot->next_sibling : CNodeKey{};
}

std::uint32_t CLiveDocument::child_count(const CNodeKey container) const noexcept
{
    const CJsonSlot* const slot = node_slot(container);
    return ((slot != nullptr) && is_container_type(slot->type)) ? slot->payload.children.count : 0u;
}

CNodeKey CLiveDocument::first_child(const CNodeKey container) const noexcept
{
    const CJsonSlot* const slot = node_slot(container);
    return ((slot != nullptr) && is_container_type(slot->type)) ? slot->payload.children.first : CNodeKey{};
}

CNodeKey CLiveDocument::last_child(const CNodeKey container) const noexcept
{
    const CJsonSlot* const slot = node_slot(container);
    return ((slot != nullptr) && is_container_type(slot->type)) ? slot->payload.children.last : CNodeKey{};
}

CNodeKey CLiveDocument::object_child(const CNodeKey object, const CPropertyNameId name) const noexcept
{
    const CJsonSlot* const object_slot = node_slot(object);
    if ((object_slot == nullptr) || (object_slot->type != EJsonNodeType::object) || !name.is_valid()) return CNodeKey{};
    for (CNodeKey child = object_slot->payload.children.first; child.is_valid(); child = next_sibling(child))
    {
        const CJsonSlot* const child_slot = node_slot(child);
        if ((child_slot != nullptr) && (child_slot->name_in_parent == name)) return child;
    }
    return CNodeKey{};
}

CNodeKey CLiveDocument::object_child(const CNodeKey object, const CStringView& name) const noexcept
{
    if (name.string() == nullptr) return CNodeKey{};
    for (CNodeKey child = first_child(object); child.is_valid(); child = next_sibling(child))
    {
        const CStringView child_name = property_name(name_in_parent(child));
        if ((child_name.string() != nullptr) && (child_name == name)) return child;
    }
    return CNodeKey{};
}

CNodeKey CLiveDocument::array_at(const CNodeKey array, const std::uint32_t index) const noexcept
{
    const CJsonSlot* const array_slot = node_slot(array);
    if ((array_slot == nullptr) || !is_array_type(array_slot->type) || (index >= array_slot->payload.children.count)) return CNodeKey{};
    CNodeKey child = array_slot->payload.children.first;
    for (std::uint32_t position = 0u; position < index; ++position) child = next_sibling(child);
    return child;
}

bool CLiveDocument::array_cursor_at(const CNodeKey array, const std::uint32_t index, CArrayCursor& cursor) const noexcept
{
    const CJsonSlot* const array_slot = node_slot(array);
    if ((array_slot == nullptr) || !is_array_type(array_slot->type) || (index >= array_slot->payload.children.count)) return false;
    const CNodeKey child = array_at(array, index);
    if (!child.is_valid()) return false;
    cursor = CArrayCursor{ array, child, index, array_slot->payload.children.revision };
    return true;
}

bool CLiveDocument::array_cursor_next(CArrayCursor& cursor) const noexcept
{
    const CJsonSlot* const array_slot = node_slot(cursor.parent);
    if ((array_slot == nullptr) || !is_array_type(array_slot->type) ||
        (array_slot->payload.children.revision != cursor.revision) || !cursor.current.is_valid())
    {
        return false;
    }
    const CNodeKey next = next_sibling(cursor.current);
    if (!next.is_valid()) return false;
    cursor.current = next;
    ++cursor.index;
    return true;
}

bool CLiveDocument::check_container_integrity(const CJsonSlot& container) const noexcept
{
    if (!is_container_type(container.type)) return false;
    const CChildList& list = container.payload.children;
    if ((list.count == 0u) != (!list.first.is_valid() && !list.last.is_valid())) return false;
    if ((list.count != 0u) && (!list.first.is_valid() || !list.last.is_valid())) return false;
    CNodeKey previous;
    CNodeKey child = list.first;
    std::uint32_t traversed = 0u;
    while (child.is_valid())
    {
        const CJsonSlot* const child_slot = node_slot(child);
        if ((child_slot == nullptr) || (child_slot->parent != container.self) || (child_slot->previous_sibling != previous)) return false;
        if ((container.type == EJsonNodeType::object) && !child_slot->name_in_parent.is_valid()) return false;
        if (is_array_type(container.type) && child_slot->name_in_parent.is_valid()) return false;
        if (++traversed > list.count) return false;
        if (container.type == EJsonNodeType::object)
        {
            for (CNodeKey earlier = list.first; earlier != child; earlier = next_sibling(earlier))
            {
                const CJsonSlot* const earlier_slot = node_slot(earlier);
                if ((earlier_slot == nullptr) || (earlier_slot->name_in_parent == child_slot->name_in_parent)) return false;
            }
        }
        previous = child;
        child = child_slot->next_sibling;
    }
    return (traversed == list.count) && (previous == list.last) &&
        (!list.first.is_valid() || !previous_sibling(list.first).is_valid()) &&
        (!list.last.is_valid() || !next_sibling(list.last).is_valid());
}

bool CLiveDocument::check_integrity() const noexcept
{
    if (!m_nodes.check_integrity() || !check_stable_strings(m_property_names) || !check_stable_strings(m_string_values)) return false;
    if (m_root.is_valid())
    {
        const CJsonSlot* const root_slot = node_slot(m_root);
        if ((root_slot == nullptr) || root_slot->parent.is_valid() || root_slot->previous_sibling.is_valid() ||
            root_slot->next_sibling.is_valid())
        {
            return false;
        }
    }
    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const CJsonSlot* const slot = m_nodes.get_slot(index);
        if ((slot == nullptr) || !slot->self.is_valid() || (node_slot(slot->self) != slot) || (slot->reserved != 0u))
        {
            return false;
        }
        if ((slot->type == EJsonNodeType::invalid) || (slot->type > EJsonNodeType::recovered_duplicate_array))
        {
            return false;
        }
        if (!json_numeric_flags_are_valid(slot->type, slot->flags) ||
            ((slot->type == EJsonNodeType::integer) && !json_integer_value_matches_flags(slot->flags, slot->payload.unsigned_bits)))
        {
            return false;
        }
        if (slot->parent.is_valid())
        {
            const CJsonSlot* const parent_slot = node_slot(slot->parent);
            if ((parent_slot == nullptr) || !is_container_type(parent_slot->type)) return false;
            CNodeKey ancestor = slot->parent;
            for (std::uint32_t depth = 0u; ancestor.is_valid(); ++depth)
            {
                if ((ancestor == slot->self) || (depth >= m_nodes.occupied_count())) return false;
                const CJsonSlot* const ancestor_slot = node_slot(ancestor);
                if (ancestor_slot == nullptr) return false;
                ancestor = ancestor_slot->parent;
            }
        }
        else if (slot->previous_sibling.is_valid() || slot->next_sibling.is_valid() || slot->name_in_parent.is_valid()) return false;
        if (slot->previous_sibling.is_valid())
        {
            const CJsonSlot* const previous = node_slot(slot->previous_sibling);
            if ((previous == nullptr) || (previous->next_sibling != slot->self) || (previous->parent != slot->parent)) return false;
        }
        if (slot->next_sibling.is_valid())
        {
            const CJsonSlot* const next = node_slot(slot->next_sibling);
            if ((next == nullptr) || (next->previous_sibling != slot->self) || (next->parent != slot->parent)) return false;
        }
        if (is_container_type(slot->type) && !check_container_integrity(*slot)) return false;
        if ((slot->type == EJsonNodeType::string) && !m_string_values.is_valid_id(slot->payload.string_value.query_value())) return false;
        if (slot->name_in_parent.is_valid() && !m_property_names.is_valid_id(slot->name_in_parent.query_value())) return false;
    }
    return true;
}

//==============================================================================
//  Baked-document promotion
//==============================================================================

CNodeKey CLiveDocument::append_from_baked(const CBakedDocument& source, const CBakedNodeIndex source_node) noexcept
{
    const EJsonNodeType source_type = source.node_type(source_node);
    CNodeKey destination_node;
    switch (source_type)
    {
        case EJsonNodeType::null_value:
        {
            destination_node = create_null();
            break;
        }
        case EJsonNodeType::boolean:
        {
            bool value = false;
            if (!source.boolean_value(source_node, value)) return CNodeKey{};
            destination_node = create_boolean(value);
            break;
        }
        case EJsonNodeType::integer:
        {
            CJsonIntegerMetadata metadata;
            if (!source.integer_metadata(source_node, metadata)) return CNodeKey{};
            if (metadata.sign == EJsonIntegerSign::unsigned_value)
            {
                std::uint64_t value = 0u;
                if (!source.unsigned_integer_value(source_node, value)) return CNodeKey{};
                destination_node = create_unsigned_integer(value, metadata);
            }
            else
            {
                std::int64_t value = 0;
                if (!source.integer_value(source_node, value)) return CNodeKey{};
                destination_node = create_integer(value, metadata);
            }
            break;
        }
        case EJsonNodeType::floating_point:
        {
            double value = 0.0;
            if (!source.floating_point_value(source_node, value)) return CNodeKey{};
            destination_node = create_floating_point(value);
            break;
        }
        case EJsonNodeType::string:
        {
            destination_node = create_string(source.string_value(source_node));
            break;
        }
        case EJsonNodeType::array:
        {
            destination_node = create_array();
            break;
        }
        case EJsonNodeType::object:
        {
            destination_node = create_object();
            break;
        }
        case EJsonNodeType::recovered_duplicate_array:
        {
            destination_node = create_node(EJsonNodeType::recovered_duplicate_array);
            break;
        }
        default:
        {
            return CNodeKey{};
        }
    }
    if (!destination_node.is_valid()) return CNodeKey{};

    if (!is_container_type(source_type)) return destination_node;
    for (CBakedNodeIndex source_child = source.first_child(source_node); source_child.is_valid();
        source_child = source.next_sibling(source_child))
    {
        const CNodeKey destination_child = append_from_baked(source, source_child);
        if (!destination_child.is_valid()) return CNodeKey{};

        const bool attached = (source_type == EJsonNodeType::object) ?
            add_object_child(destination_node, source.property_name(source.name_in_parent(source_child)), destination_child) :
            append_array_child(destination_node, destination_child);
        if (!attached) return CNodeKey{};
    }
    return destination_node;
}

void CLiveDocument::replace_with(CLiveDocument& source) noexcept
{
    m_nodes = std::move(source.m_nodes);
    m_property_names = std::move(source.m_property_names);
    m_string_values = std::move(source.m_string_values);
    m_root = source.m_root;
    m_next_node_key = source.m_next_node_key;
    source.m_root = CNodeKey{};
    source.m_next_node_key = 1u;
}

bool CLiveDocument::build_from(const CBakedDocument& source) noexcept
{
    if (!source.is_ready() || !source.check_integrity()) return false;

    CLiveDocument staged;
    if (!staged.initialise(source.node_count())) return false;
    const CNodeKey promoted_root = staged.append_from_baked(source, source.root());
    if (!promoted_root.is_valid() || !staged.set_root(promoted_root) ||
        (staged.m_nodes.occupied_count() != source.node_count()) || !staged.check_integrity())
    {
        return false;
    }
    replace_with(staged);
    return true;
}
