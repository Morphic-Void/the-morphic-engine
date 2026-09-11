
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TextLinter_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "data_model/live_document.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/TextLinter_test_suite.hpp"
#include "text/text_linter.hpp"

namespace text_linter_tests
{

using TTestContext = tests::TTestContext;

void expect_output(TTestContext& ctx, const CTextLintResult& result, const std::uint8_t* const expected, const std::size_t size)
{
    TEST_EXPECT(ctx, result.report.success);
    TEST_EXPECT(ctx, result.report.output_encoding == ETextLintEncoding::utf8);
    TEST_EXPECT(ctx, result.report.logical_text_byte_size == (size - 1u));
    TEST_EXPECT(ctx, result.output.size() == size);
    if (result.output.size() == size)
    {
        TEST_EXPECT(ctx, std::memcmp(result.output.data(), expected, size) == 0);
        TEST_EXPECT(ctx, result.output.data()[result.report.logical_text_byte_size] == 0u);
    }
}

void test_utf8_bom_terminal_zero_and_default_lines(TTestContext& ctx)
{
    const std::uint8_t input[]{ 0xefu, 0xbbu, 0xbfu, 'a', '\r', '\n', 'b', 0u, 0u };
    const std::uint8_t expected[]{ 'a', '\n', 'b', 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, result, expected, sizeof(expected));
    TEST_EXPECT(ctx, result.report.leading_bom_detected);
    TEST_EXPECT(ctx, result.report.leading_utf8_bom_stripped);
    TEST_EXPECT(ctx, !result.report.input_is_pure_ascii);
    TEST_EXPECT(ctx, result.report.output_is_pure_ascii);
    TEST_EXPECT(ctx, !result.report.first_failure.present);
    TEST_EXPECT(ctx, !result.report.allocation_failed);
    TEST_EXPECT(ctx, result.report.stripped_terminal_zero_count == 2u);
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 0u);
    TEST_EXPECT(ctx, result.report.logical_text_byte_size == 3u);
    TEST_EXPECT(ctx, result.report.input_metrics.line_count == 2u);
    TEST_EXPECT(ctx, result.report.input_metrics.total_code_points == 4u);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == 3u);
    TEST_EXPECT(ctx, result.report.input_metrics.total_likely_printable_code_points == 2u);
    TEST_EXPECT(ctx, result.report.output_metrics.total_likely_printable_code_points == 2u);
    TEST_EXPECT(ctx, result.report.output_metrics.maximum_line_bytes == 1u);
}

void test_modified_null_is_payload_not_terminator(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'a', 0xc0u, 0x80u, 'b', 0xc0u, 0x80u };
    const std::uint8_t expected[]{ 'a', 0u, 'b', 0u, 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, result, expected, sizeof(expected));
    const auto string_result = text_linter::lint(CStringView{ input, sizeof(input) });
    expect_output(ctx, string_result, expected, sizeof(expected));
    TEST_EXPECT(ctx, string_result.report.modified_utf8_nul_count == 2u);
    TEST_EXPECT(ctx, string_result.report.source_findings == result.report.source_findings);
    TEST_EXPECT(ctx, result.report.modified_utf8_nul_count == 2u);
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 0u);
    TEST_EXPECT(ctx, result.report.stripped_terminal_zero_count == 0u);
    TEST_EXPECT(ctx, !result.report.input_is_pure_ascii);
    TEST_EXPECT(ctx, result.report.output_is_pure_ascii);
    TEST_EXPECT(ctx, result.report.input_metrics.maximum_line_bytes == 6u);
    TEST_EXPECT(ctx, result.report.output_metrics.maximum_line_bytes == 4u);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == 4u);
}

void test_literal_interior_zero_is_counted(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'a', 0u, 'b', 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, result, input, sizeof(input));
    TEST_EXPECT(ctx, !result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, !result.report.first_failure.present);
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 1u);
    TEST_EXPECT(ctx, result.report.modified_utf8_nul_count == 0u);
    TEST_EXPECT(ctx, result.report.stripped_terminal_zero_count == 1u);
    TEST_EXPECT(ctx, result.report.input_metrics.total_code_points == 3u);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == 3u);
}

void test_cp1252_embedded_zeros_are_counted_once(TTestContext& ctx)
{
    const std::uint8_t input[]{ 0u, 'a', 0u, 0x80u, 'b', 0u, 'c', 0u };
    const std::uint8_t expected[]{ 0u, 'a', 0u, 0xe2u, 0x82u, 0xacu, 'b', 0u, 'c', 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, result, expected, sizeof(expected));
    TEST_EXPECT(ctx, result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 3u);
    TEST_EXPECT(ctx, result.report.modified_utf8_nul_count == 0u);
    TEST_EXPECT(ctx, result.report.stripped_terminal_zero_count == 1u);
    TEST_EXPECT(ctx, result.report.cp1252_replacement_character_count == 0u);
    TEST_EXPECT(ctx, !result.report.first_failure.present);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == 7u);
}

void test_generic_preservation_and_live_nul_storage(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'a', 0u, 0xc0u, 0x80u, '\r', '\n', '\\', 'u', '0', '0', '0', '0' };
    const std::uint8_t expected[]{ 'a', 0u, 0u, '\r', '\n', '\\', 'u', '0', '0', '0', '0', 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) }, 0u);
    expect_output(ctx, result, expected, sizeof(expected));
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 1u);
    TEST_EXPECT(ctx, result.report.modified_utf8_nul_count == 1u);
    TEST_EXPECT(ctx, result.report.normalised_line_endings == 0u);
    TEST_EXPECT(ctx, result.report.encountered_line_endings == text_line_ending_bit(ETextLineEnding::crlf));

    //  The linter does not interpret JSON escapes. Direct live admission uses
    //  the complete bounded text and normalizes only its two decoded zeros.
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView text{ result.output.data(), result.report.logical_text_byte_size };
    const CNodeKey value = document.create_string(text, text);
    TEST_EXPECT(ctx, value.is_valid());
    const std::uint8_t stored[]{ 'a', 0xc0u, 0x80u, 0xc0u, 0x80u, '\r', '\n', '\\', 'u', '0', '0', '0', '0' };
    const CStringView stored_view{ stored, sizeof(stored) };
    TEST_EXPECT(ctx, document.name(value) == stored_view);
    TEST_EXPECT(ctx, document.string_value(value) == stored_view);
    if ((document.name(value).length() == sizeof(stored)) &&
        (document.string_value(value).length() == sizeof(stored)))
    {
        TEST_EXPECT(ctx, document.name(value).string()[sizeof(stored)] == 0u);
        TEST_EXPECT(ctx, document.string_value(value).string()[sizeof(stored)] == 0u);
    }
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_failed_output_has_no_encoding(TTestContext& ctx)
{
    tests::TAllocatorFixture fixture;
    fixture.reject_allocation = true;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext memory_context{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory_context };
        const std::uint8_t input[]{ 'a', 0u, 'b', 0u };
        const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
        TEST_EXPECT(ctx, !result.report.success);
        TEST_EXPECT(ctx, result.report.allocation_failed);
        TEST_EXPECT(ctx, result.report.output_encoding == ETextLintEncoding::none);
        TEST_EXPECT(ctx, result.report.logical_text_byte_size == 0u);
        TEST_EXPECT(ctx, result.report.embedded_nul_count == 1u);
        TEST_EXPECT(ctx, result.output.size() == 0u);
    }
    TEST_EXPECT(ctx, memory_context.is_attribution_empty());
}

void test_cp1252_undefined_failure(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'H', 0x93u, 'Q', 0x81u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    TEST_EXPECT(ctx, !result.report.success && result.output.size() == 0u);
    TEST_EXPECT(ctx, !result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.report.first_failure.reason == ETextLintFailure::undefined_cp1252_byte);
    TEST_EXPECT(ctx, result.report.first_failure.location.available);
    TEST_EXPECT(ctx, result.report.first_failure.location.code_point_column_1_based == 4u);
    TEST_EXPECT(ctx, result.report.first_failure.present);
    TEST_EXPECT(ctx, result.report.cp1252_replacement_character_count == 0u);
    TEST_EXPECT(ctx, result.report.cp1252_positive_evidence != 0u);
    TEST_EXPECT(ctx, result.report.cp1252_counter_evidence != 0u);
    TEST_EXPECT(ctx, result.report.cp1252_confidence == ECP1252Confidence::low);
}

void test_cp1252_confidence_levels(TTestContext& ctx)
{
    const std::uint8_t common_punctuation[]{ 0x91u, 0x92u, 0x93u, 0x94u, 0x96u, 0x97u };
    for (const std::uint8_t value : common_punctuation)
    {
        const CTextLintResult likely = text_linter::lint(CByteConstView{ &value, 1u });
        TEST_EXPECT(ctx, likely.report.success);
        TEST_EXPECT(ctx, likely.report.recovered_as_cp1252);
        TEST_EXPECT(ctx, likely.report.cp1252_confidence == ECP1252Confidence::likely);
        TEST_EXPECT(ctx, (likely.report.cp1252_positive_evidence & text_lint_evidence_bit(ETextLintEvidence::cp1252_common_punctuation)) != 0u);
        TEST_EXPECT(ctx, (likely.report.cp1252_positive_evidence & text_lint_evidence_bit(ETextLintEvidence::cp1252_defined_c1)) == 0u);
    }

    const std::uint8_t other_defined_c1[]{ 0x80u };
    const CTextLintResult moderate = text_linter::lint(CByteConstView{ other_defined_c1, sizeof(other_defined_c1) });
    TEST_EXPECT(ctx, moderate.report.success);
    TEST_EXPECT(ctx, moderate.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, moderate.report.cp1252_confidence == ECP1252Confidence::moderate);
    TEST_EXPECT(ctx, (moderate.report.cp1252_positive_evidence & text_lint_evidence_bit(ETextLintEvidence::cp1252_defined_c1)) != 0u);

    const std::uint8_t ambiguous_printable[]{ 0xe9u };
    const CTextLintResult low = text_linter::lint(CByteConstView{ ambiguous_printable, sizeof(ambiguous_printable) });
    TEST_EXPECT(ctx, low.report.success);
    TEST_EXPECT(ctx, low.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, low.report.cp1252_confidence == ECP1252Confidence::low);
    TEST_EXPECT(ctx, (low.report.cp1252_positive_evidence & text_lint_evidence_bit(ETextLintEvidence::cp1252_printable)) != 0u);

    const std::uint8_t strict_utf8[]{ 'a' };
    const CTextLintResult none = text_linter::lint(CByteConstView{ strict_utf8, sizeof(strict_utf8) });
    TEST_EXPECT(ctx, none.report.success);
    TEST_EXPECT(ctx, !none.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, none.report.cp1252_confidence == ECP1252Confidence::none);
    TEST_EXPECT(ctx, none.report.input_is_pure_ascii);
    TEST_EXPECT(ctx, none.report.output_is_pure_ascii);
    TEST_EXPECT(ctx, !none.report.leading_bom_detected);
}

void test_empty_and_invalid_input_reporting(TTestContext& ctx)
{
    const auto present = text_linter::lint(CStringView{ "", 0u });
    TEST_EXPECT(ctx, present.report.success && present.report.input_is_empty);
    TEST_EXPECT(ctx, present.report.input_byte_size == 0u);
    TEST_EXPECT(ctx, !present.report.first_failure.present && !present.report.first_failure.before_output);
    TEST_EXPECT(ctx, present.report.output_metrics.total_code_points == 0u);
    TEST_EXPECT(ctx, present.report.output_metrics.empty_line_count == 1u);
    TEST_EXPECT(ctx, present.output.size() == 1u && present.output.data()[0] == 0u);
    TEST_EXPECT(ctx, text_linter::lint(CStringView{}).report.first_failure.reason == ETextLintFailure::invalid_input_view);
    const CTextLintResult invalid_empty = text_linter::lint(CByteConstView{});
    TEST_EXPECT(ctx, !invalid_empty.report.success);
    TEST_EXPECT(ctx, invalid_empty.report.output_encoding == ETextLintEncoding::none);
    TEST_EXPECT(ctx, invalid_empty.report.input_view_invalid);
    TEST_EXPECT(ctx, invalid_empty.report.input_is_empty);
    TEST_EXPECT(ctx, invalid_empty.report.input_byte_size == 0u);
    TEST_EXPECT(ctx, invalid_empty.output.size() == 0u);

    const std::uint8_t empty_storage = 0u;
    const CByteConstView absent_views[]{ {}, { nullptr, 0u }, { nullptr, 1u }, { &empty_storage, 0u } };
    for (const auto& view : absent_views)
    {
        const auto absent = text_linter::lint(view);
        TEST_EXPECT(ctx, !view.is_valid() && !absent.report.success);
        TEST_EXPECT(ctx, absent.report.first_failure.reason == ETextLintFailure::invalid_input_view);
        TEST_EXPECT(ctx, absent.report.first_failure.before_output);
        TEST_EXPECT(ctx, !absent.report.first_failure.location.available);
        TEST_EXPECT(ctx, absent.output.size() == 0u);
    }

    const std::uint8_t terminal_zeros[]{ 0u, 0u };
    const std::uint8_t expected[]{ 0u };
    const CTextLintResult empty_payload = text_linter::lint(CByteConstView{ terminal_zeros, sizeof(terminal_zeros) });
    expect_output(ctx, empty_payload, expected, sizeof(expected));
    TEST_EXPECT(ctx, !empty_payload.report.input_view_invalid);
    TEST_EXPECT(ctx, !empty_payload.report.input_is_empty);
    TEST_EXPECT(ctx, empty_payload.report.payload_input_byte_size == 0u);
    TEST_EXPECT(ctx, empty_payload.report.embedded_nul_count == 0u);
    TEST_EXPECT(ctx, empty_payload.report.stripped_terminal_zero_count == 2u);
    TEST_EXPECT(ctx, empty_payload.report.input_metrics.line_count == 1u);
    TEST_EXPECT(ctx, empty_payload.report.input_metrics.empty_line_count == 1u);
    TEST_EXPECT(ctx, empty_payload.report.output_metrics.empty_line_count == 1u);
}

void test_line_content_metrics(TTestContext& ctx)
{
    const std::uint8_t input[]{
        '\n', ' ', '\t', '\n', 0xc2u, 0xa0u, '\n', 'A', 0xc3u, 0xa9u, '\n'
    };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    TEST_EXPECT(ctx, result.report.success);
    TEST_EXPECT(ctx, result.report.input_metrics.line_count == 5u);
    TEST_EXPECT(ctx, result.report.input_metrics.empty_line_count == 2u);
    TEST_EXPECT(ctx, result.report.input_metrics.whitespace_only_line_count == 2u);
    TEST_EXPECT(ctx, result.report.input_metrics.total_code_points == 9u);
    TEST_EXPECT(ctx, result.report.input_metrics.total_likely_printable_code_points == 4u);
    TEST_EXPECT(ctx, result.report.input_metrics.maximum_line_code_points == 2u);
    TEST_EXPECT(ctx, result.report.input_metrics.maximum_line_bytes == 3u);
    TEST_EXPECT(ctx, result.report.input_metrics.maximum_line_likely_printable_code_points == 2u);
    TEST_EXPECT(ctx, result.report.output_metrics.line_count == result.report.input_metrics.line_count);
    TEST_EXPECT(ctx, result.report.output_metrics.empty_line_count == result.report.input_metrics.empty_line_count);
    TEST_EXPECT(ctx, result.report.output_metrics.whitespace_only_line_count == result.report.input_metrics.whitespace_only_line_count);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == result.report.input_metrics.total_code_points);
    TEST_EXPECT(ctx, result.report.output_metrics.total_likely_printable_code_points == result.report.input_metrics.total_likely_printable_code_points);
}

void test_failure_location_after_line_break(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'a', 'b', '\n', 'c', 0x81u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    TEST_EXPECT(ctx, !result.report.success);
    TEST_EXPECT(ctx, !result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.report.first_failure.present);
    TEST_EXPECT(ctx, result.report.first_failure.location.line_1_based == 2u);
    TEST_EXPECT(ctx, result.report.first_failure.location.code_point_column_1_based == 2u);
}

void test_bom_suppresses_cp1252_fallback(TTestContext& ctx)
{
    const std::uint8_t input[]{ 0xefu, 0xbbu, 0xbfu, 0xffu };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    TEST_EXPECT(ctx, !result.report.success);
    TEST_EXPECT(ctx, result.report.output_encoding == ETextLintEncoding::none);
    TEST_EXPECT(ctx, result.report.leading_bom_detected);
    TEST_EXPECT(ctx, !result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.output.size() == 0u);
}

void test_compound_line_endings_have_precedence(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'a', '\n', '\r', 'b' };
    const std::uint8_t unchanged[]{ 'a', '\n', '\r', 'b', 0u };
    const CTextLintResult default_result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, default_result, unchanged, sizeof(unchanged));
    TEST_EXPECT(ctx, default_result.report.encountered_line_endings == text_line_ending_bit(ETextLineEnding::lfcr));
    TEST_EXPECT(ctx, default_result.report.input_metrics.line_count == 1u);

    const std::uint8_t expected[]{ 'a', '\n', 'b', 0u };
    const CTextLintResult normalised = text_linter::lint(
        CByteConstView{ input, sizeof(input) }, text_line_ending_bit(ETextLineEnding::lfcr));
    expect_output(ctx, normalised, expected, sizeof(expected));
    TEST_EXPECT(ctx, normalised.report.output_metrics.line_count == 2u);
}

static bool found(const CTextLintReport& report, const ETextSourceFinding finding)
{
    return (report.source_findings & text_source_finding_bit(finding)) != 0u;
}

static void test_cesu_normalization_and_provenance(TTestContext& ctx)
{
    const std::uint8_t input[]{ 0xedu, 0xa0u, 0x80u, 0xedu, 0xb0u, 0x80u,
        0xedu, 0xafu, 0xbfu, 0xedu, 0xbfu, 0xbfu, 0xc0u, 0x80u,
        0xf0u, 0x9du, 0x84u, 0x9eu, 0u, 'x', 0u };
    const std::uint8_t expected[]{ 0xf0u, 0x90u, 0x80u, 0x80u,
        0xf4u, 0x8fu, 0xbfu, 0xbfu, 0u, 0xf0u, 0x9du, 0x84u, 0x9eu, 0u, 'x', 0u };
    const auto result = text_linter::lint(CByteConstView{ input, sizeof(input) }, k_document_text_lint_line_endings);
    expect_output(ctx, result, expected, sizeof(expected));
    TEST_EXPECT(ctx, result.report.cesu8_pair_count == 2u);
    TEST_EXPECT(ctx, result.report.modified_utf8_nul_count == 1u);
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 1u);
    TEST_EXPECT(ctx, result.report.input_metrics.total_code_points == 6u);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == 6u);
    TEST_EXPECT(ctx, result.report.input_metrics.maximum_line_bytes == sizeof(input) - 1u);
    TEST_EXPECT(ctx, result.report.output_metrics.maximum_line_bytes == sizeof(expected) - 1u);
    TEST_EXPECT(ctx, found(result.report, ETextSourceFinding::cesu8_pair));
    TEST_EXPECT(ctx, found(result.report, ETextSourceFinding::modified_nul));
    TEST_EXPECT(ctx, found(result.report, ETextSourceFinding::literal_nul));
    TEST_EXPECT(ctx, found(result.report, ETextSourceFinding::non_ascii_utf8));
    TEST_EXPECT(ctx, !found(result.report, ETextSourceFinding::cp1252));
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    TEST_EXPECT(ctx, !document.create_string(CStringView{}, CStringView{ input, 6u }).is_valid());
    TEST_EXPECT(ctx, document.create_string(CStringView{}, CStringView{ expected, 4u }).is_valid());

    const std::string malformed[]{ "\xed\xa0\x80", "\xed\xb0\x80", "\xed\xb0\x80\xed\xa0\x80",
        "\xed\xa0\x80\xed\xb0", "\xf0\x8d\xa0\x80\xed\xb0\x80",
        "\xed\xa0\x80\xf0\x8d\xb0\x80", "\xc0\x81", "\xe0\x80\x80" };
    for (const auto& bytes : malformed)
    {
        const std::string marked = std::string("\xef\xbb\xbf") + bytes;
        const auto failure = text_linter::lint(CByteConstView{ reinterpret_cast<const std::uint8_t*>(marked.data()), marked.size() });
        TEST_EXPECT(ctx, !failure.report.success);
        TEST_EXPECT(ctx, failure.report.first_failure.reason == ETextLintFailure::utf8_decode);
        TEST_EXPECT(ctx, failure.report.first_failure.before_output);
        TEST_EXPECT(ctx, failure.report.first_failure.location.available);
        TEST_EXPECT(ctx, failure.report.first_failure.location.line_1_based == 1u);
        TEST_EXPECT(ctx, failure.report.first_failure.location.code_point_column_1_based == 1u);
        TEST_EXPECT(ctx, !found(failure.report, ETextSourceFinding::cesu8_pair));
        TEST_EXPECT(ctx, !found(failure.report, ETextSourceFinding::modified_nul));
    }
    //  A later invalid UTF-8 byte selects CP1252 for the complete payload.
    const std::uint8_t fallback[]{ 0xc0u, 0x80u, 0xedu, 0xa0u, 0x80u, 0xedu, 0xb0u, 0x80u, 0x93u };
    const auto recovered = text_linter::lint(CByteConstView{ fallback, sizeof(fallback) });
    TEST_EXPECT(ctx, recovered.report.success && recovered.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, !recovered.report.first_failure.present);
    TEST_EXPECT(ctx, recovered.report.utf8_attempt_errors != 0u);
    TEST_EXPECT(ctx, found(recovered.report, ETextSourceFinding::utf8_attempt_failed));
    TEST_EXPECT(ctx, !found(recovered.report, ETextSourceFinding::cesu8_pair));
    TEST_EXPECT(ctx, !found(recovered.report, ETextSourceFinding::modified_nul));
    TEST_EXPECT(ctx, !found(recovered.report, ETextSourceFinding::non_ascii_utf8));
    TEST_EXPECT(ctx, recovered.report.cesu8_pair_count == 0u && recovered.report.modified_utf8_nul_count == 0u);
    TEST_EXPECT(ctx, recovered.report.output_metrics.total_code_points == sizeof(fallback));
}

static void test_document_lines_and_failure_coordinates(TTestContext& ctx)
{
    struct CBreak
    {
        const char* bytes;
        ETextLineEnding ending;
        std::size_t code_points;
    };
    const CBreak breaks[]{ { "\n", ETextLineEnding::lf, 1u }, { "\r", ETextLineEnding::cr, 1u },
        { "\r\n", ETextLineEnding::crlf, 2u }, { "\n\r", ETextLineEnding::lfcr, 2u },
        { "\v", ETextLineEnding::vt, 1u }, { "\f", ETextLineEnding::ff, 1u },
        { "\xc2\x85", ETextLineEnding::nel, 1u }, { "\xe2\x80\xa8", ETextLineEnding::ls, 1u },
        { "\xe2\x80\xa9", ETextLineEnding::ps, 1u } };
    for (const auto& line_break : breaks)
    {
        //  Quotes, comment markers, punctuation and escape spellings are opaque.
        const std::string source = std::string("\"//{") + line_break.bytes + "}\\n\"";
        const std::uint8_t expected[]{ '"', '/', '/', '{', '\n', '}', '\\', 'n', '"', 0u };
        const auto linted = text_linter::lint(CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size() }, k_document_text_lint_line_endings);
        expect_output(ctx, linted, expected, sizeof(expected));
        TEST_EXPECT(ctx, linted.report.encountered_line_endings == text_line_ending_bit(line_break.ending));
        TEST_EXPECT(ctx, linted.report.normalised_line_endings == text_line_ending_bit(line_break.ending));
        TEST_EXPECT(ctx, linted.report.output_metrics.total_code_points == 9u);
        TEST_EXPECT(ctx, linted.report.input_metrics.total_code_points == 8u + line_break.code_points);
        const std::string failing = std::string("\xef\xbb\xbf") + source + "\t\xc3\xa9\xcc\x81\xed\xa0\x80\xed\xb0\x80\xff";
        const auto failure = text_linter::lint(CByteConstView{ reinterpret_cast<const std::uint8_t*>(failing.data()), failing.size() }, k_document_text_lint_line_endings);
        TEST_EXPECT(ctx, !failure.report.success && failure.output.size() == 0u);
        TEST_EXPECT(ctx, !failure.report.first_failure.before_output);
        TEST_EXPECT(ctx, failure.report.first_failure.location.available);
        TEST_EXPECT(ctx, failure.report.first_failure.location.line_1_based == 2u);
        TEST_EXPECT(ctx, failure.report.first_failure.location.code_point_column_1_based == 9u);
        TEST_EXPECT(ctx, failure.report.output_metrics.total_code_points == 13u);
        TEST_EXPECT(ctx, failure.report.cesu8_pair_count == 1u);
    }
    const std::uint8_t adjacent[]{ '\r', '\n', '\v', '\f', '\n', '\r', 'x' };
    const std::uint8_t normalized[]{ '\n', '\n', '\n', '\n', 'x', 0u };
    const auto lines = text_linter::lint(CByteConstView{ adjacent, sizeof(adjacent) }, k_document_text_lint_line_endings);
    expect_output(ctx, lines, normalized, sizeof(normalized));
    TEST_EXPECT(ctx, lines.report.output_metrics.line_count == 5u);
    const std::uint8_t undefined[]{ 0x81u, 0x8du, 0x8fu, 0x90u, 0x9du };
    for (const auto byte : undefined)
    {
        const auto first = text_linter::lint(CByteConstView{ &byte, 1u });
        TEST_EXPECT(ctx, !first.report.success && first.report.first_failure.before_output);
        TEST_EXPECT(ctx, first.report.first_failure.reason == ETextLintFailure::undefined_cp1252_byte);
        TEST_EXPECT(ctx, first.report.first_failure.location.available);
        TEST_EXPECT(ctx, first.report.first_failure.location.code_point_column_1_based == 1u);
        const std::uint8_t utf8[]{ 0xc2u, byte };
        TEST_EXPECT(ctx, text_linter::lint(CByteConstView{ utf8, sizeof(utf8) }).report.success);
    }
    const std::uint8_t cp1252[]{ 0x93u, '\r', '\n', 0xe9u, '\t', 0x81u };
    const auto failure = text_linter::lint(CByteConstView{ cp1252, sizeof(cp1252) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, !failure.report.success);
    TEST_EXPECT(ctx, failure.report.first_failure.location.line_1_based == 2u);
    TEST_EXPECT(ctx, failure.report.first_failure.location.code_point_column_1_based == 3u);
    TEST_EXPECT(ctx, failure.report.output_metrics.total_code_points == 4u);
}

struct CFailingAllocator
{
    std::size_t calls = 0u;
    std::size_t fail_on = 0u;
};

static void* MV_STD_ABI_CALL allocate_until_failure(void* state, const std::size_t alignment, const std::size_t size) noexcept
{
    auto& fixture = *static_cast<CFailingAllocator*>(state);
    if (fixture.calls++ == fixture.fail_on)
    {
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static void test_resource_failure_cursors(TTestContext& ctx)
{
    const std::string source = std::string("a\r\n") + std::string(6000u, '\x93');
    bool completed = false;
    bool failed_after_prefix = false;
    for (std::size_t fail_on = 0u; (fail_on < 16u) && !completed; ++fail_on)
    {
        CFailingAllocator fixture{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &fixture, &allocate_until_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            const auto result = text_linter::lint(CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size() }, k_document_text_lint_line_endings);
            completed = result.report.success;
            if (!completed)
            {
                TEST_EXPECT(ctx, result.report.first_failure.reason == ETextLintFailure::allocation_failed);
                TEST_EXPECT(ctx, result.report.first_failure.location.available);
                TEST_EXPECT(ctx, result.output.size() == 0u && result.report.logical_text_byte_size == 0u);
                if (result.report.first_failure.before_output)
                {
                    TEST_EXPECT(ctx, result.report.first_failure.location.line_1_based == 1u);
                    TEST_EXPECT(ctx, result.report.first_failure.location.code_point_column_1_based == 1u);
                }
                else
                {
                    failed_after_prefix = true;
                    TEST_EXPECT(ctx, result.report.first_failure.location.line_1_based == 2u);
                    TEST_EXPECT(ctx, result.report.first_failure.location.code_point_column_1_based > 1u);
                    TEST_EXPECT(ctx, found(result.report, ETextSourceFinding::cp1252));
                    TEST_EXPECT(ctx, result.report.normalised_line_endings == text_line_ending_bit(ETextLineEnding::crlf));
                }
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed && failed_after_prefix);

    //  End the text immediately before the buffer's next growth recommendation.
    //  Rejected growth for its physical terminator must retain the EOF cursor.
    const std::size_t capacity = memory::k_buffer_growth_policy_min_capacity;
    std::size_t threshold = 1u;
    while (memory::buffer_growth_policy(threshold, memory::k_byte_size_ceiling) <= capacity)
    {
        ++threshold;
    }
    const std::size_t payload_bytes = threshold - 1u;
    const std::size_t wide_count = (payload_bytes - 2u) / 3u;
    const std::size_t ascii_count = (payload_bytes - 2u) % 3u;
    const std::string full = std::string("a\r\n") + std::string(wide_count, '\x93') + std::string(ascii_count, 'z');
    CFailingAllocator fixture{ 0u, 2u };
    memory::CMemoryAllocator allocator{ &fixture, &allocate_until_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        const auto result = text_linter::lint(CByteConstView{ reinterpret_cast<const std::uint8_t*>(full.data()), full.size() }, k_document_text_lint_line_endings);
        TEST_EXPECT(ctx, !result.report.success && !result.report.first_failure.before_output);
        TEST_EXPECT(ctx, result.report.first_failure.reason == ETextLintFailure::allocation_failed);
        TEST_EXPECT(ctx, result.report.first_failure.location.line_1_based == 2u);
        TEST_EXPECT(ctx, result.report.first_failure.location.code_point_column_1_based == wide_count + ascii_count + 1u);
        TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == wide_count + ascii_count + 2u);
        TEST_EXPECT(ctx, result.output.size() == 0u && result.report.logical_text_byte_size == 0u);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

} // namespace text_linter_tests

int run_text_linter_tests()
{
    text_linter_tests::TTestContext ctx;
    text_linter_tests::test_utf8_bom_terminal_zero_and_default_lines(ctx);
    text_linter_tests::test_modified_null_is_payload_not_terminator(ctx);
    text_linter_tests::test_literal_interior_zero_is_counted(ctx);
    text_linter_tests::test_cp1252_embedded_zeros_are_counted_once(ctx);
    text_linter_tests::test_generic_preservation_and_live_nul_storage(ctx);
    text_linter_tests::test_failed_output_has_no_encoding(ctx);
    text_linter_tests::test_cp1252_undefined_failure(ctx);
    text_linter_tests::test_cp1252_confidence_levels(ctx);
    text_linter_tests::test_empty_and_invalid_input_reporting(ctx);
    text_linter_tests::test_line_content_metrics(ctx);
    text_linter_tests::test_failure_location_after_line_break(ctx);
    text_linter_tests::test_bom_suppresses_cp1252_fallback(ctx);
    text_linter_tests::test_compound_line_endings_have_precedence(ctx);
    text_linter_tests::test_cesu_normalization_and_provenance(ctx);
    text_linter_tests::test_document_lines_and_failure_coordinates(ctx);
    text_linter_tests::test_resource_failure_cursors(ctx);
    std::cout << "TextLinter: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
