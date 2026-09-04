
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

#include "containers/StringBuffers.hpp"
#include "containers/TPodOrderedSlots.hpp"
#include "containers/TPodVector.hpp"
#include "data_model/data_model_types.hpp"
#include "data_model/live_document_node.hpp"

struct SLiveDocumentTestAccess;

class CLiveDocument
{
public:

    //  Lifetime and ownership
    CLiveDocument() noexcept = default;
    CLiveDocument(const CLiveDocument&) = delete;
    CLiveDocument& operator=(const CLiveDocument&) = delete;
    CLiveDocument(CLiveDocument&& source) noexcept;
    CLiveDocument& operator=(CLiveDocument&& source) noexcept;
    ~CLiveDocument() noexcept = default;

    //  Initialization and readiness
    //  A live document cannot be reattributed. Moves retain the allocation
    //  contexts already carried by its storage.
    [[nodiscard]] bool initialise(const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool reset(const std::size_t initial_node_capacity = 0u) noexcept;
    void deallocate() noexcept;

    //  Status
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_canonical() const noexcept;
    [[nodiscard]] bool is_complete() const noexcept;
    [[nodiscard]] bool check_integrity() const noexcept;

    //  Root and reachable structure
    [[nodiscard]] CNodeKey root() const noexcept;

    //  Root-reachable structure only. Detached storage is excluded.
    [[nodiscard]] std::uint32_t value_count() const noexcept;
    [[nodiscard]] std::uint32_t aggregate_payload_count() const noexcept;
    [[nodiscard]] bool contains(const CNodeKey value) const noexcept;

    //  Value classification and interned text
    [[nodiscard]] ELiveValueType value_type(const CNodeKey value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CNodeKey value) const noexcept;
    [[nodiscard]] bool is_detached(const CNodeKey value) const noexcept;

    [[nodiscard]] CPropertyNameId name_id(const CNodeKey value) const noexcept;
    [[nodiscard]] CStringView name(const CNodeKey value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CStringValueId id) const noexcept;

    //  Tree relationships
    [[nodiscard]] CNodeKey parent(const CNodeKey value) const noexcept;
    [[nodiscard]] CNodeKey previous_sibling(const CNodeKey value) const noexcept;
    [[nodiscard]] CNodeKey next_sibling(const CNodeKey value) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CNodeKey container_value) const noexcept;
    [[nodiscard]] CNodeKey first_child(const CNodeKey container_value) const noexcept;
    [[nodiscard]] CNodeKey last_child(const CNodeKey container_value) const noexcept;

    //  Typed payload access
    [[nodiscard]] bool boolean_value(const CNodeKey node, bool& value) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CNodeKey node, std::int64_t& value) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CNodeKey node, std::uint64_t& value) const noexcept;
    [[nodiscard]] bool integer_metadata(const CNodeKey node, CIntegerMetadata& metadata) const noexcept;
    [[nodiscard]] bool floating_point_value(const CNodeKey node, double& value) const noexcept;
    [[nodiscard]] CStringValueId string_value_id(const CNodeKey node) const noexcept;
    [[nodiscard]] CStringView string_value(const CNodeKey node) const noexcept;

    //  Referenced string-domain totals
    //  Distinct non-empty entries referenced from the root-reachable structure.
    [[nodiscard]] std::uint32_t referenced_property_name_count() const noexcept;
    [[nodiscard]] std::uint32_t referenced_string_value_count() const noexcept;

    //  Detached value creation
    [[nodiscard]] CNodeKey create_empty(const CStringView& name = {}) noexcept;
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

    //  Structural mutation
    [[nodiscard]] CLiveAttachmentResult append_child(
        const CNodeKey destination,
        const CNodeKey candidate,
        CNodeKey& surviving_value) noexcept;
    [[nodiscard]] CLiveAttachmentResult insert_child_before(
        const CNodeKey destination,
        const CNodeKey candidate,
        const CNodeKey before,
        CNodeKey& surviving_value) noexcept;
    [[nodiscard]] CLiveAttachmentResult insert_child_at(
        const CNodeKey destination,
        const CNodeKey candidate,
        const std::uint32_t index,
        CNodeKey& surviving_value) noexcept;

    [[nodiscard]] bool detach(const CNodeKey value) noexcept;

    //  Payload extraction and replacement. Extraction preserves the source
    //  key as the detached anonymous payload and installs a newly allocated
    //  empty value in its former topology. Attachment performs the inverse,
    //  preserving the payload key and consuming the empty target.
    [[nodiscard]] CNodeKey detach_payload(const CNodeKey source) noexcept;
    [[nodiscard]] CNodeKey attach_payload(const CNodeKey empty_target, const CNodeKey detached_payload) noexcept;

    //  Erasing the root preserves the implicit root pair and recursively
    //  erases all root-reachable content below it.
    [[nodiscard]] bool erase(const CNodeKey value) noexcept;

    //  Direct storage attribution
    //  Complete direct ownership accounting. There is intentionally no
    //  reattribution surface.
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;

private:

    //  Test access
    friend struct SLiveDocumentTestAccess;

    //  Internal operation state
    struct SPreparedString
    {
        const std::uint8_t* bytes{ nullptr };
        std::size_t size{ 0u };
        CByteBuffer storage;
    };

    struct SSubtreeTotals
    {
        std::uint32_t value_count{ 0u };
        std::uint32_t aggregate_count{ 0u };
        std::uint32_t recovered_aggregate_count{ 0u };
        std::uint32_t empty_value_count{ 0u };
    };

    enum class EReferenceAdjustment : std::uint8_t
    {
        add = 0u,
        remove
    };

    struct SAttachmentPosition
    {
        CNodeKey previous;
        CNodeKey next;
    };

    //  String admission, stabilization and interning
    [[nodiscard]] bool prepare_string(const CStringView& source, SPreparedString& prepared) const noexcept;
    [[nodiscard]] bool intern_string_domain(
        const SPreparedString& value,
        CStableStrings& strings,
        TPodVector<std::uint32_t>& reference_counts,
        bool& strings_ready,
        std::uint32_t& id) noexcept;

    //  Identity allocation and string interning
    [[nodiscard]] bool allocate_key(CNodeKey& key) noexcept;
    [[nodiscard]] bool intern_property_name(const SPreparedString& value, CPropertyNameId& id) noexcept;
    [[nodiscard]] bool intern_string_value(const SPreparedString& value, CStringValueId& id) noexcept;

    //  Node creation
    [[nodiscard]] CNodeKey create_scalar(
        const ELiveValueType type,
        const std::uint64_t payload_bits,
        const CIntegerMetadata metadata,
        const SPreparedString& prepared_name) noexcept;
    [[nodiscard]] CNodeKey create_empty_node(const CPropertyNameId name) noexcept;
    [[nodiscard]] CNodeKey create_container(const ELiveValueType type, const SPreparedString& prepared_name) noexcept;
    [[nodiscard]] bool insert_root_pair() noexcept;

    //  Node lookup
    [[nodiscard]] CLiveNode* node(const CNodeKey key) noexcept;
    [[nodiscard]] const CLiveNode* node(const CNodeKey key) const noexcept;
    [[nodiscard]] CLiveNode* value_node(const CNodeKey key) noexcept;
    [[nodiscard]] const CLiveNode* value_node(const CNodeKey key) const noexcept;
    [[nodiscard]] const CLiveNode* aggregate_for_value(const CNodeKey value) const noexcept;

    //  Results and document-domain validation
    [[nodiscard]] static CLiveAttachmentResult attachment_rejection(const ELiveAttachmentRejection rejection) noexcept;
    [[nodiscard]] bool value_payload_is_in_document_domain(const CLiveNode& value) const noexcept;
    [[nodiscard]] bool aggregate_payload_is_in_document_domain(const CLiveNode& aggregate) const noexcept;

    //  Trusted mutation traversal and accounting
    [[nodiscard]] CNodeKey subtree_next(const CNodeKey subtree_root, CNodeKey current) noexcept;
    [[nodiscard]] CNodeKey subtree_first_postorder(const CNodeKey subtree_root) noexcept;
    [[nodiscard]] CNodeKey subtree_next_postorder(const CNodeKey subtree_root, const CNodeKey current) noexcept;
    [[nodiscard]] bool apply_subtree_reachability(const CNodeKey subtree_root, const EReferenceAdjustment adjustment, SSubtreeTotals& totals) noexcept;
    [[nodiscard]] bool commit_reachable_totals(const SSubtreeTotals& removed, const SSubtreeTotals& added) noexcept;
    [[nodiscard]] bool adjust_property_name_reference(const CPropertyNameId id, const EReferenceAdjustment adjustment) noexcept;
    [[nodiscard]] bool adjust_string_value_reference(const CStringValueId id, const EReferenceAdjustment adjustment) noexcept;
    [[nodiscard]] bool query_ancestry(const CNodeKey value, const CNodeKey sought, bool& reachable, bool& found) noexcept;

    //  Bounded checked audit traversal
    [[nodiscard]] bool audit_subtree_checked(const CNodeKey subtree_root, SSubtreeTotals& totals) const noexcept;
    [[nodiscard]] bool count_subtree_reference_checked(
        const CNodeKey subtree_root,
        const std::uint32_t id,
        const bool string_domain,
        std::uint32_t& count) const noexcept;

    //  Structural mutation
    [[nodiscard]] CLiveAttachmentResult attach_child(
        const CNodeKey destination,
        const CNodeKey candidate,
        const SAttachmentPosition& position,
        CNodeKey& surviving_value) noexcept;
    [[nodiscard]] bool detach_value(const CNodeKey value, bool& was_reachable) noexcept;
    [[nodiscard]] bool substitute_value_position(const CNodeKey displaced, const CNodeKey replacement) noexcept;
    [[nodiscard]] bool erase_subtree(const CNodeKey value) noexcept;
    void mark_integrity_bad() noexcept;

    //  Integrity and move support
    [[nodiscard]] bool check_string_domain(
        const CStableStrings& strings,
        const TPodVector<std::uint32_t>& counts,
        const bool stable_ready) const noexcept;
    void replace_with(CLiveDocument& source) noexcept;

    //  Owned storage
    TPodOrderedSlots<CLiveNode, CNodeKey> m_nodes;
    CStableStrings m_property_names;
    CStableStrings m_string_values;
    TPodVector<std::uint32_t> m_property_name_counts;
    TPodVector<std::uint32_t> m_string_value_counts;

    //  Document state
    CNodeKey m_root;
    std::uint64_t m_next_monotonic_node_key{ 1u };
    std::uint32_t m_value_count{ 0u };
    std::uint32_t m_aggregate_payload_count{ 0u };
    std::uint32_t m_referenced_property_name_count{ 0u };
    std::uint32_t m_referenced_string_value_count{ 0u };
    std::uint32_t m_recovered_aggregate_count{ 0u };
    std::uint32_t m_empty_value_count{ 0u };
    bool m_property_names_ready{ false };
    bool m_string_values_ready{ false };
    bool m_integrity_known_bad{ false };
};

#endif // LIVE_DOCUMENT_HPP_INCLUDED
