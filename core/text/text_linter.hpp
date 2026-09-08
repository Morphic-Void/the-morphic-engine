
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    text_linter.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No file I/O or logging.
//
//  Bounded source-text validation, transcoding recovery, and line-ending
//  normalization for byte-backed text sources.

#pragma once

#ifndef TEXT_LINTER_HPP_INCLUDED
#define TEXT_LINTER_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t, std::uint32_t

#include "containers/ByteBuffers.hpp"

//  The bit values deliberately represent source forms rather than output forms.
//  A compound form is considered before either of its constituent forms.
enum class ETextLineEnding : std::uint32_t
{
    none = 0u,
    lf = (1u << 0), cr = (1u << 1), crlf = (1u << 2), lfcr = (1u << 3),
    vt = (1u << 4), ff = (1u << 5), nel = (1u << 6), ls = (1u << 7), ps = (1u << 8),
};

constexpr std::uint32_t text_line_ending_bit(const ETextLineEnding value) noexcept { return static_cast<std::uint32_t>(value); }

constexpr std::uint32_t operator|(const ETextLineEnding lhs, const ETextLineEnding rhs) noexcept { return text_line_ending_bit(lhs) | text_line_ending_bit(rhs); }
constexpr std::uint32_t operator|(const std::uint32_t lhs, const ETextLineEnding rhs) noexcept { return lhs | text_line_ending_bit(rhs); }
constexpr std::uint32_t operator|(const ETextLineEnding lhs, const std::uint32_t rhs) noexcept { return text_line_ending_bit(lhs) | rhs; }

constexpr std::uint32_t k_compound_line_endings = ETextLineEnding::crlf | ETextLineEnding::lfcr;

constexpr std::uint32_t k_default_text_lint_line_endings = ETextLineEnding::lf | ETextLineEnding::cr | ETextLineEnding::crlf;

enum class ECP1252Confidence : std::uint8_t { none = 0u, low, moderate, likely };

enum class ETextLintEncoding : std::uint8_t { none = 0u, utf8 };

enum class ETextLintEvidence : std::uint32_t
{
    none = 0u,
    strict_utf8_failure = (1u << 0), cp1252_defined_c1 = (1u << 1),
    cp1252_printable = (1u << 2), cp1252_undefined_byte = (1u << 3),
    cp1252_common_punctuation = (1u << 4),
};

constexpr std::uint32_t text_lint_evidence_bit(const ETextLintEvidence value) noexcept { return static_cast<std::uint32_t>(value); }

struct CTextLintLocation
{   //  Lines and code-point columns are 1-based. All byte offsets are 0-based.
    std::size_t line_1_based = 1u;
    std::size_t code_point_column_1_based = 1u;
    std::size_t byte_offset_in_line_0_based = 0u;
    std::size_t byte_offset_in_file_0_based = 0u;
    std::size_t line_start_byte_offset_in_file_0_based = 0u;
};

struct CTextLineMetrics
{   //  Lines are logical lines under the configured newline-form mask.
    std::size_t line_count = 0u;
    std::size_t empty_line_count = 0u;
    std::size_t whitespace_only_line_count = 0u;
    std::size_t total_code_points = 0u;
    std::size_t total_likely_printable_code_points = 0u;
    std::size_t maximum_line_code_points = 0u;
    std::size_t maximum_line_bytes = 0u;
    std::size_t maximum_line_likely_printable_code_points = 0u;
};

struct CTextLintFailure
{
    bool present = false;
    //  Raw unicode::utf::toolkit::cp_errors bits from the source UTF-8 attempt.
    std::uint32_t suite_utf_cp_errors = 0u;
    CTextLintLocation location;
};

struct CTextLintReport
{
    bool success = false;
    bool allocation_failed = false;
    bool output_exceeds_engine_size_limit = false;
    bool input_too_large_for_suite_utf = false;
    bool input_view_invalid = false;
    bool input_is_empty = false;

    //  Only a successful result has a published output encoding. ASCII is
    //  a UTF-8 subset; CP1252 source inference remains separately reported.
    ETextLintEncoding output_encoding = ETextLintEncoding::none;

    std::size_t input_byte_size = 0u;
    std::size_t payload_input_byte_size = 0u;
    std::size_t stripped_terminal_zero_count = 0u;
    std::size_t logical_text_byte_size = 0u;

    bool input_is_pure_ascii = true;
    bool output_is_pure_ascii = true;
    bool leading_bom_detected = false;
    bool leading_utf8_bom_stripped = false;
    std::uint32_t leading_bom_byte_count = 0u;

    //  Literal source zeros in the payload, excluding stripped terminators.
    //  Counted once before decoding, including when CP1252 fallback is needed.
    std::size_t embedded_nul_count = 0u;
    //  Accepted C0 80 sequences normalized to U+0000 on the UTF-8 path.
    //  Reset if that attempt is discarded in favour of CP1252 conversion.
    std::size_t modified_utf8_nul_count = 0u;
    std::uint32_t encountered_line_endings = 0u;
    std::uint32_t normalised_line_endings = 0u;
    CTextLineMetrics input_metrics;
    CTextLineMetrics output_metrics;

    //  CP1252 is a recovery inference, not a proof. These bitsets preserve
    //  both supporting and countervailing observations for callers/loggers.
    bool recovered_as_cp1252 = false;
    std::size_t cp1252_replacement_character_count = 0u;
    std::uint32_t cp1252_positive_evidence = 0u;
    std::uint32_t cp1252_counter_evidence = 0u;
    ECP1252Confidence cp1252_confidence = ECP1252Confidence::none;

    CTextLintFailure first_failure;
};

struct CTextLintResult
{
    //  UTF-8 payload followed by one physical zero. Embedded U+0000 is valid:
    //  consume report.logical_text_byte_size, never a zero-terminated length.
    CByteBuffer output;
    CTextLintReport report;
};

namespace text_linter
{

//  Converts recoverable CP1252 and accepted modified NULs to UTF-8. Literal
//  payload zeros are retained and counted. Trailing source zeros are stripped
//  before decoding; the output's logical length excludes only its own final
//  terminator. Pass zero flags to preserve quoted content during document
//  ingestion; line metrics follow the configured newline mask.
[[nodiscard]] CTextLintResult lint(const CByteConstView& input, const std::uint32_t line_ending_flags = k_default_text_lint_line_endings) noexcept;

};

#endif  //  TEXT_LINTER_HPP_INCLUDED
