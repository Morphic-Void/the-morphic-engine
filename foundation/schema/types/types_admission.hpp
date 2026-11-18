
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    types_admission.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Structural type admission and indivisible named-definition comparison.

#pragma once

#ifndef SCHEMA_TYPES_ADMISSION_HPP_INCLUDED
#define SCHEMA_TYPES_ADMISSION_HPP_INCLUDED

#include "schema/resolved_types.hpp"

namespace schema
{

[[nodiscard]] bool valid_identifier(const CStringView& name) noexcept;

namespace detail
{

struct STypesAdmissionDiagnostic
{
    EReason reason{ EReason::none };
    SOccurrence first, second;
    bool unestablished{};
};

enum class EDefinitionShape : std::uint8_t;

struct SAdmittedDefinition
{
    CStringView name;
    SOccurrence source;
    EDefinitionShape shape;
};

//  Borrows original declarations for one admission/merge operation only.
//  Admission requires an empty catalogue; it is consumed before publication.
struct STypesCatalogue
{
    TPodVector<SAdmittedDefinition> definitions;
};

[[nodiscard]] bool admit_types_section(const CDocumentRead& source, const SOccurrence section,
    STypesAdmissionDiagnostic& diagnostic, STypesCatalogue& catalogue) noexcept;
[[nodiscard]] bool compatible_type_sections(const CDocumentRead& first, const STypesCatalogue& first_types,
    const CDocumentRead& second, const STypesCatalogue& second_types, STypesAdmissionDiagnostic& diagnostic) noexcept;

}   // namespace detail
}   // namespace schema

#endif // SCHEMA_TYPES_ADMISSION_HPP_INCLUDED
