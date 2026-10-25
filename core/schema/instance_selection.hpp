
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    instance_selection.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Internal selection grammar and operations on meaningful instance values.

#pragma once

#ifndef SCHEMA_INSTANCE_SELECTION_HPP_INCLUDED
#define SCHEMA_INSTANCE_SELECTION_HPP_INCLUDED

#include "schema/value_codec.hpp"

namespace schema::detail
{

struct SSelectionView
{
    const CDocumentRead* document{};
    SOccurrence value;
    bool singleton{};
    bool all{};

    [[nodiscard]] static SSelectionView complete() noexcept { return { nullptr, {}, false, true }; }
};

//  Canonical output uses named compound objects and positional arrays. Null holes
//  and empty containers survive; scalar shorthand is expanded before stripping.
[[nodiscard]] bool validate_selection(const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView selection, SValueDiagnostic& diagnostic) noexcept;
[[nodiscard]] CNodeKey strip_selection(CLiveDocument& target, const CResolvedTypes& types,
    const CTypesIndex type, const SSelectionView literal, const CStringView& name,
    SValueDiagnostic& diagnostic) noexcept;
[[nodiscard]] CNodeKey copy_selection(CLiveDocument& target, const CResolvedTypes& types,
    const CTypesIndex type, const SSelectionView selection, const CStringView& name,
    SValueDiagnostic& diagnostic) noexcept;
[[nodiscard]] CNodeKey export_selection(CLiveDocument& target, const CResolvedTypes& types,
    const CTypesIndex type, const SSelectionView selection, const std::uint8_t* bytes,
    const CStringView& name, SValueDiagnostic& diagnostic) noexcept;

//  Union the old selections with meaningful differences in scope, or explicitly
//  select every scope leaf. An absent result with no diagnostic means no selection.
[[nodiscard]] CNodeKey extend_selection(CLiveDocument& target, const CResolvedTypes& types,
    const CTypesIndex type, const SSelectionView old_selection, const SSelectionView scope,
    const std::uint8_t* baseline, const std::uint8_t* bytes, const bool select_scope,
    const CStringView& name, SValueDiagnostic& diagnostic) noexcept;

//  These operations never copy padding or unrelated bit positions. Callers first
//  validate masks and spans; staged destinations are discarded on failure.
[[nodiscard]] bool inherit_selection(const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView selection, const SSelectionView scope,
    const std::uint8_t* baseline, std::uint8_t* bytes, SValueDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool check_selection(const CResolvedTypes& types, const CTypesIndex type,
    const SSelectionView selection, const std::uint8_t* baseline,
    const std::uint8_t* bytes, bool& matches, SValueDiagnostic& diagnostic) noexcept;

//  Defaults are immutable for an operation. Reuse the most recently constructed
//  type across base records without retaining state beyond the current binding.
class CDefaultBaseline
{
public:
    [[nodiscard]] bool get(const CResolvedTypes& types, const CDocumentRead& document,
        const CTypesIndex type, const std::size_t extent, const std::uint8_t*& bytes,
        SValueDiagnostic& error) noexcept
    {
        if (type != m_type)
        {
            if (extent && (!m_bytes.reserve(extent, k_max_alignment) || !m_bytes.set_size(extent)))
            {
                error = { EReason::allocation_failed, {}, type };
                return false;
            }
            if (!construct_value(types, document, type, {}, m_bytes.data(), extent, EConstructionMode::instance, error)) return false;
            m_type = type;
        }
        bytes = extent ? m_bytes.data() : nullptr;
        return true;
    }

private:
    CByteBuffer m_bytes;
    CTypesIndex m_type;
};

template<class TRecords, class TType>
[[nodiscard]] static EValueCheck check_selected_records(const CResolvedTypes& types, const CDocumentRead& document,
    const TRecords& records, const std::uint8_t* payload, const TType& type_at, SValueDiagnostic& error) noexcept
{
    CDefaultBaseline defaults;
    for (std::size_t i = 0u; i < records.size(); ++i)
    {
        const auto& record = records[i];
        const auto type = type_at(i);
        SType layout;
        if (!types.type(type, layout) || layout.size != record.extent || (record.offset & (layout.alignment - 1u)) ||
            (record.parent != UINT32_MAX && record.parent >= i))
        {
            error = { EReason::invalid_layout, SOccurrence{ record.entry }, type };
            return EValueCheck::invalid;
        }
        const std::uint8_t* baseline{};
        if (record.parent == UINT32_MAX)
        {
            if (!defaults.get(types, document, type, record.extent, baseline, error))
            {
                error.occurrence = SOccurrence{ record.entry };
                return EValueCheck::invalid;
            }
        }
        else baseline = record.extent ? payload + records[record.parent].offset : nullptr;
        bool matches{};
        const bool valid = check_selection(types, type, { &document, SOccurrence{ record.declaration } }, baseline,
            record.extent ? payload + record.offset : nullptr, matches, error);
        if (!valid || !matches)
        {
            if (!error.occurrence) error.occurrence = SOccurrence{ record.entry };
            return valid ? EValueCheck::mismatch : EValueCheck::invalid;
        }
    }
    return EValueCheck::match;
}

//  Rebind an already copied payload in parent-before-child order. Every selected
//  encoding and each destination's padding remain untouched.
template<class TRecords, class TType>
[[nodiscard]] static bool rebuild_inherited_records(const CResolvedTypes& types, const CDocumentRead& document,
    const TRecords& records, std::uint8_t* payload, const std::size_t payload_size, const TType& type_at,
    SValueDiagnostic& error) noexcept
{
    CDefaultBaseline defaults;
    for (std::size_t i = 0u; i < records.size(); ++i)
    {
        const auto& record = records[i];
        const auto type = type_at(i);
        SType layout;
        if (!types.type(type, layout) || layout.size != record.extent || (record.offset & (layout.alignment - 1u)) ||
            record.offset > payload_size || record.extent > payload_size - record.offset ||
            (record.parent != UINT32_MAX && record.parent >= i))
        {
            error = { EReason::invalid_layout, SOccurrence{ record.entry }, type };
            return false;
        }
        const SSelectionView mask{ &document, SOccurrence{ record.declaration } };
        if (!validate_selection(types, type, mask, error)) return false;
        const std::uint8_t* baseline{};
        if (record.parent == UINT32_MAX)
        {
            if (!defaults.get(types, document, type, record.extent, baseline, error))
            {
                error.occurrence = SOccurrence{ record.entry };
                return false;
            }
        }
        else baseline = record.extent ? payload + records[record.parent].offset : nullptr;
        if (!inherit_selection(types, type, mask, SSelectionView::complete(), baseline,
            record.extent ? payload + record.offset : nullptr, error)) return false;
    }
    return true;
}

}   // namespace schema::detail

#endif // SCHEMA_INSTANCE_SELECTION_HPP_INCLUDED
