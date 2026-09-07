
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
    ELiveValueType value_type{ ELiveValueType::invalid };
    ELiveAggregateKind aggregate_kind{ ELiveAggregateKind::invalid };
};

using LiveNodeSlot = std::int32_t;
constexpr LiveNodeSlot k_invalid_live_node_slot = -1;

struct SLiveValueLinks
{
    LiveNodeSlot parent_aggregate;
    LiveNodeSlot previous_sibling;
    LiveNodeSlot next_sibling;
    LiveNodeSlot owned_aggregate;
};

struct SLiveAggregateLinks
{
    LiveNodeSlot owner_value;
    LiveNodeSlot first_child;
    LiveNodeSlot last_child;
};

union SLiveNodeLinks
{
    constexpr SLiveNodeLinks() noexcept : value{
        k_invalid_live_node_slot,
        k_invalid_live_node_slot,
        k_invalid_live_node_slot,
        k_invalid_live_node_slot } {}

    SLiveValueLinks value;
    SLiveAggregateLinks aggregate;
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
    [[nodiscard]] std::uint64_t payload_bits() const noexcept;
    [[nodiscard]] CPropertyNameId name_id() const noexcept;
    [[nodiscard]] std::uint32_t child_count() const noexcept;
    [[nodiscard]] ELiveValueType value_type() const noexcept;
    [[nodiscard]] ELiveAggregateKind aggregate_kind() const noexcept;
    [[nodiscard]] bool is_object_entry() const noexcept;
    [[nodiscard]] CIntegerMetadata integer_metadata() const noexcept;

    //  Role-specific relationships
    [[nodiscard]] LiveNodeSlot value_parent_aggregate_slot() const noexcept;
    [[nodiscard]] LiveNodeSlot value_previous_sibling_slot() const noexcept;
    [[nodiscard]] LiveNodeSlot value_next_sibling_slot() const noexcept;
    [[nodiscard]] LiveNodeSlot value_owned_aggregate_slot() const noexcept;
    [[nodiscard]] LiveNodeSlot aggregate_owner_value_slot() const noexcept;
    [[nodiscard]] LiveNodeSlot aggregate_first_child_slot() const noexcept;
    [[nodiscard]] LiveNodeSlot aggregate_last_child_slot() const noexcept;

    //  Relationship mutation
    void set_value_parent_aggregate_slot(const LiveNodeSlot slot) noexcept;
    void set_value_previous_sibling_slot(const LiveNodeSlot slot) noexcept;
    void set_value_next_sibling_slot(const LiveNodeSlot slot) noexcept;
    void set_value_owned_aggregate_slot(const LiveNodeSlot slot) noexcept;
    void set_aggregate_owner_value_slot(const LiveNodeSlot slot) noexcept;
    void set_aggregate_first_child_slot(const LiveNodeSlot slot) noexcept;
    void set_aggregate_last_child_slot(const LiveNodeSlot slot) noexcept;
    void set_value_attachment(const LiveNodeSlot parent, const LiveNodeSlot previous, const LiveNodeSlot next) noexcept;
    void clear_value_attachment() noexcept;
    void clear_aggregate_children() noexcept;
    void increment_child_count() noexcept;
    void decrement_child_count() noexcept;

    //  Value payload mutation preserves the name and tree attachment.
    void clear_value_payload() noexcept;
    void move_value_payload_from(CLiveNode& source) noexcept;

    //  Node-local validity
    [[nodiscard]] bool value_payload_is_valid() const noexcept;
    [[nodiscard]] bool aggregate_payload_is_valid() const noexcept;
    [[nodiscard]] bool forms_container_pair_with(
        const CLiveNode& aggregate,
        const LiveNodeSlot value_slot,
        const LiveNodeSlot aggregate_slot) const noexcept;
    [[nodiscard]] bool aggregate_accepts_child(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool value_is_unattached() const noexcept;
    [[nodiscard]] bool value_attachment_is_consistent() const noexcept;
    [[nodiscard]] bool aggregate_child_range_is_consistent() const noexcept;
    [[nodiscard]] bool value_is_previous_sibling_of(
        const CLiveNode& value,
        const LiveNodeSlot own_slot,
        const LiveNodeSlot value_slot) const noexcept;
    [[nodiscard]] bool value_is_next_sibling_of(
        const CLiveNode& value,
        const LiveNodeSlot own_slot,
        const LiveNodeSlot value_slot) const noexcept;
    [[nodiscard]] bool aggregate_has_first_child(
        const CLiveNode& value,
        const LiveNodeSlot aggregate_slot,
        const LiveNodeSlot value_slot) const noexcept;
    [[nodiscard]] bool aggregate_has_last_child(
        const CLiveNode& value,
        const LiveNodeSlot aggregate_slot,
        const LiveNodeSlot value_slot) const noexcept;
    [[nodiscard]] bool aggregate_has_adjacent_children(
        const CLiveNode& previous,
        const CLiveNode& next,
        const LiveNodeSlot aggregate_slot,
        const LiveNodeSlot previous_slot,
        const LiveNodeSlot next_slot) const noexcept;

    //  Role initialization
    void initialise_value(
        const ELiveValueType type,
        const std::uint64_t payload_bits,
        const CPropertyNameId name,
        const CIntegerMetadata metadata) noexcept;

    void initialise_aggregate(
        const LiveNodeSlot owner,
        const ELiveAggregateKind kind,
        const CPropertyNameId empty_name) noexcept;

private:
    friend struct SLiveDocumentTestAccess;

    SLiveNodeLinks m_links;
    std::uint64_t m_payload_bits{ 0u };
    CPropertyNameId m_name;
    std::uint32_t m_child_count{ 0u };
    CIntegerMetadata m_integer_metadata;
    SLiveNodeUsage m_usage;
};

static_assert(std::is_trivially_copyable_v<CLiveNode>);
static_assert(std::is_standard_layout_v<CLiveNode>);
static_assert(sizeof(CLiveNode) == 40u);

//==============================================================================
//  Value and aggregate type helpers
//==============================================================================

[[nodiscard]] constexpr bool live_value_type_is_container(const ELiveValueType type) noexcept
{
    return
        (type == ELiveValueType::array) ||
        (type == ELiveValueType::object) ||
        (type == ELiveValueType::recovered_array);
}

[[nodiscard]] constexpr ELiveAggregateKind live_aggregate_kind_for_value_type(const ELiveValueType type) noexcept
{
    return (type == ELiveValueType::array) ? ELiveAggregateKind::array :
        ((type == ELiveValueType::object) ? ELiveAggregateKind::object :
            ((type == ELiveValueType::recovered_array) ?
                ELiveAggregateKind::recovered_array : ELiveAggregateKind::invalid));
}

//==============================================================================
//  CLiveNode: record role and common payload
//==============================================================================

inline bool CLiveNode::is_value_record() const noexcept
{
    return (m_usage.value_type != ELiveValueType::invalid) && (m_usage.aggregate_kind == ELiveAggregateKind::invalid);
}

inline bool CLiveNode::is_aggregate_record() const noexcept
{
    return (m_usage.value_type == ELiveValueType::invalid) && (m_usage.aggregate_kind != ELiveAggregateKind::invalid);
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

inline bool CLiveNode::is_object_entry() const noexcept
{
    return is_value_record() && name_id().is_valid() && !name_id().is_empty();
}

inline CIntegerMetadata CLiveNode::integer_metadata() const noexcept
{
    return m_integer_metadata;
}

//==============================================================================
//  CLiveNode: role-specific relationships
//==============================================================================

inline LiveNodeSlot CLiveNode::value_parent_aggregate_slot() const noexcept
{
    return is_value_record() ? m_links.value.parent_aggregate : k_invalid_live_node_slot;
}

inline LiveNodeSlot CLiveNode::value_previous_sibling_slot() const noexcept
{
    return is_value_record() ? m_links.value.previous_sibling : k_invalid_live_node_slot;
}

inline LiveNodeSlot CLiveNode::value_next_sibling_slot() const noexcept
{
    return is_value_record() ? m_links.value.next_sibling : k_invalid_live_node_slot;
}

inline LiveNodeSlot CLiveNode::value_owned_aggregate_slot() const noexcept
{
    return is_value_record() ? m_links.value.owned_aggregate : k_invalid_live_node_slot;
}

inline LiveNodeSlot CLiveNode::aggregate_owner_value_slot() const noexcept
{
    return is_aggregate_record() ? m_links.aggregate.owner_value : k_invalid_live_node_slot;
}

inline LiveNodeSlot CLiveNode::aggregate_first_child_slot() const noexcept
{
    return is_aggregate_record() ? m_links.aggregate.first_child : k_invalid_live_node_slot;
}

inline LiveNodeSlot CLiveNode::aggregate_last_child_slot() const noexcept
{
    return is_aggregate_record() ? m_links.aggregate.last_child : k_invalid_live_node_slot;
}

//==============================================================================
//  CLiveNode: relationship mutation
//==============================================================================

inline void CLiveNode::set_value_parent_aggregate_slot(const LiveNodeSlot slot) noexcept
{
    m_links.value.parent_aggregate = slot;
}

inline void CLiveNode::set_value_previous_sibling_slot(const LiveNodeSlot slot) noexcept
{
    m_links.value.previous_sibling = slot;
}

inline void CLiveNode::set_value_next_sibling_slot(const LiveNodeSlot slot) noexcept
{
    m_links.value.next_sibling = slot;
}

inline void CLiveNode::set_value_owned_aggregate_slot(const LiveNodeSlot slot) noexcept
{
    m_links.value.owned_aggregate = slot;
}

inline void CLiveNode::set_aggregate_owner_value_slot(const LiveNodeSlot slot) noexcept
{
    m_links.aggregate.owner_value = slot;
}

inline void CLiveNode::set_aggregate_first_child_slot(const LiveNodeSlot slot) noexcept
{
    m_links.aggregate.first_child = slot;
}

inline void CLiveNode::set_aggregate_last_child_slot(const LiveNodeSlot slot) noexcept
{
    m_links.aggregate.last_child = slot;
}

inline void CLiveNode::set_value_attachment(const LiveNodeSlot parent, const LiveNodeSlot previous, const LiveNodeSlot next) noexcept
{
    set_value_parent_aggregate_slot(parent);
    set_value_previous_sibling_slot(previous);
    set_value_next_sibling_slot(next);
}

inline void CLiveNode::clear_value_attachment() noexcept
{
    set_value_attachment(k_invalid_live_node_slot, k_invalid_live_node_slot, k_invalid_live_node_slot);
}

inline void CLiveNode::clear_aggregate_children() noexcept
{
    set_aggregate_first_child_slot(k_invalid_live_node_slot);
    set_aggregate_last_child_slot(k_invalid_live_node_slot);
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

inline void CLiveNode::clear_value_payload() noexcept
{
    m_links.value.owned_aggregate = k_invalid_live_node_slot;
    m_payload_bits = 0u;
    m_usage.value_type = ELiveValueType::empty;
    m_integer_metadata = CIntegerMetadata{};
}

inline void CLiveNode::move_value_payload_from(CLiveNode& source) noexcept
{
    m_links.value.owned_aggregate = source.m_links.value.owned_aggregate;
    m_payload_bits = source.m_payload_bits;
    m_usage.value_type = source.m_usage.value_type;
    m_integer_metadata = source.m_integer_metadata;
    source.clear_value_payload();
}

//==============================================================================
//  CLiveNode: node-local validity
//==============================================================================

inline bool CLiveNode::value_payload_is_valid() const noexcept
{
    if (!is_value_record() ||
        (value_type() == ELiveValueType::invalid) ||
        (aggregate_kind() != ELiveAggregateKind::invalid) ||
        !name_id().is_valid())
    {
        return false;
    }

    if (live_value_type_is_container(value_type()))
    {
        return (value_owned_aggregate_slot() >= 0) && (payload_bits() == 0u) && (integer_metadata() == CIntegerMetadata{});
    }
    if (value_owned_aggregate_slot() != k_invalid_live_node_slot)
    {
        return false;
    }

    switch (value_type())
    {
        case ELiveValueType::empty:
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
        (aggregate_owner_value_slot() >= 0) &&
        aggregate_child_range_is_consistent() &&
        (payload_bits() == 0u) && name_id().is_empty() &&
        (integer_metadata() == CIntegerMetadata{});
}

inline bool CLiveNode::forms_container_pair_with(const CLiveNode& aggregate, const LiveNodeSlot value_slot, const LiveNodeSlot aggregate_slot) const noexcept
{
    if (!is_value_record() ||
        !aggregate.is_aggregate_record() ||
        !live_value_type_is_container(value_type()) ||
        (value_owned_aggregate_slot() != aggregate_slot) ||
        (aggregate.aggregate_owner_value_slot() != value_slot))
    {
        return false;
    }

    return aggregate.aggregate_kind() == live_aggregate_kind_for_value_type(value_type());
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
    return is_value_record() &&
        (m_links.value.parent_aggregate == k_invalid_live_node_slot) &&
        (m_links.value.previous_sibling == k_invalid_live_node_slot) &&
        (m_links.value.next_sibling == k_invalid_live_node_slot);
}

inline bool CLiveNode::value_attachment_is_consistent() const noexcept
{
    if (!is_value_record())
    {
        return false;
    }
    if (m_links.value.parent_aggregate >= 0)
    {
        return (m_links.value.previous_sibling >= k_invalid_live_node_slot) &&
            (m_links.value.next_sibling >= k_invalid_live_node_slot);
    }
    return value_is_unattached();
}

inline bool CLiveNode::aggregate_child_range_is_consistent() const noexcept
{
    if (!is_aggregate_record())
    {
        return false;
    }
    return (m_child_count == 0u) ?
        ((m_links.aggregate.first_child == k_invalid_live_node_slot) &&
            (m_links.aggregate.last_child == k_invalid_live_node_slot)) :
        ((m_links.aggregate.first_child >= 0) && (m_links.aggregate.last_child >= 0));
}

inline bool CLiveNode::value_is_previous_sibling_of(
    const CLiveNode& value,
    const LiveNodeSlot own_slot,
    const LiveNodeSlot value_slot) const noexcept
{
    return is_value_record() && value.is_value_record() &&
        (m_links.value.parent_aggregate >= 0) &&
        (m_links.value.parent_aggregate == value.m_links.value.parent_aggregate) &&
        (m_links.value.next_sibling == value_slot) &&
        (value.m_links.value.previous_sibling == own_slot);
}

inline bool CLiveNode::value_is_next_sibling_of(
    const CLiveNode& value,
    const LiveNodeSlot own_slot,
    const LiveNodeSlot value_slot) const noexcept
{
    return is_value_record() && value.is_value_record() &&
        (m_links.value.parent_aggregate >= 0) &&
        (m_links.value.parent_aggregate == value.m_links.value.parent_aggregate) &&
        (m_links.value.previous_sibling == value_slot) &&
        (value.m_links.value.next_sibling == own_slot);
}

inline bool CLiveNode::aggregate_has_first_child(
    const CLiveNode& value,
    const LiveNodeSlot aggregate_slot,
    const LiveNodeSlot value_slot) const noexcept
{
    return is_aggregate_record() && value.is_value_record() &&
        (m_links.aggregate.first_child == value_slot) &&
        (value.m_links.value.parent_aggregate == aggregate_slot) &&
        (value.m_links.value.previous_sibling == k_invalid_live_node_slot);
}

inline bool CLiveNode::aggregate_has_last_child(
    const CLiveNode& value,
    const LiveNodeSlot aggregate_slot,
    const LiveNodeSlot value_slot) const noexcept
{
    return is_aggregate_record() && value.is_value_record() &&
        (m_links.aggregate.last_child == value_slot) &&
        (value.m_links.value.parent_aggregate == aggregate_slot) &&
        (value.m_links.value.next_sibling == k_invalid_live_node_slot);
}

inline bool CLiveNode::aggregate_has_adjacent_children(
    const CLiveNode& previous,
    const CLiveNode& next,
    const LiveNodeSlot aggregate_slot,
    const LiveNodeSlot previous_slot,
    const LiveNodeSlot next_slot) const noexcept
{
    return is_aggregate_record() &&
        (previous.m_links.value.parent_aggregate == aggregate_slot) &&
        (next.m_links.value.parent_aggregate == aggregate_slot) &&
        previous.value_is_previous_sibling_of(next, previous_slot, next_slot);
}

//==============================================================================
//  CLiveNode: role initialization
//==============================================================================

inline void CLiveNode::initialise_value(
    const ELiveValueType type,
    const std::uint64_t payload_bits,
    const CPropertyNameId name,
    const CIntegerMetadata metadata) noexcept
{
    *this = CLiveNode{};
    m_payload_bits = payload_bits;
    m_name = name;
    m_usage.value_type = type;
    m_integer_metadata = metadata;
}

inline void CLiveNode::initialise_aggregate(
    const LiveNodeSlot owner,
    const ELiveAggregateKind kind,
    const CPropertyNameId empty_name) noexcept
{
    *this = CLiveNode{};
    m_links.aggregate = SLiveAggregateLinks{
        owner,
        k_invalid_live_node_slot,
        k_invalid_live_node_slot };
    m_name = empty_name;
    m_usage.aggregate_kind = kind;
}

#endif // LIVE_DOCUMENT_NODE_HPP_INCLUDED
