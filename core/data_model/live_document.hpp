
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_document.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    1 Sep 26
//
//  Mutable, single-threaded live document foundations.

#pragma once

#ifndef LIVE_DOCUMENT_HPP_INCLUDED
#define LIVE_DOCUMENT_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "containers/StringBuffers.hpp"
#include "containers/TPodOrderedSlots.hpp"
#include "containers/TPodVector.hpp"
#include "data_model/data_model_types.hpp"

struct SLiveDocumentTestAccess;

class CLiveDocument
{
public:
    CLiveDocument() noexcept = default;
    CLiveDocument(const CLiveDocument&) = delete;
    CLiveDocument& operator=(const CLiveDocument&) = delete;
    CLiveDocument(CLiveDocument&& source) noexcept;
    CLiveDocument& operator=(CLiveDocument&& source) noexcept;
    ~CLiveDocument() noexcept = default;

    //  A live document cannot be reattributed. Moves retain the allocation
    //  contexts already carried by its storage.
    [[nodiscard]] bool initialise(const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool reset(const std::size_t initial_node_capacity = 0u) noexcept;
    void deallocate() noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_canonical() const noexcept;
    [[nodiscard]] bool check_integrity() const noexcept;

    [[nodiscard]] CNodeKey root() const noexcept;

    //  Root-reachable structure only. Detached storage is excluded.
    [[nodiscard]] std::uint32_t value_count() const noexcept;
    [[nodiscard]] std::uint32_t aggregate_payload_count() const noexcept;
    [[nodiscard]] bool contains(const CNodeKey value) const noexcept;

    [[nodiscard]] ELiveValueType value_type(const CNodeKey value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CNodeKey value) const noexcept;
    [[nodiscard]] bool is_detached(const CNodeKey value) const noexcept;

    [[nodiscard]] CPropertyNameId name_id(const CNodeKey value) const noexcept;
    [[nodiscard]] CStringView name(const CNodeKey value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CStringValueId id) const noexcept;

    [[nodiscard]] CNodeKey parent(const CNodeKey value) const noexcept;
    [[nodiscard]] CNodeKey previous_sibling(const CNodeKey value) const noexcept;
    [[nodiscard]] CNodeKey next_sibling(const CNodeKey value) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CNodeKey container_value) const noexcept;
    [[nodiscard]] CNodeKey first_child(const CNodeKey container_value) const noexcept;
    [[nodiscard]] CNodeKey last_child(const CNodeKey container_value) const noexcept;

    [[nodiscard]] bool boolean_value(const CNodeKey node, bool& value) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CNodeKey node, std::int64_t& value) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CNodeKey node, std::uint64_t& value) const noexcept;
    [[nodiscard]] bool integer_metadata(const CNodeKey node, CIntegerMetadata& metadata) const noexcept;
    [[nodiscard]] bool floating_point_value(const CNodeKey node, double& value) const noexcept;
    [[nodiscard]] CStringValueId string_value_id(const CNodeKey node) const noexcept;
    [[nodiscard]] CStringView string_value(const CNodeKey node) const noexcept;

    //  Distinct non-empty entries referenced from the root-reachable structure.
    [[nodiscard]] std::uint32_t referenced_property_name_count() const noexcept;
    [[nodiscard]] std::uint32_t referenced_string_value_count() const noexcept;

    [[nodiscard]] CNodeKey create_null(const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_boolean(const bool value, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_signed_integer(const std::int64_t value, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_signed_integer(const std::int64_t value, const CIntegerMetadata& metadata, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_unsigned_integer(const std::uint64_t value, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_unsigned_integer(const std::uint64_t value, const CIntegerMetadata& metadata, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_floating_point(const double value, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_string(const CStringView& value, const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_array(const CStringView& name = {}) noexcept;
    [[nodiscard]] CNodeKey create_object(const CStringView& name = {}) noexcept;

    //  Complete direct ownership accounting. There is intentionally no
    //  reattribution surface.
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;

private:
    friend struct SLiveDocumentTestAccess;

    struct SLiveNodeUsage
    {
        ELiveNodeRole role{ ELiveNodeRole::invalid };
        ELiveValueType value_type{ ELiveValueType::invalid };
        ELiveAggregateKind aggregate_kind{ ELiveAggregateKind::invalid };
        std::uint8_t object_entry{ 0u };
    };

    struct SLiveNode
    {
        CNodeKey self;
        CNodeKey relation_0; // value parent aggregate; aggregate owner value
        CNodeKey relation_1; // value previous sibling; aggregate first child
        CNodeKey relation_2; // value next sibling; aggregate last child
        CNodeKey relation_3; // value owned aggregate; aggregate canonical invalid
        std::uint64_t payload_bits{ 0u };
        CPropertyNameId name;
        std::uint32_t child_count{ 0u };
        SLiveNodeUsage usage;
        CIntegerMetadata integer_metadata;

        [[nodiscard]] CNodeKey value_parent_aggregate_key() const noexcept;
        [[nodiscard]] CNodeKey value_previous_sibling_key() const noexcept;
        [[nodiscard]] CNodeKey value_next_sibling_key() const noexcept;
        [[nodiscard]] CNodeKey value_owned_aggregate_key() const noexcept;
        [[nodiscard]] CNodeKey aggregate_owner_value_key() const noexcept;
        [[nodiscard]] CNodeKey aggregate_first_child_key() const noexcept;
        [[nodiscard]] CNodeKey aggregate_last_child_key() const noexcept;
    };

    struct SPreparedString
    {
        const std::uint8_t* bytes{ nullptr };
        std::size_t size{ 0u };
        CByteBuffer normalized;
    };

    struct SPreparedDomainEntry
    {
        std::uint32_t id{ 0u };
        bool append{ false };
    };

    [[nodiscard]] static bool is_container_type(const ELiveValueType type) noexcept;
    [[nodiscard]] static ELiveAggregateKind aggregate_kind_for(const ELiveValueType type) noexcept;
    [[nodiscard]] static bool prepare_string(const CStringView& source, SPreparedString& prepared) noexcept;

    [[nodiscard]] bool allocate_key(CNodeKey& key) noexcept;
    [[nodiscard]] bool prepare_property_name(const SPreparedString& value, SPreparedDomainEntry& prepared) noexcept;
    [[nodiscard]] bool prepare_string_value(const SPreparedString& value, SPreparedDomainEntry& prepared) noexcept;
    [[nodiscard]] bool commit_property_name(const SPreparedString& value, const SPreparedDomainEntry& prepared) noexcept;
    [[nodiscard]] bool commit_string_value(const SPreparedString& value, const SPreparedDomainEntry& prepared) noexcept;

    [[nodiscard]] CNodeKey create_scalar(
        const ELiveValueType type,
        const std::uint64_t payload_bits,
        const CIntegerMetadata metadata,
        const SPreparedString& prepared_name) noexcept;
    [[nodiscard]] CNodeKey create_container(const ELiveValueType type, const SPreparedString& prepared_name) noexcept;
    [[nodiscard]] bool insert_root_pair() noexcept;

    [[nodiscard]] SLiveNode* node(const CNodeKey key) noexcept;
    [[nodiscard]] const SLiveNode* node(const CNodeKey key) const noexcept;
    [[nodiscard]] SLiveNode* value_node(const CNodeKey key) noexcept;
    [[nodiscard]] const SLiveNode* value_node(const CNodeKey key) const noexcept;
    [[nodiscard]] const SLiveNode* aggregate_for_value(const CNodeKey value) const noexcept;

    [[nodiscard]] bool check_string_domain(
        const CStableStrings& strings,
        const TPodVector<std::uint32_t>& counts,
        const bool stable_ready) const noexcept;
    void replace_with(CLiveDocument& source) noexcept;

    static_assert(std::is_trivially_copyable_v<SLiveNode>);
    static_assert(std::is_standard_layout_v<SLiveNode>);
    static_assert(sizeof(SLiveNode) == 64u);

    TPodOrderedSlots<SLiveNode, CNodeKey> m_nodes;
    CStableStrings m_property_names;
    CStableStrings m_string_values;
    TPodVector<std::uint32_t> m_property_name_counts;
    TPodVector<std::uint32_t> m_string_value_counts;
    CNodeKey m_root;
    std::uint64_t m_next_monotonic_node_key{ 1u };
    std::uint32_t m_value_count{ 0u };
    std::uint32_t m_aggregate_payload_count{ 0u };
    std::uint32_t m_referenced_property_name_count{ 0u };
    std::uint32_t m_referenced_string_value_count{ 0u };
    std::uint32_t m_recovered_aggregate_count{ 0u };
    bool m_property_names_ready{ false };
    bool m_string_values_ready{ false };
};

#endif // LIVE_DOCUMENT_HPP_INCLUDED
