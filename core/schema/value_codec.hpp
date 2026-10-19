
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

#include "schema/resolved_schema.hpp"

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
[[nodiscard]] CNodeKey decode_document_scalar(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EScalarDecodeReason& reason) noexcept;

namespace detail
{

//==============================================================================
//  Internal construction and diagnostics
//==============================================================================

enum class EConstructionMode : std::uint8_t { instance = 0u, complete_bulk_data };

struct SValueDiagnostic
{
    EReason reason{ EReason::none };
    SOccurrence occurrence;
    CSchemaIndex type;
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
    [[nodiscard]] bool prepare(const CResolvedSchema& schema, const CDocumentRead& document,
        const CSchemaIndex type, const ECategory category, const SOccurrence source, SValueDiagnostic& diagnostic) noexcept;
    [[nodiscard]] SOccurrence child(const CSchemaIndex selected) noexcept;

private:
    struct SNamedInput { CSchemaIndex selected; SOccurrence source; };
    [[nodiscard]] CSchemaIndex component(const SOccurrence source) const noexcept;
    const CResolvedSchema* m_schema{};
    const CDocumentRead* m_document{};
    CSchemaIndex m_type, m_selected;
    ECategory m_category{};
    SOccurrence m_cursor;
    TPodVector<SNamedInput> m_named;
    std::size_t m_next{};
};

//  Scalar spelling is only shorthand for a compound with one scalar member.
//  It never recursively unwraps a compound or array member.
[[nodiscard]] bool is_scalar_shorthand(const CResolvedSchema& schema, const CSchemaIndex type,
    const SType& layout, const CDocumentRead& document, const SOccurrence value) noexcept;

//  Validate supplied declarations only. Omissions inherit/default later; no
//  binary storage, implicit subtree expansion or writer selection index is needed.
[[nodiscard]] bool validate_declaration(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const EConstructionMode mode,
    SValueDiagnostic& diagnostic, const bool singleton_element = false) noexcept;

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

//  Run the same construction rules against existing bytes, without writing or
//  allocating a reconstructed value. A valid declaration can still mismatch;
//  declaration errors take precedence over a mismatch within that value.
[[nodiscard]] bool check_value(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const std::uint8_t* const bytes,
    const std::size_t size, const EConstructionMode mode, bool& matches, SValueDiagnostic& diagnostic,
    const bool singleton_element = false) noexcept;
[[nodiscard]] bool check_alternative(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const std::uint8_t* const base,
    const std::size_t base_size, const std::uint8_t* const bytes, const std::size_t size,
    bool& matches, SValueDiagnostic& diagnostic) noexcept;

enum class EValueCheck : std::uint8_t { match = 0u, mismatch, invalid };

//  Live/baked loading and switching share the same record-level checks. The
//  type selector observes either current or staged owner-local indices.
template<class TRecords, class TType>
[[nodiscard]] static EValueCheck check_instance_records(const CResolvedSchema& schema, const CDocumentRead& document,
    const TRecords& records, const std::uint8_t* const payload, const TType& type_at, SValueDiagnostic& error) noexcept
{
    for (std::size_t i = 0u; i < records.size(); ++i)
    {
        const auto& record = records[i];
        const std::uint8_t* const bytes = record.extent ? payload + record.offset : nullptr;
        bool matches{}, valid{};
        if (record.parent == UINT32_MAX)
        {
            valid = check_value(schema, document, type_at(i), SOccurrence{ record.declaration },
                bytes, record.extent, EConstructionMode::instance, matches, error);
        }
        else
        {
            const auto& parent = records[record.parent];
            valid = check_alternative(schema, document, type_at(i), SOccurrence{ record.declaration },
                parent.extent ? payload + parent.offset : nullptr, parent.extent, bytes, record.extent, matches, error);
        }
        if (!valid || !matches)
        {
        if (!error.occurrence.is_valid()) error.occurrence = SOccurrence{ record.entry };
            return valid ? EValueCheck::mismatch : EValueCheck::invalid;
        }
    }
    return EValueCheck::match;
}

template<class TRecords, class TType>
[[nodiscard]] static EValueCheck check_bulk_records(const CResolvedSchema& schema, const CDocumentRead& document,
    const TRecords& records, const std::uint8_t* const payload, const TType& type_at, SValueDiagnostic& error) noexcept
{
    for (std::size_t i = 0u; i < records.size(); ++i)
    {
        const auto& record = records[i];
        const SOccurrence data = document.object_child(SOccurrence{ record.entry }, CStringView{ "data" });
        if (!data.is_valid())
        {
            continue;
        }
        SType layout;
        if (!schema.type(type_at(i), layout))
        {
            error = { EReason::unknown_type, SOccurrence{ record.entry }, type_at(i) };
            return EValueCheck::invalid;
        }
        SOccurrence item = document.first_child(data);
        for (std::uint32_t ordinal = 0u; ordinal < record.count; ++ordinal)
        {
            const std::size_t offset = record.offset + static_cast<std::size_t>(layout.size) * ordinal;
            bool matches{};
            const bool valid = check_value(schema, document, type_at(i), item,
                layout.size ? payload + offset : nullptr, static_cast<std::size_t>(layout.size),
                EConstructionMode::complete_bulk_data, matches, error, document.is_object_entry(item));
            if (!valid || !matches)
            {
                if (!error.occurrence.is_valid()) error.occurrence = item;
                return valid ? EValueCheck::mismatch : EValueCheck::invalid;
            }
            item = document.next_sibling(item);
        }
    }
    return EValueCheck::match;
}

//==============================================================================
//  Encoded-value comparison
//==============================================================================

//  Compare encoded addressable fields only. Padding and unused bit positions
//  are ignored; NaN payloads are equivalent, while other encodings are exact.
[[nodiscard]] bool compare_encoded(const CResolvedSchema& schema, const CSchemaIndex type,
    const std::uint8_t* const expected, const std::size_t expected_size,
    const std::uint8_t* const actual, const std::size_t actual_size) noexcept;

}   // namespace detail

}   // namespace schema

#endif // SCHEMA_VALUE_CODEC_HPP_INCLUDED
