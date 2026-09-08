//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentWriter_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "tests/test_suites/DocumentWriter_test_suite.hpp"

#include <charconv>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "data_model/baked_document.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/live_document.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using TTestContext = tests::TTestContext;

CDocumentWriteOptions compact(const EDocumentWriteMode mode = EDocumentWriteMode::morphic)
{
    CDocumentWriteOptions options;
    options.mode = mode;
    options.pretty_print = false;
    options.trailing_line_ending = false;
    return options;
}

void attach(TTestContext& ctx, CLiveDocument& live, const CNodeKey parent, const CNodeKey child)
{
    TEST_EXPECT(ctx, child.is_valid());
    TEST_EXPECT(ctx, live.append_child(parent, child).succeeded());
}

void expect_text(TTestContext& ctx, const CDocumentWriteResult& result, const std::string& expected)
{
    TEST_EXPECT(ctx, result.report.succeeded());
    TEST_EXPECT(ctx, result.report.logical_text_byte_size == expected.size());
    TEST_EXPECT(ctx, result.output.size() == (expected.size() + 1u));
    if (result.output.size() == (expected.size() + 1u))
    {
        TEST_EXPECT(ctx, std::memcmp(result.output.data(), expected.data(), expected.size()) == 0);
        TEST_EXPECT(ctx, result.output.data()[expected.size()] == 0u);
    }
}

void expect_failure(TTestContext& ctx, const CDocumentWriteResult& result, const EDocumentWriteStatus status)
{
    TEST_EXPECT(ctx, result.report.status == status);
    TEST_EXPECT(ctx, result.output.memory_allocation_count() == 0u);
    TEST_EXPECT(ctx, result.output.size() == 0u);
    TEST_EXPECT(ctx, result.report.logical_text_byte_size == 0u);
    TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == 0u);
    TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == 0u);
    TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == 0u);
    TEST_EXPECT(ctx, result.report.embedded_nuls_escaped == 0u);
    TEST_EXPECT(ctx, result.report.reserved_property_names_escaped == 0u);
    TEST_EXPECT(ctx, result.report.recovered_arrays_written == 0u);
}

void test_named_payloads(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey items = live.create_array(CStringView{ "items" });
    attach(ctx, live, live.root(), items);
    attach(ctx, live, items, live.create_signed_integer(1, CStringView{ "n" }));
    attach(ctx, live, items, live.create_unsigned_integer(5u, CStringView{ "u" }));
    attach(ctx, live, items, live.create_floating_point(1.5, CStringView{ "f" }));
    attach(ctx, live, items, live.create_boolean(true, CStringView{ "b" }));
    attach(ctx, live, items, live.create_null(CStringView{ "z" }));
    attach(ctx, live, items, live.create_string(CStringView{ "text" }, CStringView{ "s" }));
    const CNodeKey object = live.create_object(CStringView{ "o" });
    attach(ctx, live, items, object);
    attach(ctx, live, object, live.create_boolean(false, CStringView{ "k" }));
    const CNodeKey array = live.create_array(CStringView{ "a" });
    attach(ctx, live, items, array);
    attach(ctx, live, array, live.create_unsigned_integer(2u));
    const CNodeKey recovery = live.create_recovered_array(CStringView{ "r" });
    attach(ctx, live, items, recovery);
    attach(ctx, live, recovery, live.create_null());
    attach(ctx, live, items, live.create_empty(CStringView{ "e" }));
    attach(ctx, live, items, live.create_unsigned_integer(3u));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    for (const EDocumentWriteMode mode : { EDocumentWriteMode::morphic, EDocumentWriteMode::strict_json })
    {
        const auto result = document_writer::write(block.document(), compact(mode));
        const std::string sign = (mode == EDocumentWriteMode::morphic) ? "+" : "";
        expect_text(ctx, result, "{\"items\":[{\"n\":" + sign + "1},{\"u\":5},{\"f\":1.5},{\"b\":true},"
            "{\"z\":null},{\"s\":\"text\"},{\"o\":{\"k\":false}},{\"a\":[2]},"
            "{\"r\":{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[null]}}},{\"e\":null},3]}");
        TEST_EXPECT(ctx, result.report.recovered_arrays_written == 1u);
        TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == ((mode == EDocumentWriteMode::strict_json) ? 1u : 0u));
    }
}

void test_layout_and_options(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    expect_text(ctx, document_writer::write(block.document()), "{}\n");
    const CNodeKey array = live.create_array(CStringView{ "a" });
    attach(ctx, live, live.root(), array);
    attach(ctx, live, array, live.create_boolean(true, CStringView{ "b" }));
    attach(ctx, live, array, live.create_object());
    attach(ctx, live, array, live.create_array());
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const std::string expected = "{\n  \"a\": [\n    {\n      \"b\": true\n    },\n    {},\n    []\n  ]\n}\n";
    expect_text(ctx, document_writer::write(block.document()), expected);
    std::string windows;
    for (const char ch : expected)
    {
        if (ch == '\n') { windows += '\r'; }
        windows += ch;
    }
    CDocumentWriteOptions options;
    options.line_ending = EDocumentWriteLineEnding::crlf;
    expect_text(ctx, document_writer::write(block.document(), options), windows);
    options.indent_width = 0u;
    options.line_ending = EDocumentWriteLineEnding::lf;
    expect_text(ctx, document_writer::write(block.document(), options), "{\n\"a\": [\n{\n\"b\": true\n},\n{},\n[]\n]\n}\n");
    options.indent_width = std::numeric_limits<std::size_t>::max();
    expect_failure(ctx, document_writer::write(block.document(), options), EDocumentWriteStatus::output_exceeds_engine_size_limit);
    options.pretty_print = false;
    expect_text(ctx, document_writer::write(block.document(), options), "{\"a\":[{\"b\":true},{},[]]}\n");
    options.mode = static_cast<EDocumentWriteMode>(255u);
    expect_failure(ctx, document_writer::write(block.document(), options), EDocumentWriteStatus::invalid_options);
    options = compact();
    options.line_ending = static_cast<EDocumentWriteLineEnding>(255u);
    expect_failure(ctx, document_writer::write(block.document(), options), EDocumentWriteStatus::invalid_options);
    expect_failure(ctx, document_writer::write(CBakedDocument{}), EDocumentWriteStatus::source_not_ready);
}

void test_recovery_and_reserved_names(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey recovered = live.create_recovered_array(CStringView{ "r" });
    attach(ctx, live, live.root(), recovered);
    attach(ctx, live, recovered, live.create_unsigned_integer(22u));
    const CNodeKey competitor = live.create_object();
    attach(ctx, live, recovered, competitor);
    attach(ctx, live, competitor, live.create_string(CStringView{ "first" }, CStringView{ "x" }));
    attach(ctx, live, recovered, live.create_recovered_array());
    const CNodeKey nested = live.create_recovered_array();
    attach(ctx, live, recovered, nested);
    attach(ctx, live, nested, live.create_boolean(false));
    const CNodeKey lookalike = live.create_object(CStringView{ "$morphic" });
    attach(ctx, live, live.root(), lookalike);
    attach(ctx, live, lookalike, live.create_unsigned_integer(1u, CStringView{ "v" }));
    attach(ctx, live, lookalike, live.create_string(CStringView{ "recovered-array" }, CStringView{ "type" }));
    attach(ctx, live, lookalike, live.create_array(CStringView{ "values" }));
    const CNodeKey array = live.create_array(CStringView{ "a" });
    attach(ctx, live, live.root(), array);
    attach(ctx, live, array, live.create_null(CStringView{ "$$morphic" }));
    attach(ctx, live, array, live.create_string(CStringView{ "$morphic" }, CStringView{ "$morphic" }));
    attach(ctx, live, array, live.create_null(CStringView{ "$morphicx" }));
    attach(ctx, live, array, live.create_null(CStringView{ "$" }));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const std::string expected = "{\"r\":{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[22,{\"x\":\"first\"},"
        "{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[]}},"
        "{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[false]}}]}},"
        "\"$$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[]},"
        "\"a\":[{\"$$$morphic\":null},{\"$$morphic\":\"$morphic\"},{\"$morphicx\":null},{\"$\":null}]}";
    for (const EDocumentWriteMode mode : { EDocumentWriteMode::morphic, EDocumentWriteMode::strict_json })
    {
        auto options = compact(mode);
        for (const bool ascii : { false, true })
        {
            options.escape_non_ascii = ascii;
            const auto result = document_writer::write(block.document(), options);
            expect_text(ctx, result, expected);
            TEST_EXPECT(ctx, result.report.recovered_arrays_written == 3u);
            TEST_EXPECT(ctx, result.report.reserved_property_names_escaped == 3u);
        }
    }
}

void test_strings(TTestContext& ctx)
{
    const std::uint8_t name[]{ 'n', 0u, 0xc3u, 0xa9u };
    const std::uint8_t value[]{ 0xc0u, 0x80u, '"', '\\', '/', '\b', '\f', '\n', '\r', '\t', 1u, 31u,
        0x7fu, 0xc2u, 0x80u, 0xdfu, 0xbfu, 0xe0u, 0xa0u, 0x80u, 0xefu, 0xbfu, 0xbfu,
        0xf0u, 0x90u, 0x80u, 0x80u, 0xf4u, 0x8fu, 0xbfu, 0xbfu };
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    attach(ctx, live, live.root(), live.create_string(CStringView{ value, sizeof(value) }, CStringView{ name, sizeof(name) }));
    attach(ctx, live, live.root(), live.create_string(CStringView{}, CStringView{ "empty" }));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    for (const EDocumentWriteMode mode : { EDocumentWriteMode::morphic, EDocumentWriteMode::strict_json })
    {
        for (const bool ascii : { false, true })
        {
            auto options = compact(mode);
            options.escape_non_ascii = ascii;
            const auto result = document_writer::write(block.document(), options);
            const std::string escaped_name = ascii ? "n\\u0000\\u00e9" : "n\\u0000\xc3\xa9";
            std::string escaped_value = "\\u0000\\\"\\\\/\\b\\f\\n\\r\\t\\u0001\\u001f\x7f";
            escaped_value += ascii ? "\\u0080\\u07ff\\u0800\\uffff\\ud800\\udc00\\udbff\\udfff" :
                "\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
            expect_text(ctx, result, "{\"" + escaped_name + "\":\"" + escaped_value + "\",\"empty\":\"\"}");
            TEST_EXPECT(ctx, result.report.embedded_nuls_escaped == 2u);
            TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == (ascii ? 7u : 0u));
            for (std::size_t i = 0u; i < result.report.logical_text_byte_size; ++i)
            {
                TEST_EXPECT(ctx, result.output.data()[i] != 0u);
                if (ascii) { TEST_EXPECT(ctx, result.output.data()[i] < 128u); }
            }
        }
    }
}

std::string magnitude_digits(std::uint64_t value, const unsigned base)
{
    std::string result;
    do
    {
        result.insert(result.begin(), "0123456789abcdef"[value % base]);
        value /= base;
    } while (value != 0u);
    return result;
}

void test_integers(TTestContext& ctx)
{
    const std::int64_t signed_values[]{ std::numeric_limits<std::int64_t>::min(), -2147483649ll, -2147483648ll,
        -32769, -32768, -129, -128, -1, 0, 1, 127, 128, 32767, 32768, 2147483647ll, 2147483648ll,
        std::numeric_limits<std::int64_t>::max() };
    const std::uint64_t unsigned_values[]{ 0u, 1u, 127u, 128u, 255u, 256u, 65535u, 65536u, 4294967295ull,
        4294967296ull, 9223372036854775808ull, std::numeric_limits<std::uint64_t>::max() };
    for (unsigned form = 0u; form < 4u; ++form)
    {
        const unsigned base = (form == 0u) ? 10u : ((form == 3u) ? 2u : 16u);
        const std::string prefix = (form == 0u) ? "" : ((form == 1u) ? "0x" : ((form == 2u) ? "#" : "0b"));
        for (const bool signed_value : { false, true })
        {
            const std::size_t count = signed_value ? (sizeof(signed_values) / sizeof(signed_values[0])) :
                (sizeof(unsigned_values) / sizeof(unsigned_values[0]));
            for (std::size_t i = 0u; i < count; ++i)
            {
                CLiveDocument live;
                TEST_EXPECT(ctx, live.initialise());
                CIntegerMetadata metadata;
                metadata.domain = signed_value ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value;
                metadata.width = signed_value ? live_signed_integer_smallest_width(signed_values[i]) : live_unsigned_integer_smallest_width(unsigned_values[i]);
                metadata.notation = (form == 0u) ? EIntegerNotation::decimal : ((form == 3u) ? EIntegerNotation::binary : EIntegerNotation::hexadecimal);
                metadata.prefix = (form == 2u) ? EIntegerPrefix::alternate : EIntegerPrefix::standard;
                attach(ctx, live, live.root(), signed_value ? live.create_signed_integer(signed_values[i], metadata, CStringView{ "n" }) :
                    live.create_unsigned_integer(unsigned_values[i], metadata, CStringView{ "n" }));
                CBakedDocumentBlock block;
                TEST_EXPECT(ctx, document_translation::bake(live, block));
                const bool negative = signed_value && (signed_values[i] < 0);
                const std::uint64_t magnitude = signed_value ? (negative ? (0ull - static_cast<std::uint64_t>(signed_values[i])) :
                    static_cast<std::uint64_t>(signed_values[i])) : unsigned_values[i];
                const std::string sign = negative ? "-" : (signed_value ? "+" : "");
                expect_text(ctx, document_writer::write(block.document(), compact()), "{\"n\":" + sign + prefix + magnitude_digits(magnitude, base) + "}");
                const auto strict = document_writer::write(block.document(), compact(EDocumentWriteMode::strict_json));
                expect_text(ctx, strict, "{\"n\":" + std::string(negative ? "-" : "") + magnitude_digits(magnitude, 10u) + "}");
                TEST_EXPECT(ctx, strict.report.explicit_positive_signs_omitted == ((signed_value && !negative) ? 1u : 0u));
                TEST_EXPECT(ctx, strict.report.non_decimal_integers_normalised == ((form != 0u) ? 1u : 0u));
            }
        }
    }
}

void check_float(TTestContext& ctx, const double value, const char* const expected = nullptr)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    attach(ctx, live, live.root(), live.create_floating_point(value, CStringView{ "f" }));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const auto result = document_writer::write(block.document(), compact());
    TEST_EXPECT(ctx, result.report.succeeded());
    if (!result.report.succeeded()) { return; }
    const char* const first = reinterpret_cast<const char*>(result.output.data()) + 5u;
    const char* const last = reinterpret_cast<const char*>(result.output.data()) + result.report.logical_text_byte_size - 1u;
    const std::string number(first, last);
    TEST_EXPECT(ctx, number.find_first_of(".e") != std::string::npos);
    double parsed = 0.0;
    const auto conversion = std::from_chars(first, last, parsed);
    TEST_EXPECT(ctx, conversion.ec == std::errc{} && conversion.ptr == last);
    TEST_EXPECT(ctx, live_floating_point_bits(value) == live_floating_point_bits(parsed));
    if (expected != nullptr) { TEST_EXPECT(ctx, number == expected); }
    expect_text(ctx, document_writer::write(block.document(), compact(EDocumentWriteMode::strict_json)), "{\"f\":" + number + "}");
}

void test_floats(TTestContext& ctx)
{
    check_float(ctx, 0.0, "0.0");
    check_float(ctx, -0.0, "-0.0");
    check_float(ctx, 1.0, "1.0");
    check_float(ctx, 0.1, "0.1");
    check_float(ctx, 1.2345678901234567, "1.2345678901234567");
    check_float(ctx, 1e20, "1e+20");
    check_float(ctx, std::numeric_limits<double>::denorm_min(), "5e-324");
    check_float(ctx, std::numeric_limits<double>::min(), "2.2250738585072014e-308");
    check_float(ctx, std::numeric_limits<double>::max(), "1.7976931348623157e+308");
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (live_floating_point_is_finite(value)) { check_float(ctx, value); }
    }
}

struct SFailingAllocator
{
    std::size_t attempts{ 0u };
    std::size_t fail_on{ 0u };
};

void* MV_STD_ABI_CALL allocate_with_failure(void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    SFailingAllocator& fixture = *static_cast<SFailingAllocator*>(state);
    if (fixture.attempts++ == fixture.fail_on) { return nullptr; }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

void test_depth_and_allocation_failures(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    CNodeKey parent = live.root();
    constexpr std::size_t depth = 2048u;
    for (std::size_t i = 0u; i < depth; ++i)
    {
        const CNodeKey child = live.create_array(CStringView{ "n" });
        attach(ctx, live, parent, child);
        parent = child;
    }
    attach(ctx, live, parent, live.create_null());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    std::string expected;
    for (std::size_t i = 0u; i < depth; ++i) { expected += "{\"n\":["; }
    expected += "null";
    for (std::size_t i = 0u; i < depth; ++i) { expected += "]}"; }
    expect_text(ctx, document_writer::write(block.document(), compact()), expected);

    //  Emit transformations before growing the deep result, so later failures
    //  must discard already accumulated counters as well as partial text.
    const CNodeKey first = live.first_child(live.root());
    const std::uint8_t text[]{ 0u, 0xc3u, 0xa9u };
    TEST_EXPECT(ctx, live.insert_child_before(live.root(),
        live.create_string(CStringView{ text, sizeof(text) }, CStringView{ "$morphic" }), first).succeeded());
    const CIntegerMetadata hex{ EIntegerDomain::signed_value, EIntegerWidth::bits_8,
        EIntegerNotation::hexadecimal, EIntegerPrefix::alternate };
    TEST_EXPECT(ctx, live.insert_child_before(live.root(), live.create_signed_integer(7, hex, CStringView{ "h" }), first).succeeded());
    TEST_EXPECT(ctx, live.insert_child_before(live.root(), live.create_recovered_array(CStringView{ "r" }), first).succeeded());
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    expected = "{\"$$morphic\":\"\\u0000\\u00e9\",\"h\":7,\"r\":{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[]}}," + expected.substr(1u);
    auto options = compact(EDocumentWriteMode::strict_json);
    options.escape_non_ascii = true;
    bool completed = false;
    for (std::size_t fail_on = 0u; (fail_on < 32u) && !completed; ++fail_on)
    {
        SFailingAllocator fixture{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory_context{ allocator };
        {
            tests::TMemoryContextScope scope{ &memory_context };
            const auto result = document_writer::write(block.document(), options);
            completed = result.report.succeeded();
            if (completed)
            {
                expect_text(ctx, result, expected);
                TEST_EXPECT(ctx, result.report.reserved_property_names_escaped == 1u);
                TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == 1u);
                TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == 1u);
                TEST_EXPECT(ctx, result.report.embedded_nuls_escaped == 1u);
                TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == 1u);
                TEST_EXPECT(ctx, result.report.recovered_arrays_written == 1u);
            }
            else { expect_failure(ctx, result, EDocumentWriteStatus::allocation_failed); }
        }
        TEST_EXPECT(ctx, memory_context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed);
    TEST_EXPECT(ctx, block.document().check_integrity());
}

}

int run_document_writer_tests()
{
    TTestContext ctx;
    test_named_payloads(ctx);
    test_layout_and_options(ctx);
    test_recovery_and_reserved_names(ctx);
    test_strings(ctx);
    test_integers(ctx);
    test_floats(ctx);
    test_depth_and_allocation_failures(ctx);
    std::cout << "DocumentWriter: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
