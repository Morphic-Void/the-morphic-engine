
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_document_builder.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Aug 26
//
//  Owned dense document composition and live-to-baked conversion.

#pragma once

#ifndef BAKED_DOCUMENT_BUILDER_HPP_INCLUDED
#define BAKED_DOCUMENT_BUILDER_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "containers/StringBuffers.hpp"
#include "containers/TPodVector.hpp"
#include "data_model/live_document.hpp"
#include "text/utf8_string.hpp"

class CBakedDocumentBuilder
{
    friend class CBakedDocumentBlock;
public:

    //  Lifetime
    CBakedDocumentBuilder() noexcept = default;
    CBakedDocumentBuilder(const CBakedDocumentBuilder&) = delete;
    CBakedDocumentBuilder& operator=(const CBakedDocumentBuilder&) = delete;
    CBakedDocumentBuilder(CBakedDocumentBuilder&&) noexcept = default;
    CBakedDocumentBuilder& operator=(CBakedDocumentBuilder&&) noexcept = default;
    ~CBakedDocumentBuilder() noexcept = default;

    //  Construction
    //  The destination is changed only after a complete successful bake.
    [[nodiscard]] bool build_from(const CLiveDocument& source) noexcept;

    //  Deallocate
    void deallocate() noexcept;

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_canonical() const noexcept;
    [[nodiscard]] bool contains_recovered_duplicate_arrays() const noexcept;
    [[nodiscard]] bool requires_morphic_json_extensions() const noexcept;

    //  Root
    [[nodiscard]] CBakedNodeIndex root() const noexcept;

    //  Queries
    [[nodiscard]] std::uint32_t node_count() const noexcept;
    [[nodiscard]] std::uint32_t property_name_count() const noexcept;
    [[nodiscard]] std::uint32_t string_value_count() const noexcept;

    //  Value accessors
    [[nodiscard]] EJsonNodeType node_type(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] bool boolean_value(const CBakedNodeIndex node, bool& value) const noexcept;
    [[nodiscard]] bool integer_value(const CBakedNodeIndex node, std::int64_t& value) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CBakedNodeIndex node, std::uint64_t& value) const noexcept;
    [[nodiscard]] bool integer_metadata(const CBakedNodeIndex node, CJsonIntegerMetadata& metadata) const noexcept;
    [[nodiscard]] bool floating_point_value(const CBakedNodeIndex node, double& value) const noexcept;
    [[nodiscard]] CStringView string_value(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] CStringView string_value(const CStringValueId value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId name) const noexcept;

    //  Relationship accessors
    [[nodiscard]] CBakedNodeIndex parent(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] CPropertyNameId name_in_parent(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] CBakedNodeIndex previous_sibling(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] CBakedNodeIndex next_sibling(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CBakedNodeIndex container) const noexcept;
    [[nodiscard]] CBakedNodeIndex first_child(const CBakedNodeIndex container) const noexcept;
    [[nodiscard]] CBakedNodeIndex last_child(const CBakedNodeIndex container) const noexcept;
    [[nodiscard]] CBakedNodeIndex object_child(const CBakedNodeIndex object, const CPropertyNameId name) const noexcept;
    [[nodiscard]] CBakedNodeIndex object_child(const CBakedNodeIndex object, const CStringView& name) const noexcept;
    [[nodiscard]] CBakedNodeIndex array_at(const CBakedNodeIndex array, const std::uint32_t index) const noexcept;

    //  Integrity checking
    [[nodiscard]] bool check_integrity() const noexcept;

private:
    [[nodiscard]] static bool is_array_type(const EJsonNodeType type) noexcept;
    [[nodiscard]] static bool is_container_type(const EJsonNodeType type) noexcept;
    [[nodiscard]] static bool check_stable_strings(const CStableStrings& strings) noexcept;
    [[nodiscard]] static bool check_baked_strings(const CStableStrings& strings, std::uint32_t count) noexcept;
    [[nodiscard]] const CBakedNode* node_slot(const CBakedNodeIndex node) const noexcept;
    [[nodiscard]] CBakedNode* node_slot(const CBakedNodeIndex node) noexcept;
    [[nodiscard]] CBakedNodeIndex append_node(
        const CLiveDocument& source,
        const CNodeKey live_node,
        const CBakedNodeIndex parent,
        const CPropertyNameId name) noexcept;
    [[nodiscard]] bool build_children(const CLiveDocument& source, const CNodeKey live_parent, const CBakedNodeIndex baked_parent) noexcept;
    [[nodiscard]] CPropertyNameId copy_property_name(const CLiveDocument& source, const CPropertyNameId name) noexcept;
    [[nodiscard]] CStringValueId copy_string_value(const CStringView& value) noexcept;
    [[nodiscard]] bool check_container_integrity(const CBakedNodeIndex container) const noexcept;
    [[nodiscard]] bool parent_contains_child(const CBakedNodeIndex parent, const CBakedNodeIndex child) const noexcept;

    TPodVector<CBakedNode> m_nodes;
    CStableStrings m_property_names;
    CStableStrings m_string_values;
    CBakedNodeIndex m_root;
    std::uint32_t m_property_name_count{ 0u };
    std::uint32_t m_string_value_count{ 0u };
    bool m_contains_recovered_duplicate_arrays{ false };
    bool m_requires_morphic_json_extensions{ false };
};

//==============================================================================
//  CBakedDocumentBuilder out of class function bodies
//==============================================================================

inline void CBakedDocumentBuilder::deallocate() noexcept
{
    m_nodes.deallocate();
    m_property_names.deallocate();
    m_string_values.deallocate();
    m_root = CBakedNodeIndex{};
    m_property_name_count = 0u;
    m_string_value_count = 0u;
    m_contains_recovered_duplicate_arrays = false;
    m_requires_morphic_json_extensions = false;
}

inline bool CBakedDocumentBuilder::is_empty() const noexcept
{
    return !m_root.is_valid();
}

inline bool CBakedDocumentBuilder::is_canonical() const noexcept
{
    return !m_contains_recovered_duplicate_arrays && !m_requires_morphic_json_extensions;
}

inline bool CBakedDocumentBuilder::contains_recovered_duplicate_arrays() const noexcept
{
    return m_contains_recovered_duplicate_arrays;
}

inline bool CBakedDocumentBuilder::requires_morphic_json_extensions() const noexcept
{
    return m_requires_morphic_json_extensions;
}

inline CBakedNodeIndex CBakedDocumentBuilder::root() const noexcept
{
    return m_root;
}

inline std::uint32_t CBakedDocumentBuilder::node_count() const noexcept
{
    return (m_nodes.size() > 0u) ? static_cast<std::uint32_t>(m_nodes.size() - 1u) : 0u;
}

inline std::uint32_t CBakedDocumentBuilder::property_name_count() const noexcept
{
    return m_property_name_count;
}

inline std::uint32_t CBakedDocumentBuilder::string_value_count() const noexcept
{
    return m_string_value_count;
}

inline bool CBakedDocumentBuilder::is_array_type(const EJsonNodeType type) noexcept
{
    return (type == EJsonNodeType::array) || (type == EJsonNodeType::recovered_duplicate_array);
}

inline bool CBakedDocumentBuilder::is_container_type(const EJsonNodeType type) noexcept
{
    return is_array_type(type) || (type == EJsonNodeType::object);
}

inline bool CBakedDocumentBuilder::check_stable_strings(const CStableStrings& strings) noexcept
{
    return (strings.memory_allocation_count() == 0u) || strings.check_integrity();
}

inline bool CBakedDocumentBuilder::check_baked_strings(
    const CStableStrings& strings, const std::uint32_t count) noexcept
{
    if (!check_stable_strings(strings) || strings.is_valid_id(static_cast<std::size_t>(count) + 1u))
    {
        return false;
    }
    for (std::size_t id = 1u; id <= static_cast<std::size_t>(count); ++id)
    {
        const CStringView value = strings.view(id);
        std::size_t normalized_size = 0u;
        if ((value.string() == nullptr) ||
            !utf8_string::validate_and_measure(value.string(), value.length(), utf8_string::ELiteralNulPolicy::reject, normalized_size) ||
            (normalized_size != value.length()))
        {
            return false;
        }
    }
    return true;
}

inline bool CBakedDocumentBuilder::is_valid() const noexcept
{
    return m_nodes.is_valid() && check_stable_strings(m_property_names) && check_stable_strings(m_string_values);
}

inline bool CBakedDocumentBuilder::is_ready() const noexcept
{
    return is_valid() && m_root.is_valid() && (m_nodes.size() > m_root.query_value());
}

inline const CBakedNode* CBakedDocumentBuilder::node_slot(const CBakedNodeIndex node) const noexcept
{
    const std::size_t index = node.query_value();
    return (node.is_valid() && (index < m_nodes.size())) ? &m_nodes[index] : nullptr;
}

inline CBakedNode* CBakedDocumentBuilder::node_slot(const CBakedNodeIndex node) noexcept
{
    const std::size_t index = node.query_value();
    return (node.is_valid() && (index < m_nodes.size())) ? &m_nodes[index] : nullptr;
}

inline CPropertyNameId CBakedDocumentBuilder::copy_property_name(const CLiveDocument& source, const CPropertyNameId name) noexcept
{
    if (!name.is_valid())
    {
        return CPropertyNameId{};
    }
    const CStringView value = source.property_name(name);
    if (value.string() == nullptr)
    {
        return CPropertyNameId{};
    }
    std::size_t normalized_size = 0u;
    if (!utf8_string::validate_and_measure(value.string(), value.length(), utf8_string::ELiteralNulPolicy::promote_to_modified_utf8, normalized_size))
    {
        return CPropertyNameId{};
    }
    CByteBuffer normalized;
    const std::uint8_t* normalized_string = value.string();
    if (normalized_size != value.length())
    {
        if (!normalized.resize(normalized_size) ||
            !utf8_string::normalize_literal_nuls(value.string(), value.length(), normalized.data(), normalized.size()))
        {
            return CPropertyNameId{};
        }
        normalized_string = normalized.data();
    }
    const bool exists = m_property_names.find_id(normalized_string, normalized_size) != CStableStrings::k_invalid_id;
    const std::size_t id = m_property_names.append(normalized_string, normalized_size);
    if (!exists && (id != CStableStrings::k_invalid_id))
    {
        ++m_property_name_count;
    }
    if ((id == CStableStrings::k_invalid_id) || (id > std::numeric_limits<std::uint32_t>::max()))
    {
        return CPropertyNameId{};
    }
    return CPropertyNameId{ static_cast<std::uint32_t>(id) };
}

inline CStringValueId CBakedDocumentBuilder::copy_string_value(const CStringView& value) noexcept
{
    if (value.string() == nullptr)
    {
        return CStringValueId{};
    }
    std::size_t normalized_size = 0u;
    if (!utf8_string::validate_and_measure(value.string(), value.length(), utf8_string::ELiteralNulPolicy::promote_to_modified_utf8, normalized_size))
    {
        return CStringValueId{};
    }
    CByteBuffer normalized;
    const std::uint8_t* normalized_string = value.string();
    if (normalized_size != value.length())
    {
        if (!normalized.resize(normalized_size) ||
            !utf8_string::normalize_literal_nuls(value.string(), value.length(), normalized.data(), normalized.size()))
        {
            return CStringValueId{};
        }
        normalized_string = normalized.data();
    }
    const bool exists = m_string_values.find_id(normalized_string, normalized_size) != CStableStrings::k_invalid_id;
    const std::size_t id = m_string_values.append(normalized_string, normalized_size);
    if (!exists && (id != CStableStrings::k_invalid_id)) ++m_string_value_count;
    if ((id == CStableStrings::k_invalid_id) || (id > std::numeric_limits<std::uint32_t>::max()))
    {
        return CStringValueId{};
    }
    return CStringValueId{ static_cast<std::uint32_t>(id) };
}

inline CBakedNodeIndex CBakedDocumentBuilder::append_node(const CLiveDocument& source, const CNodeKey live_node, const CBakedNodeIndex parent_index, const CPropertyNameId name) noexcept
{
    if ((m_nodes.size() >= std::numeric_limits<std::uint32_t>::max()) || (source.node_type(live_node) == EJsonNodeType::invalid))
    {
        return CBakedNodeIndex{};
    }
    CBakedNode node{};
    node.parent = parent_index;
    node.name_in_parent = name;
    node.type = source.node_type(live_node);

    switch (node.type)
    {
        case EJsonNodeType::boolean:
        {
            bool value = false;
            if (!source.boolean_value(live_node, value))
            {
                return CBakedNodeIndex{};
            }
            node.payload.unsigned_bits = value ? 1u : 0u;
            break;
        }
        case EJsonNodeType::integer:
        {
            CJsonIntegerMetadata metadata;
            if (!source.integer_metadata(live_node, metadata)) return CBakedNodeIndex{};
            if (metadata.sign == EJsonIntegerSign::unsigned_value)
            {
                if (!source.unsigned_integer_value(live_node, node.payload.unsigned_bits))
                {
                    return CBakedNodeIndex{};
                }
            }
            else
            {
                std::int64_t value = 0;
                if (!source.integer_value(live_node, value))
                {
                    return CBakedNodeIndex{};
                }
                node.payload.unsigned_bits = json_integer_bits(value);
            }
            node.flags = json_integer_flags(metadata);
            break;
        }
        case EJsonNodeType::floating_point:
        {
            double value = 0.0;
            if (!source.floating_point_value(live_node, value) || !json_floating_point_is_finite(value))
            {
                return CBakedNodeIndex{};
            }
            node.payload.floating_value = value;
            break;
        }
        case EJsonNodeType::string:
        {
            node.payload.string_value = copy_string_value(source.string_value(live_node));
            if (!node.payload.string_value.is_valid())
            {
                return CBakedNodeIndex{};
            }
            break;
        }
        case EJsonNodeType::null_value:
        case EJsonNodeType::array:
        case EJsonNodeType::object:
        case EJsonNodeType::recovered_duplicate_array:
        {
            break;
        }
        default:
        {
            return CBakedNodeIndex{};
        }
    }

    if (!m_nodes.push_back(node))
    {
        return CBakedNodeIndex{};
    }
    if (node.type == EJsonNodeType::recovered_duplicate_array)
    {
        m_contains_recovered_duplicate_arrays = true;
    }
    if (node.type == EJsonNodeType::integer)
    {
        CJsonIntegerMetadata metadata;
        if (!json_integer_metadata_from_flags(node.flags, metadata))
        {
            return CBakedNodeIndex{};
        }
        m_requires_morphic_json_extensions = m_requires_morphic_json_extensions ||
            json_integer_requires_morphic_extensions(node.payload.unsigned_bits, metadata);
    }
    return CBakedNodeIndex{ static_cast<std::uint32_t>(m_nodes.size() - 1u) };
}

inline bool CBakedDocumentBuilder::build_children(const CLiveDocument& source, const CNodeKey live_parent, const CBakedNodeIndex baked_parent) noexcept
{
    const EJsonNodeType parent_type = source.node_type(live_parent);
    if (!is_container_type(parent_type))
    {
        return true;
    }
    CBakedNode* const parent_node = node_slot(baked_parent);
    if (parent_node == nullptr)
    {
        return false;
    }

    const std::size_t first_child = m_nodes.size();
    if (first_child > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    parent_node->first_child_index = static_cast<std::uint32_t>(first_child);
    parent_node->child_count = 0u;

    for (CNodeKey child = source.first_child(live_parent); child.is_valid(); child = source.next_sibling(child))
    {
        const CPropertyNameId name = (parent_type == EJsonNodeType::object) ?
            copy_property_name(source, source.name_in_parent(child)) : CPropertyNameId{};
        if ((parent_type == EJsonNodeType::object) && !name.is_valid())
        {
            return false;
        }
        const CBakedNodeIndex baked_child = append_node(source, child, baked_parent, name);
        if (!baked_child.is_valid())
        {
            return false;
        }
        ++node_slot(baked_parent)->child_count;
    }

    const std::uint32_t count = node_slot(baked_parent)->child_count;
    CNodeKey live_child = source.first_child(live_parent);
    for (std::uint32_t offset = 0u; offset < count; ++offset)
    {
        if (!live_child.is_valid())
        {
            return false;
        }
        const CBakedNodeIndex baked_child{ static_cast<std::uint32_t>(first_child + offset) };
        if (!build_children(source, live_child, baked_child))
        {
            return false;
        }
        live_child = source.next_sibling(live_child);
    }
    return !live_child.is_valid();
}

inline bool CBakedDocumentBuilder::build_from(const CLiveDocument& source) noexcept
{
    if (!source.is_ready() || !source.is_valid() || !source.root().is_valid() || !source.check_integrity())
    {
        return false;
    }

    CBakedDocumentBuilder staged;
    if (!staged.m_nodes.push_back(CBakedNode{}))
    {
        return false; //  index zero sentinel
    }
    const CBakedNodeIndex root = staged.append_node(source, source.root(), CBakedNodeIndex{}, CPropertyNameId{});
    if (!root.is_valid() || !staged.build_children(source, source.root(), root))
    {
        return false;
    }
    staged.m_root = root;
    if (!staged.check_integrity())
    {
        return false;
    }
    *this = std::move(staged);
    return true;
}

inline EJsonNodeType CBakedDocumentBuilder::node_type(const CBakedNodeIndex node) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    return (slot != nullptr) ? slot->type : EJsonNodeType::invalid;
}

inline bool CBakedDocumentBuilder::boolean_value(const CBakedNodeIndex node, bool& value) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    if ((slot == nullptr) || (slot->type != EJsonNodeType::boolean))
    {
        return false;
    }
    value = slot->payload.unsigned_bits != 0u;
    return true;
}

inline bool CBakedDocumentBuilder::integer_value(const CBakedNodeIndex node, std::int64_t& value) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    CJsonIntegerMetadata metadata;
    if ((slot == nullptr) || (slot->type != EJsonNodeType::integer) || !json_integer_metadata_from_flags(slot->flags, metadata) ||
        ((metadata.sign == EJsonIntegerSign::unsigned_value) && slot->payload.unsigned_bits > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
    {
        return false;
    }
    value = (metadata.sign == EJsonIntegerSign::signed_value) ?
        json_signed_integer_value(slot->payload.unsigned_bits) : static_cast<std::int64_t>(slot->payload.unsigned_bits);
    return true;
}

inline bool CBakedDocumentBuilder::unsigned_integer_value(const CBakedNodeIndex node, std::uint64_t& value) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    CJsonIntegerMetadata metadata;
    if ((slot == nullptr) || (slot->type != EJsonNodeType::integer) || !json_integer_metadata_from_flags(slot->flags, metadata) ||
        (metadata.sign == EJsonIntegerSign::signed_value))
    {
        return false;
    }
    value = slot->payload.unsigned_bits;
    return true;
}

inline bool CBakedDocumentBuilder::integer_metadata(const CBakedNodeIndex node, CJsonIntegerMetadata& metadata) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    return (slot != nullptr) && (slot->type == EJsonNodeType::integer) && json_integer_metadata_from_flags(slot->flags, metadata);
}

inline bool CBakedDocumentBuilder::floating_point_value(const CBakedNodeIndex node, double& value) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    if ((slot == nullptr) || (slot->type != EJsonNodeType::floating_point))
    {
        return false;
    }
    value = slot->payload.floating_value;
    return true;
}

inline CStringView CBakedDocumentBuilder::string_value(const CBakedNodeIndex node) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    return ((slot != nullptr) && (slot->type == EJsonNodeType::string)) ? string_value(slot->payload.string_value) : CStringView{};
}

inline CStringView CBakedDocumentBuilder::string_value(const CStringValueId value) const noexcept
{
    return value.is_valid() ? m_string_values.view(value.query_value()) : CStringView{};
}

inline CStringView CBakedDocumentBuilder::property_name(const CPropertyNameId name) const noexcept
{
    return name.is_valid() ? m_property_names.view(name.query_value()) : CStringView{};
}

inline CBakedNodeIndex CBakedDocumentBuilder::parent(const CBakedNodeIndex node) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    return (slot != nullptr) ? slot->parent : CBakedNodeIndex{};
}

inline CPropertyNameId CBakedDocumentBuilder::name_in_parent(const CBakedNodeIndex node) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    return (slot != nullptr) ? slot->name_in_parent : CPropertyNameId{};
}
inline CBakedNodeIndex CBakedDocumentBuilder::previous_sibling(const CBakedNodeIndex node) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    const CBakedNode* const parent_slot = (slot != nullptr) ? node_slot(slot->parent) : nullptr;
    const std::uint64_t first = (parent_slot != nullptr) ? parent_slot->first_child_index : 0u;
    const std::uint64_t index = node.query_value();
    if ((parent_slot == nullptr) || (index <= first) || (index >= (first + parent_slot->child_count)))
    {
        return CBakedNodeIndex{};
    }
    return CBakedNodeIndex{ static_cast<std::uint32_t>(index - 1u) };
}

inline CBakedNodeIndex CBakedDocumentBuilder::next_sibling(const CBakedNodeIndex node) const noexcept
{
    const CBakedNode* const slot = node_slot(node);
    const CBakedNode* const parent_slot = (slot != nullptr) ? node_slot(slot->parent) : nullptr;
    const std::uint64_t first = (parent_slot != nullptr) ? parent_slot->first_child_index : 0u;
    const std::uint64_t index = node.query_value();
    if ((parent_slot == nullptr) || (index < first) || ((index + 1u) >= (first + parent_slot->child_count)))
    {
        return CBakedNodeIndex{};
    }
    return CBakedNodeIndex{ static_cast<std::uint32_t>(index + 1u) };
}

inline std::uint32_t CBakedDocumentBuilder::child_count(const CBakedNodeIndex container) const noexcept
{
    const CBakedNode* const slot = node_slot(container);
    return ((slot != nullptr) && is_container_type(slot->type)) ? slot->child_count : 0u;
}

inline CBakedNodeIndex CBakedDocumentBuilder::array_at(const CBakedNodeIndex array, const std::uint32_t index) const noexcept
{
    const CBakedNode* const slot = node_slot(array);
    if ((slot == nullptr) || !is_array_type(slot->type) || (index >= slot->child_count))
    {
        return CBakedNodeIndex{};
    }
    return CBakedNodeIndex{ static_cast<std::uint32_t>(slot->first_child_index + index) };
}

inline CBakedNodeIndex CBakedDocumentBuilder::first_child(const CBakedNodeIndex container) const noexcept
{
    const CBakedNode* const slot = node_slot(container);
    if ((slot == nullptr) || !is_container_type(slot->type) || (slot->child_count == 0u))
    {
        return CBakedNodeIndex{};
    }
    return CBakedNodeIndex{ slot->first_child_index };
}

inline CBakedNodeIndex CBakedDocumentBuilder::last_child(const CBakedNodeIndex container) const noexcept
{
    const CBakedNode* const slot = node_slot(container);
    if ((slot == nullptr) || !is_container_type(slot->type) || (slot->child_count == 0u))
    {
        return CBakedNodeIndex{};
    }
    return CBakedNodeIndex{ static_cast<std::uint32_t>(slot->first_child_index + slot->child_count - 1u) };
}

inline CBakedNodeIndex CBakedDocumentBuilder::object_child(const CBakedNodeIndex object, const CPropertyNameId name) const noexcept
{
    const CBakedNode* const slot = node_slot(object);
    if ((slot == nullptr) || (slot->type != EJsonNodeType::object) || !name.is_valid())
    {
        return CBakedNodeIndex{};
    }
    for (std::uint32_t offset = 0u; offset < slot->child_count; ++offset)
    {
        const CBakedNodeIndex child_index{ static_cast<std::uint32_t>(slot->first_child_index + offset) };
        const CBakedNode* const child = node_slot(child_index);
        if ((child != nullptr) && (child->name_in_parent == name)) return child_index;
    }
    return CBakedNodeIndex{};
}

inline CBakedNodeIndex CBakedDocumentBuilder::object_child(const CBakedNodeIndex object, const CStringView& name) const noexcept
{
    if (name.string() == nullptr)
    {
        return CBakedNodeIndex{};
    }
    const CBakedNode* const object_slot = node_slot(object);
    if ((object_slot == nullptr) || (object_slot->type != EJsonNodeType::object))
    {
        return CBakedNodeIndex{};
    }
    for (std::uint32_t offset = 0u; offset < object_slot->child_count; ++offset)
    {
        const CBakedNodeIndex child{ static_cast<std::uint32_t>(object_slot->first_child_index + offset) };
        const CStringView child_name = property_name(name_in_parent(child));
        if ((child_name.string() != nullptr) && (child_name == name))
        {
            return child;
        }
    }
    return CBakedNodeIndex{};
}

inline bool CBakedDocumentBuilder::parent_contains_child(const CBakedNodeIndex parent_index, const CBakedNodeIndex child) const noexcept
{
    const CBakedNode* const parent_node = node_slot(parent_index);
    if ((parent_node == nullptr) || !is_container_type(parent_node->type))
    {
        return false;
    }
    const std::uint64_t first = parent_node->first_child_index;
    const std::uint64_t index = child.query_value();
    return (index >= first) && (index < (first + parent_node->child_count));
}

inline bool CBakedDocumentBuilder::check_container_integrity(const CBakedNodeIndex container_index) const noexcept
{
    const CBakedNode* const container = node_slot(container_index);
    if ((container == nullptr) || !is_container_type(container->type))
    {
        return false;
    }
    const std::uint64_t first = container->first_child_index;
    if ((container->child_count != 0u) &&
        ((first >= m_nodes.size()) || (container->child_count > (m_nodes.size() - first))))
    {
        return false;
    }
    for (std::uint32_t offset = 0u; offset < container->child_count; ++offset)
    {
        const CBakedNodeIndex child_index{ static_cast<std::uint32_t>(first + offset) };
        const CBakedNode* const child = node_slot(child_index);
        if ((child == nullptr) || (child->parent != container_index))
        {
            return false;
        }
        if ((container->type == EJsonNodeType::object) && !child->name_in_parent.is_valid())
        {
            return false;
        }
        if (is_array_type(container->type) && child->name_in_parent.is_valid())
        {
            return false;
        }
        if (container->type == EJsonNodeType::object)
        {
            for (std::uint32_t earlier = 0u; earlier < offset; ++earlier)
            {
                const CBakedNode* const earlier_node = node_slot(CBakedNodeIndex{ static_cast<std::uint32_t>(first + earlier) });
                if ((earlier_node == nullptr) || (earlier_node->name_in_parent == child->name_in_parent))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

inline bool CBakedDocumentBuilder::check_integrity() const noexcept
{
    if (!is_valid() || !m_root.is_valid() || (m_nodes.size() <= m_root.query_value()) ||
        (m_nodes[0].reserved != 0u) ||
        !check_baked_strings(m_property_names, m_property_name_count) ||
        !check_baked_strings(m_string_values, m_string_value_count))
    {
        return false;
    }
    bool contains_recovered_duplicate_arrays = false;
    bool requires_morphic_json_extensions = false;
    for (std::size_t index = 1u; index < m_nodes.size(); ++index)
    {
        const CBakedNodeIndex node_index{ static_cast<std::uint32_t>(index) };
        const CBakedNode& node = m_nodes[index];
        if ((node.type == EJsonNodeType::invalid) || (node.type > EJsonNodeType::recovered_duplicate_array) ||
            (node.reserved != 0u) || ((node.type == EJsonNodeType::floating_point) && !json_floating_point_is_finite(node.payload.floating_value)))
        {
            return false;
        }
        contains_recovered_duplicate_arrays = contains_recovered_duplicate_arrays || (node.type == EJsonNodeType::recovered_duplicate_array);
        if (!json_numeric_flags_are_valid(node.type, node.flags) ||
            ((node.type == EJsonNodeType::integer) && !json_integer_value_matches_flags(node.flags, node.payload.unsigned_bits)))
        {
            return false;
        }
        if (node.type == EJsonNodeType::integer)
        {
            CJsonIntegerMetadata metadata;
            if (!json_integer_metadata_from_flags(node.flags, metadata))
            {
                return false;
            }
            requires_morphic_json_extensions = requires_morphic_json_extensions ||
                json_integer_requires_morphic_extensions(node.payload.unsigned_bits, metadata);
        }
        if (node_index == m_root)
        {
            if (node.parent.is_valid() || node.name_in_parent.is_valid())
            {
                return false;
            }
        }
        else if (!node.parent.is_valid() || !parent_contains_child(node.parent, node_index))
        {
            return false;
        }
        if (is_container_type(node.type) && !check_container_integrity(node_index))
        {
            return false;
        }
        if ((node.type == EJsonNodeType::string) && !m_string_values.is_valid_id(node.payload.string_value.query_value()))
        {
            return false;
        }
        if (node.name_in_parent.is_valid() && !m_property_names.is_valid_id(node.name_in_parent.query_value()))
        {
            return false;
        }
    }
    return
        (contains_recovered_duplicate_arrays == m_contains_recovered_duplicate_arrays) &&
        (requires_morphic_json_extensions == m_requires_morphic_json_extensions);
}

#endif  //  BAKED_DOCUMENT_BUILDER_HPP_INCLUDED
