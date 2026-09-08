
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentParser_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "tests/test_suites/DocumentParser_test_suite.hpp"

#include <charconv>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "data_model/baked_document.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/live_document.hpp"
#include "text/text_linter.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace document_parser_tests
{

using tests::TTestContext;

//  Helpers have file-local linkage; the namespace groups this suite's names.
static CDocumentParseReport parse(const std::string& text, CLiveDocument& destination)
{
    return document_parser::parse(CStringView{ text.data(), text.size() }, destination);
}

static std::string write(TTestContext& ctx, const CLiveDocument& document)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(document, block));
    CDocumentWriteOptions options;
    options.pretty_print = false;
    options.trailing_line_ending = false;
    options.escape_non_ascii = true;
    const auto result = document_writer::write(block.document(), options);
    TEST_EXPECT(ctx, result.report.succeeded());
    return result.report.succeeded() ? std::string(reinterpret_cast<const char*>(result.output.data()), result.report.logical_text_byte_size) : std::string{};
}

static CNodeKey member(const CLiveDocument& document, const CNodeKey object, const CStringView& name)
{
    for (CNodeKey child = document.first_child(object); child.is_valid(); child = document.next_sibling(child))
    {
        if (document.name(child) == name)
        {
            return child;
        }
    }
    return {};
}

static void test_construction_and_features(TTestContext& ctx)
{
    CLiveDocument document;
    const auto report = parse("/*start*/ n:+0X7f, a:[null,true,false,1.5,'text',{},[],{x:2},], o:{b:-#80},", document);
    TEST_EXPECT(ctx, report.succeeded() && report.structure.succeeded());
    TEST_EXPECT(ctx, report.structure.required_relaxations == (static_cast<std::uint32_t>(document_text::ERelaxation::comments) |
        static_cast<std::uint32_t>(document_text::ERelaxation::single_quotes) | static_cast<std::uint32_t>(document_text::ERelaxation::unquoted_names) |
        static_cast<std::uint32_t>(document_text::ERelaxation::trailing_commas) | static_cast<std::uint32_t>(document_text::ERelaxation::implicit_root_object)));
    TEST_EXPECT(ctx, report.structure.numeric_extensions == (static_cast<std::uint32_t>(document_text::ENumericExtension::explicit_plus) |
        static_cast<std::uint32_t>(document_text::ENumericExtension::hexadecimal)));
    TEST_EXPECT(ctx, document.is_ready() && document.is_complete() && document.check_integrity());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":+0x7f,\"a\":[null,true,false,1.5,\"text\",{},[],{\"x\":2}],\"o\":{\"b\":-#80}}");
    const CNodeKey array = member(document, document.root(), CStringView{ "a" });
    TEST_EXPECT(ctx, document.child_count(array) == 8u);
    const CNodeKey singleton = document.last_child(array);
    TEST_EXPECT(ctx, document.value_type(singleton) == ELiveValueType::object && !document.is_object_entry(singleton));
    TEST_EXPECT(ctx, document.is_object_entry(document.first_child(singleton)));
    //  Strict syntax needs neither relaxation nor numeric-extension flags.
    const auto strict = parse("{\"true\":false,\"z\":null,\"a\":[1,2]}", document);
    TEST_EXPECT(ctx, strict.succeeded());
    TEST_EXPECT(ctx, strict.structure.required_relaxations == 0u && strict.structure.numeric_extensions == 0u);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"true\":false,\"z\":null,\"a\":[1,2]}");
}

static void test_strings_and_ingestion(TTestContext& ctx)
{
    //  Escaped names and values require independent scratch lifetimes.
    const std::string text = "{\"n\\u0000\\u00e9\":\"\\u0000\\uD834\\uDD1E\\n\\t\\\\\\\"\\/\",e:'',s:'can\\'t',a:'raw\nline',b:'\\b\\f\\r'}";
    CLiveDocument document;
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.succeeded());
    const std::uint8_t name[]{ 'n', 0xc0u, 0x80u, 0xc3u, 0xa9u };
    const CNodeKey value = member(document, document.root(), CStringView{ name, sizeof(name) });
    TEST_EXPECT(ctx, value.is_valid());
    const std::uint8_t expected[]{ 0xc0u, 0x80u, 0xf0u, 0x9du, 0x84u, 0x9eu, '\n', '\t', '\\', '"', '/' };
    TEST_EXPECT(ctx, document.string_value(value) == (CStringView{ expected, sizeof(expected) }));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "e" })).length() == 0u);
    TEST_EXPECT(ctx, document.check_integrity());

    const std::uint8_t cp1252[]{ '{', 'n', ':', '"', 0xe9u, 0u, '\r', '\n', '"', '}' };
    const auto linted = text_linter::lint(CByteConstView{ cp1252, sizeof(cp1252) }, 0u);
    TEST_EXPECT(ctx, linted.report.success && linted.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, linted.report.embedded_nul_count == 1u && linted.report.normalised_line_endings == 0u);
    const auto ingested = document_parser::parse(CStringView{ linted.output.data(), linted.report.logical_text_byte_size }, document);
    TEST_EXPECT(ctx, ingested.succeeded());
    TEST_EXPECT(ctx, ingested.structure.required_relaxations == (static_cast<std::uint32_t>(document_text::ERelaxation::unquoted_names) |
        static_cast<std::uint32_t>(document_text::ERelaxation::unescaped_controls)));
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":\"\\u00e9\\u0000\\r\\n\"}");

    const std::uint8_t modified[]{ '{', 'n', ':', '"', 0xc0u, 0x80u, '"', '}' };
    const auto normalized = text_linter::lint(CByteConstView{ modified, sizeof(modified) }, 0u);
    TEST_EXPECT(ctx, normalized.report.success && normalized.report.modified_utf8_nul_count == 1u);
    TEST_EXPECT(ctx, document_parser::parse(CStringView{ normalized.output.data(), normalized.report.logical_text_byte_size }, document).succeeded());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":\"\\u0000\"}");

    const std::uint8_t offset_input[]{ 0xefu, 0xbbu, 0xbfu, '{', 'a', ':', '"', 0xc3u, 0xa9u, '"', ',', 'n', ':', '1', 'e', '}', 0u };
    const auto shifted = text_linter::lint(CByteConstView{ offset_input, sizeof(offset_input) }, 0u);
    TEST_EXPECT(ctx, shifted.report.success && shifted.report.leading_utf8_bom_stripped);
    const auto failure = document_parser::parse(CStringView{ shifted.output.data(), shifted.report.logical_text_byte_size }, document);
    TEST_EXPECT(ctx, failure.status == EDocumentParseStatus::structural_failure);
    TEST_EXPECT(ctx, failure.byte_offset == 12u && failure.structure.byte_offset == 12u);

    const std::uint8_t cp1252_error[]{ '{', 'a', ':', '"', 0xe9u, '"', ',', 'n', ':', '1', 'e', '}' };
    const auto expanded = text_linter::lint(CByteConstView{ cp1252_error, sizeof(cp1252_error) }, 0u);
    TEST_EXPECT(ctx, expanded.report.success && expanded.report.recovered_as_cp1252);
    const auto expanded_failure = document_parser::parse(CStringView{ expanded.output.data(), expanded.report.logical_text_byte_size }, document);
    TEST_EXPECT(ctx, expanded_failure.status == EDocumentParseStatus::structural_failure);
    TEST_EXPECT(ctx, expanded_failure.byte_offset == 12u && expanded_failure.structure.byte_offset == 12u);
}

static void test_integer_metadata(TTestContext& ctx)
{
    struct CCase
    {
        const char* spelling;
        bool signed_value;
        std::uint64_t magnitude;
        bool negative;
        EIntegerNotation notation;
        EIntegerPrefix prefix;
    };
    const CCase cases[]{
        { "0", false, 0u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+0", true, 0u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-0", true, 0u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+127", true, 127u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+128", true, 128u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-128", true, 128u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-129", true, 129u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "255", false, 255u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "256", false, 256u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+32767", true, 32767u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-32769", true, 32769u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "65536", false, 65536u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+2147483648", true, 2147483648u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "4294967296", false, 4294967296u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+9223372036854775807", true, 9223372036854775807ull, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-9223372036854775808", true, 9223372036854775808ull, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "18446744073709551615", false, 18446744073709551615ull, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "0XFF", false, 255u, false, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "-#80", true, 128u, true, EIntegerNotation::hexadecimal, EIntegerPrefix::alternate },
        { "+#7FFF", true, 32767u, false, EIntegerNotation::hexadecimal, EIntegerPrefix::alternate },
        { "-0x8000000000000000", true, 9223372036854775808ull, true, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "0xffffffffffffffff", false, 18446744073709551615ull, false, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "+0B100000000", true, 256u, false, EIntegerNotation::binary, EIntegerPrefix::standard } };
    for (const auto& item : cases)
    {
        CLiveDocument document;
        TEST_CASE_EXPECT_TRUE(ctx, item.spelling, parse(std::string("n:") + item.spelling, document).succeeded());
        const CNodeKey node = document.first_child(document.root());
        CIntegerMetadata metadata;
        TEST_EXPECT(ctx, document.integer_metadata(node, metadata));
        TEST_EXPECT(ctx, metadata.domain == (item.signed_value ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value));
        TEST_EXPECT(ctx, metadata.notation == item.notation && metadata.prefix == item.prefix);
        if (item.signed_value)
        {
            const std::int64_t expected = item.negative ? ((item.magnitude == 9223372036854775808ull) ? std::numeric_limits<std::int64_t>::min() :
                -static_cast<std::int64_t>(item.magnitude)) : static_cast<std::int64_t>(item.magnitude);
            std::int64_t value = 0;
            TEST_EXPECT(ctx, document.signed_integer_value(node, value) && value == expected);
            TEST_EXPECT(ctx, metadata.width == live_signed_integer_smallest_width(expected));
        }
        else
        {
            std::uint64_t value = 0u;
            TEST_EXPECT(ctx, document.unsigned_integer_value(node, value) && value == item.magnitude);
            TEST_EXPECT(ctx, metadata.width == live_unsigned_integer_smallest_width(item.magnitude));
        }
    }
}

static void expect_float(TTestContext& ctx, const char* spelling, const double expected)
{
    CLiveDocument document;
    TEST_CASE_EXPECT_TRUE(ctx, spelling, parse(std::string("f:") + spelling, document).succeeded());
    double value = 0.0;
    TEST_EXPECT(ctx, document.floating_point_value(document.first_child(document.root()), value));
    TEST_EXPECT(ctx, live_floating_point_bits(value) == live_floating_point_bits(expected));
}

static void test_floats(TTestContext& ctx)
{
    expect_float(ctx, "-0.0", -0.0);
    expect_float(ctx, "+0.0", 0.0);
    expect_float(ctx, "0e999999999999999999999999999999", 0.0);
    expect_float(ctx, "0.1", 0.1);
    expect_float(ctx, "+1.2345678901234567", 1.2345678901234567);
    expect_float(ctx, "5e-324", std::numeric_limits<double>::denorm_min());
    expect_float(ctx, "2.2250738585072014e-308", std::numeric_limits<double>::min());
    expect_float(ctx, "1.7976931348623157e308", std::numeric_limits<double>::max());
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (!live_floating_point_is_finite(value))
        {
            continue;
        }
        char digits[64];
        const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
        TEST_EXPECT(ctx, converted.ec == std::errc{});
        if (converted.ec != std::errc{})
        {
            continue;
        }
        std::string spelling(digits, converted.ptr);
        if (spelling.find_first_of(".e") == std::string::npos)
        {
            spelling += ".0";
        }
        expect_float(ctx, spelling.c_str(), value);
    }
}

static void test_failure_publication(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).succeeded());
    const CNodeKey root = document.root();
    const CNodeKey keep = document.first_child(root);
    const std::uint64_t allocation_size = document.memory_allocation_size();
    struct CCase
    {
        const char* text;
        EDocumentParseStatus status;
        std::size_t offset;
        bool structural_success;
    };
    const CCase cases[]{
        { "{n:1e}", EDocumentParseStatus::structural_failure, 5u, false },
        { "n:18446744073709551616", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:+9223372036854775808", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:-9223372036854775809", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:1e99999", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:1e-99999", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "good:1,n:0x10000000000000000", EDocumentParseStatus::numeric_out_of_range, 9u, true },
        { "good:1,'':2", EDocumentParseStatus::empty_property_name, 7u, true },
        { "a:1,'\\u0061':2", EDocumentParseStatus::duplicate_object_name, 4u, true },
        { "a:1,a:{}", EDocumentParseStatus::duplicate_object_name, 4u, true },
        { "$morphic:{v:1,type:'recovered-array',values:[]}", EDocumentParseStatus::unsupported_morphic_representation, 0u, true },
        { "'\\u0024morphic':null", EDocumentParseStatus::unsupported_morphic_representation, 0u, true },
        { "$$morphic:1", EDocumentParseStatus::unsupported_morphic_representation, 0u, true } };
    for (const auto& item : cases)
    {
        const auto report = parse(item.text, document);
        TEST_CASE_EXPECT_EQ(ctx, item.text, report.status, item.status);
        TEST_EXPECT(ctx, report.byte_offset == item.offset);
        TEST_EXPECT(ctx, report.structure.succeeded() == item.structural_success);
        TEST_EXPECT(ctx, document.root() == root && document.first_child(root) == keep);
        TEST_EXPECT(ctx, document.memory_allocation_size() == allocation_size);
        TEST_EXPECT(ctx, document.check_integrity());
        TEST_EXPECT(ctx, write(ctx, document) == "{\"keep\":7}");
    }
    TEST_EXPECT(ctx, document_parser::parse(CStringView{}, document).status == EDocumentParseStatus::invalid_input_view);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    TEST_EXPECT(ctx, parse("$morphicx:1,s:'$morphic'", document).succeeded());
    TEST_EXPECT(ctx, parse("", document).succeeded());
    TEST_EXPECT(ctx, document.is_ready() && document.value_count() == 1u && document.child_count(document.root()) == 0u);
    //  Publication must happen after all reads, even when source aliases the
    //  destination's old string storage.
    TEST_EXPECT(ctx, parse("source:'new_value:42'", document).succeeded());
    const CStringView alias = document.string_value(document.first_child(document.root()));
    TEST_EXPECT(ctx, document_parser::parse(alias, document).succeeded());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"new_value\":42}");
}

struct CAllocatorState
{
    std::size_t attempts{ 0u };
    std::size_t fail_on{ 0u };
    bool failed{ false };
};

static void* MV_STD_ABI_CALL allocate(void* const state, const std::size_t alignment, const std::size_t size) noexcept
{
    auto& fixture = *static_cast<CAllocatorState*>(state);
    if (fixture.attempts++ == fixture.fail_on)
    {
        fixture.failed = true;
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static void test_depth_and_allocation(TTestContext& ctx)
{
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("keep:7", destination).succeeded());
    const CNodeKey keep = destination.first_child(destination.root());
    const std::string text = "'n\\u0000':'\\u0000\\u00e9',a:[{s:'\\uD834\\uDD1E'},+0x80,{},[]],b:'last'";
    bool completed = false;
    for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
    {
        CAllocatorState fixture{ 0u, fail_on, false };
        memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            const auto report = parse(text, destination);
            completed = report.succeeded();
            if (completed)
            {
                TEST_EXPECT(ctx, !fixture.failed);
                TEST_EXPECT(ctx, destination.check_integrity());
                destination.deallocate();
            }
            else
            {
                TEST_EXPECT(ctx, fixture.failed);
                TEST_EXPECT(ctx, (report.status == EDocumentParseStatus::structural_failure) ||
                    (report.status == EDocumentParseStatus::allocation_failed) || (report.status == EDocumentParseStatus::construction_failed));
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed);
    constexpr std::size_t depth = 2048u;
    const std::string deep = "a:" + std::string(depth, '[') + "null" + std::string(depth, ']');
    const auto report = parse(deep, destination);
    TEST_EXPECT(ctx, report.succeeded() && report.structure.estimates.maximum_depth == depth + 1u);
    TEST_EXPECT(ctx, destination.check_integrity());
    TEST_EXPECT(ctx, destination.value_count() == depth + 2u);
    const std::string expected = "{\"a\":" + std::string(depth, '[') + "null" + std::string(depth, ']') + "}";
    TEST_EXPECT(ctx, write(ctx, destination) == expected);
}

}   //  namespace document_parser_tests

int run_document_parser_tests()
{
    tests::TTestContext ctx;
    document_parser_tests::test_construction_and_features(ctx);
    document_parser_tests::test_strings_and_ingestion(ctx);
    document_parser_tests::test_integer_metadata(ctx);
    document_parser_tests::test_floats(ctx);
    document_parser_tests::test_failure_publication(ctx);
    document_parser_tests::test_depth_and_allocation(ctx);
    std::cout << "DocumentParser: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
