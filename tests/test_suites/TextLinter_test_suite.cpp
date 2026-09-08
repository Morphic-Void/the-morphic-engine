//
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
    const std::uint8_t input[]{ 0u, 'a', 0u, 0x80u, 'b', 0u, 0x81u, 0u };
    const std::uint8_t expected[]{ 0u, 'a', 0u, 0xe2u, 0x82u, 0xacu, 'b', 0u, 0xefu, 0xbfu, 0xbdu, 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, result, expected, sizeof(expected));
    TEST_EXPECT(ctx, result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.report.embedded_nul_count == 3u);
    TEST_EXPECT(ctx, result.report.modified_utf8_nul_count == 0u);
    TEST_EXPECT(ctx, result.report.stripped_terminal_zero_count == 1u);
    TEST_EXPECT(ctx, result.report.cp1252_replacement_character_count == 1u);
    TEST_EXPECT(ctx, result.report.first_failure.location.byte_offset_in_file_0_based == 3u);
    TEST_EXPECT(ctx, result.report.output_metrics.total_code_points == 7u);
}

void test_document_input_preserves_content_and_live_nul_storage(TTestContext& ctx)
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

void test_cp1252_recovery_and_undefined_replacement(TTestContext& ctx)
{
    const std::uint8_t input[]{ 'H', 0x93u, 'Q', 0x81u };
    const std::uint8_t expected[]{ 'H', 0xe2u, 0x80u, 0x9cu, 'Q', 0xefu, 0xbfu, 0xbdu, 0u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    expect_output(ctx, result, expected, sizeof(expected));
    TEST_EXPECT(ctx, result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.report.first_failure.present);
    TEST_EXPECT(ctx, result.report.cp1252_replacement_character_count == 1u);
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
    const CTextLintResult invalid_empty = text_linter::lint(CByteConstView{});
    TEST_EXPECT(ctx, !invalid_empty.report.success);
    TEST_EXPECT(ctx, invalid_empty.report.output_encoding == ETextLintEncoding::none);
    TEST_EXPECT(ctx, invalid_empty.report.input_view_invalid);
    TEST_EXPECT(ctx, invalid_empty.report.input_is_empty);
    TEST_EXPECT(ctx, invalid_empty.report.input_byte_size == 0u);
    TEST_EXPECT(ctx, invalid_empty.output.size() == 0u);

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
    const std::uint8_t input[]{ 'a', 'b', '\n', 'c', 0xc3u };
    const CTextLintResult result = text_linter::lint(CByteConstView{ input, sizeof(input) });
    TEST_EXPECT(ctx, result.report.success);
    TEST_EXPECT(ctx, result.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, result.report.first_failure.present);
    TEST_EXPECT(ctx, result.report.first_failure.location.line_1_based == 2u);
    TEST_EXPECT(ctx, result.report.first_failure.location.code_point_column_1_based == 2u);
    TEST_EXPECT(ctx, result.report.first_failure.location.byte_offset_in_line_0_based == 1u);
    TEST_EXPECT(ctx, result.report.first_failure.location.byte_offset_in_file_0_based == 4u);
    TEST_EXPECT(ctx, result.report.first_failure.location.line_start_byte_offset_in_file_0_based == 3u);
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

} // namespace text_linter_tests

int run_text_linter_tests()
{
    text_linter_tests::TTestContext ctx;
    text_linter_tests::test_utf8_bom_terminal_zero_and_default_lines(ctx);
    text_linter_tests::test_modified_null_is_payload_not_terminator(ctx);
    text_linter_tests::test_literal_interior_zero_is_counted(ctx);
    text_linter_tests::test_cp1252_embedded_zeros_are_counted_once(ctx);
    text_linter_tests::test_document_input_preserves_content_and_live_nul_storage(ctx);
    text_linter_tests::test_failed_output_has_no_encoding(ctx);
    text_linter_tests::test_cp1252_recovery_and_undefined_replacement(ctx);
    text_linter_tests::test_cp1252_confidence_levels(ctx);
    text_linter_tests::test_empty_and_invalid_input_reporting(ctx);
    text_linter_tests::test_line_content_metrics(ctx);
    text_linter_tests::test_failure_location_after_line_break(ctx);
    text_linter_tests::test_bom_suppresses_cp1252_fallback(ctx);
    text_linter_tests::test_compound_line_endings_have_precedence(ctx);
    std::cout << "TextLinter: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
