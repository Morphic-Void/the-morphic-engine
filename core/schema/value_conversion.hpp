
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_conversion.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Sep 26
//
//  Shared document-literal conversion for schema defaults and payload values.

//  Internal schema literal conversion. Included by resolver/codec implementations;
//  consumers should use resolved_schema.hpp or the appropriate role header.

#pragma once

#ifndef SCHEMA_VALUE_CONVERSION_HPP_INCLUDED
#define SCHEMA_VALUE_CONVERSION_HPP_INCLUDED

#include "schema/resolved_schema.hpp"
#include "types/fp16data_t.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace schema
{

namespace detail
{

//==============================================================================
//  Scalar range checks
//==============================================================================

[[nodiscard]] inline std::uint64_t max_unsigned(const unsigned width) noexcept
{
    return width == 64u ? UINT64_MAX : (UINT64_C(1) << width) - 1u;
}

[[nodiscard]] inline bool fits_scalar(const SScalar& value, const bool is_signed, const unsigned width) noexcept
{
    if (is_signed)
    {
        const std::int64_t high = (width == 64u) ? INT64_MAX : static_cast<std::int64_t>((UINT64_C(1) << (width - 1u)) - 1u);
        const std::int64_t low = -high - 1;
        return value.kind == EScalar::signed_integer ?
            ((value.value.signed_value >= low) && (value.value.signed_value <= high)) :
            (value.value.unsigned_value <= static_cast<std::uint64_t>(high));
    }
    return value.kind == EScalar::signed_integer ?
        ((value.value.signed_value >= 0) && (static_cast<std::uint64_t>(value.value.signed_value) <= max_unsigned(width))) :
        (value.value.unsigned_value <= max_unsigned(width));
}

//==============================================================================
//  Normalised rounding
//==============================================================================

//  UNORM reconstructs code/(2^n-1); SNORM reconstructs max(code/(2^(n-1)-1),-1).
//  Thus n=8 uses 255 or 127, and both raw -128 and -127 decode to -1 while
//  floating -1 encodes -127. Nearest with ties away from zero is our encoder
//  rule, consistent with D3D 3.2.3; Vulkan allows conversion rounding latitude.
//  See https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
//  and https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-fixedfpconv
[[nodiscard]] inline std::uint64_t nearest_normalised_magnitude(const double magnitude, const unsigned bits) noexcept
{
    //  Precondition: 0 < magnitude < 1 and 1 <= bits <= 64. Scaling by a power
    //  of two is exact; multiplying by (2^bits-1) in double loses low code bits.
    const double scaled = std::ldexp(magnitude, bits);
    const double whole = std::floor(scaled);
    const double fraction = scaled - whole;
    std::uint64_t code = static_cast<std::uint64_t>(whole); //  scaled < 2^64.

    //  Exact target is whole + fraction - magnitude. Compare around half without
    //  rounding that subtraction across the tie boundary.
    if ((fraction >= 0.5) && ((fraction - 0.5) >= magnitude))
    {
        ++code;
    }
    else if ((magnitude > 0.5) && (fraction < (magnitude - 0.5)))
    {
        --code;
    }
    return code;
}

//==============================================================================
//  Document literal conversion
//==============================================================================

template<class TQuery, class THandle>
[[nodiscard]] bool convert_number(const TQuery& document, const THandle source, const bool is_signed,
    const unsigned bit_width, SScalar& result, EReason& reason) noexcept
{
    SScalar value;
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    if (document.signed_integer_value(source, signed_value))
    {
        value.value.signed_value = signed_value;
        value.kind = EScalar::signed_integer;
    }
    else if (document.unsigned_integer_value(source, unsigned_value))
    {
        value.value.unsigned_value = unsigned_value;
        value.kind = EScalar::unsigned_integer;
    }
    else
    {
        double floating_value{};
        if (!document.floating_point_value(source, floating_value) || !std::isfinite(floating_value) ||
            (std::trunc(floating_value) != floating_value))
        {
            reason = EReason::invalid_range;
            return false;
        }

        //  Power-of-two exclusive upper bounds are exact in binary64, including
        //  2^63 and 2^64; rounded INT64_MAX/UINT64_MAX are unsafe cast guards.
        if (is_signed)
        {
            const double bound = std::ldexp(1.0, (bit_width - 1u));
            if ((floating_value < -bound) || (floating_value >= bound))
            {
                reason = EReason::invalid_range;
                return false;
            }
            value.kind = EScalar::signed_integer;
            value.value.signed_value = static_cast<std::int64_t>(floating_value);
        }
        else
        {
            if ((floating_value < 0.0) || (floating_value >= std::ldexp(1.0, bit_width)))
            {
                reason = EReason::invalid_range;
                return false;
            }
            value.value.unsigned_value = static_cast<std::uint64_t>(floating_value);
        }
    }
    if (!fits_scalar(value, is_signed, bit_width))
    {
        reason = EReason::invalid_range;
        return false;
    }
    if (is_signed && (value.kind != EScalar::signed_integer))
    {
        value.value.signed_value = static_cast<std::int64_t>(value.value.unsigned_value);
        value.kind = EScalar::signed_integer;
    }
    if (!is_signed && (value.kind == EScalar::signed_integer))
    {
        value.value.unsigned_value = static_cast<std::uint64_t>(value.value.signed_value);
        value.kind = EScalar::unsigned_integer;
    }
    result = value;
    return true;
}

template<class TQuery, class THandle>
[[nodiscard]] bool convert_primitive(const TQuery& document, const THandle source, const EPrimitive primitive,
    const unsigned byte_size, SScalar& result, EReason& reason) noexcept
{
    if ((primitive >= EPrimitive::i8) && (primitive <= EPrimitive::u64))
    {
        return convert_number(document, source, (primitive <= EPrimitive::i64), byte_size * 8u, result, reason);
    }
    double floating_value{};
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    bool boolean{};
    bool special = false;
    if (document.signed_integer_value(source, signed_value))
    {
        floating_value = static_cast<double>(signed_value);
    }
    else if (document.unsigned_integer_value(source, unsigned_value))
    {
        floating_value = static_cast<double>(unsigned_value);
    }
    else if (document.floating_point_value(source, floating_value))
    {
    }
    else if ((primitive == EPrimitive::b8) && document.boolean_value(source, boolean))
    {
        floating_value = boolean ? 1.0 : 0.0;
    }
    else if ((primitive != EPrimitive::b8) && (document.value_kind(source) == EDocumentValueKind::string))
    {
        const CStringView text = document.string_value(source);
        char lower[10]{};
        if (text.length() >= sizeof(lower))
        {
            reason = EReason::invalid_default;
            return false;
        }
        for (std::size_t character_index = 0u; character_index < text.length(); ++character_index)
        {
            const std::uint8_t character = text.string()[character_index];
            lower[character_index] = static_cast<char>(((character >= 'A') && (character <= 'Z')) ?
                (character + ('a' - 'A')) : character);
        }
        const CStringView word{ lower, text.length() };
        const auto equal = [](const CStringView a, const char* const b) noexcept
        {
            const CStringView other{ b };
            return a.length() == other.length() &&
                (a.length() == 0u || std::memcmp(a.string(), other.string(), a.length()) == 0);
        };
        if (equal(word, "nan"))
        {
            floating_value = std::numeric_limits<double>::quiet_NaN();
        }
        else if (equal(word, "inf") || equal(word, "+inf") || equal(word, "infinity") || equal(word, "+infinity"))
        {
            floating_value = std::numeric_limits<double>::infinity();
        }
        else if (equal(word, "-inf") || equal(word, "-infinity"))
        {
            floating_value = -std::numeric_limits<double>::infinity();
        }
        else
        {
            reason = EReason::invalid_default;
            return false;
        }
        special = true;
    }
    else
    {
        reason = EReason::invalid_default;
        return false;
    }
    if (!special && !std::isfinite(floating_value))
    {
        reason = EReason::invalid_default;
        return false;
    }
    if (primitive == EPrimitive::b8)
    {
        result.kind = EScalar::boolean;
        result.value.unsigned_value = floating_value != 0.0 ? 1u : 0u;
    }
    else if (primitive == EPrimitive::f16)
    {
        result.kind = EScalar::half;
        result.value.unsigned_value = fp16data_t{ floating_value }.getBits();
    }
    else
    {
        if (primitive == EPrimitive::f32)
        {
            if (!special && (std::abs(floating_value) > static_cast<double>(std::numeric_limits<float>::max())))
            {
                reason = EReason::invalid_range;
                return false;
            }
            const float rounded = static_cast<float>(floating_value);
            if (!special && (!std::isfinite(rounded) || ((floating_value != 0.0) && (rounded == 0.0f))))
            {
                reason = EReason::invalid_range;
                return false;
            }
            floating_value = rounded;
        }
        result.kind = EScalar::floating_point;
        result.value.floating_value = floating_value;
    }
    return true;
}

//==============================================================================
//  Normalised literal conversion
//==============================================================================

template<class TQuery, class THandle>
[[nodiscard]] bool convert_normalised(const TQuery& document, const THandle source,
    const EInterpretation interpretation, const unsigned width, SScalar& result, EReason& reason) noexcept
{
    SScalar floating;
    if (!convert_primitive(document, source, EPrimitive::f64, 8u, floating, reason))
    {
        return false;
    }
    const double floating_value = floating.value.floating_value;

    //  Check endpoints before casts: double(UINT64_MAX) rounds to 2^64,
    //  and double(INT64_MAX) rounds to 2^63. Internal magnitudes fit.
    if (interpretation == EInterpretation::unorm)
    {
        result.kind = EScalar::unsigned_integer;
        result.value.unsigned_value = std::isnan(floating_value) || (floating_value <= 0.0) ? 0u :
            (floating_value >= 1.0 ? max_unsigned(width) :
                nearest_normalised_magnitude(floating_value, width));
    }
    else
    {
        const std::uint64_t magnitude = std::isnan(floating_value) || (floating_value == 0.0) ? 0u :
            (std::abs(floating_value) >= 1.0 ? max_unsigned(width - 1u) :
                nearest_normalised_magnitude(std::abs(floating_value), width - 1u));
        result.kind = EScalar::signed_integer;
        result.value.signed_value = floating_value < 0.0 ?
            -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    }
    return true;
}

}   // namespace detail

}   // namespace schema

#endif // SCHEMA_VALUE_CONVERSION_HPP_INCLUDED
