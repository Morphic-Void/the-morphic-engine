
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_document.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    1 Sep 26
//
//  Mutable live-document construction and foundational validation.

#include "data_model/live_document.hpp"

#include <limits>
#include <utility>

#include "debug/macros.hpp"
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

bool CLiveDocument::initialise(const std::size_t initial_node_capacity) noexcept
{
    if (is_ready() || (memory_allocation_count() != 0u) || (initial_node_capacity > std::numeric_limits<std::uint32_t>::max()))
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

    if (!staged.insert_root_pair())
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
    m_empty_value_count = 0u;
    m_property_names_ready = false;
    m_string_values_ready = false;
    m_integrity_known_bad = false;
}

bool CLiveDocument::is_ready() const noexcept
{
    return
        m_nodes.is_ready() && m_property_name_counts.is_ready() &&
        m_string_value_counts.is_ready() && m_root.is_valid() && !m_integrity_known_bad;
}

bool CLiveDocument::is_canonical() const noexcept
{
    return is_ready() && (m_recovered_aggregate_count == 0u);
}

bool CLiveDocument::is_complete() const noexcept
{
    return is_ready() && (m_empty_value_count == 0u);
}

bool CLiveDocument::check_integrity() const noexcept
{
    if (!is_ready() || !m_nodes.check_integrity() ||
        !check_string_domain(m_property_names, m_property_name_counts, m_property_names_ready) ||
        !check_string_domain(m_string_values, m_string_value_counts, m_string_values_ready) ||
        (m_root.query_value() != 1u))
    {
        return false;
    }

    const CLiveNode* const root_node = value_node(m_root);
    if ((root_node == nullptr) || (root_node->value_type() != ELiveValueType::object) ||
        (root_node->name_id().query_value() != 0u) || root_node->is_object_entry() ||
        !root_node->value_is_unattached() ||
        (root_node->value_owned_aggregate_key().query_value() != 2u))
    {
        return false;
    }

    std::uint64_t values = 0u;
    std::uint64_t aggregates = 0u;
    std::uint64_t containers = 0u;
    CNodeKey previous_key;

    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const CLiveNode* const current = m_nodes.get_slot(index);
        if ((current == nullptr) || !current->key().is_valid() ||
            (m_nodes.get_slot(current->key()) != current) ||
            (previous_key.is_valid() && (previous_key.relationship(current->key()) >= 0)) ||
            !current->name_id().is_valid() ||
            (current->name_id().query_value() >= m_property_name_counts.size()))
        {
            return false;
        }
        previous_key = current->key();

        if (current->is_value_record())
        {
            ++values;
            if (!value_payload_is_in_document_domain(*current))
            {
                return false;
            }

            if (!current->value_attachment_is_consistent())
            {
                return false;
            }
            if (current->value_parent_aggregate_key().is_valid())
            {
                const CLiveNode* const parent_aggregate = node(current->value_parent_aggregate_key());
                if ((parent_aggregate == nullptr) || !aggregate_payload_is_in_document_domain(*parent_aggregate))
                {
                    return false;
                }
            }

            if (live_value_type_is_container(current->value_type()))
            {
                ++containers;
                const CLiveNode* const aggregate = node(current->value_owned_aggregate_key());
                if ((aggregate == nullptr) ||
                    !aggregate_payload_is_in_document_domain(*aggregate) ||
                    !current->forms_container_pair_with(*aggregate))
                {
                    return false;
                }
            }
        }
        else if (current->is_aggregate_record())
        {
            ++aggregates;
            const CLiveNode* const owner = value_node(current->aggregate_owner_value_key());
            if (!aggregate_payload_is_in_document_domain(*current) ||
                (owner == nullptr) || !owner->forms_container_pair_with(*current))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    if (((m_next_monotonic_node_key != 0u) && (m_next_monotonic_node_key <= previous_key.query_value())) ||
        (aggregates != containers) || (m_nodes.occupied_count() != (values + aggregates)))
    {
        return false;
    }

    SSubtreeTotals reachable;
    if (!audit_subtree_checked(m_root, reachable) ||
        (reachable.value_count != m_value_count) ||
        (reachable.aggregate_count != m_aggregate_payload_count) ||
        (reachable.recovered_aggregate_count != m_recovered_aggregate_count) ||
        (reachable.empty_value_count != m_empty_value_count))
    {
        return false;
    }

    std::uint64_t forest_records = reachable.value_count + reachable.aggregate_count;
    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const CLiveNode* const current = m_nodes.get_slot(index);
        if ((current != nullptr) && current->is_value_record() &&
            (current->key() != m_root) && !current->value_parent_aggregate_key().is_valid())
        {
            SSubtreeTotals detached;
            if (!audit_subtree_checked(current->key(), detached))
            {
                return false;
            }
            forest_records += detached.value_count + detached.aggregate_count;
            if (forest_records > m_nodes.occupied_count())
            {
                return false;
            }
        }
    }
    if (forest_records != m_nodes.occupied_count())
    {
        return false;
    }

    std::uint32_t referenced_property_names = 0u;
    for (std::size_t id = 0u; id < m_property_name_counts.size(); ++id)
    {
        std::uint32_t expected = 0u;
        if (!count_subtree_reference_checked(m_root, static_cast<std::uint32_t>(id), false, expected) ||
            (m_property_name_counts[id] != expected))
        {
            return false;
        }
        if ((id != 0u) && (expected != 0u))
        {
            ++referenced_property_names;
        }
    }
    std::uint32_t referenced_string_values = 0u;
    for (std::size_t id = 0u; id < m_string_value_counts.size(); ++id)
    {
        std::uint32_t expected = 0u;
        if (!count_subtree_reference_checked(m_root, static_cast<std::uint32_t>(id), true, expected) ||
            (m_string_value_counts[id] != expected))
        {
            return false;
        }
        if ((id != 0u) && (expected != 0u))
        {
            ++referenced_string_values;
        }
    }
    return
        (referenced_property_names == m_referenced_property_name_count) &&
        (referenced_string_values == m_referenced_string_value_count);
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

bool CLiveDocument::contains(const CNodeKey value) const noexcept
{
    return value_node(value) != nullptr;
}

ELiveValueType CLiveDocument::value_type(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->value_type() : ELiveValueType::invalid;
}

bool CLiveDocument::is_object_entry(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) && found->is_object_entry();
}

bool CLiveDocument::is_detached(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) && (value != m_root) && found->value_is_unattached();
}

CPropertyNameId CLiveDocument::name_id(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->name_id() : CPropertyNameId{};
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
    const CLiveNode* const found = value_node(value);
    if ((found == nullptr) || !found->value_parent_aggregate_key().is_valid())
    {
        return CNodeKey{};
    }
    const CLiveNode* const aggregate = node(found->value_parent_aggregate_key());
    return (aggregate != nullptr) ? aggregate->aggregate_owner_value_key() : CNodeKey{};
}

CNodeKey CLiveDocument::previous_sibling(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->value_previous_sibling_key() : CNodeKey{};
}

CNodeKey CLiveDocument::next_sibling(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) ? found->value_next_sibling_key() : CNodeKey{};
}

std::uint32_t CLiveDocument::child_count(const CNodeKey container_value) const noexcept
{
    const CLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->child_count() : 0u;
}

CNodeKey CLiveDocument::first_child(const CNodeKey container_value) const noexcept
{
    const CLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->aggregate_first_child_key() : CNodeKey{};
}

CNodeKey CLiveDocument::last_child(const CNodeKey container_value) const noexcept
{
    const CLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->aggregate_last_child_key() : CNodeKey{};
}

bool CLiveDocument::boolean_value(const CNodeKey key, bool& value) const noexcept
{
    const CLiveNode* const found = value_node(key);
    if ((found == nullptr) || (found->value_type() != ELiveValueType::boolean) || (found->payload_bits() > 1u))
    {
        return false;
    }
    value = found->payload_bits() != 0u;
    return true;
}

bool CLiveDocument::signed_integer_value(const CNodeKey key, std::int64_t& value) const noexcept
{
    const CLiveNode* const found = value_node(key);
    if ((found == nullptr) ||
        (found->value_type() != ELiveValueType::integer) ||
        (found->integer_metadata().domain != EIntegerDomain::signed_value))
    {
        return false;
    }
    value = live_signed_integer_from_bits(found->payload_bits());
    return true;
}

bool CLiveDocument::unsigned_integer_value(const CNodeKey key, std::uint64_t& value) const noexcept
{
    const CLiveNode* const found = value_node(key);
    if ((found == nullptr) ||
        (found->value_type() != ELiveValueType::integer) ||
        (found->integer_metadata().domain != EIntegerDomain::unsigned_value))
    {
        return false;
    }
    value = found->payload_bits();
    return true;
}

bool CLiveDocument::integer_metadata(const CNodeKey key, CIntegerMetadata& metadata) const noexcept
{
    const CLiveNode* const found = value_node(key);
    if ((found == nullptr) || (found->value_type() != ELiveValueType::integer))
    {
        return false;
    }
    metadata = found->integer_metadata();
    return true;
}

bool CLiveDocument::floating_point_value(const CNodeKey key, double& value) const noexcept
{
    const CLiveNode* const found = value_node(key);
    if ((found == nullptr) || (found->value_type() != ELiveValueType::floating_point))
    {
        return false;
    }
    value = live_floating_point_from_bits(found->payload_bits());
    return true;
}

CStringValueId CLiveDocument::string_value_id(const CNodeKey key) const noexcept
{
    const CLiveNode* const found = value_node(key);
    return ((found != nullptr) && (found->value_type() == ELiveValueType::string) && (found->payload_bits() < CStringValueId::k_invalid_value)) ?
        CStringValueId{ static_cast<std::uint32_t>(found->payload_bits()) } : CStringValueId{};
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

CNodeKey CLiveDocument::create_empty(const CStringView& name_value) noexcept
{
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_scalar(ELiveValueType::empty, 0u, CIntegerMetadata{}, prepared_name) : CNodeKey{};
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
        create_scalar(ELiveValueType::floating_point, live_floating_point_bits(value), CIntegerMetadata{}, prepared_name) : CNodeKey{};
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

    CPropertyNameId name_id;
    CStringValueId value_id;
    if (!intern_property_name(prepared_name, name_id) ||
        !intern_string_value(prepared_value, value_id))
    {
        return CNodeKey{};
    }

    CLiveNode new_node{};
    new_node.initialise_value(key, ELiveValueType::string, value_id.query_value(), name_id, CIntegerMetadata{});

    if (m_nodes.insert(key, new_node) < 0)
    {
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

CLiveAttachmentResult CLiveDocument::append_child(
    const CNodeKey destination,
    const CNodeKey candidate,
    CNodeKey& surviving_value) noexcept
{
    SAttachmentPosition position;
    const CLiveNode* const aggregate = aggregate_for_value(destination);
    if (aggregate != nullptr)
    {
        position.previous = aggregate->aggregate_last_child_key();
    }
    return attach_child(destination, candidate, position, surviving_value);
}

CLiveAttachmentResult CLiveDocument::insert_child_before(
    const CNodeKey destination,
    const CNodeKey candidate,
    const CNodeKey before,
    CNodeKey& surviving_value) noexcept
{
    if (!before.is_valid())
    {
        surviving_value = CNodeKey{};
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    SAttachmentPosition position;
    position.next = before;
    const CLiveNode* const before_node = value_node(before);
    if (before_node != nullptr)
    {
        position.previous = before_node->value_previous_sibling_key();
    }
    return attach_child(destination, candidate, position, surviving_value);
}

CLiveAttachmentResult CLiveDocument::insert_child_at(
    const CNodeKey destination,
    const CNodeKey candidate,
    const std::uint32_t index,
    CNodeKey& surviving_value) noexcept
{
    surviving_value = CNodeKey{};
    const CLiveNode* const destination_node = value_node(destination);
    if (destination_node == nullptr)
    {
        return is_ready() ?
            attachment_rejection(ELiveAttachmentRejection::destination_not_found) :
            attachment_rejection(ELiveAttachmentRejection::document_not_ready);
    }
    const CLiveNode* const aggregate = aggregate_for_value(destination);
    if (aggregate == nullptr)
    {
        if (live_value_type_is_container(destination_node->value_type()))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Container aggregate is missing.");
        }
        return live_value_type_is_container(destination_node->value_type()) ?
            attachment_rejection(ELiveAttachmentRejection::corrupt_structure) :
            attachment_rejection(ELiveAttachmentRejection::destination_not_container);
    }
    if (index > aggregate->child_count())
    {
        return attachment_rejection(ELiveAttachmentRejection::index_out_of_range);
    }
    if (index == aggregate->child_count())
    {
        return append_child(destination, candidate, surviving_value);
    }

    CNodeKey before = aggregate->aggregate_first_child_key();
    for (std::uint32_t ordinal = 0u; ordinal < index; ++ordinal)
    {
        const CLiveNode* const before_node = value_node(before);
        if (before_node == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Indexed child traversal encountered an invalid value.");
            return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
        }
        before = before_node->value_next_sibling_key();
    }
    if (!before.is_valid())
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Indexed child traversal ended early.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    return insert_child_before(destination, candidate, before, surviving_value);
}

bool CLiveDocument::detach(const CNodeKey value) noexcept
{
    if (!is_ready() || (value == m_root))
    {
        return false;
    }
    bool was_reachable = false;
    return detach_value(value, was_reachable);
}

CNodeKey CLiveDocument::detach_payload(const CNodeKey source) noexcept
{
    if (!is_ready() || (source == m_root))
    {
        return CNodeKey{};
    }

    const CLiveNode* source_node = value_node(source);
    if ((source_node == nullptr) || (source_node->value_type() == ELiveValueType::empty))
    {
        return CNodeKey{};
    }
    const CPropertyNameId former_name = source_node->name_id();
    const CNodeKey aggregate_key = source_node->value_owned_aggregate_key();
    const bool aggregate_is_expected = live_value_type_is_container(source_node->value_type());
    const CLiveNode* const aggregate_before_allocation = aggregate_key.is_valid() ? node(aggregate_key) : nullptr;
    if ((aggregate_key.is_valid() != aggregate_is_expected) ||
        (aggregate_is_expected && ((aggregate_before_allocation == nullptr) || !source_node->forms_container_pair_with(*aggregate_before_allocation))))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload source has an invalid aggregate pair.");
        return CNodeKey{};
    }

    bool source_reachable = false;
    bool ignored = false;
    if (!query_ancestry(source, CNodeKey{}, source_reachable, ignored))
    {
        return CNodeKey{};
    }

    const CNodeKey empty_key = create_empty_node(former_name);
    if (!empty_key.is_valid())
    {
        return CNodeKey{};
    }

    SSubtreeTotals removed, added;
    if (source_reachable &&
        (!apply_subtree_reachability(source, EReferenceAdjustment::remove, removed) ||
            !apply_subtree_reachability(empty_key, EReferenceAdjustment::add, added)))
    {
        (void)m_nodes.erase(empty_key);
        return CNodeKey{};
    }
    if (!substitute_value_position(source, empty_key))
    {
        return CNodeKey{};
    }
    if (source_reachable)
    {
        if (!commit_reachable_totals(removed, added))
        {
            return CNodeKey{};
        }
    }
    value_node(source)->set_name_id(CPropertyNameId{ CPropertyNameId::k_empty_value });
    CLiveNode* const aggregate = aggregate_key.is_valid() ? node(aggregate_key) : nullptr;
    if (aggregate_key.is_valid() && (aggregate == nullptr))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload aggregate disappeared during detachment.");
        return CNodeKey{};
    }
    if (aggregate != nullptr)
    {
        aggregate->set_name_id(CPropertyNameId{ CPropertyNameId::k_empty_value });
    }
    return empty_key;
}

CNodeKey CLiveDocument::attach_payload(const CNodeKey empty_target, const CNodeKey detached_payload) noexcept
{
    if (!is_ready())
    {
        return CNodeKey{};
    }
    CLiveNode* target = value_node(empty_target);
    CLiveNode* payload = value_node(detached_payload);
    if ((target == nullptr) || (target->value_type() != ELiveValueType::empty) || (empty_target == detached_payload))
    {
        return CNodeKey{};
    }
    if ((payload == nullptr) ||
        (payload->value_type() == ELiveValueType::empty) ||
        !payload->value_is_unattached() ||
        (payload->name_id().query_value() != CPropertyNameId::k_empty_value))
    {
        return CNodeKey{};
    }
    CLiveNode* const payload_aggregate = live_value_type_is_container(payload->value_type()) ?
        node(payload->value_owned_aggregate_key()) : nullptr;
    if (live_value_type_is_container(payload->value_type()) &&
        ((payload_aggregate == nullptr) || !payload->forms_container_pair_with(*payload_aggregate)))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload has an invalid aggregate pair.");
        return CNodeKey{};
    }

    bool target_reachable = false;
    bool cycle = false;
    if (!query_ancestry(empty_target, detached_payload, target_reachable, cycle))
    {
        return CNodeKey{};
    }
    if (cycle)
    {
        return CNodeKey{};
    }

    const CPropertyNameId target_name = target->name_id();
    if (!substitute_value_position(empty_target, detached_payload))
    {
        return CNodeKey{};
    }
    payload->set_name_id(target_name);
    if (payload_aggregate != nullptr)
    {
        payload_aggregate->set_name_id(target_name);
    }
    if (target_reachable)
    {
        SSubtreeTotals removed, added;
        if (!apply_subtree_reachability(empty_target, EReferenceAdjustment::remove, removed) ||
            !apply_subtree_reachability(detached_payload, EReferenceAdjustment::add, added))
        {
            return CNodeKey{};
        }
        if (!commit_reachable_totals(removed, added))
        {
            return CNodeKey{};
        }
    }
    if (!erase_subtree(empty_target))
    {
        return CNodeKey{};
    }
    return detached_payload;
}

bool CLiveDocument::erase(const CNodeKey value) noexcept
{
    if (!is_ready())
    {
        return false;
    }
    if (value == m_root)
    {
        CLiveNode* const root_value = value_node(m_root);
        CLiveNode* const root_aggregate = (root_value != nullptr) ? node(root_value->value_owned_aggregate_key()) : nullptr;
        if ((root_value == nullptr) || (root_aggregate == nullptr))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Root container pair is invalid.");
            return false;
        }

        CNodeKey child = root_aggregate->aggregate_first_child_key();
        while (child.is_valid())
        {
            const CLiveNode* const child_value = value_node(child);
            if (child_value == nullptr)
            {
                mark_integrity_bad();
                MV_ASSERT_MSG(false, "Root child traversal encountered an invalid value.");
                return false;
            }
            const CNodeKey next = child_value->value_next_sibling_key();
            SSubtreeTotals totals;
            if (!apply_subtree_reachability(child, EReferenceAdjustment::remove, totals))
            {
                return false;
            }
            if (!commit_reachable_totals(totals, SSubtreeTotals{}))
            {
                return false;
            }
            if (!erase_subtree(child))
            {
                return false;
            }
            child = next;
        }
        root_aggregate->clear_aggregate_children();
        return true;
    }

    CLiveNode* const value_record = value_node(value);
    if (value_record == nullptr)
    {
        return false;
    }
    if (value_record->value_parent_aggregate_key().is_valid())
    {
        bool was_reachable = false;
        if (!detach_value(value, was_reachable))
        {
            return false;
        }
    }
    else if (!value_record->value_is_unattached())
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Unattached value has inconsistent topology.");
        return false;
    }
    if (!erase_subtree(value))
    {
        return false;
    }
    return true;
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

bool CLiveDocument::prepare_string(const CStringView& source, SPreparedString& prepared) const noexcept
{
    prepared.bytes = source.string();
    prepared.size = source.length();
    prepared.storage.deallocate();

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

    if (normalized_size != prepared.size)
    {
        if (!prepared.storage.resize(normalized_size))
        {
            return false;
        }
        if (!utf8_string::normalize_literal_nuls(
            prepared.bytes,
            prepared.size,
            prepared.storage.data(),
            normalized_size))
        {
            return false;
        }
        prepared.bytes = prepared.storage.data();
        prepared.size = normalized_size;
    }
    else if (
        m_property_names.storage_overlaps(prepared.bytes, prepared.size) ||
        m_string_values.storage_overlaps(prepared.bytes, prepared.size))
    {
        if (!prepared.storage.resize(prepared.size))
        {
            return false;
        }
        for (std::size_t index = 0u; index < prepared.size; ++index)
        {
            prepared.storage.data()[index] = prepared.bytes[index];
        }
        prepared.bytes = prepared.storage.data();
    }
    return true;
}

bool CLiveDocument::intern_string_domain(
    const SPreparedString& value,
    CStableStrings& strings,
    TPodVector<std::uint32_t>& reference_counts,
    bool& strings_ready,
    std::uint32_t& id) noexcept
{
    id = 0u;
    if (value.size == 0u)
    {
        return true;
    }

    const std::size_t found = strings.find_id(value.bytes, value.size);
    if (found != CStableStrings::k_invalid_id)
    {
        if (found >= CPropertyNameId::k_invalid_value)
        {
            return false;
        }
        id = static_cast<std::uint32_t>(found);
        return true;
    }

    const std::size_t next_id = reference_counts.size();
    if (next_id >= CPropertyNameId::k_invalid_value)
    {
        return false;
    }

    if (!reference_counts.ensure_free(1u))
    {
        return false;
    }
    if (!strings_ready)
    {
        const std::size_t storage_size = value.size + 2u;
        if ((storage_size < value.size) || !strings.initialise(4u, storage_size))
        {
            return false;
        }
        strings_ready = true;
    }
    else if (!strings.ensure_free(value.size))
    {
        return false;
    }

    const std::size_t appended = strings.append(value.bytes, value.size);
    if ((appended != next_id) || !reference_counts.push_back(0u))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "String intern commit failed.");
        return false;
    }
    id = static_cast<std::uint32_t>(next_id);
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
    m_next_monotonic_node_key = (m_next_monotonic_node_key == std::numeric_limits<std::uint64_t>::max()) ?
        0u : (m_next_monotonic_node_key + 1u);
    return true;
}

bool CLiveDocument::intern_property_name(const SPreparedString& value, CPropertyNameId& id) noexcept
{
    std::uint32_t raw_id = 0u;
    if (!intern_string_domain(
        value,
        m_property_names,
        m_property_name_counts,
        m_property_names_ready,
        raw_id))
    {
        return false;
    }
    id = CPropertyNameId{ raw_id };
    return true;
}

bool CLiveDocument::intern_string_value(const SPreparedString& value, CStringValueId& id) noexcept
{
    std::uint32_t raw_id = 0u;
    if (!intern_string_domain(
        value,
        m_string_values,
        m_string_value_counts,
        m_string_values_ready,
        raw_id))
    {
        return false;
    }
    id = CStringValueId{ raw_id };
    return true;
}

CNodeKey CLiveDocument::create_scalar(
    const ELiveValueType type,
    const std::uint64_t payload_bits,
    const CIntegerMetadata metadata,
    const SPreparedString& prepared_name) noexcept
{
    if (!is_ready() || live_value_type_is_container(type) ||
        (type == ELiveValueType::invalid))
    {
        return CNodeKey{};
    }

    CNodeKey key;
    if (!allocate_key(key))
    {
        return CNodeKey{};
    }

    CPropertyNameId name_id;
    if (!intern_property_name(prepared_name, name_id))
    {
        return CNodeKey{};
    }

    CLiveNode new_node{};
    new_node.initialise_value(key, type, payload_bits, name_id, metadata);

    if (m_nodes.insert(key, new_node) < 0)
    {
        return CNodeKey{};
    }

    return key;
}

CNodeKey CLiveDocument::create_empty_node(const CPropertyNameId name) noexcept
{
    CNodeKey key;
    if (!allocate_key(key))
    {
        return CNodeKey{};
    }

    CLiveNode value{};
    value.initialise_value(key, ELiveValueType::empty, 0u, name, CIntegerMetadata{});
    return (m_nodes.insert(key, value) >= 0) ? key : CNodeKey{};
}

CNodeKey CLiveDocument::create_container(const ELiveValueType type, const SPreparedString& prepared_name) noexcept
{
    if (!is_ready() || !live_value_type_is_container(type))
    {
        return CNodeKey{};
    }

    CNodeKey value_key;
    CNodeKey aggregate_key;
    if (!allocate_key(value_key) || !allocate_key(aggregate_key))
    {
        return CNodeKey{};
    }

    CPropertyNameId name_id;
    if (!intern_property_name(prepared_name, name_id))
    {
        return CNodeKey{};
    }

    CLiveNode value{};
    value.initialise_value(value_key, type, 0u, name_id, CIntegerMetadata{});
    value.set_value_owned_aggregate_key(aggregate_key);

    CLiveNode aggregate{};
    aggregate.initialise_aggregate(aggregate_key, value_key, live_aggregate_kind_for_value_type(type), name_id);

    if (m_nodes.insert(value_key, value) < 0)
    {
        return CNodeKey{};
    }
    if (m_nodes.insert(aggregate_key, aggregate) < 0)
    {
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

    CLiveNode root_node{};
    root_node.initialise_value(root_key, ELiveValueType::object, 0u, CPropertyNameId{ CPropertyNameId::k_empty_value }, CIntegerMetadata{});
    root_node.set_value_owned_aggregate_key(aggregate_key);

    CLiveNode aggregate{};
    aggregate.initialise_aggregate(aggregate_key, root_key, ELiveAggregateKind::object, CPropertyNameId{ CPropertyNameId::k_empty_value });

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

CLiveNode* CLiveDocument::node(const CNodeKey key) noexcept
{
    CLiveNode* const found = key.is_valid() ? m_nodes.get_slot(key) : nullptr;
    return ((found != nullptr) && (found->key() == key)) ? found : nullptr;
}

const CLiveNode* CLiveDocument::node(const CNodeKey key) const noexcept
{
    const CLiveNode* const found = key.is_valid() ? m_nodes.get_slot(key) : nullptr;
    return ((found != nullptr) && (found->key() == key)) ? found : nullptr;
}

CLiveNode* CLiveDocument::value_node(const CNodeKey key) noexcept
{
    CLiveNode* const found = node(key);
    return ((found != nullptr) && found->is_value_record()) ? found : nullptr;
}

const CLiveNode* CLiveDocument::value_node(const CNodeKey key) const noexcept
{
    const CLiveNode* const found = node(key);
    return ((found != nullptr) && found->is_value_record()) ? found : nullptr;
}

const CLiveNode* CLiveDocument::aggregate_for_value(const CNodeKey value) const noexcept
{
    const CLiveNode* const owner = value_node(value);
    if ((owner == nullptr) || !live_value_type_is_container(owner->value_type()))
    {
        return nullptr;
    }
    const CLiveNode* const aggregate = node(owner->value_owned_aggregate_key());
    return ((aggregate != nullptr) && aggregate->is_aggregate_record()) ? aggregate : nullptr;
}

CLiveAttachmentResult CLiveDocument::attachment_rejection(const ELiveAttachmentRejection rejection) noexcept
{
    return CLiveAttachmentResult{ ELiveAttachmentOutcome::rejected, rejection };
}

void CLiveDocument::mark_integrity_bad() noexcept
{
    m_integrity_known_bad = true;
}

bool CLiveDocument::value_payload_is_in_document_domain(const CLiveNode& value) const noexcept
{
    return value.value_payload_is_valid() &&
        (value.name_id().query_value() < m_property_name_counts.size()) &&
        ((value.value_type() != ELiveValueType::string) || (value.payload_bits() < m_string_value_counts.size()));
}

bool CLiveDocument::aggregate_payload_is_in_document_domain(const CLiveNode& aggregate) const noexcept
{
    return aggregate.aggregate_payload_is_valid() &&
        (aggregate.name_id().query_value() < m_property_name_counts.size());
}

CNodeKey CLiveDocument::subtree_next(const CNodeKey subtree_root, CNodeKey current) noexcept
{
    const CLiveNode* value = value_node(current);
    if (value == nullptr)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Subtree traversal encountered an invalid value.");
        return CNodeKey{};
    }

    if (live_value_type_is_container(value->value_type()))
    {
        const CLiveNode* const aggregate = node(value->value_owned_aggregate_key());
        if (aggregate == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree traversal encountered an invalid aggregate.");
            return CNodeKey{};
        }
        if (aggregate->aggregate_first_child_key().is_valid())
        {
            return aggregate->aggregate_first_child_key();
        }
    }

    for (std::uint32_t ascent = 0u; ascent <= m_nodes.occupied_count(); ++ascent)
    {
        if (current == subtree_root)
        {
            return CNodeKey{};
        }
        value = value_node(current);
        if (value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree traversal encountered an invalid value.");
            return CNodeKey{};
        }
        if (value->value_next_sibling_key().is_valid())
        {
            return value->value_next_sibling_key();
        }
        const CLiveNode* const parent = node(value->value_parent_aggregate_key());
        if (parent == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree traversal encountered an invalid parent aggregate.");
            return CNodeKey{};
        }
        current = parent->aggregate_owner_value_key();
    }
    mark_integrity_bad();
    MV_ASSERT_MSG(false, "Subtree traversal did not terminate.");
    return CNodeKey{};
}

CNodeKey CLiveDocument::subtree_first_postorder(const CNodeKey subtree_root) noexcept
{
    CNodeKey first = subtree_root;
    for (std::uint32_t descent = 0u; descent <= m_nodes.occupied_count(); ++descent)
    {
        const CLiveNode* const value = value_node(first);
        if (value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid value.");
            return CNodeKey{};
        }
        if (!live_value_type_is_container(value->value_type()))
        {
            return first;
        }
        const CLiveNode* const aggregate = node(value->value_owned_aggregate_key());
        if (aggregate == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid aggregate.");
            return CNodeKey{};
        }
        if (!aggregate->aggregate_first_child_key().is_valid())
        {
            return first;
        }
        first = aggregate->aggregate_first_child_key();
    }
    mark_integrity_bad();
    MV_ASSERT_MSG(false, "Postorder traversal did not terminate.");
    return CNodeKey{};
}

CNodeKey CLiveDocument::subtree_next_postorder(const CNodeKey subtree_root, const CNodeKey current) noexcept
{
    if (current == subtree_root)
    {
        return CNodeKey{};
    }
    const CLiveNode* const value = value_node(current);
    if (value == nullptr)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid value.");
        return CNodeKey{};
    }
    if (value->value_next_sibling_key().is_valid())
    {
        return subtree_first_postorder(value->value_next_sibling_key());
    }
    const CLiveNode* const parent = node(value->value_parent_aggregate_key());
    if (parent == nullptr)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid parent aggregate.");
        return CNodeKey{};
    }
    return parent->aggregate_owner_value_key();
}

bool CLiveDocument::audit_subtree_checked(const CNodeKey subtree_root, SSubtreeTotals& totals) const noexcept
{
    totals = SSubtreeTotals{};

    if (value_node(subtree_root) == nullptr)
    {
        return false;
    }

    CNodeKey current = subtree_root;
    while (current.is_valid())
    {
        const CLiveNode* const value = value_node(current);
        if ((value == nullptr) || (value->key() != current) || !value_payload_is_in_document_domain(*value))
        {
            return false;
        }
        ++totals.value_count;
        if (value->value_type() == ELiveValueType::empty)
        {
            ++totals.empty_value_count;
        }
        if ((totals.value_count + totals.aggregate_count) > m_nodes.occupied_count())
        {
            return false;
        }

        const CLiveNode* aggregate = nullptr;
        if (live_value_type_is_container(value->value_type()))
        {
            aggregate = node(value->value_owned_aggregate_key());
            if ((aggregate == nullptr) ||
                !aggregate_payload_is_in_document_domain(*aggregate) ||
                !value->forms_container_pair_with(*aggregate))
            {
                return false;
            }
            ++totals.aggregate_count;
            if (aggregate->aggregate_kind() == ELiveAggregateKind::recovered_array)
            {
                ++totals.recovered_aggregate_count;
            }
            if ((totals.value_count + totals.aggregate_count) > m_nodes.occupied_count())
            {
                return false;
            }

            CNodeKey previous;
            CNodeKey child = aggregate->aggregate_first_child_key();
            std::uint64_t children = 0u;
            while (child.is_valid())
            {
                const CLiveNode* const child_node = value_node(child);
                if (child_node == nullptr)
                {
                    return false;
                }
                if (previous.is_valid())
                {
                    const CLiveNode* const previous_node = value_node(previous);
                    if ((previous_node == nullptr) || !previous_node->value_is_previous_sibling_of(*child_node))
                    {
                        return false;
                    }
                }
                else if (!aggregate->aggregate_has_first_child(*child_node))
                {
                    return false;
                }
                if (!aggregate->aggregate_accepts_child(*child_node))
                {
                    return false;
                }
                if (aggregate->aggregate_kind() == ELiveAggregateKind::object)
                {
                    std::uint64_t earlier_count = 0u;
                    for (CNodeKey earlier = aggregate->aggregate_first_child_key(); earlier.is_valid() && (earlier != child); earlier = next_sibling(earlier))
                    {
                        const CLiveNode* const earlier_node = value_node(earlier);
                        ++earlier_count;
                        if ((earlier_count > m_nodes.occupied_count()) ||
                            (earlier_node == nullptr) ||
                            (earlier_node->name_id() == child_node->name_id()))
                        {
                            return false;
                        }
                    }
                }
                previous = child;
                child = child_node->value_next_sibling_key();
                ++children;
                if (children > m_nodes.occupied_count())
                {
                    return false;
                }
            }
            const CLiveNode* const last_child = value_node(previous);
            if ((children != aggregate->child_count()) ||
                ((children != 0u) && ((last_child == nullptr) || !aggregate->aggregate_has_last_child(*last_child))) ||
                ((aggregate->aggregate_kind() == ELiveAggregateKind::recovered_array) && (children < 2u)))
            {
                return false;
            }
        }

        if ((aggregate != nullptr) && aggregate->aggregate_first_child_key().is_valid())
        {
            current = aggregate->aggregate_first_child_key();
            continue;
        }

        while (current != subtree_root)
        {
            const CLiveNode* const completed = value_node(current);
            if (completed == nullptr)
            {
                return false;
            }
            if (completed->value_next_sibling_key().is_valid())
            {
                current = completed->value_next_sibling_key();
                break;
            }
            const CLiveNode* const parent_aggregate = node(completed->value_parent_aggregate_key());
            if ((parent_aggregate == nullptr) || !aggregate_payload_is_in_document_domain(*parent_aggregate))
            {
                return false;
            }
            current = parent_aggregate->aggregate_owner_value_key();
        }
        if (current == subtree_root)
        {
            current = CNodeKey{};
        }
    }

    return true;
}

bool CLiveDocument::apply_subtree_reachability(const CNodeKey subtree_root, const EReferenceAdjustment adjustment, SSubtreeTotals& totals) noexcept
{
    totals = SSubtreeTotals{};
    CNodeKey current = subtree_root;
    std::uint32_t visited_values = 0u;
    while (current.is_valid())
    {
        if (visited_values >= m_nodes.occupied_count())
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Reachability traversal did not terminate.");
            return false;
        }
        ++visited_values;
        const CLiveNode* const value = value_node(current);
        if (value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Reachability traversal encountered an invalid value.");
            return false;
        }
        if (!adjust_property_name_reference(value->name_id(), adjustment))
        {
            return false;
        }
        ++totals.value_count;
        if (value->value_type() == ELiveValueType::empty)
        {
            ++totals.empty_value_count;
        }
        if ((value->value_type() == ELiveValueType::string) &&
            !adjust_string_value_reference(CStringValueId{ static_cast<std::uint32_t>(value->payload_bits()) }, adjustment))
        {
            return false;
        }
        if (live_value_type_is_container(value->value_type()))
        {
            const CLiveNode* const aggregate = node(value->value_owned_aggregate_key());
            if (aggregate == nullptr)
            {
                mark_integrity_bad();
                MV_ASSERT_MSG(false, "Reachability traversal encountered an invalid aggregate.");
                return false;
            }
            if (!adjust_property_name_reference(aggregate->name_id(), adjustment))
            {
                return false;
            }
            ++totals.aggregate_count;
            if (aggregate->aggregate_kind() == ELiveAggregateKind::recovered_array)
            {
                ++totals.recovered_aggregate_count;
            }
        }
        current = subtree_next(subtree_root, current);
        if (m_integrity_known_bad)
        {
            return false;
        }
    }
    return totals.value_count != 0u;
}

bool CLiveDocument::commit_reachable_totals(const SSubtreeTotals& removed, const SSubtreeTotals& added) noexcept
{
    if ((m_value_count < removed.value_count) ||
        (m_aggregate_payload_count < removed.aggregate_count) ||
        (m_recovered_aggregate_count < removed.recovered_aggregate_count) ||
        (m_empty_value_count < removed.empty_value_count))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Reachable document totals would underflow.");
        return false;
    }

    m_value_count = m_value_count - removed.value_count + added.value_count;
    m_aggregate_payload_count = m_aggregate_payload_count - removed.aggregate_count + added.aggregate_count;
    m_recovered_aggregate_count = m_recovered_aggregate_count - removed.recovered_aggregate_count + added.recovered_aggregate_count;
    m_empty_value_count = m_empty_value_count - removed.empty_value_count + added.empty_value_count;
    return true;
}

bool CLiveDocument::adjust_property_name_reference(const CPropertyNameId id, const EReferenceAdjustment adjustment) noexcept
{
    if (!id.is_valid() || (id.query_value() >= m_property_name_counts.size()))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Property-name reference identifier is invalid.");
        return false;
    }
    std::uint32_t& count = m_property_name_counts[id.query_value()];
    if (adjustment == EReferenceAdjustment::add)
    {
        if ((id.query_value() != 0u) && (count == 0u))
        {
            ++m_referenced_property_name_count;
        }
        ++count;
        return true;
    }
    if (count == 0u)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Property-name reference count underflowed.");
        return false;
    }
    --count;
    if ((id.query_value() != 0u) && (count == 0u))
    {
        if (m_referenced_property_name_count == 0u)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Referenced property-name total underflowed.");
            return false;
        }
        --m_referenced_property_name_count;
    }
    return true;
}

bool CLiveDocument::adjust_string_value_reference(const CStringValueId id, const EReferenceAdjustment adjustment) noexcept
{
    if (!id.is_valid() || (id.query_value() >= m_string_value_counts.size()))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "String-value reference identifier is invalid.");
        return false;
    }
    std::uint32_t& count = m_string_value_counts[id.query_value()];
    if (adjustment == EReferenceAdjustment::add)
    {
        if ((id.query_value() != 0u) && (count == 0u))
        {
            ++m_referenced_string_value_count;
        }
        ++count;
        return true;
    }
    if (count == 0u)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "String-value reference count underflowed.");
        return false;
    }
    --count;
    if ((id.query_value() != 0u) && (count == 0u))
    {
        if (m_referenced_string_value_count == 0u)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Referenced string-value total underflowed.");
            return false;
        }
        --m_referenced_string_value_count;
    }
    return true;
}

bool CLiveDocument::count_subtree_reference_checked(
    const CNodeKey subtree_root,
    const std::uint32_t id,
    const bool string_domain,
    std::uint32_t& count) const noexcept
{
    count = 0u;
    CNodeKey current = subtree_root;
    std::uint64_t visited = 0u;
    while (current.is_valid())
    {
        const CLiveNode* const value = value_node(current);
        if (value == nullptr)
        {
            return false;
        }
        ++visited;
        if (visited > m_nodes.occupied_count())
        {
            return false;
        }
        if (string_domain)
        {
            if ((value->value_type() == ELiveValueType::string) && (value->payload_bits() == id))
            {
                ++count;
            }
        }
        else
        {
            if (value->name_id().query_value() == id)
            {
                ++count;
            }
        }

        const CLiveNode* aggregate = nullptr;
        if (live_value_type_is_container(value->value_type()))
        {
            aggregate = node(value->value_owned_aggregate_key());
            if (aggregate == nullptr)
            {
                return false;
            }
            if (!string_domain && (aggregate->name_id().query_value() == id))
            {
                ++count;
            }
        }
        if ((aggregate != nullptr) && aggregate->aggregate_first_child_key().is_valid())
        {
            current = aggregate->aggregate_first_child_key();
            continue;
        }
        while (current != subtree_root)
        {
            const CLiveNode* const completed = value_node(current);
            if (completed == nullptr)
            {
                return false;
            }
            if (completed->value_next_sibling_key().is_valid())
            {
                current = completed->value_next_sibling_key();
                break;
            }
            const CLiveNode* const parent_aggregate = node(completed->value_parent_aggregate_key());
            if (parent_aggregate == nullptr)
            {
                return false;
            }
            current = parent_aggregate->aggregate_owner_value_key();
        }
        if (current == subtree_root)
        {
            current = CNodeKey{};
        }
    }
    return true;
}

bool CLiveDocument::query_ancestry(const CNodeKey value, const CNodeKey sought, bool& reachable, bool& found) noexcept
{
    reachable = false;
    found = false;
    CNodeKey current = value;
    for (std::uint32_t hops = 0u; hops <= m_nodes.occupied_count(); ++hops)
    {
        const CLiveNode* const current_value = value_node(current);
        if (current_value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Ancestry traversal encountered an invalid value.");
            return false;
        }
        if (current == sought)
        {
            found = true;
        }
        if (current == m_root)
        {
            reachable = true;
            return true;
        }
        const CNodeKey parent_key = current_value->value_parent_aggregate_key();
        if (!parent_key.is_valid())
        {
            return current_value->value_is_unattached();
        }
        const CLiveNode* const aggregate = node(parent_key);
        if (aggregate == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Ancestry traversal encountered an invalid aggregate.");
            return false;
        }
        current = aggregate->aggregate_owner_value_key();
    }
    mark_integrity_bad();
    MV_ASSERT_MSG(false, "Ancestry traversal did not terminate.");
    return false;
}

CLiveAttachmentResult CLiveDocument::attach_child(
    const CNodeKey destination,
    const CNodeKey candidate,
    const SAttachmentPosition& position,
    CNodeKey& surviving_value) noexcept
{
    surviving_value = CNodeKey{};
    if (!is_ready())
    {
        return attachment_rejection(ELiveAttachmentRejection::document_not_ready);
    }
    CLiveNode* const destination_node = value_node(destination);
    if (destination_node == nullptr)
    {
        return attachment_rejection(ELiveAttachmentRejection::destination_not_found);
    }
    CLiveNode* const candidate_node = value_node(candidate);
    if (candidate_node == nullptr)
    {
        return attachment_rejection(ELiveAttachmentRejection::candidate_not_found);
    }
    if (!live_value_type_is_container(destination_node->value_type()))
    {
        return attachment_rejection(ELiveAttachmentRejection::destination_not_container);
    }
    if (candidate == m_root)
    {
        return attachment_rejection(ELiveAttachmentRejection::candidate_is_root);
    }
    if (!candidate_node->value_is_unattached())
    {
        return attachment_rejection(ELiveAttachmentRejection::candidate_not_detached);
    }

    CLiveNode* const aggregate = node(destination_node->value_owned_aggregate_key());
    if ((aggregate == nullptr) ||
        !aggregate_payload_is_in_document_domain(*aggregate) ||
        (aggregate->aggregate_owner_value_key() != destination) ||
        (aggregate->name_id() != destination_node->name_id()))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Destination container pair is invalid.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    if ((aggregate->aggregate_kind() != ELiveAggregateKind::object) &&
        (aggregate->aggregate_kind() != ELiveAggregateKind::array))
    {
        return attachment_rejection(ELiveAttachmentRejection::unsupported_destination_kind);
    }
    if ((destination_node->key() != destination) ||
        !destination_node->forms_container_pair_with(*aggregate))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Destination value and aggregate disagree.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }

    CLiveNode* next_node = nullptr;
    CLiveNode* previous_node = nullptr;
    if (position.next.is_valid())
    {
        next_node = value_node(position.next);
        if (next_node == nullptr)
        {
            return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
        }
    }
    else if (position.previous != aggregate->aggregate_last_child_key())
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    if (position.previous.is_valid())
    {
        previous_node = value_node(position.previous);
        if (previous_node == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Previous insertion sibling is invalid.");
            return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
        }
    }
    else if (position.next != aggregate->aggregate_first_child_key())
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    if ((previous_node != nullptr) && (next_node != nullptr) &&
        !aggregate->aggregate_has_adjacent_children(*previous_node, *next_node))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Insertion siblings are not adjacent.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    if ((previous_node == nullptr) && (next_node != nullptr) &&
        !aggregate->aggregate_has_first_child(*next_node))
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    if ((previous_node != nullptr) && (next_node == nullptr) &&
        !aggregate->aggregate_has_last_child(*previous_node))
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }

    if (aggregate->aggregate_kind() == ELiveAggregateKind::object)
    {
        if (!candidate_node->is_object_entry() || (candidate_node->name_id().query_value() == 0u))
        {
            return attachment_rejection(ELiveAttachmentRejection::object_entry_required);
        }
        CNodeKey existing = aggregate->aggregate_first_child_key();
        for (std::uint32_t visited = 0u; existing.is_valid(); ++visited)
        {
            const CLiveNode* const existing_node = value_node(existing);
            if ((existing_node == nullptr) || (visited >= m_nodes.occupied_count()))
            {
                mark_integrity_bad();
                MV_ASSERT_MSG(false, "Object child traversal did not terminate.");
                return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
            }
            if (existing_node->name_id() == candidate_node->name_id())
            {
                return attachment_rejection(ELiveAttachmentRejection::duplicate_object_name);
            }
            existing = existing_node->value_next_sibling_key();
        }
    }

    bool destination_reachable = false;
    bool cycle = false;
    if (!query_ancestry(destination, candidate, destination_reachable, cycle))
    {
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    if (cycle)
    {
        return attachment_rejection(ELiveAttachmentRejection::cycle);
    }

    SSubtreeTotals totals;
    if (destination_reachable)
    {
        if (!apply_subtree_reachability(candidate, EReferenceAdjustment::add, totals))
        {
            return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
        }
    }

    candidate_node->set_value_attachment(aggregate->key(), position.previous, position.next);
    if (previous_node != nullptr)
    {
        previous_node->set_value_next_sibling_key(candidate);
    }
    else
    {
        aggregate->set_aggregate_first_child_key(candidate);
    }
    if (next_node != nullptr)
    {
        next_node->set_value_previous_sibling_key(candidate);
    }
    else
    {
        aggregate->set_aggregate_last_child_key(candidate);
    }
    aggregate->increment_child_count();

    if (destination_reachable)
    {
        m_value_count += totals.value_count;
        m_aggregate_payload_count += totals.aggregate_count;
        m_recovered_aggregate_count += totals.recovered_aggregate_count;
        m_empty_value_count += totals.empty_value_count;
    }
    surviving_value = candidate;
    return CLiveAttachmentResult{ ELiveAttachmentOutcome::inserted, ELiveAttachmentRejection::none };
}

bool CLiveDocument::detach_value(const CNodeKey value, bool& was_reachable) noexcept
{
    was_reachable = false;
    CLiveNode* const value_record = value_node(value);
    if ((value_record == nullptr) || (value == m_root) ||
        !value_record->value_parent_aggregate_key().is_valid())
    {
        return false;
    }
    CLiveNode* const aggregate = node(value_record->value_parent_aggregate_key());
    const CNodeKey previous_key = value_record->value_previous_sibling_key();
    const CNodeKey next_key = value_record->value_next_sibling_key();
    if ((aggregate == nullptr) || !aggregate_payload_is_in_document_domain(*aggregate))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Detached value has an invalid parent aggregate.");
        return false;
    }
    if (aggregate->child_count() == 0u)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Parent aggregate has an invalid child count.");
        return false;
    }

    CLiveNode* previous = nullptr;
    if (previous_key.is_valid())
    {
        previous = value_node(previous_key);
        if ((previous == nullptr) || !previous->value_is_previous_sibling_of(*value_record))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Detached value has an invalid previous sibling.");
            return false;
        }
    }
    else if (!aggregate->aggregate_has_first_child(*value_record))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Detached value is not its parent's first child.");
        return false;
    }

    CLiveNode* next = nullptr;
    if (next_key.is_valid())
    {
        next = value_node(next_key);
        if ((next == nullptr) || !next->value_is_next_sibling_of(*value_record))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Detached value has an invalid next sibling.");
            return false;
        }
    }
    else if (!aggregate->aggregate_has_last_child(*value_record))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Detached value is not its parent's last child.");
        return false;
    }

    bool ignored = false;
    if (!query_ancestry(value, CNodeKey{}, was_reachable, ignored))
    {
        return false;
    }

    SSubtreeTotals totals;
    if (was_reachable && !apply_subtree_reachability(value, EReferenceAdjustment::remove, totals))
    {
        return false;
    }

    if (previous != nullptr)
    {
        previous->set_value_next_sibling_key(value_record->value_next_sibling_key());
    }
    else
    {
        aggregate->set_aggregate_first_child_key(value_record->value_next_sibling_key());
    }
    if (next != nullptr)
    {
        next->set_value_previous_sibling_key(value_record->value_previous_sibling_key());
    }
    else
    {
        aggregate->set_aggregate_last_child_key(value_record->value_previous_sibling_key());
    }
    aggregate->decrement_child_count();
    value_record->clear_value_attachment();

    if (was_reachable)
    {
        if (!commit_reachable_totals(totals, SSubtreeTotals{}))
        {
            return false;
        }
    }
    return true;
}

bool CLiveDocument::substitute_value_position(const CNodeKey displaced, const CNodeKey replacement) noexcept
{
    CLiveNode* const displaced_value = value_node(displaced);
    CLiveNode* const replacement_value = value_node(replacement);
    if ((displaced_value == nullptr) || (replacement_value == nullptr) ||
        (displaced == replacement) || !replacement_value->value_is_unattached())
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Value substitution arguments are inconsistent.");
        return false;
    }

    const CNodeKey parent_key = displaced_value->value_parent_aggregate_key();
    if (!parent_key.is_valid())
    {
        if (!displaced_value->value_is_unattached())
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Unattached displaced value has inconsistent topology.");
            return false;
        }
        return true;
    }

    CLiveNode* const parent = node(parent_key);
    const CNodeKey previous_key = displaced_value->value_previous_sibling_key();
    const CNodeKey next_key = displaced_value->value_next_sibling_key();
    CLiveNode* const previous = previous_key.is_valid() ? value_node(previous_key) : nullptr;
    CLiveNode* const next = next_key.is_valid() ? value_node(next_key) : nullptr;
    if ((parent == nullptr) || !aggregate_payload_is_in_document_domain(*parent) ||
        (parent->child_count() == 0u) ||
        (previous_key.is_valid() && ((previous == nullptr) || !previous->value_is_previous_sibling_of(*displaced_value))) ||
        (!previous_key.is_valid() && !parent->aggregate_has_first_child(*displaced_value)) ||
        (next_key.is_valid() && ((next == nullptr) || !next->value_is_next_sibling_of(*displaced_value))) ||
        (!next_key.is_valid() && !parent->aggregate_has_last_child(*displaced_value)))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Displaced value has inconsistent parent topology.");
        return false;
    }

    replacement_value->set_value_attachment(parent_key, previous_key, next_key);
    if (previous != nullptr)
    {
        previous->set_value_next_sibling_key(replacement);
    }
    else
    {
        parent->set_aggregate_first_child_key(replacement);
    }
    if (next != nullptr)
    {
        next->set_value_previous_sibling_key(replacement);
    }
    else
    {
        parent->set_aggregate_last_child_key(replacement);
    }
    displaced_value->clear_value_attachment();
    return true;
}

bool CLiveDocument::erase_subtree(const CNodeKey value) noexcept
{
    CNodeKey current = subtree_first_postorder(value);
    if (!current.is_valid() || m_integrity_known_bad)
    {
        return false;
    }
    const std::uint32_t traversal_limit = m_nodes.occupied_count();
    std::uint32_t erased_values = 0u;
    while (current.is_valid())
    {
        const CLiveNode* const current_value = value_node(current);
        if (current_value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree erase encountered an invalid value.");
            return false;
        }
        const CNodeKey aggregate = current_value->value_owned_aggregate_key();
        const CNodeKey next = subtree_next_postorder(value, current);
        if (m_integrity_known_bad)
        {
            return false;
        }
        if (aggregate.is_valid() && !m_nodes.erase(aggregate))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree aggregate erase failed.");
            return false;
        }
        if (!m_nodes.erase(current))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree value erase failed.");
            return false;
        }
        ++erased_values;
        if (erased_values > traversal_limit)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree erase did not terminate.");
            return false;
        }
        current = next;
    }
    return erased_values != 0u;
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
    m_empty_value_count = source.m_empty_value_count;
    m_property_names_ready = source.m_property_names_ready;
    m_string_values_ready = source.m_string_values_ready;
    m_integrity_known_bad = source.m_integrity_known_bad;

    source.m_root = CNodeKey{};
    source.m_next_monotonic_node_key = 1u;
    source.m_value_count = 0u;
    source.m_aggregate_payload_count = 0u;
    source.m_referenced_property_name_count = 0u;
    source.m_referenced_string_value_count = 0u;
    source.m_recovered_aggregate_count = 0u;
    source.m_empty_value_count = 0u;
    source.m_property_names_ready = false;
    source.m_string_values_ready = false;
    source.m_integrity_known_bad = false;
}
