
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentStructure_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "tests/test_suites/DocumentStructure_test_suite.hpp"

#include <iostream>
#include <string>

#include "data_model/baked_document.hpp"
#include "data_model/document_structure.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/live_document.hpp"
#include "text/text_linter.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

//  Group suite implementation names; static helpers remain local to this file.
namespace document_structure_tests
{

using tests::TTestContext;
using document_text::ESyntaxError;
using document_text::ETokenKind;

static CStringView text_view(const std::string& text)
{
    return CStringView{ text.data(), text.size() };
}

static CDocumentStructureReport check(const std::string& text, CDocumentStructureEstimates* estimates = nullptr)
{
    return document_structure::check(text_view(text), estimates);
}

static void expect_failure(TTestContext& ctx, const CDocumentStructureReport& report, const EDocumentStructureStatus status)
{
    TEST_EXPECT(ctx, report.status == status);
}

static void expect_no_estimates(TTestContext& ctx, const CDocumentStructureEstimates& estimates)
{
    TEST_EXPECT(ctx, estimates.value_count == 0u && estimates.object_count == 0u && estimates.array_count == 0u);
    TEST_EXPECT(ctx, estimates.named_entry_count == 0u && estimates.string_source_byte_size == 0u && estimates.maximum_depth == 0u);
}

static void test_partial_findings(TTestContext& ctx)
{
    struct CCase
    {
        const char* text;
        ESyntaxError error;
        std::uint32_t findings;
    };
    const CCase cases[]{
        { "/*head*/ a:'line\n\t", ESyntaxError::unterminated_string, EDocumentFinding::comments |
            EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names | EDocumentFinding::single_quotes |
            EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::raw_quoted_controls },
        { "{n:+#F,a:[1,],bad:} /*unexamined*/", ESyntaxError::expected_value, EDocumentFinding::unquoted_names |
            EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix |
            EDocumentFinding::trailing_commas },
        { "{n:+0x,b:}", ESyntaxError::expected_value, EDocumentFinding::unquoted_names | EDocumentFinding::unquoted_strings },
        { "{\"\":}", ESyntaxError::expected_value, document_finding_bit(EDocumentFinding::empty_member_name) },
        { "{} /*unfinished", ESyntaxError::unterminated_comment, document_finding_bit(EDocumentFinding::comments) }
    };
    for (const auto& item : cases)
    {
        CDocumentStructureEstimates estimates{ 1u, 1u, 1u, 1u, 1u, 1u };
        const auto report = check(item.text, &estimates);
        TEST_CASE_EXPECT_TRUE(ctx, item.text, report.syntax_error == item.error);
        TEST_CASE_EXPECT_EQ(ctx, item.text, report.findings, item.findings);
        TEST_EXPECT(ctx, !report.succeeded() && report.structure_start.available && report.failure_point.available);
        expect_no_estimates(ctx, estimates);
    }
    CDocumentStructureEstimates estimates{ 1u, 1u, 1u, 1u, 1u, 1u };
    const auto absent = document_structure::check(CStringView{}, &estimates);
    TEST_EXPECT(ctx, absent.status == EDocumentStructureStatus::invalid_input_view && absent.findings == 0u);
    expect_no_estimates(ctx, estimates);
    TEST_EXPECT(ctx, check("").findings == 0u);
    TEST_EXPECT(ctx, check("// empty").findings == document_finding_bit(EDocumentFinding::comments));
    TEST_EXPECT(ctx, check("{\"s\":\"\\u0000\\n\\t\"}").findings == document_finding_bit(EDocumentFinding::logical_nul));
    TEST_EXPECT(ctx, check("{\"s\":\"\n\"}").findings == document_finding_bit(EDocumentFinding::raw_quoted_line_breaks));
    TEST_EXPECT(ctx, check("{\"s\":\"\t\"}").findings == document_finding_bit(EDocumentFinding::raw_quoted_controls));
    TEST_EXPECT(ctx, check("{\"s\":\"0x1 #1 +1 0b1\"}").findings == 0u);
}

static void test_grammar(TTestContext& ctx)
{
    const char* const accepted[]{ "", " \r\n\t", "// empty", "{}", "[]", "1", "true", "'text'", "{\"a\":[]}",
        "{\"n\":123,\"f\":-0.0e+12,\"b\":true,\"z\":null,\"s\":\"text\"}",
        "a:1, enabled:true", "{a:1,}", "a:1,", "{a:[1,2,]}",
        "{'x':'can\\'t', \"y\":'\"quoted\"'}", "{true:false,null:true,false:null}",
        "/*head*/{a:1/*value*/,b:[true//line\n,false]}/*tail*/",
        "{a:[{n:1},{s:'text'},{z:null},{b:true},{o:{}},{a:[]}]}",
        "{a:\"[ } // /* \\u0000\",b:'a\nb'}", "{a:+0,b:-#FF,c:0Xfa,d:+0B10}",
        "{a:0,b:-0,c:0.1,d:0e0,e:1E-9}",
        "{a:truth}", "{a:True}", "{1:2}",
        "{a:01}", "{a:-01}", "{a:.5}", "{a:1.}", "{a:1e}", "{a:1e+}", "{a:1e-}",
        "{a:+}", "{a:--1}", "{a:0x}", "{a:#}", "{a:0b2}", "{a:0x1g}", "{a:0b10.1}",
        "{a:1e2x}", "{a:1_000}", "{a:NaN}", "{a:Infinity}", "{a:1/2}", "{a:/}" };
    for (const char* text : accepted)
    {
        TEST_CASE_EXPECT_TRUE(ctx, text, check(text).succeeded());
    }
    TEST_EXPECT(ctx, check("{\"\":1,\"\":2}").succeeded());

    const char* const rejected[]{ "[a:1]", "{a:[n:1]}",
        "{a:[\"n\":1]}", "{a}", "{a:}", "{a:1 b:2}", "{a:1,,}", "{,}", "{a:[,]}",
        "{a:[1,,2]}", "{a:[1 2]}", "{a:[1}}", "{a:1]", "{a:1", "{a:[", "a:",
        "{a:1} {}", "{a:1},", "a:1}", "{a:\"\\q\"}", "{a:\"\\'\"}", "{a:'\\x41'}", "{a:\"\\u12\"}",
        "{a:\"\\uD800\"}", "{a:\"\\uDC00\"}", "{a:\"\\uD800\\u0041\"}",
        "{a:\"\\uD800\\uDC0z\"}", "{a:'unterminated}", "/*", "/*/", "{} /*tail" };
    for (const char* text : rejected)
    {
        const auto report = check(text);
        expect_failure(ctx, report, EDocumentStructureStatus::syntax_error);
        TEST_EXPECT(ctx, report.syntax_error != ESyntaxError::none);
        TEST_EXPECT(ctx, report.failure_point.available);
    }
}

static void test_input_presence(TTestContext& ctx)
{
    const auto absent = document_structure::check(CStringView{});
    expect_failure(ctx, absent, EDocumentStructureStatus::invalid_input_view);
    TEST_EXPECT(ctx, absent.syntax_error == ESyntaxError::none && !absent.failure_point.available);
    const char terminator = '\0';
    const CStringView present{ &terminator, 0u };
    CDocumentStructureEstimates estimates;
    const auto empty = document_structure::check(present, &estimates);
    TEST_EXPECT(ctx, empty.succeeded());
    TEST_EXPECT(ctx, estimates.value_count == 1u && estimates.object_count == 1u);
    TEST_EXPECT(ctx, estimates.array_count == 0u && estimates.named_entry_count == 0u);
    TEST_EXPECT(ctx, estimates.string_source_byte_size == 0u && estimates.maximum_depth == 1u);
    TEST_EXPECT(ctx, empty.findings == 0u);
    TEST_EXPECT(ctx, empty.syntax_error == ESyntaxError::none && !empty.failure_point.available);
    document_text::CScanner scanner(present);
    const auto end = scanner.next();
    TEST_EXPECT(ctx, end.kind == ETokenKind::end && end.offset == 0u && end.size == 0u);
    TEST_EXPECT(ctx, scanner.findings() == 0u);
    //  The same backing byte, when included in the logical extent, is content.
    const auto nul = document_structure::check(CStringView{ &terminator, 1u }, &estimates);
    TEST_EXPECT(ctx, nul.succeeded() && nul.findings == (EDocumentFinding::logical_nul | EDocumentFinding::unquoted_strings));
    TEST_EXPECT(ctx, estimates.value_count == 2u && estimates.array_count == 1u && estimates.object_count == 0u);
}

static void test_policy_boundary(TTestContext& ctx)
{
    const std::string huge(8192u, '9');
    TEST_EXPECT(ctx, check("{n:" + huge + ",n:-" + huge + ",n:1e+" + huge + ",n:1e-" + huge + "}").succeeded());
    TEST_EXPECT(ctx, check("{n:0x" + std::string(8192u, 'f') + ",n:0b" + std::string(8192u, '1') + "}").succeeded());
    TEST_EXPECT(ctx, check("{n:18446744073709551616,n:-9223372036854775809,n:1e99999,n:1e-99999}").succeeded());
    //  Protocol interpretation belongs to parsing, even with duplicate,
    //  unsupported, missing or incorrectly typed metadata fields.
    TEST_EXPECT(ctx, check("{\"$morphic\":{v:999,v:1,type:false,extra:1,values:null}}").succeeded());
    TEST_EXPECT(ctx, check("{\"$morphic\":null,\"$morphic\":{},\"$$morphic\":1}").succeeded());
    TEST_EXPECT(ctx, check("{\"a\":1,\"\\u0061\":2,a:3}").succeeded());
}

static void test_reports(TTestContext& ctx)
{
    CDocumentStructureEstimates estimates;
    const auto strict = check("{\"a\":[1,{\"b\":\"x\"}],\"c\":true}", &estimates);
    TEST_EXPECT(ctx, strict.succeeded());
    TEST_EXPECT(ctx, strict.findings == 0u);
    TEST_EXPECT(ctx, !strict.failure_point.available && strict.syntax_error == ESyntaxError::none);
    TEST_EXPECT(ctx, estimates.value_count == 6u);
    TEST_EXPECT(ctx, estimates.object_count == 2u && estimates.array_count == 1u);
    TEST_EXPECT(ctx, estimates.named_entry_count == 3u);
    TEST_EXPECT(ctx, estimates.string_source_byte_size == 12u);
    TEST_EXPECT(ctx, estimates.maximum_depth == 3u);
    const auto relaxed = check("/*c*/ a:'x\ny', b:[+#F,0b1,],");
    TEST_EXPECT(ctx, relaxed.succeeded());
    TEST_EXPECT(ctx, relaxed.findings == (EDocumentFinding::comments |
        EDocumentFinding::single_quotes | EDocumentFinding::unquoted_names | EDocumentFinding::trailing_commas |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::implicit_body | EDocumentFinding::explicit_plus |
        EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix | EDocumentFinding::binary));
    TEST_EXPECT(ctx, check("{\"a\":\"comments // and 0xFF and 'quoted'\"}").findings == 0u);

    struct CFailure
    {
        const char* text;
        ESyntaxError error;
        std::size_t offset;
    };
    const CFailure failures[]{ { "{a:1e+ 2}", ESyntaxError::expected_separator, 7u },
        { "{a:[}", ESyntaxError::mismatched_delimiter, 4u },
        { "{a 1}", ESyntaxError::expected_colon, 3u },
        { "{a:}", ESyntaxError::expected_value, 3u },
        { "{a:[1 2]}", ESyntaxError::expected_separator, 6u },
        { "{a:", ESyntaxError::unexpected_end, 3u },
        { "{} true", ESyntaxError::trailing_content, 3u },
        { "{a:'x", ESyntaxError::unterminated_string, 5u },
        { "{a:\"\\q\"}", ESyntaxError::invalid_escape, 5u },
        { "/*", ESyntaxError::unterminated_comment, 2u } };
    for (const auto& failure : failures)
    {
        const auto report = check(failure.text);
        expect_failure(ctx, report, EDocumentStructureStatus::syntax_error);
        TEST_EXPECT(ctx, report.syntax_error == failure.error);
        TEST_EXPECT(ctx, report.failure_point.available && report.failure_point.code_point_column_1_based == failure.offset + 1u);
    }
}

static void test_lexical_reuse(TTestContext& ctx)
{
    struct CLocationCase
    {
        const char* source;
        std::size_t start_column;
        std::size_t failure_column;
    };
    const CLocationCase locations[]{ { "{\"s\":\"abc", 6u, 10u },
        { "{\"a\":[1}", 6u, 8u }, { "{\"a\":{\"b\":1]}", 6u, 12u },
        { "{\"a\" 1}", 2u, 6u }, { "{", 1u, 2u }, { "{} /*tail", 4u, 10u },
        { "{a:\"\\q\"}", 4u, 6u }, { "{a:}", 2u, 4u } };
    for (const auto& item : locations)
    {
        const auto report = check(item.source);
        TEST_EXPECT(ctx, !report.succeeded());
        TEST_EXPECT(ctx, report.structure_start.available && report.failure_point.available);
        TEST_EXPECT(ctx, report.structure_start.line_1_based == 1u && report.failure_point.line_1_based == 1u);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.structure_start.code_point_column_1_based, item.start_column);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.failure_point.code_point_column_1_based, item.failure_column);
    }
    const std::string multiline = "\xef\xbb\xbf{\r\n\"s\":\"\xc3\xa9\xcc\x81\t\xed\xa0\xbd\xed\xb8\x80\xe2\x80\xa8x";
    const auto linted = text_linter::lint(CByteConstView{ reinterpret_cast<const std::uint8_t*>(multiline.data()), multiline.size() }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, linted.report.success);
    const auto unterminated = document_structure::check(CStringView{ linted.output.data(), linted.report.logical_text_byte_size });
    TEST_EXPECT(ctx, unterminated.syntax_error == ESyntaxError::unterminated_string);
    TEST_EXPECT(ctx, unterminated.structure_start.line_1_based == 2u && unterminated.structure_start.code_point_column_1_based == 5u);
    TEST_EXPECT(ctx, unterminated.failure_point.line_1_based == 3u && unterminated.failure_point.code_point_column_1_based == 2u);

    struct CEscape
    {
        const char* text;
        std::uint32_t scalar;
    };
    const CEscape escapes[]{ { "\\u0000", 0u }, { "\\uD800\\uDC00", 0x10000u },
        { "\\uDBFF\\uDFFF", 0x10ffffu }, { "\\u00e9", 0xe9u }, { "\\n", '\n' },
        { "\\b", '\b' }, { "\\f", '\f' }, { "\\r", '\r' }, { "\\t", '\t' },
        { "\\/", '/' }, { "\\\\", '\\' }, { "\\\"", '"' }, { "\\'", '\'' } };
    for (const auto& escape : escapes)
    {
        const std::string text(escape.text);
        std::size_t offset = 0u;
        std::uint32_t scalar = 1u;
        const char quote = (escape.scalar == '"') ? '"' : '\'';
        TEST_EXPECT(ctx, document_text::read_escape(text_view(text), offset, quote, scalar) == ESyntaxError::none);
        TEST_EXPECT(ctx, scalar == escape.scalar && offset == text.size());
        TEST_EXPECT(ctx, check(std::string("{a:") + quote + text + quote + "}").succeeded());
    }
    const std::string text = " /* c */ 'name' : +0xFF, n:1e9999999999999999999999";
    document_text::CScanner scanner(text_view(text));
    const auto name = scanner.next();
    TEST_EXPECT(ctx, name.kind == ETokenKind::string && text.substr(name.offset, name.size) == "'name'");
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::colon);
    const auto number = scanner.next();
    TEST_EXPECT(ctx, number.kind == ETokenKind::integer && text.substr(number.offset, number.size) == "+0xFF");
    TEST_EXPECT(ctx, number.value_findings == (EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal));
    TEST_EXPECT(ctx, scanner.findings() == (EDocumentFinding::comments | EDocumentFinding::single_quotes));
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::comma);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::unquoted_string);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::colon);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::floating_point);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::end);
}

static void test_unquoted_boundaries(TTestContext& ctx)
{
    struct CBoundary
    {
        char separator;
        ETokenKind next;
    };
    const CBoundary boundaries[]{ { '{', ETokenKind::object_begin }, { '}', ETokenKind::object_end },
        { '[', ETokenKind::array_begin }, { ']', ETokenKind::array_end }, { ':', ETokenKind::colon },
        { ',', ETokenKind::comma }, { '"', ETokenKind::error }, { ' ', ETokenKind::end },
        { '\t', ETokenKind::end }, { '\r', ETokenKind::end }, { '\n', ETokenKind::end } };
    for (const auto& item : boundaries)
    {
        const std::string source = std::string("text") + item.separator;
        document_text::CScanner scanner(text_view(source));
        const auto token = scanner.next();
        TEST_EXPECT(ctx, token.kind == ETokenKind::unquoted_string && token.offset == 0u && token.size == 4u);
        TEST_EXPECT(ctx, token.value_findings == document_finding_bit(EDocumentFinding::unquoted_strings));
        TEST_EXPECT(ctx, scanner.next().kind == item.next);
    }
    const auto location = check("{s:\xc3\xa9\\u0020x\"tail\"}");
    TEST_EXPECT(ctx, location.syntax_error == ESyntaxError::expected_separator);
    TEST_EXPECT(ctx, location.structure_start.available && location.structure_start.code_point_column_1_based == 1u);
    TEST_EXPECT(ctx, location.failure_point.available && location.failure_point.code_point_column_1_based == 12u);
    TEST_EXPECT(ctx, location.findings == (EDocumentFinding::unquoted_names | EDocumentFinding::unquoted_strings));

    const auto invalid_escape = check("{s:\xc3\xa9\\q}");
    TEST_EXPECT(ctx, invalid_escape.syntax_error == ESyntaxError::invalid_escape);
    TEST_EXPECT(ctx, invalid_escape.structure_start.code_point_column_1_based == 4u);
    TEST_EXPECT(ctx, invalid_escape.failure_point.code_point_column_1_based == 6u);

    CDocumentStructureEstimates estimates;
    const auto report = check("{a\\u0020b:c\\u002Cd}", &estimates);
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, estimates.named_entry_count == 1u && estimates.value_count == 2u);
    TEST_EXPECT(ctx, estimates.string_source_byte_size == 16u);
    const std::string bounded = "{a:word\\u002Cword}";
    for (std::size_t count = 1u; count < bounded.size(); ++count)
    {
        TEST_EXPECT(ctx, !document_structure::check(CStringView{ bounded.data(), count }).succeeded());
    }
}

static void test_semicolon_comments(TTestContext& ctx)
{
    const char* const accepted[]{ ";", ";note", ";head\n{}", "{\"s\":;note\n1}",
        "{;\"'{}[]:,/* \\q\n\"s\":1};tail" };
    for (const char* source : accepted)
    {
        const auto report = check(source);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.succeeded());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::comments));
    }
    const char* const endings[]{ "\n", "\r", "\r\n" };
    for (const char* ending : endings)
    {
        const std::string missing_source = std::string("{\"s\":;note") + ending + "  }";
        const auto missing_linted = text_linter::lint(text_view(missing_source), k_document_text_lint_line_endings);
        TEST_EXPECT(ctx, missing_linted.report.success);
        const auto missing = document_structure::check(CStringView{ missing_linted.output.data(), missing_linted.report.logical_text_byte_size });
        TEST_EXPECT(ctx, !missing.succeeded() && missing.syntax_error == ESyntaxError::expected_value);
        TEST_EXPECT(ctx, missing.findings == document_finding_bit(EDocumentFinding::comments));
        TEST_EXPECT(ctx, missing.failure_point.available && missing.failure_point.line_1_based == 2u &&
            missing.failure_point.code_point_column_1_based == 3u);

        const std::string source = std::string(";\"'{}[]:,/* \\q") + ending + "  alpha;beta";
        const auto linted = text_linter::lint(text_view(source), k_document_text_lint_line_endings);
        TEST_EXPECT(ctx, linted.report.success);
        document_text::CScanner scanner(CStringView{ linted.output.data(), linted.report.logical_text_byte_size });
        const auto token = scanner.next();
        TEST_EXPECT(ctx, token.kind == ETokenKind::unquoted_string && token.size == 10u);
        TEST_EXPECT(ctx, token.location.available && token.location.line_1_based == 2u &&
            token.location.code_point_column_1_based == 3u);
        TEST_EXPECT(ctx, scanner.findings() == document_finding_bit(EDocumentFinding::comments));
        TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::end);
    }
    const auto embedded = check("{alpha;beta:alpha;beta}");
    TEST_EXPECT(ctx, embedded.succeeded());
    TEST_EXPECT(ctx, embedded.findings == (EDocumentFinding::unquoted_names | EDocumentFinding::unquoted_strings));
}

static void test_name_line_breaks(TTestContext& ctx)
{
    const char* const escapes[]{ "\\n", "\\r", "\\f", "\\u000A", "\\u000D", "\\u000B", "\\u000C",
        "\\u0085", "\\u2028", "\\u2029" };
    for (const char* escape : escapes)
    {
        for (const char* quote : { "\"", "'", "" })
        {
            const std::string source = std::string("{") + quote + "\xc3\xa9" + escape + "tail" + quote + ":1} /*unexamined*/";
            CDocumentStructureEstimates estimates{ 1u, 1u, 1u, 1u, 1u, 1u };
            const auto report = check(source, &estimates);
            TEST_CASE_EXPECT_TRUE(ctx, source.c_str(), !report.succeeded() && report.syntax_error == ESyntaxError::newline_in_name);
            const std::uint32_t findings = (*quote == '\'') ? document_finding_bit(EDocumentFinding::single_quotes) :
                (*quote == '\0') ? document_finding_bit(EDocumentFinding::unquoted_names) : 0u;
            TEST_EXPECT(ctx, report.findings == findings);
            TEST_EXPECT(ctx, report.structure_start.available && report.structure_start.line_1_based == 1u &&
                report.structure_start.code_point_column_1_based == 2u);
            TEST_EXPECT(ctx, report.failure_point.available && report.failure_point.line_1_based == 1u &&
                report.failure_point.code_point_column_1_based == ((*quote == '\0') ? 3u : 4u));
            expect_no_estimates(ctx, estimates);
            const auto value = check(std::string("{\"s\":") + quote + "a" + escape + "b" + quote + "}");
            TEST_EXPECT(ctx, value.succeeded());
            TEST_EXPECT(ctx, (value.findings & document_finding_bit(EDocumentFinding::raw_quoted_line_breaks)) == 0u);
        }
    }
    const auto literal = check("{\"\xc3\xa9\nname\":1}");
    TEST_EXPECT(ctx, literal.syntax_error == ESyntaxError::newline_in_name);
    TEST_EXPECT(ctx, literal.structure_start.line_1_based == 1u && literal.structure_start.code_point_column_1_based == 2u);
    TEST_EXPECT(ctx, literal.failure_point.line_1_based == 1u && literal.failure_point.code_point_column_1_based == 4u);
    TEST_EXPECT(ctx, literal.findings == document_finding_bit(EDocumentFinding::raw_quoted_line_breaks));
    TEST_EXPECT(ctx, check("{\"a\\tb\\bc\\u0001d\\u0000\":1}").succeeded());
    TEST_EXPECT(ctx, check("{\"a\\\\nb\":1}").succeeded());

    const std::string source = "\"a\nb\" \"a\\nb\" 'plain' a\\nb";
    document_text::CScanner scanner(text_view(source));
    const auto raw = scanner.next();
    const auto escaped = scanner.next();
    const auto plain = scanner.next();
    const auto unquoted = scanner.next();
    TEST_EXPECT(ctx, raw.literal_line_break != 0u && raw.first_line_break.available);
    TEST_EXPECT(ctx, escaped.literal_line_break == 0u && escaped.first_line_break.available);
    TEST_EXPECT(ctx, plain.literal_line_break == 0u && !plain.first_line_break.available);
    TEST_EXPECT(ctx, unquoted.literal_line_break == 0u && unquoted.first_line_break.available);
    TEST_EXPECT(ctx, escaped.location.line_1_based == 2u && escaped.location.code_point_column_1_based == 4u);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::end);
}

static void test_root_inference(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        bool object;
        std::size_t children;
        std::uint32_t findings;
    };
    const CCase cases[]{
        { "", true, 0u, 0u }, { " \n\t", true, 0u, 0u },
        { ";empty", true, 0u, document_finding_bit(EDocumentFinding::comments) },
        { "{}", true, 0u, 0u }, { "[]", false, 0u, 0u }, { "[1,true,null]", false, 3u, 0u },
        { "\"a\":1", true, 1u, document_finding_bit(EDocumentFinding::implicit_body) },
        { "123:1", true, 1u, EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "0x10:1", true, 1u, EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "\"\":1", true, 1u, EDocumentFinding::implicit_body | EDocumentFinding::empty_member_name },
        { "42", false, 1u, 0u }, { "1.5", false, 1u, 0u }, { "true", false, 1u, 0u },
        { "false", false, 1u, 0u }, { "null", false, 1u, 0u }, { "\"hello\"", false, 1u, 0u },
        { "1,true,\"hello\"", false, 3u, document_finding_bit(EDocumentFinding::implicit_body) },
        { "1,", false, 1u, document_finding_bit(EDocumentFinding::trailing_commas) },
        { "1,2,", false, 2u, EDocumentFinding::implicit_body | EDocumentFinding::trailing_commas },
        { "+#F", false, 1u, EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix },
        { "a\\u003Ab", false, 1u, document_finding_bit(EDocumentFinding::unquoted_strings) },
        { "a\\u003Ab:1", true, 1u, EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "\"a\" /* : ignored */ :1", true, 1u, EDocumentFinding::implicit_body | EDocumentFinding::comments },
        { "\"a\" /* : ignored */", false, 1u, document_finding_bit(EDocumentFinding::comments) },
        { "\"a\";ignored\n:1", true, 1u, EDocumentFinding::implicit_body | EDocumentFinding::comments },
        { "\"line\nbreak\"", false, 1u, document_finding_bit(EDocumentFinding::raw_quoted_line_breaks) }
    };
    for (const auto& item : cases)
    {
        CDocumentStructureEstimates estimates;
        const auto report = check(item.source, &estimates);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.succeeded());
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.findings, item.findings);
        TEST_EXPECT(ctx, estimates.value_count == item.children + 1u);
        TEST_EXPECT(ctx, estimates.object_count == (item.object ? 1u : 0u));
        TEST_EXPECT(ctx, estimates.array_count == (item.object ? 0u : 1u));
        TEST_EXPECT(ctx, estimates.named_entry_count == (item.object ? item.children : 0u));
        TEST_EXPECT(ctx, estimates.maximum_depth == 1u);
        TEST_EXPECT(ctx, !report.structure_start.available && !report.failure_point.available);
    }
    struct CFailure
    {
        const char* source;
        ESyntaxError error;
        std::size_t start_column;
        std::size_t failure_column;
    };
    const CFailure failures[]{
        { "1 2", ESyntaxError::expected_separator, 1u, 3u },
        { "[1}", ESyntaxError::mismatched_delimiter, 1u, 3u },
        { "[1", ESyntaxError::unexpected_end, 1u, 3u },
        { "[] true", ESyntaxError::trailing_content, 4u, 4u },
        { "{} ,1", ESyntaxError::trailing_content, 4u, 4u },
        { "1,a:2", ESyntaxError::expected_separator, 1u, 4u },
        { "\"a\":1,2", ESyntaxError::expected_colon, 7u, 8u },
        { "[a:1]", ESyntaxError::expected_separator, 1u, 3u },
        { "1,,2", ESyntaxError::expected_value, 1u, 3u },
        { "\"a\\nb\":1", ESyntaxError::newline_in_name, 1u, 3u }
    };
    for (const auto& item : failures)
    {
        CDocumentStructureEstimates estimates{ 1u, 1u, 1u, 1u, 1u, 1u };
        const auto report = check(item.source, &estimates);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, !report.succeeded() && report.syntax_error == item.error);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.structure_start.code_point_column_1_based, item.start_column);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.failure_point.code_point_column_1_based, item.failure_column);
        expect_no_estimates(ctx, estimates);
    }
    //  Root lookahead must not publish observations beyond a failing first name.
    const auto partial = check("\"a\\nb\" /*later*/ :1");
    TEST_EXPECT(ctx, partial.syntax_error == ESyntaxError::newline_in_name);
    TEST_EXPECT(ctx, partial.findings == document_finding_bit(EDocumentFinding::implicit_body));
    const auto nested = check("1,{\"a\":[2]}");
    TEST_EXPECT(ctx, nested.succeeded() && nested.findings == document_finding_bit(EDocumentFinding::implicit_body));
    const auto location = check(" ;head\n \xc3\xa9 \"tail\"");
    TEST_EXPECT(ctx, location.syntax_error == ESyntaxError::expected_separator);
    TEST_EXPECT(ctx, location.structure_start.line_1_based == 1u && location.structure_start.code_point_column_1_based == 1u);
    TEST_EXPECT(ctx, location.failure_point.line_1_based == 2u && location.failure_point.code_point_column_1_based == 4u);
}

static void test_ingestion_and_bounds(TTestContext& ctx)
{
    const std::uint8_t input[]{ '{', '"', 'n', 0u, '"', ':', '"', 0xe9u, 0u, '"', '}' };
    const auto linted = text_linter::lint(CByteConstView{ input, sizeof(input) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, linted.report.success && linted.report.recovered_as_cp1252);
    const auto report = document_structure::check(CStringView{ linted.output.data(), linted.report.logical_text_byte_size });
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::logical_nul));
    //  An included physical terminator is not silently stripped by checking.
    expect_failure(ctx, document_structure::check(CStringView{ linted.output.data(), linted.output.size() }), EDocumentStructureStatus::syntax_error);
    const std::string embedded = std::string("{a:1,") + '\0' + "b:2}";
    TEST_EXPECT(ctx, check(embedded).succeeded());
    TEST_EXPECT(ctx, check(embedded).findings == (EDocumentFinding::unquoted_names | EDocumentFinding::logical_nul));
    const char* const samples[]{ "{a:'\\uD834\\uDD1E',b:0x123}", "{a:1e-123}", "{a:1/*comment*/}", "{a:'\"'}" };
    for (const char* sample : samples)
    {
        const std::string text(sample);
        for (std::size_t n = 1u; n < text.size(); ++n)
        {
            const auto prefix = document_structure::check(CStringView{ text.data(), n });
            TEST_EXPECT(ctx, !prefix.succeeded());
            TEST_EXPECT(ctx, prefix.failure_point.available);
        }
    }
}

struct CAllocatorState
{
    std::size_t attempts{ 0u };
    std::size_t fail_on{ 0u };
};

static void* MV_STD_ABI_CALL allocate(void* state, const std::size_t alignment, const std::size_t size) noexcept
{
    auto& fixture = *static_cast<CAllocatorState*>(state);
    if (fixture.attempts++ == fixture.fail_on)
    {
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static void test_depth_and_resources(TTestContext& ctx)
{
    constexpr std::size_t depth = 4096u;
    const std::string text = "n:" + std::string(depth, '[') + "0" + std::string(depth, ']');
    bool completed = false;
    for (std::size_t fail_on = 0u; (fail_on < 40u) && !completed; ++fail_on)
    {
        CAllocatorState fixture{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CDocumentStructureEstimates estimates{ 1u, 1u, 1u, 1u, 1u, 1u };
            const auto report = check(text, &estimates);
            completed = report.succeeded();
            if (completed)
            {
                TEST_EXPECT(ctx, estimates.maximum_depth == depth + 1u);
                TEST_EXPECT(ctx, estimates.value_count == depth + 2u);
            }
            else
            {
                expect_failure(ctx, report, EDocumentStructureStatus::allocation_failed);
                TEST_EXPECT(ctx, report.syntax_error == ESyntaxError::none);
                TEST_EXPECT(ctx, report.findings == ((fail_on == 0u) ? 0u :
                    (EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names)));
                expect_no_estimates(ctx, estimates);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed);
    expect_failure(ctx, check(text.substr(0u, text.size() - 1u)), EDocumentStructureStatus::syntax_error);

    //  Token scanning allocates nothing, and flat input needs only the root
    //  frame regardless of the number of names or the length of its numbers.
    const std::string flat = "{a:" + std::string(8192u, '9') + ",a:1e999999,a:'x',a:null}";
    CAllocatorState fixture{ 0u, 1u };
    memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        document_text::CScanner scanner(text_view(flat));
        ETokenKind kind = ETokenKind::error;
        for (std::size_t i = 0u; i <= flat.size(); ++i)
        {
            kind = scanner.next().kind;
            if ((kind == ETokenKind::end) || (kind == ETokenKind::error))
            {
                break;
            }
        }
        TEST_EXPECT(ctx, kind == ETokenKind::end);
        TEST_EXPECT(ctx, fixture.attempts == 0u);
        TEST_EXPECT(ctx, check(flat).succeeded());
        TEST_EXPECT(ctx, fixture.attempts == 1u);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_writer_compatibility(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey array = live.create_array(CStringView{ "a" });
    TEST_EXPECT(ctx, live.append_child(live.root(), array).succeeded());
    TEST_EXPECT(ctx, live.append_child(array, live.create_signed_integer(1, CStringView{ "n" })).succeeded());
    TEST_EXPECT(ctx, live.append_child(array, live.create_recovered_array(CStringView{ "r" })).succeeded());
    const std::uint8_t content[]{ 0u, 0xc3u, 0xa9u };
    TEST_EXPECT(ctx, live.append_child(array, live.create_string(CStringView{ content, sizeof(content) })).succeeded());
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(live, baked));
    for (const auto mode : { EDocumentWriteMode::morphic, EDocumentWriteMode::strict_json })
    {
        for (const bool ascii : { false, true })
        {
            CDocumentWriteOptions options;
            options.mode = mode;
            options.escape_non_ascii = ascii;
            const auto written = document_writer::write(baked.document(), options);
            TEST_EXPECT(ctx, written.report.succeeded());
            const auto report = document_structure::check(CStringView{ written.output.data(), written.report.logical_text_byte_size });
            TEST_EXPECT(ctx, report.succeeded() && ((report.findings & document_findings::k_relaxed) == 0u));
            TEST_EXPECT(ctx, (report.findings & document_findings::k_morphic) == ((mode == EDocumentWriteMode::morphic) ? document_finding_bit(EDocumentFinding::explicit_plus) : 0u));
        }
    }
}

}

int run_document_structure_tests()
{
    tests::TTestContext ctx;
    document_structure_tests::test_grammar(ctx);
    document_structure_tests::test_input_presence(ctx);
    document_structure_tests::test_policy_boundary(ctx);
    document_structure_tests::test_reports(ctx);
    document_structure_tests::test_partial_findings(ctx);
    document_structure_tests::test_lexical_reuse(ctx);
    document_structure_tests::test_unquoted_boundaries(ctx);
    document_structure_tests::test_semicolon_comments(ctx);
    document_structure_tests::test_name_line_breaks(ctx);
    document_structure_tests::test_root_inference(ctx);
    document_structure_tests::test_ingestion_and_bounds(ctx);
    document_structure_tests::test_depth_and_resources(ctx);
    document_structure_tests::test_writer_compatibility(ctx);
    std::cout << "DocumentStructure: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
