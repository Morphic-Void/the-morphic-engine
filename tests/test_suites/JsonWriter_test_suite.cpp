//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    JsonWriter_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Aug 26

#include <charconv>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "data_model/baked_document.hpp"
#include "text/json_writer.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/JsonWriter_test_suite.hpp"

namespace json_writer_tests
{
using tests::TTestContext;

[[nodiscard]] static CJsonWriteOptions compact() noexcept
{
    CJsonWriteOptions options;
    options.pretty_print = false;
    options.trailing_line_ending = false;
    return options;
}

[[nodiscard]] static bool bake(const CLiveDocument& live, CBakedDocumentBlock& block) noexcept
{
    CBakedDocumentBuilder builder;
    return builder.build_from(live) && block.build_from(builder);
}

static void expect_text(TTestContext& ctx, const CJsonWriteResult& result, const std::string& expected)
{
    TEST_EXPECT(ctx, result.report.succeeded());
    TEST_EXPECT(ctx, result.report.logical_text_byte_size == expected.size());
    TEST_EXPECT(ctx, result.output.size() == expected.size() + 1u);
    if (result.output.size() == expected.size() + 1u)
    {
        TEST_EXPECT(ctx, std::memcmp(result.output.data(), expected.data(), expected.size()) == 0);
        TEST_EXPECT(ctx, result.output.data()[expected.size()] == 0u);
    }
}

static void expect_failure(TTestContext& ctx, const CJsonWriteResult& result, const EJsonWriteStatus status)
{
    TEST_EXPECT(ctx, result.report.status == status);
    TEST_EXPECT(ctx, result.output.size() == 0u);
    TEST_EXPECT(ctx, result.output.memory_allocation_count() == 0u);
    TEST_EXPECT(ctx, result.report.logical_text_byte_size == 0u);
    TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == 0u);
    TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == 0u);
    TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == 0u);
    TEST_EXPECT(ctx, result.report.embedded_nuls_escaped == 0u);
    TEST_EXPECT(ctx, result.report.recovery_nodes_written == 0u);
    TEST_EXPECT(ctx, !result.report.diagnostic_envelope_written);
}

static void test_structure_and_formatting(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root = live.create_object();
    const CNodeKey array = live.create_array();
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.add_object_child(root, CStringView{ "z" }, array));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_null()));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_boolean(true)));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_boolean(false)));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_integer(-7)));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_floating_point(1.0)));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_string(CStringView{ "" })));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_object()));
    TEST_EXPECT(ctx, live.append_array_child(array, live.create_array()));
    TEST_EXPECT(ctx, live.add_object_child(root, CStringView{ "a" }, live.create_string(CStringView{ "last" })));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    const std::string expected = "{\n  \"z\": [\n    null,\n    true,\n    false,\n    -7,\n    1.0,\n    \"\",\n    {},\n    []\n  ],\n  \"a\": \"last\"\n}\n";
    expect_text(ctx, json_writer::write(block.document()), expected);
    const auto options = compact();
    const auto first = json_writer::write(block.document(), options);
    expect_text(ctx, first, "{\"z\":[null,true,false,-7,1.0,\"\",{},[]],\"a\":\"last\"}");
    const auto second = json_writer::write(block.document(), options);
    TEST_EXPECT(ctx, first.output.size() == second.output.size());
    if (first.output.size() == second.output.size())
        TEST_EXPECT(ctx, std::memcmp(first.output.data(), second.output.data(), first.output.size()) == 0);

    CJsonWriteOptions crlf;
    crlf.line_ending = EJsonWriteLineEnding::crlf;
    std::string windows;
    for (char ch : expected) { if (ch == '\n') windows += '\r'; windows += ch; }
    expect_text(ctx, json_writer::write(block.document(), crlf), windows);
    crlf.pretty_print = false;
    crlf.trailing_line_ending = true;
    expect_text(ctx, json_writer::write(block.document(), crlf), "{\"z\":[null,true,false,-7,1.0,\"\",{},[]],\"a\":\"last\"}\r\n");
}

static void test_indent_and_empty_roots(TTestContext& ctx)
{
    for (bool object : { false, true })
    {
        CLiveDocument live;
        TEST_EXPECT(ctx, live.initialise());
        const CNodeKey root = object ? live.create_object() : live.create_array();
        TEST_EXPECT(ctx, live.set_root(root));
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake(live, block));
        expect_text(ctx, json_writer::write(block.document()), object ? "{}\n" : "[]\n");
    }
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root = live.create_array();
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.append_array_child(root, live.create_null()));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    CJsonWriteOptions options;
    options.indent_width = 4u;
    options.trailing_line_ending = false;
    expect_text(ctx, json_writer::write(block.document(), options), "[\n    null\n]");
    options.indent_width = 0u;
    expect_text(ctx, json_writer::write(block.document(), options), "[\nnull\n]");
    options.indent_width = std::numeric_limits<std::size_t>::max();
    expect_failure(ctx, json_writer::write(block.document(), options), EJsonWriteStatus::output_exceeds_engine_size_limit);
    options.pretty_print = false;
    expect_text(ctx, json_writer::write(block.document(), options), "[null]");
}

static void test_strings_and_options(TTestContext& ctx)
{
    //  Bake promotes literal NUL; an already modified NUL must write identically.
    const std::uint8_t name[]{ 'n', 0u, 0xc3u, 0xa9u };
    const std::uint8_t value[]{ 0xc0u, 0x80u, '"', '\\', '/', '\b', '\f', '\n', '\r', '\t', 1u, 31u,
        0x7fu, 0xc2u, 0x80u, 0xdfu, 0xbfu, 0xe0u, 0xa0u, 0x80u, 0xefu, 0xbfu, 0xbfu,
        0xf0u, 0x90u, 0x80u, 0x80u, 0xf4u, 0x8fu, 0xbfu, 0xbfu };
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root = live.create_object();
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.add_object_child(root, CStringView{ name, sizeof(name) }, live.create_string(CStringView{ value, sizeof(value) })));
    const CJsonIntegerMetadata hex{ EJsonIntegerSign::signed_value, EJsonIntegerWidth::bits_8,
        EJsonIntegerNotation::hexadecimal, EJsonIntegerPrefix::alternate };
    TEST_EXPECT(ctx, live.add_object_child(root, CStringView{ "integer" }, live.create_integer(127, hex)));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    for (bool strict : { false, true }) for (bool ascii : { false, true })
    {
        auto options = compact();
        options.strict_json = strict;
        options.escape_non_ascii = ascii;
        const std::string escaped_name = ascii ? "n\\u0000\\u00e9" : "n\\u0000\xc3\xa9";
        std::string escaped_value = "\\u0000\\\"\\\\/\\b\\f\\n\\r\\t\\u0001\\u001f\x7f";
        escaped_value += ascii ? "\\u0080\\u07ff\\u0800\\uffff\\ud800\\udc00\\udbff\\udfff" :
            "\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
        const auto result = json_writer::write(block.document(), options);
        expect_text(ctx, result, "{\"" + escaped_name + "\":\"" + escaped_value + "\",\"integer\":" + (strict ? "127" : "+#7f") + "}");
        TEST_EXPECT(ctx, result.report.embedded_nuls_escaped == 2u);
        TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == (ascii ? 7u : 0u));
        TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == (strict ? 1u : 0u));
        TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == (strict ? 1u : 0u));
        if (ascii) for (std::size_t i = 0; i < result.output.size(); ++i) TEST_EXPECT(ctx, result.output.data()[i] < 128u);
    }

    std::uint8_t controls[32];
    for (std::uint8_t i = 0u; i < 32u; ++i) controls[i] = i;
    CLiveDocument all_controls;
    TEST_EXPECT(ctx, all_controls.initialise());
    TEST_EXPECT(ctx, all_controls.set_root(all_controls.create_string(CStringView{ controls, sizeof(controls) })));
    CBakedDocumentBlock control_block;
    TEST_EXPECT(ctx, bake(all_controls, control_block));
    expect_text(ctx, json_writer::write(control_block.document(), compact()),
        "\"\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
        "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"");
}

[[nodiscard]] static std::string magnitude_digits(std::uint64_t magnitude, const unsigned base)
{
    const char* digits = "0123456789abcdef";
    std::string result;
    do { result.insert(result.begin(), digits[magnitude % base]); magnitude /= base; } while (magnitude);
    return result;
}

static void test_integer_boundaries(TTestContext& ctx)
{
    const std::int64_t signed_values[]{ std::numeric_limits<std::int64_t>::min(), -2147483649ll, -2147483648ll,
        -32769, -32768, -129, -128, -1, 0, 1, 127, 128, 32767, 32768, 2147483647ll, 2147483648ll,
        std::numeric_limits<std::int64_t>::max() };
    const std::uint64_t unsigned_values[]{ 0u, 1u, 127u, 128u, 255u, 256u, 65535u, 65536u, 4294967295ull,
        4294967296ull, 9223372036854775808ull, std::numeric_limits<std::uint64_t>::max() };
    for (unsigned form = 0u; form < 4u; ++form)
    {
        const unsigned base = form == 0u ? 10u : (form == 3u ? 2u : 16u);
        const std::string prefix = form == 0u ? "" : (form == 1u ? "0x" : (form == 2u ? "#" : "0b"));
        CJsonIntegerMetadata metadata;
        metadata.notation = form == 0u ? EJsonIntegerNotation::decimal : (form == 3u ? EJsonIntegerNotation::binary : EJsonIntegerNotation::hexadecimal);
        metadata.prefix = form == 2u ? EJsonIntegerPrefix::alternate : EJsonIntegerPrefix::standard;
        for (bool signed_value : { false, true })
        {
            const std::size_t count = signed_value ? sizeof(signed_values) / sizeof(signed_values[0]) : sizeof(unsigned_values) / sizeof(unsigned_values[0]);
            for (std::size_t i = 0u; i < count; ++i)
            {
                CLiveDocument live;
                TEST_EXPECT(ctx, live.initialise());
                metadata.sign = signed_value ? EJsonIntegerSign::signed_value : EJsonIntegerSign::unsigned_value;
                metadata.width = signed_value ? json_signed_integer_smallest_width(signed_values[i]) : json_unsigned_integer_smallest_width(unsigned_values[i]);
                const CNodeKey node = signed_value ? live.create_integer(signed_values[i], metadata) : live.create_unsigned_integer(unsigned_values[i], metadata);
                TEST_EXPECT(ctx, live.set_root(node));
                CBakedDocumentBlock block;
                TEST_EXPECT(ctx, bake(live, block));
                const bool negative = signed_value && (signed_values[i] < 0);
                const std::uint64_t magnitude = signed_value ? (negative ? 0ull - static_cast<std::uint64_t>(signed_values[i]) : static_cast<std::uint64_t>(signed_values[i])) : unsigned_values[i];
                const std::string sign = negative ? "-" : (signed_value ? "+" : "");
                expect_text(ctx, json_writer::write(block.document(), compact()), sign + prefix + magnitude_digits(magnitude, base));
                auto strict = compact(); strict.strict_json = true;
                const auto result = json_writer::write(block.document(), strict);
                expect_text(ctx, result, (negative ? "-" : "") + magnitude_digits(magnitude, 10u));
                TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == (signed_value && !negative ? 1u : 0u));
                TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == (form ? 1u : 0u));
            }
        }
    }
}

static void check_float(TTestContext& ctx, const double value)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    TEST_EXPECT(ctx, live.set_root(live.create_floating_point(value)));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    const auto result = json_writer::write(block.document(), compact());
    TEST_EXPECT(ctx, result.report.succeeded());
    if (!result.report.succeeded()) return;
    const char* first = reinterpret_cast<const char*>(result.output.data());
    const char* last = first + result.report.logical_text_byte_size;
    const std::string text(first, last);
    TEST_EXPECT(ctx, text.find_first_of(".e") != std::string::npos);
    TEST_EXPECT(ctx, text.front() != '+');
    double parsed = 0.0;
    const auto converted = std::from_chars(first, last, parsed, std::chars_format::general);
    TEST_EXPECT(ctx, converted.ec == std::errc{} && converted.ptr == last);
    TEST_EXPECT(ctx, std::memcmp(&parsed, &value, sizeof(value)) == 0);
    for (bool strict : { false, true }) for (bool ascii : { false, true })
    {
        auto options = compact(); options.strict_json = strict; options.escape_non_ascii = ascii;
        const auto variant = json_writer::write(block.document(), options);
        expect_text(ctx, variant, text);
        TEST_EXPECT(ctx, variant.report.explicit_positive_signs_omitted == 0u);
    }
    if (value == 0.0) expect_text(ctx, result, (json_floating_point_bits(value) >> 63u) ? "-0.0" : "0.0");
    if (value == 1.0) expect_text(ctx, result, "1.0");
}

static void test_floating_round_trips(TTestContext& ctx)
{
    for (const double value : { 0.0, -0.0, 1.0, -1.0, 0.1, 1.2345678901234567, 1e20, 1e-20,
        std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
        std::numeric_limits<double>::lowest(), std::numeric_limits<double>::denorm_min(),
        -std::numeric_limits<double>::denorm_min() }) check_float(ctx, value);
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u; bits ^= bits >> 7u; bits ^= bits << 17u;
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        if (json_floating_point_is_finite(value)) check_float(ctx, value);
    }
}

static void refresh_crc(std::vector<std::uint8_t>& bytes) noexcept
{
    auto* header = reinterpret_cast<CBakedDocumentHeader*>(bytes.data());
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = header->header_size; i < bytes.size(); ++i)
    {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit) crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    header->payload_crc = ~crc;
}

static void test_recovery_and_rejected_views(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root = live.create_object();
    const CNodeKey collision = live.create_array();
    const CNodeKey nested = live.create_array();
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.add_object_child(root, CStringView{ "settings" }, collision));
    TEST_EXPECT(ctx, live.append_array_child(collision, live.create_integer(1)));
    TEST_EXPECT(ctx, live.append_array_child(collision, nested));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    std::vector<std::uint8_t> bytes(block.bytes().data(), block.bytes().data() + block.bytes().size());
    auto* header = reinterpret_cast<CBakedDocumentHeader*>(bytes.data());
    auto* nodes = reinterpret_cast<CBakedNode*>(bytes.data() + header->nodes.offset);
    const auto index = block.document().first_child(block.document().root());
    nodes[index.query_value()].type = EJsonNodeType::recovered_duplicate_array;
    nodes[block.document().array_at(index, 1u).query_value()].type = EJsonNodeType::recovered_duplicate_array;
    header->flags |= CBakedDocument::k_flag_recovered_duplicate_arrays;
    refresh_crc(bytes);
    const CBakedDocument recovered(bytes.data(), bytes.size());
    TEST_EXPECT(ctx, recovered.is_ready());
    TEST_EXPECT(ctx, recovered.check_integrity());
    for (bool strict : { false, true }) for (bool ascii : { false, true })
    {
        auto options = compact(); options.strict_json = strict; options.escape_non_ascii = ascii;
        const auto result = json_writer::write(recovered, options);
        expect_text(ctx, result, "{\"$morphic.recovery\":{\"format\":\"diagnostic-document\",\"version\":1,\"document\":{\"settings\":{\"$morphic.recovery\":{\"kind\":\"duplicate-member-array\",\"values\":[1,{\"$morphic.recovery\":{\"kind\":\"duplicate-member-array\",\"values\":[]}}]}}}}}");
        TEST_EXPECT(ctx, result.report.recovery_nodes_written == 2u);
        TEST_EXPECT(ctx, result.report.diagnostic_envelope_written);
    }
    expect_text(ctx, json_writer::write(recovered),
        "{\n  \"$morphic.recovery\": {\n    \"format\": \"diagnostic-document\",\n    \"version\": 1,\n    \"document\": {\n      \"settings\": {\n        \"$morphic.recovery\": {\n          \"kind\": \"duplicate-member-array\",\n          \"values\": [\n            1,\n            {\n              \"$morphic.recovery\": {\n                \"kind\": \"duplicate-member-array\",\n                \"values\": []\n              }\n            }\n          ]\n        }\n      }\n    }\n  }\n}\n");

    //  Malformed bytes fail at checked-view construction, not in serialization.
    std::vector<std::uint8_t> corrupt = bytes;
    corrupt.back() ^= 1u;
    const CBakedDocument rejected(corrupt.data(), corrupt.size());
    TEST_EXPECT(ctx, !rejected.is_ready());
    expect_failure(ctx, json_writer::write(rejected), EJsonWriteStatus::source_not_ready);
    expect_failure(ctx, json_writer::write(CBakedDocument{}), EJsonWriteStatus::source_not_ready);
}

static void test_deep_traversal_and_allocation_failure(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    CNodeKey parent = live.create_array();
    TEST_EXPECT(ctx, live.set_root(parent));
    constexpr std::size_t depth = 128u;
    for (std::size_t i = 1u; i < depth; ++i)
    {
        const auto child = live.create_array();
        TEST_EXPECT(ctx, live.append_array_child(parent, child));
        parent = child;
    }
    TEST_EXPECT(ctx, live.append_array_child(parent, live.create_boolean(true)));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    expect_text(ctx, json_writer::write(block.document(), compact()), std::string(depth, '[') + "true" + std::string(depth, ']'));
    tests::TAllocatorFixture state;
    state.reject_allocation = true;
    memory::CMemoryAllocator allocator(&state, tests::allocate_test_memory, tests::deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    {
        const tests::TModuleIdScope module(module_ids::executable);
        const tests::TMemoryContextScope memory_scope(&context);
        expect_failure(ctx, json_writer::write(block.document()), EJsonWriteStatus::allocation_failed);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    TEST_EXPECT(ctx, block.document().check_integrity());
}

struct TAllocationBudget
{
    std::size_t allowed{ std::numeric_limits<std::size_t>::max() };
    std::size_t attempted{ 0u };
};

[[nodiscard]] static void* MV_STD_ABI_CALL allocate_with_budget(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& budget = *static_cast<TAllocationBudget*>(state);
    if (budget.attempted++ >= budget.allowed)
    {
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

static void test_growth_allocation_failures(TTestContext& ctx)
{
    //  Populate every occurrence counter before enough trailing text to grow again.
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const auto root = live.create_array();
    TEST_EXPECT(ctx, live.set_root(root));
    const CJsonIntegerMetadata hex{ EJsonIntegerSign::signed_value, EJsonIntegerWidth::bits_8,
        EJsonIntegerNotation::hexadecimal, EJsonIntegerPrefix::alternate };
    TEST_EXPECT(ctx, live.append_array_child(root, live.create_integer(127, hex)));
    const std::uint8_t special[]{ 0u, 0xc3u, 0xa9u };
    TEST_EXPECT(ctx, live.append_array_child(root, live.create_string(CStringView{ special, sizeof(special) })));
    const std::string padding(16384u, 'x');
    TEST_EXPECT(ctx, live.append_array_child(root, live.create_string(CStringView{ padding.c_str() })));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    std::vector<std::uint8_t> bytes(block.bytes().data(), block.bytes().data() + block.bytes().size());
    auto* header = reinterpret_cast<CBakedDocumentHeader*>(bytes.data());
    auto* nodes = reinterpret_cast<CBakedNode*>(bytes.data() + header->nodes.offset);
    nodes[header->root_index].type = EJsonNodeType::recovered_duplicate_array;
    header->flags |= CBakedDocument::k_flag_recovered_duplicate_arrays;
    refresh_crc(bytes);
    const CBakedDocument recovered(bytes.data(), bytes.size());
    TEST_EXPECT(ctx, recovered.is_ready());

    for (const bool pretty : { false, true })
    {
        CJsonWriteOptions options;
        options.strict_json = true;
        options.escape_non_ascii = true;
        options.pretty_print = pretty;
        TAllocationBudget budget;
        memory::CMemoryAllocator allocator(&budget, allocate_with_budget, tests::deallocate_test_memory, system_ids::host);
        memory::CMemoryContext context(allocator, system_ids::host);
        std::string expected;
        {
            const tests::TModuleIdScope module(module_ids::executable);
            const tests::TMemoryContextScope memory_scope(&context);
            const auto result = json_writer::write(recovered, options);
            TEST_EXPECT(ctx, result.report.succeeded());
            if (!result.report.succeeded())
            {
                return;
            }
            expected.assign(reinterpret_cast<const char*>(result.output.data()), result.report.logical_text_byte_size);
            expect_text(ctx, result, expected);
            TEST_EXPECT(ctx, expected.find(padding) != std::string::npos);
            TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == 1u);
            TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == 1u);
            TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == 1u);
            TEST_EXPECT(ctx, result.report.embedded_nuls_escaped == 1u);
            TEST_EXPECT(ctx, result.report.recovery_nodes_written == 1u);
            TEST_EXPECT(ctx, result.report.diagnostic_envelope_written);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        const std::size_t allocation_count = budget.attempted;
        TEST_EXPECT(ctx, allocation_count > 1u);

        //  Reject the initial allocation, then each later growth allocation in turn.
        for (std::size_t allowed = 0u; allowed < allocation_count; ++allowed)
        {
            budget.allowed = allowed;
            budget.attempted = 0u;
            {
                const tests::TModuleIdScope module(module_ids::executable);
                const tests::TMemoryContextScope memory_scope(&context);
                expect_failure(ctx, json_writer::write(recovered, options), EJsonWriteStatus::allocation_failed);
            }
            TEST_EXPECT(ctx, budget.attempted == allowed + 1u);
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }

        budget.allowed = allocation_count;
        budget.attempted = 0u;
        {
            const tests::TModuleIdScope module(module_ids::executable);
            const tests::TMemoryContextScope memory_scope(&context);
            expect_text(ctx, json_writer::write(recovered, options), expected);
        }
        TEST_EXPECT(ctx, budget.attempted == allocation_count);
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, recovered.check_integrity());
}

static void test_terminal_zero_allocation_failure(TTestContext& ctx)
{
    //  Find the first bytewise growth boundary without fixing a capacity policy here.
    CByteBuffer probe;
    TEST_EXPECT(ctx, probe.append(1u));
    const std::size_t initial_capacity = probe.capacity();
    while (probe.capacity() == initial_capacity)
    {
        const bool appended = probe.append(1u);
        TEST_EXPECT(ctx, appended);
        if (!appended)
        {
            return;
        }
    }
    const std::size_t text_size = probe.size() - 1u;
    TEST_EXPECT(ctx, text_size >= 3u);
    if (text_size < 3u)
    {
        return;
    }
    probe.deallocate();

    for (const bool trailing_line_ending : { false, true })
    {
        CLiveDocument live;
        TEST_EXPECT(ctx, live.initialise());
        const std::string payload(text_size - 2u - (trailing_line_ending ? 1u : 0u), 'x');
        TEST_EXPECT(ctx, live.set_root(live.create_string(CStringView{ payload.c_str() })));
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake(live, block));
        auto options = compact();
        options.trailing_line_ending = trailing_line_ending;
        TAllocationBudget budget;
        budget.allowed = 1u;
        memory::CMemoryAllocator allocator(&budget, allocate_with_budget, tests::deallocate_test_memory, system_ids::host);
        memory::CMemoryContext context(allocator, system_ids::host);
        {
            const tests::TModuleIdScope module(module_ids::executable);
            const tests::TMemoryContextScope memory_scope(&context);
            expect_failure(ctx, json_writer::write(block.document(), options), EJsonWriteStatus::allocation_failed);
        }
        TEST_EXPECT(ctx, budget.attempted == 2u);
        TEST_EXPECT(ctx, context.is_attribution_empty());
        budget.allowed = 2u;
        budget.attempted = 0u;
        {
            const tests::TModuleIdScope module(module_ids::executable);
            const tests::TMemoryContextScope memory_scope(&context);
            expect_text(ctx, json_writer::write(block.document(), options), "\"" + payload + "\"" + (trailing_line_ending ? "\n" : ""));
        }
        TEST_EXPECT(ctx, budget.attempted == 2u);
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

static void test_recovered_root_options_and_ordinary_marker(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const auto root = live.create_array();
    TEST_EXPECT(ctx, live.set_root(root));
    const CJsonIntegerMetadata hex{ EJsonIntegerSign::signed_value, EJsonIntegerWidth::bits_8,
        EJsonIntegerNotation::hexadecimal, EJsonIntegerPrefix::alternate };
    TEST_EXPECT(ctx, live.append_array_child(root, live.create_integer(127, hex)));
    TEST_EXPECT(ctx, live.append_array_child(root, live.create_string(CStringView{ "\xc3\xa9" })));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(live, block));
    expect_text(ctx, json_writer::write(block.document(), compact()), "[+#7f,\"\xc3\xa9\"]");
    std::vector<std::uint8_t> bytes(block.bytes().data(), block.bytes().data() + block.bytes().size());
    auto* header = reinterpret_cast<CBakedDocumentHeader*>(bytes.data());
    auto* nodes = reinterpret_cast<CBakedNode*>(bytes.data() + header->nodes.offset);
    nodes[header->root_index].type = EJsonNodeType::recovered_duplicate_array;
    header->flags |= CBakedDocument::k_flag_recovered_duplicate_arrays;
    refresh_crc(bytes);
    const CBakedDocument recovered(bytes.data(), bytes.size());
    TEST_EXPECT(ctx, recovered.is_ready());
    for (bool strict : { false, true }) for (bool ascii : { false, true })
    {
        auto options = compact(); options.strict_json = strict; options.escape_non_ascii = ascii;
        const auto result = json_writer::write(recovered, options);
        const std::string payload = std::string(strict ? "127" : "+#7f") + ",\"" + (ascii ? "\\u00e9" : "\xc3\xa9") + "\"";
        expect_text(ctx, result, "{\"$morphic.recovery\":{\"format\":\"diagnostic-document\",\"version\":1,\"document\":{\"$morphic.recovery\":{\"kind\":\"duplicate-member-array\",\"values\":[" + payload + "]}}}}");
        TEST_EXPECT(ctx, result.report.recovery_nodes_written == 1u);
        TEST_EXPECT(ctx, result.report.non_decimal_integers_normalised == (strict ? 1u : 0u));
        TEST_EXPECT(ctx, result.report.explicit_positive_signs_omitted == (strict ? 1u : 0u));
        TEST_EXPECT(ctx, result.report.non_ascii_code_points_escaped == (ascii ? 1u : 0u));
    }
    CLiveDocument ordinary;
    TEST_EXPECT(ctx, ordinary.initialise());
    const auto object = ordinary.create_object();
    TEST_EXPECT(ctx, ordinary.set_root(object));
    TEST_EXPECT(ctx, ordinary.add_object_child(object, CStringView{ "$morphic.recovery" }, ordinary.create_boolean(false)));
    CBakedDocumentBlock ordinary_block;
    TEST_EXPECT(ctx, bake(ordinary, ordinary_block));
    const auto ordinary_result = json_writer::write(ordinary_block.document(), compact());
    expect_text(ctx, ordinary_result, "{\"$morphic.recovery\":false}");
    TEST_EXPECT(ctx, !ordinary_result.report.diagnostic_envelope_written);
    TEST_EXPECT(ctx, ordinary_result.report.recovery_nodes_written == 0u);
}

} // namespace json_writer_tests

int run_json_writer_tests()
{
    tests::TTestContext ctx;
    json_writer_tests::test_structure_and_formatting(ctx);
    json_writer_tests::test_indent_and_empty_roots(ctx);
    json_writer_tests::test_strings_and_options(ctx);
    json_writer_tests::test_integer_boundaries(ctx);
    json_writer_tests::test_floating_round_trips(ctx);
    json_writer_tests::test_recovery_and_rejected_views(ctx);
    json_writer_tests::test_deep_traversal_and_allocation_failure(ctx);
    json_writer_tests::test_growth_allocation_failures(ctx);
    json_writer_tests::test_terminal_zero_allocation_failure(ctx);
    json_writer_tests::test_recovered_root_options_and_ordinary_marker(ctx);
    std::cout << "JsonWriter: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.exit_code();
}
