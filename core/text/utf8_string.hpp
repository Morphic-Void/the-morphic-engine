//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    utf8_string.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Aug 26
//
//  Bounded UTF-8 validation for document strings.

#pragma once

#ifndef UTF8_STRING_HPP_INCLUDED
#define UTF8_STRING_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

namespace utf8_string
{

enum class ELiteralNulPolicy : std::uint8_t
{
    reject,
    promote_to_modified_utf8,
};

//  Accepts strict UTF-8 plus the exact modified-UTF-8 encoding C0 80 for
//  U+0000. Under promote_to_modified_utf8, literal zero bytes are accepted
//  and each contributes two bytes to normalized_size; otherwise they fail.
[[nodiscard]] bool validate_and_measure(
    const std::uint8_t* source,
    std::size_t source_size,
    ELiteralNulPolicy literal_nul_policy,
    std::size_t& normalized_size) noexcept;

//  The source must already have passed validate_and_measure with
//  promote_to_modified_utf8. Copies strict/modified UTF-8 unchanged and
//  replaces each literal zero byte with C0 80.
[[nodiscard]] bool normalize_literal_nuls(
    const std::uint8_t* source,
    std::size_t source_size,
    std::uint8_t* destination,
    std::size_t destination_size) noexcept;

} // namespace utf8_string

#endif // UTF8_STRING_HPP_INCLUDED
