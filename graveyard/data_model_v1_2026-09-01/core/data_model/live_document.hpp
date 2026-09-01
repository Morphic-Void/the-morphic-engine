
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_document.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Aug 26
//
//  Mutable, JSON-shaped, single-threaded document construction model.

#pragma once

#ifndef LIVE_DOCUMENT_HPP_INCLUDED
#define LIVE_DOCUMENT_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "containers/StringBuffers.hpp"
#include "containers/TPodOrderedSlots.hpp"
#include "data_model/data_model_types.hpp"

class CLiveDocument
{
public:

    //  Lifetime
    CLiveDocument() noexcept = default;
    CLiveDocument(const CLiveDocument&) = delete;
    CLiveDocument& operator=(const CLiveDocument&) = delete;
    CLiveDocument(CLiveDocument&& source) noexcept;
    CLiveDocument& operator=(CLiveDocument&& source) noexcept;

    ~CLiveDocument() noexcept = default;

    //  A document is accessed by one thread at a time. Reading is permitted
    //  only while mutation is prohibited and the document is quiescent.
    //  Moves preserve storage, keys, IDs, cursors and string views, with the
    //  destination owning the contents. References to the source object do not
    //  follow them. Assignment releases the destination's previous contents;
    //  self-move is a no-op. The empty source can be initialised again.
    //  Moves retain allocation attribution and do not permit thread transfer.

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_canonical() const noexcept;
    [[nodiscard]] bool contains_recovered_duplicate_arrays() const noexcept;
    [[nodiscard]] bool requires_morphic_json_extensions() const noexcept;

    //  Root
    [[nodiscard]] CNodeKey root() const noexcept;

    //  Value accessors
    [[nodiscard]] EJsonNodeType node_type(const CNodeKey node) const noexcept;
    [[nodiscard]] bool boolean_value(const CNodeKey node, bool& value) const noexcept;
    [[nodiscard]] bool integer_value(const CNodeKey node, std::int64_t& value) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CNodeKey node, std::uint64_t& value) const noexcept;
    [[nodiscard]] bool integer_metadata(const CNodeKey node, CJsonIntegerMetadata& metadata) const noexcept;
    [[nodiscard]] bool floating_point_value(const CNodeKey node, double& value) const noexcept;
    [[nodiscard]] CStringView string_value(const CNodeKey node) const noexcept;
    [[nodiscard]] CStringView string_value(const CStringValueId value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId name) const noexcept;

    //  Relationship accessors
    [[nodiscard]] CNodeKey parent(const CNodeKey node) const noexcept;
    [[nodiscard]] CPropertyNameId name_in_parent(const CNodeKey node) const noexcept;
    [[nodiscard]] CNodeKey previous_sibling(const CNodeKey node) const noexcept;
    [[nodiscard]] CNodeKey next_sibling(const CNodeKey node) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CNodeKey container) const noexcept;
    [[nodiscard]] CNodeKey first_child(const CNodeKey container) const noexcept;
    [[nodiscard]] CNodeKey last_child(const CNodeKey container) const noexcept;
    [[nodiscard]] CNodeKey object_child(const CNodeKey object, const CPropertyNameId name) const noexcept;
    [[nodiscard]] CNodeKey object_child(const CNodeKey object, const CStringView& name) const noexcept;
    [[nodiscard]] CNodeKey array_at(const CNodeKey array, const std::uint32_t index) const noexcept;
    [[nodiscard]] bool array_cursor_at(const CNodeKey array, const std::uint32_t index, CArrayCursor& cursor) const noexcept;
    [[nodiscard]] bool array_cursor_next(CArrayCursor& cursor) const noexcept;

    //  Content management
    [[nodiscard]] bool set_root(const CNodeKey node) noexcept;
    [[nodiscard]] CNodeKey create_null() noexcept;
    [[nodiscard]] CNodeKey create_boolean(const bool value) noexcept;
    [[nodiscard]] CNodeKey create_integer(const std::int64_t value) noexcept;
    [[nodiscard]] CNodeKey create_integer(const std::int64_t value, const CJsonIntegerMetadata& metadata) noexcept;
    [[nodiscard]] CNodeKey create_unsigned_integer(const std::uint64_t value, const CJsonIntegerMetadata& metadata) noexcept;
    [[nodiscard]] CNodeKey create_floating_point(const double value) noexcept;
    [[nodiscard]] CNodeKey create_string(const CStringView& value) noexcept;
    [[nodiscard]] CNodeKey create_array() noexcept;
    [[nodiscard]] CNodeKey create_object() noexcept;
    [[nodiscard]] CPropertyNameId intern_property_name(const CStringView& name) noexcept;
    [[nodiscard]] bool append_array_child(const CNodeKey array, const CNodeKey child) noexcept;
    [[nodiscard]] bool insert_array_child_before(const CNodeKey array, const CNodeKey before, const CNodeKey child) noexcept;
    [[nodiscard]] bool add_object_child(const CNodeKey object, const CStringView& name, const CNodeKey child) noexcept;
    //  Append all donor children, preserving their keys, names and subtrees.
    //  Both nodes must belong to this document and be objects, or array kinds.
    //  Self-transfer, cycles, name collisions and count overflow are rejected
    //  without mutation. No allocation is needed. An empty donor is a no-op;
    //  otherwise both child-list cursors are invalidated and the donor is empty.
    [[nodiscard]] bool transfer_children(const CNodeKey donor, const CNodeKey recipient) noexcept;
    [[nodiscard]] bool detach(const CNodeKey child) noexcept;
    [[nodiscard]] bool erase_detached(const CNodeKey node) noexcept;

    //  Recovery-node construction; populate through the ordinary array APIs.
    [[nodiscard]] CNodeKey create_duplicate_array() noexcept;

    //  Initialisation and promotion
    [[nodiscard]] bool initialise(const std::size_t initial_slot_count = 0u) noexcept;

    //  The destination is changed only after a complete successful promotion.
    [[nodiscard]] bool build_from(const CBakedDocument& source) noexcept;

    //  Deallocate
    void deallocate() noexcept;

    //  Integrity checking
    [[nodiscard]] bool check_integrity() const noexcept;

private:
    [[nodiscard]] static bool is_array_type(const EJsonNodeType type) noexcept;
    [[nodiscard]] static bool is_container_type(const EJsonNodeType type) noexcept;
    [[nodiscard]] static bool check_stable_strings(const CStableStrings& strings) noexcept;
    [[nodiscard]] CNodeKey create_node(const EJsonNodeType type) noexcept;
    [[nodiscard]] CJsonSlot* node_slot(const CNodeKey node) noexcept;
    [[nodiscard]] const CJsonSlot* node_slot(const CNodeKey node) const noexcept;
    [[nodiscard]] bool can_attach(const CNodeKey parent, const CNodeKey child) const noexcept;
    [[nodiscard]] bool attach_before(
        const CNodeKey parent,
        const CNodeKey before,
        const CNodeKey child,
        const CPropertyNameId name) noexcept;
    [[nodiscard]] CNodeKey append_from_baked(const CBakedDocument& source, const CBakedNodeIndex source_node) noexcept;
    [[nodiscard]] bool object_has_name(const CNodeKey object, const CPropertyNameId name) const noexcept;
    [[nodiscard]] bool check_container_integrity(const CJsonSlot& container) const noexcept;
    void replace_with(CLiveDocument& source) noexcept;

    TPodOrderedSlots<CJsonSlot, CNodeKey> m_nodes;
    CStableStrings m_property_names;
    CStableStrings m_string_values;
    CNodeKey m_root;
    std::uint64_t m_next_node_key{ 1u };
};

#endif  //  LIVE_DOCUMENT_HPP_INCLUDED
