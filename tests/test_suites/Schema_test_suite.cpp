//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    Schema_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Schema boundary, lifecycle and allocation tests with compiler-check fixtures.

#include "tests/test_suites/Schema_test_suite.hpp"
#include "schema/resolved_schema.hpp"
#include "schema/document_query.hpp"
#include "memory/memory_policies.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/live_document.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/environment/test_paths.hpp"
#include "types/fp16data_t.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/path/native_path.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace schema_tests
{
using namespace schema;
using tests::TTestContext;
static_assert(sizeof(CSchemaIndex) == 4u);
static_assert(!std::is_copy_constructible_v<CResolvedSchema>);
static_assert(std::is_nothrow_move_constructible_v<CResolvedSchema>);
static_assert(std::is_nothrow_move_assignable_v<CResolvedSchema>);

static const char* const fixture = R"({"types":{
 "structures":{
  "Nested":{"members":[{"tag":{"type":"u8"}},{"position":{"type":"Vector4"}},{"flags":{"type":"Flags"}},{"tail":{"type":"u64"}}]},
  "Vector4":{"detail":{"alignment":4,"size":16,"internal":true},"members":[{"x":{"type":"f32"}},{"y":{"type":"f32","default":-0.0}},{"z":{"type":"f32"}},{"w":{"type":"f32"}}]},
  "Arrays":{"members":[{"rows":{"type":{"element":{"element":"f32","count":2},"count":3},"default":[[1.0],[2.0,3.0]]}},{"single":{"type":{"element":"Mode","count":1},"default":[]}},{"records":{"type":{"element":"Nested","count":2}}}]},
  "Atoms":{"members":[{"a":{"type":"i8","default":-128}},{"b":{"type":"u8","default":255}},{"c":{"type":"i16","default":-32768}},{"d":{"type":"u16","default":65535}},{"e":{"type":"i32","default":-2147483648}},{"f":{"type":"u32","default":4294967295}},{"g":{"type":"i64","default":-9223372036854775808}},{"h":{"type":"u64","default":18446744073709551615}},{"i":{"type":"f16","default":1e100}},{"j":{"type":"f32","default":"-INF"}},{"k":{"type":"f64","default":"NaN"}},{"l":{"type":"b8","default":-0.01}},{"m":{"type":"Mode","default":"alias"}},{"n":{"type":"SignedMask"}}]},
  "_ascii7":{"members":[{"std":{"type":{"element":"i64","count":2}}},{"fp16data_t":{"type":"f16"}},{"b8":{"type":"b8"}},{"_lower2":{"type":"u64"}},{"_ascii7":{"type":"u8"}}]}
 },
 "enumerations":{
  "Mode":{"storage":"u8","values":{"first":2,"alias":2,"last":7,"Mode":1}},
  "Signed":{"storage":"i8","values":{"negative":-4,"positive":+3}},
  "WideSigned":{"storage":"i64","values":{"minimum":-9223372036854775808,"maximum":9223372036854775807}},
  "WideUnsigned":{"storage":"u64","values":{"maximum":18446744073709551615}}
 },
 "bit_structures":{
  "Flags":{"storage":"u16","members":[{"visible":{"type":"b8","mask":1,"default":true}},{"mode":{"type":"Mode","mask":14,"default":"last"}},{"axis":{"type":"Signed","mask":112,"default":"negative"}},{"normal":{"type":"u8","mask":384,"interpretation":"unorm","default":0.5}}]},
  "SignedMask":{"storage":"i64","members":[{"all":{"type":"u64","mask":18446744073709551615,"default":18446744073709551615}}]}
 }},"instances":{"untouched":null},"data":{"untouched":[null]}})";

static bool bake(const std::string& text, CBakedDocumentBlock& block)
{
    CLiveDocument live;
    //  The ordinary parser policy accepts Morphic numbers and rejects collision
    //  extension. Never bake a policy-rejected partial/merged schema document.
    const CDocumentReport report = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), 1u }, live);
    if (!report.accepted() && report.failure.reason != EDocumentFailureReason::none)
    {
        std::cerr << "Schema fixture parse failure " << static_cast<unsigned>(report.failure.reason) << ": "
                  << text.substr(0u, 140u) << '\n';
    }
    return report.accepted() && document_translation::bake(live, block);
}

static bool resolve(TTestContext& ctx, const std::string& text, CBakedDocumentBlock& block, CResolvedSchema& schema)
{
    TEST_EXPECT(ctx, bake(text, block));
    SDiagnostic error;
    const bool ok = schema.resolve(block.document(), error);
    if (!ok)
    {
        std::cerr << "Schema unexpected failure: reason " << static_cast<unsigned>(error.reason) << " stage "
                  << static_cast<unsigned>(error.stage) << " occurrence " << error.occurrence.query_value() << '\n';
    }
    TEST_EXPECT(ctx, ok);
    return ok;
}

static std::string one(const std::string& type, const std::string& value)
{
    return "{\"types\":{\"structures\":{\"Test\":{\"members\":[{\"value\":{\"type\":" + type +
           (value.empty() ? "" : ",\"default\":" + value) + "}}]}}}}";
}

static SDefault first_default(
    TTestContext& ctx, const CResolvedSchema& s, const char* const type, const char* const member)
{
    SMember m;
    SDefault d;
    TEST_EXPECT(ctx, s.member(s.find_member(s.find_type(CStringView{ type }), CStringView{ member }), m));
    TEST_EXPECT(ctx, s.default_value(m.type, m.default_description, d));
    return d;
}

static void expect_failure(TTestContext& ctx, const std::string& text, const EReason reason = EReason::none)
{
    CBakedDocumentBlock block;
    CResolvedSchema s;
    TEST_EXPECT(ctx, bake(text, block));
    SDiagnostic error;
    TEST_EXPECT(ctx, !s.resolve(block.document(), error));
    TEST_EXPECT(ctx, !s.is_ready() && s.document() == nullptr && s.definition_count() == 0u);
    TEST_EXPECT(ctx, error.reason != EReason::none && error.stage != EStage::none);
    if (reason != EReason::none)
    {
        TEST_EXPECT(ctx, error.reason == reason);
    }
}

//  Separate compiler-validation source derived from the resolved observations.
//  Generated declarations themselves contain no assertions or operational code.
static std::string write_validation(TTestContext& ctx, const CResolvedSchema& s, const char* const stem = "schema")
{
    CByteBuffer output;
    SDiagnostic error;
    TEST_EXPECT(ctx, generate_cpp(s, CStringView{ "schema_fixture" }, output, error));
    if (!output.is_ready())
    {
        return {};
    }
    const std::string declarations{ reinterpret_cast<const char*>(output.data()), output.size() - 1u };
    const std::string header_filename = std::string(stem) + "_generated.hpp";
    const std::string checks_filename = std::string(stem) + "_layout.cpp";
    const std::string header_path = test_environment::test_output_path(header_filename.c_str());
    const std::string checks_path = test_environment::test_output_path(checks_filename.c_str());
    const auto write_file = [&](const std::string& path, const void* const data, const std::size_t size)
    {
        std::FILE* const file = platform::filesystem::openFile(
            platform::path::makeNativePath(path.c_str()), platform::filesystem::EOpenMode::BinaryWrite);
        TEST_EXPECT(ctx, file != nullptr);
        if (file)
        {
            TEST_EXPECT(ctx, std::fwrite(data, 1u, size, file) == size);
            TEST_EXPECT(ctx, std::fclose(file) == 0);
        }
    };
    write_file(header_path, output.data(), output.size() - 1u);
    std::ostringstream checks;
    checks << "#include \"" << header_path << "\"\n#include <cstddef>\n#include <type_traits>\n";
    checks << "static_assert(std::is_standard_layout_v<fp16data_t> && std::is_trivially_copyable_v<fp16data_t>);\n";
    const auto spelling = [&](const CPropertyNameId id)
    {
        const CStringView name = s.document()->property_name(id);
        return std::string(reinterpret_cast<const char*>(name.string()), name.length());
    };
    const char* const primitives[] = { "", "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t",
        "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t", "fp16data_t", "float", "double",
        "schema_fixture::b8" };
    for (std::uint32_t n = 0u; n < s.definition_count(); ++n)
    {
        const CSchemaIndex id = s.definition_at(n);
        SType t;
        TEST_EXPECT(ctx, s.type(id, t));
        const std::string type_name = "schema_fixture::" + spelling(t.name);
        if (t.category != ECategory::bit_structure)
        {
            checks << "static_assert(sizeof(" << type_name << ") == " << t.size << ");\n";
            checks << "static_assert(alignof(" << type_name << ") == " << t.alignment << ");\n";
            checks << "static_assert(std::is_standard_layout_v<" << type_name << "> && std::is_trivially_copyable_v<"
                   << type_name << ">);\n";
        }
        if (t.category == ECategory::structure)
        {
            for (std::uint32_t m = 0u; m < t.count; ++m)
            {
                SMember member;
                SType mt;
                TEST_EXPECT(ctx, s.member(s.member_at(id, m), member));
                TEST_EXPECT(ctx, s.type(member.type, mt));
                const std::string name = spelling(member.name);
                checks << "static_assert(offsetof(" << type_name << ", " << name << ") == " << member.offset << ");\n";
                checks << "static_assert(sizeof(" << type_name << "::" << name << ") == " << member.size << ");\n";
                for (unsigned dimension = 0u; mt.category == ECategory::array; ++dimension)
                {
                    checks << "static_assert(std::extent_v<decltype(" << type_name << "::" << name << "), " << dimension
                           << "> == " << mt.count << ");\n";
                    TEST_EXPECT(ctx, s.type(mt.element_or_storage, mt));
                }
            }
        }
        if (t.category == ECategory::enumeration)
        {
            SType storage;
            TEST_EXPECT(ctx, s.type(t.element_or_storage, storage));
            checks << "static_assert(std::is_same_v<std::underlying_type_t<" << type_name << ">, "
                   << primitives[static_cast<unsigned>(storage.primitive)] << ">);\n";
            for (std::uint32_t m = 0u; m < t.count; ++m)
            {
                SLabel label;
                TEST_EXPECT(ctx, s.label(s.label_at(id, m), label));
                checks << "static_assert(static_cast<" << primitives[static_cast<unsigned>(storage.primitive)] << ">("
                       << type_name << "::" << spelling(label.name) << ") == ";
                if (label.value.kind == EScalar::signed_integer)
                {
                    if (label.value.value.signed_value == INT64_MIN)
                    {
                        checks << "(-9223372036854775807LL - 1LL)";
                    }
                    else
                    {
                        checks << label.value.value.signed_value << "LL";
                    }
                }
                else
                {
                    checks << label.value.value.unsigned_value << "ULL";
                }
                checks << ");\n";
            }
        }
        if (t.category == ECategory::bit_structure)
        {
            SType storage;
            TEST_EXPECT(ctx, s.type(t.element_or_storage, storage));
            for (std::uint32_t m = 0u; m < t.count; ++m)
            {
                SField field;
                TEST_EXPECT(ctx, s.field(s.field_at(id, m), field));
                checks << "static_assert(static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<"
                       << primitives[static_cast<unsigned>(storage.primitive)] << ">>("
                       << type_name << "::" << spelling(field.name) << ")) == " << field.mask << "ULL);\n";
                checks << "static_assert(std::is_same_v<std::remove_cv_t<decltype(" << type_name
                       << "::" << spelling(field.name) << ")>, " << primitives[static_cast<unsigned>(storage.primitive)]
                       << ">);\n";
            }
        }
    }
    const std::string check_text = checks.str();
    write_file(checks_path, check_text.data(), check_text.size());
    std::cout << "Schema layout validation source: " << checks_path << '\n';
    return declarations;
}

static void test_success(TTestContext& ctx)
{
    CBakedDocumentBlock block;
    CResolvedSchema s;
    if (!resolve(ctx, fixture, block, s))
    {
        return;
    }
    TEST_EXPECT(ctx, s.definition_count() == 11u);
    SType t;
    const CSchemaIndex vector = s.find_type(CStringView{ "Vector4" });
    TEST_EXPECT(
        ctx, s.type(vector, t) && t.size == 16u && t.alignment == 4u && !t.gaps && t.named_components && t.internal);
    const char* const components[] = { "x", "y", "z", "w" };
    for (std::uint32_t n = 0u; n < 4u; ++n)
    {
        const CSchemaIndex member = s.member_at(vector, n);
        SMember m;
        TEST_EXPECT(ctx, member == s.find_member(vector, CStringView{ components[n] }));
        TEST_EXPECT(ctx, s.member(member, m) && m.offset == n * 4u);
        TEST_EXPECT(ctx, s.map_occurrence(m.source) == member);
        TEST_EXPECT(ctx, !s.type(member, t));
    }
    TEST_EXPECT(ctx, !s.member_at(vector, 4u) && !s.field_at(vector, 0u));
    const CSchemaIndex nested = s.find_type(CStringView{ "Nested" });
    TEST_EXPECT(ctx, s.type(nested, t) && t.size == 32u && t.alignment == 8u && t.gaps);
    const std::uint64_t offsets[] = { 0u, 4u, 20u, 24u };
    for (std::uint32_t n = 0u; n < 4u; ++n)
    {
        SMember m;
        TEST_EXPECT(ctx, s.member(s.member_at(nested, n), m) && m.offset == offsets[n]);
    }
    const CSchemaIndex arrays = s.find_type(CStringView{ "Arrays" });
    SMember rows;
    TEST_EXPECT(ctx, s.member(s.member_at(arrays, 0u), rows));
    TEST_EXPECT(ctx, s.type(rows.type, t) && t.count == 3u && t.size == 24u && t.stride == 8u);
    TEST_EXPECT(ctx, s.map_occurrence(t.source) == rows.type);
    SDefault def;
    TEST_EXPECT(ctx, s.default_value(rows.type, rows.default_description, def) && def.supplied_count == 2u);
    CSchemaIndex first, tail;
    TEST_EXPECT(ctx, s.default_element(rows.type, rows.default_description, 0u, first) && first);
    TEST_EXPECT(ctx, s.default_element(rows.type, rows.default_description, 2u, tail) && !tail);
    TEST_EXPECT(ctx, !s.default_element(rows.type, rows.default_description, 3u, tail));
    TEST_EXPECT(ctx, s.default_value(t.element_or_storage, first, def) && def.supplied_count == 1u);
    TEST_EXPECT(ctx, !s.map_occurrence(def.source));
    const CSchemaIndex mode = s.find_type(CStringView{ "Mode" });
    SLabel label;
    TEST_EXPECT(ctx, s.label(s.find_label(mode, CStringView{ "alias" }), label));
    TEST_EXPECT(ctx, s.first_label_for_value(mode, label.value) == s.label_at(mode, 0u));
    TEST_EXPECT(ctx, s.map_occurrence(label.source) == s.find_label(mode, CStringView{ "alias" }));
    TEST_EXPECT(ctx, s.default_value(mode, {}, def) && def.scalar.value.unsigned_value == 2u);
    SField field;
    TEST_EXPECT(ctx, s.field(s.find_field(s.find_type(CStringView{ "Flags" }), CStringView{ "axis" }), field));
    TEST_EXPECT(ctx, field.mask == 112u && field.shift == 4u && field.width == 3u && field.signed_value);
    TEST_EXPECT(ctx,
        s.map_occurrence(field.source) == s.find_field(s.find_type(CStringView{ "Flags" }), CStringView{ "axis" }));
    TEST_EXPECT(ctx, s.type(s.find_type(CStringView{ "SignedMask" }), t) && !t.gaps);
    TEST_EXPECT(ctx, first_default(ctx, s, "Atoms", "g").scalar.value.signed_value == INT64_MIN);
    TEST_EXPECT(ctx, first_default(ctx, s, "Atoms", "h").scalar.value.unsigned_value == UINT64_MAX);
    TEST_EXPECT(ctx, first_default(ctx, s, "Atoms", "i").scalar.value.unsigned_value == fp16data_t{ 1e100 }.getBits());
    TEST_EXPECT(ctx, std::signbit(first_default(ctx, s, "Vector4", "y").scalar.value.floating_value));
    TEST_EXPECT(ctx, std::isnan(first_default(ctx, s, "Atoms", "k").scalar.value.floating_value));
    TEST_EXPECT(ctx, !s.map_occurrence(block.document().root()));
    TEST_EXPECT(ctx, !s.find_type(CStringView{ "missing" }));
    const std::string declarations = write_validation(ctx, s);
    const std::size_t mode_position = declarations.find("enum class Mode");
    const std::size_t flags_position = declarations.find("namespace Flags");
    const std::size_t nested_position = declarations.find("struct Nested\n");
    TEST_EXPECT(ctx, (mode_position != std::string::npos) && (flags_position != std::string::npos) &&
        (nested_position != std::string::npos) && (mode_position < flags_position) && (flags_position < nested_position));
    for (const char* const spelling : {
        "using b8 = std::int8_t;", "first = 2u,", "negative = -4,", "positive = 3,",
        "minimum = (-0x7fffffffffffffff - 1),", "maximum = 0x7fffffffffffffff,",
        "maximum = 0xffffffffffffffffu,", "all = static_cast<std::int64_t>(0xffffffffffffffffu);",
        "std::int64_t std[2];", "std::uint64_t _lower2;", "Vector4 position;",
        "Mode single[1];", "Nested records[2];", "::schema_fixture::b8 b8;" })
    {
        TEST_EXPECT(ctx, declarations.find(spelling) != std::string::npos);
    }
    TEST_EXPECT(ctx, declarations.find("::schema_fixture::Vector4") == std::string::npos);
    TEST_EXPECT(ctx, declarations.find("::schema_fixture::Mode") == std::string::npos);
    TEST_EXPECT(ctx, declarations.find("::schema_fixture::Nested") == std::string::npos);
    for (const char* const name : { "class", "_reserved", "a__b", "std", "fp16data_t", "a::b", "\xc3\xa9" })
    {
        CByteBuffer output;
        SDiagnostic error;
        TEST_EXPECT(ctx, !generate_cpp(s, CStringView{ name }, output, error));
        TEST_EXPECT(ctx, error.reason == EReason::invalid_identifier && !output.is_ready());
    }
    CResolvedSchema moved{ std::move(s) };
    TEST_EXPECT(ctx, !s.is_ready() && !s.document() && moved.type(vector, t));
    CBakedDocumentBlock replacement;
    TEST_EXPECT(ctx, bake(one("\"u8\"", "7"), replacement));
    SDiagnostic replacement_error;
    TEST_EXPECT(ctx, s.resolve(replacement.document(), replacement_error) && s.definition_count() == 1u);
    s = std::move(moved);
    TEST_EXPECT(ctx, !moved.is_ready() && s.type(vector, t) && s.definition_count() == 11u);
    SDiagnostic error;
    const CBakedDocument* const borrowed = s.document();
    TEST_EXPECT(ctx, s.resolve(*borrowed, error));
    CBakedDocumentBlock invalid;
    TEST_EXPECT(ctx, bake(one("\"missing\"", ""), invalid));
    TEST_EXPECT(ctx, !s.resolve(invalid.document(), error));
    TEST_EXPECT(ctx, !s.is_ready() && !s.document() && !s.type(vector, t));
    TEST_EXPECT(ctx, error.occurrence && error.enclosing_type && error.enclosing_member);
    TEST_EXPECT(ctx, invalid.document().contains(error.occurrence));
    CByteBuffer output;
    TEST_EXPECT(ctx, !generate_cpp(s, CStringView{ "valid" }, output, error) && !output.is_ready());
}

static void test_failures(TTestContext& ctx)
{
    expect_failure(ctx, "{}", EReason::missing_property);
    expect_failure(ctx, "{\"types\":{},\"unknown\":{}}", EReason::unknown_property);
    expect_failure(ctx, "{\"types\":{},\"instances\":null}", EReason::invalid_input);
    expect_failure(ctx, "{\"types\":{\"structures\":{\"Test\":{\"members\":[]}}}}", EReason::invalid_input);
    expect_failure(ctx, one("\"missing\"", ""), EReason::unknown_type);
    expect_failure(ctx, one("\"Test\"", ""), EReason::cycle);
    expect_failure(ctx, one("{\"element\":\"u8\",\"count\":0}", ""), EReason::invalid_range);
    expect_failure(ctx, one("{\"kind\":\"array\",\"element\":\"u8\",\"count\":1}", ""), EReason::unknown_property);
    expect_failure(ctx, one("{\"element\":\"u8\"}", ""), EReason::missing_property);
    expect_failure(ctx, one("{\"element\":{\"element\":\"u64\",\"count\":4294967295},\"count\":4294967295}", ""),
        EReason::storage_limit);
    expect_failure(ctx,
        "{\"types\":{\"structures\":{\"Bad\":{\"members\":[{\"x\":{\"type\":\"u8\"}},{\"x\":{\"type\":\"u8\"}}]}}}}",
        EReason::duplicate_declaration);
    for (const char* const name : { "class", "bad-name", "__bad", "_Upper", "i32", "na\\u00efve", "\\u03b1" })
    {
        const std::string text =
            "{\"types\":{\"structures\":{\"" + std::string(name) + "\":{\"members\":[{\"x\":{\"type\":\"u8\"}}]}}}}";
        expect_failure(ctx, text);
    }
    for (const char* const member : { "\"offset\":0", "\"strange\":0" })
    {
        expect_failure(ctx, "{\"types\":{\"structures\":{\"Test\":{\"members\":[{\"x\":{\"type\":\"u8\"," +
                                std::string(member) + "}}]}}}}");
    }
    for (const char* const detail :
        { "\"alignment\":2", "\"alignment\":0", "\"alignment\":3", "\"size\":2", "\"internal\":1", "\"other\":0" })
    {
        expect_failure(ctx, "{\"types\":{\"structures\":{\"Test\":{\"detail\":{" + std::string(detail) +
                                "},\"members\":[{\"x\":{\"type\":\"u8\"}}]}}}}");
    }
    for (const char* const value : { "null", "128", "-129", "0.5", "\"1\"", "true", "18446744073709551615" })
    {
        expect_failure(ctx, one("\"i8\"", value));
    }
    for (const char* const value : { "-1", "256", "1.1", "\"NaN\"" })
    {
        expect_failure(ctx, one("\"u8\"", value));
    }
    expect_failure(ctx, one("\"u64\"", "18446744073709551616.0"));
    expect_failure(ctx, one("\"i64\"", "9223372036854775808.0"));
    expect_failure(ctx, one("\"f32\"", "1e100"));
    expect_failure(ctx, one("\"f32\"", "1e-100"));
    for (const char* const value : { "\" nan\"", "\"+nan\"", "\"nan(1)\"", "\"1.0\"", "null", "true" })
    {
        expect_failure(ctx, one("\"f64\"", value));
    }
    for (const char* const value : { "\"NaN\"", "\"false\"", "null" })
    {
        expect_failure(ctx, one("\"b8\"", value));
    }
    for (const char* const value : { "[1,2,3]", "[null]", "{\"x\":1}", "[ {\"x\":1} ]" })
    {
        expect_failure(ctx, one("{\"element\":\"i8\",\"count\":2}", value));
    }
    expect_failure(ctx,
        "{\"types\":{\"structures\":{\"Outer\":{\"members\":[{\"v\":{\"type\":\"Inner\",\"default\":{}}}]},\"Inner\":{\"members\":[{\"x\":{\"type\":\"u8\"}}]}}}}",
        EReason::invalid_default);
    const std::string bit_prefix = "{\"types\":{\"bit_structures\":{\"Bits\":{\"storage\":\"u8\",\"members\":[";
    for (const char* const mask : { "0", "5", "256", "-1", "0.5" })
    {
        expect_failure(ctx, bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":" + mask + "}}]}}}}");
    }
    expect_failure(ctx, bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":3}},{\"y\":{\"type\":\"u8\",\"mask\":2}}]}}}}",
        EReason::invalid_layout);
    expect_failure(
        ctx, bit_prefix + "{\"x\":{\"type\":\"i8\",\"mask\":3,\"default\":2}}]}}}}", EReason::invalid_default);
    expect_failure(ctx,
        bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":3,\"interpretation\":\"unorm\",\"default\":1.01}}]}}}}",
        EReason::invalid_default);
    expect_failure(ctx, bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":3,\"interpretation\":\"snorm\"}}]}}}}",
        EReason::unsupported_feature);
    CBakedDocumentBlock collision;
    TEST_EXPECT(ctx, !bake("{\"types\":{},\"types\":{}}", collision));
    TEST_EXPECT(ctx, !collision.is_ready());

    const auto with_enum = [](const std::string& storage, const std::string& labels, const std::string& value)
    {
        return "{\"types\":{\"enumerations\":{\"Choice\":{\"storage\":\"" + storage + "\",\"values\":" + labels +
               "}},\"structures\":{\"Record\":{\"members\":[{\"choice\":{\"type\":\"Choice\",\"default\":" + value +
               "}}]}}}}";
    };
    expect_failure(ctx, with_enum("u8", "{\"first\":1}", "1"), EReason::invalid_default);
    expect_failure(ctx, with_enum("u8", "{\"first\":1}", "\"other\""), EReason::invalid_default);
    expect_failure(ctx, with_enum("u8", "{}", "\"first\""));
    expect_failure(ctx, with_enum("u8", "{\"first\":256}", "\"first\""), EReason::invalid_range);
    expect_failure(ctx, with_enum("i8", "{\"first\":0.5}", "\"first\""), EReason::invalid_range);
    expect_failure(ctx, with_enum("f32", "{\"first\":1}", "\"first\""), EReason::invalid_range);
    expect_failure(ctx,
        "{\"types\":{\"enumerations\":{\"Repeated\":{\"storage\":\"u8\",\"values\":{\"x\":1}}},\"structures\":{\"Repeated\":{\"members\":[{\"x\":{\"type\":\"u8\"}}]}}}}",
        EReason::duplicate_declaration);
    expect_failure(ctx,
        "{\"types\":{\"enumerations\":{\"Choice\":{\"storage\":\"i8\",\"values\":{\"x\":2}}},\"bit_structures\":{\"Bits\":{\"storage\":\"u8\",\"members\":[{\"x\":{\"type\":\"Choice\",\"mask\":3}}]}}}}",
        EReason::invalid_range);
    expect_failure(ctx,
        "{\"types\":{\"structures\":{\"A\":{\"members\":[{\"b\":{\"type\":{\"element\":\"B\",\"count\":1}}}]},\"B\":{\"members\":[{\"a\":{\"type\":\"A\"}}]}}}}",
        EReason::cycle);
    expect_failure(ctx,
        "{\"types\":{\"structures\":{\"Outer\":{\"members\":[{\"v\":{\"type\":{\"element\":\"Inner\",\"count\":1},\"default\":[]}}]},\"Inner\":{\"members\":[{\"x\":{\"type\":\"u8\"}}]}}}}",
        EReason::invalid_default);
    {
        CBakedDocumentBlock block;
        TEST_EXPECT(
            ctx, bake(bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":6}},{\"y\":{\"type\":\"u8\",\"mask\":12}}]}}}}",
                     block));
        CResolvedSchema s;
        SDiagnostic error;
        TEST_EXPECT(ctx, !s.resolve(block.document(), error));
        TEST_EXPECT(ctx, error.ranges_available && error.related && error.range_begin == 2u && error.range_end == 4u &&
                             error.related_begin == 1u && error.related_end == 3u);
    }
    {
        const std::string path = test_environment::repository_path("docs/schema/schema-example.json");
        std::FILE* const file = platform::filesystem::openFile(
            platform::path::makeNativePath(path.c_str()), platform::filesystem::EOpenMode::BinaryRead);
        TEST_EXPECT(ctx, file != nullptr);
        if (file)
        {
            std::string sample;
            char buffer[4096];
            std::size_t count;
            while ((count = std::fread(buffer, 1u, sizeof(buffer), file)) != 0u)
            {
                sample.append(buffer, count);
            }
            TEST_EXPECT(ctx, std::fclose(file) == 0);
            expect_failure(ctx, sample, EReason::unsupported_feature);
        }
    }
}

static void test_scalars_and_limits(TTestContext& ctx)
{
    {
        CBakedDocumentBlock block;
        CResolvedSchema s;
        if (resolve(ctx, one("\"f32\"", "16777217"), block, s))
        {
            TEST_EXPECT(ctx, first_default(ctx, s, "Test", "value").scalar.value.floating_value == 16777216.0);
        }
        if (resolve(ctx, one("\"f32\"", "1e-40"), block, s))
        {
            TEST_EXPECT(
                ctx, first_default(ctx, s, "Test", "value").scalar.value.floating_value == static_cast<double>(1e-40f));
        }
        if (resolve(ctx, one("\"b8\"", "-0.0"), block, s))
        {
            TEST_EXPECT(ctx, first_default(ctx, s, "Test", "value").scalar.value.unsigned_value == 0u);
        }
        if (resolve(ctx, one("\"f16\"", "-0.0"), block, s))
        {
            TEST_EXPECT(
                ctx, first_default(ctx, s, "Test", "value").scalar.value.unsigned_value == fp16data_t{ -0.0 }.getBits());
        }
    }
    for (const char* const value : { "-0.0", "127.0", "+0", "-128" })
    {
        CBakedDocumentBlock block;
        CResolvedSchema s;
        resolve(ctx, one("\"i8\"", value), block, s);
    }
    for (const char* const value : { "1e-40", "16777217", "\"infinity\"", "\"+INF\"", "\"-infinity\"" })
    {
        CBakedDocumentBlock block;
        CResolvedSchema s;
        resolve(ctx, one("\"f32\"", value), block, s);
    }
    for (const char* const value : { "1e-100", "1e100", "\"inf\"", "\"nan\"", "-0.0" })
    {
        CBakedDocumentBlock block;
        CResolvedSchema s;
        resolve(ctx, one("\"f16\"", value), block, s);
    }
    CBakedDocumentBlock block;
    CResolvedSchema s;
    if (resolve(ctx, one("{\"element\":\"u8\",\"count\":1000000000}", "[1]"), block, s))
    {
        SMember member;
        SType t;
        SDefault d;
        TEST_EXPECT(ctx, s.member(s.member_at(s.definition_at(0u), 0u), member));
        TEST_EXPECT(ctx, s.type(member.type, t) && t.size == 1000000000u);
        TEST_EXPECT(ctx, s.default_value(member.type, member.default_description, d) && d.supplied_count == 1u);
    }
    std::string nested = "\"u8\"";
    for (unsigned n = 0u; n < 260u; ++n)
    {
        nested = "{\"element\":" + nested + ",\"count\":1}";
    }
    expect_failure(ctx, one(nested, ""), EReason::storage_limit);
}

static void test_record_layout(TTestContext& ctx)
{
    const SRecordSizes sizes = CResolvedSchema::record_sizes();
    TEST_EXPECT(ctx, (sizes.type == 32u) && (sizes.member == 24u) && (sizes.label == 24u) &&
        (sizes.field == 32u) && (sizes.default_value == 32u) && (sizes.mapping == 8u));

    CBakedDocumentBlock block;
    CResolvedSchema schema;
    //  These describe large extents without allocating instance payloads.
    for (const char* const element : {
        R"({"element":"u8","count":2147483648})",
        R"({"element":"u64","count":268435456})",
        R"({"element":{"element":"u8","count":65536},"count":32768})" })
    {
        if (resolve(ctx, one(element, ""), block, schema))
        {
            SMember member;
            SType structure, array;
            const CSchemaIndex structure_index = schema.find_type(CStringView{ "Test" });
            TEST_EXPECT(ctx, schema.type(structure_index, structure));
            TEST_EXPECT(ctx, schema.member(schema.member_at(structure_index, 0u), member));
            TEST_EXPECT(ctx, schema.type(member.type, array));
            TEST_EXPECT(ctx, (structure.size == memory::k_byte_size_ceiling) && (structure.stride == structure.size));
            TEST_EXPECT(ctx, (member.offset == 0u) && (member.size == structure.size) && (array.size == member.size));
            TEST_EXPECT(ctx, (array.category == ECategory::array) && (array.primitive == EPrimitive::none));
            if (array.count == 32768u)
            {
                TEST_EXPECT(ctx, array.stride == 65536u);
            }
        }
    }
    const char* const padded_ceiling = R"({"types":{"structures":{"Test":{"detail":{"size":2147483648,"alignment":8},
        "members":[{"lead":{"type":"u64"}},{"tail":{"type":{"element":"u8","count":2147483639}}}]}}}})";
    if (resolve(ctx, padded_ceiling, block, schema))
    {
        SType structure;
        TEST_EXPECT(ctx, schema.type(schema.definition_at(0u), structure));
        TEST_EXPECT(ctx, (structure.size == memory::k_byte_size_ceiling) && (structure.alignment == 8u) && structure.gaps);
    }
    for (const char* const element : {
        R"({"element":"u8","count":2147483649})",
        R"({"element":"u64","count":268435457})",
        R"({"element":{"element":"u8","count":2147483648},"count":4294967295})",
        R"({"element":{"element":{"element":"u64","count":4294967295},"count":4294967295},"count":4294967295})" })
    {
        expect_failure(ctx, one(element, ""), EReason::storage_limit);
    }
    expect_failure(ctx, one(R"({"element":"u8","count":4294967296})", ""), EReason::invalid_range);
    expect_failure(ctx, R"({"types":{"structures":{"Test":{"members":[{"lead":{"type":"u8"}},
        {"tail":{"type":{"element":"u8","count":2147483648}}}]}}}})", EReason::storage_limit);
    expect_failure(ctx, R"({"types":{"structures":{"Test":{"members":[{"lead":{"type":{"element":"u8","count":2147483647}}},
        {"tail":{"type":"u64"}}]}}}})", EReason::storage_limit);
    for (const char* const detail : { R"("size":2147483649)", R"("size":18446744073709551615)", R"("alignment":4294967296)" })
    {
        expect_failure(ctx, "{\"types\":{\"structures\":{\"Test\":{\"detail\":{" + std::string(detail) +
            "},\"members\":[{\"value\":{\"type\":\"u8\"}}]}}}}", EReason::storage_limit);
    }

    const char* const padded_members = R"({"types":{"structures":{
        "Outer":{"members":[{"lead":{"type":"u8"}},{"nested":{"type":"Padded"}},
            {"rows":{"type":{"element":"Padded","count":3}}}]},
        "Padded":{"members":[{"wide":{"type":"u32"}},{"tail":{"type":"u8"}}]}}}})";
    if (resolve(ctx, padded_members, block, schema))
    {
        const CSchemaIndex outer = schema.find_type(CStringView{ "Outer" });
        SMember nested, rows;
        TEST_EXPECT(ctx, schema.member(schema.member_at(outer, 1u), nested));
        TEST_EXPECT(ctx, schema.member(schema.member_at(outer, 2u), rows));
        TEST_EXPECT(ctx, (nested.offset == 4u) && (nested.size == 8u) && (rows.offset == 12u) && (rows.size == 24u));
    }
    if (!resolve(ctx, fixture, block, schema))
    {
        return;
    }
    const char* const primitive_names[] = { "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f16", "f32", "f64", "b8" };
    const std::uint64_t primitive_sizes[] = { 1u, 2u, 4u, 8u, 1u, 2u, 4u, 8u, 2u, 4u, 8u, 1u };
    constexpr std::size_t primitive_count = sizeof(primitive_names) / sizeof(primitive_names[0]);
    static_assert((sizeof(primitive_sizes) / sizeof(primitive_sizes[0])) == primitive_count);
    for (std::uint32_t primitive_ordinal = 0u; primitive_ordinal < primitive_count; ++primitive_ordinal)
    {
        SType primitive;
        TEST_EXPECT(ctx, schema.type(schema.find_type(CStringView{ primitive_names[primitive_ordinal] }), primitive));
        TEST_EXPECT(ctx, (primitive.category == ECategory::primitive) &&
            (primitive.primitive == static_cast<EPrimitive>(primitive_ordinal + 1u)) &&
            (primitive.size == primitive_sizes[primitive_ordinal]) && (primitive.alignment == primitive.size));
    }
    for (std::uint32_t definition_ordinal = 0u; definition_ordinal < schema.definition_count(); ++definition_ordinal)
    {
        const CSchemaIndex type_index = schema.definition_at(definition_ordinal);
        SType type;
        TEST_EXPECT(ctx, schema.type(type_index, type));
        if (type.category == ECategory::structure)
        {
            TEST_EXPECT(ctx, type.primitive == EPrimitive::none);
            TEST_EXPECT(ctx, !schema.find_label(type_index, CStringView{ "x" }) && !schema.find_field(type_index, CStringView{ "x" }));
        }
        else
        {
            SType storage;
            TEST_EXPECT(ctx, schema.type(type.element_or_storage, storage));
            TEST_EXPECT(ctx, (type.primitive == storage.primitive) && (type.alignment == storage.alignment));
            TEST_EXPECT(ctx, !schema.find_member(type_index, CStringView{ "x" }));
        }
        for (std::uint32_t child_ordinal = 0u; child_ordinal < type.count; ++child_ordinal)
        {
            if (type.category == ECategory::structure)
            {
                const CSchemaIndex member_index = schema.member_at(type_index, child_ordinal);
                SMember member;
                SType member_type;
                TEST_EXPECT(ctx, schema.member(member_index, member) && schema.type(member.type, member_type));
                TEST_EXPECT(ctx, (member.size == member_type.size) && ((member.offset + member.size) <= type.size));
                TEST_EXPECT(ctx, schema.find_member(type_index, schema.document()->property_name(member.name)) == member_index);
                TEST_EXPECT(ctx, !schema.find_member(member_index, CStringView{ "x" }));
            }
            else if (type.category == ECategory::enumeration)
            {
                const CSchemaIndex label_index = schema.label_at(type_index, child_ordinal);
                SLabel label;
                TEST_EXPECT(ctx, schema.label(label_index, label));
                TEST_EXPECT(ctx, schema.find_label(type_index, schema.document()->property_name(label.name)) == label_index);
                SLabel first_label;
                TEST_EXPECT(ctx, schema.label(schema.first_label_for_value(type_index, label.value), first_label));
                TEST_EXPECT(ctx, first_label.value.kind == label.value.kind);
                TEST_EXPECT(ctx, !schema.find_label(label_index, CStringView{ "x" }));
            }
            else
            {
                const CSchemaIndex field_index = schema.field_at(type_index, child_ordinal);
                SField field;
                SType logical;
                TEST_EXPECT(ctx, schema.field(field_index, field) && schema.type(field.type, logical));
                TEST_EXPECT(ctx, (field.primitive == logical.primitive) && (field.primitive != EPrimitive::none));
                TEST_EXPECT(ctx, schema.find_field(type_index, schema.document()->property_name(field.name)) == field_index);
                TEST_EXPECT(ctx, !schema.find_field(field_index, CStringView{ "x" }));
            }
        }
    }
    const CSchemaIndex flags = schema.find_type(CStringView{ "Flags" });
    SField axis;
    SType physical;
    TEST_EXPECT(ctx, schema.field(schema.find_field(flags, CStringView{ "axis" }), axis) && schema.type(flags, physical));
    TEST_EXPECT(ctx, (axis.primitive == EPrimitive::i8) && (physical.primitive == EPrimitive::u16));
    const CSchemaIndex vector = schema.find_type(CStringView{ "Vector4" });
    CResolvedSchema moved{ std::move(schema) };
    TEST_EXPECT(ctx, !schema.find_member(vector, CStringView{ "x" }) && moved.find_member(vector, CStringView{ "x" }));
    CBakedDocumentBlock invalid;
    TEST_EXPECT(ctx, bake(one(R"({"element":"u8","count":2147483649})", ""), invalid));
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, !moved.resolve(invalid.document(), diagnostic) && (diagnostic.reason == EReason::storage_limit));
    TEST_EXPECT(ctx, !moved.is_ready() && !moved.type(vector, physical) && !moved.find_member(vector, CStringView{ "x" }));
    TEST_EXPECT(ctx, !moved.find_field(flags, CStringView{ "axis" }) && !moved.find_label({}, CStringView{ "first" }));
    TEST_EXPECT(ctx, !moved.first_label_for_value({}, SScalar{}));
    TEST_EXPECT(ctx, diagnostic.occurrence && invalid.document().contains(diagnostic.occurrence));
    TEST_EXPECT(ctx, moved.resolve(block.document(), diagnostic));
    moved.clear();
    TEST_EXPECT(ctx, !moved.find_member(vector, CStringView{ "x" }) && !moved.find_field(flags, CStringView{ "axis" }));
}

static void test_review_regressions(TTestContext& ctx)
{
    struct SNamedDefaultCase
    {
        const char* type;
        const char* value;
        bool descend;
    };

    const SNamedDefaultCase cases[] = { { R"({"element":"u8","count":1})", R"([{"":1}])", false },
        { R"({"element":{"element":"u8","count":1},"count":1})", R"([[{"":1}]])", true },
        { R"({"element":{"element":"u8","count":1},"count":1})", R"([{"": [1]}])", false } };
    for (const SNamedDefaultCase& test : cases)
    {
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake(one(test.type, test.value), block));
        const CBakedDocument doc = block.document();
        const CBakedValueIndex types = doc.object_child(doc.root(), CStringView{ "types" });
        const CBakedValueIndex structures = doc.object_child(types, CStringView{ "structures" });
        const CBakedValueIndex definition = doc.object_child(structures, CStringView{ "Test" });
        const CBakedValueIndex members = doc.object_child(definition, CStringView{ "members" });
        const CBakedValueIndex member = doc.array_at(members, 0u);
        const CBakedValueIndex value = doc.object_child(member, CStringView{ "default" });
        CBakedValueIndex named = doc.array_at(value, 0u);
        if (test.descend)
        {
            named = doc.array_at(named, 0u);
        }
        TEST_EXPECT(ctx, named.is_valid() && doc.is_object_entry(named));
        TEST_EXPECT(ctx, doc.name(named).length() == 0u);
        CResolvedSchema s;
        SDiagnostic error;
        TEST_EXPECT(ctx, !s.resolve(doc, error));
        TEST_EXPECT(ctx, error.reason == EReason::invalid_default && error.occurrence == named);
        TEST_EXPECT(ctx, !s.is_ready() && s.document() == nullptr);
    }
    {
        CBakedDocumentBlock block;
        CResolvedSchema s;
        resolve(ctx, R"({"types":{"structures":{"Same":{"members":[{"Same":{"type":"u8"}}]}}}})", block, s);
    }
    for (const char* const primitive : { "i64", "f32", "f64" })
    {
        CBakedDocumentBlock block;
        CResolvedSchema s;
        if (!resolve(ctx, one("\"" + std::string(primitive) + "\"", ""), block, s))
        {
            continue;
        }
        const SDefault value = first_default(ctx, s, "Test", "value");
        TEST_EXPECT(ctx, value.kind == EDefault::scalar && !value.source);
        if (primitive[0] == 'i')
        {
            TEST_EXPECT(ctx, value.scalar.kind == EScalar::signed_integer && value.scalar.value.signed_value == 0);
        }
        else
        {
            TEST_EXPECT(ctx, value.scalar.kind == EScalar::floating_point && value.scalar.value.floating_value == 0.0 &&
                                 !std::signbit(value.scalar.value.floating_value));
        }
    }
}

static void test_representation(TTestContext& ctx)
{
    const char* const source = R"({"types":{
 "enumerations":{
  "Thresholds":{"storage":"i32","values":{"negative_decimal":-0xffff,"positive_decimal":+0xffff,"negative_hex":-0x10000,"positive_hex":+0x10000,"minimum":-0x80000000,"zero":+0}},
  "UnsignedThresholds":{"storage":"u32","values":{"decimal":0xffff,"hex":0x10000}},
  "SignedWidths":{"storage":"i64","values":{"negative_unsigned32":-0xffffffff,"negative_wide":-0x100000000,"positive_unsigned32":+0xffffffff}}
 },
 "structures":{
  "Extents":{"members":[{"decimal":{"type":{"element":"u8","count":65535}}},{"hex":{"type":{"element":"u8","count":0x10000}}}]}
 },
 "bit_structures":{
  "Unsigned8":{"storage":"u8","members":[{"low":{"type":"b8","mask":0x01}},{"high":{"type":"b8","mask":0x80}}]},
  "Signed8":{"storage":"i8","members":[{"low":{"type":"b8","mask":0x01}},{"high":{"type":"b8","mask":0x80}}]},
  "Unsigned16":{"storage":"u16","members":[{"low":{"type":"b8","mask":0x0001}},{"high":{"type":"b8","mask":0x8000}}]},
  "Signed16":{"storage":"i16","members":[{"low":{"type":"b8","mask":0x0001}},{"high":{"type":"b8","mask":0x8000}}]},
  "Unsigned32":{"storage":"u32","members":[{"low":{"type":"b8","mask":0x00000001}},{"high":{"type":"b8","mask":0x80000000}}]},
  "Signed32":{"storage":"i32","members":[{"low":{"type":"b8","mask":0x00000001}},{"high":{"type":"b8","mask":0x80000000}}]},
  "Unsigned64":{"storage":"u64","members":[{"low":{"type":"b8","mask":0x0000000000000001}},{"high":{"type":"b8","mask":0x8000000000000000}}]},
  "Signed64":{"storage":"i64","members":[{"low":{"type":"b8","mask":0x0000000000000001}},{"high":{"type":"b8","mask":0x8000000000000000}}]},
  "MaskNames":{"storage":"i32","members":[{"std":{"type":"b8","mask":0x00000001}},{"high":{"type":"b8","mask":0x80000000}}]}
 }}})";
    CBakedDocumentBlock block;
    CResolvedSchema schema;
    if (!resolve(ctx, source, block, schema))
    {
        return;
    }
    const std::string declarations = write_validation(ctx, schema, "schema_representation");
    for (const char* const spelling : {
        "negative_decimal = -65535,", "positive_decimal = 65535,", "negative_hex = -0x10000,",
        "positive_hex = 0x10000,", "minimum = -0x80000000ll,", "zero = 0,",
        "decimal = 65535u,", "hex = 0x10000u,", "negative_unsigned32 = -0xffffffffll,",
        "negative_wide = -0x100000000,", "positive_unsigned32 = 0xffffffff,",
        "std::uint8_t decimal[65535];", "std::uint8_t hex[0x10000];",
        "inline constexpr std::uint8_t low = 0x01u;", "inline constexpr std::uint8_t high = 0x80u;",
        "inline constexpr std::int8_t low = 0x01;", "inline constexpr std::int8_t high = static_cast<std::int8_t>(0x80u);",
        "inline constexpr std::uint16_t low = 0x0001u;", "inline constexpr std::uint16_t high = 0x8000u;",
        "inline constexpr std::int16_t low = 0x0001;", "inline constexpr std::int16_t high = static_cast<std::int16_t>(0x8000u);",
        "inline constexpr std::uint32_t low = 0x00000001u;", "inline constexpr std::uint32_t high = 0x80000000u;",
        "inline constexpr std::int32_t low = 0x00000001;", "inline constexpr std::int32_t high = static_cast<std::int32_t>(0x80000000u);",
        "inline constexpr std::uint64_t low = 0x0000000000000001u;", "inline constexpr std::uint64_t high = 0x8000000000000000u;",
        "inline constexpr std::int64_t low = 0x0000000000000001;", "inline constexpr std::int64_t high = static_cast<std::int64_t>(0x8000000000000000u);",
        "inline constexpr std::int32_t std = 0x00000001;", "inline constexpr std::int32_t high = static_cast<std::int32_t>(0x80000000u);" })
    {
        TEST_EXPECT(ctx, declarations.find(spelling) != std::string::npos);
    }
    TEST_EXPECT(ctx, declarations.find("namespace schema_fixture\n{\n\nusing b8 = std::int8_t;\n\nenum class Thresholds") != std::string::npos);
    TEST_EXPECT(ctx, declarations.find("namespace Unsigned8\n{\ninline constexpr std::uint8_t low = 0x01u;\ninline constexpr std::uint8_t high = 0x80u;\n}\n\nnamespace Signed8") != std::string::npos);
    TEST_EXPECT(ctx, declarations.find("};\n\nenum class UnsignedThresholds") != std::string::npos);
    TEST_EXPECT(ctx, declarations.find("};\n\nstruct Extents") != std::string::npos);
    TEST_EXPECT(ctx, (declarations.size() >= 4u) && (declarations.compare((declarations.size() - 4u), 4u, "\n\n}\n") == 0));
    TEST_EXPECT(ctx, declarations.find("\n\n\n") == std::string::npos);
    TEST_EXPECT(ctx, declarations.find(" \n") == std::string::npos);
    TEST_EXPECT(ctx, declarations.find("::std::") == std::string::npos);

    if (!resolve(ctx, R"({"types":{"structures":{"std":{"members":[{"value":{"type":"u8"}}]}}}})", block, schema))
    {
        return;
    }
    const std::string shadowed = write_validation(ctx, schema, "schema_std_shadow");
    TEST_EXPECT(ctx, shadowed.find("using b8 = ::std::int8_t;") != std::string::npos);
    TEST_EXPECT(ctx, shadowed.find("struct std\n{\n    ::std::uint8_t value;\n};") != std::string::npos);

    if (!resolve(ctx, R"({"types":{"bit_structures":{"std":{"storage":"i8","members":[{"high":{"type":"b8","mask":0x80}}]}}}})", block, schema))
    {
        return;
    }
    const std::string namespace_shadowed = write_validation(ctx, schema, "schema_std_namespace");
    TEST_EXPECT(ctx, namespace_shadowed.find("namespace std\n{\ninline constexpr ::std::int8_t high = static_cast<::std::int8_t>(0x80u);\n}") != std::string::npos);

    if (!resolve(ctx, R"({"types":{}})", block, schema))
    {
        return;
    }
    CByteBuffer output;
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, generate_cpp(schema, CStringView{ "compact" }, output, diagnostic));
    if (output.is_ready())
    {
        const std::string compact{ reinterpret_cast<const char*>(output.data()), (output.size() - 1u) };
        TEST_EXPECT(ctx, compact == "#pragma once\n#include <cstdint>\n#include \"types/fp16data_t.hpp\"\n\nnamespace compact\n{\nusing b8 = std::int8_t;\n}\n");
    }
}

static void test_local_type_references(TTestContext& ctx)
{
    const char* const source = R"({"types":{
 "enumerations":{"Mode":{"storage":"u8","values":{"value":1}}},
 "structures":{
  "Holder":{"members":[
   {"plain":{"type":"Record"}},{"records":{"type":{"element":"Record","count":2}}},
   {"plain_mode":{"type":"Mode"}},{"plain_boolean":{"type":"b8"}},
   {"Record":{"type":"Record"}},{"repeated_record":{"type":"Record"}},
   {"repeated_records":{"type":{"element":{"element":"Record","count":2},"count":3}}},
   {"Mode":{"type":"Mode"}},{"repeated_mode":{"type":"Mode"}},
   {"b8":{"type":"b8"}},{"repeated_boolean":{"type":"b8"}},
   {"booleans":{"type":{"element":"b8","count":2}}},
   {"schema_fixture":{"type":"u8"}},{"qualified_after_namespace_member":{"type":"Record"}}
  ]},
  "Record":{"members":[{"value":{"type":"u8"}}]},
  "Following":{"members":[{"plain":{"type":"Record"}},{"boolean":{"type":"b8"}}]},
  "OtherHider":{"members":[{"Record":{"type":"u8"}},{"record":{"type":"Record"}}]}
 }}})";
    CBakedDocumentBlock block;
    CResolvedSchema schema;
    if (!resolve(ctx, source, block, schema))
    {
        return;
    }
    const std::string declarations = write_validation(ctx, schema, "schema_local_references");
    const char* const holder =
        "struct Holder\n{\n"
        "    ::schema_fixture::Record plain;\n"
        "    ::schema_fixture::Record records[2];\n"
        "    ::schema_fixture::Mode plain_mode;\n"
        "    ::schema_fixture::b8 plain_boolean;\n"
        "    ::schema_fixture::Record Record;\n"
        "    ::schema_fixture::Record repeated_record;\n"
        "    ::schema_fixture::Record repeated_records[3][2];\n"
        "    ::schema_fixture::Mode Mode;\n"
        "    ::schema_fixture::Mode repeated_mode;\n"
        "    ::schema_fixture::b8 b8;\n"
        "    ::schema_fixture::b8 repeated_boolean;\n"
        "    ::schema_fixture::b8 booleans[2];\n"
        "    std::uint8_t schema_fixture;\n"
        "    ::schema_fixture::Record qualified_after_namespace_member;\n"
        "};\n";
    TEST_EXPECT(ctx, declarations.find(holder) != std::string::npos);
    TEST_EXPECT(ctx, declarations.find("struct Following\n{\n    Record plain;\n    b8 boolean;\n};") != std::string::npos);
    TEST_EXPECT(ctx, declarations.find("struct OtherHider\n{\n    std::uint8_t Record;\n    ::schema_fixture::Record record;\n};") != std::string::npos);
}

struct SFailingAllocator
{
    std::size_t calls{}, fail_on{};
};

static void* MV_STD_ABI_CALL allocate_with_failure(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    SFailingAllocator& fixture = *static_cast<SFailingAllocator*>(state);
    if (fixture.calls++ == fixture.fail_on)
    {
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

static void test_allocations(TTestContext& ctx)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(fixture, block));
    bool success = false;
    for (std::size_t fail_on = 0u; fail_on < 128u && !success; ++fail_on)
    {
        SFailingAllocator failing{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory_context{ allocator };
        {
            tests::TMemoryContextScope scope{ &memory_context };
            CResolvedSchema s;
            SDiagnostic error;
            success = s.resolve(block.document(), error);
            if (!success)
            {
                TEST_EXPECT(ctx, error.reason == EReason::allocation_failed && !s.is_ready() && !s.document());
            }
        }
        TEST_EXPECT(ctx, memory_context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, success);
    CResolvedSchema s;
    SDiagnostic error;
    TEST_EXPECT(ctx, s.resolve(block.document(), error));
    success = false;
    for (std::size_t fail_on = 0u; fail_on < 128u && !success; ++fail_on)
    {
        SFailingAllocator failing{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory_context{ allocator };
        {
            tests::TMemoryContextScope scope{ &memory_context };
            CByteBuffer output;
            success = generate_cpp(s, CStringView{ "sample" }, output, error);
            if (!success)
            {
                TEST_EXPECT(ctx, error.reason == EReason::allocation_failed && !output.is_ready());
                TEST_EXPECT(ctx, error.stage == EStage::generation);
                TEST_EXPECT(ctx, !error.occurrence || error.enclosing_type.is_valid());
                TEST_EXPECT(ctx, !error.enclosing_member || error.occurrence == error.enclosing_member);
            }
        }
        TEST_EXPECT(ctx, memory_context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, success);
}

static void test_document_read_boundary(TTestContext& ctx)
{
    static_assert(!std::is_same_v<CSchemaHandle, CInstanceHandle>);
    static_assert(!std::is_same_v<CSchemaHandle, CBulkHandle>);
    static_assert(!std::is_same_v<CInstanceHandle, CBulkHandle>);
    TEST_EXPECT(ctx, !CSchemaHandle{}.is_valid() && !CInstanceHandle{}.is_valid() && !CBulkHandle{}.is_valid());
    TEST_EXPECT(ctx, !detail::SOccurrence{}.is_valid());

    const std::string source = R"({"object":{"null":null,"boolean":true,"signed":-4,"unsigned":4294967295,"float":1.5,"string":"value","array":[3,false]}})";
    CLiveDocument live;
    const CDocumentReport report = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), 1u }, live);
    TEST_EXPECT(ctx, report.accepted());
    const CNodeKey named_object = live.object_child(live.root(), CStringView{ "object" });
    const CNodeKey named_string = live.object_child(named_object, CStringView{ "string" });
    TEST_EXPECT(ctx, live.set_newline_escaping_suppressed(named_string, true));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const detail::CDocumentRead live_query{ live };
    detail::CDocumentRead baked_query;
    {
        const CBakedDocument short_lived_view = block.document();
        baked_query = detail::CDocumentRead{ short_lived_view };
    }
    TEST_EXPECT(ctx, live_query.is_ready() && baked_query.is_ready());
    const detail::SOccurrence live_root = live_query.root();
    const detail::SOccurrence baked_root = baked_query.root();
    TEST_EXPECT(ctx, live_root.is_live() && baked_root.is_baked());
    detail::SOccurrence ambiguous = live_root;
    ambiguous.baked = baked_root.baked;
    TEST_EXPECT(ctx, !ambiguous.is_valid() && !live_query.contains(ambiguous) && !baked_query.contains(ambiguous));
    TEST_EXPECT(ctx, live_query.value_kind(live_root) == EDocumentValueKind::object);
    TEST_EXPECT(ctx, baked_query.value_kind(baked_root) == EDocumentValueKind::object);
    TEST_EXPECT(ctx, !live_query.contains(baked_root) && !baked_query.contains(live_root));
    TEST_EXPECT(ctx, live_query.value_kind(baked_root) == EDocumentValueKind::invalid);
    TEST_EXPECT(ctx, baked_query.value_kind(live_root) == EDocumentValueKind::invalid);

    const detail::SOccurrence live_object = live_query.object_child(live_root, CStringView{ "object" });
    const detail::SOccurrence baked_object = baked_query.object_child(baked_root, CStringView{ "object" });
    TEST_EXPECT(ctx, live_query.contains(live_object) && baked_query.contains(baked_object));
    TEST_EXPECT(ctx, live_query.name(live_object) == baked_query.name(baked_object));
    TEST_EXPECT(ctx, live_query.property_name(live_query.name_id(live_object)) ==
        baked_query.property_name(baked_query.name_id(baked_object)));
    TEST_EXPECT(ctx, live_query.is_object_entry(live_object) && baked_query.is_object_entry(baked_object));
    TEST_EXPECT(ctx, live_query.parent(live_object).live == live_root.live);
    TEST_EXPECT(ctx, baked_query.parent(baked_object).baked == baked_root.baked);
    TEST_EXPECT(ctx, live_query.child_count(live_object) == baked_query.child_count(baked_object));

    detail::SOccurrence live_child = live_query.first_child(live_object);
    detail::SOccurrence baked_child = baked_query.first_child(baked_object);
    for (std::uint32_t ordinal = 0u; ordinal < live_query.child_count(live_object); ++ordinal)
    {
        TEST_EXPECT(ctx, live_child.is_live() && baked_child.is_baked());
        TEST_EXPECT(ctx, live_query.name(live_child) == baked_query.name(baked_child));
        TEST_EXPECT(ctx, live_query.value_kind(live_child) == baked_query.value_kind(baked_child));
        const CStringView name = live_query.name(live_child);
        TEST_EXPECT(ctx, live_query.object_child(live_object, name).live == live_child.live);
        TEST_EXPECT(ctx, baked_query.object_child(baked_object, name).baked == baked_child.baked);
        live_child = live_query.next_sibling(live_child);
        baked_child = baked_query.next_sibling(baked_child);
    }
    TEST_EXPECT(ctx, !live_child.is_valid() && !baked_child.is_valid());

    const auto live_value = [&](const char* const name) { return live_query.object_child(live_object, CStringView{ name }); };
    const auto baked_value = [&](const char* const name) { return baked_query.object_child(baked_object, CStringView{ name }); };
    bool boolean = false;
    TEST_EXPECT(ctx, live_query.boolean_value(live_value("boolean"), boolean) && boolean);
    boolean = false;
    TEST_EXPECT(ctx, baked_query.boolean_value(baked_value("boolean"), boolean) && boolean);
    std::int64_t signed_value = 0;
    TEST_EXPECT(ctx, live_query.signed_integer_value(live_value("signed"), signed_value) && signed_value == -4);
    TEST_EXPECT(ctx, baked_query.signed_integer_value(baked_value("signed"), signed_value) && signed_value == -4);
    std::uint64_t unsigned_value = 0u;
    TEST_EXPECT(ctx, live_query.unsigned_integer_value(live_value("unsigned"), unsigned_value) && unsigned_value == 4294967295u);
    TEST_EXPECT(ctx, baked_query.unsigned_integer_value(baked_value("unsigned"), unsigned_value) && unsigned_value == 4294967295u);
    CIntegerMetadata live_metadata, baked_metadata;
    TEST_EXPECT(ctx, live_query.integer_metadata(live_value("unsigned"), live_metadata));
    TEST_EXPECT(ctx, baked_query.integer_metadata(baked_value("unsigned"), baked_metadata));
    TEST_EXPECT(ctx, live_metadata == baked_metadata);
    double floating = 0.0;
    TEST_EXPECT(ctx, live_query.floating_point_value(live_value("float"), floating) && floating == 1.5);
    TEST_EXPECT(ctx, baked_query.floating_point_value(baked_value("float"), floating) && floating == 1.5);
    TEST_EXPECT(ctx, live_query.string_value(live_value("string")) == baked_query.string_value(baked_value("string")));
    TEST_EXPECT(ctx, live_query.suppresses_newline_escaping(live_value("string")) &&
        baked_query.suppresses_newline_escaping(baked_value("string")));
    TEST_EXPECT(ctx, !live_query.suppresses_newline_escaping(live_value("signed")) &&
        !baked_query.suppresses_newline_escaping(baked_value("signed")));
    const detail::SOccurrence live_array = live_value("array");
    const detail::SOccurrence baked_array = baked_value("array");
    TEST_EXPECT(ctx, live_query.child_count(live_array) == 2u && baked_query.child_count(baked_array) == 2u);
    TEST_EXPECT(ctx, live_query.value_kind(live_query.array_at(live_array, 0u)) == EDocumentValueKind::integer);
    TEST_EXPECT(ctx, baked_query.value_kind(baked_query.array_at(baked_array, 1u)) == EDocumentValueKind::boolean);
    TEST_EXPECT(ctx, !live_query.array_at(live_array, 2u).is_valid() && !baked_query.array_at(baked_array, 2u).is_valid());

    boolean = true;
    TEST_EXPECT(ctx, !live_query.boolean_value(baked_value("boolean"), boolean) && boolean);
    TEST_EXPECT(ctx, !baked_query.boolean_value(live_value("boolean"), boolean) && boolean);
    TEST_EXPECT(ctx, !detail::CDocumentRead{}.root().is_valid());
    const CNodeKey empty = live.create_empty(CStringView{ "pending" });
    TEST_EXPECT(ctx, live.append_child(live_object.live, empty).succeeded());
    TEST_EXPECT(ctx, live_query.value_kind(detail::SOccurrence{ empty }) == EDocumentValueKind::empty);
    TEST_EXPECT(ctx, !baked_query.object_child(baked_object, CStringView{ "pending" }).is_valid());
}
}   // namespace schema_tests

int run_schema_tests()
{
    tests::TTestContext ctx;
    schema_tests::test_success(ctx);
    schema_tests::test_failures(ctx);
    schema_tests::test_scalars_and_limits(ctx);
    schema_tests::test_record_layout(ctx);
    schema_tests::test_review_regressions(ctx);
    schema_tests::test_representation(ctx);
    schema_tests::test_local_type_references(ctx);
    schema_tests::test_allocations(ctx);
    schema_tests::test_document_read_boundary(ctx);
    const schema::SRecordSizes sizes = schema::CResolvedSchema::record_sizes();
    std::cout << "Schema record bytes: type=" << sizes.type << " member=" << sizes.member << " label=" << sizes.label
              << " field=" << sizes.field << " default=" << sizes.default_value << " mapping=" << sizes.mapping << '\n';
    std::cout << "Schema: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed == 0 ? 0 : 1;
}
