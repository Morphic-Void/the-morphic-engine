
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
#include <vector>

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
    TEST_EXPECT(ctx, document.value_type(singleton) == ELiveValueType::integer && document.is_object_entry(singleton));
    TEST_EXPECT(ctx, document.name(singleton) == CStringView{ "x" });
    TEST_EXPECT(ctx, report.interpretations.singleton_objects_unwrapped == 1u);
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
        { "$morphic:{v:1,type:'recovered-array',values:[]}", EDocumentParseStatus::invalid_root_value, 0u, true },
        { "'\\u0024morphic':null", EDocumentParseStatus::invalid_root_value, 0u, true } };
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

static std::string recovery(const std::string& values)
{
    return "{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[" + values + "]}}";
}

static void expect_no_interpretations(TTestContext& ctx, const CDocumentParseReport& report)
{
    TEST_EXPECT(ctx, report.interpretations.recovered_arrays_decoded == 0u);
    TEST_EXPECT(ctx, report.interpretations.reserved_names_unescaped == 0u);
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 0u);
    TEST_EXPECT(ctx, report.interpretations.singleton_objects_unwrapped == 0u);
}

static void test_recovery_and_collisions(TTestContext& ctx)
{
    CLiveDocument document;
    const std::string nested = recovery("false");
    const std::string text = "first:0,item:1,middle:2,item:'two',item:null,item:" + nested + ",last:3";
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 3u);
    TEST_EXPECT(ctx, report.interpretations.recovered_arrays_decoded == 1u);
    TEST_EXPECT(ctx, document.check_integrity() && document.is_complete());
    const CNodeKey item = member(document, document.root(), CStringView{ "item" });
    TEST_EXPECT(ctx, document.value_type(item) == ELiveValueType::recovered_array && document.child_count(item) == 4u);
    TEST_EXPECT(ctx, document.value_type(document.last_child(item)) == ELiveValueType::recovered_array);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"first\":0,\"item\":" + recovery("1,\"two\",null," + nested) + ",\"middle\":2,\"last\":3}");
    for (CNodeKey child = document.first_child(item); child.is_valid(); child = document.next_sibling(child))
    {
        TEST_EXPECT(ctx, !document.is_object_entry(child));
    }

    for (const std::string& values : { std::string{}, std::string("1"), std::string("1,2") })
    {
        const auto extended = parse("r:" + recovery(values) + ",r:{a:1,a:2},r:" + nested, document);
        TEST_EXPECT(ctx, extended.succeeded());
        TEST_EXPECT(ctx, extended.interpretations.duplicate_members_recovered == 3u);
        TEST_EXPECT(ctx, extended.interpretations.recovered_arrays_decoded == 2u);
        const std::string prefix = values.empty() ? std::string{} : values + ",";
        TEST_EXPECT(ctx, write(ctx, document) == "{\"r\":" + recovery(prefix + "{\"a\":" + recovery("1,2") + "}," + nested) + "}");
        TEST_EXPECT(ctx, document.check_integrity());
    }

    //  All six metadata field orders, decoded control names, relaxed version
    //  spellings, and a singleton competitor that must remain anonymous.
    const char* fields[]{ "'\\u0076':+0x01", "type:'recovered-\\u0061rray'", "values:[{n:1}]" };
    const unsigned orders[][3]{ {0u,1u,2u}, {0u,2u,1u}, {1u,0u,2u}, {1u,2u,0u}, {2u,0u,1u}, {2u,1u,0u} };
    for (const auto& order : orders)
    {
        const auto decoded = parse(std::string("r:{'\\u0024morphic':{/*metadata*/") + fields[order[0]] + "," + fields[order[1]] + "," + fields[order[2]] + ",}}", document);
        TEST_EXPECT(ctx, decoded.succeeded());
        TEST_EXPECT(ctx, decoded.interpretations.recovered_arrays_decoded == 1u && decoded.interpretations.singleton_objects_unwrapped == 0u);
        const CNodeKey r = document.first_child(document.root());
        const CNodeKey competitor = document.first_child(r);
        TEST_EXPECT(ctx, document.value_type(r) == ELiveValueType::recovered_array && document.child_count(r) == 1u);
        TEST_EXPECT(ctx, document.value_type(competitor) == ELiveValueType::object && !document.is_object_entry(competitor));
        TEST_EXPECT(ctx, write(ctx, document) == "{\"r\":" + recovery("{\"n\":1}") + "}");
    }
    const auto aliases = parse("'a\\u0000':1,'a" + std::string(1u, '\0') + "':2,'$$morphic':3,'\\u0024$morphic':4,$$$morphic:5,$morphicx:6,s:'$$morphic'", document);
    TEST_EXPECT(ctx, aliases.succeeded());
    TEST_EXPECT(ctx, aliases.interpretations.duplicate_members_recovered == 2u && aliases.interpretations.reserved_names_unescaped == 3u);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"a\\u0000\":" + recovery("1,2") + ",\"$$morphic\":" + recovery("3,4") + ",\"$$$morphic\":5,\"$morphicx\":6,\"s\":\"$$morphic\"}");
    for (const char* version : { "1", "+1", "0X0001", "#01", "+#1", "0b001", "+0B1" })
    {
        const auto decoded = parse(std::string("r:{$morphic:{v:") + version + ",type:'recovered-array',values:[]}}", document);
        TEST_CASE_EXPECT_TRUE(ctx, version, decoded.succeeded());
        TEST_EXPECT(ctx, decoded.interpretations.recovered_arrays_decoded == 1u);
        TEST_EXPECT(ctx, write(ctx, document) == "{\"r\":" + recovery("") + "}");
    }
}

static void test_singleton_contexts(TTestContext& ctx)
{
    CLiveDocument document;
    const std::string text = "a:[{n:null},{b:true},{s:'text'},{i:+1},{f:-0.0},{o:{}},{a:[]},{r:" + recovery("") + "},"
        "{d:1,d:2},{},{x:1,y:2}," + recovery("{n:1},[{a:2}]," + recovery("{b:3}")) + "],o:{one:1}";
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.interpretations.singleton_objects_unwrapped == 10u);
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 1u && report.interpretations.recovered_arrays_decoded == 3u);
    const CNodeKey array = member(document, document.root(), CStringView{ "a" });
    const ELiveValueType types[]{ ELiveValueType::null_value, ELiveValueType::boolean, ELiveValueType::string,
        ELiveValueType::integer, ELiveValueType::floating_point, ELiveValueType::object, ELiveValueType::array,
        ELiveValueType::recovered_array, ELiveValueType::recovered_array, ELiveValueType::object,
        ELiveValueType::object, ELiveValueType::recovered_array };
    CNodeKey child = document.first_child(array);
    for (unsigned i = 0u; i < 12u; ++i)
    {
        TEST_EXPECT(ctx, document.value_type(child) == types[i]);
        TEST_EXPECT(ctx, document.is_object_entry(child) == (i < 9u));
        child = document.next_sibling(child);
    }
    TEST_EXPECT(ctx, !child.is_valid());
    const CNodeKey transport = document.last_child(array);
    TEST_EXPECT(ctx, document.value_type(document.first_child(transport)) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(document.first_child(document.last_child(transport))) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "o" })) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::object && document.check_integrity());
}

static void test_protocol_failures(TTestContext& ctx)
{
    struct CCase
    {
        const char* text;
        EDocumentParseStatus status;
    };
    const CCase cases[]{
        { "r:{$morphic:null}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:[]}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array'}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,v:1,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array','\\u0074ype':'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{values:[],values:[],v:1,type:'recovered-array'}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:[],extra:0}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{'':0}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{$$morphic:0}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{extra:0,$morphic:{v:1,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:[]},extra:0}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:[]},$morphic:{}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1.0,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:true,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:2,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:0,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:#00,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:0b11,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:-1,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:18446744073709551616,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:1,type:null,values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'array',values:[]}}", EDocumentParseStatus::unsupported_recovery_type },
        { "r:{$morphic:{v:1,type:'recovered-array\\u0000',values:[]}}", EDocumentParseStatus::unsupported_recovery_type },
        { "r:{$morphic:{v:1,type:'recovered-array',values:{}}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:null}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "{$morphic:{v:1,type:'recovered-array',values:[]}}", EDocumentParseStatus::invalid_root_value } };
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).succeeded());
    const CNodeKey keep = document.first_child(document.root());
    const std::uint64_t allocation_size = document.memory_allocation_size();
    for (const auto& item : cases)
    {
        const auto report = parse(item.text, document);
        TEST_CASE_EXPECT_EQ(ctx, item.text, report.status, item.status);
        TEST_EXPECT(ctx, report.structure.succeeded());
        TEST_EXPECT(ctx, report.byte_offset < std::strlen(item.text));
        expect_no_interpretations(ctx, report);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep && document.memory_allocation_size() == allocation_size);
        TEST_EXPECT(ctx, document.check_integrity() && write(ctx, document) == "{\"keep\":7}");
    }
    const std::string prefix = "$$morphic:1,a:[{n:1}],d:1,d:2,r:" + recovery("") + ",bad:";
    const auto late = parse(prefix + "{$morphic:{v:2,type:'recovered-array',values:[]}}", document);
    TEST_EXPECT(ctx, late.status == EDocumentParseStatus::unsupported_recovery_version);
    TEST_EXPECT(ctx, late.byte_offset == prefix.size() + std::strlen("{$morphic:{v:"));
    expect_no_interpretations(ctx, late);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    const auto duplicate = parse("r:{$morphic:{v:1,v:1,type:'recovered-array',values:[]}}", document);
    TEST_EXPECT(ctx, duplicate.byte_offset == std::strlen("r:{$morphic:{v:1,"));
    //  User-authored protocol lookalikes are ordinary data when escaped.
    const auto data = parse("$$morphic:{v:2,type:'array',values:[],values:[1]},$$$morphic:3", document);
    TEST_EXPECT(ctx, data.succeeded() && data.interpretations.recovered_arrays_decoded == 0u);
    TEST_EXPECT(ctx, data.interpretations.reserved_names_unescaped == 2u && data.interpretations.duplicate_members_recovered == 1u);
}

static void attach(TTestContext& ctx, CLiveDocument& document, const CNodeKey parent, const CNodeKey child)
{
    TEST_EXPECT(ctx, child.is_valid());
    TEST_EXPECT(ctx, document.append_child(parent, child).succeeded());
}

static void expect_same_semantics(TTestContext& ctx, const CLiveDocument& source, const CLiveDocument& parsed, const bool strict)
{
    struct CPair
    {
        CNodeKey source;
        CNodeKey parsed;
    };
    std::vector<CPair> pending{ { source.root(), parsed.root() } };
    while (!pending.empty())
    {
        CPair pair = pending.back();
        pending.pop_back();
        if ((source.value_type(source.parent(pair.source)) == ELiveValueType::array) &&
            (source.value_type(pair.source) == ELiveValueType::object) && !source.is_object_entry(pair.source) &&
            (source.child_count(pair.source) == 1u))
        {
            pair.source = source.first_child(pair.source);
        }
        const ELiveValueType original_type = source.value_type(pair.source);
        const ELiveValueType type = (original_type == ELiveValueType::empty) ? ELiveValueType::null_value : original_type;
        TEST_EXPECT(ctx, parsed.value_type(pair.parsed) == type);
        TEST_EXPECT(ctx, source.name(pair.source) == parsed.name(pair.parsed));
        TEST_EXPECT(ctx, source.is_object_entry(pair.source) == parsed.is_object_entry(pair.parsed));
        switch (type)
        {
            case ELiveValueType::null_value:
            {
                break;
            }
            case ELiveValueType::boolean:
            {
                bool before = false;
                bool after = false;
                TEST_EXPECT(ctx, source.boolean_value(pair.source, before) && parsed.boolean_value(pair.parsed, after) && before == after);
                break;
            }
            case ELiveValueType::integer:
            {
                CIntegerMetadata before;
                CIntegerMetadata after;
                TEST_EXPECT(ctx, source.integer_metadata(pair.source, before) && parsed.integer_metadata(pair.parsed, after));
                std::int64_t signed_value = 0;
                std::uint64_t unsigned_value = 0u;
                if (before.domain == EIntegerDomain::signed_value)
                {
                    TEST_EXPECT(ctx, source.signed_integer_value(pair.source, signed_value));
                }
                else
                {
                    TEST_EXPECT(ctx, source.unsigned_integer_value(pair.source, unsigned_value));
                }
                if (strict)
                {
                    before.notation = EIntegerNotation::decimal;
                    before.prefix = EIntegerPrefix::standard;
                    if ((before.domain == EIntegerDomain::signed_value) && (signed_value >= 0))
                    {
                        before.domain = EIntegerDomain::unsigned_value;
                        unsigned_value = static_cast<std::uint64_t>(signed_value);
                    }
                }
                TEST_EXPECT(ctx, before.domain == after.domain && before.notation == after.notation && before.prefix == after.prefix);
                if (before.domain == EIntegerDomain::signed_value)
                {
                    std::int64_t value = 0;
                    TEST_EXPECT(ctx, parsed.signed_integer_value(pair.parsed, value) && value == signed_value);
                    TEST_EXPECT(ctx, after.width == live_signed_integer_smallest_width(signed_value));
                }
                else
                {
                    std::uint64_t value = 0u;
                    TEST_EXPECT(ctx, parsed.unsigned_integer_value(pair.parsed, value) && value == unsigned_value);
                    TEST_EXPECT(ctx, after.width == live_unsigned_integer_smallest_width(unsigned_value));
                }
                break;
            }
            case ELiveValueType::floating_point:
            {
                double before = 0.0;
                double after = 0.0;
                TEST_EXPECT(ctx, source.floating_point_value(pair.source, before) && parsed.floating_point_value(pair.parsed, after));
                TEST_EXPECT(ctx, live_floating_point_bits(before) == live_floating_point_bits(after));
                break;
            }
            case ELiveValueType::string:
            {
                TEST_EXPECT(ctx, source.string_value(pair.source) == parsed.string_value(pair.parsed));
                break;
            }
            case ELiveValueType::object:
            case ELiveValueType::array:
            case ELiveValueType::recovered_array:
            {
                TEST_EXPECT(ctx, source.child_count(pair.source) == parsed.child_count(pair.parsed));
                CNodeKey before = source.first_child(pair.source);
                CNodeKey after = parsed.first_child(pair.parsed);
                while (before.is_valid() && after.is_valid())
                {
                    pending.push_back(CPair{ before, after });
                    before = source.next_sibling(before);
                    after = parsed.next_sibling(after);
                }
                TEST_EXPECT(ctx, !before.is_valid() && !after.is_valid());
                break;
            }
            default:
            {
                TEST_EXPECT(ctx, false);
                break;
            }
        }
    }
}

static void test_round_trips(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, source.initialise());
    const CNodeKey array = source.create_array(CStringView{ "items" });
    attach(ctx, source, source.root(), array);
    attach(ctx, source, array, source.create_empty(CStringView{ "placeholder" }));
    attach(ctx, source, array, source.create_null(CStringView{ "null" }));
    attach(ctx, source, array, source.create_boolean(true, CStringView{ "bool" }));
    attach(ctx, source, array, source.create_signed_integer(7, CStringView{ "signed" }));
    attach(ctx, source, array, source.create_signed_integer(std::numeric_limits<std::int64_t>::min(), CStringView{ "minimum" }));
    attach(ctx, source, array, source.create_unsigned_integer(std::numeric_limits<std::uint64_t>::max(), CStringView{ "maximum" }));
    CIntegerMetadata hexadecimal;
    hexadecimal.domain = EIntegerDomain::signed_value;
    hexadecimal.notation = EIntegerNotation::hexadecimal;
    hexadecimal.prefix = EIntegerPrefix::alternate;
    hexadecimal.width = live_signed_integer_smallest_width(-128);
    attach(ctx, source, array, source.create_signed_integer(-128, hexadecimal, CStringView{ "hex" }));
    CIntegerMetadata binary;
    binary.domain = EIntegerDomain::unsigned_value;
    binary.notation = EIntegerNotation::binary;
    binary.width = live_unsigned_integer_smallest_width(256u);
    attach(ctx, source, array, source.create_unsigned_integer(256u, binary, CStringView{ "binary" }));
    attach(ctx, source, array, source.create_floating_point(-0.0, CStringView{ "float" }));
    const std::uint8_t unicode[]{ 'x', 0u, 0xc3u, 0xa9u, 0xf0u, 0x9du, 0x84u, 0x9eu, '\r', '\n' };
    attach(ctx, source, array, source.create_string(CStringView{ unicode, sizeof(unicode) }, CStringView{ unicode, sizeof(unicode) }));
    attach(ctx, source, array, source.create_string(CStringView{ "" }, CStringView{ "empty" }));
    attach(ctx, source, array, source.create_array(CStringView{ "array" }));
    const CNodeKey object = source.create_object(CStringView{ "object" });
    attach(ctx, source, array, object);
    attach(ctx, source, object, source.create_null(CStringView{ "$morphic" }));
    const CNodeKey redundant = source.create_object();
    attach(ctx, source, array, redundant);
    attach(ctx, source, redundant, source.create_boolean(false, CStringView{ "singleton" }));
    attach(ctx, source, array, source.create_object());
    const CNodeKey multi = source.create_object();
    attach(ctx, source, array, multi);
    attach(ctx, source, multi, source.create_null(CStringView{ "a" }));
    attach(ctx, source, multi, source.create_null(CStringView{ "b" }));

    for (unsigned count = 0u; count < 4u; ++count)
    {
        const std::string name = std::string(count + 1u, '$') + "morphic";
        const CNodeKey recovered = source.create_recovered_array(CStringView{ name.data(), name.size() });
        attach(ctx, source, array, recovered);
        for (unsigned i = 0u; i < count; ++i)
        {
            const CNodeKey competitor = source.create_object();
            attach(ctx, source, recovered, competitor);
            attach(ctx, source, competitor, source.create_unsigned_integer(i, CStringView{ "n" }));
        }
    }
    const CNodeKey recovery_array = source.create_recovered_array();
    attach(ctx, source, array, recovery_array);
    attach(ctx, source, recovery_array, source.create_recovered_array());
    const CNodeKey competitor_array = source.create_array();
    attach(ctx, source, recovery_array, competitor_array);
    attach(ctx, source, competitor_array, source.create_recovered_array(CStringView{ "nested" }));
    const CNodeKey lookalike = source.create_object(CStringView{ "$morphic" });
    attach(ctx, source, source.root(), lookalike);
    attach(ctx, source, lookalike, source.create_unsigned_integer(2u, CStringView{ "v" }));
    attach(ctx, source, lookalike, source.create_string(CStringView{ "array" }, CStringView{ "type" }));
    attach(ctx, source, lookalike, source.create_array(CStringView{ "values" }));

    const CNodeKey numbers = source.create_array(CStringView{ "numbers" });
    attach(ctx, source, source.root(), numbers);
    for (const double value : { std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(), std::numeric_limits<double>::max() })
    {
        attach(ctx, source, numbers, source.create_floating_point(value));
    }
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (live_floating_point_is_finite(value))
        {
            attach(ctx, source, numbers, source.create_floating_point(value));
        }
    }
    TEST_EXPECT(ctx, source.check_integrity());
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(source, baked));
    for (unsigned variant = 0u; variant < 8u; ++variant)
    {
        CDocumentWriteOptions options;
        const bool strict = (variant & 1u) != 0u;
        options.mode = strict ? EDocumentWriteMode::strict_json : EDocumentWriteMode::morphic;
        options.escape_non_ascii = (variant & 2u) != 0u;
        options.pretty_print = (variant & 4u) != 0u;
        options.line_ending = EDocumentWriteLineEnding::crlf;
        const auto written = document_writer::write(baked.document(), options);
        TEST_EXPECT(ctx, written.report.succeeded());
        const auto linted = text_linter::lint(CByteConstView{ written.output.data(), written.output.size() }, 0u);
        TEST_EXPECT(ctx, linted.report.success && linted.report.normalised_line_endings == 0u);
        CLiveDocument parsed;
        const auto report = document_parser::parse(CStringView{ linted.output.data(), linted.report.logical_text_byte_size }, parsed);
        TEST_EXPECT(ctx, report.succeeded());
        if (!report.succeeded())
        {
            continue;
        }
        TEST_EXPECT(ctx, parsed.check_integrity() && parsed.is_complete());
        TEST_EXPECT(ctx, report.interpretations.recovered_arrays_decoded == written.report.recovered_arrays_written);
        TEST_EXPECT(ctx, report.interpretations.reserved_names_unescaped == written.report.reserved_property_names_escaped);
        TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 0u && report.interpretations.singleton_objects_unwrapped > 0u);
        TEST_EXPECT(ctx, report.structure.required_relaxations == 0u);
        TEST_EXPECT(ctx, (report.structure.numeric_extensions == 0u) == strict);
        expect_same_semantics(ctx, source, parsed, strict);
        CBakedDocumentBlock rebaked;
        TEST_EXPECT(ctx, document_translation::bake(parsed, rebaked));
        const auto rewritten = document_writer::write(rebaked.document(), options);
        TEST_EXPECT(ctx, rewritten.report.succeeded());
        TEST_EXPECT(ctx, rewritten.report.logical_text_byte_size == written.report.logical_text_byte_size);
        if (rewritten.report.logical_text_byte_size == written.report.logical_text_byte_size)
        {
            TEST_EXPECT(ctx, std::memcmp(rewritten.output.data(), written.output.data(), written.report.logical_text_byte_size) == 0);
        }
    }
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
    const std::string text = "'n\\u0000':'\\u0000\\u00e9',a:[{s:'\\uD834\\uDD1E'},+0x80,{},[],{d:1,d:2}],"
        "r:{$morphic:{values:[{n:1},[{a:2}]," + recovery("false") + "],type:'recovered-\\u0061rray',v:1}},"
        "r:{x:1,x:2},r:" + recovery("") + ",$$morphic:0,'\\u0024$morphic':1,b:'last'";
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
                expect_no_interpretations(ctx, report);
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

    constexpr std::size_t recovery_depth = 512u;
    std::string recovered = "null";
    for (std::size_t i = 0u; i < recovery_depth; ++i)
    {
        recovered = recovery(recovered);
    }
    const auto nested = parse("r:" + recovered, destination);
    TEST_EXPECT(ctx, nested.succeeded() && nested.interpretations.recovered_arrays_decoded == recovery_depth);
    TEST_EXPECT(ctx, destination.value_count() == recovery_depth + 2u && destination.check_integrity());
    TEST_EXPECT(ctx, write(ctx, destination) == "{\"r\":" + recovered + "}");
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
    document_parser_tests::test_recovery_and_collisions(ctx);
    document_parser_tests::test_singleton_contexts(ctx);
    document_parser_tests::test_protocol_failures(ctx);
    document_parser_tests::test_round_trips(ctx);
    document_parser_tests::test_depth_and_allocation(ctx);
    std::cout << "DocumentParser: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
