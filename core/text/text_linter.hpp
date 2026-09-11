
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
#include "containers/StringBuffers.hpp"
#include "text/text_diagnostics.hpp"

//  The bit values deliberately represent source forms rather than output forms.
//  A compound form is considered before either of its constituent forms.
enum class ETextLineEnding : std::uint32_t
{
    none = 0u,
    lf = (1u << 0), cr = (1u << 1), crlf = (1u << 2), lfcr = (1u << 3),
    vt = (1u << 4), ff = (1u << 5), nel = (1u << 6), ls = (1u << 7), ps = (1u << 8)
};

constexpr std::uint32_t text_line_ending_bit(const ETextLineEnding value) noexcept { return static_cast<std::uint32_t>(value); }

constexpr std::uint32_t operator|(const ETextLineEnding lhs, const ETextLineEnding rhs) noexcept { return text_line_ending_bit(lhs) | text_line_ending_bit(rhs); }
constexpr std::uint32_t operator|(const std::uint32_t lhs, const ETextLineEnding rhs) noexcept { return lhs | text_line_ending_bit(rhs); }
constexpr std::uint32_t operator|(const ETextLineEnding lhs, const std::uint32_t rhs) noexcept { return text_line_ending_bit(lhs) | rhs; }

constexpr std::uint32_t k_compound_line_endings = ETextLineEnding::crlf | ETextLineEnding::lfcr;

constexpr std::uint32_t k_default_text_lint_line_endings = ETextLineEnding::lf | ETextLineEnding::cr | ETextLineEnding::crlf;
constexpr std::uint32_t k_document_text_lint_line_endings = (1u << 9) - 1u;

enum class ECP1252Confidence : std::uint8_t { none = 0u, low, moderate, likely };

enum class ETextLintEncoding : std::uint8_t { none = 0u, utf8 };

enum class ETextLintEvidence : std::uint32_t
{
    none = 0u,
    strict_utf8_failure = (1u << 0), cp1252_defined_c1 = (1u << 1),
    cp1252_printable = (1u << 2), cp1252_undefined_byte = (1u << 3),
    cp1252_common_punctuation = (1u << 4)
};

constexpr std::uint32_t text_lint_evidence_bit(const ETextLintEvidence value) noexcept { return static_cast<std::uint32_t>(value); }

enum class ETextLintFailure : std::uint8_t
{
    none = 0u, invalid_input_view, input_limit, output_limit, allocation_failed,
    utf8_decode, undefined_cp1252_byte, cp1252_decode
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
    bool before_output = false;
    ETextLintFailure reason = ETextLintFailure::none;
    //  Raw SuiteUTF bits from the terminal decoding failure, if applicable.
    std::uint32_t suite_utf_cp_errors = 0u;
    CTextLocation location;
};

struct CTextLintReport
{
    bool success = false;
    bool allocation_failed = false;
    bool output_exceeds_engine_size_limit = false;
    bool input_too_large_for_suite_utf = false;
    bool input_view_invalid = false;
    bool input_is_empty = false;
    std::uint32_t source_findings = 0u;
    //  Evidence from an abandoned UTF-8 attempt has no output location and
    //  is not a terminal failure after successful CP1252 conversion.
    std::uint32_t utf8_attempt_errors = 0u;

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
    std::size_t cesu8_pair_count = 0u;
    std::uint32_t encountered_line_endings = 0u;
    std::uint32_t normalised_line_endings = 0u;
    CTextLineMetrics input_metrics;
    CTextLineMetrics output_metrics;

    //  CP1252 is a recovery inference, not a proof. These bitsets preserve
    //  both supporting and countervailing observations for callers/loggers.
    bool recovered_as_cp1252 = false;
    //  Retained aggregate; undefined CP1252 now fails, so this remains zero.
    std::size_t cp1252_replacement_character_count = 0u;
    std::uint32_t cp1252_positive_evidence = 0u;
    std::uint32_t cp1252_counter_evidence = 0u;
    ECP1252Confidence cp1252_confidence = ECP1252Confidence::none;

    //  Terminal failure only. It remains available after output disposal.
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

//  Converts defined CP1252, exact modified NULs and valid CESU pairs to UTF-8. Literal
//  payload zeros are retained and counted. Trailing source zeros are stripped
//  before decoding; the output's logical length excludes only its own final
//  terminator. Generic line metrics follow the configured newline mask.
//  Document ingestion uses k_document_text_lint_line_endings; normalization
//  applies uniformly, with no awareness of quotes, comments or escapes.
//  A null byte view is absent input and reports invalid_input_view.
[[nodiscard]] CTextLintResult lint(const CByteConstView& input, const std::uint32_t line_ending_flags = k_default_text_lint_line_endings) noexcept;
//  CStringView can represent present zero-length input; CByteConstView cannot.
//  This is still source text, with the same encoding detection as byte input.
//  Both overloads enter the linter directly without conversion between views.
[[nodiscard]] CTextLintResult lint(const CStringView& input, const std::uint32_t line_ending_flags = k_default_text_lint_line_endings) noexcept;

}   //  namespace text_linter

#endif  //  TEXT_LINTER_HPP_INCLUDED
