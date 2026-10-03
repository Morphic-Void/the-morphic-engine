
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_codec.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Sep 26
//
//  Internal bounded construction of schema-typed payload values.

#pragma once

#ifndef SCHEMA_VALUE_CODEC_HPP_INCLUDED
#define SCHEMA_VALUE_CODEC_HPP_INCLUDED

#include "schema/resolved_schema.hpp"

namespace schema::detail
{

enum class EConstructionMode : std::uint8_t { instance, complete_bulk };

struct SValueDiagnostic
{
    EReason reason{ EReason::none };
    SOccurrence occurrence;
    CSchemaIndex type;
};

//  Scalar spelling is only shorthand for a compound with one scalar member.
//  It never recursively unwraps a compound or array member.
[[nodiscard]] bool is_scalar_shorthand(const CResolvedSchema& schema, const CSchemaIndex type,
    const SType& layout, const CDocumentRead& document, const SOccurrence value) noexcept;

//  Caller owns both the document and storage. The destination may be partially
//  changed on failure and must then be discarded. Structure/array padding is
//  untouched; a new bit-storage word is initialised as a unit.
[[nodiscard]] bool construct_value(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, std::uint8_t* const destination,
    const std::size_t destination_size, const EConstructionMode mode, SValueDiagnostic& diagnostic,
    const bool singleton_element = false) noexcept;

//  The base is preserved; the independent destination receives its complete
//  bytes before selected values are changed, preserving inherited padding and
//  unselected bits. Positive overlap of the supplied spans is rejected.
[[nodiscard]] bool construct_alternative(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const std::uint8_t* const base,
    const std::size_t base_size, std::uint8_t* const destination, const std::size_t destination_size,
    SValueDiagnostic& diagnostic) noexcept;

//  Compare encoded addressable fields only. Padding and unused bit positions
//  are ignored; NaN payloads are equivalent, while other encodings are exact.
[[nodiscard]] bool compare_encoded(const CResolvedSchema& schema, const CSchemaIndex type,
    const std::uint8_t* const expected, const std::size_t expected_size,
    const std::uint8_t* const actual, const std::size_t actual_size) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_VALUE_CODEC_HPP_INCLUDED
