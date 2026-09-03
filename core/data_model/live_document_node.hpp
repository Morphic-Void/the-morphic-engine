
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_document_node.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    2 Sep 26
//
//  Internal live-document node representation and node-local invariants.

#pragma once

#ifndef LIVE_DOCUMENT_NODE_HPP_INCLUDED
#define LIVE_DOCUMENT_NODE_HPP_INCLUDED

#include <cstdint>
#include <type_traits>

#include "data_model/data_model_types.hpp"

struct SLiveDocumentTestAccess;

[[nodiscard]] constexpr bool live_value_type_is_container(const ELiveValueType type) noexcept;

[[nodiscard]] constexpr ELiveAggregateKind live_aggregate_kind_for_value_type(const ELiveValueType type) noexcept;

struct SLiveNodeUsage
{
    ELiveNodeRole role{ ELiveNodeRole::invalid };
    ELiveValueType value_type{ ELiveValueType::invalid };
    ELiveAggregateKind aggregate_kind{ ELiveAggregateKind::invalid };
    std::uint8_t object_entry{ 0u };
};

class CLiveNode
{
public:

    //  Default lifetime
    CLiveNode() noexcept = default;
    ~CLiveNode() noexcept = default;

    //  Record role and common payload
    [[nodiscard]] bool is_value_record() const noexcept;
    [[nodiscard]] bool is_aggregate_record() const noexcept;
    [[nodiscard]] CNodeKey key() const noexcept;
    [[nodiscard]] std::uint64_t payload_bits() const noexcept;
    [[nodiscard]] CPropertyNameId name_id() const noexcept;
    [[nodiscard]] std::uint32_t child_count() const noexcept;
    [[nodiscard]] ELiveValueType value_type() const noexcept;
    [[nodiscard]] ELiveAggregateKind aggregate_kind() const noexcept;
    [[nodiscard]] std::uint8_t object_entry_state() const noexcept;
    [[nodiscard]] bool is_object_entry() const noexcept;
    [[nodiscard]] CIntegerMetadata integer_metadata() const noexcept;

    //  Role-specific relationships
    [[nodiscard]] CNodeKey value_parent_aggregate_key() const noexcept;
    [[nodiscard]] CNodeKey value_previous_sibling_key() const noexcept;
    [[nodiscard]] CNodeKey value_next_sibling_key() const noexcept;
    [[nodiscard]] CNodeKey value_owned_aggregate_key() const noexcept;
    [[nodiscard]] CNodeKey aggregate_owner_value_key() const noexcept;
    [[nodiscard]] CNodeKey aggregate_first_child_key() const noexcept;
    [[nodiscard]] CNodeKey aggregate_last_child_key() const noexcept;

    //  Relationship mutation
    void set_value_parent_aggregate_key(const CNodeKey key) noexcept;
    void set_value_previous_sibling_key(const CNodeKey key) noexcept;
    void set_value_next_sibling_key(const CNodeKey key) noexcept;
    void set_value_owned_aggregate_key(const CNodeKey key) noexcept;
    void set_aggregate_owner_value_key(const CNodeKey key) noexcept;
    void set_aggregate_first_child_key(const CNodeKey key) noexcept;
    void set_aggregate_last_child_key(const CNodeKey key) noexcept;
    void set_value_attachment(const CNodeKey parent, const CNodeKey previous, const CNodeKey next) noexcept;
    void clear_value_attachment() noexcept;
    void clear_aggregate_children() noexcept;
    void increment_child_count() noexcept;
    void decrement_child_count() noexcept;

    //  Node-local validity
    [[nodiscard]] bool value_payload_is_valid() const noexcept;
    [[nodiscard]] bool aggregate_payload_is_valid() const noexcept;
    [[nodiscard]] bool forms_container_pair_with(const CLiveNode& aggregate) const noexcept;
    [[nodiscard]] bool aggregate_accepts_child(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool value_is_unattached() const noexcept;
    [[nodiscard]] bool value_attachment_is_consistent() const noexcept;
    [[nodiscard]] bool aggregate_child_range_is_consistent() const noexcept;
    [[nodiscard]] bool aggregate_unused_relation_is_invalid() const noexcept;
    [[nodiscard]] bool value_is_previous_sibling_of(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool value_is_next_sibling_of(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool aggregate_has_first_child(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool aggregate_has_last_child(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool aggregate_has_adjacent_children(const CLiveNode& previous, const CLiveNode& next) const noexcept;

    //  Role initialization
    void initialise_value(
        const CNodeKey key,
        const ELiveValueType type,
        const std::uint64_t payload_bits,
        const CPropertyNameId name,
        const CIntegerMetadata metadata) noexcept;

    void initialise_aggregate(
        const CNodeKey key,
        const CNodeKey owner,
        const ELiveAggregateKind kind,
        const CPropertyNameId name) noexcept;

private:
    friend struct SLiveDocumentTestAccess;

    CNodeKey m_self;
    CNodeKey m_relation_0; // value parent aggregate; aggregate owner value
    CNodeKey m_relation_1; // value previous sibling; aggregate first child
    CNodeKey m_relation_2; // value next sibling; aggregate last child
    CNodeKey m_relation_3; // value owned aggregate; aggregate canonical invalid
    std::uint64_t m_payload_bits{ 0u };
    CPropertyNameId m_name;
    std::uint32_t m_child_count{ 0u };
    SLiveNodeUsage m_usage;
    CIntegerMetadata m_integer_metadata;
};

static_assert(std::is_trivially_copyable_v<CLiveNode>);
static_assert(std::is_standard_layout_v<CLiveNode>);
static_assert(sizeof(CLiveNode) == 64u);

//==============================================================================
//  Value and aggregate type helpers
//==============================================================================

[[nodiscard]] constexpr bool live_value_type_is_container(const ELiveValueType type) noexcept
{
    return (type == ELiveValueType::array) || (type == ELiveValueType::object);
}

[[nodiscard]] constexpr ELiveAggregateKind live_aggregate_kind_for_value_type(const ELiveValueType type) noexcept
{
    return (type == ELiveValueType::array) ? ELiveAggregateKind::array :
        ((type == ELiveValueType::object) ? ELiveAggregateKind::object : ELiveAggregateKind::invalid);
}

//==============================================================================
//  CLiveNode: record role and common payload
//==============================================================================

inline bool CLiveNode::is_value_record() const noexcept
{
    return m_usage.role == ELiveNodeRole::value;
}

inline bool CLiveNode::is_aggregate_record() const noexcept
{
    return m_usage.role == ELiveNodeRole::aggregate;
}

inline CNodeKey CLiveNode::key() const noexcept
{
    return m_self;
}

inline std::uint64_t CLiveNode::payload_bits() const noexcept
{
    return m_payload_bits;
}

inline CPropertyNameId CLiveNode::name_id() const noexcept
{
    return m_name;
}

inline std::uint32_t CLiveNode::child_count() const noexcept
{
    return m_child_count;
}

inline ELiveValueType CLiveNode::value_type() const noexcept
{
    return m_usage.value_type;
}

inline ELiveAggregateKind CLiveNode::aggregate_kind() const noexcept
{
    return m_usage.aggregate_kind;
}

inline std::uint8_t CLiveNode::object_entry_state() const noexcept
{
    return m_usage.object_entry;
}

inline bool CLiveNode::is_object_entry() const noexcept
{
    return m_usage.object_entry != 0u;
}

inline CIntegerMetadata CLiveNode::integer_metadata() const noexcept
{
    return m_integer_metadata;
}

//==============================================================================
//  CLiveNode: role-specific relationships
//==============================================================================

inline CNodeKey CLiveNode::value_parent_aggregate_key() const noexcept
{
    return is_value_record() ? m_relation_0 : CNodeKey{};
}

inline CNodeKey CLiveNode::value_previous_sibling_key() const noexcept
{
    return is_value_record() ? m_relation_1 : CNodeKey{};
}

inline CNodeKey CLiveNode::value_next_sibling_key() const noexcept
{
    return is_value_record() ? m_relation_2 : CNodeKey{};
}

inline CNodeKey CLiveNode::value_owned_aggregate_key() const noexcept
{
    return is_value_record() ? m_relation_3 : CNodeKey{};
}

inline CNodeKey CLiveNode::aggregate_owner_value_key() const noexcept
{
    return is_aggregate_record() ? m_relation_0 : CNodeKey{};
}

inline CNodeKey CLiveNode::aggregate_first_child_key() const noexcept
{
    return is_aggregate_record() ? m_relation_1 : CNodeKey{};
}

inline CNodeKey CLiveNode::aggregate_last_child_key() const noexcept
{
    return is_aggregate_record() ? m_relation_2 : CNodeKey{};
}

//==============================================================================
//  CLiveNode: relationship mutation
//==============================================================================

inline void CLiveNode::set_value_parent_aggregate_key(const CNodeKey key) noexcept
{
    m_relation_0 = key;
}

inline void CLiveNode::set_value_previous_sibling_key(const CNodeKey key) noexcept
{
    m_relation_1 = key;
}

inline void CLiveNode::set_value_next_sibling_key(const CNodeKey key) noexcept
{
    m_relation_2 = key;
}

inline void CLiveNode::set_value_owned_aggregate_key(const CNodeKey key) noexcept
{
    m_relation_3 = key;
}

inline void CLiveNode::set_aggregate_owner_value_key(const CNodeKey key) noexcept
{
    m_relation_0 = key;
}

inline void CLiveNode::set_aggregate_first_child_key(const CNodeKey key) noexcept
{
    m_relation_1 = key;
}

inline void CLiveNode::set_aggregate_last_child_key(const CNodeKey key) noexcept
{
    m_relation_2 = key;
}

inline void CLiveNode::set_value_attachment(const CNodeKey parent, const CNodeKey previous, const CNodeKey next) noexcept
{
    set_value_parent_aggregate_key(parent);
    set_value_previous_sibling_key(previous);
    set_value_next_sibling_key(next);
}

inline void CLiveNode::clear_value_attachment() noexcept
{
    set_value_attachment(CNodeKey{}, CNodeKey{}, CNodeKey{});
}

inline void CLiveNode::clear_aggregate_children() noexcept
{
    set_aggregate_first_child_key(CNodeKey{});
    set_aggregate_last_child_key(CNodeKey{});
    m_child_count = 0u;
}

inline void CLiveNode::increment_child_count() noexcept
{
    ++m_child_count;
}

inline void CLiveNode::decrement_child_count() noexcept
{
    --m_child_count;
}

//==============================================================================
//  CLiveNode: node-local validity
//==============================================================================

inline bool CLiveNode::value_payload_is_valid() const noexcept
{
    if (!is_value_record() ||
        (value_type() == ELiveValueType::invalid) ||
        (aggregate_kind() != ELiveAggregateKind::invalid) ||
        (object_entry_state() > 1u) ||
        ((name_id().query_value() != 0u) != is_object_entry()) ||
        !name_id().is_valid())
    {
        return false;
    }

    if (live_value_type_is_container(value_type()))
    {
        return value_owned_aggregate_key().is_valid() &&
            (payload_bits() == 0u) && (integer_metadata() == CIntegerMetadata{});
    }
    if (value_owned_aggregate_key().is_valid())
    {
        return false;
    }

    switch (value_type())
    {
        case ELiveValueType::null_value:
        {
            return (payload_bits() == 0u) && (integer_metadata() == CIntegerMetadata{});
        }
        case ELiveValueType::boolean:
        {
            return (payload_bits() <= 1u) && (integer_metadata() == CIntegerMetadata{});
        }
        case ELiveValueType::integer:
        {
            return (integer_metadata().domain == EIntegerDomain::signed_value) ?
                live_integer_metadata_matches_signed(live_signed_integer_from_bits(payload_bits()), integer_metadata()) :
                live_integer_metadata_matches_unsigned(payload_bits(), integer_metadata());
        }
        case ELiveValueType::floating_point:
        {
            return live_floating_point_is_finite(live_floating_point_from_bits(payload_bits())) && (integer_metadata() == CIntegerMetadata{});
        }
        case ELiveValueType::string:
        {
            return (payload_bits() < CStringValueId::k_invalid_value) && (integer_metadata() == CIntegerMetadata{});
        }
        default:
        {
            return false;
        }
    }
}

inline bool CLiveNode::aggregate_payload_is_valid() const noexcept
{
    const ELiveAggregateKind kind = aggregate_kind();
    return is_aggregate_record() &&
        (value_type() == ELiveValueType::invalid) &&
        ((kind == ELiveAggregateKind::array) || (kind == ELiveAggregateKind::object) || (kind == ELiveAggregateKind::recovered_array)) &&
        (object_entry_state() == 0u) &&
        aggregate_owner_value_key().is_valid() &&
        aggregate_child_range_is_consistent() &&
        aggregate_unused_relation_is_invalid() &&
        (payload_bits() == 0u) && name_id().is_valid() &&
        (integer_metadata() == CIntegerMetadata{});
}

inline bool CLiveNode::forms_container_pair_with(const CLiveNode& aggregate) const noexcept
{
    if (!is_value_record() ||
        !aggregate.is_aggregate_record() ||
        !live_value_type_is_container(value_type()) ||
        (value_owned_aggregate_key() != aggregate.key()) ||
        (aggregate.aggregate_owner_value_key() != key()) ||
        (name_id() != aggregate.name_id()))
    {
        return false;
    }

    if (value_type() == ELiveValueType::object)
    {
        return aggregate.aggregate_kind() == ELiveAggregateKind::object;
    }
    return (aggregate.aggregate_kind() == ELiveAggregateKind::array) ||
        ((aggregate.aggregate_kind() == ELiveAggregateKind::recovered_array) && is_object_entry());
}

inline bool CLiveNode::aggregate_accepts_child(const CLiveNode& value) const noexcept
{
    if (!is_aggregate_record() || !value.is_value_record())
    {
        return false;
    }
    if (aggregate_kind() == ELiveAggregateKind::object)
    {
        return value.is_object_entry();
    }
    return (aggregate_kind() == ELiveAggregateKind::array) ||
        ((aggregate_kind() == ELiveAggregateKind::recovered_array) && !value.is_object_entry());
}

inline bool CLiveNode::value_is_unattached() const noexcept
{
    return is_value_record() && !m_relation_0.is_valid() && !m_relation_1.is_valid() && !m_relation_2.is_valid();
}

inline bool CLiveNode::value_attachment_is_consistent() const noexcept
{
    return is_value_record() &&
        (m_relation_0.is_valid() || (!m_relation_1.is_valid() && !m_relation_2.is_valid()));
}

inline bool CLiveNode::aggregate_child_range_is_consistent() const noexcept
{
    return is_aggregate_record() &&
        ((m_child_count == 0u) == !m_relation_1.is_valid()) &&
        ((m_child_count == 0u) == !m_relation_2.is_valid());
}

inline bool CLiveNode::aggregate_unused_relation_is_invalid() const noexcept
{
    return is_aggregate_record() && !m_relation_3.is_valid();
}

inline bool CLiveNode::value_is_previous_sibling_of(const CLiveNode& value) const noexcept
{
    return is_value_record() && value.is_value_record() && m_relation_0.is_valid() &&
        (m_relation_0 == value.m_relation_0) && (m_relation_2 == value.m_self) &&
        (value.m_relation_1 == m_self);
}

inline bool CLiveNode::value_is_next_sibling_of(const CLiveNode& value) const noexcept
{
    return is_value_record() && value.is_value_record() && m_relation_0.is_valid() &&
        (m_relation_0 == value.m_relation_0) && (m_relation_1 == value.m_self) &&
        (value.m_relation_2 == m_self);
}

inline bool CLiveNode::aggregate_has_first_child(const CLiveNode& value) const noexcept
{
    return is_aggregate_record() && value.is_value_record() &&
        (m_relation_1 == value.m_self) && (value.m_relation_0 == m_self) &&
        !value.m_relation_1.is_valid();
}

inline bool CLiveNode::aggregate_has_last_child(const CLiveNode& value) const noexcept
{
    return is_aggregate_record() && value.is_value_record() &&
        (m_relation_2 == value.m_self) && (value.m_relation_0 == m_self) &&
        !value.m_relation_2.is_valid();
}

inline bool CLiveNode::aggregate_has_adjacent_children(const CLiveNode& previous, const CLiveNode& next) const noexcept
{
    return is_aggregate_record() && (previous.m_relation_0 == m_self) &&
        (next.m_relation_0 == m_self) && previous.value_is_previous_sibling_of(next);
}

//==============================================================================
//  CLiveNode: role initialization
//==============================================================================

inline void CLiveNode::initialise_value(
    const CNodeKey key,
    const ELiveValueType type,
    const std::uint64_t payload_bits,
    const CPropertyNameId name,
    const CIntegerMetadata metadata) noexcept
{
    *this = CLiveNode{};
    m_self = key;
    m_payload_bits = payload_bits;
    m_name = name;
    m_usage.role = ELiveNodeRole::value;
    m_usage.value_type = type;
    m_usage.object_entry = (name.query_value() != 0u) ? 1u : 0u;
    m_integer_metadata = metadata;
}

inline void CLiveNode::initialise_aggregate(
    const CNodeKey key,
    const CNodeKey owner,
    const ELiveAggregateKind kind,
    const CPropertyNameId name) noexcept
{
    *this = CLiveNode{};
    m_self = key;
    m_relation_0 = owner;
    m_name = name;
    m_usage.role = ELiveNodeRole::aggregate;
    m_usage.aggregate_kind = kind;
}

#endif // LIVE_DOCUMENT_NODE_HPP_INCLUDED
