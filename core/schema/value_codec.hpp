
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_codec.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Sep 26
//
//  Internal scalar decoding and bounded construction of schema-typed values.

#pragma once

#ifndef SCHEMA_VALUE_CODEC_HPP_INCLUDED
#define SCHEMA_VALUE_CODEC_HPP_INCLUDED

#include "schema/resolved_schema.hpp"

namespace schema
{

//  These words carry scalar encodings, including full-width integers and f64.
//  Callers provide a valid scalar span of at most eight bytes.
[[nodiscard]] inline std::uint64_t read_scalar_bits(const std::uint8_t* const bytes, const std::size_t size) noexcept
{
    std::uint64_t value{};
    for (std::size_t i = 0u; i < size; ++i)
    {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8u);
    }
    return value;
}

inline void write_scalar_bits(std::uint8_t* const bytes, const std::size_t size, const std::uint64_t value) noexcept
{
    for (std::size_t i = 0u; i < size; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(value >> (i * 8u));
    }
}

enum class EScalarDecodeReason : std::uint8_t { none, invalid_type, unrepresentable_value, allocation_failed };

//  Produce a document literal from a scalar encoding. Width is the storage or
//  bit-field width. Callers retain reconstruction checks for exact encoded fidelity.
[[nodiscard]] CNodeKey decode_document_scalar(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EScalarDecodeReason& reason) noexcept;

namespace detail
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

}   // namespace detail

}   // namespace schema

#endif // SCHEMA_VALUE_CODEC_HPP_INCLUDED
