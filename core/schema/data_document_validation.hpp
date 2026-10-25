
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_document_validation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Prepared data admission without payload construction.

#pragma once

#ifndef SCHEMA_DATA_DOCUMENT_VALIDATION_HPP_INCLUDED
#define SCHEMA_DATA_DOCUMENT_VALIDATION_HPP_INCLUDED

#include "schema/resolved_types.hpp"

#include <type_traits>

namespace schema
{
enum class EInstanceLoadReason : std::uint8_t;
}

namespace schema::detail
{

enum class EDataValidationReason : std::uint8_t
{
    none = 0u,
    invalid_input,
    missing_property,
    unknown_property,
    unknown_type,
    invalid_count,
    invalid_value,
    allocation_failed,
    storage_limit
};

struct SDataValidationDiagnostic
{
    EDataValidationReason reason{ EDataValidationReason::none };
    SOccurrence occurrence;
};

struct SDataConstructionEntry
{
    SOccurrence source, declaration, children;
    std::uint32_t parent{ UINT32_MAX };
    std::uint32_t count{}, offset{}, extent{};
    //  Filled only while building a private stripped description.
    CNodeKey target, selection, specialisations;
};

struct SDataConstructionGroup
{
    SOccurrence source;
    CTypesIndex type;
    std::uint32_t first{}, count{};
};

struct SDataConstructionPlan
{
    TPodVector<SDataConstructionGroup> groups;
    TPodVector<SDataConstructionEntry> entries;
    std::size_t payload_size{};
};

//  Domain diagnostics share entry admission reasons, with a distinct name for
//  invalid supplied values. Keep typed handles at this public boundary only.
template<class TReason>
[[nodiscard]] static TReason data_validation_reason(const EDataValidationReason reason) noexcept
{
    switch (reason)
    {
        case EDataValidationReason::missing_property:
        {
            return TReason::missing_property;
        }
        case EDataValidationReason::unknown_property:
        {
            return TReason::unknown_property;
        }
        case EDataValidationReason::unknown_type:
        {
            return TReason::unknown_type;
        }
        case EDataValidationReason::invalid_count:
        {
            return TReason::invalid_count;
        }
        case EDataValidationReason::allocation_failed:
        {
            return TReason::allocation_failed;
        }
        case EDataValidationReason::storage_limit:
        {
            return TReason::invalid_range;
        }
        case EDataValidationReason::invalid_value:
        {
            if constexpr (std::is_same_v<TReason, EInstanceLoadReason>)
            {
                return TReason::invalid_declaration;
            }
            else
            {
                return TReason::incomplete_record;
            }
        }
        default:
        {
            return TReason::invalid_input;
        }
    }
}

[[nodiscard]] bool data_entry_property(const CStringView& name, const bool bulk, const bool stored) noexcept;
[[nodiscard]] bool align_data_offset(const std::uint64_t position, const std::uint64_t alignment, std::uint64_t& result) noexcept;
[[nodiscard]] bool data_extent(const SType& layout, const std::uint64_t count, std::uint64_t& extent) noexcept;

//  Aggregate validation supplies its already resolved contained types and reads
//  the original section directly, preserving source diagnostic occurrences.
[[nodiscard]] bool validate_data_section(const CDocumentRead& document, const SOccurrence section,
    const CResolvedTypes& types, const bool bulk, SDataValidationDiagnostic& diagnostic,
    SDataConstructionPlan* plan = nullptr) noexcept;

//  Admit containers and known entry properties without resolving types or
//  requiring complete record values. Missing sections remain absent.
[[nodiscard]] bool admit_data_section(const CDocumentRead& document, const SOccurrence section,
    const bool bulk, SDataValidationDiagnostic& diagnostic) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DATA_DOCUMENT_VALIDATION_HPP_INCLUDED
