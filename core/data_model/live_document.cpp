
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_document.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    1 Sep 26
//
//  Mutable live-document construction and foundational validation.

#include "data_model/live_document.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include "memory/memory_policies.hpp"
#include "text/utf8_string.hpp"

CLiveDocument::CLiveDocument(CLiveDocument&& source) noexcept
{
    replace_with(source);
}

CLiveDocument& CLiveDocument::operator=(CLiveDocument&& source) noexcept
{
    replace_with(source);
    return *this;
}

CNodeKey CLiveDocument::SLiveNode::value_parent_aggregate_key() const noexcept
{
    return (usage.role == ELiveNodeRole::value) ? relation_0 : CNodeKey{};
}

CNodeKey CLiveDocument::SLiveNode::value_previous_sibling_key() const noexcept
{
    return (usage.role == ELiveNodeRole::value) ? relation_1 : CNodeKey{};
}

CNodeKey CLiveDocument::SLiveNode::value_next_sibling_key() const noexcept
{
    return (usage.role == ELiveNodeRole::value) ? relation_2 : CNodeKey{};
}

CNodeKey CLiveDocument::SLiveNode::value_owned_aggregate_key() const noexcept
{
    return (usage.role == ELiveNodeRole::value) ? relation_3 : CNodeKey{};
}

CNodeKey CLiveDocument::SLiveNode::aggregate_owner_value_key() const noexcept
{
    return (usage.role == ELiveNodeRole::aggregate) ? relation_0 : CNodeKey{};
}

CNodeKey CLiveDocument::SLiveNode::aggregate_first_child_key() const noexcept
{
    return (usage.role == ELiveNodeRole::aggregate) ? relation_1 : CNodeKey{};
}

CNodeKey CLiveDocument::SLiveNode::aggregate_last_child_key() const noexcept
{
    return (usage.role == ELiveNodeRole::aggregate) ? relation_2 : CNodeKey{};
}

bool CLiveDocument::is_container_type(const ELiveValueType type) noexcept
{
    return (type == ELiveValueType::array) || (type == ELiveValueType::object);
}

ELiveAggregateKind CLiveDocument::aggregate_kind_for(const ELiveValueType type) noexcept
{
    return (type == ELiveValueType::array) ? ELiveAggregateKind::array :
        ((type == ELiveValueType::object) ? ELiveAggregateKind::object : ELiveAggregateKind::invalid);
}

bool CLiveDocument::prepare_string(const CStringView& source, SPreparedString& prepared) noexcept
{
    prepared.bytes = source.string();
    prepared.size = source.length();
    prepared.normalized.deallocate();

    if (prepared.size == 0u)
    {
        return true;
    }
    if (prepared.bytes == nullptr)
    {
        return false;
    }

    std::size_t normalized_size = 0u;
    if (!utf8_string::validate_and_measure(
        prepared.bytes,
        prepared.size,
        utf8_string::ELiteralNulPolicy::promote_to_modified_utf8,
        normalized_size))
    {
        return false;
    }

    if (!prepared.normalized.resize(normalized_size))
    {
        return false;
    }
    if (normalized_size != prepared.size)
    {
        if (!utf8_string::normalize_literal_nuls(
            prepared.bytes, prepared.size, prepared.normalized.data(), normalized_size))
        {
            return false;
        }
    }
    else
    {
        std::memcpy(prepared.normalized.data(), prepared.bytes, prepared.size);
    }
    prepared.bytes = prepared.normalized.data();
    prepared.size = normalized_size;
    return true;
}

bool CLiveDocument::allocate_key(CNodeKey& key) noexcept
{
    key = CNodeKey{};
    if (m_next_monotonic_node_key == 0u)
    {
        return false;
    }

    key = CNodeKey{ m_next_monotonic_node_key };
    m_next_monotonic_node_key =
        (m_next_monotonic_node_key == std::numeric_limits<std::uint64_t>::max()) ?
        0u : (m_next_monotonic_node_key + 1u);
    return true;
}

bool CLiveDocument::prepare_property_name(const SPreparedString& value, SPreparedDomainEntry& prepared) noexcept
{
    prepared = SPreparedDomainEntry{};
    if (value.size == 0u)
    {
        return true;
    }

    const std::size_t found = m_property_names.find_id(value.bytes, value.size);
    if (found != CStableStrings::k_invalid_id)
    {
        if (found >= CPropertyNameId::k_invalid_value)
        {
            return false;
        }
        prepared.id = static_cast<std::uint32_t>(found);
        return true;
    }

    const std::size_t next_id = m_property_name_counts.size();
    if (next_id >= CPropertyNameId::k_invalid_value)
    {
        return false;
    }

    if (!m_property_names_ready)
    {
        const std::size_t storage_size = value.size + 2u;
        if ((storage_size < value.size) || !m_property_names.initialise(4u, storage_size))
        {
            return false;
        }
        m_property_names_ready = true;
    }
    else if (!m_property_names.ensure_free(value.size))
    {
        return false;
    }

    if (!m_property_name_counts.ensure_free(1u))
    {
        return false;
    }

    prepared.id = static_cast<std::uint32_t>(next_id);
    prepared.append = true;
    return true;
}

bool CLiveDocument::prepare_string_value(const SPreparedString& value, SPreparedDomainEntry& prepared) noexcept
{
    prepared = SPreparedDomainEntry{};
    if (value.size == 0u)
    {
        return true;
    }

    const std::size_t found = m_string_values.find_id(value.bytes, value.size);
    if (found != CStableStrings::k_invalid_id)
    {
        if (found >= CStringValueId::k_invalid_value)
        {
            return false;
        }
        prepared.id = static_cast<std::uint32_t>(found);
        return true;
    }

    const std::size_t next_id = m_string_value_counts.size();
    if (next_id >= CStringValueId::k_invalid_value)
    {
        return false;
    }

    if (!m_string_values_ready)
    {
        const std::size_t storage_size = value.size + 2u;
        if ((storage_size < value.size) || !m_string_values.initialise(4u, storage_size))
        {
            return false;
        }
        m_string_values_ready = true;
    }
    else if (!m_string_values.ensure_free(value.size))
    {
        return false;
    }

    if (!m_string_value_counts.ensure_free(1u))
    {
        return false;
    }

    prepared.id = static_cast<std::uint32_t>(next_id);
    prepared.append = true;
    return true;
}

bool CLiveDocument::commit_property_name(const SPreparedString& value, const SPreparedDomainEntry& prepared) noexcept
{
    if (!prepared.append)
    {
        return true;
    }

    const std::size_t id = m_property_names.append(value.bytes, value.size);
    if ((id != prepared.id) || !m_property_name_counts.push_back(0u))
    {
        return false;
    }
    return true;
}

bool CLiveDocument::commit_string_value(const SPreparedString& value, const SPreparedDomainEntry& prepared) noexcept
{
    if (!prepared.append)
    {
        return true;
    }

    const std::size_t id = m_string_values.append(value.bytes, value.size);
    if ((id != prepared.id) || !m_string_value_counts.push_back(0u))
    {
        return false;
    }
    return true;
}

CNodeKey CLiveDocument::create_scalar(
    const ELiveValueType type,
    const std::uint64_t payload_bits,
    const CIntegerMetadata metadata,
    const SPreparedString& prepared_name) noexcept
{
    if (!is_ready() || is_container_type(type) || (type == ELiveValueType::invalid))
    {
        return CNodeKey{};
    }

    CNodeKey key;
    if (!allocate_key(key))
    {
        return CNodeKey{};
    }

    SPreparedDomainEntry prepared_name_entry;
    if (!prepare_property_name(prepared_name, prepared_name_entry))
    {
        return CNodeKey{};
    }

    SLiveNode new_node{};
    new_node.self = key;
    new_node.payload_bits = payload_bits;
    new_node.name = CPropertyNameId{ prepared_name_entry.id };
    new_node.usage.role = ELiveNodeRole::value;
    new_node.usage.value_type = type;
    new_node.usage.object_entry = (prepared_name_entry.id != 0u) ? 1u : 0u;
    new_node.integer_metadata = metadata;

    if (m_nodes.insert(key, new_node) < 0)
    {
        return CNodeKey{};
    }
    if (!commit_property_name(prepared_name, prepared_name_entry))
    {
        (void)m_nodes.erase(key);
        return CNodeKey{};
    }

    return key;
}

CNodeKey CLiveDocument::create_container(const ELiveValueType type, const SPreparedString& prepared_name) noexcept
{
    if (!is_ready() || !is_container_type(type))
    {
        return CNodeKey{};
    }

    CNodeKey value_key;
    CNodeKey aggregate_key;
    if (!allocate_key(value_key) || !allocate_key(aggregate_key))
    {
        return CNodeKey{};
    }

    SPreparedDomainEntry prepared_name_entry;
    if (!prepare_property_name(prepared_name, prepared_name_entry))
    {
        return CNodeKey{};
    }

    const CPropertyNameId name_id{ prepared_name_entry.id };
    SLiveNode value{};
    value.self = value_key;
    value.relation_3 = aggregate_key;
    value.name = name_id;
    value.usage.role = ELiveNodeRole::value;
    value.usage.value_type = type;
    value.usage.object_entry = (prepared_name_entry.id != 0u) ? 1u : 0u;

    SLiveNode aggregate{};
    aggregate.self = aggregate_key;
    aggregate.relation_0 = value_key;
    aggregate.name = name_id;
    aggregate.usage.role = ELiveNodeRole::aggregate;
    aggregate.usage.aggregate_kind = aggregate_kind_for(type);

    if (m_nodes.insert(value_key, value) < 0)
    {
        return CNodeKey{};
    }
    if (m_nodes.insert(aggregate_key, aggregate) < 0)
    {
        (void)m_nodes.erase(value_key);
        return CNodeKey{};
    }
    if (!commit_property_name(prepared_name, prepared_name_entry))
    {
        (void)m_nodes.erase(aggregate_key);
        (void)m_nodes.erase(value_key);
        return CNodeKey{};
    }

    return value_key;
}

bool CLiveDocument::insert_root_pair() noexcept
{
    CNodeKey root_key;
    CNodeKey aggregate_key;
    if (!allocate_key(root_key) || !allocate_key(aggregate_key))
    {
        return false;
    }

    SLiveNode root_node{};
    root_node.self = root_key;
    root_node.relation_3 = aggregate_key;
    root_node.name = CPropertyNameId{ CPropertyNameId::k_empty_value };
    root_node.usage.role = ELiveNodeRole::value;
    root_node.usage.value_type = ELiveValueType::object;

    SLiveNode aggregate{};
    aggregate.self = aggregate_key;
    aggregate.relation_0 = root_key;
    aggregate.name = CPropertyNameId{ CPropertyNameId::k_empty_value };
    aggregate.usage.role = ELiveNodeRole::aggregate;
    aggregate.usage.aggregate_kind = ELiveAggregateKind::object;

    if (m_nodes.insert(root_key, root_node) < 0)
    {
        return false;
    }
    if (m_nodes.insert(aggregate_key, aggregate) < 0)
    {
        (void)m_nodes.erase(root_key);
        return false;
    }

    m_root = root_key;
    m_value_count = 1u;
    m_aggregate_payload_count = 1u;
    m_property_name_counts[0u] = 2u;
    return true;
}

bool CLiveDocument::initialise(const std::size_t initial_node_capacity) noexcept
{
    if (is_ready() || (memory_allocation_count() != 0u) ||
        (initial_node_capacity > std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }

    CLiveDocument staged;
    if (!staged.m_nodes.initialise((initial_node_capacity < 2u) ? 2u : initial_node_capacity) ||
        !staged.m_property_name_counts.resize(1u) ||
        !staged.m_string_value_counts.resize(1u))
    {
        return false;
    }

    staged.m_property_name_counts[0u] = 0u;
    staged.m_string_value_counts[0u] = 0u;

    if (!staged.insert_root_pair() || !staged.check_integrity())
    {
        return false;
    }

    replace_with(staged);
    return true;
}

bool CLiveDocument::reset(const std::size_t initial_node_capacity) noexcept
{
    deallocate();
    return initialise(initial_node_capacity);
}

void CLiveDocument::deallocate() noexcept
{
    m_nodes.deallocate();
    m_property_names.deallocate();
    m_string_values.deallocate();
    m_property_name_counts.deallocate();
    m_string_value_counts.deallocate();
    m_root = CNodeKey{};
    m_next_monotonic_node_key = 1u;
    m_value_count = 0u;
    m_aggregate_payload_count = 0u;
    m_referenced_property_name_count = 0u;
    m_referenced_string_value_count = 0u;
    m_recovered_aggregate_count = 0u;
    m_property_names_ready = false;
    m_string_values_ready = false;
}

bool CLiveDocument::is_ready() const noexcept
{
    return
        m_nodes.is_ready() && m_property_name_counts.is_ready() &&
        m_string_value_counts.is_ready() && m_root.is_valid();
}

bool CLiveDocument::is_canonical() const noexcept
{
    return is_ready() && (m_recovered_aggregate_count == 0u);
}

CNodeKey CLiveDocument::root() const noexcept
{
    return is_ready() ? m_root : CNodeKey{};
}

std::uint32_t CLiveDocument::value_count() const noexcept
{
    return is_ready() ? m_value_count : 0u;
}

std::uint32_t CLiveDocument::aggregate_payload_count() const noexcept
{
    return is_ready() ? m_aggregate_payload_count : 0u;
}

CLiveDocument::SLiveNode* CLiveDocument::node(const CNodeKey key) noexcept
{
    return key.is_valid() ? m_nodes.get_slot(key) : nullptr;
}

const CLiveDocument::SLiveNode* CLiveDocument::node(const CNodeKey key) const noexcept
{
    return key.is_valid() ? m_nodes.get_slot(key) : nullptr;
}

CLiveDocument::SLiveNode* CLiveDocument::value_node(const CNodeKey key) noexcept
{
    SLiveNode* const found = node(key);
    return ((found != nullptr) && (found->usage.role == ELiveNodeRole::value)) ? found : nullptr;
}

const CLiveDocument::SLiveNode* CLiveDocument::value_node(const CNodeKey key) const noexcept
{
    const SLiveNode* const found = node(key);
    return ((found != nullptr) && (found->usage.role == ELiveNodeRole::value)) ? found : nullptr;
}

const CLiveDocument::SLiveNode* CLiveDocument::aggregate_for_value(const CNodeKey value) const noexcept
{
    const SLiveNode* const owner = value_node(value);
    if ((owner == nullptr) || !is_container_type(owner->usage.value_type))
    {
        return nullptr;
    }
    const SLiveNode* const aggregate = node(owner->value_owned_aggregate_key());
    return ((aggregate != nullptr) && (aggregate->usage.role == ELiveNodeRole::aggregate)) ? aggregate : nullptr;
}

bool CLiveDocument::contains(const CNodeKey value) const noexcept
{
    return value_node(value) != nullptr;
}

ELiveValueType CLiveDocument::value_type(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->usage.value_type : ELiveValueType::invalid;
}

bool CLiveDocument::is_object_entry(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    return (found != nullptr) && (found->usage.object_entry != 0u);
}

bool CLiveDocument::is_detached(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    return (found != nullptr) && (value != m_root) &&
        !found->value_parent_aggregate_key().is_valid() &&
        !found->value_previous_sibling_key().is_valid() &&
        !found->value_next_sibling_key().is_valid();
}

CPropertyNameId CLiveDocument::name_id(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->name : CPropertyNameId{};
}

CStringView CLiveDocument::name(const CNodeKey value) const noexcept
{
    return property_name(name_id(value));
}

CStringView CLiveDocument::property_name(const CPropertyNameId id) const noexcept
{
    if (!id.is_valid() || (id.query_value() >= m_property_name_counts.size()))
    {
        return CStringView{};
    }
    return m_property_names.view(id.query_value());
}

CStringView CLiveDocument::string_value(const CStringValueId id) const noexcept
{
    if (!id.is_valid() || (id.query_value() >= m_string_value_counts.size()))
    {
        return CStringView{};
    }
    return m_string_values.view(id.query_value());
}

CNodeKey CLiveDocument::parent(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    if ((found == nullptr) || !found->value_parent_aggregate_key().is_valid())
    {
        return CNodeKey{};
    }
    const SLiveNode* const aggregate = node(found->value_parent_aggregate_key());
    return (aggregate != nullptr) ? aggregate->aggregate_owner_value_key() : CNodeKey{};
}

CNodeKey CLiveDocument::previous_sibling(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->value_previous_sibling_key() : CNodeKey{};
}

CNodeKey CLiveDocument::next_sibling(const CNodeKey value) const noexcept
{
    const SLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->value_next_sibling_key() : CNodeKey{};
}

std::uint32_t CLiveDocument::child_count(const CNodeKey container_value) const noexcept
{
    const SLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->child_count : 0u;
}

CNodeKey CLiveDocument::first_child(const CNodeKey container_value) const noexcept
{
    const SLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->aggregate_first_child_key() : CNodeKey{};
}

CNodeKey CLiveDocument::last_child(const CNodeKey container_value) const noexcept
{
    const SLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->aggregate_last_child_key() : CNodeKey{};
}

bool CLiveDocument::boolean_value(const CNodeKey key, bool& value) const noexcept
{
    const SLiveNode* const found = value_node(key);
    if ((found == nullptr) || (found->usage.value_type != ELiveValueType::boolean) || (found->payload_bits > 1u))
    {
        return false;
    }
    value = found->payload_bits != 0u;
    return true;
}

bool CLiveDocument::signed_integer_value(const CNodeKey key, std::int64_t& value) const noexcept
{
    const SLiveNode* const found = value_node(key);
    if ((found == nullptr) ||
        (found->usage.value_type != ELiveValueType::integer) ||
        (found->integer_metadata.domain != EIntegerDomain::signed_value))
    {
        return false;
    }
    value = live_signed_integer_from_bits(found->payload_bits);
    return true;
}

bool CLiveDocument::unsigned_integer_value(const CNodeKey key, std::uint64_t& value) const noexcept
{
    const SLiveNode* const found = value_node(key);
    if ((found == nullptr) ||
        (found->usage.value_type != ELiveValueType::integer) ||
        (found->integer_metadata.domain != EIntegerDomain::unsigned_value))
    {
        return false;
    }
    value = found->payload_bits;
    return true;
}

bool CLiveDocument::integer_metadata(const CNodeKey key, CIntegerMetadata& metadata) const noexcept
{
    const SLiveNode* const found = value_node(key);
    if ((found == nullptr) || (found->usage.value_type != ELiveValueType::integer))
    {
        return false;
    }
    metadata = found->integer_metadata;
    return true;
}

bool CLiveDocument::floating_point_value(const CNodeKey key, double& value) const noexcept
{
    const SLiveNode* const found = value_node(key);
    if ((found == nullptr) || (found->usage.value_type != ELiveValueType::floating_point))
    {
        return false;
    }
    value = live_floating_point_from_bits(found->payload_bits);
    return true;
}

CStringValueId CLiveDocument::string_value_id(const CNodeKey key) const noexcept
{
    const SLiveNode* const found = value_node(key);
    return ((found != nullptr) && (found->usage.value_type == ELiveValueType::string) &&
        (found->payload_bits < CStringValueId::k_invalid_value)) ?
        CStringValueId{ static_cast<std::uint32_t>(found->payload_bits) } : CStringValueId{};
}

CStringView CLiveDocument::string_value(const CNodeKey key) const noexcept
{
    return string_value(string_value_id(key));
}

std::uint32_t CLiveDocument::referenced_property_name_count() const noexcept
{
    return is_ready() ? m_referenced_property_name_count : 0u;
}

std::uint32_t CLiveDocument::referenced_string_value_count() const noexcept
{
    return is_ready() ? m_referenced_string_value_count : 0u;
}

CNodeKey CLiveDocument::create_null(const CStringView& name_value) noexcept
{
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_scalar(ELiveValueType::null_value, 0u, CIntegerMetadata{}, prepared_name) : CNodeKey{};
}

CNodeKey CLiveDocument::create_boolean(const bool value, const CStringView& name_value) noexcept
{
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_scalar(ELiveValueType::boolean, value ? 1u : 0u, CIntegerMetadata{}, prepared_name) : CNodeKey{};
}

CNodeKey CLiveDocument::create_signed_integer(const std::int64_t value, const CStringView& name_value) noexcept
{
    const CIntegerMetadata metadata{
        EIntegerDomain::signed_value,
        live_signed_integer_smallest_width(value),
        EIntegerNotation::decimal,
        EIntegerPrefix::standard };
    return create_signed_integer(value, metadata, name_value);
}

CNodeKey CLiveDocument::create_signed_integer(const std::int64_t value, const CIntegerMetadata& metadata, const CStringView& name_value) noexcept
{
    if (!live_integer_metadata_matches_signed(value, metadata))
    {
        return CNodeKey{};
    }
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_scalar(ELiveValueType::integer, live_signed_integer_bits(value), metadata, prepared_name) : CNodeKey{};
}

CNodeKey CLiveDocument::create_unsigned_integer(const std::uint64_t value, const CStringView& name_value) noexcept
{
    const CIntegerMetadata metadata{
        EIntegerDomain::unsigned_value,
        live_unsigned_integer_smallest_width(value),
        EIntegerNotation::decimal,
        EIntegerPrefix::standard };
    return create_unsigned_integer(value, metadata, name_value);
}

CNodeKey CLiveDocument::create_unsigned_integer(
    const std::uint64_t value, const CIntegerMetadata& metadata, const CStringView& name_value) noexcept
{
    if (!live_integer_metadata_matches_unsigned(value, metadata))
    {
        return CNodeKey{};
    }
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_scalar(ELiveValueType::integer, value, metadata, prepared_name) : CNodeKey{};
}

CNodeKey CLiveDocument::create_floating_point(const double value, const CStringView& name_value) noexcept
{
    if (!live_floating_point_is_finite(value))
    {
        return CNodeKey{};
    }
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_scalar(ELiveValueType::floating_point, live_floating_point_bits(value), CIntegerMetadata{}, prepared_name) :
        CNodeKey{};
}

CNodeKey CLiveDocument::create_string(const CStringView& value, const CStringView& name_value) noexcept
{
    SPreparedString prepared_value;
    SPreparedString prepared_name;
    if (!prepare_string(value, prepared_value) || !prepare_string(name_value, prepared_name) || !is_ready())
    {
        return CNodeKey{};
    }

    CNodeKey key;
    if (!allocate_key(key))
    {
        return CNodeKey{};
    }

    SPreparedDomainEntry prepared_name_entry;
    SPreparedDomainEntry prepared_value_entry;
    if (!prepare_property_name(prepared_name, prepared_name_entry) ||
        !prepare_string_value(prepared_value, prepared_value_entry))
    {
        return CNodeKey{};
    }

    SLiveNode new_node{};
    new_node.self = key;
    new_node.payload_bits = prepared_value_entry.id;
    new_node.name = CPropertyNameId{ prepared_name_entry.id };
    new_node.usage.role = ELiveNodeRole::value;
    new_node.usage.value_type = ELiveValueType::string;
    new_node.usage.object_entry = (prepared_name_entry.id != 0u) ? 1u : 0u;

    if (m_nodes.insert(key, new_node) < 0)
    {
        return CNodeKey{};
    }
    if (!commit_property_name(prepared_name, prepared_name_entry) ||
        !commit_string_value(prepared_value, prepared_value_entry))
    {
        (void)m_nodes.erase(key);
        return CNodeKey{};
    }

    return key;
}

CNodeKey CLiveDocument::create_array(const CStringView& name_value) noexcept
{
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_container(ELiveValueType::array, prepared_name) : CNodeKey{};
}

CNodeKey CLiveDocument::create_object(const CStringView& name_value) noexcept
{
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_container(ELiveValueType::object, prepared_name) : CNodeKey{};
}

bool CLiveDocument::check_string_domain(
    const CStableStrings& strings,
    const TPodVector<std::uint32_t>& counts,
    const bool stable_ready) const noexcept
{
    if (!counts.is_ready() || (counts.size() == 0u) ||
        (counts.size() > CPropertyNameId::k_invalid_value))
    {
        return false;
    }
    if (!stable_ready)
    {
        return (counts.size() == 1u) && (strings.memory_allocation_count() == 0u);
    }
    if (!strings.check_integrity())
    {
        return false;
    }
    if (strings.is_valid_id(counts.size()) ||
        ((counts.size() > 1u) && !strings.is_valid_id(counts.size() - 1u)))
    {
        return false;
    }

    for (std::size_t id = 1u; id < counts.size(); ++id)
    {
        const CStringView value = strings.view(id);
        std::size_t normalized_size = 0u;
        if ((value.string() == nullptr) || (value.length() == 0u) ||
            !utf8_string::validate_and_measure(value.string(), value.length(), utf8_string::ELiteralNulPolicy::reject, normalized_size) ||
            (normalized_size != value.length()))
        {
            return false;
        }
    }
    return true;
}

bool CLiveDocument::check_integrity() const noexcept
{
    if (!is_ready() || !m_nodes.check_integrity() ||
        !check_string_domain(m_property_names, m_property_name_counts, m_property_names_ready) ||
        !check_string_domain(m_string_values, m_string_value_counts, m_string_values_ready) ||
        (m_root.query_value() != 1u) || (m_recovered_aggregate_count != 0u))
    {
        return false;
    }

    const SLiveNode* const root_node = value_node(m_root);
    if ((root_node == nullptr) || (root_node->usage.value_type != ELiveValueType::object) ||
        (root_node->name.query_value() != 0u) || (root_node->usage.object_entry != 0u) ||
        root_node->value_parent_aggregate_key().is_valid() ||
        root_node->value_previous_sibling_key().is_valid() ||
        root_node->value_next_sibling_key().is_valid() ||
        (root_node->value_owned_aggregate_key().query_value() != 2u))
    {
        return false;
    }

    std::uint32_t values = 0u;
    std::uint32_t aggregates = 0u;
    std::uint32_t containers = 0u;
    CNodeKey previous_key;

    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const SLiveNode* const current = m_nodes.get_slot(index);
        if ((current == nullptr) || !current->self.is_valid() ||
            (m_nodes.get_slot(current->self) != current) ||
            (previous_key.is_valid() && (previous_key.relationship(current->self) >= 0)) ||
            !current->name.is_valid() || (current->name.query_value() >= m_property_name_counts.size()))
        {
            return false;
        }
        previous_key = current->self;

        if (current->usage.role == ELiveNodeRole::value)
        {
            ++values;
            if ((current->usage.value_type == ELiveValueType::invalid) ||
                (current->usage.aggregate_kind != ELiveAggregateKind::invalid) ||
                (current->usage.object_entry > 1u) ||
                ((current->name.query_value() != 0u) != (current->usage.object_entry != 0u)) ||
                current->value_previous_sibling_key().is_valid() ||
                current->value_next_sibling_key().is_valid() ||
                ((current->self != m_root) && current->value_parent_aggregate_key().is_valid()))
            {
                return false;
            }

            if (is_container_type(current->usage.value_type))
            {
                ++containers;
                const SLiveNode* const aggregate = node(current->value_owned_aggregate_key());
                if ((aggregate == nullptr) ||
                    (aggregate->usage.role != ELiveNodeRole::aggregate) ||
                    (aggregate->aggregate_owner_value_key() != current->self) ||
                    (aggregate->name != current->name) ||
                    (aggregate->usage.aggregate_kind != aggregate_kind_for(current->usage.value_type)) ||
                    (current->payload_bits != 0u) || (current->integer_metadata != CIntegerMetadata{}))
                {
                    return false;
                }
            }
            else if (current->value_owned_aggregate_key().is_valid())
            {
                return false;
            }

            switch (current->usage.value_type)
            {
                case ELiveValueType::null_value:
                {
                    if ((current->payload_bits != 0u) || (current->integer_metadata != CIntegerMetadata{}))
                    {
                        return false;
                    }
                    break;
                }
                case ELiveValueType::boolean:
                {
                    if ((current->payload_bits > 1u) || (current->integer_metadata != CIntegerMetadata{}))
                    {
                        return false;
                    }
                    break;
                }
                case ELiveValueType::integer:
                {
                    if ((current->integer_metadata.domain == EIntegerDomain::signed_value) ?
                        !live_integer_metadata_matches_signed(live_signed_integer_from_bits(current->payload_bits), current->integer_metadata) :
                        !live_integer_metadata_matches_unsigned(current->payload_bits, current->integer_metadata))
                    {
                        return false;
                    }
                    break;
                }
                case ELiveValueType::floating_point:
                {
                    if (!live_floating_point_is_finite(live_floating_point_from_bits(current->payload_bits)) ||
                        (current->integer_metadata != CIntegerMetadata{}))
                    {
                        return false;
                    }
                    break;
                }
                case ELiveValueType::string:
                {
                    if ((current->payload_bits >= m_string_value_counts.size()) ||
                        (current->integer_metadata != CIntegerMetadata{}))
                    {
                        return false;
                    }
                    break;
                }
                case ELiveValueType::array:
                case ELiveValueType::object:
                {
                    break;
                }
                default:
                {
                    return false;
                }
            }
        }
        else if (current->usage.role == ELiveNodeRole::aggregate)
        {
            ++aggregates;
            const SLiveNode* const owner = value_node(current->aggregate_owner_value_key());
            if ((owner == nullptr) || !is_container_type(owner->usage.value_type) ||
                (owner->value_owned_aggregate_key() != current->self) ||
                (owner->name != current->name) ||
                (current->usage.aggregate_kind != aggregate_kind_for(owner->usage.value_type)) ||
                current->aggregate_first_child_key().is_valid() ||
                current->aggregate_last_child_key().is_valid() ||
                current->relation_3.is_valid() || (current->child_count != 0u) ||
                (current->payload_bits != 0u) ||
                (current->usage.value_type != ELiveValueType::invalid) ||
                (current->usage.object_entry != 0u) ||
                (current->integer_metadata != CIntegerMetadata{}))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    if (((m_next_monotonic_node_key != 0u) &&
            (m_next_monotonic_node_key <= previous_key.query_value())) ||
        (m_value_count != 1u) || (m_aggregate_payload_count != 1u) ||
        (aggregates != containers) ||
        (m_nodes.occupied_count() != (values + aggregates)) ||
        (m_property_name_counts[0u] != 2u) || (m_string_value_counts[0u] != 0u))
    {
        return false;
    }
    std::uint32_t referenced_property_names = 0u;
    for (std::size_t id = 1u; id < m_property_name_counts.size(); ++id)
    {
        referenced_property_names += (m_property_name_counts[id] != 0u) ? 1u : 0u;
    }
    std::uint32_t referenced_string_values = 0u;
    for (std::size_t id = 1u; id < m_string_value_counts.size(); ++id)
    {
        referenced_string_values += (m_string_value_counts[id] != 0u) ? 1u : 0u;
    }
    return
        (referenced_property_names == m_referenced_property_name_count) &&
        (referenced_string_values == m_referenced_string_value_count);
}

std::uint32_t CLiveDocument::memory_token_count() const noexcept
{
    return m_nodes.memory_token_count() + m_property_names.memory_token_count() +
        m_string_values.memory_token_count() + m_property_name_counts.memory_token_count() +
        m_string_value_counts.memory_token_count();
}

std::uint32_t CLiveDocument::memory_allocation_count() const noexcept
{
    return m_nodes.memory_allocation_count() + m_property_names.memory_allocation_count() +
        m_string_values.memory_allocation_count() + m_property_name_counts.memory_allocation_count() +
        m_string_value_counts.memory_allocation_count();
}

std::uint64_t CLiveDocument::memory_allocation_size() const noexcept
{
    return m_nodes.memory_allocation_size() + m_property_names.memory_allocation_size() +
        m_string_values.memory_allocation_size() + m_property_name_counts.memory_allocation_size() +
        m_string_value_counts.memory_allocation_size();
}

void CLiveDocument::replace_with(CLiveDocument& source) noexcept
{
    if (this == &source)
    {
        return;
    }

    m_nodes = std::move(source.m_nodes);
    m_property_names = std::move(source.m_property_names);
    m_string_values = std::move(source.m_string_values);
    m_property_name_counts = std::move(source.m_property_name_counts);
    m_string_value_counts = std::move(source.m_string_value_counts);
    m_root = source.m_root;
    m_next_monotonic_node_key = source.m_next_monotonic_node_key;
    m_value_count = source.m_value_count;
    m_aggregate_payload_count = source.m_aggregate_payload_count;
    m_referenced_property_name_count = source.m_referenced_property_name_count;
    m_referenced_string_value_count = source.m_referenced_string_value_count;
    m_recovered_aggregate_count = source.m_recovered_aggregate_count;
    m_property_names_ready = source.m_property_names_ready;
    m_string_values_ready = source.m_string_values_ready;

    source.m_root = CNodeKey{};
    source.m_next_monotonic_node_key = 1u;
    source.m_value_count = 0u;
    source.m_aggregate_payload_count = 0u;
    source.m_referenced_property_name_count = 0u;
    source.m_referenced_string_value_count = 0u;
    source.m_recovered_aggregate_count = 0u;
    source.m_property_names_ready = false;
    source.m_string_values_ready = false;
}
