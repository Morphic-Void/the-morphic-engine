
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    text_diagnostics.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Sep 26

#pragma once

#ifndef TEXT_DIAGNOSTICS_HPP_INCLUDED
#define TEXT_DIAGNOSTICS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

struct CTextLocation
{
    //  Coordinates in emitted UTF-8, excluding the physical terminator.
    //  LF advances the line; every other Unicode scalar advances one column.
    bool available = false;
    std::size_t line_1_based = 1u;
    std::size_t code_point_column_1_based = 1u;
};

enum class ETextSourceFinding : std::uint32_t
{
    none = 0u,
    //  Acceptance-relevant source forms. ASCII needs no presence bit.
    non_ascii_utf8 = 1u << 0,
    modified_nul = 1u << 1,
    cesu8_pair = 1u << 2,
    cp1252 = 1u << 3,
    //  Observations and transformations, not permissions.
    literal_nul = 1u << 4,
    leading_bom = 1u << 5,
    stripped_utf8_bom = 1u << 6,
    stripped_terminal_zeros = 1u << 7,
    //  Decoder evidence and terminal issues, not permissions.
    utf8_attempt_failed = 1u << 8,
    undefined_cp1252_byte = 1u << 9
};

constexpr std::uint32_t text_source_finding_bit(const ETextSourceFinding value) noexcept { return static_cast<std::uint32_t>(value); }

constexpr std::uint32_t k_text_source_encoding_features = (1u << 4) - 1u;
constexpr std::uint32_t k_text_source_observations = ((1u << 8) - 1u) & ~k_text_source_encoding_features;
constexpr std::uint32_t k_text_source_issues = ((1u << 10) - 1u) & ~((1u << 8) - 1u);

#endif // TEXT_DIAGNOSTICS_HPP_INCLUDED
