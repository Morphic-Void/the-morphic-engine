
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_codec.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Sep 26
//
//  Internal scalar decoding and bounded construction of schema-typed values.

//  Internal schema construction, decoding and comparison. Included by role
//  implementations; consumers should include the appropriate role header.

#pragma once

#ifndef SCHEMA_VALUE_CODEC_HPP_INCLUDED
#define SCHEMA_VALUE_CODEC_HPP_INCLUDED

#include "schema/resolved_types.hpp"

namespace schema
{

//==============================================================================
//  Scalar bit access
//==============================================================================

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

//==============================================================================
//  Scalar decoding
//==============================================================================

enum class EScalarDecodeReason : std::uint8_t { none = 0u, invalid_type, unrepresentable_value, allocation_failed };

//  Produce a document literal from a scalar encoding. Width is the storage or
//  bit-field width. Callers retain reconstruction checks for exact encoded fidelity.
[[nodiscard]] CNodeKey decode_document_scalar(CLiveDocument& document, const CResolvedTypes& schema,
    const CTypesIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EScalarDecodeReason& reason) noexcept;

namespace detail
{

//==============================================================================
//  Internal construction and diagnostics
//==============================================================================

enum class EConstructionMode : std::uint8_t { instance = 0u, complete_bulk_data };

//  Call only for a positional instance-selection child, never a named value.
[[nodiscard]] inline bool inherited_hole(const CDocumentRead& document, const SOccurrence value) noexcept
{
    return value && !document.is_object_entry(value) && (document.value_kind(value) == EDocumentValueKind::null_value);
}

struct SValueDiagnostic
{
    EReason reason{ EReason::none };
    SOccurrence occurrence;
    CTypesIndex type;
};

template<class TReason>
[[nodiscard]] static TReason value_failure(const SValueDiagnostic& diagnostic, const TReason invalid) noexcept
{
    return diagnostic.reason == EReason::allocation_failed ? TReason::allocation_failed : invalid;
}

//  Traverse named selections in schema declaration order. Already ordered
//  objects use a forward cursor without allocations; shuffled objects get an
//  operation-local sorted index. Query children in increasing declaration order;
//  the selected object and schema must remain unchanged during the traversal.
class CObjectSelection
{
public:
    [[nodiscard]] bool prepare(const CResolvedTypes& schema, const CDocumentRead& document,
        const CTypesIndex type, const ECategory category, const SOccurrence source, SValueDiagnostic& diagnostic) noexcept;
    [[nodiscard]] SOccurrence child(const CTypesIndex selected) noexcept;

private:
    struct SNamedInput { CTypesIndex selected; SOccurrence source; };
    [[nodiscard]] CTypesIndex component(const SOccurrence source) const noexcept;
    const CResolvedTypes* m_schema{};
    const CDocumentRead* m_document{};
    CTypesIndex m_type, m_selected;
    ECategory m_category{};
    SOccurrence m_cursor;
    TPodVector<SNamedInput> m_named;
    std::size_t m_next{};
};

//  Scalar spelling is only shorthand for a compound with one scalar member.
//  It never recursively unwraps a compound or array member.
[[nodiscard]] bool is_scalar_shorthand(const CResolvedTypes& schema, const CTypesIndex type,
    const SType& layout, const CDocumentRead& document, const SOccurrence value) noexcept;

//  Validate supplied declarations only. Omissions inherit/default later; no
//  binary storage, implicit subtree expansion or writer selection index is needed.
[[nodiscard]] bool validate_declaration(const CResolvedTypes& schema, const CDocumentRead& document,
    const CTypesIndex type, const SOccurrence declaration, const EConstructionMode mode,
    SValueDiagnostic& diagnostic, const bool singleton_element = false) noexcept;

//  Caller owns both the document and storage. The destination may be partially
//  changed on failure and must then be discarded. Structure/array padding is
//  untouched; a new bit-storage word is initialised as a unit.
[[nodiscard]] bool construct_value(const CResolvedTypes& schema, const CDocumentRead& document,
    const CTypesIndex type, const SOccurrence declaration, std::uint8_t* const destination,
    const std::size_t destination_size, const EConstructionMode mode, SValueDiagnostic& diagnostic,
    const bool singleton_element = false) noexcept;

//  The base is preserved; the independent destination receives its complete
//  bytes before selected values are changed, preserving inherited padding and
//  unselected bits. Positive overlap of the supplied spans is rejected.
[[nodiscard]] bool construct_alternative(const CResolvedTypes& schema, const CDocumentRead& document,
    const CTypesIndex type, const SOccurrence declaration, const std::uint8_t* const base,
    const std::size_t base_size, std::uint8_t* const destination, const std::size_t destination_size,
    SValueDiagnostic& diagnostic) noexcept;

//  Run the same construction rules against existing bytes, without writing or
//  allocating a reconstructed value. A valid declaration can still mismatch;
//  declaration errors take precedence over a mismatch within that value.
[[nodiscard]] bool check_value(const CResolvedTypes& schema, const CDocumentRead& document,
    const CTypesIndex type, const SOccurrence declaration, const std::uint8_t* const bytes,
    const std::size_t size, const EConstructionMode mode, bool& matches, SValueDiagnostic& diagnostic,
    const bool singleton_element = false) noexcept;
[[nodiscard]] bool check_alternative(const CResolvedTypes& schema, const CDocumentRead& document,
    const CTypesIndex type, const SOccurrence declaration, const std::uint8_t* const base,
    const std::size_t base_size, const std::uint8_t* const bytes, const std::size_t size,
    bool& matches, SValueDiagnostic& diagnostic) noexcept;

enum class EValueCheck : std::uint8_t { match = 0u, mismatch, invalid };

//==============================================================================
//  Encoded-value comparison
//==============================================================================

//  Compare encoded addressable fields only. Padding and unused bit positions
//  are ignored; NaN payloads are equivalent, while other encodings are exact.
[[nodiscard]] bool compare_encoded(const CResolvedTypes& schema, const CTypesIndex type,
    const std::uint8_t* const expected, const std::size_t expected_size,
    const std::uint8_t* const actual, const std::size_t actual_size) noexcept;

}   // namespace detail

}   // namespace schema

#endif // SCHEMA_VALUE_CODEC_HPP_INCLUDED
