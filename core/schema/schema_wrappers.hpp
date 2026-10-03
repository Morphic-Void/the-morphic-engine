
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_wrappers.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Schema-role document ownership, guarded editing and client bindings.

#pragma once

#ifndef SCHEMA_WRAPPERS_HPP_INCLUDED
#define SCHEMA_WRAPPERS_HPP_INCLUDED

#include "schema/resolved_schema.hpp"
#include "data_model/live_document.hpp"

namespace schema
{

class CSchemaBinding;
class CLiveSchema;

namespace detail
{

struct SSchemaBindingState
{
    CSchemaBinding* head{ nullptr };
    std::size_t count{};
    CResolvedSchema* resolution{ nullptr };
    CSchemaDocumentQuery query;

    void invalidate() noexcept;
};

}   // namespace detail

//==============================================================================
//  Client-held schema binding
//==============================================================================

class CSchemaBinding
{
public:
    CSchemaBinding() noexcept = default;
    CSchemaBinding(const CSchemaBinding&) = delete;
    CSchemaBinding& operator=(const CSchemaBinding&) = delete;
    CSchemaBinding(CSchemaBinding&& source) noexcept;
    CSchemaBinding& operator=(CSchemaBinding&& source) noexcept;
    ~CSchemaBinding() noexcept;

    [[nodiscard]] bool bind(class CBakedSchema& schema) noexcept;
    [[nodiscard]] bool bind(class CLiveSchema& schema) noexcept;
    void release() noexcept;

    [[nodiscard]] bool is_attached() const noexcept { return m_owner != nullptr; }
    [[nodiscard]] bool is_usable() const noexcept { return resolved() != nullptr; }
    [[nodiscard]] const CResolvedSchema* resolved() const noexcept;
    [[nodiscard]] CSchemaDocumentQuery document_query() const noexcept;

private:
    friend struct detail::SSchemaBindingState;
    [[nodiscard]] bool bind_to(detail::SSchemaBindingState& target) noexcept;
    void take_link(CSchemaBinding& source) noexcept;

    detail::SSchemaBindingState* m_owner{ nullptr };
    CSchemaBinding* m_previous{ nullptr };
    CSchemaBinding* m_next{ nullptr };
};

//==============================================================================
//  Baked schema view
//==============================================================================

class CBakedSchema
{
public:
    CBakedSchema() noexcept;
    CBakedSchema(const CBakedSchema&) = delete;
    CBakedSchema& operator=(const CBakedSchema&) = delete;
    CBakedSchema(CBakedSchema&&) = delete;
    CBakedSchema& operator=(CBakedSchema&&) = delete;
    ~CBakedSchema() noexcept;

    [[nodiscard]] bool set_document(const CBakedDocument& document) noexcept;
    [[nodiscard]] bool try_take_from(CBakedSchema&& source) noexcept;
    [[nodiscard]] bool resolve(SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool clear_resolution() noexcept;
    [[nodiscard]] bool clear() noexcept;
    [[nodiscard]] bool promote(CLiveSchema& destination, SDiagnostic& diagnostic) const noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool resolved_ready() const noexcept { return m_resolution.is_ready(); }
    [[nodiscard]] CSchemaDocumentQuery document_query() const noexcept;
    [[nodiscard]] CSchemaHandle types_root() const noexcept;
    [[nodiscard]] const CResolvedSchema* resolved() const noexcept;
    [[nodiscard]] std::size_t reference_count() const noexcept { return m_bindings.count; }

private:
    friend class CSchemaBinding;
    [[nodiscard]] bool can_replace() const noexcept;

    CBakedDocument m_document;
    CResolvedSchema m_resolution;
    detail::SSchemaBindingState m_bindings;
};

//==============================================================================
//  Live schema owner and guarded editor
//==============================================================================

class CLiveSchema
{
public:
    class CEditor
    {
    public:
        [[nodiscard]] CSchemaHandle append_empty(const CSchemaHandle parent, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_null(const CSchemaHandle parent, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_boolean(const CSchemaHandle parent, const bool value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_signed(const CSchemaHandle parent, const std::int64_t value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_unsigned(const CSchemaHandle parent, const std::uint64_t value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_floating(const CSchemaHandle parent, const double value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_string(const CSchemaHandle parent, const CStringView& value, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_array(const CSchemaHandle parent, const CStringView& name = {}) noexcept;
        [[nodiscard]] CSchemaHandle append_object(const CSchemaHandle parent, const CStringView& name = {}) noexcept;

        [[nodiscard]] bool set_name(const CSchemaHandle target, const CStringView& name) noexcept;
        [[nodiscard]] bool set_newline_escaping_suppressed(const CSchemaHandle target, const bool suppressed) noexcept;
        [[nodiscard]] bool erase(const CSchemaHandle target) noexcept;
        [[nodiscard]] bool replace_null(const CSchemaHandle target) noexcept;
        [[nodiscard]] bool replace_boolean(const CSchemaHandle target, const bool value) noexcept;
        [[nodiscard]] bool replace_signed(const CSchemaHandle target, const std::int64_t value) noexcept;
        [[nodiscard]] bool replace_unsigned(const CSchemaHandle target, const std::uint64_t value) noexcept;
        [[nodiscard]] bool replace_floating(const CSchemaHandle target, const double value) noexcept;
        [[nodiscard]] bool replace_string(const CSchemaHandle target, const CStringView& value) noexcept;
        [[nodiscard]] bool replace_array(const CSchemaHandle target) noexcept;
        [[nodiscard]] bool replace_object(const CSchemaHandle target) noexcept;

    private:
        friend class CLiveSchema;
        explicit CEditor(CLiveSchema& owner) noexcept : m_owner(&owner) {}
        CLiveSchema* m_owner;
    };

    CLiveSchema() noexcept;
    CLiveSchema(const CLiveSchema&) = delete;
    CLiveSchema& operator=(const CLiveSchema&) = delete;
    CLiveSchema(CLiveSchema&&) = delete;
    CLiveSchema& operator=(CLiveSchema&&) = delete;
    ~CLiveSchema() noexcept;

    [[nodiscard]] bool initialise(const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool reset(const std::size_t initial_node_capacity = 0u) noexcept;
    [[nodiscard]] bool try_adopt(CLiveDocument&& document) noexcept;
    [[nodiscard]] bool try_take_from(CLiveSchema&& source) noexcept;
    [[nodiscard]] bool resolve(SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool clear_resolution() noexcept;
    [[nodiscard]] bool clear() noexcept;
    [[nodiscard]] bool bake(CBakedDocumentBlock& destination_block, CBakedSchema& destination_schema) const noexcept;

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] bool resolved_ready() const noexcept { return m_resolution.is_ready(); }
    [[nodiscard]] CSchemaDocumentQuery document_query() const noexcept;
    [[nodiscard]] CSchemaHandle types_root() const noexcept;
    [[nodiscard]] const CResolvedSchema* resolved() const noexcept;
    [[nodiscard]] std::size_t reference_count() const noexcept { return m_bindings.count; }
    [[nodiscard]] CEditor edit() noexcept { return CEditor{ *this }; }

private:
    friend class CSchemaBinding;
    friend class CBakedSchema;

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
    [[nodiscard]] bool in_types(const CSchemaHandle target, const bool include_root) const noexcept;
    [[nodiscard]] bool append_parent(const CSchemaHandle parent, const SEditValue& value) const noexcept;
    [[nodiscard]] CNodeKey create_value(const SEditValue& value) noexcept;
    [[nodiscard]] CSchemaHandle append(const CSchemaHandle parent, const SEditValue& value) noexcept;
    [[nodiscard]] bool replace(const CSchemaHandle target, const SEditValue& value) noexcept;
    [[nodiscard]] bool set_name(const CSchemaHandle target, const CStringView& name) noexcept;
    [[nodiscard]] bool set_newline_escaping_suppressed(const CSchemaHandle target, const bool suppressed) noexcept;
    [[nodiscard]] bool erase(const CSchemaHandle target) noexcept;

    CLiveDocument m_document;
    CResolvedSchema m_resolution;
    detail::SSchemaBindingState m_bindings;
};

}   // namespace schema

#endif // SCHEMA_WRAPPERS_HPP_INCLUDED
