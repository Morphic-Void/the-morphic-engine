
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    instance_selection.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Selected paths are independent of the values held in runtime binary storage.

#include "schema/instance_selection.hpp"
#include "data_model/document_copy.hpp"

#include <algorithm>
#include <cstring>

namespace schema::detail
{

using document_translation::detail::CDocumentConstruction;

[[nodiscard]] static bool fail(SValueDiagnostic& error, const EReason reason,
    const CTypesIndex type, const SSelectionView input) noexcept
{
    if (error.reason == EReason::none) error = { reason, input.value, type };
    return false;
}

[[nodiscard]] static bool present(const SSelectionView input) noexcept
{
    return input.all || input.value.is_valid();
}

[[nodiscard]] static bool marker(const SSelectionView input) noexcept
{
    bool value{};
    return input.all || (input.document && input.document->boolean_value(input.value, value) && value);
}

[[nodiscard]] static const std::uint8_t* at(const std::uint8_t* bytes, const std::uint64_t offset) noexcept
{
    return bytes ? bytes + offset : nullptr;
}

class CChildren
{
public:
    [[nodiscard]] bool prepare(const CResolvedTypes& types, const CTypesIndex type, const SType& layout,
        const SSelectionView input, const bool literal, SValueDiagnostic& error) noexcept;
    [[nodiscard]] SSelectionView child(const std::uint32_t ordinal) noexcept;
    [[nodiscard]] std::uint32_t count() const noexcept { return m_count; }

private:
    const CResolvedTypes* m_types{};
    CTypesIndex m_type;
    SType m_layout;
    SSelectionView m_input;
    CObjectSelection m_named;
    SOccurrence m_cursor;
    CTypesIndex m_singleton;
    std::uint32_t m_count{};
    bool m_positional{}, m_shorthand{};
};

bool CChildren::prepare(const CResolvedTypes& types, const CTypesIndex type, const SType& layout,
    const SSelectionView input, const bool literal, SValueDiagnostic& error) noexcept
{
    m_types = &types;
    m_type = type;
    m_layout = layout;
    m_input = input;
    if (input.all)
    {
        m_count = layout.count;
        return true;
    }
    if (!input.value) return true;
    if (!input.document || !input.document->contains(input.value)) return fail(error, EReason::invalid_input, type, input);
    const auto& document = *input.document;
    if (input.singleton)
    {
        m_singleton = (layout.category == ECategory::structure) ? types.find_member(type, document.name(input.value)) :
            ((layout.category == ECategory::bit_structure) ? types.find_field(type, document.name(input.value)) : CTypesIndex{});
        if (!m_singleton) return fail(error, EReason::invalid_input, type, input);
        m_count = layout.count;
        return true;
    }
    const auto kind = document.value_kind(input.value);
    if (literal && is_scalar_shorthand(types, type, layout, document, input.value))
    {
        m_shorthand = true;
        m_count = 1u;
        return true;
    }
    m_positional = kind == EDocumentValueKind::array;
    if (m_positional)
    {
        m_count = document.child_count(input.value);
        if (m_count > layout.count) return fail(error, EReason::invalid_range, type, input);
        m_cursor = document.first_child(input.value);
        return true;
    }
    if ((kind != EDocumentValueKind::object) || (layout.category == ECategory::array))
    {
        return fail(error, EReason::invalid_input, type, input);
    }
    m_count = layout.count;
    return m_named.prepare(types, document, type, layout.category, input.value, error);
}

SSelectionView CChildren::child(const std::uint32_t ordinal) noexcept
{
    if (m_input.all) return SSelectionView::complete();
    if (!m_input.value || ordinal >= m_count) return {};
    const CDocumentRead& document = *m_input.document;
    if (m_positional)
    {
        const SOccurrence value = m_cursor;
        m_cursor = document.next_sibling(m_cursor);
        if (inherited_hole(document, value)) return {};
        return { &document, value, document.is_object_entry(value), false };
    }
    if (m_shorthand) return { &document, m_input.value };
    const CTypesIndex component = (m_layout.category == ECategory::structure) ?
        m_types->member_at(m_type, ordinal) : m_types->field_at(m_type, ordinal);
    return { &document, m_input.singleton ? ((component == m_singleton) ? m_input.value : SOccurrence{}) : m_named.child(component) };
}

enum class EBuild : std::uint8_t { validate = 0u, strip, copy, export_values, extend };

class CSelectionBuilder
{
public:
    CSelectionBuilder(CLiveDocument* target, const CResolvedTypes& types, const EBuild mode,
        SValueDiagnostic& error, const bool select_scope = false) noexcept :
        m_target(target), m_types(types), m_mode(mode), m_error(error), m_select_scope(select_scope) {}

    [[nodiscard]] CNodeKey build(const CTypesIndex type, const SSelectionView input, const SSelectionView scope,
        const std::uint8_t* baseline, const std::uint8_t* bytes, const CStringView& name, const unsigned depth = 0u) noexcept;

private:
    [[nodiscard]] CNodeKey leaf(const CTypesIndex type, const SSelectionView input, const SSelectionView scope,
        const std::uint8_t* baseline, const std::uint8_t* bytes, const CStringView& name,
        const unsigned width, const SField* field = nullptr, const std::size_t word_size = 0u) noexcept;
    CLiveDocument* m_target;
    const CResolvedTypes& m_types;
    EBuild m_mode;
    SValueDiagnostic& m_error;
    bool m_select_scope;
};

CNodeKey CSelectionBuilder::leaf(const CTypesIndex type, const SSelectionView input, const SSelectionView scope,
    const std::uint8_t* baseline, const std::uint8_t* bytes, const CStringView& name,
    const unsigned width, const SField* field, const std::size_t word_size) noexcept
{
    if (input.singleton || (present(input) && (m_mode != EBuild::strip) && !marker(input)))
    {
        (void)fail(m_error, EReason::invalid_input, type, input);
        return {};
    }
    if (scope.singleton || (present(scope) && !marker(scope)))
    {
        (void)fail(m_error, EReason::invalid_input, type, scope);
        return {};
    }
    bool selected = present(input);
    if ((m_mode == EBuild::extend) && present(scope))
    {
        if (m_select_scope) selected = true;
        else if (field)
        {
            selected |= ((read_scalar_bits(baseline, word_size) ^ read_scalar_bits(bytes, word_size)) & field->mask) != 0u;
        }
        else selected |= !compare_encoded(m_types, type, baseline, width / 8u, bytes, width / 8u);
    }
    if (!selected || m_mode == EBuild::validate) return {};
    CNodeKey result;
    if (m_mode == EBuild::export_values)
    {
        EScalarDecodeReason error;
        const std::uint64_t bits = field ? ((read_scalar_bits(bytes, word_size) & field->mask) >> field->shift) :
            read_scalar_bits(bytes, width / 8u);
        result = decode_document_scalar(*m_target, m_types, type, bits, width, name, error);
        if (!result)
        {
            (void)fail(m_error, (error == EScalarDecodeReason::allocation_failed) ? EReason::allocation_failed : EReason::invalid_input, type, input);
            return {};
        }
    }
    else result = m_target->create_boolean(true, name);
    if (!result) (void)fail(m_error, EReason::allocation_failed, type, input);
    return result;
}

CNodeKey CSelectionBuilder::build(const CTypesIndex type, const SSelectionView input, const SSelectionView scope,
    const std::uint8_t* baseline, const std::uint8_t* bytes, const CStringView& name, const unsigned depth) noexcept
{
    if (!present(input) && ((m_mode != EBuild::extend) || !present(scope))) return {};
    SType layout;
    if ((depth >= document_limits::k_max_recursion_depth) || !m_types.type(type, layout))
    {
        (void)fail(m_error, (depth >= document_limits::k_max_recursion_depth) ? EReason::storage_limit : EReason::unknown_type, type, input);
        return {};
    }
    if (layout.category == ECategory::primitive || layout.category == ECategory::enumeration)
    {
        return leaf(type, input, scope, baseline, bytes, name, static_cast<unsigned>(layout.size * 8u));
    }
    if (layout.category != ECategory::structure && layout.category != ECategory::bit_structure && layout.category != ECategory::array)
    {
        (void)fail(m_error, EReason::unknown_type, type, input);
        return {};
    }
    CChildren old_children, scope_children;
    if (!old_children.prepare(m_types, type, layout, input, m_mode == EBuild::strip, m_error) ||
        !scope_children.prepare(m_types, type, layout, scope, false, m_error)) return {};
    std::uint32_t count = old_children.count();
    if (input.all && layout.category == ECategory::array)
    {
        SType element;
        if (m_types.type(layout.element_or_storage, element) && !element.size) count = 0u;
    }
    if (m_mode == EBuild::extend)
    {
        SType element;
        const bool padding_only_array = (layout.category == ECategory::array) &&
            m_types.type(layout.element_or_storage, element) && element.size == 0u;
        if (!padding_only_array || !scope.all) count = std::max(count, scope_children.count());
    }
    const bool array = layout.category == ECategory::array;
    CNodeKey result;
    if (m_mode != EBuild::validate)
    {
        result = array ? m_target->create_array(name) : m_target->create_object(name);
        if (!result) { (void)fail(m_error, EReason::allocation_failed, type, input); return {}; }
    }
    std::uint32_t appended{};
    for (std::uint32_t i = 0u; i < count && m_error.reason == EReason::none; ++i)
    {
        const auto old_child = old_children.child(i), scope_child = scope_children.child(i);
        CNodeKey child;
        if (layout.category == ECategory::bit_structure)
        {
            SField field;
            (void)m_types.field(m_types.field_at(type, i), field);
            child = leaf(field.type, old_child, scope_child, baseline, bytes, m_types.name(field.name),
                field.width, &field, static_cast<std::size_t>(layout.size));
        }
        else if (array)
        {
            child = build(layout.element_or_storage, old_child, scope_child, at(baseline, layout.stride * i),
                at(bytes, layout.stride * i), {}, depth + 1u);
        }
        else
        {
            SMember member;
            (void)m_types.member(m_types.member_at(type, i), member);
            child = build(member.type, old_child, scope_child, at(baseline, member.offset), at(bytes, member.offset),
                m_types.name(member.name), depth + 1u);
        }
        if (m_mode == EBuild::validate) continue;
        if (child)
        {
            while (array && appended < i)
            {
                const auto hole = m_target->create_null();
                if (!hole || !CDocumentConstruction::append(*m_target, result, hole))
                {
                    if (hole) (void)m_target->erase(hole);
                    (void)fail(m_error, EReason::allocation_failed, type, input);
                    break;
                }
                ++appended;
            }
            if (m_error.reason != EReason::none || !CDocumentConstruction::append(*m_target, result, child))
            {
                (void)m_target->erase(child);
                (void)fail(m_error, EReason::allocation_failed, type, input);
            }
            ++appended;
        }
    }
    if (result && (m_error.reason != EReason::none ||
        (!present(input) && !m_target->child_count(result) && !(m_select_scope && scope.value))))
    {
        (void)m_target->erase(result);
        result = {};
    }
    return result;
}

[[nodiscard]] static bool transfer(const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView selection, const SSelectionView scope, const std::uint8_t* baseline,
    const std::uint8_t* bytes, std::uint8_t* writable, bool& matches, SValueDiagnostic& error, const unsigned depth) noexcept
{
    if (!present(scope)) return true;
    SType layout;
    if (depth >= document_limits::k_max_recursion_depth) return fail(error, EReason::storage_limit, type, selection);
    if (!types.type(type, layout)) return fail(error, EReason::unknown_type, type, selection);
    if (!layout.size) return true;
    if (!baseline || !bytes) return fail(error, EReason::invalid_input, type, selection);
    if (layout.category == ECategory::primitive || layout.category == ECategory::enumeration)
    {
        if (!present(selection))
        {
            if (writable) std::memcpy(writable, baseline, static_cast<std::size_t>(layout.size));
            else matches &= compare_encoded(types, type, baseline, static_cast<std::size_t>(layout.size), bytes, static_cast<std::size_t>(layout.size));
        }
        return true;
    }
    CChildren protected_children, scope_children;
    if (!protected_children.prepare(types, type, layout, selection, false, error) ||
        !scope_children.prepare(types, type, layout, scope, false, error)) return false;
    if (layout.category == ECategory::array)
    {
        SType element;
        if (!types.type(layout.element_or_storage, element)) return fail(error, EReason::unknown_type, type, selection);
        if (!element.size) return true;
    }
    for (std::uint32_t i = 0u; i < scope_children.count(); ++i)
    {
        const auto child = protected_children.child(i), child_scope = scope_children.child(i);
        if (layout.category == ECategory::bit_structure)
        {
            if (!present(child_scope) || present(child)) continue;
            SField field;
            (void)types.field(types.field_at(type, i), field);
            const auto expected = read_scalar_bits(baseline, static_cast<std::size_t>(layout.size));
            const auto actual = read_scalar_bits(bytes, static_cast<std::size_t>(layout.size));
            if (writable) write_scalar_bits(writable, static_cast<std::size_t>(layout.size), (actual & ~field.mask) | (expected & field.mask));
            else matches &= ((expected ^ actual) & field.mask) == 0u;
        }
        else
        {
            CTypesIndex child_type;
            std::uint64_t offset{};
            if (layout.category == ECategory::structure)
            {
                SMember member;
                (void)types.member(types.member_at(type, i), member);
                child_type = member.type;
                offset = member.offset;
            }
            else if (layout.category == ECategory::array)
            {
                child_type = layout.element_or_storage;
                offset = layout.stride * i;
            }
            else return fail(error, EReason::unknown_type, type, selection);
            if (!transfer(types, child_type, child, child_scope, at(baseline, offset), at(bytes, offset),
                writable ? writable + offset : nullptr, matches, error, depth + 1u)) return false;
        }
    }
    return true;
}

bool validate_selection(const CResolvedTypes& types, const CTypesIndex type, const SSelectionView selection,
    SValueDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CSelectionBuilder builder{ nullptr, types, EBuild::validate, diagnostic };
    (void)builder.build(type, selection, {}, nullptr, nullptr, {});
    return diagnostic.reason == EReason::none;
}

CNodeKey strip_selection(CLiveDocument& target, const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView literal, const CStringView& name, SValueDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!literal.value) return {};
    if (!literal.document || !validate_declaration(types, *literal.document, type, literal.value,
        EConstructionMode::instance, diagnostic, literal.singleton)) return {};
    CSelectionBuilder builder{ &target, types, EBuild::strip, diagnostic };
    return builder.build(type, literal, {}, nullptr, nullptr, name);
}

CNodeKey copy_selection(CLiveDocument& target, const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView selection, const CStringView& name, SValueDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CSelectionBuilder builder{ &target, types, EBuild::copy, diagnostic };
    return builder.build(type, selection, {}, nullptr, nullptr, name);
}

CNodeKey export_selection(CLiveDocument& target, const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView selection, const std::uint8_t* bytes, const CStringView& name, SValueDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    SType layout;
    if (!types.type(type, layout) || (layout.size && !bytes))
    {
        (void)fail(diagnostic, EReason::invalid_input, type, selection);
        return {};
    }
    CSelectionBuilder builder{ &target, types, EBuild::export_values, diagnostic };
    CNodeKey result = builder.build(type, selection, {}, nullptr, bytes, name);
    if (result)
    {
        const CDocumentRead output{ target };
        bool matches{};
        if (!check_alternative(types, output, type, SOccurrence{ result }, bytes, static_cast<std::size_t>(layout.size),
            bytes, static_cast<std::size_t>(layout.size), matches, diagnostic) || !matches)
        {
            (void)target.erase(result);
            result = {};
            diagnostic = { EReason::invalid_input, selection.value, type };
        }
    }
    return result;
}

CNodeKey extend_selection(CLiveDocument& target, const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView old_selection, const SSelectionView scope, const std::uint8_t* baseline,
    const std::uint8_t* bytes, const bool select_scope, const CStringView& name, SValueDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CSelectionBuilder builder{ &target, types, EBuild::extend, diagnostic, select_scope };
    return builder.build(type, old_selection, scope, baseline, bytes, name);
}

bool inherit_selection(const CResolvedTypes& types, const CTypesIndex type, const SSelectionView selection,
    const SSelectionView scope, const std::uint8_t* baseline, std::uint8_t* bytes, SValueDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    bool matches = true;
    return transfer(types, type, selection, scope, baseline, bytes, bytes, matches, diagnostic, 0u);
}

bool check_selection(const CResolvedTypes& types, const CTypesIndex type, const SSelectionView selection,
    const std::uint8_t* baseline, const std::uint8_t* bytes, bool& matches, SValueDiagnostic& diagnostic) noexcept
{
    matches = true;
    return validate_selection(types, type, selection, diagnostic) &&
        transfer(types, type, selection, SSelectionView::complete(), baseline, bytes, nullptr, matches, diagnostic, 0u);
}

}   // namespace schema::detail
