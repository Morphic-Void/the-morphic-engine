
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

#include "schema/resolved_schema.hpp"

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

[[nodiscard]] bool data_entry_property(const CStringView& name, const bool bulk, const bool stored) noexcept;
[[nodiscard]] bool align_data_offset(const std::uint64_t position, const std::uint64_t alignment, std::uint64_t& result) noexcept;
[[nodiscard]] bool data_extent(const SType& layout, const std::uint64_t count, std::uint64_t& extent) noexcept;

//  Strict document-only root. Instances visit supplied selections and hierarchy;
//  bulk data visits complete records. Packed extent is checked without allocating.
[[nodiscard]] bool validate_data_document(const CDocumentRead& document, const CResolvedSchema& schema,
    const bool bulk, SDataValidationDiagnostic& diagnostic) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DATA_DOCUMENT_VALIDATION_HPP_INCLUDED
