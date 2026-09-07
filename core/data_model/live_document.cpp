
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
    if (!staged.m_nodes.initialise((initial_node_capacity < 2u) ? 2u : initial_node_capacity))
    {
        return false;
    }

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
    m_root = CNodeKey{};
    m_next_monotonic_node_key = 1u;
    m_integrity_known_bad = false;
}

bool CLiveDocument::is_ready() const noexcept
{
    return m_nodes.is_ready() && m_root.is_valid() && !m_integrity_known_bad;
}

bool CLiveDocument::is_canonical() const noexcept
{
    SLiveDocumentAnalysis result;
    return analyse(result) && (result.recovered_aggregate_count == 0u);
}

bool CLiveDocument::is_complete() const noexcept
{
    SLiveDocumentAnalysis result;
    return analyse(result) && (result.empty_value_count == 0u);
}

template<typename TVisitor>
bool CLiveDocument::visit_subtree(const TLiveNodeSlot subtree_root, TVisitor&& visitor) const noexcept
{
    TLiveNodeSlot current = subtree_root;
    std::uint32_t visited = 0u;
    while (current >= 0)
    {
        if (visited >= m_nodes.occupied_count())
        {
            return false;
        }
        ++visited;
        const CLiveNode* const value = value_node(current);
        if ((value == nullptr) || !value_payload_is_in_document_domain(*value))
        {
            return false;
        }
        const CLiveNode* const aggregate = live_value_type_is_container(value->value_type()) ?
            node(value->value_owned_aggregate_slot()) : nullptr;
        if (live_value_type_is_container(value->value_type()) &&
            ((aggregate == nullptr) || !aggregate_payload_is_in_document_domain(*aggregate)))
        {
            return false;
        }
        if (!visitor(current, *value, aggregate) || !subtree_next(subtree_root, current, current))
        {
            return false;
        }
    }
    return visited != 0u;
}

bool CLiveDocument::analyse(SLiveDocumentAnalysis& result, SLiveDocumentStringAnalysis* const strings) const noexcept
{
    result = SLiveDocumentAnalysis{};
    if (strings != nullptr)
    {
        strings->referenced_property_name_count = 0u;
        strings->referenced_string_value_count = 0u;
    }
    if (!is_ready())
    {
        return false;
    }
    if (strings != nullptr)
    {
        const std::size_t names = m_property_names.string_count();
        const std::size_t values = m_string_values.string_count();
        if ((names >= CPropertyNameId::k_invalid_value) ||
            (values >= CStringValueId::k_invalid_value) ||
            !strings->property_name_references.resize(names + 1u) ||
            !strings->string_value_references.resize(values + 1u))
        {
            return false;
        }
        for (std::size_t id = 0u; id <= names; ++id)
        {
            strings->property_name_references[id] = 0u;
        }
        for (std::size_t id = 0u; id <= values; ++id)
        {
            strings->string_value_references[id] = 0u;
        }
    }

    SLiveDocumentAnalysis measured;
    const bool success = visit_subtree(node_slot(m_root),
        [&measured, strings](const TLiveNodeSlot, const CLiveNode& value, const CLiveNode* const aggregate) noexcept
        {
            ++measured.value_count;
            if (value.value_type() == ELiveValueType::empty)
            {
                ++measured.empty_value_count;
            }
            if (aggregate != nullptr)
            {
                if (aggregate->aggregate_kind() == ELiveAggregateKind::recovered_array)
                {
                    ++measured.recovered_aggregate_count;
                }
            }
            if (strings != nullptr)
            {
                const std::uint32_t name_id = value.name_id().query_value();
                std::uint32_t& name_references = strings->property_name_references[name_id];
                if ((name_id != 0u) && (name_references == 0u))
                {
                    ++strings->referenced_property_name_count;
                }
                ++name_references;
                if (value.value_type() == ELiveValueType::string)
                {
                    const std::uint32_t string_id = static_cast<std::uint32_t>(value.payload_bits());
                    std::uint32_t& value_references = strings->string_value_references[string_id];
                    if ((string_id != 0u) && (value_references == 0u))
                    {
                        ++strings->referenced_string_value_count;
                    }
                    ++value_references;
                }
            }
            return true;
        });
    if (!success)
    {
        return false;
    }
    result = measured;
    return true;
}

bool CLiveDocument::check_integrity() const noexcept
{
    if (!is_ready() || !m_nodes.check_integrity() ||
        !check_string_domain(m_property_names) ||
        !check_string_domain(m_string_values) ||
        (m_root.query_value() != 1u))
    {
        return false;
    }

    const CLiveNode* const root_node = value_node(m_root);
    if ((root_node == nullptr) || (root_node->value_type() != ELiveValueType::object) ||
        (root_node->name_id().query_value() != 0u) ||
        !root_node->value_is_unattached() ||
        (node_key(root_node->value_owned_aggregate_slot()).query_value() != 2u))
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
        const CNodeKey* const current_key = m_nodes.key_at_slot(index);
        if ((current == nullptr) || (current_key == nullptr) || !current_key->is_valid() ||
            (previous_key.is_valid() && (previous_key.relationship(*current_key) >= 0)) ||
            !current->name_id().is_valid() ||
            (!current->name_id().is_empty() && !m_property_names.is_valid_id(current->name_id().query_value())))
        {
            return false;
        }
        previous_key = *current_key;

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
            if (current->value_parent_aggregate_slot() >= 0)
            {
                const CLiveNode* const parent_aggregate = node(current->value_parent_aggregate_slot());
                if ((parent_aggregate == nullptr) || !aggregate_payload_is_in_document_domain(*parent_aggregate))
                {
                    return false;
                }
            }

            if (live_value_type_is_container(current->value_type()))
            {
                ++containers;
                const TLiveNodeSlot aggregate_slot = current->value_owned_aggregate_slot();
                const CLiveNode* const aggregate = node(aggregate_slot);
                if ((aggregate == nullptr) ||
                    !aggregate_payload_is_in_document_domain(*aggregate) ||
                    !current->forms_container_pair_with(*aggregate, index, aggregate_slot))
                {
                    return false;
                }
            }
        }
        else if (current->is_aggregate_record())
        {
            ++aggregates;
            const TLiveNodeSlot owner_slot = current->aggregate_owner_value_slot();
            const CLiveNode* const owner = value_node(owner_slot);
            if (!aggregate_payload_is_in_document_domain(*current) ||
                (owner == nullptr) || !owner->forms_container_pair_with(*current, owner_slot, index))
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

    std::uint64_t forest_records = 0u;
    if (!audit_subtree_checked(node_slot(m_root), forest_records))
    {
        return false;
    }

    for (std::int32_t index = m_nodes.first_live(); index >= 0; index = m_nodes.next_live(index))
    {
        const CLiveNode* const current = m_nodes.get_slot(index);
        if ((current != nullptr) && current->is_value_record() &&
            (node_key(index) != m_root) && (current->value_parent_aggregate_slot() < 0))
        {
            std::uint64_t detached_records = 0u;
            if (!audit_subtree_checked(index, detached_records))
            {
                return false;
            }
            forest_records += detached_records;
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

    return true;
}

CNodeKey CLiveDocument::root() const noexcept
{
    return is_ready() ? m_root : CNodeKey{};
}

std::uint32_t CLiveDocument::value_count() const noexcept
{
    SLiveDocumentAnalysis result;
    return analyse(result) ? result.value_count : 0u;
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
    if (!id.is_valid() || (!id.is_empty() && !m_property_names.is_valid_id(id.query_value())))
    {
        return CStringView{};
    }
    return m_property_names.view(id.query_value());
}

CStringView CLiveDocument::string_value(const CStringValueId id) const noexcept
{
    if (!id.is_valid() || (!id.is_empty() && !m_string_values.is_valid_id(id.query_value())))
    {
        return CStringView{};
    }
    return m_string_values.view(id.query_value());
}

CPropertyNameId CLiveDocument::property_name_id_at_rank(const std::uint32_t rank) const noexcept
{
    return is_ready() ? CPropertyNameId{ string_id_at_rank(m_property_names, rank) } : CPropertyNameId{};
}

CStringValueId CLiveDocument::string_value_id_at_rank(const std::uint32_t rank) const noexcept
{
    return is_ready() ? CStringValueId{ string_id_at_rank(m_string_values, rank) } : CStringValueId{};
}

CNodeKey CLiveDocument::parent(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    if ((found == nullptr) || (found->value_parent_aggregate_slot() < 0))
    {
        return CNodeKey{};
    }
    const CLiveNode* const aggregate = node(found->value_parent_aggregate_slot());
    return (aggregate != nullptr) ? node_key(aggregate->aggregate_owner_value_slot()) : CNodeKey{};
}

CNodeKey CLiveDocument::previous_sibling(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) ? node_key(found->value_previous_sibling_slot()) : CNodeKey{};
}

CNodeKey CLiveDocument::next_sibling(const CNodeKey value) const noexcept
{
    const CLiveNode* const found = value_node(value);
    return (found != nullptr) ? node_key(found->value_next_sibling_slot()) : CNodeKey{};
}

std::uint32_t CLiveDocument::child_count(const CNodeKey container_value) const noexcept
{
    const CLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? aggregate->child_count() : 0u;
}

CNodeKey CLiveDocument::first_child(const CNodeKey container_value) const noexcept
{
    const CLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? node_key(aggregate->aggregate_first_child_slot()) : CNodeKey{};
}

CNodeKey CLiveDocument::last_child(const CNodeKey container_value) const noexcept
{
    const CLiveNode* const aggregate = aggregate_for_value(container_value);
    return (aggregate != nullptr) ? node_key(aggregate->aggregate_last_child_slot()) : CNodeKey{};
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
    new_node.initialise_value(ELiveValueType::string, value_id.query_value(), name_id, CIntegerMetadata{});

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

CNodeKey CLiveDocument::create_recovered_array(const CStringView& name_value) noexcept
{
    SPreparedString prepared_name;
    return prepare_string(name_value, prepared_name) ?
        create_container(ELiveValueType::recovered_array, prepared_name) : CNodeKey{};
}

CLiveAttachmentResult CLiveDocument::append_child(const CNodeKey destination, const CNodeKey candidate) noexcept
{
    SAttachmentPosition position;
    const CLiveNode* const aggregate = aggregate_for_value(destination);
    if (aggregate != nullptr)
    {
        position.previous = aggregate->aggregate_last_child_slot();
    }
    return attach_child(destination, candidate, position);
}

CLiveAttachmentResult CLiveDocument::insert_child_before(const CNodeKey destination, const CNodeKey candidate, const CNodeKey before) noexcept
{
    if (!before.is_valid())
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    SAttachmentPosition position;
    const CLiveNode* const before_node = value_node(before);
    if (before_node == nullptr)
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    position.next = node_slot(before);
    position.previous = before_node->value_previous_sibling_slot();
    return attach_child(destination, candidate, position);
}

CLiveAttachmentResult CLiveDocument::insert_child_at(const CNodeKey destination, const CNodeKey candidate, const std::uint32_t index) noexcept
{
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
        return append_child(destination, candidate);
    }

    TLiveNodeSlot before = aggregate->aggregate_first_child_slot();
    for (std::uint32_t ordinal = 0u; ordinal < index; ++ordinal)
    {
        const CLiveNode* const before_node = value_node(before);
        if (before_node == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Indexed child traversal encountered an invalid value.");
            return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
        }
        before = before_node->value_next_sibling_slot();
    }
    if (before < 0)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Indexed child traversal ended early.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    SAttachmentPosition position;
    position.next = before;
    const CLiveNode* const before_node = value_node(before);
    if (before_node != nullptr)
    {
        position.previous = before_node->value_previous_sibling_slot();
    }
    return attach_child(destination, candidate, position);
}

bool CLiveDocument::detach(const CNodeKey value) noexcept
{
    if (!is_ready() || (value == m_root))
    {
        return false;
    }
    return detach_value(node_slot(value));
}

CNodeKey CLiveDocument::detach_payload(const CNodeKey source) noexcept
{
    if (!is_ready() || (source == m_root))
    {
        return CNodeKey{};
    }

    const TLiveNodeSlot source_slot = node_slot(source);
    const CLiveNode* source_node = value_node(source_slot);
    if ((source_node == nullptr) || (source_node->value_type() == ELiveValueType::empty))
    {
        return CNodeKey{};
    }
    const TLiveNodeSlot aggregate_slot = source_node->value_owned_aggregate_slot();
    const bool aggregate_is_expected = live_value_type_is_container(source_node->value_type());
    const CLiveNode* const aggregate_before_allocation = node(aggregate_slot);
    if (((aggregate_slot >= 0) != aggregate_is_expected) ||
        (aggregate_is_expected && ((aggregate_before_allocation == nullptr) ||
            !source_node->forms_container_pair_with(*aggregate_before_allocation, source_slot, aggregate_slot))))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload source has an invalid aggregate pair.");
        return CNodeKey{};
    }

    const CNodeKey payload_key = create_empty_node(
        CPropertyNameId{ CPropertyNameId::k_empty_value });
    if (!payload_key.is_valid())
    {
        return CNodeKey{};
    }

    const TLiveNodeSlot payload_slot = node_slot(payload_key);
    if (!move_value_payload(payload_slot, source_slot))
    {
        (void)m_nodes.erase(payload_slot);
        return CNodeKey{};
    }
    return payload_key;
}

CNodeKey CLiveDocument::attach_payload(const CNodeKey empty_target, const CNodeKey detached_payload) noexcept
{
    if (!is_ready())
    {
        return CNodeKey{};
    }
    const TLiveNodeSlot target_slot = node_slot(empty_target);
    const TLiveNodeSlot payload_slot = node_slot(detached_payload);
    CLiveNode* target = value_node(target_slot);
    CLiveNode* payload = value_node(payload_slot);
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
    const TLiveNodeSlot payload_aggregate_slot = payload->value_owned_aggregate_slot();
    CLiveNode* const payload_aggregate = live_value_type_is_container(payload->value_type()) ?
        node(payload_aggregate_slot) : nullptr;
    if (live_value_type_is_container(payload->value_type()) &&
        ((payload_aggregate == nullptr) || !payload->forms_container_pair_with(*payload_aggregate, payload_slot, payload_aggregate_slot)))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload has an invalid aggregate pair.");
        return CNodeKey{};
    }

    bool cycle = false;
    if (!query_ancestry(target_slot, payload_slot, cycle))
    {
        return CNodeKey{};
    }
    if (cycle)
    {
        return CNodeKey{};
    }

    if (!move_value_payload(target_slot, payload_slot))
    {
        return CNodeKey{};
    }
    if (!m_nodes.erase(payload_slot))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Consumed payload shell could not be erased.");
        return CNodeKey{};
    }
    return empty_target;
}

bool CLiveDocument::erase_payload(const CNodeKey value) noexcept
{
    if (!is_ready())
    {
        return false;
    }
    if (value == m_root)
    {
        return clear_root();
    }

    const TLiveNodeSlot value_slot = node_slot(value);
    CLiveNode* value_record = value_node(value_slot);
    if (value_record == nullptr)
    {
        return false;
    }
    if (!value_payload_is_in_document_domain(*value_record))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Erased value has an invalid payload.");
        return false;
    }
    if (value_record->value_type() == ELiveValueType::empty)
    {
        return true;
    }

    if (live_value_type_is_container(value_record->value_type()))
    {
        const TLiveNodeSlot aggregate_slot = value_record->value_owned_aggregate_slot();
        CLiveNode* const aggregate = node(aggregate_slot);
        if ((aggregate == nullptr) ||
            !aggregate_payload_is_in_document_domain(*aggregate) ||
            !value_record->forms_container_pair_with(*aggregate, value_slot, aggregate_slot))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Erased value has an invalid aggregate pair.");
            return false;
        }
        if (!erase_aggregate_children(aggregate_slot) || !m_nodes.erase(aggregate_slot))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Erased value aggregate could not be removed.");
            return false;
        }
        value_record = value_node(value_slot);
        if (value_record == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Value disappeared while erasing its payload.");
            return false;
        }
    }

    value_record->clear_value_payload();
    return true;
}

bool CLiveDocument::erase(const CNodeKey value) noexcept
{
    if (!is_ready())
    {
        return false;
    }
    if (value == m_root)
    {
        return clear_root();
    }

    const TLiveNodeSlot value_slot = node_slot(value);
    CLiveNode* const value_record = value_node(value_slot);
    if (value_record == nullptr)
    {
        return false;
    }
    if (value_record->value_parent_aggregate_slot() >= 0)
    {
        if (!detach_value(value_slot))
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
    if (!erase_subtree(value_slot))
    {
        return false;
    }
    return true;
}

std::uint32_t CLiveDocument::memory_token_count() const noexcept
{
    return m_nodes.memory_token_count() + m_property_names.memory_token_count() +
        m_string_values.memory_token_count();
}

std::uint32_t CLiveDocument::memory_allocation_count() const noexcept
{
    return m_nodes.memory_allocation_count() + m_property_names.memory_allocation_count() +
        m_string_values.memory_allocation_count();
}

std::uint64_t CLiveDocument::memory_allocation_size() const noexcept
{
    return m_nodes.memory_allocation_size() + m_property_names.memory_allocation_size() +
        m_string_values.memory_allocation_size();
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

bool CLiveDocument::intern_string_domain(const SPreparedString& value, CStableStrings& strings, std::uint32_t& id) noexcept
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

    const std::size_t next_id = strings.string_count() + 1u;
    if (next_id >= CPropertyNameId::k_invalid_value)
    {
        return false;
    }

    const std::size_t appended = strings.append(value.bytes, value.size);
    if (appended == CStableStrings::k_invalid_id)
    {
        return false;
    }
    if (appended != next_id)
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
    if (!intern_string_domain(value, m_string_values, raw_id))
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
    new_node.initialise_value(type, payload_bits, name_id, metadata);

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
    value.initialise_value(ELiveValueType::empty, 0u, name, CIntegerMetadata{});
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
    value.initialise_value(type, 0u, name_id, CIntegerMetadata{});
    const TLiveNodeSlot value_slot = m_nodes.insert(value_key, value);
    if (value_slot < 0)
    {
        return CNodeKey{};
    }

    CLiveNode aggregate{};
    aggregate.initialise_aggregate(
        value_slot,
        live_aggregate_kind_for_value_type(type),
        CPropertyNameId{ CPropertyNameId::k_empty_value });
    const TLiveNodeSlot aggregate_slot = m_nodes.insert(aggregate_key, aggregate);
    if (aggregate_slot < 0)
    {
        (void)m_nodes.erase(value_slot);
        return CNodeKey{};
    }
    value_node(value_slot)->set_value_owned_aggregate_slot(aggregate_slot);
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
    root_node.initialise_value(
        ELiveValueType::object,
        0u,
        CPropertyNameId{ CPropertyNameId::k_empty_value },
        CIntegerMetadata{});
    const TLiveNodeSlot root_slot = m_nodes.insert(root_key, root_node);
    if (root_slot < 0)
    {
        return false;
    }

    CLiveNode aggregate{};
    aggregate.initialise_aggregate(
        root_slot,
        ELiveAggregateKind::object,
        CPropertyNameId{ CPropertyNameId::k_empty_value });
    const TLiveNodeSlot aggregate_slot = m_nodes.insert(aggregate_key, aggregate);
    if (aggregate_slot < 0)
    {
        (void)m_nodes.erase(root_slot);
        return false;
    }
    value_node(root_slot)->set_value_owned_aggregate_slot(aggregate_slot);

    m_root = root_key;
    return true;
}

TLiveNodeSlot CLiveDocument::node_slot(const CNodeKey key) const noexcept
{
    return key.is_valid() ? m_nodes.find_index(key) : k_invalid_live_node_slot;
}

CNodeKey CLiveDocument::node_key(const TLiveNodeSlot slot) const noexcept
{
    const CNodeKey* const key = m_nodes.key_at_slot(slot);
    return (key != nullptr) ? *key : CNodeKey{};
}

CLiveNode* CLiveDocument::node(const TLiveNodeSlot slot) noexcept
{
    return m_nodes.get_slot(slot);
}

const CLiveNode* CLiveDocument::node(const TLiveNodeSlot slot) const noexcept
{
    return m_nodes.get_slot(slot);
}

CLiveNode* CLiveDocument::value_node(const CNodeKey key) noexcept
{
    CLiveNode* const found = node(node_slot(key));
    return ((found != nullptr) && found->is_value_record()) ? found : nullptr;
}

const CLiveNode* CLiveDocument::value_node(const CNodeKey key) const noexcept
{
    const CLiveNode* const found = node(node_slot(key));
    return ((found != nullptr) && found->is_value_record()) ? found : nullptr;
}

CLiveNode* CLiveDocument::value_node(const TLiveNodeSlot slot) noexcept
{
    CLiveNode* const found = node(slot);
    return ((found != nullptr) && found->is_value_record()) ? found : nullptr;
}

const CLiveNode* CLiveDocument::value_node(const TLiveNodeSlot slot) const noexcept
{
    const CLiveNode* const found = node(slot);
    return ((found != nullptr) && found->is_value_record()) ? found : nullptr;
}

const CLiveNode* CLiveDocument::aggregate_for_value(const CNodeKey value) const noexcept
{
    const CLiveNode* const owner = value_node(value);
    if ((owner == nullptr) || !live_value_type_is_container(owner->value_type()))
    {
        return nullptr;
    }
    const CLiveNode* const aggregate = node(owner->value_owned_aggregate_slot());
    return ((aggregate != nullptr) && aggregate->is_aggregate_record()) ? aggregate : nullptr;
}

CLiveAttachmentResult CLiveDocument::attachment_rejection(const ELiveAttachmentRejection rejection) noexcept
{
    return CLiveAttachmentResult{ rejection };
}

void CLiveDocument::mark_integrity_bad() noexcept
{
    m_integrity_known_bad = true;
}

bool CLiveDocument::value_payload_is_in_document_domain(const CLiveNode& value) const noexcept
{
    return value.value_payload_is_valid() &&
        (value.name_id().is_empty() || m_property_names.is_valid_id(value.name_id().query_value())) &&
        ((value.value_type() != ELiveValueType::string) || (value.payload_bits() == 0u) ||
            m_string_values.is_valid_id(static_cast<std::size_t>(value.payload_bits())));
}

bool CLiveDocument::aggregate_payload_is_in_document_domain(const CLiveNode& aggregate) const noexcept
{
    return aggregate.aggregate_payload_is_valid();
}

std::uint32_t CLiveDocument::string_id_at_rank(const CStableStrings& strings, const std::uint32_t rank) noexcept
{
    if (rank == 0u)
    {
        return 0u;
    }
    const std::size_t ref_index = strings.rank_to_ref_index(rank);
    const std::size_t id = strings.ref_index_to_id(ref_index);
    return ((id != CStableStrings::k_invalid_id) && (id < CPropertyNameId::k_invalid_value)) ?
        static_cast<std::uint32_t>(id) : CPropertyNameId::k_invalid_value;
}

bool CLiveDocument::subtree_next(const TLiveNodeSlot subtree_root, TLiveNodeSlot current, TLiveNodeSlot& next) const noexcept
{
    next = k_invalid_live_node_slot;
    const CLiveNode* value = value_node(current);
    if (value == nullptr)
    {
        return false;
    }
    if (live_value_type_is_container(value->value_type()))
    {
        const CLiveNode* const aggregate = node(value->value_owned_aggregate_slot());
        if ((aggregate == nullptr) || !aggregate->is_aggregate_record())
        {
            return false;
        }
        if (aggregate->aggregate_first_child_slot() >= 0)
        {
            next = aggregate->aggregate_first_child_slot();
            return true;
        }
    }
    for (std::uint32_t ascent = 0u; ascent <= m_nodes.occupied_count(); ++ascent)
    {
        if (current == subtree_root)
        {
            return true;
        }
        value = value_node(current);
        if (value == nullptr)
        {
            return false;
        }
        if (value->value_next_sibling_slot() >= 0)
        {
            next = value->value_next_sibling_slot();
            return true;
        }
        const CLiveNode* const parent = node(value->value_parent_aggregate_slot());
        if ((parent == nullptr) || !parent->is_aggregate_record())
        {
            return false;
        }
        current = parent->aggregate_owner_value_slot();
    }
    return false;
}

TLiveNodeSlot CLiveDocument::subtree_first_postorder(const TLiveNodeSlot subtree_root) noexcept
{
    TLiveNodeSlot first = subtree_root;
    for (std::uint32_t descent = 0u; descent <= m_nodes.occupied_count(); ++descent)
    {
        const CLiveNode* const value = value_node(first);
        if (value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid value.");
            return k_invalid_live_node_slot;
        }
        if (!live_value_type_is_container(value->value_type()))
        {
            return first;
        }
        const CLiveNode* const aggregate = node(value->value_owned_aggregate_slot());
        if (aggregate == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid aggregate.");
            return k_invalid_live_node_slot;
        }
        if (aggregate->aggregate_first_child_slot() < 0)
        {
            return first;
        }
        first = aggregate->aggregate_first_child_slot();
    }
    mark_integrity_bad();
    MV_ASSERT_MSG(false, "Postorder traversal did not terminate.");
    return k_invalid_live_node_slot;
}

TLiveNodeSlot CLiveDocument::subtree_next_postorder(const TLiveNodeSlot subtree_root, const TLiveNodeSlot current) noexcept
{
    if (current == subtree_root)
    {
        return k_invalid_live_node_slot;
    }
    const CLiveNode* const value = value_node(current);
    if (value == nullptr)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid value.");
        return k_invalid_live_node_slot;
    }
    if (value->value_next_sibling_slot() >= 0)
    {
        return subtree_first_postorder(value->value_next_sibling_slot());
    }
    const CLiveNode* const parent = node(value->value_parent_aggregate_slot());
    if (parent == nullptr)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Postorder traversal encountered an invalid parent aggregate.");
        return k_invalid_live_node_slot;
    }
    return parent->aggregate_owner_value_slot();
}

bool CLiveDocument::audit_subtree_checked(const TLiveNodeSlot subtree_root, std::uint64_t& records) const noexcept
{
    records = 0u;
    return visit_subtree(subtree_root,
        [this, &records](const TLiveNodeSlot value_slot, const CLiveNode& value_record, const CLiveNode* const aggregate) noexcept
    {
        const CLiveNode* const value = &value_record;
        ++records;
        if (records > m_nodes.occupied_count())
        {
            return false;
        }

        if (aggregate != nullptr)
        {
            const TLiveNodeSlot aggregate_slot = value->value_owned_aggregate_slot();
            if (!value->forms_container_pair_with(*aggregate, value_slot, aggregate_slot))
            {
                return false;
            }
            ++records;
            if (records > m_nodes.occupied_count())
            {
                return false;
            }

            TLiveNodeSlot previous = k_invalid_live_node_slot;
            TLiveNodeSlot child = aggregate->aggregate_first_child_slot();
            std::uint64_t children = 0u;
            while (child >= 0)
            {
                const CLiveNode* const child_node = value_node(child);
                if (child_node == nullptr)
                {
                    return false;
                }
                if (previous >= 0)
                {
                    const CLiveNode* const previous_node = value_node(previous);
                    if ((previous_node == nullptr) ||
                        !previous_node->value_is_previous_sibling_of(*child_node, previous, child))
                    {
                        return false;
                    }
                }
                else if (!aggregate->aggregate_has_first_child(*child_node, aggregate_slot, child))
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
                    for (TLiveNodeSlot earlier = aggregate->aggregate_first_child_slot();
                        (earlier >= 0) && (earlier != child);)
                    {
                        const CLiveNode* const earlier_node = value_node(earlier);
                        ++earlier_count;
                        if ((earlier_count > m_nodes.occupied_count()) ||
                            (earlier_node == nullptr) ||
                            (earlier_node->name_id() == child_node->name_id()))
                        {
                            return false;
                        }
                        earlier = earlier_node->value_next_sibling_slot();
                    }
                }
                previous = child;
                child = child_node->value_next_sibling_slot();
                ++children;
                if (children > m_nodes.occupied_count())
                {
                    return false;
                }
            }
            const CLiveNode* const last_child = value_node(previous);
            if ((children != aggregate->child_count()) ||
                ((children != 0u) && ((last_child == nullptr) ||
                    !aggregate->aggregate_has_last_child(*last_child, aggregate_slot, previous))))
            {
                return false;
            }
        }

        return true;
    });
}

bool CLiveDocument::query_ancestry(const TLiveNodeSlot value, const TLiveNodeSlot sought, bool& found) noexcept
{
    found = false;
    TLiveNodeSlot current = value;
    const TLiveNodeSlot root_slot = node_slot(m_root);
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
        if (current == root_slot)
        {
            return true;
        }
        const TLiveNodeSlot parent_slot = current_value->value_parent_aggregate_slot();
        if (parent_slot < 0)
        {
            return current_value->value_is_unattached();
        }
        const CLiveNode* const aggregate = node(parent_slot);
        if (aggregate == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Ancestry traversal encountered an invalid aggregate.");
            return false;
        }
        current = aggregate->aggregate_owner_value_slot();
    }
    mark_integrity_bad();
    MV_ASSERT_MSG(false, "Ancestry traversal did not terminate.");
    return false;
}

CLiveAttachmentResult CLiveDocument::attach_child(const CNodeKey destination, const CNodeKey candidate, const SAttachmentPosition& position) noexcept
{
    if (!is_ready())
    {
        return attachment_rejection(ELiveAttachmentRejection::document_not_ready);
    }
    const TLiveNodeSlot destination_slot = node_slot(destination);
    CLiveNode* const destination_node = value_node(destination_slot);
    if (destination_node == nullptr)
    {
        return attachment_rejection(ELiveAttachmentRejection::destination_not_found);
    }
    const TLiveNodeSlot candidate_slot = node_slot(candidate);
    CLiveNode* const candidate_node = value_node(candidate_slot);
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

    const TLiveNodeSlot aggregate_slot = destination_node->value_owned_aggregate_slot();
    CLiveNode* const aggregate = node(aggregate_slot);
    if ((aggregate == nullptr) ||
        !aggregate_payload_is_in_document_domain(*aggregate) ||
        (aggregate->aggregate_owner_value_slot() != destination_slot))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Destination container pair is invalid.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    if (!destination_node->forms_container_pair_with(*aggregate, destination_slot, aggregate_slot))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Destination value and aggregate disagree.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }

    CLiveNode* next_node = nullptr;
    CLiveNode* previous_node = nullptr;
    if (position.next >= 0)
    {
        next_node = value_node(position.next);
        if (next_node == nullptr)
        {
            return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
        }
    }
    else if (position.previous != aggregate->aggregate_last_child_slot())
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    if (position.previous >= 0)
    {
        previous_node = value_node(position.previous);
        if (previous_node == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Previous insertion sibling is invalid.");
            return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
        }
    }
    else if (position.next != aggregate->aggregate_first_child_slot())
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    if ((previous_node != nullptr) && (next_node != nullptr) &&
        !aggregate->aggregate_has_adjacent_children(
            *previous_node,
            *next_node,
            aggregate_slot,
            position.previous,
            position.next))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Insertion siblings are not adjacent.");
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    if ((previous_node == nullptr) && (next_node != nullptr) && !aggregate->aggregate_has_first_child(*next_node, aggregate_slot, position.next))
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }
    if ((previous_node != nullptr) && (next_node == nullptr) && !aggregate->aggregate_has_last_child(*previous_node, aggregate_slot, position.previous))
    {
        return attachment_rejection(ELiveAttachmentRejection::insert_before_not_child);
    }

    if (!aggregate->aggregate_accepts_child(*candidate_node))
    {
        return attachment_rejection(
            (aggregate->aggregate_kind() == ELiveAggregateKind::object) ?
                ELiveAttachmentRejection::object_entry_required :
                ELiveAttachmentRejection::anonymous_value_required);
    }
    if (aggregate->aggregate_kind() == ELiveAggregateKind::object)
    {
        TLiveNodeSlot existing = aggregate->aggregate_first_child_slot();
        for (std::uint32_t visited = 0u; existing >= 0; ++visited)
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
            existing = existing_node->value_next_sibling_slot();
        }
    }
    bool cycle = false;
    if (!query_ancestry(destination_slot, candidate_slot, cycle))
    {
        return attachment_rejection(ELiveAttachmentRejection::corrupt_structure);
    }
    if (cycle)
    {
        return attachment_rejection(ELiveAttachmentRejection::cycle);
    }

    candidate_node->set_value_attachment(aggregate_slot, position.previous, position.next);
    if (previous_node != nullptr)
    {
        previous_node->set_value_next_sibling_slot(candidate_slot);
    }
    else
    {
        aggregate->set_aggregate_first_child_slot(candidate_slot);
    }
    if (next_node != nullptr)
    {
        next_node->set_value_previous_sibling_slot(candidate_slot);
    }
    else
    {
        aggregate->set_aggregate_last_child_slot(candidate_slot);
    }
    aggregate->increment_child_count();

    return CLiveAttachmentResult{};
}

bool CLiveDocument::detach_value(const TLiveNodeSlot value) noexcept
{
    CLiveNode* const value_record = value_node(value);
    if ((value_record == nullptr) || (node_key(value) == m_root) ||
        (value_record->value_parent_aggregate_slot() < 0))
    {
        return false;
    }
    const TLiveNodeSlot aggregate_slot = value_record->value_parent_aggregate_slot();
    CLiveNode* const aggregate = node(aggregate_slot);
    const TLiveNodeSlot previous_slot = value_record->value_previous_sibling_slot();
    const TLiveNodeSlot next_slot = value_record->value_next_sibling_slot();
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
    if (previous_slot >= 0)
    {
        previous = value_node(previous_slot);
        if ((previous == nullptr) ||
            !previous->value_is_previous_sibling_of(*value_record, previous_slot, value))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Detached value has an invalid previous sibling.");
            return false;
        }
    }
    else if (!aggregate->aggregate_has_first_child(*value_record, aggregate_slot, value))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Detached value is not its parent's first child.");
        return false;
    }

    CLiveNode* next = nullptr;
    if (next_slot >= 0)
    {
        next = value_node(next_slot);
        if ((next == nullptr) ||
            !next->value_is_next_sibling_of(*value_record, next_slot, value))
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Detached value has an invalid next sibling.");
            return false;
        }
    }
    else if (!aggregate->aggregate_has_last_child(*value_record, aggregate_slot, value))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Detached value is not its parent's last child.");
        return false;
    }

    if (previous != nullptr)
    {
        previous->set_value_next_sibling_slot(value_record->value_next_sibling_slot());
    }
    else
    {
        aggregate->set_aggregate_first_child_slot(value_record->value_next_sibling_slot());
    }
    if (next != nullptr)
    {
        next->set_value_previous_sibling_slot(value_record->value_previous_sibling_slot());
    }
    else
    {
        aggregate->set_aggregate_last_child_slot(value_record->value_previous_sibling_slot());
    }
    aggregate->decrement_child_count();
    value_record->clear_value_attachment();

    return true;
}

bool CLiveDocument::clear_root() noexcept
{
    const TLiveNodeSlot root_slot = node_slot(m_root);
    CLiveNode* const root_value = value_node(root_slot);
    const TLiveNodeSlot root_aggregate_slot = (root_value != nullptr) ?
        root_value->value_owned_aggregate_slot() : k_invalid_live_node_slot;
    CLiveNode* const root_aggregate = node(root_aggregate_slot);
    if ((root_value == nullptr) || (root_aggregate == nullptr))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Root container pair is invalid.");
        return false;
    }

    return erase_aggregate_children(root_aggregate_slot);
}

bool CLiveDocument::move_value_payload(const TLiveNodeSlot target, const TLiveNodeSlot source) noexcept
{
    CLiveNode* const target_value = value_node(target);
    CLiveNode* const source_value = value_node(source);
    if ((target_value == nullptr) || (source_value == nullptr) || (target == source) ||
        (target_value->value_type() != ELiveValueType::empty) ||
        (source_value->value_type() == ELiveValueType::empty) ||
        !value_payload_is_in_document_domain(*target_value) ||
        !value_payload_is_in_document_domain(*source_value))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload move arguments are inconsistent.");
        return false;
    }

    const TLiveNodeSlot aggregate_slot = source_value->value_owned_aggregate_slot();
    CLiveNode* const aggregate = node(aggregate_slot);
    if (live_value_type_is_container(source_value->value_type()) &&
        ((aggregate == nullptr) ||
            !aggregate_payload_is_in_document_domain(*aggregate) ||
            !source_value->forms_container_pair_with(*aggregate, source, aggregate_slot)))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Payload source has an invalid aggregate pair.");
        return false;
    }

    target_value->move_value_payload_from(*source_value);
    if (aggregate != nullptr)
    {
        aggregate->set_aggregate_owner_value_slot(target);
    }
    return true;
}

bool CLiveDocument::erase_aggregate_children(const TLiveNodeSlot aggregate_slot) noexcept
{
    CLiveNode* aggregate = node(aggregate_slot);
    if ((aggregate == nullptr) || !aggregate_payload_is_in_document_domain(*aggregate))
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Child erase encountered an invalid aggregate.");
        return false;
    }

    TLiveNodeSlot child = aggregate->aggregate_first_child_slot();
    while (child >= 0)
    {
        const CLiveNode* const child_value = value_node(child);
        if (child_value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Child erase encountered an invalid value.");
            return false;
        }
        const TLiveNodeSlot next = child_value->value_next_sibling_slot();
        if (!erase_subtree(child))
        {
            return false;
        }
        child = next;
    }

    aggregate = node(aggregate_slot);
    if (aggregate == nullptr)
    {
        mark_integrity_bad();
        MV_ASSERT_MSG(false, "Aggregate disappeared while erasing its children.");
        return false;
    }
    aggregate->clear_aggregate_children();
    return true;
}

bool CLiveDocument::erase_subtree(const TLiveNodeSlot value) noexcept
{
    TLiveNodeSlot current = subtree_first_postorder(value);
    if ((current < 0) || m_integrity_known_bad)
    {
        return false;
    }
    const std::uint32_t traversal_limit = m_nodes.occupied_count();
    std::uint32_t erased_values = 0u;
    while (current >= 0)
    {
        const CLiveNode* const current_value = value_node(current);
        if (current_value == nullptr)
        {
            mark_integrity_bad();
            MV_ASSERT_MSG(false, "Subtree erase encountered an invalid value.");
            return false;
        }
        const TLiveNodeSlot aggregate = current_value->value_owned_aggregate_slot();
        const TLiveNodeSlot next = subtree_next_postorder(value, current);
        if (m_integrity_known_bad)
        {
            return false;
        }
        if ((aggregate >= 0) && !m_nodes.erase(aggregate))
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

bool CLiveDocument::check_string_domain(const CStableStrings& strings) const noexcept
{
    if (strings.memory_allocation_count() == 0u)
    {
        return strings.string_count() == 0u;
    }
    if (!strings.check_integrity() || (strings.string_count() >= CPropertyNameId::k_invalid_value))
    {
        return false;
    }
    for (std::size_t id = 1u; id <= strings.string_count(); ++id)
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
    m_root = source.m_root;
    m_next_monotonic_node_key = source.m_next_monotonic_node_key;
    m_integrity_known_bad = source.m_integrity_known_bad;

    source.m_root = CNodeKey{};
    source.m_next_monotonic_node_key = 1u;
    source.m_integrity_known_bad = false;
}
