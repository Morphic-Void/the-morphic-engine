
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    types_wrappers.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Types-role document ownership, guarded editing and client bindings.

//  Public entry point for live/baked types owners and their client bindings.
//  Includes resolved observations and document queries used by these roles.

#pragma once

#ifndef TYPES_WRAPPERS_HPP_INCLUDED
#define TYPES_WRAPPERS_HPP_INCLUDED

#include "schema/resolved_types.hpp"
#include "data_model/live_document.hpp"

namespace schema
{

class CTypesBinding;
class CLiveTypes;

//  Checked replacement validates the available prior type context. Explicit
//  recovery requires that context to be lost and asserts compatible representation.
enum class ETypeBindMode : std::uint8_t { checked = 0u, asserted_recovery };

//==============================================================================
//  Internal binding state
//==============================================================================

namespace detail
{

struct STypesBindingState
{
    CTypesBinding* head{ nullptr };
    std::size_t count{};
    CResolvedTypes* resolution{ nullptr };
    CTypesDocumentQuery query;

    void invalidate() noexcept;
};

}   // namespace detail

//==============================================================================
//  Client-held schema binding
//==============================================================================

class CTypesBinding
{
public:
    CTypesBinding() noexcept = default;
    CTypesBinding(const CTypesBinding&) = delete;
    CTypesBinding& operator=(const CTypesBinding&) = delete;
    CTypesBinding(CTypesBinding&& source) noexcept;
    CTypesBinding& operator=(CTypesBinding&& source) noexcept;
    ~CTypesBinding() noexcept;

    [[nodiscard]] bool bind(class CBakedTypes& schema) noexcept;
    [[nodiscard]] bool bind(class CLiveTypes& schema) noexcept;
    [[nodiscard]] bool bind(const CTypesBinding& source) noexcept;
    void release() noexcept;

    [[nodiscard]] bool is_attached() const noexcept { return m_owner != nullptr; }
    [[nodiscard]] bool is_usable() const noexcept { return resolved() != nullptr; }
    [[nodiscard]] const CResolvedTypes* resolved() const noexcept;
    [[nodiscard]] CTypesDocumentQuery document_query() const noexcept;

private:
    friend struct detail::STypesBindingState;
    [[nodiscard]] bool bind_to(detail::STypesBindingState& target) noexcept;
    void take_link(CTypesBinding& source) noexcept;

    detail::STypesBindingState* m_owner{ nullptr };
    CTypesBinding* m_previous{ nullptr };
    CTypesBinding* m_next{ nullptr };
};

//==============================================================================
//  Baked schema view
//==============================================================================

class CBakedTypes
{
public:
    CBakedTypes() noexcept;
    CBakedTypes(const CBakedTypes&) = delete;
    CBakedTypes& operator=(const CBakedTypes&) = delete;
    CBakedTypes(CBakedTypes&&) = delete;
    CBakedTypes& operator=(CBakedTypes&&) = delete;
    ~CBakedTypes() noexcept;

    [[nodiscard]] bool set_block(const class CBakedDataBlock& block) noexcept;
    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool try_take_from(CBakedTypes&& source) noexcept;
    [[nodiscard]] bool resolve(SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool clear_resolution() noexcept;
    [[nodiscard]] bool clear() noexcept;
    [[nodiscard]] bool promote(CLiveTypes& destination, SDiagnostic& diagnostic) const noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool resolved_ready() const noexcept { return m_resolution.is_ready(); }
    [[nodiscard]] CTypesDocumentQuery document_query() const noexcept;
    [[nodiscard]] CTypesHandle types_root() const noexcept;
    [[nodiscard]] const CResolvedTypes* resolved() const noexcept;
    [[nodiscard]] std::size_t reference_count() const noexcept { return m_bindings.count; }

private:
    friend class CTypesBinding;
    [[nodiscard]] bool can_replace() const noexcept;

    CBakedDocument m_document;
    CResolvedTypes m_resolution;
    detail::STypesBindingState m_bindings;
};

//==============================================================================
//  Live schema owner and guarded editor
//==============================================================================

class CLiveTypes
{
public:
    class CEditor
    {
    public:
        [[nodiscard]] CTypesHandle append_empty(const CTypesHandle parent, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_null(const CTypesHandle parent, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_boolean(const CTypesHandle parent, const bool value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_signed(const CTypesHandle parent, const std::int64_t value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_unsigned(const CTypesHandle parent, const std::uint64_t value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_floating(const CTypesHandle parent, const double value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_string(const CTypesHandle parent, const CStringView& value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_array(const CTypesHandle parent, const CStringView& name = {}) noexcept;
        [[nodiscard]] CTypesHandle append_object(const CTypesHandle parent, const CStringView& name = {}) noexcept;

        [[nodiscard]] bool set_name(const CTypesHandle target, const CStringView& name) noexcept;
        [[nodiscard]] bool set_newline_escaping_suppressed(const CTypesHandle target, const bool suppressed) noexcept;
        [[nodiscard]] bool erase(const CTypesHandle target) noexcept;
        [[nodiscard]] bool replace_null(const CTypesHandle target) noexcept;
        [[nodiscard]] bool replace_boolean(const CTypesHandle target, const bool value) noexcept;
        [[nodiscard]] bool replace_signed(const CTypesHandle target, const std::int64_t value) noexcept;
        [[nodiscard]] bool replace_unsigned(const CTypesHandle target, const std::uint64_t value) noexcept;
        [[nodiscard]] bool replace_floating(const CTypesHandle target, const double value) noexcept;
        [[nodiscard]] bool replace_string(const CTypesHandle target, const CStringView& value) noexcept;
        [[nodiscard]] bool replace_array(const CTypesHandle target) noexcept;
        [[nodiscard]] bool replace_object(const CTypesHandle target) noexcept;

    private:
        friend class CLiveTypes;
        explicit CEditor(CLiveTypes& owner) noexcept : m_owner(&owner) {}
        CLiveTypes* m_owner;
    };

    CLiveTypes() noexcept;
    CLiveTypes(const CLiveTypes&) = delete;
    CLiveTypes& operator=(const CLiveTypes&) = delete;
    CLiveTypes(CLiveTypes&&) = delete;
    CLiveTypes& operator=(CLiveTypes&&) = delete;
    ~CLiveTypes() noexcept;

    [[nodiscard]] bool initialise(const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool reset(const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool try_adopt(CLiveDocument&& document) noexcept;
    [[nodiscard]] bool try_take_from(CLiveTypes&& source) noexcept;
    [[nodiscard]] bool resolve(SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool clear_resolution() noexcept;
    [[nodiscard]] bool clear() noexcept;
    [[nodiscard]] bool bake(CBakedDocumentBlock& destination_block, CBakedTypes& destination_schema) const noexcept;
    [[nodiscard]] bool bake(class CBakedDataBlock& destination_block, CBakedTypes& destination_schema) const noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool resolved_ready() const noexcept { return m_resolution.is_ready(); }
    [[nodiscard]] CTypesDocumentQuery document_query() const noexcept;
    [[nodiscard]] CTypesHandle types_root() const noexcept;
    [[nodiscard]] const CResolvedTypes* resolved() const noexcept;
    [[nodiscard]] std::size_t reference_count() const noexcept { return m_bindings.count; }
    [[nodiscard]] CEditor edit() noexcept { return CEditor{ *this }; }

private:
    friend class CTypesBinding;
    friend class CBakedTypes;

    struct SEditValue
    {
        ELiveValueType type{ ELiveValueType::empty };
        CStringView name, text;
        std::int64_t signed_value{};
        std::uint64_t unsigned_value{};
        double floating_value{};
        bool unsigned_integer{};
    };

    [[nodiscard]] bool can_replace() const noexcept;
    [[nodiscard]] bool can_edit() const noexcept;
    [[nodiscard]] bool document_empty() const noexcept;
    [[nodiscard]] bool in_types(const CTypesHandle target, const bool include_root) const noexcept;
    [[nodiscard]] bool append_parent(const CTypesHandle parent, const SEditValue& value) const noexcept;
    [[nodiscard]] CNodeKey create_value(const SEditValue& value) noexcept;
    [[nodiscard]] CTypesHandle append(const CTypesHandle parent, const SEditValue& value) noexcept;
    [[nodiscard]] bool replace(const CTypesHandle target, const SEditValue& value) noexcept;
    [[nodiscard]] bool set_name(const CTypesHandle target, const CStringView& name) noexcept;
    [[nodiscard]] bool set_newline_escaping_suppressed(const CTypesHandle target, const bool suppressed) noexcept;
    [[nodiscard]] bool erase(const CTypesHandle target) noexcept;

    CLiveDocument m_document;
    CResolvedTypes m_resolution;
    detail::STypesBindingState m_bindings;
};

}   // namespace schema

#endif // TYPES_WRAPPERS_HPP_INCLUDED
