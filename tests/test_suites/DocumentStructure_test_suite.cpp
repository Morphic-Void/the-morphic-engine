
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
using document_text::ERelaxation;
using document_text::ENumericExtension;
using document_text::ESyntaxError;
using document_text::ETokenKind;

static CStringView text_view(const std::string& text)
{
    return CStringView{ text.data(), text.size() };
}

static CDocumentStructureReport check(const std::string& text) { return document_structure::check(text_view(text)); }

static void expect_failure(TTestContext& ctx, const CDocumentStructureReport& report, const EDocumentStructureStatus status)
{
    TEST_EXPECT(ctx, report.status == status);
    TEST_EXPECT(ctx, report.required_relaxations == 0u);
    TEST_EXPECT(ctx, report.numeric_extensions == 0u);
    TEST_EXPECT(ctx, report.estimates.value_count == 0u);
    TEST_EXPECT(ctx, report.estimates.object_count == 0u);
    TEST_EXPECT(ctx, report.estimates.array_count == 0u);
    TEST_EXPECT(ctx, report.estimates.named_entry_count == 0u);
    TEST_EXPECT(ctx, report.estimates.string_source_byte_size == 0u);
    TEST_EXPECT(ctx, report.estimates.maximum_depth == 0u);
}

static void test_grammar(TTestContext& ctx)
{
    const char* const accepted[]{ "", " \r\n\t", "// empty", "{}", "{\"a\":[]}",
        "{\"n\":123,\"f\":-0.0e+12,\"b\":true,\"z\":null,\"s\":\"text\"}",
        "a:1, enabled:true", "{a:1,}", "a:1,", "{a:[1,2,]}",
        "{'x':'can\\'t', \"y\":'\\\"quoted\\\"'}", "{true:false,null:true,false:null}",
        "/*head*/{a:1/*value*/,b:[true//line\n,false]}/*tail*/",
        "{a:[{n:1},{s:'text'},{z:null},{b:true},{o:{}},{a:[]}]}",
        "{a:\"[ } // /* \\u0000\",b:'a\nb'}", "{a:+0,b:-#FF,c:0Xfa,d:+0B10}",
        "{a:0,b:-0,c:0.1,d:0e0,e:1E-9}" };
    for (const char* text : accepted)
    {
        TEST_CASE_EXPECT_TRUE(ctx, text, check(text).succeeded());
    }
    TEST_EXPECT(ctx, check("{\"\":1,\"\":2}").succeeded());

    const char* const rejected[]{ "[]", "1", "true", "'text'", "[a:1]", "{a:[n:1]}",
        "{a:[\"n\":1]}", "{a}", "{a:}", "{a:1 b:2}", "{a:1,,}", "{,}", "{a:[,]}",
        "{a:[1,,2]}", "{a:[1 2]}", "{a:[1}}", "{a:1]", "{a:1", "{a:[", "a:",
        "{a:1} {}", "{a:1},", "a:1}", "{a:truth}", "{a:True}", "{1:2}",
        "{a:01}", "{a:-01}", "{a:.5}", "{a:1.}", "{a:1e}", "{a:1e+}", "{a:1e-}",
        "{a:+}", "{a:--1}", "{a:0x}", "{a:#}", "{a:0b2}", "{a:0x1g}", "{a:0b10.1}",
        "{a:1e2x}", "{a:1_000}", "{a:NaN}", "{a:Infinity}", "{a:1/2}",
        "{a:\"\\q\"}", "{a:\"\\'\"}", "{a:'\\x41'}", "{a:\"\\u12\"}",
        "{a:\"\\uD800\"}", "{a:\"\\uDC00\"}", "{a:\"\\uD800\\u0041\"}",
        "{a:\"\\uD800\\uDC0z\"}", "{a:'unterminated}", "/*", "/*/", "{} /*tail", "{a:/}" };
    for (const char* text : rejected)
    {
        const auto report = check(text);
        expect_failure(ctx, report, EDocumentStructureStatus::syntax_error);
        TEST_EXPECT(ctx, report.syntax_error != ESyntaxError::none);
        TEST_EXPECT(ctx, report.byte_offset <= std::string(text).size());
    }
}

static void test_input_presence(TTestContext& ctx)
{
    const auto absent = document_structure::check(CStringView{});
    expect_failure(ctx, absent, EDocumentStructureStatus::invalid_input_view);
    TEST_EXPECT(ctx, absent.syntax_error == ESyntaxError::none && absent.byte_offset == 0u);
    const char terminator = '\0';
    const CStringView present{ &terminator, 0u };
    const auto empty = document_structure::check(present);
    TEST_EXPECT(ctx, empty.succeeded());
    TEST_EXPECT(ctx, empty.estimates.value_count == 1u && empty.estimates.object_count == 1u);
    TEST_EXPECT(ctx, empty.estimates.array_count == 0u && empty.estimates.named_entry_count == 0u);
    TEST_EXPECT(ctx, empty.estimates.string_source_byte_size == 0u && empty.estimates.maximum_depth == 1u);
    TEST_EXPECT(ctx, empty.required_relaxations == static_cast<std::uint32_t>(ERelaxation::implicit_root_object));
    TEST_EXPECT(ctx, empty.numeric_extensions == 0u && empty.syntax_error == ESyntaxError::none && empty.byte_offset == 0u);
    document_text::CScanner scanner(present);
    const auto end = scanner.next();
    TEST_EXPECT(ctx, end.kind == ETokenKind::end && end.offset == 0u && end.size == 0u);
    TEST_EXPECT(ctx, scanner.required_relaxations() == 0u && scanner.numeric_extensions() == 0u);
    //  The same backing byte, when included in the logical extent, is content.
    expect_failure(ctx, document_structure::check(CStringView{ &terminator, 1u }), EDocumentStructureStatus::syntax_error);
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
    const auto strict = check("{\"a\":[1,{\"b\":\"x\"}],\"c\":true}");
    TEST_EXPECT(ctx, strict.succeeded());
    TEST_EXPECT(ctx, strict.required_relaxations == 0u && strict.numeric_extensions == 0u);
    TEST_EXPECT(ctx, strict.byte_offset == 0u && strict.syntax_error == ESyntaxError::none);
    TEST_EXPECT(ctx, strict.estimates.value_count == 6u);
    TEST_EXPECT(ctx, strict.estimates.object_count == 2u && strict.estimates.array_count == 1u);
    TEST_EXPECT(ctx, strict.estimates.named_entry_count == 3u);
    TEST_EXPECT(ctx, strict.estimates.string_source_byte_size == 12u);
    TEST_EXPECT(ctx, strict.estimates.maximum_depth == 3u);
    const auto relaxed = check("/*c*/ a:'x\ny', b:[+#F,0b1,],");
    TEST_EXPECT(ctx, relaxed.succeeded());
    TEST_EXPECT(ctx, relaxed.required_relaxations == (static_cast<std::uint32_t>(ERelaxation::comments) |
        static_cast<std::uint32_t>(ERelaxation::single_quotes) | static_cast<std::uint32_t>(ERelaxation::unquoted_names) |
        static_cast<std::uint32_t>(ERelaxation::trailing_commas) | static_cast<std::uint32_t>(ERelaxation::unescaped_controls) |
        static_cast<std::uint32_t>(ERelaxation::implicit_root_object)));
    TEST_EXPECT(ctx, relaxed.numeric_extensions == (static_cast<std::uint32_t>(ENumericExtension::explicit_plus) |
        static_cast<std::uint32_t>(ENumericExtension::hexadecimal) | static_cast<std::uint32_t>(ENumericExtension::binary)));
    TEST_EXPECT(ctx, check("{\"a\":\"comments // and 0xFF and 'quoted'\"}").required_relaxations == 0u);

    struct CFailure
    {
        const char* text;
        ESyntaxError error;
        std::size_t offset;
    };
    const CFailure failures[]{ { "{a:1e+}", ESyntaxError::invalid_number, 6u },
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
        TEST_EXPECT(ctx, report.byte_offset == failure.offset);
    }
}

static void test_lexical_reuse(TTestContext& ctx)
{
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
        TEST_EXPECT(ctx, document_text::read_escape(text_view(text), offset, '\'', scalar) == ESyntaxError::none);
        TEST_EXPECT(ctx, scalar == escape.scalar && offset == text.size());
        TEST_EXPECT(ctx, check("{a:'" + text + "'}").succeeded());
    }
    const std::string text = " /* c */ 'name' : +0xFF, n:1e9999999999999999999999";
    document_text::CScanner scanner(text_view(text));
    const auto name = scanner.next();
    TEST_EXPECT(ctx, name.kind == ETokenKind::string && text.substr(name.offset, name.size) == "'name'");
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::colon);
    const auto number = scanner.next();
    TEST_EXPECT(ctx, number.kind == ETokenKind::integer && text.substr(number.offset, number.size) == "+0xFF");
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::comma);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::identifier);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::colon);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::floating_point);
    TEST_EXPECT(ctx, scanner.next().kind == ETokenKind::end);
}

static void test_ingestion_and_bounds(TTestContext& ctx)
{
    const std::uint8_t input[]{ '{', '"', 'n', 0u, '"', ':', '"', 0xe9u, 0u, '"', '}' };
    const auto linted = text_linter::lint(CByteConstView{ input, sizeof(input) }, 0u);
    TEST_EXPECT(ctx, linted.report.success && linted.report.recovered_as_cp1252);
    const auto report = document_structure::check(CStringView{ linted.output.data(), linted.report.logical_text_byte_size });
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.required_relaxations == static_cast<std::uint32_t>(ERelaxation::unescaped_controls));
    //  An included physical terminator is not silently stripped by checking.
    expect_failure(ctx, document_structure::check(CStringView{ linted.output.data(), linted.output.size() }), EDocumentStructureStatus::syntax_error);
    const std::string embedded = std::string("{a:1,") + '\0' + "b:2}";
    expect_failure(ctx, check(embedded), EDocumentStructureStatus::syntax_error);
    const char* const samples[]{ "{a:'\\uD834\\uDD1E',b:0x123}", "{a:1e-123}", "{a:1/*comment*/}", "{a:'\\\"'}" };
    for (const char* sample : samples)
    {
        const std::string text(sample);
        for (std::size_t n = 1u; n < text.size(); ++n)
        {
            const auto prefix = document_structure::check(CStringView{ text.data(), n });
            TEST_EXPECT(ctx, !prefix.succeeded());
            TEST_EXPECT(ctx, prefix.byte_offset <= n);
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
            const auto report = check(text);
            completed = report.succeeded();
            if (completed)
            {
                TEST_EXPECT(ctx, report.estimates.maximum_depth == depth + 1u);
                TEST_EXPECT(ctx, report.estimates.value_count == depth + 2u);
            }
            else
            {
                expect_failure(ctx, report, EDocumentStructureStatus::allocation_failed);
                TEST_EXPECT(ctx, report.syntax_error == ESyntaxError::none);
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
            TEST_EXPECT(ctx, report.succeeded() && report.required_relaxations == 0u);
            TEST_EXPECT(ctx, report.numeric_extensions == ((mode == EDocumentWriteMode::morphic) ? static_cast<std::uint32_t>(ENumericExtension::explicit_plus) : 0u));
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
    document_structure_tests::test_lexical_reuse(ctx);
    document_structure_tests::test_ingestion_and_bounds(ctx);
    document_structure_tests::test_depth_and_resources(ctx);
    document_structure_tests::test_writer_compatibility(ctx);
    std::cout << "DocumentStructure: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
