//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    utf8_string.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Aug 26
//
//  Bounded UTF-8 validation for document strings.

#include "text/utf8_string.hpp"

#include <limits>

namespace utf8_string
{

//  Keep byte-sequence mechanics distinct from the public document-string
//  policy while retaining explicit translation-unit linkage for each helper.
namespace utf8_util
{

[[nodiscard]] static constexpr bool is_continuation(const std::uint8_t value) noexcept
{
    return (value & 0xc0u) == 0x80u;
}

[[nodiscard]] static bool consume_sequence(const std::uint8_t* const source, const std::size_t source_size, std::size_t& offset) noexcept
{
    const std::uint8_t first = source[offset];
    const std::size_t remaining = source_size - offset;

    if (first <= 0x7fu)
    {
        ++offset;
        return true;
    }
    if ((first == 0xc0u) && (remaining >= 2u) && (source[offset + 1u] == 0x80u))
    {
        offset += 2u;
        return true;
    }
    if ((first >= 0xc2u) && (first <= 0xdfu))
    {
        if ((remaining < 2u) || !is_continuation(source[offset + 1u])) return false;
        offset += 2u;
        return true;
    }
    if ((first >= 0xe0u) && (first <= 0xefu))
    {
        if ((remaining < 3u) || !is_continuation(source[offset + 1u]) ||
            !is_continuation(source[offset + 2u]))
        {
            return false;
        }
        const std::uint8_t second = source[offset + 1u];
        if (((first == 0xe0u) && (second < 0xa0u)) ||
            ((first == 0xedu) && (second >= 0xa0u)))
        {
            return false; // overlong or surrogate
        }
        offset += 3u;
        return true;
    }
    if ((first >= 0xf0u) && (first <= 0xf4u))
    {
        if ((remaining < 4u) || !is_continuation(source[offset + 1u]) ||
            !is_continuation(source[offset + 2u]) || !is_continuation(source[offset + 3u]))
        {
            return false;
        }
        const std::uint8_t second = source[offset + 1u];
        if (((first == 0xf0u) && (second < 0x90u)) ||
            ((first == 0xf4u) && (second > 0x8fu)))
        {
            return false; // overlong or above U+10FFFF
        }
        offset += 4u;
        return true;
    }
    return false;
}

}   //  namespace utf8_util

bool validate_and_measure(
    const std::uint8_t* const source,
    const std::size_t source_size,
    const ELiteralNulPolicy literal_nul_policy,
    std::size_t& normalized_size) noexcept
{
    normalized_size = 0u;
    if (source == nullptr) return false;

    std::size_t offset = 0u;
    while (offset < source_size)
    {
        if (source[offset] == 0u)
        {
            if (literal_nul_policy == ELiteralNulPolicy::reject) return false;
            if (normalized_size > (std::numeric_limits<std::size_t>::max() - 2u)) return false;
            normalized_size += 2u;
            ++offset;
            continue;
        }

        const std::size_t sequence_start = offset;
        if (!utf8_util::consume_sequence(source, source_size, offset)) return false;
        const std::size_t sequence_size = offset - sequence_start;
        if (normalized_size > (std::numeric_limits<std::size_t>::max() - sequence_size)) return false;
        normalized_size += sequence_size;
    }
    return true;
}

bool normalize_literal_nuls(
    const std::uint8_t* const source,
    const std::size_t source_size,
    std::uint8_t* const destination,
    const std::size_t destination_size) noexcept
{
    if ((source == nullptr) || ((destination == nullptr) && (destination_size != 0u))) return false;

    std::size_t measured_size = 0u;
    if (!validate_and_measure(source, source_size, ELiteralNulPolicy::promote_to_modified_utf8, measured_size) ||
        (measured_size != destination_size))
    {
        return false;
    }

    std::size_t output = 0u;
    for (std::size_t input = 0u; input < source_size; ++input)
    {
        if (source[input] == 0u)
        {
            destination[output++] = 0xc0u;
            destination[output++] = 0x80u;
        }
        else
        {
            destination[output++] = source[input];
        }
    }
    return output == destination_size;
}

} // namespace utf8_string
