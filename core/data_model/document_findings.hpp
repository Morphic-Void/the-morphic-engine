
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_findings.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Sep 26
//
//  Shared findings and caller-policy definitions for the parser migration.

#pragma once

#ifndef DOCUMENT_FINDINGS_HPP_INCLUDED
#define DOCUMENT_FINDINGS_HPP_INCLUDED

#include <cstdint>

#include "text/text_diagnostics.hpp"

enum class EDocumentFinding : std::uint32_t
{
    none = 0u,

    //  Source observations retain the linter's bit identities. Detailed
    //  line-ending forms, decoder evidence and statistics stay in CTextLintReport.
    non_ascii_utf8 = text_source_finding_bit(ETextSourceFinding::non_ascii_utf8),
    modified_nul = text_source_finding_bit(ETextSourceFinding::modified_nul),
    cesu8_pair = text_source_finding_bit(ETextSourceFinding::cesu8_pair),
    cp1252 = text_source_finding_bit(ETextSourceFinding::cp1252),
    literal_source_nul = text_source_finding_bit(ETextSourceFinding::literal_nul),
    leading_bom = text_source_finding_bit(ETextSourceFinding::leading_bom),
    stripped_utf8_bom = text_source_finding_bit(ETextSourceFinding::stripped_utf8_bom),
    stripped_terminal_zeros = text_source_finding_bit(ETextSourceFinding::stripped_terminal_zeros),
    utf8_attempt_failed = text_source_finding_bit(ETextSourceFinding::utf8_attempt_failed),
    k_source_start = non_ascii_utf8,
    k_source_end = utf8_attempt_failed,

    //  Reserved for the linter's error flag.
    reserved_undefined_cp1252_byte = text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte),

    //  Relaxed syntax
    comments = 1u << 10,
    unquoted_names = 1u << 11,
    unquoted_strings = 1u << 12,
    single_quotes = 1u << 13,
    trailing_commas = 1u << 14,
    raw_quoted_line_breaks = 1u << 15,
    raw_quoted_controls = 1u << 16,
    name_collision_extension = 1u << 17,
    implicit_body = 1u << 18, //  Non-empty unbraced object or multiple unbracketed root values.
    k_relaxed_start = comments,
    k_relaxed_end = implicit_body,

    //  Morphic extensions
    explicit_plus = 1u << 19,
    binary = 1u << 20,
    hexadecimal = 1u << 21, //  Both 0x/0X and #; # also sets alternate_hexadecimal_prefix.
    alternate_hexadecimal_prefix = 1u << 22,
    k_morphic_start = explicit_plus,
    k_morphic_end = alternate_hexadecimal_prefix,

    //  Semantic observations
    empty_member_name = 1u << 23,
    singleton_normalization = 1u << 24,
    logical_nul = 1u << 25,
    k_semantic_start = empty_member_name,
    k_semantic_end = logical_nul
};

enum class EDocumentFailureStage : std::uint8_t
{
    none = 0u, linter, structure, parser
};

//  One terminal reason, independent of cumulative findings and caller policy.
//  Retain the first terminal failure; subsequent failures must not replace it.
enum class EDocumentFailureReason : std::uint8_t
{
    none = 0u,

    //  Decoding
    utf8_decode, undefined_cp1252_byte, cp1252_decode,

    //  Structure
    unexpected_character, unterminated_comment, unterminated_string,
    invalid_escape, invalid_surrogate_pair, newline_in_name,
    missing_name, missing_colon, missing_value, missing_separator,
    mismatched_delimiter, unexpected_end, trailing_content,

    //  Construction
    numeric_out_of_range, construction_failed,

    //  Resources and invocation (the stage identifies where failure occurred)
    invalid_input_view, allocation_failed, input_limit, storage_limit, internal_error
};

struct CDocumentFailure
{
    EDocumentFailureStage stage{ EDocumentFailureStage::none };
    EDocumentFailureReason reason{ EDocumentFailureReason::none };
};

[[nodiscard]] constexpr std::uint32_t document_finding_bit(const EDocumentFinding finding) noexcept
{
    return static_cast<std::uint32_t>(finding);
}

[[nodiscard]] constexpr std::uint32_t operator|(const EDocumentFinding lhs, const EDocumentFinding rhs) noexcept
{
    return document_finding_bit(lhs) | document_finding_bit(rhs);
}

[[nodiscard]] constexpr std::uint32_t operator|(const std::uint32_t lhs, const EDocumentFinding rhs) noexcept
{
    return lhs | document_finding_bit(rhs);
}

[[nodiscard]] constexpr std::uint32_t operator|(const EDocumentFinding lhs, const std::uint32_t rhs) noexcept
{
    return document_finding_bit(lhs) | rhs;
}

namespace document_findings
{

//  Category boundaries name the first and last included findings.
constexpr std::uint32_t k_source = (document_finding_bit(EDocumentFinding::k_source_end) << 1u) - document_finding_bit(EDocumentFinding::k_source_start);
constexpr std::uint32_t k_relaxed = (document_finding_bit(EDocumentFinding::k_relaxed_end) << 1u) - document_finding_bit(EDocumentFinding::k_relaxed_start);
constexpr std::uint32_t k_morphic = (document_finding_bit(EDocumentFinding::k_morphic_end) << 1u) - document_finding_bit(EDocumentFinding::k_morphic_start);
constexpr std::uint32_t k_semantic = (document_finding_bit(EDocumentFinding::k_semantic_end) << 1u) - document_finding_bit(EDocumentFinding::k_semantic_start);
constexpr std::uint32_t k_all = k_source | k_relaxed | k_morphic | k_semantic;
constexpr std::uint32_t k_encoding_features = k_text_source_encoding_features;
constexpr std::uint32_t k_acceptance_features = k_encoding_features | k_relaxed | k_morphic;

//  Import cumulative observations only. The linter's terminal reason is carried
//  separately, excluding its reserved undefined-CP1252 error bit.
[[nodiscard]] constexpr std::uint32_t from_source(const std::uint32_t source_findings) noexcept
{
    return source_findings & k_source;
}

} // namespace document_findings

namespace document_policy
{

//  ASCII needs no feature permission. These encoding presets grant no syntax
//  permissions, and decoded JSON Unicode escapes do not change source encoding.
constexpr std::uint32_t k_ascii = 0u;
constexpr std::uint32_t k_utf8 = document_finding_bit(EDocumentFinding::non_ascii_utf8);
constexpr std::uint32_t k_modified_utf8 = k_utf8 | EDocumentFinding::modified_nul | EDocumentFinding::cesu8_pair;
constexpr std::uint32_t k_default = k_modified_utf8 | EDocumentFinding::cp1252 | document_findings::k_morphic;
constexpr std::uint32_t k_all_supported = document_findings::k_acceptance_features;

} // namespace document_policy

struct CDocumentParseOptions
{
    std::uint32_t allowed_features{ document_policy::k_default };
};

enum class EDocumentPolicyStatus : std::uint8_t
{
    unexamined = 0u, accepted, rejected, invalid_options
};

struct CDocumentPolicyResult
{
    EDocumentPolicyStatus status{ EDocumentPolicyStatus::unexamined };
    std::uint32_t effective_allowed_features{ 0u };
    std::uint32_t disallowed_features{ 0u };
    std::uint32_t unknown_policy_bits{ 0u };

    [[nodiscard]] constexpr bool accepted() const noexcept;
};

namespace document_policy
{

//  Feature-policy evaluation only: processing status and stage completion are
//  separate. The pipeline must succeed before using this result to publish.
//  Evidence and semantic bits are never interpreted as feature permissions.
[[nodiscard]] constexpr CDocumentPolicyResult evaluate(const std::uint32_t findings, const CDocumentParseOptions& options = {}) noexcept
{
    CDocumentPolicyResult result;
    result.unknown_policy_bits = options.allowed_features & ~document_findings::k_acceptance_features;
    if (result.unknown_policy_bits != 0u)
    {
        result.status = EDocumentPolicyStatus::invalid_options;
        return result;
    }
    result.effective_allowed_features = options.allowed_features;
    if ((options.allowed_features & (EDocumentFinding::modified_nul | EDocumentFinding::cesu8_pair)) != 0u)
    {
        result.effective_allowed_features |= k_utf8;
    }
    result.disallowed_features = (findings & document_findings::k_acceptance_features) & ~result.effective_allowed_features;
    result.status = (result.disallowed_features == 0u) ? EDocumentPolicyStatus::accepted : EDocumentPolicyStatus::rejected;
    return result;
}

} // namespace document_policy

inline constexpr bool CDocumentPolicyResult::accepted() const noexcept
{
    return status == EDocumentPolicyStatus::accepted;
}

#endif // DOCUMENT_FINDINGS_HPP_INCLUDED
