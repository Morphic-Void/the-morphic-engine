
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    integer_notation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Oct 26
//
//  Shared integer presentation policy for schema and data document construction.

//  Internal schema helpers. Public consumers use the schema/data output APIs.

#pragma once

#ifndef SCHEMA_INTEGER_NOTATION_HPP_INCLUDED
#define SCHEMA_INTEGER_NOTATION_HPP_INCLUDED

#include "schema/resolved_schema.hpp"
#include "data_model/live_document.hpp"

namespace schema
{

//==============================================================================
//  Integer presentation and measured storage
//==============================================================================

[[nodiscard]] constexpr EIntegerNotation integer_notation(const EPrimitive primitive) noexcept
{
    return ((primitive == EPrimitive::i32) || (primitive == EPrimitive::u32) ||
        (primitive == EPrimitive::i64) || (primitive == EPrimitive::u64)) ?
        EIntegerNotation::hexadecimal : EIntegerNotation::decimal;
}

[[nodiscard]] constexpr EIntegerNotation mask_notation(const std::uint32_t storage_size) noexcept
{
    return (storage_size == 1u) ? EIntegerNotation::hexadecimal_2 :
        ((storage_size == 2u) ? EIntegerNotation::hexadecimal_4 :
            ((storage_size == 4u) ? EIntegerNotation::hexadecimal_8 : EIntegerNotation::hexadecimal_16));
}

[[nodiscard]] constexpr CIntegerMetadata signed_integer_metadata(const std::int64_t value, const EIntegerNotation notation) noexcept
{
    return { EIntegerDomain::signed_value, live_signed_integer_smallest_width(value), notation, EIntegerPrefix::standard };
}

[[nodiscard]] constexpr CIntegerMetadata unsigned_integer_metadata(const std::uint64_t value, const EIntegerNotation notation) noexcept
{
    return { EIntegerDomain::unsigned_value, live_unsigned_integer_smallest_width(value), notation, EIntegerPrefix::standard };
}

//==============================================================================
//  Structural quantities
//==============================================================================

[[nodiscard]] inline CNodeKey create_structural_integer(CLiveDocument& document, const std::uint32_t value,
    const CStringView& name) noexcept
{
    return document.create_unsigned_integer(value, unsigned_integer_metadata(value, EIntegerNotation::decimal_or_hexadecimal), name);
}

}   // namespace schema

#endif // SCHEMA_INTEGER_NOTATION_HPP_INCLUDED
