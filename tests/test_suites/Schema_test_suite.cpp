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
#include "schema/schema_wrappers.hpp"
#include "schema/baked_bulk_data.hpp"
#include "schema/live_bulk_data.hpp"
#include "schema/baked_instances.hpp"
#include "schema/live_instances.hpp"
#include "schema/data_remap.hpp"
#include "schema/value_codec.hpp"
#include "memory/memory_policies.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/live_document.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/environment/test_paths.hpp"
#include "types/fp16data_t.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/path/native_path.hpp"
#include <cmath>
#include <cstring>
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
static_assert(!std::is_copy_constructible_v<CBakedSchema> && !std::is_move_constructible_v<CBakedSchema>);
static_assert(!std::is_copy_constructible_v<CLiveSchema> && !std::is_move_constructible_v<CLiveSchema>);
static_assert(!std::is_copy_constructible_v<CSchemaBinding> && std::is_nothrow_move_constructible_v<CSchemaBinding>);
static_assert(!std::is_copy_constructible_v<CBakedBulkData> && !std::is_move_constructible_v<CBakedBulkData>);
static_assert(!std::is_copy_constructible_v<CLiveBulkData> && !std::is_move_constructible_v<CLiveBulkData>);
static_assert(!std::is_copy_constructible_v<CDataRemapPlan> && std::is_nothrow_move_constructible_v<CDataRemapPlan>);

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
                  << static_cast<unsigned>(error.stage) << " occurrence valid " << error.occurrence.is_valid() << '\n';
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
    const bool accepted = s.resolve(block.document(), error);
    if (accepted)
    {
        std::cerr << "Schema unexpectedly accepted: " << text.substr(0u, 220u) << '\n';
    }
    TEST_EXPECT(ctx, !accepted);
    TEST_EXPECT(ctx, !s.is_ready() && s.definition_count() == 0u);
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
        const CStringView name = s.name(id);
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
        if (t.size == 0u)
        {
            continue;
        }
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
                if (mt.size == 0u)
                {
                    continue;
                }
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

static void test_empty_types(TTestContext& ctx)
{
    const std::string input = R"({"types":{"structures":{
        "Empty":{"detail":{"size":0,"alignment":1},"members":[]},
        "NestedEmpty":{"members":[{"part":{"type":"Empty"}},
            {"grid":{"type":{"element":{"element":"Empty","count":2},"count":3}}}]},
        "Mixed":{"members":[{"prefix":{"type":"u8"}},
            {"marker":{"type":"NestedEmpty"}},
            {"row":{"type":{"element":"Empty","count":4}}},
            {"tail":{"type":"u16"}}]}
    }}})";
    CBakedDocumentBlock block;
    CResolvedSchema resolved;
    if (!resolve(ctx, input, block, resolved))
    {
        return;
    }

    const CSchemaIndex empty = resolved.find_type(CStringView{ "Empty" });
    const CSchemaIndex nested = resolved.find_type(CStringView{ "NestedEmpty" });
    const CSchemaIndex mixed = resolved.find_type(CStringView{ "Mixed" });
    SType empty_type, nested_type, mixed_type;
    TEST_EXPECT(ctx, resolved.definition_count() == 3u && empty && nested && mixed &&
        resolved.definition_at(0u) == empty && resolved.definition_at(1u) == nested &&
        resolved.definition_at(2u) == mixed);
    TEST_EXPECT(ctx, resolved.type(empty, empty_type) && resolved.type(nested, nested_type) &&
        resolved.type(mixed, mixed_type));
    TEST_EXPECT(ctx, empty_type.size == 0u && empty_type.stride == 0u && empty_type.alignment == 1u &&
        empty_type.count == 0u && nested_type.size == 0u && nested_type.stride == 0u &&
        nested_type.alignment == 1u && nested_type.count == 2u);
    TEST_EXPECT(ctx, mixed_type.size == 4u && mixed_type.alignment == 2u && mixed_type.count == 4u && mixed_type.gaps);

    const CSchemaDocumentQuery query{ block.document() };
    const CSchemaHandle structures = query.object_child(query.object_child(query.root(), CStringView{ "types" }),
        CStringView{ "structures" });
    TEST_EXPECT(ctx, resolved.map_occurrence(query.object_child(structures, CStringView{ "Empty" })) == empty &&
        resolved.map_occurrence(query.object_child(structures, CStringView{ "NestedEmpty" })) == nested &&
        resolved.map_occurrence(query.object_child(structures, CStringView{ "Mixed" })) == mixed);

    const std::uint64_t mixed_offsets[] = { 0u, 1u, 1u, 2u };
    const std::uint64_t mixed_sizes[] = { 1u, 0u, 0u, 2u };
    for (std::uint32_t ordinal = 0u; ordinal < mixed_type.count; ++ordinal)
    {
        const CSchemaIndex member_index = resolved.member_at(mixed, ordinal);
        SMember member;
        TEST_EXPECT(ctx, resolved.member(member_index, member) && member.offset == mixed_offsets[ordinal] &&
            member.size == mixed_sizes[ordinal] && resolved.map_occurrence(member.source) == member_index);
        TEST_EXPECT(ctx, resolved.find_member(mixed, resolved.name(member.name)) == member_index);
    }
    for (std::uint32_t ordinal = 0u; ordinal < nested_type.count; ++ordinal)
    {
        const CSchemaIndex member_index = resolved.member_at(nested, ordinal);
        SMember member;
        TEST_EXPECT(ctx, resolved.member(member_index, member) && member.offset == 0u && member.size == 0u &&
            resolved.map_occurrence(member.source) == member_index);
    }
    SMember grid, row;
    SType grid_type, grid_element, row_type;
    TEST_EXPECT(ctx, resolved.member(resolved.find_member(nested, CStringView{ "grid" }), grid) &&
        resolved.type(grid.type, grid_type) && resolved.type(grid_type.element_or_storage, grid_element));
    TEST_EXPECT(ctx, grid_type.category == ECategory::array && grid_type.count == 3u && grid_type.size == 0u &&
        grid_type.stride == 0u && grid_element.count == 2u && grid_element.size == 0u &&
        grid_element.stride == 0u);
    TEST_EXPECT(ctx, resolved.member(resolved.find_member(mixed, CStringView{ "row" }), row) &&
        resolved.type(row.type, row_type) && row_type.count == 4u && row_type.size == 0u && row_type.stride == 0u);

    const std::string declarations = write_validation(ctx, resolved, "schema_empty");
    TEST_EXPECT(ctx, declarations.find("struct Mixed") != std::string::npos &&
        declarations.find("struct Empty") == std::string::npos &&
        declarations.find("struct NestedEmpty") == std::string::npos &&
        declarations.find(" marker;") == std::string::npos && declarations.find(" row[") == std::string::npos &&
        declarations.find(" prefix;") != std::string::npos && declarations.find(" tail;") != std::string::npos);

    CLiveDocument live_document;
    const CDocumentReport report = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), 1u }, live_document);
    TEST_EXPECT(ctx, report.accepted());
    CResolvedSchema live_resolved;
    SDiagnostic live_diagnostic;
    TEST_EXPECT(ctx, live_resolved.resolve(live_document, live_diagnostic));
    const CSchemaIndex live_mixed = live_resolved.find_type(CStringView{ "Mixed" });
    SType live_empty_type, live_mixed_type;
    TEST_EXPECT(ctx, live_resolved.type(live_resolved.find_type(CStringView{ "Empty" }), live_empty_type) &&
        live_resolved.type(live_mixed, live_mixed_type) && live_empty_type.size == 0u &&
        live_mixed_type.size == mixed_type.size && live_mixed_type.alignment == mixed_type.alignment);
    const CSchemaDocumentQuery live_query{ live_document };
    const CSchemaHandle live_structures = live_query.object_child(
        live_query.object_child(live_query.root(), CStringView{ "types" }), CStringView{ "structures" });
    TEST_EXPECT(ctx, live_resolved.map_occurrence(live_query.object_child(live_structures, CStringView{ "Empty" })) ==
        live_resolved.find_type(CStringView{ "Empty" }));
    for (std::uint32_t ordinal = 0u; ordinal < live_mixed_type.count; ++ordinal)
    {
        const CSchemaIndex index = live_resolved.member_at(live_mixed, ordinal);
        SMember member;
        TEST_EXPECT(ctx, live_resolved.member(index, member) && member.offset == mixed_offsets[ordinal] &&
            member.size == mixed_sizes[ordinal] && live_resolved.map_occurrence(member.source) == index &&
            live_resolved.find_member(live_mixed, live_resolved.name(member.name)) == index);
    }
    CByteBuffer live_output;
    TEST_EXPECT(ctx, generate_cpp(live_resolved, CStringView{ "schema_fixture" }, live_output, live_diagnostic));
    TEST_EXPECT(ctx, live_output.is_ready() && live_output.size() == declarations.size() + 1u &&
        std::memcmp(live_output.data(), declarations.data(), declarations.size()) == 0);

    CBakedDocumentBlock alias_block;
    CResolvedSchema alias_only;
    if (resolve(ctx, R"({"types":{"structures":{"std":{"members":[]}}}})", alias_block, alias_only))
    {
        CByteBuffer output;
        SDiagnostic diagnostic;
        TEST_EXPECT(ctx, generate_cpp(alias_only, CStringView{ "schema_fixture" }, output, diagnostic));
        const std::string text{ reinterpret_cast<const char*>(output.data()), output.size() - 1u };
        TEST_EXPECT(ctx, text.find("struct std") == std::string::npos &&
            text.find("using b8 = std::int8_t;\n}") != std::string::npos &&
            text.find("using b8 = ::std::int8_t") == std::string::npos);
    }

    expect_failure(ctx, R"({"types":{"structures":{"Bad":{"detail":{"size":1},"members":[]}}}})", EReason::invalid_layout);
    expect_failure(ctx, R"({"types":{"structures":{"Bad":{"detail":{"alignment":2},"members":[]}}}})", EReason::invalid_layout);
    expect_failure(ctx, R"({"types":{"structures":{"Empty":{"members":[]},"All":{"detail":{"size":1},"members":[{"part":{"type":"Empty"}}]}}}})",
        EReason::invalid_layout);
    expect_failure(ctx, R"({"types":{"structures":{"Empty":{"members":[]},"All":{"detail":{"alignment":2},"members":[{"part":{"type":"Empty"}}]}}}})",
        EReason::invalid_layout);
    expect_failure(ctx, R"({"types":{"structures":{"Bad":{"members":{}}}}})", EReason::invalid_input);
    expect_failure(ctx, R"({"types":{"structures":{"Bad":{}}}})", EReason::missing_property);
    expect_failure(ctx, R"({"types":{"structures":{"Empty":{"members":[]},"Outer":{"members":[{"value":{"type":"Empty","default":{}}}]}}}})",
        EReason::invalid_default);
    expect_failure(ctx, R"({"types":{"structures":{"Empty":{"members":[]},"Outer":{"members":[{"value":{"type":{"element":"Empty","count":2},"default":[]}}]}}}})",
        EReason::invalid_default);
    expect_failure(ctx, R"({"types":{"structures":{"Empty":{"members":[]},"Outer":{"members":[{"value":{"type":{"element":"Empty","count":0}}}]}}}})",
        EReason::invalid_range);
    expect_failure(ctx, R"({"types":{"enumerations":{"Bad":{"storage":"u8","values":{}}}}})", EReason::invalid_input);
    expect_failure(ctx, R"({"types":{"bit_structures":{"Bad":{"storage":"u8","members":[]}}}})", EReason::invalid_input);
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
    TEST_EXPECT(ctx, s.field(s.find_field(s.find_type(CStringView{ "Flags" }), CStringView{ "normal" }), field));
    SDefault normal_default;
    TEST_EXPECT(ctx, field.interpretation == EInterpretation::unorm &&
        s.default_value(field.type, field.default_description, normal_default) &&
        normal_default.scalar.kind == EScalar::unsigned_integer && normal_default.scalar.value.unsigned_value == 2u);
    TEST_EXPECT(ctx, s.type(s.find_type(CStringView{ "SignedMask" }), t) && !t.gaps);
    TEST_EXPECT(ctx, first_default(ctx, s, "Atoms", "g").scalar.value.signed_value == INT64_MIN);
    TEST_EXPECT(ctx, first_default(ctx, s, "Atoms", "h").scalar.value.unsigned_value == UINT64_MAX);
    TEST_EXPECT(ctx, first_default(ctx, s, "Atoms", "i").scalar.value.unsigned_value == fp16data_t{ 1e100 }.getBits());
    TEST_EXPECT(ctx, std::signbit(first_default(ctx, s, "Vector4", "y").scalar.value.floating_value));
    TEST_EXPECT(ctx, std::isnan(first_default(ctx, s, "Atoms", "k").scalar.value.floating_value));
    TEST_EXPECT(ctx, !s.map_occurrence(CSchemaDocumentQuery{ block.document() }.root()));
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
    TEST_EXPECT(ctx, !s.is_ready() && moved.type(vector, t));
    CBakedDocumentBlock replacement;
    TEST_EXPECT(ctx, bake(one("\"u8\"", "7"), replacement));
    SDiagnostic replacement_error;
    TEST_EXPECT(ctx, s.resolve(replacement.document(), replacement_error) && s.definition_count() == 1u);
    s = std::move(moved);
    TEST_EXPECT(ctx, !moved.is_ready() && s.type(vector, t) && s.definition_count() == 11u);
    SDiagnostic error;
    TEST_EXPECT(ctx, s.resolve(error));
    CBakedDocumentBlock invalid;
    TEST_EXPECT(ctx, bake(one("\"missing\"", ""), invalid));
    TEST_EXPECT(ctx, !s.resolve(invalid.document(), error));
    TEST_EXPECT(ctx, !s.is_ready() && !s.type(vector, t));
    TEST_EXPECT(ctx, error.occurrence && error.enclosing_type && error.enclosing_member);
    TEST_EXPECT(ctx, CSchemaDocumentQuery{ invalid.document() }.contains(error.occurrence));
    CByteBuffer output;
    TEST_EXPECT(ctx, !generate_cpp(s, CStringView{ "valid" }, output, error) && !output.is_ready());
}

static void test_explicit_layout(TTestContext& ctx)
{
    const std::string input = R"({"types":{"structures":{
        "Empty":{"members":[]},
        "morphic_padding_1":{"members":[{"value":{"type":"u8"}}]},
        "Inner":{"detail":{"alignment":16,"size":32},"members":[
            {"tag":{"type":"u32","offset":0}},
            {"payload":{"type":"u64","offset":16}}]},
        "Outer":{"detail":{"alignment":32,"size":128},"members":[
            {"last":{"type":"u32","offset":112,"default":7}},
            {"marker":{"type":"Empty","offset":128}},
            {"rows":{"type":{"element":"Inner","count":2},"offset":32}},
            {"first":{"type":"u8","offset":0}},
            {"morphic_padding_0":{"type":"u8","offset":1}}]},
        "Natural":{"detail":{"alignment":16,"size":16},"members":[
            {"value":{"type":"u32"}}]},
        "Aligned":{"detail":{"alignment":128,"size":128},"members":[
            {"value":{"type":"u8"}}]},
        "AllEmpty":{"detail":{"size":0},"members":[
            {"marker":{"type":"Empty","offset":0}}]},
        "Dense":{"detail":{"size":8},"members":[
            {"tail":{"type":"u32","offset":4}},
            {"head":{"type":"u32","offset":0}}]},
        "Leading":{"detail":{"size":16},"members":[
            {"value":{"type":"u32","offset":4}}]},
        "LowChild":{"detail":{"alignment":4,"size":4},"members":[
            {"value":{"type":"u8"}}]},
        "LowPrefix":{"members":[{"prefix":{"type":"u8"}},
            {"child":{"type":"LowChild"}}]},
        "LowTail":{"members":[{"child":{"type":"LowChild"}},
            {"suffix":{"type":"u8"}}]},
        "LowProp":{"members":[{"prefix":{"type":"u8"}},
            {"tail":{"type":"LowTail"}}]}
    }}})";
    CBakedDocumentBlock block;
    CResolvedSchema resolved;
    if (!resolve(ctx, input, block, resolved))
    {
        return;
    }
    const CSchemaIndex outer = resolved.find_type(CStringView{ "Outer" });
    const CSchemaIndex inner = resolved.find_type(CStringView{ "Inner" });
    SType outer_type, inner_type, natural_type, aligned_type, empty_type;
    TEST_EXPECT(ctx, resolved.type(outer, outer_type) && outer_type.size == 128u &&
        outer_type.stride == 128u && outer_type.alignment == 32u && outer_type.gaps);
    TEST_EXPECT(ctx, resolved.type(inner, inner_type) && inner_type.size == 32u &&
        inner_type.stride == 32u && inner_type.alignment == 16u && inner_type.gaps);
    TEST_EXPECT(ctx, resolved.type(resolved.find_type(CStringView{ "Natural" }), natural_type) &&
        natural_type.size == 16u && natural_type.alignment == 16u && natural_type.gaps);
    TEST_EXPECT(ctx, resolved.type(resolved.find_type(CStringView{ "Aligned" }), aligned_type) &&
        aligned_type.size == 128u && aligned_type.alignment == 128u && aligned_type.gaps);
    TEST_EXPECT(ctx, resolved.type(resolved.find_type(CStringView{ "AllEmpty" }), empty_type) &&
        empty_type.size == 0u && empty_type.stride == 0u && empty_type.alignment == 1u && !empty_type.gaps);
    SType dense_type;
    TEST_EXPECT(ctx, resolved.type(resolved.find_type(CStringView{ "Dense" }), dense_type) &&
        dense_type.size == 8u && dense_type.alignment == 4u && !dense_type.gaps);
    SType leading_type;
    TEST_EXPECT(ctx, resolved.type(resolved.find_type(CStringView{ "Leading" }), leading_type) &&
        leading_type.size == 16u && leading_type.alignment == 4u && leading_type.gaps);
    const char* const names[] = { "last", "marker", "rows", "first", "morphic_padding_0" };
    const std::uint64_t offsets[] = { 112u, 128u, 32u, 0u, 1u };
    for (std::uint32_t ordinal = 0u; ordinal < outer_type.count; ++ordinal)
    {
        const CSchemaIndex member_index = resolved.member_at(outer, ordinal);
        SMember member;
        TEST_EXPECT(ctx, resolved.member(member_index, member) &&
            resolved.name(member.name) == CStringView{ names[ordinal] } &&
            member.offset == offsets[ordinal] &&
            resolved.find_member(outer, CStringView{ names[ordinal] }) == member_index &&
            resolved.map_occurrence(member.source) == member_index);
    }
    SMember last, rows;
    SType array_type;
    TEST_EXPECT(ctx, resolved.member(resolved.find_member(outer, CStringView{ "last" }), last) &&
        resolved.member(resolved.find_member(outer, CStringView{ "rows" }), rows) &&
        resolved.type(rows.type, array_type) && array_type.size == 64u && array_type.stride == 32u);
    SDefault value;
    TEST_EXPECT(ctx, resolved.default_value(last.type, last.default_description, value) &&
        value.scalar.value.unsigned_value == 7u);

    const std::string declarations = write_validation(ctx, resolved, "schema_explicit");
    const std::size_t first = declarations.find(" first;");
    const std::size_t rows_pos = declarations.find(" rows[2];");
    const std::size_t last_pos = declarations.find(" last;");
    TEST_EXPECT(ctx, first != std::string::npos && rows_pos != std::string::npos &&
        last_pos != std::string::npos && first < rows_pos && rows_pos < last_pos);
    TEST_EXPECT(ctx, declarations.find("struct alignas(32) Outer") != std::string::npos &&
        declarations.find("struct alignas(16) Inner") != std::string::npos &&
        declarations.find("struct alignas(16) Natural") != std::string::npos &&
        declarations.find("struct alignas(128) Aligned") != std::string::npos &&
        declarations.find("morphic_padding_2[") != std::string::npos &&
        declarations.find(" morphic_padding_0;") != std::string::npos &&
        declarations.find("struct AllEmpty") == std::string::npos);
    const std::size_t dense = declarations.find("struct Dense\n");
    TEST_EXPECT(ctx, dense != std::string::npos && declarations.find(" head;", dense) <
        declarations.find(" tail;", dense));
    const std::size_t leading = declarations.find("struct Leading\n");
    TEST_EXPECT(ctx, leading != std::string::npos && declarations.find("morphic_padding_0[4]", leading) <
        declarations.find(" value;", leading));
    const std::size_t low_prefix = declarations.find("struct LowPrefix\n");
    const std::size_t low_tail = declarations.find("struct LowTail\n");
    const std::size_t low_prop = declarations.find("struct LowProp\n");
    TEST_EXPECT(ctx, low_prefix != std::string::npos &&
        declarations.find("morphic_padding_0[3]", low_prefix) < declarations.find(" child;", low_prefix));
    TEST_EXPECT(ctx, low_tail != std::string::npos &&
        declarations.find(" suffix;", low_tail) < declarations.find("morphic_padding_0[3]", low_tail));
    TEST_EXPECT(ctx, low_prop != std::string::npos &&
        declarations.find("morphic_padding_0[3]", low_prop) < declarations.find(" tail;", low_prop));

    CLiveDocument live_document;
    const CDocumentReport report = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), 1u }, live_document);
    TEST_EXPECT(ctx, report.accepted());
    CResolvedSchema live_resolved;
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, live_resolved.resolve(live_document, diagnostic));
    SType live_outer;
    const CSchemaIndex live_index = live_resolved.find_type(CStringView{ "Outer" });
    TEST_EXPECT(ctx, live_resolved.type(live_index, live_outer) && live_outer.size == outer_type.size &&
        live_outer.alignment == outer_type.alignment && live_outer.gaps == outer_type.gaps);
    for (std::uint32_t ordinal = 0u; ordinal < outer_type.count; ++ordinal)
    {
        SMember member;
        TEST_EXPECT(ctx, live_resolved.member(live_resolved.member_at(live_index, ordinal), member) &&
            live_resolved.name(member.name) == CStringView{ names[ordinal] } && member.offset == offsets[ordinal]);
    }
    CByteBuffer live_output;
    TEST_EXPECT(ctx, generate_cpp(live_resolved, CStringView{ "schema_fixture" }, live_output, diagnostic) &&
        live_output.is_ready() && live_output.size() == declarations.size() + 1u &&
        std::memcmp(live_output.data(), declarations.data(), declarations.size()) == 0);
}

static void test_failures(TTestContext& ctx)
{
    const auto layout = [](const std::string& detail, const std::string& members)
    {
        return "{\"types\":{\"structures\":{\"Test\":{\"detail\":{" + detail +
            "},\"members\":[" + members + "]}}}}";
    };
    const std::string first = R"({"first":{"type":"u32","offset":0}})";
    const std::string second = R"({"second":{"type":"u32","offset":4}})";
    expect_failure(ctx, layout("\"alignment\":4", first), EReason::missing_property);
    expect_failure(ctx, layout("\"size\":8", first + R"(,{"second":{"type":"u32"}})"), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8", R"({"first":{"type":"u32"}},)" + second), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8", R"({"first":{"type":"u32","offset":2}})"), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":6", first), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":0", first), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":4", first + "," + second), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8", first + R"(,{"second":{"type":"u32","offset":0}})"),
        EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8", R"({"first":{"type":"u64","offset":0}},)" + second),
        EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8", R"({"first":{"type":"u64","offset":2147483647}})"),
        EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8", R"({"first":{"type":"u8","offset":2147483648}})"),
        EReason::storage_limit);
    expect_failure(ctx, layout("\"size\":8,\"alignment\":3", first), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":8,\"alignment\":2", first), EReason::invalid_layout);
    expect_failure(ctx, layout("\"size\":256,\"alignment\":256", first), EReason::unsupported_feature);
    expect_failure(ctx, layout("\"size\":8", R"({"empty":{"type":{"element":"u8","count":0},"offset":8}})"),
        EReason::invalid_range);
    expect_failure(ctx, layout("\"size\":8", R"({"empty":{"type":"u8","offset":9}})"), EReason::invalid_layout);
    expect_failure(ctx, R"({"types":{"structures":{"Empty":{"members":[]},"Test":{"detail":{"size":0},
        "members":[{"marker":{"type":"Empty","offset":1}}]}}}})", EReason::invalid_layout);
    expect_failure(ctx, R"({"types":{"structures":{
        "Inner":{"detail":{"size":32},"members":[{"tag":{"type":"u32","offset":0}},
            {"payload":{"type":"u64","offset":16}}]},
        "Test":{"detail":{"size":40},"members":[{"inner":{"type":"Inner","offset":0}},
            {"second":{"type":"u8","offset":24}}]}
    }}})", EReason::invalid_layout);
    {
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake(layout("\"size\":8", first + R"(,{"second":{"type":"u16","offset":2}})"), block));
        CResolvedSchema schema;
        SDiagnostic error;
        TEST_EXPECT(ctx, !schema.resolve(block.document(), error) && error.reason == EReason::invalid_layout &&
            error.related && error.ranges_available && error.range_begin == 2u && error.range_end == 4u &&
            error.related_begin == 0u && error.related_end == 4u);
    }
    expect_failure(ctx, "{}", EReason::missing_property);
    expect_failure(ctx, "{\"types\":{},\"unknown\":{}}", EReason::unknown_property);
    expect_failure(ctx, "{\"types\":{},\"instances\":null}", EReason::invalid_input);
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
    expect_failure(ctx, "{\"types\":{\"structures\":{\"Test\":{\"members\":[{\"x\":{\"type\":\"u8\",\"strange\":0}}]}}}}",
        EReason::unknown_property);
    for (const char* const detail :
        { "\"alignment\":0", "\"alignment\":3", "\"size\":2", "\"internal\":1", "\"other\":0" })
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
    expect_failure(ctx, bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":3,\"interpretation\":\"snorm\"}}]}}}}",
        EReason::invalid_range);
    expect_failure(ctx, bit_prefix + "{\"x\":{\"type\":\"u8\",\"mask\":3,\"interpretation\":\"other\"}}]}}}}",
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
}

static void test_schema_sample(TTestContext& ctx)
{
    const std::string path = test_environment::repository_path("docs/schema/schema-example.json");
    std::FILE* const file = platform::filesystem::openFile(
        platform::path::makeNativePath(path.c_str()), platform::filesystem::EOpenMode::BinaryRead);
    TEST_EXPECT(ctx, file != nullptr);
    if (!file)
    {
        return;
    }
    std::string sample;
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1u, sizeof(buffer), file)) != 0u)
    {
        sample.append(buffer, count);
    }
    TEST_EXPECT(ctx, std::fclose(file) == 0);
    CBakedDocumentBlock block;
    CResolvedSchema resolved;
    if (!resolve(ctx, sample, block, resolved))
    {
        return;
    }
    const CSchemaIndex internal = resolved.find_type(CStringView{ "InternalRecord" });
    const CSchemaIndex arrays = resolved.find_type(CStringView{ "ArrayExamples" });
    SType type;
    SMember position, records;
    SType records_type;
    TEST_EXPECT(ctx, resolved.type(internal, type) && type.size == 32u && type.alignment == 16u &&
        type.internal && type.gaps);
    TEST_EXPECT(ctx, resolved.member(resolved.find_member(internal, CStringView{ "position" }), position) &&
        position.offset == 16u);
    TEST_EXPECT(ctx, resolved.type(arrays, type) && type.size == 96u && type.alignment == 16u &&
        resolved.member(resolved.find_member(arrays, CStringView{ "records" }), records) &&
        records.offset == 32u && resolved.type(records.type, records_type) && records_type.stride == 32u);
    write_validation(ctx, resolved, "schema_sample");
    CBakedSchema sample_schema;
    SDiagnostic schema_error;
    CMutableBakedDocument mutable_document{ block };
    CBakedBulkData bulk;
    CByteBuffer payload;
    SBulkDiagnostic bulk_error;
    TEST_EXPECT(ctx, sample_schema.set_document(block.document()) && sample_schema.resolve(schema_error));
    TEST_EXPECT(ctx, bulk.set_document(mutable_document) && bulk.bind_schema(sample_schema));
    TEST_EXPECT(ctx, bulk.materialise(payload, bulk_error));
    SBulkEntryView triangle;
    TEST_EXPECT(ctx, bulk.entry(bulk.find_entry(CStringView{ "Vertex" }, CStringView{ "triangle" }), triangle) &&
        triangle.count == 3u && triangle.stride == 24u && triangle.byte_count == 72u && triangle.bytes != nullptr);
    CLiveBulkData live_bulk;
    TEST_EXPECT(ctx, bulk.promote(live_bulk, sample_schema, bulk_error));
    SBulkEntryView live_triangle;
    TEST_EXPECT(ctx, live_bulk.entry(live_bulk.find_entry(CStringView{ "Vertex" }, CStringView{ "triangle" }),
        live_triangle) && live_triangle.count == triangle.count && live_triangle.byte_count == triangle.byte_count &&
        live_triangle.bytes != triangle.bytes &&
        std::memcmp(live_triangle.bytes, triangle.bytes, static_cast<std::size_t>(triangle.byte_count)) == 0);
    CBakedInstances instances;
    CByteBuffer instance_payload;
    SInstanceDiagnostic instance_error;
    TEST_EXPECT(ctx, instances.set_document(mutable_document) && instances.bind_schema(sample_schema));
    TEST_EXPECT(ctx, instances.materialise(instance_payload, instance_error));
    const CInstanceHandle vector = instances.find_base(CStringView{ "Vector4" }, CStringView{ "base" });
    const CInstanceHandle padded = instances.find_specialisation(vector, CStringView{ "padded" });
    const CInstanceHandle vertex = instances.find_base(CStringView{ "Vertex" }, CStringView{ "base" });
    const CInstanceHandle short_uv = instances.find_specialisation(vertex, CStringView{ "short_uv" });
    SInstanceEntryView vector_view, padded_view, vertex_view, short_view;
    TEST_EXPECT(ctx, instances.entry(vector, vector_view) && instances.entry(padded, padded_view) &&
        instances.entry(vertex, vertex_view) && instances.entry(short_uv, short_view));
    float vector_w{}, padded_w{}, base_uv_tail{}, short_uv_tail{};
    if (vector_view.bytes && padded_view.bytes && vertex_view.bytes && short_view.bytes)
    {
        std::memcpy(&vector_w, vector_view.bytes + 12u, sizeof(vector_w));
        std::memcpy(&padded_w, padded_view.bytes + 12u, sizeof(padded_w));
        std::memcpy(&base_uv_tail, vertex_view.bytes + 16u, sizeof(base_uv_tail));
        std::memcpy(&short_uv_tail, short_view.bytes + 16u, sizeof(short_uv_tail));
    }
    TEST_EXPECT(ctx, vector_w == 7.0f && padded_w == 7.0f &&
        base_uv_tail == 0.75f && short_uv_tail == 0.75f &&
        vector_view.bytes != padded_view.bytes && vertex_view.bytes != short_view.bytes);
    CLiveInstances live_instances;
    TEST_EXPECT(ctx, instances.promote(live_instances, sample_schema, instance_error));
    for (const EDataOutputForm form : { EDataOutputForm::embedded, EDataOutputForm::external })
    {
        CBakedDocumentBlock output_block;
        CByteBuffer output_payload;
        CBakedInstances output_role;
        TEST_EXPECT(ctx, live_instances.demote(output_block, output_payload, output_role,
            sample_schema, form, instance_error));
        SInstanceEntryView output_view;
        const CInstanceHandle output_vector = output_role.find_base(CStringView{ "Vector4" }, CStringView{ "base" });
        TEST_EXPECT(ctx, output_role.loaded_ready() && output_role.entry(output_vector, output_view) &&
            output_view.byte_count == vector_view.byte_count &&
            detail::compare_encoded(*sample_schema.resolved(), output_view.type,
                vector_view.bytes, static_cast<std::size_t>(vector_view.byte_count),
                output_view.bytes, static_cast<std::size_t>(output_view.byte_count)));
        CBakedInstances reloaded;
        TEST_EXPECT(ctx, reloaded.set_document(output_block.document()) && reloaded.bind_schema(sample_schema) &&
            reloaded.load_supplied(output_payload.const_view(), form == EDataOutputForm::embedded, instance_error));
    }
}

static void test_unorm_defaults(TTestContext& ctx)
{
    const auto input = [](const char* const storage, const char* const logical, const char* const mask,
        const char* const value)
    {
        return std::string("{\"types\":{\"bit_structures\":{\"Bits\":{\"storage\":\"") + storage +
            "\",\"members\":[{\"code\":{\"type\":\"" + logical + "\",\"mask\":" + mask +
            ",\"interpretation\":\"unorm\"" + (value ? std::string(",\"default\":") + value : "") +
            "}}]}}}}";
    };
    struct SCase
    {
        const char* storage;
        const char* logical;
        const char* mask;
        const char* value;
        std::uint64_t code;
    };
    const SCase accepted[] = {
        { "u8", "u8", "3", nullptr, 0u },
        { "u8", "u8", "3", "0", 0u },
        { "u8", "u8", "3", "1", 1u },
        { "u8", "u8", "3", "3", 3u },
        { "u8", "u8", "3", "0.0", 0u },
        { "u8", "u8", "3", "0.24999999999999997", 1u },
        { "u8", "u8", "3", "0.25", 1u },
        { "u8", "u8", "3", "0.5", 2u },
        { "u8", "u8", "3", "0.75", 2u },
        { "u8", "u8", "3", "1.0", 3u },
        { "u8", "u8", "3", "1.01", 3u },
        { "u8", "u8", "3", "-0.1", 0u },
        { "u8", "u8", "3", "-0.0", 0u },
        { "u8", "u8", "3", "\"NaN\"", 0u },
        { "u8", "u8", "3", "\"+INF\"", 3u },
        { "u8", "u8", "3", "\"-INFINITY\"", 0u },
        { "u8", "u8", "1", "0.49999999999999994", 0u },
        { "u8", "u8", "1", "0.5", 1u },
        { "u8", "u8", "0xFF", "0.5", 128u },
        { "u8", "u8", "0xFF", "0.75", 191u },
        { "u8", "u8", "0xFF", "1.0", 255u },
        { "u8", "u8", "0xF0", "0.5", 8u },
        { "u16", "u8", "0x3ff", "0.1", 102u },
        { "u64", "u64", "18446744073709551615", "18446744073709551615", UINT64_MAX },
        { "u64", "u64", "18446744073709551615", "0.5", UINT64_C(0x8000000000000000) },
        { "u64", "u64", "18446744073709551615", "0.75", UINT64_C(0xbfffffffffffffff) },
        { "u64", "u64", "18446744073709551615", "0.9999999999999999", UINT64_MAX - 2048u },
        { "u64", "u64", "18446744073709551615", "1.0", UINT64_MAX },
        { "u64", "u64", "18446744073709551615", "18446744073709551616.0", UINT64_MAX },
        { "u64", "u64", "18446744073709551615", "2.710505431213761e-20", 0u },
        { "u64", "u64", "18446744073709551615", "2.7105054312137617e-20", 1u },
        { "u64", "u64", "18446744073709551615", "1e-20", 0u },
        { "u64", "u64", "18446744073709551615", "1e-19", 2u }
    };
    for (const SCase& test : accepted)
    {
        const std::string source = input(test.storage, test.logical, test.mask, test.value);
        CBakedDocumentBlock block;
        CResolvedSchema baked;
        if (!resolve(ctx, source, block, baked))
        {
            continue;
        }
        const CSchemaIndex type = baked.find_type(CStringView{ "Bits" });
        const CSchemaIndex index = baked.find_field(type, CStringView{ "code" });
        SField field;
        SDefault value;
        TEST_EXPECT(ctx, baked.field(index, field) && field.interpretation == EInterpretation::unorm &&
            baked.map_occurrence(field.source) == index &&
            baked.default_value(field.type, field.default_description, value) &&
            value.scalar.kind == EScalar::unsigned_integer && value.scalar.value.unsigned_value == test.code);
        const CSchemaDocumentQuery query{ block.document() };
        const CSchemaHandle default_source = query.object_child(field.source, CStringView{ "default" });
        TEST_EXPECT(ctx, test.value ? value.source == default_source : !value.source);

        CLiveDocument live_document;
        const CDocumentReport report = document_parser::parse(
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), 1u }, live_document);
        TEST_EXPECT(ctx, report.accepted());
        CResolvedSchema live;
        SDiagnostic diagnostic;
        TEST_EXPECT(ctx, live.resolve(live_document, diagnostic));
        SField live_field;
        SDefault live_value;
        const CSchemaIndex live_type = live.find_type(CStringView{ "Bits" });
        const CSchemaIndex live_index = live.find_field(live_type, CStringView{ "code" });
        TEST_EXPECT(ctx, live.field(live_index, live_field) &&
            live.map_occurrence(live_field.source) == live_index &&
            live_field.width == field.width && live_field.shift == field.shift &&
            live.default_value(live_field.type, live_field.default_description, live_value) &&
            live_value.scalar.kind == EScalar::unsigned_integer &&
            live_value.scalar.value.unsigned_value == value.scalar.value.unsigned_value);
    }

    struct SInvalid
    {
        const char* storage;
        const char* logical;
        const char* mask;
        const char* value;
    };
    const SInvalid rejected[] = {
        { "u8", "u8", "3", "-1" },
        { "u8", "u8", "3", "4" },
        { "u8", "u8", "3", "256" },
        { "u16", "u8", "0x3ff", "256" },
        { "u16", "u8", "0x3ff", "0.5" },
        { "u8", "u8", "3", "\"0.5\"" },
        { "u8", "u8", "3", "\"nan(1)\"" },
        { "u8", "u8", "3", "true" },
        { "u8", "u8", "3", "null" }
    };
    for (const SInvalid& test : rejected)
    {
        const std::string source = input(test.storage, test.logical, test.mask, test.value);
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake(source, block));
        const CSchemaDocumentQuery query{ block.document() };
        const CSchemaHandle types = query.object_child(query.root(), CStringView{ "types" });
        const CSchemaHandle bits = query.object_child(query.object_child(types, CStringView{ "bit_structures" }),
            CStringView{ "Bits" });
        const CSchemaHandle member = query.array_at(query.object_child(bits, CStringView{ "members" }), 0u);
        const CSchemaHandle def = query.object_child(member, CStringView{ "default" });
        CResolvedSchema schema;
        SDiagnostic diagnostic;
        TEST_EXPECT(ctx, !schema.resolve(block.document(), diagnostic) &&
            diagnostic.stage == EStage::defaults && diagnostic.occurrence == def &&
            (diagnostic.reason == EReason::invalid_range || diagnostic.reason == EReason::invalid_default));

        CLiveDocument live_document;
        const CDocumentReport report = document_parser::parse(
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), 1u }, live_document);
        TEST_EXPECT(ctx, report.accepted());
        CResolvedSchema live;
        SDiagnostic live_diagnostic;
        const CSchemaDocumentQuery live_query{ live_document };
        const CSchemaHandle live_types = live_query.object_child(live_query.root(), CStringView{ "types" });
        const CSchemaHandle live_bits = live_query.object_child(
            live_query.object_child(live_types, CStringView{ "bit_structures" }), CStringView{ "Bits" });
        const CSchemaHandle live_member = live_query.array_at(
            live_query.object_child(live_bits, CStringView{ "members" }), 0u);
        const CSchemaHandle live_def = live_query.object_child(live_member, CStringView{ "default" });
        TEST_EXPECT(ctx, !live.resolve(live_document, live_diagnostic) &&
            live_diagnostic.reason == diagnostic.reason && live_diagnostic.stage == diagnostic.stage &&
            live_diagnostic.occurrence == live_def);
    }
    expect_failure(ctx, input("u8", "i8", "3", "0.5"), EReason::invalid_range);
    expect_failure(ctx, input("u8", "b8", "1", "0.5"), EReason::invalid_range);
    expect_failure(ctx, R"({"types":{"enumerations":{"Mode":{"storage":"u8","values":{"one":1}}},
        "bit_structures":{"Bits":{"storage":"u8","members":[{"code":{"type":"Mode",
        "mask":3,"interpretation":"unorm","default":0.5}}]}}}})", EReason::invalid_range);
}

static void test_snorm_defaults(TTestContext& ctx)
{
    const auto input = [](const char* const storage, const char* const logical, const char* const mask,
        const char* const value)
    {
        return std::string("{\"types\":{\"bit_structures\":{\"Bits\":{\"storage\":\"") + storage +
            "\",\"members\":[{\"code\":{\"type\":\"" + logical + "\",\"mask\":" + mask +
            ",\"interpretation\":\"snorm\"" + (value ? std::string(",\"default\":") + value : "") +
            "}}]}}}}";
    };
    struct SCase
    {
        const char* storage;
        const char* logical;
        const char* mask;
        const char* value;
        std::int64_t code;
    };
    const SCase accepted[] = {
        { "i8", "i8", "3", nullptr, 0 },
        { "i8", "i8", "3", "-2", -2 },
        { "i8", "i8", "3", "-1", -1 },
        { "i8", "i8", "3", "1", 1 },
        { "i8", "i8", "3", "-1.0", -1 },
        { "i8", "i8", "3", "-0.5", -1 },
        { "i8", "i8", "3", "-0.49999999999999994", 0 },
        { "i8", "i8", "3", "0.5", 1 },
        { "i8", "i8", "3", "1.0", 1 },
        { "i8", "i8", "3", "\"NaN\"", 0 },
        { "i8", "i8", "3", "\"INF\"", 1 },
        { "i8", "i8", "3", "\"-INF\"", -1 },
        { "i8", "i8", "0xF0", "0.5", 4 },
        { "i8", "i8", "0xF0", "-0.5", -4 },
        { "i8", "i8", "0xFF", "-128", -128 },
        { "i8", "i8", "0xFF", "127", 127 },
        { "i8", "i8", "0xFF", "-1.0", -127 },
        { "u8", "i8", "0xFF", "-1.0", -127 },
        { "i8", "i8", "0xFF", "-0.0", 0 },
        { "i8", "i8", "0xFF", "0.5", 64 },
        { "i8", "i8", "0xFF", "-0.5", -64 },
        { "i8", "i8", "0xFF", "0.75", 95 },
        { "i8", "i8", "0xFF", "-0.75", -95 },
        { "i8", "i8", "0xFF", "1.01", 127 },
        { "i8", "i8", "0xFF", "-1.01", -127 },
        { "i64", "i64", "18446744073709551615", "-9223372036854775808", INT64_MIN },
        { "i64", "i64", "18446744073709551615", "9223372036854775807", INT64_MAX },
        { "i64", "i64", "18446744073709551615", "0.5", INT64_C(0x4000000000000000) },
        { "i64", "i64", "18446744073709551615", "-0.5", -INT64_C(0x4000000000000000) },
        { "i64", "i64", "18446744073709551615", "0.75", INT64_C(0x5fffffffffffffff) },
        { "i64", "i64", "18446744073709551615", "-0.75", -INT64_C(0x5fffffffffffffff) },
        { "i64", "i64", "18446744073709551615", "0.9999999999999999", INT64_MAX - 1024 },
        { "i64", "i64", "18446744073709551615", "-0.9999999999999999", -INT64_MAX + 1024 },
        { "i64", "i64", "18446744073709551615", "-1.0", -INT64_MAX },
        { "i64", "i64", "18446744073709551615", "5.421010862427522e-20", 0 },
        { "i64", "i64", "18446744073709551615", "5.421010862427523e-20", 1 },
        { "i64", "i64", "18446744073709551615", "-5.421010862427523e-20", -1 }
    };
    for (const SCase& test : accepted)
    {
        const std::string source = input(test.storage, test.logical, test.mask, test.value);
        CBakedDocumentBlock block;
        CResolvedSchema baked;
        if (!resolve(ctx, source, block, baked))
        {
            continue;
        }
        const CSchemaIndex type = baked.find_type(CStringView{ "Bits" });
        const CSchemaIndex index = baked.find_field(type, CStringView{ "code" });
        SField field;
        SDefault value;
        TEST_EXPECT(ctx, baked.field(index, field) && field.interpretation == EInterpretation::snorm &&
            baked.map_occurrence(field.source) == index &&
            baked.default_value(field.type, field.default_description, value) &&
            value.scalar.kind == EScalar::signed_integer && value.scalar.value.signed_value == test.code);
        const CSchemaDocumentQuery query{ block.document() };
        const CSchemaHandle default_source = query.object_child(field.source, CStringView{ "default" });
        TEST_EXPECT(ctx, test.value ? value.source == default_source : !value.source);

        CLiveDocument live_document;
        const CDocumentReport report = document_parser::parse(
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), 1u }, live_document);
        TEST_EXPECT(ctx, report.accepted());
        CResolvedSchema live;
        SDiagnostic diagnostic;
        TEST_EXPECT(ctx, live.resolve(live_document, diagnostic));
        SField live_field;
        SDefault live_value;
        const CSchemaIndex live_type = live.find_type(CStringView{ "Bits" });
        const CSchemaIndex live_index = live.find_field(live_type, CStringView{ "code" });
        TEST_EXPECT(ctx, live.field(live_index, live_field) &&
            live.map_occurrence(live_field.source) == live_index &&
            live_field.width == field.width && live_field.shift == field.shift &&
            live.default_value(live_field.type, live_field.default_description, live_value) &&
            live_value.scalar.kind == EScalar::signed_integer &&
            live_value.scalar.value.signed_value == value.scalar.value.signed_value);
    }

    struct SInvalid
    {
        const char* storage;
        const char* logical;
        const char* mask;
        const char* value;
    };
    const SInvalid rejected[] = {
        { "i8", "i8", "3", "-3" },
        { "i8", "i8", "3", "2" },
        { "i8", "i8", "0xFF", "-129" },
        { "i8", "i8", "0xFF", "128" },
        { "i16", "i8", "0xFFFF", "1.0" },
        { "i8", "i8", "3", "\"0.5\"" },
        { "i8", "i8", "3", "\"nan(1)\"" },
        { "i8", "i8", "3", "true" },
        { "i8", "i8", "3", "null" }
    };
    for (const SInvalid& test : rejected)
    {
        const std::string source = input(test.storage, test.logical, test.mask, test.value);
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake(source, block));
        const CSchemaDocumentQuery query{ block.document() };
        const CSchemaHandle types = query.object_child(query.root(), CStringView{ "types" });
        const CSchemaHandle bits = query.object_child(query.object_child(types, CStringView{ "bit_structures" }),
            CStringView{ "Bits" });
        const CSchemaHandle member = query.array_at(query.object_child(bits, CStringView{ "members" }), 0u);
        const CSchemaHandle def = query.object_child(member, CStringView{ "default" });
        CResolvedSchema schema;
        SDiagnostic diagnostic;
        TEST_EXPECT(ctx, !schema.resolve(block.document(), diagnostic) &&
            diagnostic.stage == EStage::defaults && diagnostic.occurrence == def &&
            (diagnostic.reason == EReason::invalid_range || diagnostic.reason == EReason::invalid_default));

        CLiveDocument live_document;
        const CDocumentReport report = document_parser::parse(
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), 1u }, live_document);
        TEST_EXPECT(ctx, report.accepted());
        CResolvedSchema live;
        SDiagnostic live_diagnostic;
        const CSchemaDocumentQuery live_query{ live_document };
        const CSchemaHandle live_types = live_query.object_child(live_query.root(), CStringView{ "types" });
        const CSchemaHandle live_bits = live_query.object_child(
            live_query.object_child(live_types, CStringView{ "bit_structures" }), CStringView{ "Bits" });
        const CSchemaHandle live_member = live_query.array_at(
            live_query.object_child(live_bits, CStringView{ "members" }), 0u);
        const CSchemaHandle live_def = live_query.object_child(live_member, CStringView{ "default" });
        TEST_EXPECT(ctx, !live.resolve(live_document, live_diagnostic) &&
            live_diagnostic.reason == diagnostic.reason && live_diagnostic.stage == diagnostic.stage &&
            live_diagnostic.occurrence == live_def);
    }
    expect_failure(ctx, input("i8", "i8", "1", "0.5"), EReason::invalid_range);
    expect_failure(ctx, input("u8", "u8", "3", "0.5"), EReason::invalid_range);
    expect_failure(ctx, input("i8", "b8", "3", "0.5"), EReason::invalid_range);
    expect_failure(ctx, R"({"types":{"enumerations":{"Mode":{"storage":"i8","values":{"one":1}}},
        "bit_structures":{"Bits":{"storage":"i8","members":[{"code":{"type":"Mode",
        "mask":3,"interpretation":"snorm","default":0.5}}]}}}})", EReason::invalid_range);
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
    TEST_EXPECT(ctx, (sizes.type == 48u) && (sizes.member == 40u) && (sizes.label == 40u) &&
        (sizes.field == 48u) && (sizes.default_value == 48u) && (sizes.mapping == 24u));

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
                TEST_EXPECT(ctx, schema.find_member(type_index, schema.name(member.name)) == member_index);
                TEST_EXPECT(ctx, !schema.find_member(member_index, CStringView{ "x" }));
            }
            else if (type.category == ECategory::enumeration)
            {
                const CSchemaIndex label_index = schema.label_at(type_index, child_ordinal);
                SLabel label;
                TEST_EXPECT(ctx, schema.label(label_index, label));
                TEST_EXPECT(ctx, schema.find_label(type_index, schema.name(label.name)) == label_index);
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
                TEST_EXPECT(ctx, schema.find_field(type_index, schema.name(field.name)) == field_index);
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
    TEST_EXPECT(ctx, diagnostic.occurrence && CSchemaDocumentQuery{ invalid.document() }.contains(diagnostic.occurrence));
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
        const CSchemaDocumentQuery query{ doc };
        const CSchemaHandle role_types = query.object_child(query.root(), CStringView{ "types" });
        const CSchemaHandle role_structures = query.object_child(role_types, CStringView{ "structures" });
        const CSchemaHandle role_definition = query.object_child(role_structures, CStringView{ "Test" });
        const CSchemaHandle role_members = query.object_child(role_definition, CStringView{ "members" });
        const CSchemaHandle role_member = query.array_at(role_members, 0u);
        const CSchemaHandle role_value = query.object_child(role_member, CStringView{ "default" });
        CSchemaHandle role_named = query.array_at(role_value, 0u);
        if (test.descend)
        {
            role_named = query.array_at(role_named, 0u);
        }
        CResolvedSchema s;
        SDiagnostic error;
        TEST_EXPECT(ctx, !s.resolve(doc, error));
        TEST_EXPECT(ctx, error.reason == EReason::invalid_default && error.occurrence == role_named);
        TEST_EXPECT(ctx, !s.is_ready());
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
                TEST_EXPECT(ctx, error.reason == EReason::allocation_failed && !s.is_ready());
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

static void test_instance_document_query(TTestContext& ctx)
{
    static_assert(!std::is_convertible_v<CInstanceHandle, CSchemaHandle>);
    static_assert(!std::is_convertible_v<CInstanceHandle, CBulkHandle>);
    const std::string source = R"({"types":{},"instances":{"Position":{"origin":{
        "declaration":[1.0,2.0],"specialisation":{"raised":{"declaration":{"y":3.0}}}}}},
        "data":{"boolean":true,"signed":-2,"unsigned":4294967295,
            "float":1.5,"string":"value"}})";
    CLiveDocument live;
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{
        reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), 1u }, live).accepted());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const CInstanceDocumentQuery live_query{ live };
    CInstanceDocumentQuery baked_query;
    {
        const CBakedDocument temporary_view = block.document();
        baked_query = CInstanceDocumentQuery{ temporary_view };
    }
    TEST_EXPECT(ctx, !CInstanceDocumentQuery{}.is_ready() && !CInstanceDocumentQuery{}.root());
    TEST_EXPECT(ctx, CInstanceHandle{} == CInstanceHandle{});
    TEST_EXPECT(ctx, live_query.is_ready() && baked_query.is_ready());
    const CInstanceHandle live_root = live_query.root();
    const CInstanceHandle baked_root = baked_query.root();
    TEST_EXPECT(ctx, live_query.contains(live_root) && baked_query.contains(baked_root));
    TEST_EXPECT(ctx, !live_query.contains(baked_root) && !baked_query.contains(live_root));
    TEST_EXPECT(ctx, live_query.value_kind(live_root) == EDocumentValueKind::object &&
        baked_query.value_kind(baked_root) == EDocumentValueKind::object);
    const CInstanceHandle live_instances = live_query.object_child(live_root, CStringView{ "instances" });
    const CInstanceHandle baked_instances = baked_query.object_child(baked_root, CStringView{ "instances" });
    TEST_EXPECT(ctx, live_instances && baked_instances && live_instances != baked_instances);
    TEST_EXPECT(ctx, live_query.name(live_instances) == baked_query.name(baked_instances) &&
        live_query.property_name(live_query.name_id(live_instances)) == CStringView{ "instances" } &&
        baked_query.property_name(baked_query.name_id(baked_instances)) == CStringView{ "instances" });
    TEST_EXPECT(ctx, live_query.parent(live_instances) == live_root && baked_query.parent(baked_instances) == baked_root);
    TEST_EXPECT(ctx, live_query.is_object_entry(live_instances) && baked_query.is_object_entry(baked_instances));
    TEST_EXPECT(ctx, live_query.first_child(live_root) == live_query.object_child(live_root, CStringView{ "types" }));
    TEST_EXPECT(ctx, live_query.next_sibling(live_instances) == live_query.object_child(live_root, CStringView{ "data" }));
    TEST_EXPECT(ctx, baked_query.next_sibling(baked_instances) == baked_query.object_child(baked_root, CStringView{ "data" }));
    const CInstanceHandle live_group = live_query.first_child(live_instances);
    const CInstanceHandle baked_group = baked_query.first_child(baked_instances);
    TEST_EXPECT(ctx, live_query.name(live_group) == CStringView{ "Position" } &&
        baked_query.name(baked_group) == CStringView{ "Position" } &&
        live_query.child_count(live_group) == 1u && baked_query.child_count(baked_group) == 1u);
    const CInstanceHandle live_origin = live_query.first_child(live_group);
    const CInstanceHandle baked_origin = baked_query.first_child(baked_group);
    const CInstanceHandle live_declaration = live_query.object_child(live_origin, CStringView{ "declaration" });
    const CInstanceHandle baked_declaration = baked_query.object_child(baked_origin, CStringView{ "declaration" });
    double floating{};
    TEST_EXPECT(ctx, live_query.value_kind(live_declaration) == EDocumentValueKind::array &&
        baked_query.value_kind(baked_declaration) == EDocumentValueKind::array &&
        live_query.child_count(live_declaration) == 2u && baked_query.child_count(baked_declaration) == 2u);
    TEST_EXPECT(ctx, live_query.floating_point_value(live_query.array_at(live_declaration, 1u), floating) && floating == 2.0);
    TEST_EXPECT(ctx, baked_query.floating_point_value(baked_query.array_at(baked_declaration, 1u), floating) && floating == 2.0);
    TEST_EXPECT(ctx, !live_query.array_at(live_declaration, 2u) && !baked_query.array_at(baked_declaration, 2u));
    const CInstanceHandle live_specialisations = live_query.object_child(live_origin, CStringView{ "specialisation" });
    const CInstanceHandle baked_specialisations = baked_query.object_child(baked_origin, CStringView{ "specialisation" });
    TEST_EXPECT(ctx, live_query.name(live_query.first_child(live_specialisations)) == CStringView{ "raised" } &&
        baked_query.name(baked_query.first_child(baked_specialisations)) == CStringView{ "raised" });
    const CInstanceHandle live_data = live_query.object_child(live_root, CStringView{ "data" });
    const CInstanceHandle baked_data = baked_query.object_child(baked_root, CStringView{ "data" });
    const auto live_value = [&](const char* const name) { return live_query.object_child(live_data, CStringView{ name }); };
    const auto baked_value = [&](const char* const name) { return baked_query.object_child(baked_data, CStringView{ name }); };
    bool boolean{};
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    TEST_EXPECT(ctx, live_query.boolean_value(live_value("boolean"), boolean) && boolean &&
        baked_query.boolean_value(baked_value("boolean"), boolean) && boolean);
    TEST_EXPECT(ctx, live_query.signed_integer_value(live_value("signed"), signed_value) && signed_value == -2 &&
        baked_query.signed_integer_value(baked_value("signed"), signed_value) && signed_value == -2);
    TEST_EXPECT(ctx, live_query.unsigned_integer_value(live_value("unsigned"), unsigned_value) && unsigned_value == 4294967295u &&
        baked_query.unsigned_integer_value(baked_value("unsigned"), unsigned_value) && unsigned_value == 4294967295u);
    TEST_EXPECT(ctx, live_query.floating_point_value(live_value("float"), floating) && floating == 1.5 &&
        baked_query.floating_point_value(baked_value("float"), floating) && floating == 1.5);
    TEST_EXPECT(ctx, live_query.string_value(live_value("string")) == CStringView{ "value" } &&
        baked_query.string_value(baked_value("string")) == CStringView{ "value" });
}

static void test_live_baked_resolution_parity(TTestContext& ctx)
{
    CLiveDocument live_document;
    const std::size_t length = std::strlen(fixture);
    const CDocumentReport parsed = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(fixture), length, 1u }, live_document);
    TEST_EXPECT(ctx, parsed.accepted());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live_document, block));
    CResolvedSchema live_schema, baked_schema;
    SDiagnostic live_error, baked_error;
    TEST_EXPECT(ctx, live_schema.resolve(live_document, live_error));
    TEST_EXPECT(ctx, baked_schema.resolve(block.document(), baked_error));
    TEST_EXPECT(ctx, live_schema.definition_count() == baked_schema.definition_count());
    const CSchemaDocumentQuery live_query{ live_document };
    const CSchemaDocumentQuery baked_query{ block.document() };
    for (std::uint32_t ordinal = 0u; ordinal < live_schema.definition_count(); ++ordinal)
    {
        const CSchemaIndex live_index = live_schema.definition_at(ordinal);
        const CSchemaIndex baked_index = baked_schema.definition_at(ordinal);
        SType live_type, baked_type;
        TEST_EXPECT(ctx, live_index == baked_index && live_schema.type(live_index, live_type) &&
            baked_schema.type(baked_index, baked_type));
        TEST_EXPECT(ctx, live_type.category == baked_type.category && live_type.primitive == baked_type.primitive &&
            live_type.size == baked_type.size && live_type.alignment == baked_type.alignment &&
            live_type.stride == baked_type.stride && live_type.count == baked_type.count);
        TEST_EXPECT(ctx, live_schema.name(live_type.name) == baked_schema.name(baked_type.name));
        TEST_EXPECT(ctx, live_query.contains(live_type.source) && baked_query.contains(baked_type.source));
        TEST_EXPECT(ctx, live_schema.map_occurrence(live_type.source) == live_index &&
            baked_schema.map_occurrence(baked_type.source) == baked_index);
    }
    const CSchemaIndex vector = live_schema.find_type(CStringView{ "Vector4" });
    const CSchemaIndex member = live_schema.find_member(vector, CStringView{ "y" });
    SMember live_member, baked_member;
    TEST_EXPECT(ctx, live_schema.member(member, live_member) && baked_schema.member(member, baked_member));
    TEST_EXPECT(ctx, live_member.offset == baked_member.offset && live_member.size == baked_member.size &&
        live_schema.map_occurrence(live_member.source) == member && baked_schema.map_occurrence(baked_member.source) == member);
    CByteBuffer live_cpp, baked_cpp;
    TEST_EXPECT(ctx, generate_cpp(live_schema, CStringView{ "schema_fixture" }, live_cpp, live_error));
    TEST_EXPECT(ctx, generate_cpp(baked_schema, CStringView{ "schema_fixture" }, baked_cpp, baked_error));
    TEST_EXPECT(ctx, live_cpp.size() == baked_cpp.size() &&
        std::memcmp(live_cpp.data(), baked_cpp.data(), live_cpp.size()) == 0);

    const CSchemaHandle source = live_member.source;
    const std::uint64_t old_size = live_member.size;
    TEST_EXPECT(ctx, live_schema.resolve(live_error));
    TEST_EXPECT(ctx, live_schema.member(member, live_member) && live_member.source == source &&
        live_member.size == old_size && live_schema.map_occurrence(source) == member);
    TEST_EXPECT(ctx, baked_schema.resolve(baked_error));
    TEST_EXPECT(ctx, baked_schema.member(member, baked_member) && baked_schema.map_occurrence(baked_member.source) == member);

    const std::string bad = one("\"missing\"", "");
    CLiveDocument bad_live;
    const CDocumentReport bad_parsed = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size(), 1u }, bad_live);
    TEST_EXPECT(ctx, bad_parsed.accepted());
    CBakedDocumentBlock bad_block;
    TEST_EXPECT(ctx, document_translation::bake(bad_live, bad_block));
    TEST_EXPECT(ctx, !live_schema.resolve(bad_live, live_error) && !baked_schema.resolve(bad_block.document(), baked_error));
    TEST_EXPECT(ctx, !live_schema.is_ready() && !baked_schema.is_ready());
    TEST_EXPECT(ctx, live_error.reason == baked_error.reason && live_error.stage == baked_error.stage);
    const CSchemaDocumentQuery bad_live_query{ bad_live };
    const CSchemaDocumentQuery bad_baked_query{ bad_block.document() };
    TEST_EXPECT(ctx, bad_live_query.contains(live_error.occurrence) &&
        bad_baked_query.contains(baked_error.occurrence));
    TEST_EXPECT(ctx, bad_live_query.string_value(live_error.occurrence) == CStringView{ "missing" } &&
        bad_baked_query.string_value(baked_error.occurrence) == CStringView{ "missing" });
    TEST_EXPECT(ctx, bad_live_query.parent(live_error.occurrence) == live_error.enclosing_member &&
        bad_baked_query.parent(baked_error.occurrence) == baked_error.enclosing_member);
    TEST_EXPECT(ctx, bad_live_query.name(live_error.enclosing_member) == CStringView{ "value" } &&
        bad_baked_query.name(baked_error.enclosing_member) == CStringView{ "value" });
    TEST_EXPECT(ctx, bad_live_query.name(live_error.enclosing_type) == CStringView{ "Test" } &&
        bad_baked_query.name(baked_error.enclosing_type) == CStringView{ "Test" });
    TEST_EXPECT(ctx, !live_schema.map_occurrence(source) && !live_schema.member(member, live_member));
}

static void test_live_resolution_move_failure_and_depth(TTestContext& ctx)
{
    const std::string nested_default = one(
        R"({"element":{"element":"u8","count":2},"count":2})", "[[1],[2,3]]");
    CLiveDocument live;
    const CDocumentReport parsed = document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(nested_default.data()), nested_default.size(), 1u }, live);
    TEST_EXPECT(ctx, parsed.accepted());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    CResolvedSchema live_schema, baked_schema;
    SDiagnostic live_error, baked_error;
    TEST_EXPECT(ctx, live_schema.resolve(live, live_error) && baked_schema.resolve(block.document(), baked_error));
    const CSchemaIndex type = live_schema.find_type(CStringView{ "Test" });
    const CSchemaIndex member_index = live_schema.find_member(type, CStringView{ "value" });
    SMember live_member, baked_member;
    TEST_EXPECT(ctx, live_schema.member(member_index, live_member) && baked_schema.member(member_index, baked_member));
    CSchemaIndex live_inner, baked_inner;
    TEST_EXPECT(ctx, live_schema.default_element(live_member.type, live_member.default_description, 1u, live_inner) &&
        baked_schema.default_element(baked_member.type, baked_member.default_description, 1u, baked_inner));
    SDefault live_default, baked_default;
    SType array_type;
    TEST_EXPECT(ctx, live_schema.type(live_member.type, array_type));
    TEST_EXPECT(ctx, live_schema.default_value(array_type.element_or_storage, live_inner, live_default) &&
        baked_schema.default_value(array_type.element_or_storage, baked_inner, baked_default));
    TEST_EXPECT(ctx, live_default.supplied_count == 2u && baked_default.supplied_count == 2u);
    CSchemaIndex live_leaf, baked_leaf;
    TEST_EXPECT(ctx, live_schema.default_element(array_type.element_or_storage, live_inner, 1u, live_leaf) &&
        baked_schema.default_element(array_type.element_or_storage, baked_inner, 1u, baked_leaf));
    SType scalar_type;
    TEST_EXPECT(ctx, live_schema.type(array_type.element_or_storage, scalar_type));
    TEST_EXPECT(ctx, live_schema.default_value(scalar_type.element_or_storage, live_leaf, live_default) &&
        baked_schema.default_value(scalar_type.element_or_storage, baked_leaf, baked_default));
    TEST_EXPECT(ctx, live_default.scalar.value.unsigned_value == 3u && baked_default.scalar.value.unsigned_value == 3u);

    const CSchemaHandle source = live_member.source;
    CResolvedSchema moved{ std::move(live_schema) };
    TEST_EXPECT(ctx, !live_schema.is_ready() && moved.name(live_member.name) == CStringView{ "value" });
    TEST_EXPECT(ctx, moved.map_occurrence(source) == member_index && moved.resolve(live_error));
    TEST_EXPECT(ctx, moved.member(member_index, live_member) && live_member.source == source);
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CResolvedSchema failure_schema;
            TEST_EXPECT(ctx, failure_schema.resolve(live, live_error));
            failing.fail_on = failing.calls;
            TEST_EXPECT(ctx, !failure_schema.resolve(live_error));
            TEST_EXPECT(ctx, live_error.reason == EReason::allocation_failed && !failure_schema.is_ready());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }

    for (const unsigned definition_count : { 128u, 129u })
    {
        std::string named_types;
        for (unsigned n = 0u; n < definition_count; ++n)
        {
            if (n != 0u)
            {
                named_types += ',';
            }
            named_types += "\"T" + std::to_string(n) + "\":{\"members\":[{\"next\":{\"type\":\"";
            named_types += (n + 1u == definition_count) ? "u8" : ("T" + std::to_string(n + 1u));
            named_types += "\"}}]}";
        }
        const std::string named_chain = "{\"types\":{\"structures\":{" + named_types + "}}}";
        CLiveDocument deep_live;
        const CDocumentReport named_parsed = document_parser::parse(
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(named_chain.data()), named_chain.size(), 1u }, deep_live);
        TEST_EXPECT(ctx, named_parsed.accepted());
        CBakedDocumentBlock deep_block;
        TEST_EXPECT(ctx, document_translation::bake(deep_live, deep_block));
        if (definition_count == 128u)
        {
            TEST_EXPECT(ctx, moved.resolve(deep_live, live_error) && baked_schema.resolve(deep_block.document(), baked_error));
            TEST_EXPECT(ctx, moved.definition_count() == definition_count && baked_schema.definition_count() == definition_count);
        }
        else
        {
            TEST_EXPECT(ctx, !moved.resolve(deep_live, live_error) && live_error.reason == EReason::storage_limit);
            TEST_EXPECT(ctx, !baked_schema.resolve(deep_block.document(), baked_error) && baked_error.reason == EReason::storage_limit);
        }
    }

    for (const unsigned array_depth : { 254u, 255u })
    {
        std::string nested_type = "\"u8\"";
        std::string nested_values = "1";
        for (unsigned n = 0u; n < array_depth; ++n)
        {
            nested_type = "{\"element\":" + nested_type + ",\"count\":1}";
            nested_values = '[' + nested_values + ']';
        }
        const std::string deep_default = one(nested_type, nested_values);
        CLiveDocument default_live;
        const CDocumentReport default_parsed = document_parser::parse(
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(deep_default.data()), deep_default.size(), 1u }, default_live);
        TEST_EXPECT(ctx, default_parsed.accepted());
        CBakedDocumentBlock default_block;
        TEST_EXPECT(ctx, document_translation::bake(default_live, default_block));
        if (array_depth == 254u)
        {
            TEST_EXPECT(ctx, moved.resolve(default_live, live_error) && baked_schema.resolve(default_block.document(), baked_error));
            for (CResolvedSchema* resolved : { &moved, &baked_schema })
            {
                const CSchemaIndex root_type = resolved->find_type(CStringView{ "Test" });
                const CSchemaIndex root_member = resolved->find_member(root_type, CStringView{ "value" });
                SMember leaf_member;
                TEST_EXPECT(ctx, resolved->member(root_member, leaf_member));
                CSchemaIndex leaf_type = leaf_member.type;
                CSchemaIndex leaf_default = leaf_member.default_description;
                for (unsigned n = 0u; n < array_depth; ++n)
                {
                    SType description;
                    CSchemaIndex child_default;
                    TEST_EXPECT(ctx, resolved->type(leaf_type, description) && description.category == ECategory::array);
                    TEST_EXPECT(ctx, resolved->default_element(leaf_type, leaf_default, 0u, child_default));
                    leaf_type = description.element_or_storage;
                    leaf_default = child_default;
                }
                SDefault leaf;
                TEST_EXPECT(ctx, resolved->default_value(leaf_type, leaf_default, leaf));
                TEST_EXPECT(ctx, leaf.scalar.value.unsigned_value == 1u);
            }
        }
        else
        {
            TEST_EXPECT(ctx, !moved.resolve(default_live, live_error) && live_error.reason == EReason::storage_limit);
            TEST_EXPECT(ctx, !baked_schema.resolve(default_block.document(), baked_error) && baked_error.reason == EReason::storage_limit);
        }
    }
}

static bool parse_live(const std::string& text, CLiveDocument& document)
{
    return document_parser::parse(
        CByteConstView{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), 1u }, document).accepted();
}

static void test_schema_wrapper_queries_and_transfer(TTestContext& ctx)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(fixture, block));
    CBakedSchema baked;
    TEST_EXPECT(ctx, baked.set_document(block.document()) && baked.document_ready() && !baked.resolved_ready());
    const CSchemaDocumentQuery baked_query = baked.document_query();
    TEST_EXPECT(ctx, baked_query.name(baked.types_root()) == CStringView{ "types" });
    TEST_EXPECT(ctx, baked_query.object_child(baked_query.root(), CStringView{ "instances" }));
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, baked.resolve(diagnostic) && baked.resolved() != nullptr);
    TEST_EXPECT(ctx, baked.resolved()->definition_count() != 0u);

    CBakedSchema moved_baked;
    TEST_EXPECT(ctx, moved_baked.try_take_from(std::move(baked)));
    TEST_EXPECT(ctx, !baked.document_ready() && moved_baked.resolved_ready());
    TEST_EXPECT(ctx, moved_baked.resolved()->find_type(CStringView{ "Vector4" }));

    CLiveSchema moved_live;
    CSchemaHandle retained_type;
    {
        CLiveDocument parsed;
        TEST_EXPECT(ctx, parse_live(fixture, parsed));
        CLiveSchema live;
        TEST_EXPECT(ctx, live.try_adopt(std::move(parsed)) && !parsed.is_ready());
        TEST_EXPECT(ctx, live.resolve(diagnostic));
        const CSchemaDocumentQuery query = live.document_query();
        retained_type = query.object_child(query.object_child(live.types_root(), CStringView{ "structures" }),
            CStringView{ "Vector4" });
        TEST_EXPECT(ctx, live.resolved()->map_occurrence(retained_type) == live.resolved()->find_type(CStringView{ "Vector4" }));
        TEST_EXPECT(ctx, moved_live.try_take_from(std::move(live)) && !live.document_ready());
    }
    TEST_EXPECT(ctx, moved_live.document_query().name(retained_type) == CStringView{ "Vector4" });
    TEST_EXPECT(ctx, moved_live.resolved()->map_occurrence(retained_type) ==
        moved_live.resolved()->find_type(CStringView{ "Vector4" }));
    TEST_EXPECT(ctx, moved_live.resolve(diagnostic));
    TEST_EXPECT(ctx, moved_live.resolved()->find_type(CStringView{ "Vector4" }));
}

static void test_schema_wrapper_editor(TTestContext& ctx)
{
    CLiveSchema live;
    TEST_EXPECT(ctx, live.initialise());
    CLiveSchema::CEditor editor = live.edit();
    const CSchemaHandle root = live.document_query().root();
    const CSchemaHandle types = editor.append_object(root, CStringView{ "types" });
    const CSchemaHandle structures = editor.append_object(types, CStringView{ "structures" });
    const CSchemaHandle definition = editor.append_object(structures, CStringView{ "Test" });
    const CSchemaHandle members = editor.append_array(definition, CStringView{ "members" });
    const CSchemaHandle member = editor.append_object(members, CStringView{ "value" });
    const CSchemaHandle type = editor.append_string(member, CStringView{ "u8" }, CStringView{ "type" });
    TEST_EXPECT(ctx, types && structures && definition && members && member && type);
    TEST_EXPECT(ctx, !editor.append_object(root, CStringView{ "instances" }));
    TEST_EXPECT(ctx, !editor.set_name(types, CStringView{ "different" }) && !editor.erase(types));
    TEST_EXPECT(ctx, !editor.append_object(root, CStringView{ "types" }));
    const std::uint32_t before_duplicate = live.document_query().child_count(structures);
    TEST_EXPECT(ctx, !editor.append_object(structures, CStringView{ "Test" }));
    TEST_EXPECT(ctx, live.document_query().child_count(structures) == before_duplicate);
    SDiagnostic diagnostic;
    const bool resolved = live.resolve(diagnostic);
    TEST_EXPECT(ctx, resolved);
    if (!resolved) return;
    const CSchemaIndex named = live.resolved()->find_type(CStringView{ "Test" });
    SType type_info;
    TEST_EXPECT(ctx, live.resolved()->type(named, type_info) && type_info.size == 1u);

    CSchemaBinding binding;
    TEST_EXPECT(ctx, binding.bind(live) && live.reference_count() == 1u);
    {
        tests::TAssertionTestScope assertions{ ctx };
        assertions.expect_assertion(ctx, [&]
        {
            TEST_EXPECT(ctx, !editor.replace_string(type, CStringView{ "u16" }));
        });
        assertions.expect_assertion(ctx, [&]
        {
            TEST_EXPECT(ctx, !live.reset());
        });
        CLiveSchema empty;
        assertions.expect_assertion(ctx, [&]
        {
            TEST_EXPECT(ctx, !empty.try_take_from(std::move(live)));
        });
    }
    TEST_EXPECT(ctx, live.document_query().string_value(type) == CStringView{ "u8" } && live.resolved_ready());
    TEST_EXPECT(ctx, live.resolve(diagnostic) && binding.is_usable());
    TEST_EXPECT(ctx, live.resolved()->find_type(CStringView{ "Test" }) == named);
    binding.release();
    TEST_EXPECT(ctx, editor.replace_string(type, CStringView{ "u16" }) && !live.resolved_ready());
    TEST_EXPECT(ctx, live.document_query().string_value(type) == CStringView{ "u16" });
    TEST_EXPECT(ctx, live.resolve(diagnostic));
    TEST_EXPECT(ctx, live.resolved()->type(live.resolved()->find_type(CStringView{ "Test" }), type_info) && type_info.size == 2u);

    const std::string malformed = R"({"types":null,"instances":{"keep":1},"data":{"keep":2}})";
    CLiveDocument parsed;
    TEST_EXPECT(ctx, parse_live(malformed, parsed));
    CLiveSchema combined;
    TEST_EXPECT(ctx, combined.try_adopt(std::move(parsed)));
    CLiveSchema::CEditor combined_editor = combined.edit();
    const CSchemaDocumentQuery combined_query = combined.document_query();
    const CSchemaHandle combined_root = combined_query.root();
    const CSchemaHandle instances = combined_query.object_child(combined_root, CStringView{ "instances" });
    const CSchemaHandle data = combined_query.object_child(combined_root, CStringView{ "data" });
    TEST_EXPECT(ctx, !combined_editor.append_object(instances, CStringView{ "blocked" }));
    TEST_EXPECT(ctx, !combined_editor.replace_object(instances));
    TEST_EXPECT(ctx, combined_editor.replace_object(combined.types_root()));
    TEST_EXPECT(ctx, combined.document_query().object_child(combined_root, CStringView{ "instances" }) == instances &&
        combined.document_query().object_child(combined_root, CStringView{ "data" }) == data);
    TEST_EXPECT(ctx, combined.resolve(diagnostic));
}

static void test_schema_wrapper_bindings(TTestContext& ctx)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(one("\"u8\"", "1"), block));
    SFailingAllocator failing{ 0u, SIZE_MAX };
    memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CBakedSchema first, second;
        SDiagnostic diagnostic;
        TEST_EXPECT(ctx, first.set_document(block.document()) && second.set_document(block.document()));
        TEST_EXPECT(ctx, first.resolve(diagnostic) && second.resolve(diagnostic));
        CSchemaBinding head, middle, tail, other;
        TEST_EXPECT(ctx, head.bind(first) && middle.bind(first) && tail.bind(first) && other.bind(second));
        TEST_EXPECT(ctx, first.reference_count() == 3u && second.reference_count() == 1u);
        middle = std::move(head);
        TEST_EXPECT(ctx, !head.is_attached() && middle.is_usable() && first.reference_count() == 2u);
        middle = std::move(middle);
        TEST_EXPECT(ctx, middle.is_usable() && first.reference_count() == 2u);
        other = std::move(tail);
        TEST_EXPECT(ctx, !tail.is_attached() && first.reference_count() == 2u && second.reference_count() == 0u);
        TEST_EXPECT(ctx, other.bind(second) && first.reference_count() == 1u && second.reference_count() == 1u);
        middle.release();
        TEST_EXPECT(ctx, first.reference_count() == 0u && second.reference_count() == 1u);

        CSchemaBinding first_link, middle_link, next_link, last_link;
        TEST_EXPECT(ctx, first_link.bind(first) && middle_link.bind(first) && next_link.bind(first) && last_link.bind(first));
        middle_link.release();
        TEST_EXPECT(ctx, first.reference_count() == 3u && first_link.is_usable() && next_link.is_usable());
        last_link.release();
        first_link.release();
        TEST_EXPECT(ctx, first.reference_count() == 1u && next_link.is_usable());
        next_link.release();
        TEST_EXPECT(ctx, first.reference_count() == 0u);

        CSchemaBinding surviving_a, surviving_b, surviving_c;
        {
            CBakedSchema temporary;
            TEST_EXPECT(ctx, temporary.set_document(block.document()) && temporary.resolve(diagnostic));
            TEST_EXPECT(ctx, surviving_a.bind(temporary) && surviving_b.bind(temporary) && surviving_c.bind(temporary));
            TEST_EXPECT(ctx, temporary.reference_count() == 3u);
        }
        TEST_EXPECT(ctx, !surviving_a.is_attached() && !surviving_b.is_attached() && !surviving_c.is_attached());
        TEST_EXPECT(ctx, !surviving_a.resolved() && !surviving_b.document_query().is_ready());
        surviving_a.release();
        surviving_b.release();
        surviving_c.release();

        CSchemaBinding persistent;
        TEST_EXPECT(ctx, persistent.bind(first));
        failing.fail_on = failing.calls;
        TEST_EXPECT(ctx, !first.resolve(diagnostic) && diagnostic.reason == EReason::allocation_failed);
        TEST_EXPECT(ctx, persistent.is_attached() && !persistent.is_usable() && !persistent.resolved());
        TEST_EXPECT(ctx, persistent.document_query().is_ready() && first.reference_count() == 1u);
        TEST_EXPECT(ctx, !other.bind(first) && other.is_usable() && second.reference_count() == 1u);
        TEST_EXPECT(ctx, !persistent.bind(first) && persistent.is_attached());
        failing.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, first.resolve(diagnostic) && persistent.is_usable());
        persistent.release();
        other.release();
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_schema_wrapper_rejections(TTestContext& ctx)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(one("\"u8\"", "1"), block));
    SDiagnostic diagnostic;
    CBakedSchema baked, baked_destination;
    TEST_EXPECT(ctx, baked.set_document(block.document()) && baked.resolve(diagnostic));
    CSchemaBinding baked_client;
    TEST_EXPECT(ctx, baked_client.bind(baked));
    {
        tests::TAssertionTestScope assertions{ ctx };
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !baked.clear_resolution()); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !baked.clear()); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !baked.set_document(block.document())); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !baked_destination.try_take_from(std::move(baked))); });
    }
    TEST_EXPECT(ctx, baked.document_ready() && baked.resolved_ready() && baked_client.is_usable() &&
        baked.reference_count() == 1u && !baked_destination.document_ready());
    baked_client.release();
    TEST_EXPECT(ctx, baked_destination.set_document(block.document()));
    TEST_EXPECT(ctx, !baked_destination.try_take_from(std::move(baked)) && baked.document_ready() &&
        baked_destination.document_ready());

    CLiveDocument parsed, candidate;
    const std::string input = one("\"u8\"", "1");
    TEST_EXPECT(ctx, parse_live(input, parsed) && parse_live(input, candidate));
    CLiveSchema live, live_destination;
    TEST_EXPECT(ctx, live.try_adopt(std::move(parsed)) && live.resolve(diagnostic));
    CSchemaBinding live_client;
    TEST_EXPECT(ctx, live_client.bind(live));
    {
        tests::TAssertionTestScope assertions{ ctx };
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !live.clear_resolution()); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !live.clear()); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !live.try_adopt(std::move(candidate))); });
    }
    TEST_EXPECT(ctx, candidate.is_ready() && live.document_ready() && live.resolved_ready() && live_client.is_usable());
    live_client.release();
    TEST_EXPECT(ctx, live_destination.try_adopt(std::move(candidate)) && !candidate.is_ready());
    CLiveDocument another;
    TEST_EXPECT(ctx, parse_live(input, another));
    TEST_EXPECT(ctx, !live_destination.try_adopt(std::move(another)) && another.is_ready() &&
        live_destination.document_ready());
    TEST_EXPECT(ctx, !live_destination.try_take_from(std::move(live)) && live.document_ready() &&
        live_destination.document_ready() && live.resolved_ready());

    const CSchemaHandle old_root = live.document_query().root();
    {
        SFailingAllocator failing{ 0u, 0u };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            TEST_EXPECT(ctx, !live.reset());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, live.document_ready() && live.resolved_ready() && live.document_query().root() == old_root);

    CSchemaBinding surviving_a, surviving_b;
    {
        CLiveDocument temporary_document;
        TEST_EXPECT(ctx, parse_live(input, temporary_document));
        CLiveSchema temporary;
        TEST_EXPECT(ctx, temporary.try_adopt(std::move(temporary_document)) && temporary.resolve(diagnostic));
        TEST_EXPECT(ctx, surviving_a.bind(temporary) && surviving_b.bind(temporary));
    }
    TEST_EXPECT(ctx, !surviving_a.is_attached() && !surviving_b.is_attached() &&
        !surviving_a.resolved() && !surviving_b.document_query().is_ready());
    surviving_a.release();
    surviving_b.release();
}

static void test_schema_wrapper_edit_allocations(TTestContext& ctx)
{
    const std::string document = one("\"u8\"", "1");
    const std::string long_value(65536u, 'x');
    unsigned rejected = 0u, accepted = 0u;
    for (std::size_t failure_offset = 0u; failure_offset < 8u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveDocument parsed;
            TEST_EXPECT(ctx, parse_live(document, parsed));
            CLiveSchema live;
            TEST_EXPECT(ctx, live.try_adopt(std::move(parsed)));
            const CSchemaDocumentQuery query = live.document_query();
            const CSchemaHandle structures = query.object_child(live.types_root(), CStringView{ "structures" });
            const CSchemaHandle definition = query.object_child(structures, CStringView{ "Test" });
            const CSchemaHandle members = query.object_child(definition, CStringView{ "members" });
            const CSchemaHandle member = query.array_at(members, 0u);
            const CSchemaHandle original = query.object_child(member, CStringView{ "default" });
            TEST_EXPECT(ctx, original && query.value_kind(original) == EDocumentValueKind::integer);
            const CStringView replacement{ long_value.data(), long_value.size() };
            failing.fail_on = failing.calls + failure_offset;
            const bool changed = live.edit().replace_string(original, replacement);
            if (changed)
            {
                ++accepted;
                TEST_EXPECT(ctx, live.document_query().string_value(original) == replacement);
            }
            else
            {
                ++rejected;
                std::int64_t signed_value{};
                std::uint64_t unsigned_value{};
                TEST_EXPECT(ctx,
                    (live.document_query().signed_integer_value(original, signed_value) && (signed_value == 1)) ||
                    (live.document_query().unsigned_integer_value(original, unsigned_value) && (unsigned_value == 1u)));
                TEST_EXPECT(ctx, live.document_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, rejected != 0u && accepted != 0u);

    //  Six aggregate values including the root use twelve slots. Two scalars
    //  bring the total to fourteen; seventeen fillers leave one of the initial
    //  32 slots free. The new candidate fills it; detaching the old payload
    //  must grow the node store.
    SFailingAllocator failing{ 0u, SIZE_MAX };
    memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CLiveSchema live;
        TEST_EXPECT(ctx, live.initialise(32u));
        CLiveSchema::CEditor editor = live.edit();
        const CSchemaHandle types = editor.append_object(live.document_query().root(), CStringView{ "types" });
        const CSchemaHandle structures = editor.append_object(types, CStringView{ "structures" });
        const CSchemaHandle definition = editor.append_object(structures, CStringView{ "Test" });
        const CSchemaHandle members = editor.append_array(definition, CStringView{ "members" });
        const CSchemaHandle member = editor.append_object(members, CStringView{ "value" });
        TEST_EXPECT(ctx, editor.append_string(member, CStringView{ "u8" }, CStringView{ "type" }));
        const CSchemaHandle original = editor.append_signed(member, 1, CStringView{ "default" });
        TEST_EXPECT(ctx, original);
        for (unsigned n = 0u; n < 17u; ++n)
        {
            const std::string name = "padding" + std::to_string(n);
            TEST_EXPECT(ctx, editor.append_null(member, CStringView{ name.c_str() }));
        }
        failing.fail_on = failing.calls;
        TEST_EXPECT(ctx, !editor.replace_string(original, CStringView{ "u8" }));
        TEST_EXPECT(ctx, failing.calls == failing.fail_on + 1u);
        std::int64_t old_value{};
        TEST_EXPECT(ctx, live.document_query().signed_integer_value(original, old_value) && old_value == 1);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static bool baked_has_string(const CBakedDocument& document, const CStringView& sought) noexcept
{
    for (std::uint32_t rank = 1u; rank <= document.string_value_count(); ++rank)
    {
        if (document.string_value(document.string_value_id_at_rank(rank)) == sought)
        {
            return true;
        }
    }
    return false;
}

static void test_schema_root_member_translation(TTestContext& ctx)
{
    const std::string middle = R"({"instances":{"private":"instance-secret"},"types":{"number":4294967295,"text":"selected"},"data":{"private":"data-secret"}})";
    const std::string last = R"({"data":{"private":"data-secret"},"instances":{"private":"instance-secret"},"types":{"number":4294967295,"text":"selected"}})";
    for (const std::string& input : { middle, last })
    {
        CLiveDocument live;
        TEST_EXPECT(ctx, parse_live(input, live));
        const CNodeKey source_types = live.object_child(live.root(), CStringView{ "types" });
        const CNodeKey source_number = live.object_child(source_types, CStringView{ "number" });
        const CNodeKey source_text = live.object_child(source_types, CStringView{ "text" });
        TEST_EXPECT(ctx, live.set_newline_escaping_suppressed(source_text, true));
        CIntegerMetadata original_metadata;
        TEST_EXPECT(ctx, live.integer_metadata(source_number, original_metadata));

        CBakedDocumentBlock projected_block;
        TEST_EXPECT(ctx, document_translation::bake_root_member(live, CStringView{ "types" }, projected_block));
        const CBakedDocument projected = projected_block.document();
        const CBakedValueIndex projected_root = projected.root();
        const CBakedValueIndex projected_types = projected.object_child(projected_root, CStringView{ "types" });
        const CBakedValueIndex projected_number = projected.object_child(projected_types, CStringView{ "number" });
        const CBakedValueIndex projected_text = projected.object_child(projected_types, CStringView{ "text" });
        CIntegerMetadata baked_metadata;
        TEST_EXPECT(ctx, projected.check_integrity() && projected.value_count() == 4u &&
            projected.child_count(projected_root) == 1u && projected_types.is_valid());
        TEST_EXPECT(ctx, !projected.object_child(projected_root, CStringView{ "instances" }) &&
            !projected.object_child(projected_root, CStringView{ "data" }));
        TEST_EXPECT(ctx, !baked_has_string(projected, CStringView{ "instance-secret" }) &&
            !baked_has_string(projected, CStringView{ "data-secret" }) &&
            baked_has_string(projected, CStringView{ "selected" }));
        TEST_EXPECT(ctx, projected.integer_metadata(projected_number, baked_metadata) &&
            baked_metadata == original_metadata && projected.suppresses_newline_escaping(projected_text));
        TEST_EXPECT(ctx, !projected.previous_sibling(projected_types) && !projected.next_sibling(projected_types));

        CBakedDocumentBlock full_block;
        TEST_EXPECT(ctx, document_translation::bake(live, full_block));
        CLiveDocument projected_live;
        TEST_EXPECT(ctx, document_translation::promote_root_member(
            full_block.document(), CStringView{ "types" }, projected_live));
        TEST_EXPECT(ctx, projected_live.check_integrity() && projected_live.child_count(projected_live.root()) == 1u);
        const CNodeKey live_types = projected_live.object_child(projected_live.root(), CStringView{ "types" });
        const CNodeKey live_number = projected_live.object_child(live_types, CStringView{ "number" });
        const CNodeKey live_text = projected_live.object_child(live_types, CStringView{ "text" });
        CIntegerMetadata live_metadata;
        TEST_EXPECT(ctx, projected_live.integer_metadata(live_number, live_metadata) &&
            live_metadata == original_metadata && projected_live.suppresses_newline_escaping(live_text));
        TEST_EXPECT(ctx, !projected_live.object_child(projected_live.root(), CStringView{ "instances" }) &&
            !projected_live.object_child(projected_live.root(), CStringView{ "data" }));
    }

    CLiveDocument missing;
    TEST_EXPECT(ctx, parse_live(R"({"instances":{"value":null}})", missing));
    const CNodeKey unrelated = missing.create_empty(CStringView{ "unfinished" });
    TEST_EXPECT(ctx, missing.append_child(missing.root(), unrelated).succeeded());
    CBakedDocumentBlock root_only;
    TEST_EXPECT(ctx, document_translation::bake_root_member(missing, CStringView{ "types" }, root_only));
    TEST_EXPECT(ctx, root_only.document().check_integrity() && root_only.document().value_count() == 1u &&
        root_only.document().child_count(root_only.document().root()) == 0u);
    CLiveDocument restored;
    TEST_EXPECT(ctx, document_translation::promote_root_member(root_only.document(), CStringView{ "types" }, restored));
    TEST_EXPECT(ctx, restored.check_integrity() && restored.child_count(restored.root()) == 0u);
}

static void test_schema_conversion(TTestContext& ctx)
{
    const std::string combined = R"({"instances":{"private":"instance-secret"},"types":{"structures":{"Test":{"members":[{"value":{"type":"u32","default":4294967295}}]}}},"data":{"private":"data-secret"}})";
    SDiagnostic diagnostic;
    CLiveSchema promoted;
    {
        CBakedDocumentBlock source_block;
        TEST_EXPECT(ctx, bake(combined, source_block));
        CBakedSchema source;
        TEST_EXPECT(ctx, source.set_document(source_block.document()));
        TEST_EXPECT(ctx, !source.resolved_ready() && source.promote(promoted, diagnostic));
        TEST_EXPECT(ctx, promoted.document_ready() && promoted.resolved_ready() &&
            !source.resolved_ready() && source.reference_count() == 0u && promoted.reference_count() == 0u);
        TEST_EXPECT(ctx, promoted.document_query().child_count(promoted.document_query().root()) == 1u &&
            !promoted.document_query().object_child(promoted.document_query().root(), CStringView{ "instances" }));
        TEST_EXPECT(ctx, source.resolve(diagnostic));
        CSchemaBinding source_client;
        TEST_EXPECT(ctx, source_client.bind(source) && source.reference_count() == 1u);
        CLiveSchema other;
        TEST_EXPECT(ctx, source.promote(other, diagnostic) && other.resolved_ready() &&
            source_client.is_usable() && source.reference_count() == 1u && other.reference_count() == 0u);
    }
    TEST_EXPECT(ctx, promoted.resolved_ready() &&
        promoted.resolved()->find_type(CStringView{ "Test" }).is_valid());

    CSchemaBinding live_client;
    TEST_EXPECT(ctx, live_client.bind(promoted) && promoted.reference_count() == 1u);
    CBakedDocumentBlock demoted_block;
    CBakedSchema demoted;
    TEST_EXPECT(ctx, promoted.demote(demoted_block, demoted));
    TEST_EXPECT(ctx, demoted_block.is_ready() && demoted.document_ready() && !demoted.resolved_ready() &&
        demoted.reference_count() == 0u && live_client.is_usable() && promoted.reference_count() == 1u);
    TEST_EXPECT(ctx, demoted_block.document().child_count(demoted_block.document().root()) == 1u &&
        !baked_has_string(demoted_block.document(), CStringView{ "instance-secret" }));
    TEST_EXPECT(ctx, demoted.resolve(diagnostic) && demoted.resolved()->find_type(CStringView{ "Test" }).is_valid());

    CBakedDocumentBlock combined_demoted_block;
    CBakedSchema combined_demoted;
    {
        CLiveDocument parsed;
        TEST_EXPECT(ctx, parse_live(combined, parsed));
        CLiveSchema combined_live;
        TEST_EXPECT(ctx, combined_live.try_adopt(std::move(parsed)));
        TEST_EXPECT(ctx, combined_live.demote(combined_demoted_block, combined_demoted));
    }
    TEST_EXPECT(ctx, combined_demoted_block.document().child_count(combined_demoted_block.document().root()) == 1u &&
        !baked_has_string(combined_demoted_block.document(), CStringView{ "instance-secret" }) &&
        !baked_has_string(combined_demoted_block.document(), CStringView{ "data-secret" }));
    TEST_EXPECT(ctx, combined_demoted.resolve(diagnostic) &&
        combined_demoted.resolved()->find_type(CStringView{ "Test" }).is_valid());
    const SDefault copied_default = first_default(ctx, *combined_demoted.resolved(), "Test", "value");
    TEST_EXPECT(ctx, copied_default.scalar.value.unsigned_value == 4294967295u);

    CLiveSchema occupied_live;
    TEST_EXPECT(ctx, occupied_live.initialise());
    const CSchemaHandle occupied_root = occupied_live.document_query().root();
    TEST_EXPECT(ctx, !demoted.promote(occupied_live, diagnostic) && diagnostic.reason == EReason::invalid_input &&
        occupied_live.document_query().root() == occupied_root && !occupied_live.resolved_ready());
    CBakedDocumentBlock occupied_block;
    TEST_EXPECT(ctx, bake(one("\"u8\"", "1"), occupied_block));
    const std::uint8_t* const occupied_bytes = occupied_block.bytes().data();
    CBakedSchema empty_output;
    TEST_EXPECT(ctx, !promoted.demote(occupied_block, empty_output) &&
        occupied_block.bytes().data() == occupied_bytes && !empty_output.document_ready());
    CBakedSchema occupied_schema;
    TEST_EXPECT(ctx, occupied_schema.set_document(occupied_block.document()));
    CBakedDocumentBlock empty_block;
    TEST_EXPECT(ctx, !promoted.demote(empty_block, occupied_schema) && !empty_block.is_ready() &&
        occupied_schema.document_ready());

    CBakedDocumentBlock malformed_block;
    TEST_EXPECT(ctx, bake(one("\"MissingType\"", ""), malformed_block));
    CBakedSchema malformed;
    TEST_EXPECT(ctx, malformed.set_document(malformed_block.document()));
    CLiveSchema rejected;
    TEST_EXPECT(ctx, !malformed.promote(rejected, diagnostic) && diagnostic.reason == EReason::unknown_type &&
        diagnostic.stage != EStage::none && !diagnostic.occurrence && !diagnostic.enclosing_type &&
        !diagnostic.enclosing_member && !diagnostic.related && !diagnostic.ranges_available &&
        !rejected.document_ready() && malformed.document_ready());

    CLiveSchema unfinished;
    TEST_EXPECT(ctx, unfinished.initialise());
    CBakedDocumentBlock unfinished_block;
    CBakedSchema unfinished_baked;
    TEST_EXPECT(ctx, unfinished.demote(unfinished_block, unfinished_baked) &&
        unfinished_block.document().value_count() == 1u && !unfinished_baked.resolved_ready());
    CLiveSchema failed_repromotion;
    TEST_EXPECT(ctx, !unfinished_baked.promote(failed_repromotion, diagnostic) &&
        diagnostic.reason != EReason::none && !failed_repromotion.document_ready());
}

static void test_schema_conversion_allocations(TTestContext& ctx)
{
    const std::string input = one("\"u8\"", "1");
    bool translation_failure = false, resolution_failure = false, promotion_success = false;
    for (std::size_t offset = 0u; offset < 128u && !promotion_success; ++offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CBakedDocumentBlock block;
            TEST_EXPECT(ctx, bake(input, block));
            CBakedSchema source;
            CLiveSchema destination;
            TEST_EXPECT(ctx, source.set_document(block.document()));
            failing.fail_on = failing.calls + offset;
            SDiagnostic diagnostic;
            promotion_success = source.promote(destination, diagnostic);
            if (promotion_success)
            {
                TEST_EXPECT(ctx, destination.resolved_ready() && diagnostic.reason == EReason::none);
            }
            else
            {
                translation_failure |= diagnostic.reason == EReason::translation_failed;
                resolution_failure |= diagnostic.reason == EReason::allocation_failed;
                TEST_EXPECT(ctx, (diagnostic.reason == EReason::translation_failed ||
                    diagnostic.reason == EReason::allocation_failed) && !destination.document_ready() &&
                    source.document_ready() && !diagnostic.occurrence && !diagnostic.enclosing_type &&
                    !diagnostic.enclosing_member && !diagnostic.related && !diagnostic.ranges_available);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, translation_failure && resolution_failure && promotion_success);

    bool demotion_failure = false, demotion_success = false;
    for (std::size_t offset = 0u; offset < 128u && !demotion_success; ++offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveDocument parsed;
            TEST_EXPECT(ctx, parse_live(input, parsed));
            CLiveSchema source;
            TEST_EXPECT(ctx, source.try_adopt(std::move(parsed)));
            CBakedDocumentBlock destination_block;
            CBakedSchema destination_schema;
            failing.fail_on = failing.calls + offset;
            demotion_success = source.demote(destination_block, destination_schema);
            if (demotion_success)
            {
                TEST_EXPECT(ctx, destination_block.is_ready() && destination_schema.document_ready() &&
                    !destination_schema.resolved_ready());
            }
            else
            {
                demotion_failure = true;
                TEST_EXPECT(ctx, source.document_ready() && !destination_block.is_ready() &&
                    !destination_schema.document_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, demotion_failure && demotion_success);
}

static void test_value_codec(TTestContext& ctx)
{
    CBakedDocumentBlock schema_block;
    CResolvedSchema resolved;
    if (!resolve(ctx, fixture, schema_block, resolved))
    {
        return;
    }
    const CSchemaIndex vector = resolved.find_type(CStringView{ "Vector4" });
    const CSchemaIndex arrays = resolved.find_type(CStringView{ "Arrays" });
    const CSchemaIndex flags = resolved.find_type(CStringView{ "Flags" });
    SType vector_type, arrays_type, flags_type;
    TEST_EXPECT(ctx, resolved.type(vector, vector_type) && resolved.type(arrays, arrays_type) &&
        resolved.type(flags, flags_type));
    const bool fixture_sizes_valid = vector_type.size != 0u && arrays_type.size != 0u &&
        flags_type.size != 0u && vector_type.size <= 512u && arrays_type.size <= 512u &&
        flags_type.size <= 512u;
    TEST_EXPECT(ctx, fixture_sizes_valid);
    if (!fixture_sizes_valid)
    {
        return;
    }
    const std::size_t vector_size = static_cast<std::size_t>(vector_type.size);
    const std::size_t arrays_size = static_cast<std::size_t>(arrays_type.size);
    const std::size_t flags_size = static_cast<std::size_t>(flags_type.size);

    const std::string values = R"({"base":[2,1,4,7],"named":{"y":3},"positional":[9],
        "equal":{"x":2},"bulk":[2,1,4,7],"short":[2,1,4],"unknown":{"q":1},
        "null":[null],"excess":[1,2,3,4,5],"arrayBase":{"rows":[[4]]},
        "arrayAlternative":{"rows":[[5]]},"bits":{"visible":false},
        "namedPosition":[{"x":1}],"namedNested":{"rows":[{"x":1}]},
        "namedBits":[{"visible":false}]})";
    CBakedDocumentBlock data_block;
    CLiveDocument live;
    TEST_EXPECT(ctx, bake(values, data_block) && parse_live(values, live));
    if (!data_block.is_ready() || !live.is_ready())
    {
        return;
    }
    const auto exercise = [&](const detail::CDocumentRead& document)
    {
        const auto at = [&](const char* const name) noexcept
        {
            return document.object_child(document.root(), CStringView{ name });
        };
        alignas(128) std::uint8_t base[512], parent[512], child[512], equal[512];
        std::memset(base, 0xa5, sizeof(base));
        std::memset(parent, 0xa5, sizeof(parent));
        std::memset(child, 0xa5, sizeof(child));
        std::memset(equal, 0xa5, sizeof(equal));
        detail::SValueDiagnostic diagnostic;
        const auto construct = [&](CSchemaIndex type, detail::SOccurrence source, std::uint8_t* out,
            std::size_t size, detail::EConstructionMode mode)
        {
            return detail::construct_value(resolved, document, type, source, out, size, mode, diagnostic);
        };
        const auto alternative = [&](CSchemaIndex type, detail::SOccurrence source,
            const std::uint8_t* from, std::size_t from_size, std::uint8_t* out, std::size_t out_size)
        {
            return detail::construct_alternative(resolved, document, type, source,
                from, from_size, out, out_size, diagnostic);
        };
        const auto component = [](const std::uint8_t* bytes, const std::size_t index)
        {
            float value{};
            std::memcpy(&value, bytes + index * sizeof(float), sizeof(value));
            return value;
        };
        TEST_EXPECT(ctx, construct(vector, at("base"), base, vector_size, detail::EConstructionMode::instance));
        TEST_EXPECT(ctx, component(base, 0u) == 2.0f && component(base, 1u) == 1.0f &&
            component(base, 2u) == 4.0f && component(base, 3u) == 7.0f);
        TEST_EXPECT(ctx, alternative(vector, at("named"), base, vector_size, parent, vector_size));
        TEST_EXPECT(ctx, alternative(vector, at("positional"), parent, vector_size, child, vector_size));
        TEST_EXPECT(ctx, component(child, 0u) == 9.0f && component(child, 1u) == 3.0f &&
            component(child, 2u) == 4.0f && component(child, 3u) == 7.0f);
        TEST_EXPECT(ctx, component(base, 1u) == 1.0f && component(parent, 0u) == 2.0f &&
            component(parent, 3u) == 7.0f);
        TEST_EXPECT(ctx, alternative(vector, at("equal"), base, vector_size, equal, vector_size) &&
            std::memcmp(base, equal, vector_size) == 0);
        std::memcpy(parent, base, vector_size);
        const float changed_x = 9.0f;
        std::memcpy(parent, &changed_x, sizeof(changed_x));
        //  A new parent changed x; the retained equal selection still supplies 2.
        TEST_EXPECT(ctx, alternative(vector, at("equal"), parent, vector_size, equal, vector_size) &&
            component(parent, 0u) == 9.0f && component(equal, 0u) == 2.0f);
        TEST_EXPECT(ctx, construct(vector, at("short"), equal, vector_size, detail::EConstructionMode::instance) &&
            component(equal, 3u) == 0.0f);
        TEST_EXPECT(ctx, construct(vector, at("bulk"), equal, vector_size, detail::EConstructionMode::complete_bulk));
        TEST_EXPECT(ctx, !construct(vector, at("short"), equal, vector_size, detail::EConstructionMode::complete_bulk) &&
            diagnostic.reason == EReason::missing_property);
        TEST_EXPECT(ctx, !construct(vector, at("unknown"), equal, vector_size, detail::EConstructionMode::instance) &&
            diagnostic.reason == EReason::unknown_property);
        TEST_EXPECT(ctx, !construct(vector, at("null"), equal, vector_size, detail::EConstructionMode::instance) &&
            diagnostic.reason == EReason::invalid_input);
        TEST_EXPECT(ctx, !construct(vector, at("excess"), equal, vector_size, detail::EConstructionMode::instance) &&
            diagnostic.reason == EReason::invalid_range);
        TEST_EXPECT(ctx, !construct(vector, at("namedPosition"), equal, vector_size,
            detail::EConstructionMode::instance) && diagnostic.reason == EReason::invalid_input);
        TEST_EXPECT(ctx, !construct(vector, at("base"), equal, vector_size - 1u, detail::EConstructionMode::instance) &&
            diagnostic.reason == EReason::invalid_range);
        TEST_EXPECT(ctx, !construct(vector, at("base"), equal + 1u, vector_size, detail::EConstructionMode::instance) &&
            diagnostic.reason == EReason::invalid_range);
        std::uint8_t before[sizeof(base)];
        std::memcpy(before, base, sizeof(base));
        TEST_EXPECT(ctx, !alternative(vector, at("named"), base, vector_size, base + 4u, vector_size) &&
            diagnostic.reason == EReason::invalid_range && std::memcmp(base, before, sizeof(base)) == 0);
        TEST_EXPECT(ctx, !alternative(vector, at("named"), base, vector_size - 1u, parent, vector_size) &&
            diagnostic.reason == EReason::invalid_range);

        TEST_EXPECT(ctx, construct(arrays, at("arrayBase"), base, arrays_size, detail::EConstructionMode::instance));
        TEST_EXPECT(ctx, component(base, 0u) == 4.0f && component(base, 1u) == 0.0f &&
            component(base, 2u) == 2.0f && component(base, 3u) == 3.0f);
        TEST_EXPECT(ctx, alternative(arrays, at("arrayAlternative"), base, arrays_size,
            parent, arrays_size));
        TEST_EXPECT(ctx, component(parent, 0u) == 5.0f && component(parent, 1u) == 0.0f &&
            component(parent, 2u) == 2.0f && component(parent, 3u) == 3.0f);
        TEST_EXPECT(ctx, !construct(arrays, at("namedNested"), equal, arrays_size,
            detail::EConstructionMode::instance) && diagnostic.reason == EReason::invalid_input);

        TEST_EXPECT(ctx, construct(flags, {}, base, flags_size, detail::EConstructionMode::instance));
        base[1] |= 0x80u; //  An unaddressed bit must survive a selected-field alternative.
        TEST_EXPECT(ctx, alternative(flags, at("bits"), base, flags_size, parent, flags_size));
        TEST_EXPECT(ctx, (base[0] & 1u) == 1u && (parent[0] & 1u) == 0u &&
            (parent[0] & 0xfeu) == (base[0] & 0xfeu) && parent[1] == base[1]);
        TEST_EXPECT(ctx, !construct(flags, at("namedBits"), equal, flags_size,
            detail::EConstructionMode::instance) && diagnostic.reason == EReason::invalid_input);
    };
    exercise(detail::CDocumentRead{ data_block.document() });
    exercise(detail::CDocumentRead{ live });

    const std::string wire_schema = R"({"types":{
        "structures":{
            "Wire":{"detail":{"alignment":8,"size":24},"members":[
                {"tail":{"type":"i16","offset":8}},
                {"head":{"type":"u16","offset":0}},
                {"half":{"type":"f16","offset":4}},
                {"wide":{"type":"f64","offset":16}}]},
            "Inner":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
            "Outer":{"members":[{"inner":{"type":"Inner"}}]}},
        "bit_structures":{
            "Full":{"storage":"u64","members":[
                {"negative":{"type":"i64","mask":18446744073709551615}}]},
            "Norm":{"storage":"u16","members":[
                {"u":{"type":"u8","mask":15,"interpretation":"unorm"}},
                {"s":{"type":"i8","mask":240,"interpretation":"snorm"}}]}}}})";
    const std::string wire_values = R"({"wire":[-2,4660,1.0,1.0],"wireAlternative":[-1],
        "full":{"negative":-9223372036854775808},
        "norm":{"u":1.0,"s":-1.0},"normRaw":{"u":15,"s":-8},
        "incomplete":{"inner":{"a":1}},"complete":{"inner":{"a":1,"b":2}}})";
    CBakedDocumentBlock wire_schema_block, wire_data_block;
    CResolvedSchema wire_resolved;
    const bool wire_schema_ready = resolve(ctx, wire_schema, wire_schema_block, wire_resolved);
    TEST_EXPECT(ctx, bake(wire_values, wire_data_block));
    if (wire_schema_ready && wire_data_block.is_ready())
    {
        const detail::CDocumentRead document{ wire_data_block.document() };
        const auto at = [&](const char* const name) noexcept
        {
            return document.object_child(document.root(), CStringView{ name });
        };
        const auto type = [&](const char* const name) noexcept { return wire_resolved.find_type(CStringView{ name }); };
        detail::SValueDiagnostic diagnostic;
        alignas(128) std::uint8_t bytes[64], alternative[64];
        std::memset(bytes, 0xa5, sizeof(bytes));
        TEST_EXPECT(ctx, detail::construct_value(wire_resolved, document, type("Wire"), at("wire"),
            bytes, 24u, detail::EConstructionMode::instance, diagnostic));
        const std::uint8_t expected_wire[24] = {
            0x34u, 0x12u, 0xa5u, 0xa5u, 0x00u, 0x3cu, 0xa5u, 0xa5u,
            0xfeu, 0xffu, 0xa5u, 0xa5u, 0xa5u, 0xa5u, 0xa5u, 0xa5u,
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xf0u, 0x3fu };
        TEST_EXPECT(ctx, std::memcmp(bytes, expected_wire, sizeof(expected_wire)) == 0);
        TEST_EXPECT(ctx, detail::construct_alternative(wire_resolved, document, type("Wire"),
            at("wireAlternative"), bytes, 24u, alternative, 24u, diagnostic) &&
            alternative[8] == 0xffu && alternative[9] == 0xffu &&
            alternative[0] == 0x34u && alternative[10] == 0xa5u && bytes[8] == 0xfeu);
        TEST_EXPECT(ctx, detail::construct_value(wire_resolved, document, type("Full"), at("full"),
            bytes, 8u, detail::EConstructionMode::complete_bulk, diagnostic));
        bool full_width = bytes[7] == 0x80u;
        for (unsigned i = 0u; i < 7u; ++i) full_width &= bytes[i] == 0u;
        TEST_EXPECT(ctx, full_width);
        TEST_EXPECT(ctx, detail::construct_value(wire_resolved, document, type("Norm"), at("norm"),
            bytes, 2u, detail::EConstructionMode::complete_bulk, diagnostic) &&
            bytes[0] == 0x9fu && bytes[1] == 0u);
        TEST_EXPECT(ctx, detail::construct_value(wire_resolved, document, type("Norm"), at("normRaw"),
            bytes, 2u, detail::EConstructionMode::complete_bulk, diagnostic) && bytes[0] == 0x8fu);
        TEST_EXPECT(ctx, !detail::construct_value(wire_resolved, document, type("Outer"), at("incomplete"),
            bytes, 2u, detail::EConstructionMode::complete_bulk, diagnostic) &&
            diagnostic.reason == EReason::missing_property);
        TEST_EXPECT(ctx, detail::construct_value(wire_resolved, document, type("Outer"), at("complete"),
            bytes, 2u, detail::EConstructionMode::complete_bulk, diagnostic) &&
            bytes[0] == 1u && bytes[1] == 2u);
    }

    const std::string empty_schema = R"({"types":{"structures":{
        "Empty":{"members":[]},"Huge":{"members":[{"items":{
        "type":{"element":"Empty","count":100000000}}}]},
        "Small":{"members":[{"items":{"type":{"element":"Empty","count":2}}}]}}}})";
    CBakedDocumentBlock empty_block, empty_data;
    CResolvedSchema empty_resolved;
    if (resolve(ctx, empty_schema, empty_block, empty_resolved))
    {
        TEST_EXPECT(ctx, bake(R"({"value":{"items":[]},"bad":{"items":[null]},
            "shape":{"items":null},"excess":{"items":[{},{},{}]}})", empty_data));
        const detail::CDocumentRead document{ empty_data.document() };
        const CSchemaIndex huge = empty_resolved.find_type(CStringView{ "Huge" });
        detail::SValueDiagnostic diagnostic;
        TEST_EXPECT(ctx, detail::construct_value(empty_resolved, document, huge,
            document.object_child(document.root(), CStringView{ "value" }), nullptr, 0u,
            detail::EConstructionMode::instance, diagnostic));
        TEST_EXPECT(ctx, !detail::construct_value(empty_resolved, document, huge,
            document.object_child(document.root(), CStringView{ "bad" }), nullptr, 0u,
            detail::EConstructionMode::instance, diagnostic) && diagnostic.reason == EReason::invalid_input);
        TEST_EXPECT(ctx, !detail::construct_value(empty_resolved, document, huge,
            document.object_child(document.root(), CStringView{ "shape" }), nullptr, 0u,
            detail::EConstructionMode::instance, diagnostic) && diagnostic.reason == EReason::invalid_input);
        TEST_EXPECT(ctx, !detail::construct_value(empty_resolved, document,
            empty_resolved.find_type(CStringView{ "Small" }),
            document.object_child(document.root(), CStringView{ "excess" }), nullptr, 0u,
            detail::EConstructionMode::instance, diagnostic) && diagnostic.reason == EReason::invalid_range);
    }
}

static void test_baked_bulk(TTestContext& ctx)
{
    const std::string definitions = R"("types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Aligned":{"members":[{"value":{"type":"u16"}}]},
        "Empty":{"members":[]},
        "Nested":{"members":[{"pair":{"type":"Pair"}},
            {"values":{"type":{"element":"u8","count":2}}}]}}})";
    const std::string supplied_text = "{" + definitions + R"(,"instances":{"keep":1},"data":{
        "Pair":{"first":{"locator":{"offset":0,"valid":true,"count":2},
            "data":[{"a":1,"b":2},{"a":3,"b":4}]},
            "second":{"locator":{"offset":4,"valid":true,"size":2}}}}})";
    CBakedDocumentBlock supplied_block;
    TEST_EXPECT(ctx, bake(supplied_text, supplied_block));
    CBakedSchema baked_schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, baked_schema.set_document(supplied_block.document()) && baked_schema.resolve(schema_error));
    CBakedBulkData supplied;
    TEST_EXPECT(ctx, supplied.set_document(supplied_block.document()) && supplied.bind_schema(baked_schema));
    const CBulkDocumentQuery query = supplied.document_query();
    TEST_EXPECT(ctx, query.is_ready() && query.root() && query.name(supplied.data_root()) == CStringView{ "data" });
    TEST_EXPECT(ctx, query.object_child(query.root(), CStringView{ "instances" }));
    TEST_EXPECT(ctx, query.object_child(query.root(), CStringView{ "types" }));
    alignas(128) std::uint8_t bytes[128] = { 1u, 2u, 3u, 4u, 5u, 6u };
    SBulkDiagnostic error;
    TEST_EXPECT(ctx, supplied.load_supplied(CByteConstView{ bytes, 6u, 128u }, true, error));
    const CBulkHandle first = supplied.find_entry(CStringView{ "Pair" }, CStringView{ "first" });
    const CBulkHandle second = supplied.find_entry(CStringView{ "Pair" }, CStringView{ "second" });
    SBulkEntryView first_view, second_view;
    TEST_EXPECT(ctx, supplied.entry(first, first_view) && first_view.count == 2u &&
        first_view.stride == 2u && first_view.byte_count == 4u && first_view.bytes == bytes);
    TEST_EXPECT(ctx, supplied.entry(second, second_view) && second_view.count == 1u &&
        second_view.offset == 4u && second_view.bytes == bytes + 4u);
    TEST_EXPECT(ctx, query.parent(first) == query.object_child(supplied.data_root(), CStringView{ "Pair" }));
    bytes[0] = 9u;
    TEST_EXPECT(ctx, !supplied.load_supplied(CByteConstView{ bytes, 6u, 128u }, true, error) &&
        error.reason == EBulkLoadReason::embedded_mismatch && !supplied.loaded_ready());
    TEST_EXPECT(ctx, supplied.load_supplied(CByteConstView{ bytes, 6u, 128u }, false, error));
    TEST_EXPECT(ctx, supplied.entry(first, first_view) && first_view.bytes[0] == 9u);
    CBakedBulkData misaligned;
    TEST_EXPECT(ctx, misaligned.set_document(supplied_block.document()) && misaligned.bind_schema(baked_schema));
    TEST_EXPECT(ctx, !misaligned.load_supplied(CByteConstView{ bytes + 1u, 6u, 1u }, false, error) &&
        error.reason == EBulkLoadReason::invalid_range);
    supplied.clear();
    TEST_EXPECT(ctx, !supplied.document_ready() && !supplied.loaded_ready());

    const std::string material_text = "{" + definitions + R"(,"data":{"Pair":{
        "first":{"locator":{"offset":4294967295,"valid":false,"count":2},
            "data":[{"a":1,"b":2},{"a":3,"b":4}]},
        "second":{"locator":{"offset":0,"valid":false},"data":[{"a":5,"b":6}]}},
        "Nested":{"record":{"locator":{"offset":0,"valid":false},
            "data":[{"pair":{"a":7,"b":8},"values":[9,10]}]}}}})";
    CBakedDocumentBlock material_block;
    TEST_EXPECT(ctx, bake(material_text, material_block));
    CMutableBakedDocument mutable_document{ material_block };
    CBakedBulkData material;
    TEST_EXPECT(ctx, material.set_document(mutable_document) && material.bind_schema(baked_schema));
    CByteBuffer owner;
    TEST_EXPECT(ctx, material.materialise(owner, error) && material.loaded_ready());
    const CBulkHandle second_material = material.find_entry(CStringView{ "Pair" }, CStringView{ "second" });
    SBulkEntryView material_view;
    TEST_EXPECT(ctx, material.entry(second_material, material_view) && material_view.offset == 4u &&
        material_view.bytes[0] == 5u && material_view.bytes[1] == 6u);
    const CBulkHandle nested = material.find_entry(CStringView{ "Nested" }, CStringView{ "record" });
    TEST_EXPECT(ctx, material.entry(nested, material_view) && material_view.offset == 6u &&
        material_view.byte_count == 4u && material_view.bytes[0] == 7u &&
        material_view.bytes[1] == 8u && material_view.bytes[2] == 9u && material_view.bytes[3] == 10u);
    const CBakedDocument changed = material_block.document();
    const CBakedValueIndex locator = changed.object_child(
        changed.object_child(changed.object_child(changed.root(), CStringView{ "data" }), CStringView{ "Pair" }),
        CStringView{ "second" });
    const CBakedValueIndex locator_value = changed.object_child(locator, CStringView{ "locator" });
    std::uint64_t updated_offset{};
    bool updated_valid{};
    TEST_EXPECT(ctx, changed.unsigned_integer_value(changed.object_child(locator_value, CStringView{ "offset" }), updated_offset) &&
        changed.boolean_value(changed.object_child(locator_value, CStringView{ "valid" }), updated_valid) &&
        updated_offset == 4u && updated_valid);
    const std::uint8_t* const original_bytes = material.payload_view().data();
    CByteBuffer moved_owner{ std::move(owner) };
    TEST_EXPECT(ctx, material.payload_view().data() == original_bytes && moved_owner.const_view().data() == original_bytes);
    material.clear();
    TEST_EXPECT(ctx, moved_owner.const_view().data() == original_bytes);

    const std::string readonly_text = "{" + definitions + R"(,"data":{"Pair":{
        "ready":{"locator":{"offset":0,"valid":true,"count":1},"data":[[11,12]]}}}})";
    CBakedDocumentBlock readonly_block;
    TEST_EXPECT(ctx, bake(readonly_text, readonly_block));
    CBakedBulkData readonly_material;
    TEST_EXPECT(ctx, readonly_material.set_document(readonly_block.document()) &&
        readonly_material.bind_schema(baked_schema));
    CByteBuffer readonly_owner;
    TEST_EXPECT(ctx, readonly_material.materialise(readonly_owner, error));
    TEST_EXPECT(ctx, readonly_material.entry(readonly_material.find_entry(CStringView{ "Pair" },
        CStringView{ "ready" }), material_view) && material_view.byte_count == 2u &&
        material_view.bytes[0] == 11u && material_view.bytes[1] == 12u);
    for (std::size_t failure_offset = 0u; failure_offset < 2u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CBakedBulkData attempt;
            TEST_EXPECT(ctx, attempt.set_document(readonly_block.document()) && attempt.bind_schema(baked_schema));
            CByteBuffer output;
            failing.fail_on = failing.calls + failure_offset;
            SBulkDiagnostic allocation_error;
            TEST_EXPECT(ctx, !attempt.materialise(output, allocation_error) &&
                allocation_error.reason == EBulkLoadReason::allocation_failed &&
                !attempt.loaded_ready() && !output.is_ready());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }

    const std::string empty_text = "{" + definitions + R"(,"data":{"Empty":{
        "many":{"locator":{"offset":0,"valid":true,"count":4294967295,"size":0}},
        "missing_count":{"locator":{"offset":0,"valid":true,"size":0}}}}})";
    CBakedDocumentBlock empty_block;
    TEST_EXPECT(ctx, bake(empty_text, empty_block));
    CBakedBulkData empty_bulk;
    TEST_EXPECT(ctx, empty_bulk.set_document(empty_block.document()) && empty_bulk.bind_schema(baked_schema));
    TEST_EXPECT(ctx, !empty_bulk.load_supplied(CByteConstView{}, false, error) &&
        error.reason == EBulkLoadReason::invalid_count);
    const std::string empty_valid_text = "{" + definitions + R"(,"data":{"Empty":{
        "many":{"locator":{"offset":0,"valid":true,"count":4294967295,"size":0}}}}})";
    CBakedDocumentBlock empty_valid_block;
    TEST_EXPECT(ctx, bake(empty_valid_text, empty_valid_block));
    CBakedBulkData empty_valid;
    TEST_EXPECT(ctx, empty_valid.set_document(empty_valid_block.document()) && empty_valid.bind_schema(baked_schema));
    TEST_EXPECT(ctx, empty_valid.load_supplied(CByteConstView{}, false, error));
    TEST_EXPECT(ctx, empty_valid.entry(empty_valid.find_entry(CStringView{ "Empty" }, CStringView{ "many" }), material_view) &&
        material_view.count == UINT32_MAX && material_view.byte_count == 0u && material_view.bytes == nullptr);
    const std::string empty_embedded_text = "{" + definitions + R"(,"data":{"Empty":{
        "one":{"locator":{"offset":0,"valid":false},"data":[{}]}}}})";
    CBakedDocumentBlock empty_embedded_block;
    TEST_EXPECT(ctx, bake(empty_embedded_text, empty_embedded_block));
    CMutableBakedDocument empty_mutable{ empty_embedded_block };
    CBakedBulkData empty_material;
    TEST_EXPECT(ctx, empty_material.set_document(empty_mutable) && empty_material.bind_schema(baked_schema));
    CByteBuffer empty_owner;
    TEST_EXPECT(ctx, empty_material.materialise(empty_owner, error) && empty_material.loaded_ready());
    TEST_EXPECT(ctx, empty_material.entry(empty_material.find_entry(CStringView{ "Empty" },
        CStringView{ "one" }), material_view) && material_view.count == 1u &&
        material_view.byte_count == 0u && material_view.bytes == nullptr);

    std::string wide_values;
    for (unsigned index = 0u; index < 256u; ++index)
    {
        if (index != 0u) wide_values += ',';
        wide_values += '0';
    }
    const std::string wide_text = R"({"types":{"structures":{
        "Wide":{"members":[{"values":{"type":{"element":"u8","count":256}}}]},
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]}}},
        "data":{"Wide":{"first":{"locator":{"offset":0,"valid":true},
            "data":[[[)" + wide_values + R"(]]]}},
            "Pair":{"second":{"locator":{"offset":0,"valid":false},"data":[[1,2]]}}}})";
    CBakedDocumentBlock wide_block;
    TEST_EXPECT(ctx, bake(wide_text, wide_block));
    CBakedSchema wide_schema;
    TEST_EXPECT(ctx, wide_schema.set_document(wide_block.document()) && wide_schema.resolve(schema_error));
    CMutableBakedDocument wide_mutable{ wide_block };
    CBakedBulkData wide_bulk;
    TEST_EXPECT(ctx, wide_bulk.set_document(wide_mutable) && wide_bulk.bind_schema(wide_schema));
    CByteBuffer wide_owner;
    TEST_EXPECT(ctx, wide_bulk.materialise(wide_owner, error));
    const CBulkHandle wide_second = wide_bulk.find_entry(CStringView{ "Pair" }, CStringView{ "second" });
    TEST_EXPECT(ctx, wide_bulk.entry(wide_second, material_view) && material_view.offset == 256u &&
        material_view.bytes[0] == 1u && material_view.bytes[1] == 2u);

    const auto reject_supplied = [&](const std::string& data, const EBulkLoadReason reason)
    {
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake("{" + definitions + ",\"data\":" + data + "}", block));
        CBakedBulkData bulk;
        TEST_EXPECT(ctx, bulk.set_document(block.document()) && bulk.bind_schema(baked_schema));
        SBulkDiagnostic diagnostic;
        TEST_EXPECT(ctx, !bulk.load_supplied(CByteConstView{ bytes, 6u, 128u }, false, diagnostic) &&
            diagnostic.reason == reason && !bulk.loaded_ready());
    };
    reject_supplied(R"({"Pair":{"bad":{"data":[{"a":1,"b":2}]}}})", EBulkLoadReason::missing_property);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":1},"extra":0}}})",
        EBulkLoadReason::unknown_property);
    reject_supplied(R"({"Absent":{"bad":{"locator":{"offset":0,"valid":true,"count":1}}}})",
        EBulkLoadReason::unknown_type);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":1},
        "data":[{"a":1,"b":2},{"a":3,"b":4}]}}})", EBulkLoadReason::invalid_count);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":1,"size":3}}}})",
        EBulkLoadReason::invalid_range);
    reject_supplied(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true,"count":2}},
        "b":{"locator":{"offset":2,"valid":true,"count":1}}}})", EBulkLoadReason::overlap);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":6,"valid":true,"count":1}}}})",
        EBulkLoadReason::invalid_range);
    reject_supplied(R"({"Aligned":{"bad":{"locator":{"offset":1,"valid":true,"count":1}}}})",
        EBulkLoadReason::invalid_range);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":false,"count":1}}}})",
        EBulkLoadReason::invalid_locator);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":1},
        "data":[{"a":1}]}}})", EBulkLoadReason::invalid_input);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":1},
        "data":[{"": [1,2]}]}}})", EBulkLoadReason::invalid_input);
    reject_supplied(R"({"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":4294967295}}}})",
        EBulkLoadReason::invalid_count);
    const std::string unchecked_text = "{" + definitions + R"(,"data":{"Pair":{"partial":{
        "locator":{"offset":0,"valid":true,"count":1},"data":[[1]]}}}})";
    CBakedDocumentBlock unchecked_block;
    TEST_EXPECT(ctx, bake(unchecked_text, unchecked_block));
    CBakedBulkData unchecked_bulk;
    TEST_EXPECT(ctx, unchecked_bulk.set_document(unchecked_block.document()) &&
        unchecked_bulk.bind_schema(baked_schema));
    TEST_EXPECT(ctx, unchecked_bulk.load_supplied(CByteConstView{ bytes, 6u, 128u }, false, error));
    TEST_EXPECT(ctx, !unchecked_bulk.load_supplied(CByteConstView{ bytes, 6u, 128u }, true, error) &&
        error.reason == EBulkLoadReason::incomplete_record);

    const std::string incomplete_text = "{" + definitions + R"(,"data":{"Pair":{"bad":{
        "locator":{"offset":4294967295,"valid":false},"data":[[1]]}}}})";
    CBakedDocumentBlock incomplete_block;
    TEST_EXPECT(ctx, bake(incomplete_text, incomplete_block));
    CBakedBulkData readonly_bulk;
    TEST_EXPECT(ctx, readonly_bulk.set_document(incomplete_block.document()) && readonly_bulk.bind_schema(baked_schema));
    CByteBuffer rejected_owner;
    const bool readonly_result = readonly_bulk.materialise(rejected_owner, error);
    TEST_EXPECT(ctx, !readonly_result && error.reason == EBulkLoadReason::invalid_locator && !rejected_owner.is_ready());
    CMutableBakedDocument incomplete_mutable{ incomplete_block };
    CBakedBulkData incomplete_bulk;
    TEST_EXPECT(ctx, incomplete_bulk.set_document(incomplete_mutable) && incomplete_bulk.bind_schema(baked_schema));
    const bool incomplete_result = incomplete_bulk.materialise(rejected_owner, error);
    TEST_EXPECT(ctx, !incomplete_result && error.reason == EBulkLoadReason::incomplete_record && !rejected_owner.is_ready());
    const CBakedDocument unchanged = incomplete_block.document();
    const CBakedValueIndex bad_locator = unchanged.object_child(unchanged.object_child(
        unchanged.object_child(unchanged.object_child(unchanged.root(), CStringView{ "data" }),
            CStringView{ "Pair" }), CStringView{ "bad" }), CStringView{ "locator" });
    bool valid_after_failure{};
    TEST_EXPECT(ctx, unchanged.boolean_value(unchanged.object_child(bad_locator, CStringView{ "valid" }),
        valid_after_failure) && !valid_after_failure);

    CBakedBulkData invalidated;
    {
        CBakedSchema temporary;
        TEST_EXPECT(ctx, temporary.set_document(supplied_block.document()) && temporary.resolve(schema_error));
        TEST_EXPECT(ctx, invalidated.set_document(supplied_block.document()) && invalidated.bind_schema(temporary));
        TEST_EXPECT(ctx, invalidated.load_supplied(CByteConstView{ bytes, 6u, 128u }, false, error));
    }
    TEST_EXPECT(ctx, !invalidated.loaded_ready() && !invalidated.find_entry(CStringView{ "Pair" }, CStringView{ "first" }));
    invalidated.clear();

    CLiveDocument parsed_schema;
    TEST_EXPECT(ctx, parse_live(supplied_text, parsed_schema));
    CLiveSchema live_schema;
    TEST_EXPECT(ctx, live_schema.try_adopt(std::move(parsed_schema)) && live_schema.resolve(schema_error));
    CBakedBulkData live_bound;
    TEST_EXPECT(ctx, live_bound.set_document(supplied_block.document()) && live_bound.bind_schema(live_schema));
    TEST_EXPECT(ctx, live_bound.load_supplied(CByteConstView{ bytes, 6u, 128u }, false, error));
}

static void test_live_instances(TTestContext& ctx)
{
    static_assert(!std::is_copy_constructible_v<CLiveInstances> && !std::is_move_constructible_v<CLiveInstances>);
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Bag":{"members":[{"rows":{"type":{"element":"Pair","count":2}}}]},
        "FloatPair":{"members":[{"f":{"type":"f32"}},{"flag":{"type":"b8"}}]},
        "EnumHolder":{"members":[{"mode":{"type":"Mode"}}]},
        "Empty":{"members":[]}},"enumerations":{"Mode":{"storage":"u8",
        "values":{"first":2,"alias":2}}},"bit_structures":{"Bits":{"storage":"u8",
        "members":[{"lo":{"type":"u8","mask":15}},{"hi":{"type":"u8","mask":240}}]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    CBakedDocumentBlock source_block;
    TEST_EXPECT(ctx, bake(R"({"instances":{"Pair":{"base":{"locator":{"offset":0,"valid":true,"count":1},
        "declaration":{"a":1,"b":2},"specialisation":{"child":{"locator":{"offset":2,"valid":true},
        "declaration":{"a":3}}}}}}})", source_block));
    alignas(128) std::uint8_t source_bytes[128] = { 1u, 2u, 9u, 2u };
    CBakedInstances source;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, source.set_document(source_block.document()) && source.bind_schema(schema) &&
        source.load_supplied(CByteConstView{ source_bytes, 4u, 128u }, false, error));
    CLiveInstances live;
    TEST_EXPECT(ctx, source.promote(live, schema, error) && live.loaded_ready() && live.document_ready());
    const CInstanceHandle base = live.find_base(CStringView{ "Pair" }, CStringView{ "base" });
    const CInstanceHandle child = live.find_specialisation(base, CStringView{ "child" });
    SInstanceEntryView base_view, child_view;
    TEST_EXPECT(ctx, live.entry(base, base_view) && live.entry(child, child_view) &&
        base_view.bytes[0] == 1u && child_view.bytes[0] == 9u &&
        base_view.bytes != source_bytes && child_view.parent == base);
    const CInstanceDocumentQuery live_query = live.document_query();
    const CInstanceHandle locator = live_query.object_child(base, CStringView{ "locator" });
    TEST_EXPECT(ctx, !live_query.object_child(locator, CStringView{ "count" }));
    source_bytes[0] = 77u;
    TEST_EXPECT(ctx, live.entry(base, base_view) && base_view.bytes[0] == 1u);

    CBakedDocumentBlock incompatible_block;
    TEST_EXPECT(ctx, bake(R"({"types":{"structures":{"Pair":{"members":[
        {"a":{"type":"u8"}},{"b":{"type":"u8","default":8}}]}}}})", incompatible_block));
    CBakedSchema incompatible;
    TEST_EXPECT(ctx, incompatible.set_document(incompatible_block.document()) && incompatible.resolve(schema_error));
    CLiveInstances rejected;
    TEST_EXPECT(ctx, !source.promote(rejected, incompatible, error) &&
        error.reason == EInstanceLoadReason::incompatible_schema && !rejected.document_ready() && source.loaded_ready());

    alignas(128) std::uint8_t new_base[128] = { 5u, 6u };
    TEST_EXPECT(ctx, live.capture_base(CStringView{ "Pair" }, CStringView{ "base" },
        CByteConstView{ new_base, 2u, 128u }, error) == base);
    TEST_EXPECT(ctx, live.entry(base, base_view) && live.entry(child, child_view) &&
        base_view.bytes[0] == 5u && base_view.bytes[1] == 6u &&
        child_view.bytes[0] == 9u && child_view.bytes[1] == 6u);
    const CInstanceHandle child_declaration = live.entry(child, child_view) ? child_view.declaration : CInstanceHandle{};
    std::uint64_t canonical_a{};
    TEST_EXPECT(ctx, live_query.unsigned_integer_value(
        live_query.object_child(child_declaration, CStringView{ "a" }), canonical_a) && canonical_a == 9u);

    CLiveDocument values;
    TEST_EXPECT(ctx, parse_live(R"({"seven":7,"six":6,"just_b":{"b":4},"none":{},
        "low":{"lo":3},"base":{"rows":[[1,2],[3,4]]},
        "override":{"rows":[[5,6],[7,8]]},"empty_rows":{"rows":[]}})", values));
    const CInstanceDocumentQuery values_query{ values };
    const CInstanceHandle seven = values_query.object_child(values_query.root(), CStringView{ "seven" });
    const SInstanceSelectionStep b_step{ EInstanceSelectionStepKind::member, CStringView{ "b" }, 0u };

    CBakedDocumentBlock stale_block;
    TEST_EXPECT(ctx, bake(R"({"instances":{"Pair":{"base":{"locator":{"offset":0,"valid":true},
        "declaration":{"a":1,"b":2},"specialisation":{"child":{"locator":{"offset":2,"valid":true},
        "declaration":{"a":999}}}}}}})", stale_block));
    alignas(128) std::uint8_t stale_bytes[128] = { 1u, 2u, 9u, 2u };
    CBakedInstances stale_source;
    CLiveInstances stale_live;
    TEST_EXPECT(ctx, stale_source.set_document(stale_block.document()) && stale_source.bind_schema(schema) &&
        stale_source.load_supplied(CByteConstView{ stale_bytes, 4u, 128u }, false, error) &&
        stale_source.promote(stale_live, schema, error));
    const CInstanceHandle stale_base = stale_live.find_base(CStringView{ "Pair" }, CStringView{ "base" });
    const CInstanceHandle stale_child = stale_live.find_specialisation(stale_base, CStringView{ "child" });
    TEST_EXPECT(ctx, stale_live.set_selection(stale_child, &b_step, 1u, values_query, seven, error));
    TEST_EXPECT(ctx, stale_live.entry(stale_child, child_view) && child_view.bytes[0] == 9u && child_view.bytes[1] == 7u);
    TEST_EXPECT(ctx, stale_live.remove_selection(stale_child, &b_step, 1u, error));
    TEST_EXPECT(ctx, stale_live.entry(stale_child, child_view) && child_view.bytes[0] == 9u && child_view.bytes[1] == 2u);

    TEST_EXPECT(ctx, live.set_selection(child, &b_step, 1u, values_query, seven, error));
    TEST_EXPECT(ctx, live.entry(child, child_view) && child_view.bytes[0] == 9u && child_view.bytes[1] == 7u);
    TEST_EXPECT(ctx, live.remove_selection(child, &b_step, 1u, error));
    TEST_EXPECT(ctx, live.entry(child, child_view) && child_view.bytes[0] == 9u && child_view.bytes[1] == 6u);
    alignas(128) std::uint8_t captured_child[128] = { 11u, 99u };
    TEST_EXPECT(ctx, live.capture_specialisation(child, CByteConstView{ captured_child, 2u, 128u }, error));
    TEST_EXPECT(ctx, live.entry(child, child_view) && child_view.bytes[0] == 11u && child_view.bytes[1] == 6u);
    const CInstanceHandle six = values_query.object_child(values_query.root(), CStringView{ "six" });
    TEST_EXPECT(ctx, live.set_selection(child, &b_step, 1u, values_query, six, error));
    TEST_EXPECT(ctx, live.entry(child, child_view));
    const CInstanceDocumentQuery equal_query = live.document_query();
    std::uint64_t selected_equal{};
    TEST_EXPECT(ctx, equal_query.unsigned_integer_value(
        equal_query.object_child(child_view.declaration, CStringView{ "b" }), selected_equal) &&
        selected_equal == 6u && child_view.bytes[1] == 6u);
    TEST_EXPECT(ctx, live.remove_selection(child, &b_step, 1u, error));
    TEST_EXPECT(ctx, live.entry(child, child_view));

    const CInstanceDocumentQuery self_query = live.document_query();
    const CInstanceHandle group = self_query.parent(base);
    const CInstanceHandle self_created = live.create_base(self_query.name(group), self_query.name(child),
        self_query, child_view.declaration, error);
    SInstanceEntryView self_view;
    TEST_EXPECT(ctx, self_created && live.entry(self_created, self_view) &&
        self_view.bytes[0] == 11u && self_view.bytes[1] == 0u);
    TEST_EXPECT(ctx, live.entry(child, child_view));
    const CInstanceHandle self_capture = live.capture_base(CStringView{ "Pair" }, CStringView{ "snapshot" },
        CByteConstView{ child_view.bytes, 2u, 1u }, error);
    TEST_EXPECT(ctx, self_capture && live.entry(self_capture, self_view) &&
        self_view.bytes[0] == 11u && self_view.bytes[1] == 6u);

    const CInstanceHandle bag = live.create_base(CStringView{ "Bag" }, CStringView{ "bag" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "base" }), error);
    const CInstanceHandle bag_child = live.create_specialisation(bag, CStringView{ "variant" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "override" }), error);
    TEST_EXPECT(ctx, bag && bag_child);
    const SInstanceSelectionStep path[] = {
        { EInstanceSelectionStepKind::member, CStringView{ "rows" }, 0u },
        { EInstanceSelectionStepKind::element, {}, 0u },
        { EInstanceSelectionStepKind::member, CStringView{ "b" }, 0u }
    };
    TEST_EXPECT(ctx, live.set_selection(bag_child, path, 3u, values_query, seven, error));
    SInstanceEntryView bag_view;
    TEST_EXPECT(ctx, live.entry(bag_child, bag_view) && bag_view.byte_count == 4u &&
        bag_view.bytes[0] == 5u && bag_view.bytes[1] == 7u &&
        bag_view.bytes[2] == 7u && bag_view.bytes[3] == 8u);
    TEST_EXPECT(ctx, live.remove_selection(bag_child, path, 3u, error));
    TEST_EXPECT(ctx, live.entry(bag_child, bag_view) && bag_view.bytes[0] == 5u &&
        bag_view.bytes[1] == 2u && bag_view.bytes[2] == 7u && bag_view.bytes[3] == 8u);
    const SInstanceSelectionStep interior[] = {
        { EInstanceSelectionStepKind::member, CStringView{ "rows" }, 0u },
        { EInstanceSelectionStepKind::element, {}, 0u }
    };
    TEST_EXPECT(ctx, !live.remove_selection(bag_child, interior, 2u, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration && live.entry(bag_child, bag_view) &&
        bag_view.bytes[0] == 5u && bag_view.bytes[2] == 7u);
    const CInstanceHandle empty_rows = live.create_specialisation(bag, CStringView{ "empty" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "empty_rows" }), error);
    const SInstanceSelectionStep skipped[] = {
        { EInstanceSelectionStepKind::member, CStringView{ "rows" }, 0u },
        { EInstanceSelectionStepKind::element, {}, 1u }
    };
    const CInstanceHandle pair_literal = values_query.array_at(values_query.object_child(
        values_query.object_child(values_query.root(), CStringView{ "base" }), CStringView{ "rows" }), 0u);
    TEST_EXPECT(ctx, empty_rows && !live.set_selection(empty_rows, skipped, 2u, values_query, pair_literal, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration);

    const CInstanceHandle grand = live.create_specialisation(child, CStringView{ "grand" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "just_b" }), error);
    TEST_EXPECT(ctx, grand);
    alignas(128) std::uint8_t deep_base[128] = { 12u, 13u };
    TEST_EXPECT(ctx, live.capture_base(CStringView{ "Pair" }, CStringView{ "base" },
        CByteConstView{ deep_base, 2u, 128u }, error) == base);
    SInstanceEntryView grand_view;
    TEST_EXPECT(ctx, live.entry(child, child_view) && live.entry(grand, grand_view) &&
        child_view.bytes[0] == 11u && child_view.bytes[1] == 13u &&
        grand_view.bytes[0] == 11u && grand_view.bytes[1] == 4u);

    alignas(128) std::uint8_t bits_base[128] = { 0xa1u };
    const CInstanceHandle bits = live.capture_base(CStringView{ "Bits" }, CStringView{ "base" },
        CByteConstView{ bits_base, 1u, 128u }, error);
    const CInstanceHandle bits_child = live.create_specialisation(bits, CStringView{ "child" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "low" }), error);
    alignas(128) std::uint8_t bits_override[128] = { 0xf5u };
    TEST_EXPECT(ctx, bits && bits_child && live.capture_specialisation(bits_child,
        CByteConstView{ bits_override, 1u, 128u }, error));
    bits_base[0] = 0xb1u;
    TEST_EXPECT(ctx, live.capture_base(CStringView{ "Bits" }, CStringView{ "base" },
        CByteConstView{ bits_base, 1u, 128u }, error) == bits);
    SInstanceEntryView bits_view;
    TEST_EXPECT(ctx, live.entry(bits_child, bits_view) && bits_view.bytes[0] == 0xb5u);
    const SInstanceSelectionStep hi_step{ EInstanceSelectionStepKind::member, CStringView{ "hi" }, 0u };
    TEST_EXPECT(ctx, live.set_selection(bits_child, &hi_step, 1u, values_query, seven, error));
    TEST_EXPECT(ctx, live.entry(bits_child, bits_view) && bits_view.bytes[0] == 0x75u);
    TEST_EXPECT(ctx, live.remove_selection(bits_child, &hi_step, 1u, error));
    TEST_EXPECT(ctx, live.entry(bits_child, bits_view) && bits_view.bytes[0] == 0xb5u);

    CLiveDocument float_values;
    TEST_EXPECT(ctx, parse_live(R"({"selected":{"f":"nan","flag":true}})", float_values));
    const CInstanceDocumentQuery float_query{ float_values };
    alignas(128) std::uint8_t float_base[128] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    const CInstanceHandle fbase = live.capture_base(CStringView{ "FloatPair" }, CStringView{ "base" },
        CByteConstView{ float_base, 8u, 128u }, error);
    const CInstanceHandle fchild = live.create_specialisation(fbase, CStringView{ "child" }, float_query,
        float_query.object_child(float_query.root(), CStringView{ "selected" }), error);
    TEST_EXPECT(ctx, fbase && fchild);
    alignas(128) std::uint8_t unusual[128] = { 0x45u, 0x23u, 0xc1u, 0x7fu, 0x7fu, 0u, 0u, 0u };
    TEST_EXPECT(ctx, live.capture_specialisation(fchild, CByteConstView{ unusual, 8u, 128u }, error));
    float_base[0] = 0u;
    float_base[1] = 0u;
    float_base[2] = 0x80u;
    float_base[3] = 0x3fu;
    TEST_EXPECT(ctx, live.capture_base(CStringView{ "FloatPair" }, CStringView{ "base" },
        CByteConstView{ float_base, 8u, 128u }, error) == fbase);
    SInstanceEntryView float_view;
    TEST_EXPECT(ctx, live.entry(fchild, float_view) &&
        std::memcmp(float_view.bytes, unusual, 5u) == 0);

    alignas(128) std::uint8_t unknown_enum[128] = { 5u };
    TEST_EXPECT(ctx, !live.capture_base(CStringView{ "EnumHolder" }, CStringView{ "bad" },
        CByteConstView{ unknown_enum, 1u, 128u }, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration &&
        !live.find_base(CStringView{ "EnumHolder" }, CStringView{ "bad" }));
    unknown_enum[0] = 2u;
    const CInstanceHandle enum_instance = live.capture_base(CStringView{ "EnumHolder" }, CStringView{ "good" },
        CByteConstView{ unknown_enum, 1u, 128u }, error);
    SInstanceEntryView enum_view;
    TEST_EXPECT(ctx, enum_instance && live.entry(enum_instance, enum_view));
    const CInstanceDocumentQuery enum_query = live.document_query();
    TEST_EXPECT(ctx, enum_query.string_value(enum_query.object_child(enum_view.declaration, CStringView{ "mode" })) ==
        CStringView{ "first" });

    const CInstanceHandle empty = live.capture_base(CStringView{ "Empty" }, CStringView{ "zero" }, {}, error);
    TEST_EXPECT(ctx, empty && live.entry(empty, bag_view) && bag_view.byte_count == 0u &&
        bag_view.bytes == nullptr && bag_view.declaration);
    const CInstanceHandle empty_child = live.create_specialisation(empty, CStringView{ "same" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "none" }), error);
    TEST_EXPECT(ctx, empty_child && live.entry(empty_child, bag_view) && bag_view.byte_count == 0u &&
        bag_view.declaration && live.capture_specialisation(empty_child, {}, error));
}

static void test_live_instance_output(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Bag":{"members":[{"rows":{"type":{"element":"Pair","count":2}}}]},
        "Container":{"members":[{"pair":{"type":"Pair"}},{"tail":{"type":"u8"}}]},
        "Inner":{"members":[{"b":{"type":"u8"}},{"c":{"type":"u8"}}]},
        "Outer":{"members":[{"a":{"type":"u8"}},{"b":{"type":"Inner"}}]},
        "NestedBag":{"members":[{"rows":{"type":{"element":"Outer","count":1}}}]},
        "BitBag":{"members":[{"rows":{"type":{"element":"Bits","count":1}}}]},
        "ScalarBag":{"members":[{"rows":{"type":{"element":"u8","count":1}}}]},
        "Empty":{"members":[]}},"bit_structures":{"Bits":{"storage":"u8",
            "members":[{"lo":{"type":"u8","mask":15}},{"hi":{"type":"u8","mask":240}}]}}}})";
    CBakedDocumentBlock schema_block;
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, bake(definitions, schema_block) && schema.set_document(schema_block.document()) &&
        schema.resolve(schema_error));
    CLiveDocument values;
    TEST_EXPECT(ctx, parse_live(R"({"base":{"rows":[{"a":1,"b":2}]},
        "selection":{"rows":[{"b":9}]},"first":{"rows":[{"a":6}]},
        "equal":{"rows":[{"b":2}]},"empty":{"rows":[]},
        "other":{"a":4,"b":5}})", values));
    const CInstanceDocumentQuery values_query{ values };
    CLiveInstances live;
    TEST_EXPECT(ctx, live.initialise(schema));
    SInstanceDiagnostic error;
    const CInstanceHandle bag = live.create_base(CStringView{ "Bag" }, CStringView{ "bag" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "base" }), error);
    const CInstanceHandle variant = live.create_specialisation(bag, CStringView{ "variant" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "selection" }), error);
    const CInstanceHandle first = live.create_specialisation(bag, CStringView{ "first" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "first" }), error);
    const CInstanceHandle equal = live.create_specialisation(bag, CStringView{ "equal" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "equal" }), error);
    const CInstanceHandle empty_selection = live.create_specialisation(bag, CStringView{ "empty" }, values_query,
        values_query.object_child(values_query.root(), CStringView{ "empty" }), error);
    SInstanceEntryView view;
    TEST_EXPECT(ctx, bag && variant && first && equal && empty_selection &&
        live.entry(variant, view) && view.byte_count == 4u &&
        view.bytes[0] == 1u && view.bytes[1] == 9u);

    const auto exercise = [&](const EDataOutputForm form)
    {
        CLiveDocument prepared;
        CByteBuffer payload;
        TEST_EXPECT(ctx, live.prepare_output(prepared, payload, schema, form, error));
        if (!prepared.is_ready())
        {
            return;
        }
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, document_translation::bake(prepared, block));
        const CDocumentWriteResult written = document_writer::write(block.document());
        TEST_EXPECT(ctx, written.report.succeeded());
        CDocumentWriteOptions strict_options;
        strict_options.mode = EDocumentWriteMode::strict_json;
        const CDocumentWriteResult strict_written = document_writer::write(block.document(), strict_options);
        TEST_EXPECT(ctx, strict_written.report.succeeded());
        const std::string text{ reinterpret_cast<const char*>(written.output.data()),
            written.report.logical_text_byte_size };
        const std::string strict_text{ reinterpret_cast<const char*>(strict_written.output.data()),
            strict_written.report.logical_text_byte_size };
        CBakedDocumentBlock reparsed;
        TEST_EXPECT(ctx, bake(text, reparsed));
        CBakedDocumentBlock strict_reparsed;
        TEST_EXPECT(ctx, bake(strict_text, strict_reparsed));
        CBakedInstances reload;
        TEST_EXPECT(ctx, reload.set_document(reparsed.document()) && reload.bind_schema(schema) &&
            reload.load_supplied(payload.const_view(), form == EDataOutputForm::embedded, error));
        CBakedInstances strict_reload;
        TEST_EXPECT(ctx, strict_reload.set_document(strict_reparsed.document()) &&
            strict_reload.bind_schema(schema) &&
            strict_reload.load_supplied(payload.const_view(), form == EDataOutputForm::embedded, error));
        if (form == EDataOutputForm::embedded)
        {
            CBakedInstances material;
            CByteBuffer material_owner;
            TEST_EXPECT(ctx, material.set_document(reparsed.document()) && material.bind_schema(schema) &&
                material.materialise(material_owner, error));
            SInstanceEntryView material_view;
            const CInstanceHandle material_base = material.find_base(CStringView{ "Bag" }, CStringView{ "bag" });
            TEST_EXPECT(ctx, material.entry(material.find_specialisation(material_base,
                CStringView{ "variant" }), material_view) &&
                material_view.bytes[0] == 1u && material_view.bytes[1] == 9u);
        }
        CLiveInstances promoted;
        TEST_EXPECT(ctx, reload.promote(promoted, schema, error));
        const CInstanceHandle reloaded_base = promoted.find_base(CStringView{ "Bag" }, CStringView{ "bag" });
        const CInstanceHandle reloaded_child = promoted.find_specialisation(reloaded_base, CStringView{ "variant" });
        const CInstanceHandle reloaded_first = promoted.find_specialisation(reloaded_base, CStringView{ "first" });
        const CInstanceHandle reloaded_equal = promoted.find_specialisation(reloaded_base, CStringView{ "equal" });
        const CInstanceHandle reloaded_empty = promoted.find_specialisation(reloaded_base, CStringView{ "empty" });
        alignas(128) std::uint8_t changed[128] = { 7u, 8u, 0u, 0u };
        TEST_EXPECT(ctx, promoted.capture_base(CStringView{ "Bag" }, CStringView{ "bag" },
            CByteConstView{ changed, 4u, 128u }, error) == reloaded_base);
        SInstanceEntryView reloaded_view;
        TEST_EXPECT(ctx, promoted.entry(reloaded_child, reloaded_view) &&
            reloaded_view.bytes[0] == 7u && reloaded_view.bytes[1] == 9u);
        TEST_EXPECT(ctx, promoted.entry(reloaded_first, reloaded_view) &&
            reloaded_view.bytes[0] == 6u && reloaded_view.bytes[1] == 8u);
        TEST_EXPECT(ctx, promoted.entry(reloaded_equal, reloaded_view) &&
            reloaded_view.bytes[0] == 7u && reloaded_view.bytes[1] == 2u);
        const CInstanceDocumentQuery promoted_query = promoted.document_query();
        TEST_EXPECT(ctx, promoted.entry(reloaded_empty, reloaded_view) &&
            reloaded_view.bytes[0] == 7u && reloaded_view.bytes[1] == 8u &&
            promoted_query.value_kind(promoted_query.object_child(reloaded_view.declaration,
                CStringView{ "rows" })) == EDocumentValueKind::array &&
            promoted_query.child_count(promoted_query.object_child(reloaded_view.declaration,
                CStringView{ "rows" })) == 0u);
    };
    exercise(EDataOutputForm::embedded);
    exercise(EDataOutputForm::external);

    CLiveDocument compound_values;
    TEST_EXPECT(ctx, parse_live(R"({"base":{"rows":[{"a":1,"b":{"b":2,"c":3}}]},
        "nested":{"rows":[{"b":{"c":9}}]},"positional":{"rows":[{"b":[7]}]},
        "bits":{"rows":[{"lo":1,"hi":2}]},"bit_child":{"rows":[{"hi":3}]},
        "invalid":{"rows":[{"x":9}]},"unknown":{"rows":[{"q":9}]},
        "positional_compound":[{"b":9}]})", compound_values));
    const CInstanceDocumentQuery compound_query{ compound_values };
    const auto compound_at = [&](const char* const name)
    {
        return compound_query.object_child(compound_query.root(), CStringView{ name });
    };
    const CInstanceHandle nested_base = live.create_base(CStringView{ "NestedBag" }, CStringView{ "base" },
        compound_query, compound_at("base"), error);
    const CInstanceHandle nested_child = live.create_specialisation(nested_base, CStringView{ "nested" },
        compound_query, compound_at("nested"), error);
    const CInstanceHandle positional_child = live.create_specialisation(nested_base, CStringView{ "positional" },
        compound_query, compound_at("positional"), error);
    TEST_EXPECT(ctx, nested_base && nested_child && positional_child &&
        live.entry(nested_child, view) && view.bytes[0] == 1u && view.bytes[1] == 2u && view.bytes[2] == 9u &&
        live.entry(positional_child, view) && view.bytes[0] == 1u && view.bytes[1] == 7u && view.bytes[2] == 3u);
    CLiveDocument scalar_value;
    TEST_EXPECT(ctx, parse_live(R"({"value":11})", scalar_value));
    const CInstanceDocumentQuery scalar_query{ scalar_value };
    const SInstanceSelectionStep nested_path[] = {
        { EInstanceSelectionStepKind::member, CStringView{ "rows" }, 0u },
        { EInstanceSelectionStepKind::element, {}, 0u },
        { EInstanceSelectionStepKind::member, CStringView{ "b" }, 0u },
        { EInstanceSelectionStepKind::member, CStringView{ "b" }, 0u }
    };
    TEST_EXPECT(ctx, live.set_selection(nested_child, nested_path, 4u, scalar_query,
        scalar_query.object_child(scalar_query.root(), CStringView{ "value" }), error) &&
        live.entry(nested_child, view) && view.bytes[1] == 11u && view.bytes[2] == 9u);
    TEST_EXPECT(ctx, live.remove_selection(nested_child, nested_path, 4u, error) &&
        live.entry(nested_child, view) && view.bytes[1] == 2u && view.bytes[2] == 9u);
    alignas(128) std::uint8_t captured_positional[128] = { 8u, 10u, 11u };
    TEST_EXPECT(ctx, live.capture_specialisation(positional_child,
        CByteConstView{ captured_positional, 3u, 128u }, error) &&
        live.entry(positional_child, view) && view.bytes[0] == 1u && view.bytes[1] == 10u && view.bytes[2] == 3u);
    alignas(128) std::uint8_t changed_nested[128] = { 4u, 5u, 6u };
    TEST_EXPECT(ctx, live.capture_base(CStringView{ "NestedBag" }, CStringView{ "base" },
        CByteConstView{ changed_nested, 3u, 128u }, error) == nested_base &&
        live.entry(nested_child, view) && view.bytes[0] == 4u && view.bytes[1] == 5u && view.bytes[2] == 9u &&
        live.entry(positional_child, view) && view.bytes[0] == 4u && view.bytes[1] == 10u && view.bytes[2] == 6u);
    const CInstanceHandle bit_base = live.create_base(CStringView{ "BitBag" }, CStringView{ "base" },
        compound_query, compound_at("bits"), error);
    const CInstanceHandle bit_child = live.create_specialisation(bit_base, CStringView{ "child" },
        compound_query, compound_at("bit_child"), error);
    alignas(128) std::uint8_t changed_bits[128] = { 0x45u };
    TEST_EXPECT(ctx, bit_base && bit_child && live.capture_base(CStringView{ "BitBag" }, CStringView{ "base" },
        CByteConstView{ changed_bits, 1u, 128u }, error) == bit_base &&
        live.entry(bit_child, view) && view.bytes[0] == 0x35u);
    TEST_EXPECT(ctx, !live.create_base(CStringView{ "ScalarBag" }, CStringView{ "invalid" },
        compound_query, compound_at("invalid"), error) &&
        error.reason == EInstanceLoadReason::invalid_declaration);
    TEST_EXPECT(ctx, !live.create_base(CStringView{ "NestedBag" }, CStringView{ "unknown" },
        compound_query, compound_at("unknown"), error) &&
        error.reason == EInstanceLoadReason::invalid_declaration);
    const CInstanceHandle positional_compound = live.create_base(CStringView{ "Container" },
        CStringView{ "positional" }, compound_query, compound_at("positional_compound"), error);
    TEST_EXPECT(ctx, positional_compound && live.entry(positional_compound, view) &&
        view.bytes[0] == 0u && view.bytes[1] == 9u && view.bytes[2] == 0u);
}

static void test_live_instance_output_boundaries(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Empty":{"members":[]},
        "Odd":{"members":[{"value":{"type":"u8"}}]},
        "Wide":{"detail":{"alignment":4,"size":4},
            "members":[{"value":{"type":"u32"}}]}}}})";
    CBakedDocumentBlock schema_block;
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, bake(definitions, schema_block) && schema.set_document(schema_block.document()) &&
        schema.resolve(schema_error));
    CLiveInstances order;
    SInstanceDiagnostic error;
    alignas(128) std::uint8_t first_bytes[128] = { 1u, 2u };
    alignas(128) std::uint8_t second_bytes[128] = { 3u, 4u };
    const CInstanceHandle first = order.initialise(schema) ? order.capture_base(CStringView{ "Pair" },
        CStringView{ "first" }, CByteConstView{ first_bytes, 2u, 128u }, error) : CInstanceHandle{};
    const CInstanceHandle second = order.capture_base(CStringView{ "Pair" }, CStringView{ "second" },
        CByteConstView{ second_bytes, 2u, 128u }, error);
    CLiveDocument values;
    TEST_EXPECT(ctx, parse_live(R"({"selection":{"b":9}})", values));
    const CInstanceDocumentQuery query{ values };
    const CInstanceHandle child = order.create_specialisation(first, CStringView{ "same" }, query,
        query.object_child(query.root(), CStringView{ "selection" }), error);
    const CInstanceHandle other_child = order.create_specialisation(second, CStringView{ "same" }, query,
        query.object_child(query.root(), CStringView{ "selection" }), error);
    const CInstanceHandle empty = order.capture_base(CStringView{ "Empty" }, CStringView{ "zero" }, {}, error);
    alignas(128) std::uint8_t odd_bytes[128] = { 5u };
    alignas(128) std::uint8_t wide_bytes[128] = { 6u, 0u, 0u, 0u };
    const CInstanceHandle odd = order.capture_base(CStringView{ "Odd" }, CStringView{ "odd" },
        CByteConstView{ odd_bytes, 1u, 128u }, error);
    const CInstanceHandle wide = order.capture_base(CStringView{ "Wide" }, CStringView{ "wide" },
        CByteConstView{ wide_bytes, 4u, 128u }, error);
    TEST_EXPECT(ctx, first && second && child && other_child && empty && odd && wide);
    CBakedDocumentBlock block;
    CByteBuffer payload;
    CBakedInstances demoted;
    TEST_EXPECT(ctx, order.demote(block, payload, demoted, schema, EDataOutputForm::embedded, error));
    const CInstanceHandle baked_first = demoted.find_base(CStringView{ "Pair" }, CStringView{ "first" });
    const CInstanceHandle baked_second = demoted.find_base(CStringView{ "Pair" }, CStringView{ "second" });
    SInstanceEntryView view;
    TEST_EXPECT(ctx, demoted.entry(baked_first, view) && view.offset == 0u &&
        demoted.entry(demoted.find_specialisation(baked_first, CStringView{ "same" }), view) &&
        view.offset == 2u && view.bytes[1] == 9u &&
        demoted.entry(baked_second, view) && view.offset == 4u &&
        demoted.entry(demoted.find_base(CStringView{ "Empty" }, CStringView{ "zero" }), view) &&
        view.offset == 0u && view.byte_count == 0u && view.bytes == nullptr &&
        demoted.entry(demoted.find_base(CStringView{ "Odd" }, CStringView{ "odd" }), view) &&
        view.offset == 8u &&
        demoted.entry(demoted.find_base(CStringView{ "Wide" }, CStringView{ "wide" }), view) &&
        view.offset == 12u && payload.size() == 16u);
    const std::uint8_t* const payload_data = payload.data();
    order.clear();
    CByteBuffer moved_payload{ std::move(payload) };
    CBakedDocumentBlock moved_block{ std::move(block) };
    TEST_EXPECT(ctx, demoted.loaded_ready() && demoted.payload_view().data() == payload_data &&
        demoted.entry(baked_first, view) && view.bytes[0] == 1u &&
        moved_payload.data() == payload_data && moved_block.is_ready());
    CBakedDocumentBlock legacy_empty_block;
    TEST_EXPECT(ctx, bake(R"({"instances":{"Pair":{"one":{"locator":{"offset":0,"valid":true}}},
        "Empty":{"zero":{"locator":{"offset":2,"valid":true}}}}})", legacy_empty_block));
    CBakedInstances legacy_empty;
    TEST_EXPECT(ctx, legacy_empty.set_document(legacy_empty_block.document()) && legacy_empty.bind_schema(schema) &&
        legacy_empty.load_supplied(CByteConstView{ first_bytes, 2u, 128u }, false, error));

    const std::string stale_text = R"({"instances":{"Pair":{"base":{
        "locator":{"offset":0,"valid":true},"declaration":{"a":1,"b":2},
        "specialisation":{"child":{"locator":{"offset":2,"valid":true},
            "declaration":{"b":4}}}}}}})";
    CBakedDocumentBlock stale_block;
    TEST_EXPECT(ctx, bake(stale_text, stale_block));
    alignas(128) std::uint8_t stale_bytes[128] = { 1u, 2u, 1u, 9u };
    CBakedInstances stale;
    CLiveInstances stale_live;
    TEST_EXPECT(ctx, stale.set_document(stale_block.document()) && stale.bind_schema(schema) &&
        stale.load_supplied(CByteConstView{ stale_bytes, 4u, 128u }, false, error) &&
        stale.promote(stale_live, schema, error));
    CLiveDocument embedded;
    CByteBuffer embedded_payload;
    TEST_EXPECT(ctx, stale_live.prepare_output(embedded, embedded_payload, schema,
        EDataOutputForm::embedded, error));
    const CNodeKey instances = embedded.object_child(embedded.root(), CStringView{ "instances" });
    const CNodeKey group = embedded.object_child(instances, CStringView{ "Pair" });
    const CNodeKey base = embedded.object_child(group, CStringView{ "base" });
    const CNodeKey specialisations = embedded.object_child(base, CStringView{ "specialisation" });
    const CNodeKey output_child = embedded.object_child(specialisations, CStringView{ "child" });
    const CNodeKey declaration = embedded.object_child(output_child, CStringView{ "declaration" });
    std::uint64_t selected{};
    TEST_EXPECT(ctx, embedded.unsigned_integer_value(embedded.object_child(declaration, CStringView{ "b" }),
        selected) && selected == 9u && !embedded.object_child(declaration, CStringView{ "a" }));
    alignas(128) std::uint8_t child_mismatch_bytes[128] = { 1u, 2u, 7u, 9u };
    CBakedInstances child_mismatch;
    CLiveInstances child_mismatch_live;
    TEST_EXPECT(ctx, child_mismatch.set_document(stale_block.document()) && child_mismatch.bind_schema(schema) &&
        child_mismatch.load_supplied(CByteConstView{ child_mismatch_bytes, 4u, 128u }, false, error) &&
        child_mismatch.promote(child_mismatch_live, schema, error));
    CLiveDocument rejected_child;
    CByteBuffer rejected_child_payload;
    TEST_EXPECT(ctx, !child_mismatch_live.prepare_output(rejected_child, rejected_child_payload, schema,
        EDataOutputForm::embedded, error) && error.reason == EInstanceLoadReason::unrepresentable_value &&
        error.occurrence == child_mismatch_live.find_specialisation(
            child_mismatch_live.find_base(CStringView{ "Pair" }, CStringView{ "base" }), CStringView{ "child" }) &&
        !rejected_child.is_ready() && !rejected_child_payload.is_ready());
    TEST_EXPECT(ctx, child_mismatch_live.prepare_output(rejected_child, rejected_child_payload, schema,
        EDataOutputForm::external, error) && rejected_child_payload.size() == 4u &&
        rejected_child_payload.data()[2] == 7u && rejected_child_payload.data()[3] == 9u);

    const std::string omitted_text = R"({"instances":{"Pair":{"base":{
        "locator":{"offset":0,"valid":true},"declaration":{"a":1}}}}})";
    CBakedDocumentBlock omitted_block;
    TEST_EXPECT(ctx, bake(omitted_text, omitted_block));
    alignas(128) std::uint8_t omitted_bytes[128] = { 1u, 7u };
    CBakedInstances omitted;
    CLiveInstances omitted_live;
    TEST_EXPECT(ctx, omitted.set_document(omitted_block.document()) && omitted.bind_schema(schema) &&
        omitted.load_supplied(CByteConstView{ omitted_bytes, 2u, 128u }, false, error) &&
        omitted.promote(omitted_live, schema, error));
    CLiveDocument rejected;
    CByteBuffer rejected_payload;
    TEST_EXPECT(ctx, !omitted_live.prepare_output(rejected, rejected_payload, schema,
        EDataOutputForm::embedded, error) &&
        error.reason == EInstanceLoadReason::unrepresentable_value && error.occurrence &&
        !rejected.is_ready() && !rejected_payload.is_ready());
    TEST_EXPECT(ctx, omitted_live.prepare_output(rejected, rejected_payload, schema,
        EDataOutputForm::external, error) && rejected.is_ready() && rejected_payload.is_ready());
    std::string shifted_definitions = definitions;
    shifted_definitions.insert(shifted_definitions.find("\"Pair\""), "\"Ahead\":{\"members\":[]},");
    CBakedDocumentBlock shifted_block;
    CBakedSchema shifted_schema;
    TEST_EXPECT(ctx, bake(shifted_definitions, shifted_block) &&
        shifted_schema.set_document(shifted_block.document()) && shifted_schema.resolve(schema_error) &&
        schema.resolved()->find_type(CStringView{ "Pair" }) !=
            shifted_schema.resolved()->find_type(CStringView{ "Pair" }));
    CLiveDocument shifted_output;
    CByteBuffer shifted_payload;
    TEST_EXPECT(ctx, stale_live.prepare_output(shifted_output, shifted_payload, shifted_schema,
        EDataOutputForm::embedded, error));
    std::string changed_definitions = definitions;
    const std::size_t field = changed_definitions.find("\"a\":{\"type\":\"u8\"}");
    TEST_EXPECT(ctx, field != std::string::npos);
    changed_definitions.replace(field, std::strlen("\"a\":{\"type\":\"u8\"}"),
        "\"a\":{\"type\":\"u8\",\"default\":1}");
    CBakedDocumentBlock changed_block;
    CBakedSchema changed_schema;
    TEST_EXPECT(ctx, bake(changed_definitions, changed_block) &&
        changed_schema.set_document(changed_block.document()) && changed_schema.resolve(schema_error));
    CLiveDocument incompatible_output;
    CByteBuffer incompatible_payload;
    TEST_EXPECT(ctx, !stale_live.prepare_output(incompatible_output, incompatible_payload, changed_schema,
        EDataOutputForm::embedded, error) && error.reason == EInstanceLoadReason::incompatible_schema &&
        !incompatible_output.is_ready() && !incompatible_payload.is_ready());

    const std::string scalar_definitions = R"({"types":{"structures":{"ScalarBag":{"members":[
        {"rows":{"type":{"element":"u8","count":1}}}]}}}})";
    CBakedDocumentBlock scalar_schema_block, scalar_source_block;
    CBakedSchema scalar_schema;
    TEST_EXPECT(ctx, bake(scalar_definitions, scalar_schema_block) &&
        scalar_schema.set_document(scalar_schema_block.document()) && scalar_schema.resolve(schema_error) &&
        bake(R"({"instances":{"ScalarBag":{"base":{"locator":{"offset":0,"valid":true},
            "declaration":{"rows":[1]},
            "specialisation":{"child":{"locator":{"offset":1,"valid":true},
                "declaration":{"rows":[{"x":9}]}}}}}}})", scalar_source_block));
    alignas(128) std::uint8_t scalar_bytes[128] = { 1u, 9u };
    CBakedInstances scalar_source;
    CLiveInstances scalar_live;
    TEST_EXPECT(ctx, scalar_source.set_document(scalar_source_block.document()) &&
        scalar_source.bind_schema(scalar_schema) &&
        scalar_source.load_supplied(CByteConstView{ scalar_bytes, 2u, 128u }, false, error) &&
        scalar_source.promote(scalar_live, scalar_schema, error));
    CLiveDocument scalar_output;
    CByteBuffer scalar_payload;
    TEST_EXPECT(ctx, !scalar_live.prepare_output(scalar_output, scalar_payload, scalar_schema,
        EDataOutputForm::embedded, error) && error.reason == EInstanceLoadReason::unrepresentable_value &&
        error.occurrence == scalar_live.find_specialisation(
            scalar_live.find_base(CStringView{ "ScalarBag" }, CStringView{ "base" }), CStringView{ "child" }) &&
        !scalar_output.is_ready() && !scalar_payload.is_ready());
    alignas(128) std::uint8_t scalar_changed[128] = { 7u };
    TEST_EXPECT(ctx, !scalar_live.capture_base(CStringView{ "ScalarBag" }, CStringView{ "base" },
        CByteConstView{ scalar_changed, 1u, 128u }, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration);

    const std::string scalar_edge_definitions = R"({"types":{"enumerations":{
        "Mode":{"storage":"u8","values":{"normal":2}}},"structures":{
        "ScalarEdge":{"members":[{"flag":{"type":"b8"}},{"mode":{"type":"Mode"}}]}}}})";
    CBakedDocumentBlock edge_schema_block, edge_source_block;
    CBakedSchema edge_schema;
    TEST_EXPECT(ctx, bake(scalar_edge_definitions, edge_schema_block) &&
        edge_schema.set_document(edge_schema_block.document()) && edge_schema.resolve(schema_error) &&
        bake(R"({"instances":{"ScalarEdge":{"edge":{"locator":{"offset":0,"valid":true},
            "declaration":{"flag":true,"mode":"normal"}}}}})", edge_source_block));
    alignas(128) std::uint8_t unusual_boolean[128] = { 2u, 2u };
    CBakedInstances edge_source;
    CLiveInstances edge_live;
    TEST_EXPECT(ctx, edge_source.set_document(edge_source_block.document()) &&
        edge_source.bind_schema(edge_schema) &&
        edge_source.load_supplied(CByteConstView{ unusual_boolean, 2u, 128u }, false, error) &&
        edge_source.promote(edge_live, edge_schema, error));
    CLiveDocument edge_external;
    CByteBuffer edge_payload;
    TEST_EXPECT(ctx, edge_live.prepare_output(edge_external, edge_payload, edge_schema,
        EDataOutputForm::external, error) && edge_payload.size() == 2u &&
        edge_payload.data()[0] == 2u && edge_payload.data()[1] == 2u);
    CLiveDocument edge_embedded;
    CByteBuffer edge_embedded_payload;
    TEST_EXPECT(ctx, !edge_live.prepare_output(edge_embedded, edge_embedded_payload, edge_schema,
        EDataOutputForm::embedded, error) && error.reason == EInstanceLoadReason::unrepresentable_value &&
        !edge_embedded.is_ready() && !edge_embedded_payload.is_ready());
    unusual_boolean[0] = 1u;
    unusual_boolean[1] = 5u;
    CBakedInstances unknown_enum_source;
    CLiveInstances unknown_enum_live;
    TEST_EXPECT(ctx, unknown_enum_source.set_document(edge_source_block.document()) &&
        unknown_enum_source.bind_schema(edge_schema) &&
        unknown_enum_source.load_supplied(CByteConstView{ unusual_boolean, 2u, 128u }, false, error) &&
        unknown_enum_source.promote(unknown_enum_live, edge_schema, error));
    CLiveDocument unknown_external;
    CByteBuffer unknown_payload;
    TEST_EXPECT(ctx, unknown_enum_live.prepare_output(unknown_external, unknown_payload, edge_schema,
        EDataOutputForm::external, error) && unknown_payload.data()[1] == 5u);
    CLiveDocument unknown_embedded;
    CByteBuffer unknown_embedded_payload;
    TEST_EXPECT(ctx, !unknown_enum_live.prepare_output(unknown_embedded, unknown_embedded_payload,
        edge_schema, EDataOutputForm::embedded, error) &&
        error.reason == EInstanceLoadReason::unrepresentable_value);

    CLiveDocument live_schema_document;
    CLiveSchema live_schema;
    TEST_EXPECT(ctx, parse_live(definitions, live_schema_document) &&
        live_schema.try_adopt(std::move(live_schema_document)) && live_schema.resolve(schema_error));
    CLiveDocument live_schema_output;
    CByteBuffer live_schema_payload;
    TEST_EXPECT(ctx, stale_live.prepare_output(live_schema_output, live_schema_payload, live_schema,
        EDataOutputForm::embedded, error));
    CBakedDocumentBlock live_schema_block;
    CByteBuffer live_schema_demoted_payload;
    CBakedInstances live_schema_role;
    TEST_EXPECT(ctx, stale_live.demote(live_schema_block, live_schema_demoted_payload, live_schema_role,
        live_schema, EDataOutputForm::external, error) && live_schema_role.loaded_ready());
    CLiveDocument occupied;
    CByteBuffer occupied_payload;
    TEST_EXPECT(ctx, occupied.initialise());
    TEST_EXPECT(ctx, !stale_live.prepare_output(occupied, occupied_payload, schema,
        EDataOutputForm::embedded, error) && error.reason == EInstanceLoadReason::invalid_input &&
        occupied.is_ready() && !occupied_payload.is_ready());
    const std::uint8_t* const occupied_bytes = moved_payload.data();
    TEST_EXPECT(ctx, !stale_live.demote(moved_block, moved_payload, demoted, schema,
        EDataOutputForm::embedded, error) && error.reason == EInstanceLoadReason::invalid_input &&
        moved_block.is_ready() && moved_payload.data() == occupied_bytes && demoted.loaded_ready());
    CLiveInstances no_instances;
    TEST_EXPECT(ctx, no_instances.initialise(schema));
    CLiveDocument no_instances_document;
    CByteBuffer no_instances_payload;
    TEST_EXPECT(ctx, no_instances.prepare_output(no_instances_document, no_instances_payload, schema,
        EDataOutputForm::embedded, error) && no_instances_document.is_ready() &&
        !no_instances_payload.is_ready());
    CBakedDocumentBlock no_instances_block;
    CBakedInstances no_instances_role;
    CByteBuffer no_instances_demoted_payload;
    TEST_EXPECT(ctx, no_instances.demote(no_instances_block, no_instances_demoted_payload,
        no_instances_role, schema, EDataOutputForm::embedded, error) &&
        no_instances_role.loaded_ready() && !no_instances_demoted_payload.is_ready());
    CLiveInstances zero_only;
    TEST_EXPECT(ctx, zero_only.initialise(schema) &&
        zero_only.capture_base(CStringView{ "Empty" }, CStringView{ "zero" }, {}, error));
    CBakedDocumentBlock zero_block;
    CByteBuffer zero_payload;
    CBakedInstances zero_role;
    TEST_EXPECT(ctx, zero_only.demote(zero_block, zero_payload, zero_role, schema,
        EDataOutputForm::embedded, error) && zero_role.loaded_ready() &&
        !zero_payload.is_ready() && zero_role.entry(zero_role.find_base(CStringView{ "Empty" },
            CStringView{ "zero" }), view) && view.byte_count == 0u && view.offset == 0u);

    const CInstanceHandle stale_base_handle = stale_live.find_base(CStringView{ "Pair" }, CStringView{ "base" });
    const CInstanceHandle stale_child_handle = stale_live.find_specialisation(stale_base_handle, CStringView{ "child" });
    const std::size_t bindings = schema.reference_count();
    std::size_t output_failures{}, output_successes{};
    for (std::size_t failure_offset = 0u; (failure_offset < 160u) && (output_successes == 0u); ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveDocument attempt_document;
            CByteBuffer attempt_payload;
            failing.fail_on = failing.calls + failure_offset;
            if (stale_live.prepare_output(attempt_document, attempt_payload, schema,
                EDataOutputForm::embedded, error))
            {
                ++output_successes;
                TEST_EXPECT(ctx, attempt_document.is_ready() && attempt_payload.is_ready());
            }
            else
            {
                ++output_failures;
                TEST_EXPECT(ctx, error.reason == EInstanceLoadReason::allocation_failed &&
                    !attempt_document.is_ready() && !attempt_payload.is_ready());
            }
            SInstanceEntryView source_view;
            std::uint64_t old_literal{};
            const CInstanceDocumentQuery source_query = stale_live.document_query();
            TEST_EXPECT(ctx, stale_live.entry(stale_child_handle, source_view) &&
                source_view.bytes[0] == 1u && source_view.bytes[1] == 9u &&
                source_query.unsigned_integer_value(source_query.object_child(source_view.declaration,
                    CStringView{ "b" }), old_literal) && old_literal == 4u &&
                (schema.reference_count() == bindings));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && (schema.reference_count() == bindings));
    }
    TEST_EXPECT(ctx, (output_failures >= 3u) && (output_successes == 1u) && stale_live.loaded_ready());
    std::size_t demote_failures{}, demote_successes{};
    for (std::size_t failure_offset = 0u; (failure_offset < 240u) && (demote_successes == 0u); ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CBakedDocumentBlock attempt_block;
            CByteBuffer attempt_payload;
            CBakedInstances attempt_role;
            failing.fail_on = failing.calls + failure_offset;
            if (stale_live.demote(attempt_block, attempt_payload, attempt_role, schema,
                EDataOutputForm::embedded, error))
            {
                ++demote_successes;
                TEST_EXPECT(ctx, attempt_block.is_ready() && attempt_payload.is_ready() &&
                    attempt_role.loaded_ready() && (schema.reference_count() == (bindings + 1u)));
            }
            else
            {
                ++demote_failures;
                TEST_EXPECT(ctx, error.reason == EInstanceLoadReason::allocation_failed &&
                    !attempt_block.is_ready() && !attempt_payload.is_ready() &&
                    !attempt_role.document_ready() && (schema.reference_count() == bindings));
            }
            SInstanceEntryView source_view;
            std::uint64_t old_literal{};
            const CInstanceDocumentQuery source_query = stale_live.document_query();
            TEST_EXPECT(ctx, stale_live.entry(stale_child_handle, source_view) &&
                source_view.bytes[0] == 1u && source_view.bytes[1] == 9u &&
                source_query.unsigned_integer_value(source_query.object_child(source_view.declaration,
                    CStringView{ "b" }), old_literal) && old_literal == 4u);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && (schema.reference_count() == bindings));
    }
    TEST_EXPECT(ctx, (demote_failures >= 3u) && (demote_successes == 1u) && stale_live.loaded_ready());
}

static void test_live_instance_boundaries(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Big":{"detail":{"alignment":128,"size":1048576},
            "members":[{"byte":{"type":"u8","offset":0}}]}}}})";
    CBakedDocumentBlock schema_block, source_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block) &&
        bake(R"({"instances":{"Pair":{"base":{"locator":{"offset":0,"valid":true},
            "specialisation":{"child":{"locator":{"offset":2,"valid":true},
            "declaration":{"a":9}}}}}}})", source_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    alignas(128) std::uint8_t original[128] = { 1u, 2u, 9u, 2u };
    alignas(128) std::uint8_t updated[128] = { 5u, 6u };
    CBakedInstances source;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, source.set_document(source_block.document()) && source.bind_schema(schema) &&
        source.load_supplied(CByteConstView{ original, 4u, 128u }, false, error));

    std::size_t promotion_failures{}, promotion_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 28u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveInstances destination;
            failing.fail_on = failing.calls + failure_offset;
            const bool promoted = source.promote(destination, schema, error);
            if (promoted)
            {
                ++promotion_successes;
                SInstanceEntryView view;
                TEST_EXPECT(ctx, destination.entry(destination.find_base(CStringView{ "Pair" }, CStringView{ "base" }),
                    view) && view.bytes[0] == 1u);
            }
            else
            {
                ++promotion_failures;
                TEST_EXPECT(ctx, !destination.document_ready() && !error.occurrence &&
                    error.reason == EInstanceLoadReason::allocation_failed && source.loaded_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, promotion_failures >= 3u && promotion_successes >= 1u);

    std::size_t append_failures{}, append_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 28u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveInstances attempt;
            TEST_EXPECT(ctx, attempt.initialise(schema));
            const CInstanceHandle prior = attempt.capture_base(CStringView{ "Pair" }, CStringView{ "prior" },
                CByteConstView{ original, 2u, 128u }, error);
            TEST_EXPECT(ctx, prior);
            failing.fail_on = failing.calls + failure_offset;
            const CInstanceHandle next = attempt.capture_base(CStringView{ "Pair" }, CStringView{ "next" },
                CByteConstView{ updated, 2u, 128u }, error);
            SInstanceEntryView prior_view;
            TEST_EXPECT(ctx, attempt.loaded_ready() && attempt.entry(prior, prior_view) &&
                prior_view.bytes[0] == 1u && prior_view.bytes[1] == 2u);
            if (next)
            {
                ++append_successes;
                TEST_EXPECT(ctx, attempt.find_base(CStringView{ "Pair" }, CStringView{ "next" }) == next);
            }
            else
            {
                ++append_failures;
                const CInstanceDocumentQuery query = attempt.document_query();
                const CInstanceHandle group = query.parent(prior);
                TEST_EXPECT(ctx, error.reason == EInstanceLoadReason::allocation_failed &&
                    !error.occurrence && !query.object_child(group, CStringView{ "next" }));
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, append_failures >= 3u && append_successes >= 1u);

    CLiveInstances growing;
    TEST_EXPECT(ctx, growing.initialise(schema));
    CByteBuffer large_bytes;
    TEST_EXPECT(ctx, large_bytes.allocate(1048576u, 128u) && large_bytes.set_size(1048576u));
    std::memset(large_bytes.data(), 0, large_bytes.size());
    large_bytes.data()[0] = 17u;
    const CInstanceHandle large_first = growing.capture_base(CStringView{ "Big" }, CStringView{ "first" },
        large_bytes.const_view(), error);
    TEST_EXPECT(ctx, large_first);
    const std::uint8_t* previous_payload = growing.payload_view().data();
    bool payload_grew{};
    for (unsigned ordinal = 0u; ordinal < 4u; ++ordinal)
    {
        SInstanceEntryView borrowed, copied;
        TEST_EXPECT(ctx, growing.entry(large_first, borrowed));
        const std::string name = "copy" + std::to_string(ordinal);
        const CInstanceHandle next = growing.capture_base(CStringView{ "Big" }, CStringView{ name.c_str() },
            CByteConstView{ borrowed.bytes, 1048576u, 128u }, error);
        TEST_EXPECT(ctx, next && growing.entry(next, copied) && copied.bytes[0] == 17u);
        const std::uint8_t* current_payload = growing.payload_view().data();
        payload_grew |= current_payload != previous_payload;
        previous_payload = current_payload;
    }
    SInstanceEntryView retained_large;
    TEST_EXPECT(ctx, payload_grew && growing.entry(large_first, retained_large) && retained_large.bytes[0] == 17u);

    CLiveDocument values;
    TEST_EXPECT(ctx, parse_live(R"({"selected":{"a":9}})", values));
    const CInstanceDocumentQuery query{ values };
    std::size_t edit_failures{}, edit_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 32u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveInstances attempt;
            TEST_EXPECT(ctx, attempt.initialise(schema));
            const CInstanceHandle base = attempt.capture_base(CStringView{ "Pair" }, CStringView{ "base" },
                CByteConstView{ original, 2u, 128u }, error);
            const CInstanceHandle child = attempt.create_specialisation(base, CStringView{ "child" }, query,
                query.object_child(query.root(), CStringView{ "selected" }), error);
            TEST_EXPECT(ctx, base && child);
            failing.fail_on = failing.calls + failure_offset;
            const CInstanceHandle changed = attempt.capture_base(CStringView{ "Pair" }, CStringView{ "base" },
                CByteConstView{ updated, 2u, 128u }, error);
            if (changed)
            {
                ++edit_successes;
                SInstanceEntryView view;
                TEST_EXPECT(ctx, changed == base && attempt.entry(child, view) &&
                    view.bytes[0] == 9u && view.bytes[1] == 6u);
            }
            else
            {
                ++edit_failures;
                if (attempt.loaded_ready())
                {
                    SInstanceEntryView base_view, child_view;
                    TEST_EXPECT(ctx, attempt.entry(base, base_view) && attempt.entry(child, child_view) &&
                        base_view.bytes[0] == 1u && base_view.bytes[1] == 2u &&
                        child_view.bytes[0] == 9u && child_view.bytes[1] == 2u);
                }
                else TEST_EXPECT(ctx, attempt.document_query().is_ready());
                TEST_EXPECT(ctx, error.reason == EInstanceLoadReason::allocation_failed && !error.occurrence);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, edit_failures >= 3u && edit_successes >= 1u);

    const std::string explicit_defaults = R"({"types":{"structures":{"Pair":{"members":[
        {"a":{"type":"u8","default":0}},{"b":{"type":"u8","default":0}}]}}}})";
    CBakedDocumentBlock defaults_block;
    TEST_EXPECT(ctx, bake(explicit_defaults, defaults_block));
    CBakedSchema defaults_schema;
    TEST_EXPECT(ctx, defaults_schema.set_document(defaults_block.document()) && defaults_schema.resolve(schema_error));
    CLiveInstances equivalent;
    TEST_EXPECT(ctx, source.promote(equivalent, defaults_schema, error) && equivalent.loaded_ready());

    const auto array_defaults = [](const char* const supplied)
    {
        return std::string{ "{\"types\":{\"structures\":{\"ArrayBag\":{\"members\":[" } +
            "{\"values\":{\"type\":{\"element\":\"u8\",\"count\":3},\"default\":" + supplied +
            "}}]}}}}";
    };
    CBakedDocumentBlock short_block, long_block, different_block, array_source_block;
    TEST_EXPECT(ctx, bake(array_defaults("[1]"), short_block) &&
        bake(array_defaults("[1,0]"), long_block) &&
        bake(array_defaults("[1,2]"), different_block) &&
        bake(R"({"instances":{"ArrayBag":{"base":{"locator":{"offset":0,"valid":true}}}}})",
            array_source_block));
    CBakedSchema short_schema, long_schema, different_schema;
    TEST_EXPECT(ctx, short_schema.set_document(short_block.document()) && short_schema.resolve(schema_error) &&
        long_schema.set_document(long_block.document()) && long_schema.resolve(schema_error) &&
        different_schema.set_document(different_block.document()) && different_schema.resolve(schema_error));
    alignas(128) std::uint8_t array_bytes[128] = { 1u, 0u, 0u };
    CBakedInstances array_source;
    TEST_EXPECT(ctx, array_source.set_document(array_source_block.document()) &&
        array_source.bind_schema(short_schema) &&
        array_source.load_supplied(CByteConstView{ array_bytes, 3u, 128u }, false, error));
    CLiveInstances equal_tail, changed_tail;
    TEST_EXPECT(ctx, array_source.promote(equal_tail, long_schema, error));
    TEST_EXPECT(ctx, !array_source.promote(changed_tail, different_schema, error) &&
        error.reason == EInstanceLoadReason::incompatible_schema && !changed_tail.document_ready());

    CLiveInstances detached;
    {
        CLiveDocument parsed;
        TEST_EXPECT(ctx, parse_live(definitions, parsed));
        CLiveSchema temporary;
        TEST_EXPECT(ctx, temporary.try_adopt(std::move(parsed)) && temporary.resolve(schema_error));
        TEST_EXPECT(ctx, detached.initialise(temporary));
        TEST_EXPECT(ctx, detached.capture_base(CStringView{ "Pair" }, CStringView{ "base" },
            CByteConstView{ original, 2u, 128u }, error));
    }
    SInstanceEntryView detached_view;
    TEST_EXPECT(ctx, !detached.loaded_ready() && detached.document_query().is_ready() &&
        !detached.entry(detached.find_base(CStringView{ "Pair" }, CStringView{ "base" }), detached_view));

    SFailingAllocator retry_failure{ 0u, SIZE_MAX };
    memory::CMemoryAllocator retry_allocator{ &retry_failure, &allocate_with_failure,
        &tests::deallocate_test_memory };
    memory::CMemoryContext retry_context{ retry_allocator };
    {
        tests::TMemoryContextScope scope{ &retry_context };
        CLiveDocument parsed;
        TEST_EXPECT(ctx, parse_live(definitions, parsed));
        CLiveSchema live_schema;
        TEST_EXPECT(ctx, live_schema.try_adopt(std::move(parsed)) && live_schema.resolve(schema_error));
        CLiveInstances bound;
        TEST_EXPECT(ctx, bound.initialise(live_schema));
        const CInstanceHandle retained = bound.capture_base(CStringView{ "Pair" }, CStringView{ "base" },
            CByteConstView{ original, 2u, 128u }, error);
        TEST_EXPECT(ctx, retained);
        retry_failure.fail_on = retry_failure.calls;
        TEST_EXPECT(ctx, !live_schema.resolve(schema_error) && !bound.loaded_ready() &&
            bound.document_query().is_ready() && !bound.entry(retained, detached_view));
        retry_failure.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, live_schema.resolve(schema_error) && bound.loaded_ready() &&
            bound.entry(retained, detached_view) && detached_view.bytes[0] == 1u);
    }
    TEST_EXPECT(ctx, retry_context.is_attribution_empty());
}

static void test_baked_instances(TTestContext& ctx)
{
    static_assert(!std::is_copy_constructible_v<CBakedInstances> && !std::is_move_constructible_v<CBakedInstances>);
    const std::string definitions = R"({"types":{"structures":{
        "Vec":{"members":[{"x":{"type":"f32"}},{"y":{"type":"f32"}},
            {"z":{"type":"f32"}},{"w":{"type":"f32","default":7.0}}]},
        "Empty":{"members":[]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    const std::string source = R"({"instances":{"Vec":{
        "base":{"locator":{"offset":0,"valid":false},"declaration":[2.0,1.0,4.0,7.0],
            "specialisation":{
                "first":{"locator":{"offset":0,"valid":false},"declaration":[2.0,1.0,4.0],
                    "specialisation":{"deep":{"locator":{"offset":0,"valid":false},
                        "declaration":{"y":3.0}}}},
                "sibling":{"locator":{"offset":0,"valid":false},"declaration":{"x":9.0}}}},
        "other":{"locator":{"offset":0,"valid":false},
            "specialisation":{"deep":{"locator":{"offset":0,"valid":false}}}}},
        "Empty":{"zero":{"locator":{"offset":0,"valid":false,"count":1,"size":0},
            "specialisation":{"same":{"locator":{"offset":0,"valid":false}}}}}},
        "data":{"untouched":true}})";
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, bake(source, block));
    CMutableBakedDocument mutable_document{ block };
    CBakedInstances instances;
    TEST_EXPECT(ctx, instances.set_document(mutable_document) && instances.bind_schema(schema));
    const CInstanceDocumentQuery query = instances.document_query();
    TEST_EXPECT(ctx, query.root() && query.name(instances.instances_root()) == CStringView{ "instances" } &&
        query.object_child(query.root(), CStringView{ "data" }));
    CByteBuffer owner;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, instances.materialise(owner, error) && instances.loaded_ready());
    const CInstanceHandle base = instances.find_base(CStringView{ "Vec" }, CStringView{ "base" });
    const CInstanceHandle first = instances.find_specialisation(base, CStringView{ "first" });
    const CInstanceHandle deep = instances.find_specialisation(first, CStringView{ "deep" });
    const CInstanceHandle sibling = instances.find_specialisation(base, CStringView{ "sibling" });
    const CInstanceHandle other = instances.find_base(CStringView{ "Vec" }, CStringView{ "other" });
    const CInstanceHandle other_deep = instances.find_specialisation(other, CStringView{ "deep" });
    const CInstanceHandle zero = instances.find_base(CStringView{ "Empty" }, CStringView{ "zero" });
    const CInstanceHandle zero_same = instances.find_specialisation(zero, CStringView{ "same" });
    SInstanceEntryView base_view, first_view, deep_view, sibling_view, other_view, other_deep_view, zero_view;
    TEST_EXPECT(ctx, instances.entry(base, base_view) && instances.entry(first, first_view) &&
        instances.entry(deep, deep_view) && instances.entry(sibling, sibling_view) &&
        instances.entry(other, other_view) && instances.entry(other_deep, other_deep_view) &&
        instances.entry(zero, zero_view));
    const auto component = [](const SInstanceEntryView& value, const std::size_t offset)
    {
        float result{};
        if (value.bytes) std::memcpy(&result, value.bytes + offset, sizeof(result));
        return result;
    };
    TEST_EXPECT(ctx, component(base_view, 0u) == 2.0f && component(first_view, 0u) == 2.0f &&
        component(deep_view, 4u) == 3.0f && component(sibling_view, 0u) == 9.0f &&
        component(base_view, 4u) == 1.0f && component(first_view, 4u) == 1.0f &&
        component(sibling_view, 4u) == 1.0f && component(first_view, 12u) == 7.0f &&
        component(other_view, 0u) == 0.0f && component(other_deep_view, 12u) == 7.0f);
    TEST_EXPECT(ctx, base_view.bytes != first_view.bytes && first_view.bytes != deep_view.bytes &&
        deep_view.bytes != sibling_view.bytes && base_view.byte_count == 16u);
    TEST_EXPECT(ctx, !base_view.parent && first_view.parent == base && deep_view.parent == first &&
        instances.parent_instance(deep) == first && !instances.parent_instance(base));
    TEST_EXPECT(ctx, instances.first_specialisation(base) == first &&
        instances.next_specialisation(first) == sibling && !instances.next_specialisation(sibling) &&
        instances.find_specialisation(base, CStringView{ "deep" }) == CInstanceHandle{} &&
        other_deep != deep);
    TEST_EXPECT(ctx, first_view.declaration && !other_view.declaration && zero_view.byte_count == 0u &&
        zero_view.bytes == nullptr && instances.parent_instance(zero_same) == zero);
    const CBakedDocument changed = block.document();
    const CBakedValueIndex base_locator = changed.object_child(changed.object_child(
        changed.object_child(changed.object_child(changed.root(), CStringView{ "instances" }),
            CStringView{ "Vec" }), CStringView{ "base" }), CStringView{ "locator" });
    bool valid{};
    TEST_EXPECT(ctx, changed.boolean_value(changed.object_child(base_locator, CStringView{ "valid" }), valid) && valid);

    CBakedInstances supplied;
    TEST_EXPECT(ctx, supplied.set_document(block.document()) && supplied.bind_schema(schema));
    TEST_EXPECT(ctx, supplied.load_supplied(owner.const_view(), true, error));
    const std::uint8_t* const owned_address = owner.const_view().data();
    CByteBuffer moved_owner{ std::move(owner) };
    TEST_EXPECT(ctx, supplied.payload_view().data() == owned_address && moved_owner.const_view().data() == owned_address);
    alignas(128) std::uint8_t altered[128]{};
    TEST_EXPECT(ctx, moved_owner.size() <= sizeof(altered));
    if (moved_owner.size() <= sizeof(altered))
    {
        std::memcpy(altered, moved_owner.data(), moved_owner.size());
        altered[base_view.offset + 3u] ^= 1u;
        TEST_EXPECT(ctx, supplied.load_supplied(CByteConstView{ altered, moved_owner.size(), 128u }, false, error));
        TEST_EXPECT(ctx, !supplied.load_supplied(CByteConstView{ altered, moved_owner.size(), 128u }, true, error) &&
            error.reason == EInstanceLoadReason::embedded_mismatch && !supplied.loaded_ready());
    }
    TEST_EXPECT(ctx, supplied.document_query().is_ready() && !supplied.entry(base, base_view));
    supplied.clear();
    TEST_EXPECT(ctx, moved_owner.const_view().data() == owned_address);

    CLiveDocument parsed_schema;
    TEST_EXPECT(ctx, parse_live(definitions, parsed_schema));
    CLiveSchema live_schema;
    TEST_EXPECT(ctx, live_schema.try_adopt(std::move(parsed_schema)) && live_schema.resolve(schema_error));
    CBakedInstances live_bound;
    TEST_EXPECT(ctx, live_bound.set_document(block.document()) && live_bound.bind_schema(live_schema));
    TEST_EXPECT(ctx, live_bound.load_supplied(moved_owner.const_view(), true, error));

    CBakedInstances invalidated;
    {
        CBakedSchema temporary;
        TEST_EXPECT(ctx, temporary.set_document(schema_block.document()) && temporary.resolve(schema_error));
        TEST_EXPECT(ctx, invalidated.set_document(block.document()) && invalidated.bind_schema(temporary));
        TEST_EXPECT(ctx, invalidated.load_supplied(moved_owner.const_view(), false, error));
    }
    TEST_EXPECT(ctx, !invalidated.loaded_ready() && !invalidated.find_base(CStringView{ "Vec" }, CStringView{ "base" }) &&
        invalidated.document_query().is_ready());
    invalidated.clear();

    SFailingAllocator failing{ 0u, SIZE_MAX };
    memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CBakedSchema retry_schema;
        TEST_EXPECT(ctx, retry_schema.set_document(schema_block.document()) && retry_schema.resolve(schema_error));
        CBakedInstances retry_client;
        TEST_EXPECT(ctx, retry_client.set_document(block.document()) && retry_client.bind_schema(retry_schema));
        TEST_EXPECT(ctx, retry_client.load_supplied(moved_owner.const_view(), false, error));
        failing.fail_on = failing.calls;
        TEST_EXPECT(ctx, !retry_schema.resolve(schema_error) && !retry_client.loaded_ready() &&
            retry_client.document_query().is_ready());
        failing.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, retry_schema.resolve(schema_error) && retry_client.loaded_ready());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_baked_instance_rejections(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Aligned":{"members":[{"value":{"type":"u16"}}]},
        "Empty":{"members":[]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    alignas(128) std::uint8_t bytes[128] = { 1u, 2u, 3u, 4u };
    const auto reject = [&](const std::string& entries, const EInstanceLoadReason reason)
    {
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, bake("{\"instances\":" + entries + "}", block));
        CBakedInstances instances;
        TEST_EXPECT(ctx, instances.set_document(block.document()) && instances.bind_schema(schema));
        SInstanceDiagnostic diagnostic;
        TEST_EXPECT(ctx, !instances.load_supplied(CByteConstView{ bytes, 4u, 128u }, false, diagnostic) &&
            diagnostic.reason == reason && !instances.loaded_ready() && instances.document_query().is_ready());
    };
    reject(R"({"Missing":{"a":{"locator":{"offset":0,"valid":true}}}})", EInstanceLoadReason::unknown_type);
    reject(R"({"Pair":{"a":{"declaration":[1,2]}}})", EInstanceLoadReason::missing_property);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true,"count":2}}}})", EInstanceLoadReason::invalid_count);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true,"size":3}}}})", EInstanceLoadReason::invalid_range);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true}},
        "b":{"locator":{"offset":1,"valid":true}}}})", EInstanceLoadReason::overlap);
    reject(R"({"Pair":{"a":{"locator":{"offset":4,"valid":true}}}})", EInstanceLoadReason::invalid_range);
    reject(R"({"Aligned":{"a":{"locator":{"offset":1,"valid":true}}}})", EInstanceLoadReason::invalid_range);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":false}}}})", EInstanceLoadReason::invalid_locator);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true},"extra":0}}})",
        EInstanceLoadReason::unknown_property);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true},"specialisation":[]}}})",
        EInstanceLoadReason::invalid_input);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true},"declaration":null}}})",
        EInstanceLoadReason::invalid_declaration);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true},
        "specialisation":{"child":{"declaration":[1,2]}}}}})", EInstanceLoadReason::missing_property);
    reject(R"({"Pair":{"a":{"locator":{"offset":0,"valid":true,"count":-1}}}})",
        EInstanceLoadReason::invalid_count);

    const std::string incomplete = R"({"instances":{"Pair":{"a":{
        "locator":{"offset":0,"valid":true},"declaration":[1]}}}})";
    CBakedDocumentBlock incomplete_block;
    TEST_EXPECT(ctx, bake(incomplete, incomplete_block));
    CBakedInstances unchecked;
    TEST_EXPECT(ctx, unchecked.set_document(incomplete_block.document()) && unchecked.bind_schema(schema));
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, unchecked.load_supplied(CByteConstView{ bytes, 4u, 128u }, false, error));
    //  A short base declaration is valid and fills the second member from defaults.
    alignas(128) std::uint8_t short_bytes[128] = { 1u, 0u };
    TEST_EXPECT(ctx, unchecked.load_supplied(CByteConstView{ short_bytes, 2u, 128u }, true, error));
    const std::string bad_value = R"({"instances":{"Pair":{"a":{
        "locator":{"offset":0,"valid":true},"declaration":{"missing":1}}}}})";
    CBakedDocumentBlock bad_block;
    TEST_EXPECT(ctx, bake(bad_value, bad_block));
    CBakedInstances bad;
    TEST_EXPECT(ctx, bad.set_document(bad_block.document()) && bad.bind_schema(schema));
    TEST_EXPECT(ctx, bad.load_supplied(CByteConstView{ bytes, 4u, 128u }, false, error));
    TEST_EXPECT(ctx, !bad.load_supplied(CByteConstView{ bytes, 4u, 128u }, true, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration && !bad.loaded_ready());

    const std::string unset = R"({"instances":{"Pair":{"a":{
        "locator":{"offset":4294967295,"valid":false},"declaration":[1,2]}}}})";
    CBakedDocumentBlock unset_block;
    TEST_EXPECT(ctx, bake(unset, unset_block));
    CBakedInstances immutable;
    TEST_EXPECT(ctx, immutable.set_document(unset_block.document()) && immutable.bind_schema(schema));
    CByteBuffer refused_owner;
    TEST_EXPECT(ctx, !immutable.materialise(refused_owner, error) &&
        error.reason == EInstanceLoadReason::invalid_locator && !refused_owner.is_ready());
    CMutableBakedDocument mutable_unset{ unset_block };
    CBakedInstances material;
    TEST_EXPECT(ctx, material.set_document(mutable_unset) && material.bind_schema(schema));
    TEST_EXPECT(ctx, material.materialise(refused_owner, error));
    SInstanceEntryView view;
    TEST_EXPECT(ctx, material.entry(material.find_base(CStringView{ "Pair" }, CStringView{ "a" }), view) &&
        view.offset == 0u && view.bytes[0] == 1u && view.bytes[1] == 2u);
    const CBakedDocument updated = unset_block.document();
    const CBakedValueIndex locator = updated.object_child(updated.object_child(updated.object_child(
        updated.object_child(updated.root(), CStringView{ "instances" }), CStringView{ "Pair" }),
        CStringView{ "a" }), CStringView{ "locator" });
    std::uint64_t published_offset{};
    bool published_valid{};
    TEST_EXPECT(ctx, updated.unsigned_integer_value(updated.object_child(locator, CStringView{ "offset" }), published_offset) &&
        updated.boolean_value(updated.object_child(locator, CStringView{ "valid" }), published_valid) &&
        published_offset == 0u && published_valid);

    CBakedDocumentBlock zero_block;
    TEST_EXPECT(ctx, bake(R"({"instances":{"Empty":{"zero":{
        "locator":{"offset":0,"valid":true,"size":0,"count":1},
        "specialisation":{"same":{"locator":{"offset":0,"valid":true}}}}}}})", zero_block));
    CBakedInstances zero;
    TEST_EXPECT(ctx, zero.set_document(zero_block.document()) && zero.bind_schema(schema));
    TEST_EXPECT(ctx, zero.load_supplied(CByteConstView{}, true, error));
    TEST_EXPECT(ctx, zero.entry(zero.find_base(CStringView{ "Empty" }, CStringView{ "zero" }), view) &&
        view.byte_count == 0u && view.bytes == nullptr);
    CBakedInstances misaligned;
    TEST_EXPECT(ctx, misaligned.set_document(incomplete_block.document()) && misaligned.bind_schema(schema));
    TEST_EXPECT(ctx, !misaligned.load_supplied(CByteConstView{ bytes + 1u, 4u, 1u }, false, error) &&
        error.reason == EInstanceLoadReason::invalid_range);
}

static void test_baked_instance_boundaries(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Wide":{"members":[{"values":{"type":{"element":"u8","count":256}}}]},
        "Empty":{"members":[]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    const std::string bad_text = R"({"instances":{"Pair":{"bad":{
        "locator":{"offset":0,"valid":false},"declaration":{"unknown":1}}}}})";
    CBakedDocumentBlock bad_block;
    TEST_EXPECT(ctx, bake(bad_text, bad_block));
    CMutableBakedDocument bad_mutable{ bad_block };
    CBakedInstances bad;
    TEST_EXPECT(ctx, bad.set_document(bad_mutable) && bad.bind_schema(schema));
    CByteBuffer unchanged_owner;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, !bad.materialise(unchanged_owner, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration && !unchanged_owner.is_ready() && !bad.loaded_ready());
    const CBakedDocument unchanged = bad_block.document();
    const CBakedValueIndex bad_locator = unchanged.object_child(unchanged.object_child(unchanged.object_child(
        unchanged.object_child(unchanged.root(), CStringView{ "instances" }), CStringView{ "Pair" }),
        CStringView{ "bad" }), CStringView{ "locator" });
    bool still_invalid{};
    TEST_EXPECT(ctx, unchanged.boolean_value(unchanged.object_child(bad_locator, CStringView{ "valid" }), still_invalid) &&
        !still_invalid);

    const std::string ready_text = R"({"instances":{"Pair":{"ready":{
        "locator":{"offset":0,"valid":true,"count":1,"size":2},"declaration":[1,2]}}}})";
    CBakedDocumentBlock ready_block;
    TEST_EXPECT(ctx, bake(ready_text, ready_block));
    CBakedInstances readonly;
    TEST_EXPECT(ctx, readonly.set_document(ready_block.document()) && readonly.bind_schema(schema));
    CByteBuffer readonly_owner;
    TEST_EXPECT(ctx, readonly.materialise(readonly_owner, error));
    SInstanceEntryView view;
    TEST_EXPECT(ctx, readonly.entry(readonly.find_base(CStringView{ "Pair" }, CStringView{ "ready" }), view) &&
        view.byte_count == 2u && view.bytes[0] == 1u && view.bytes[1] == 2u);
    for (std::size_t failure_offset = 0u; failure_offset < 2u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CBakedInstances attempt;
            TEST_EXPECT(ctx, attempt.set_document(ready_block.document()) && attempt.bind_schema(schema));
            CByteBuffer output;
            failing.fail_on = failing.calls + failure_offset;
            SInstanceDiagnostic allocation_error;
            TEST_EXPECT(ctx, !attempt.materialise(output, allocation_error) &&
                allocation_error.reason == EInstanceLoadReason::allocation_failed &&
                !attempt.loaded_ready() && !output.is_ready());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }

    std::string values;
    for (unsigned index = 0u; index < 256u; ++index)
    {
        if (index != 0u) values += ',';
        values += '0';
    }
    const std::string wide_text = R"({"instances":{"Wide":{"first":{
        "locator":{"offset":0,"valid":true},"declaration":[[)" + values + R"(]]}},
        "Pair":{"second":{"locator":{"offset":0,"valid":false},"declaration":[1,2]}}}})";
    CBakedDocumentBlock wide_block;
    TEST_EXPECT(ctx, bake(wide_text, wide_block));
    CMutableBakedDocument wide_mutable{ wide_block };
    CBakedInstances wide;
    TEST_EXPECT(ctx, wide.set_document(wide_mutable) && wide.bind_schema(schema));
    CByteBuffer wide_owner;
    TEST_EXPECT(ctx, wide.materialise(wide_owner, error));
    TEST_EXPECT(ctx, wide.entry(wide.find_base(CStringView{ "Pair" }, CStringView{ "second" }), view) &&
        view.offset == 256u && view.bytes[0] == 1u && view.bytes[1] == 2u);

    std::string nested = R"({"locator":{"offset":0,"valid":true}})";
    for (int depth = 109; depth >= 0; --depth)
    {
        nested = R"({"locator":{"offset":0,"valid":true},"specialisation":{"level)" +
            std::to_string(depth) + "\":" + nested + "}}";
    }
    CBakedDocumentBlock deep_block;
    TEST_EXPECT(ctx, bake("{\"instances\":{\"Empty\":{\"root\":" + nested + "}}}", deep_block));
    CBakedInstances deep;
    TEST_EXPECT(ctx, deep.set_document(deep_block.document()) && deep.bind_schema(schema));
    TEST_EXPECT(ctx, deep.load_supplied(CByteConstView{}, true, error));
    CInstanceHandle step = deep.find_base(CStringView{ "Empty" }, CStringView{ "root" });
    for (unsigned depth = 0u; depth < 110u && step; ++depth)
    {
        const std::string name = "level" + std::to_string(depth);
        step = deep.find_specialisation(step, CStringView{ name.c_str() });
    }
    TEST_EXPECT(ctx, step && deep.entry(step, view) && view.byte_count == 0u && view.bytes == nullptr);

    const std::string nan_text = R"({"instances":{"f32":{"nan":{
        "locator":{"offset":0,"valid":true},"declaration":"NaN"}}}})";
    CBakedDocumentBlock nan_block;
    TEST_EXPECT(ctx, bake(nan_text, nan_block));
    CBakedInstances nan;
    TEST_EXPECT(ctx, nan.set_document(nan_block.document()) && nan.bind_schema(schema));
    alignas(128) std::uint8_t nan_bytes[128] = { 1u, 0u, 0xc0u, 0xffu };
    TEST_EXPECT(ctx, nan.load_supplied(CByteConstView{ nan_bytes, 4u, 128u }, true, error));
    const std::string zero_text = R"({"instances":{"f32":{"zero":{
        "locator":{"offset":0,"valid":true},"declaration":-0.0}}}})";
    CBakedDocumentBlock signed_zero_block;
    TEST_EXPECT(ctx, bake(zero_text, signed_zero_block));
    CBakedInstances signed_zero;
    TEST_EXPECT(ctx, signed_zero.set_document(signed_zero_block.document()) && signed_zero.bind_schema(schema));
    alignas(128) std::uint8_t plus_zero[128]{};
    TEST_EXPECT(ctx, !signed_zero.load_supplied(CByteConstView{ plus_zero, 4u, 128u }, true, error) &&
        error.reason == EInstanceLoadReason::embedded_mismatch);
}

static void test_baked_instance_staging(TTestContext& ctx)
{
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(R"({"types":{"structures":{"Pair":{"members":[
        {"a":{"type":"u8"}},{"b":{"type":"u8"}}]}}}})", schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    const std::string bad_child = R"({"instances":{"Pair":{"base":{
        "specialisation":{"child":{"declaration":{"unknown":3},
            "locator":{"offset":0,"valid":false}}},
        "declaration":[1,2],"locator":{"offset":0,"valid":false}}}}})";
    CBakedDocumentBlock bad_block;
    TEST_EXPECT(ctx, bake(bad_child, bad_block));
    CMutableBakedDocument bad_mutable{ bad_block };
    CBakedInstances bad;
    TEST_EXPECT(ctx, bad.set_document(bad_mutable) && bad.bind_schema(schema));
    CByteBuffer unchanged_owner;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, !bad.materialise(unchanged_owner, error) &&
        error.reason == EInstanceLoadReason::invalid_declaration && !unchanged_owner.is_ready());
    const CBakedDocument bad_document = bad_block.document();
    const CBakedValueIndex bad_base = bad_document.object_child(bad_document.object_child(
        bad_document.object_child(bad_document.root(), CStringView{ "instances" }), CStringView{ "Pair" }),
        CStringView{ "base" });
    const CBakedValueIndex bad_descendant = bad_document.object_child(
        bad_document.object_child(bad_base, CStringView{ "specialisation" }), CStringView{ "child" });
    const auto valid = [&](const CBakedValueIndex instance)
    {
        bool flag{ true };
        const CBakedValueIndex locator = bad_document.object_child(instance, CStringView{ "locator" });
        return bad_document.boolean_value(bad_document.object_child(locator, CStringView{ "valid" }), flag) && flag;
    };
    TEST_EXPECT(ctx, !valid(bad_base) && !valid(bad_descendant));

    const std::string good_child = R"({"instances":{"Pair":{"base":{
        "specialisation":{"child":{"declaration":{"b":3},
            "locator":{"offset":0,"valid":false}}},
        "declaration":[1,2],"locator":{"offset":0,"valid":false}}}}})";
    CBakedDocumentBlock good_block;
    TEST_EXPECT(ctx, bake(good_child, good_block));
    CMutableBakedDocument good_mutable{ good_block };
    bool saw_frame_failure = false, materialised = false;
    for (std::size_t failure_offset = 0u; failure_offset < 8u && !materialised; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CBakedInstances attempt;
            TEST_EXPECT(ctx, attempt.set_document(good_mutable) && attempt.bind_schema(schema));
            CByteBuffer output;
            failing.fail_on = failing.calls + failure_offset;
            SInstanceDiagnostic allocation_error;
            materialised = attempt.materialise(output, allocation_error);
            if (materialised)
            {
                SInstanceEntryView child;
                const CInstanceHandle base = attempt.find_base(CStringView{ "Pair" }, CStringView{ "base" });
                TEST_EXPECT(ctx, attempt.entry(attempt.find_specialisation(base, CStringView{ "child" }), child) &&
                    child.bytes[0] == 1u && child.bytes[1] == 3u);
            }
            else
            {
                saw_frame_failure |= allocation_error.reason == EInstanceLoadReason::allocation_failed &&
                    attempt.document_query().name(allocation_error.occurrence) == CStringView{ "specialisation" };
                TEST_EXPECT(ctx, allocation_error.reason == EInstanceLoadReason::allocation_failed &&
                    !attempt.loaded_ready() && !output.is_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, materialised && saw_frame_failure);

    alignas(128) std::uint8_t expected_bytes[128] = { 1u, 2u, 1u, 3u };
    bool saw_expected_failure = false, compared = false;
    for (std::size_t failure_offset = 0u; failure_offset < 8u && !compared; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CBakedInstances attempt;
            TEST_EXPECT(ctx, attempt.set_document(good_block.document()) && attempt.bind_schema(schema));
            failing.fail_on = failing.calls + failure_offset;
            SInstanceDiagnostic allocation_error;
            compared = attempt.load_supplied(CByteConstView{ expected_bytes, 4u, 128u }, true, allocation_error);
            if (compared)
            {
                TEST_EXPECT(ctx, attempt.loaded_ready());
            }
            else
            {
                saw_expected_failure |= allocation_error.reason == EInstanceLoadReason::allocation_failed &&
                    !allocation_error.occurrence;
                TEST_EXPECT(ctx, allocation_error.reason == EInstanceLoadReason::allocation_failed && !attempt.loaded_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, compared && saw_expected_failure);

    const std::string combined_text = R"({"types":{"structures":{"Pair":{"members":[
        {"a":{"type":"u8"}},{"b":{"type":"u8"}}]}}},
        "instances":{"Pair":{"bad":{"locator":{"offset":0,"valid":true,"count":2}}}},
        "data":{"Pair":{"array":{"locator":{"offset":0,"valid":true,"count":1},
            "data":[[1,2]]}}}})";
    CBakedDocumentBlock combined_block;
    TEST_EXPECT(ctx, bake(combined_text, combined_block));
    CBakedSchema combined_schema;
    TEST_EXPECT(ctx, combined_schema.set_document(combined_block.document()) && combined_schema.resolve(schema_error));
    CBakedBulkData surviving_bulk;
    TEST_EXPECT(ctx, surviving_bulk.set_document(combined_block.document()) && surviving_bulk.bind_schema(combined_schema));
    SInstanceDiagnostic instance_error;
    SBulkDiagnostic bulk_error;
    TEST_EXPECT(ctx, surviving_bulk.load_supplied(CByteConstView{ expected_bytes, 2u, 128u }, true, bulk_error));
    CBakedInstances rejected_instances;
    TEST_EXPECT(ctx, rejected_instances.set_document(combined_block.document()) &&
        rejected_instances.bind_schema(combined_schema));
    TEST_EXPECT(ctx, !rejected_instances.load_supplied(CByteConstView{ expected_bytes, 2u, 128u }, false, instance_error) &&
        instance_error.reason == EInstanceLoadReason::invalid_count && !rejected_instances.loaded_ready() &&
        combined_schema.resolved_ready() && surviving_bulk.loaded_ready());
    SBulkEntryView surviving_entry;
    TEST_EXPECT(ctx, surviving_bulk.entry(surviving_bulk.find_entry(CStringView{ "Pair" }, CStringView{ "array" }),
        surviving_entry) && surviving_entry.bytes[0] == 1u && surviving_entry.bytes[1] == 2u);
}

static void test_bulk_encoded_comparison(TTestContext& ctx)
{
    const std::string text = R"({"types":{
        "structures":{"Padded":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u32"}}]}},
        "bit_structures":{
            "Bits":{"storage":"u16","members":[{"flag":{"type":"b8","mask":1}}]},
            "Norm":{"storage":"u8","members":[{"value":{
                "type":"i8","mask":3,"interpretation":"snorm"}}]}}}})";
    CBakedDocumentBlock block;
    CResolvedSchema resolved;
    if (!resolve(ctx, text, block, resolved)) return;
    const auto same = [&](const char* const type, const std::uint8_t* const left,
        const std::uint8_t* const right, const std::size_t size)
    {
        return detail::compare_encoded(resolved, resolved.find_type(CStringView{ type }),
            left, size, right, size);
    };
    const std::uint8_t plus_zero[4] = { 0u, 0u, 0u, 0u };
    const std::uint8_t minus_zero[4] = { 0u, 0u, 0u, 0x80u };
    const std::uint8_t nan_a[4] = { 1u, 0u, 0xc0u, 0x7fu };
    const std::uint8_t nan_b[4] = { 2u, 0u, 0x80u, 0xffu };
    TEST_EXPECT(ctx, !same("f32", plus_zero, minus_zero, 4u));
    TEST_EXPECT(ctx, same("f32", nan_a, nan_b, 4u));
    const std::uint8_t half_a[2] = { 1u, 0x7eu }, half_b[2] = { 2u, 0xfeu };
    TEST_EXPECT(ctx, same("f16", half_a, half_b, 2u));
    const std::uint8_t double_a[8] = { 1u, 0u, 0u, 0u, 0u, 0u, 0xf8u, 0x7fu };
    const std::uint8_t double_b[8] = { 2u, 0u, 0u, 0u, 0u, 0u, 0xf0u, 0xffu };
    TEST_EXPECT(ctx, same("f64", double_a, double_b, 8u));
    const std::uint8_t bool_a[1] = { 1u }, bool_b[1] = { 2u };
    TEST_EXPECT(ctx, !same("b8", bool_a, bool_b, 1u));
    const std::uint8_t bits_a[2] = { 1u, 0u }, bits_b[2] = { 1u, 0x80u };
    TEST_EXPECT(ctx, same("Bits", bits_a, bits_b, 2u));
    TEST_EXPECT(ctx, !same("Bits", bits_a, plus_zero, 2u));
    const std::uint8_t norm_a[1] = { 2u }, norm_b[1] = { 3u };
    TEST_EXPECT(ctx, !same("Norm", norm_a, norm_b, 1u));
    const std::uint8_t padded_a[8] = { 1u, 0u, 0u, 0u, 2u, 0u, 0u, 0u };
    const std::uint8_t padded_b[8] = { 1u, 255u, 255u, 255u, 2u, 0u, 0u, 0u };
    TEST_EXPECT(ctx, same("Padded", padded_a, padded_b, 8u));
}
static void test_live_bulk(TTestContext& ctx)
{
    const std::string source_definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Aligned":{"members":[{"v":{"type":"u16"}}]},
        "Empty":{"members":[]}}}})";
    const std::string destination_definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8","default":7}},{"b":{"type":"u8"}}]},
        "Aligned":{"members":[{"v":{"type":"u16"}}]},
        "Empty":{"members":[]},"Unused":{"members":[{"q":{"type":"u32"}}]}}}})";
    CBakedDocumentBlock source_schema_block, destination_schema_block;
    TEST_EXPECT(ctx, bake(source_definitions, source_schema_block));
    TEST_EXPECT(ctx, bake(destination_definitions, destination_schema_block));
    CBakedSchema source_schema, destination_schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, source_schema.set_document(source_schema_block.document()) && source_schema.resolve(schema_error));
    TEST_EXPECT(ctx, destination_schema.set_document(destination_schema_block.document()) &&
        destination_schema.resolve(schema_error));

    CLiveBulkData live;
    alignas(128) std::uint8_t authoritative[128] = { 9u, 8u, 7u, 6u };
    {
        const std::string bulk_text = R"({"instances":{"ignored":1},"data":{
            "Pair":{"old":{"locator":{"offset":0,"valid":true},
                "data":[{"a":1,"b":2},{"a":3,"b":4}]}},
            "Empty":{"zero":{"locator":{"offset":4,"valid":true},"data":[{}]}}}})";
        CBakedDocumentBlock source_block;
        TEST_EXPECT(ctx, bake(bulk_text, source_block));
        CBakedBulkData baked;
        TEST_EXPECT(ctx, baked.set_document(source_block.document()) && baked.bind_schema(source_schema));
        SBulkDiagnostic error;
        TEST_EXPECT(ctx, baked.load_supplied(CByteConstView{ authoritative, 4u, 128u }, false, error));
        TEST_EXPECT(ctx, baked.promote(live, destination_schema, error) && live.loaded_ready());
        TEST_EXPECT(ctx, !baked.promote(live, destination_schema, error) &&
            error.reason == EBulkLoadReason::invalid_input);
        TEST_EXPECT(ctx, baked.loaded_ready());
    }
    authoritative[0] = 0u;
    const CBulkDocumentQuery query = live.document_query();
    const CBulkHandle old = live.find_entry(CStringView{ "Pair" }, CStringView{ "old" });
    const CBulkHandle zero = live.find_entry(CStringView{ "Empty" }, CStringView{ "zero" });
    SBulkEntryView observed;
    TEST_EXPECT(ctx, live.entry(old, observed) && observed.count == 2u && observed.bytes[0] == 9u &&
        observed.bytes[3] == 6u);
    TEST_EXPECT(ctx, live.entry(zero, observed) && observed.count == 1u &&
        observed.byte_count == 0u && observed.bytes == nullptr);
    TEST_EXPECT(ctx, query.is_ready() && query.object_child(query.root(), CStringView{ "instances" }) == CBulkHandle{} &&
        query.object_child(query.root(), CStringView{ "data" }) == live.data_root());
    const CBulkHandle old_locator = query.object_child(old, CStringView{ "locator" });
    std::uint64_t retained_count{};
    TEST_EXPECT(ctx, query.object_child(old, CStringView{ "data" }) == CBulkHandle{} &&
        query.unsigned_integer_value(query.object_child(old_locator, CStringView{ "count" }),
            retained_count) && retained_count == 2u);

    SBulkDiagnostic error;
    alignas(128) std::uint8_t replacement[128] = { 3u, 4u, 5u, 6u, 7u, 8u };
    const CBulkHandle replaced = live.capture(CStringView{ "Pair" }, CStringView{ "old" },
        CByteConstView{ replacement, 2u, 128u }, 1u, error);
    TEST_EXPECT(ctx, replaced == old && live.entry(old, observed) && observed.count == 1u &&
        observed.offset == 0u && observed.bytes[0] == 3u);
    const CBulkHandle grown = live.capture(CStringView{ "Pair" }, CStringView{ "old" },
        CByteConstView{ replacement, 6u, 128u }, 3u, error);
    TEST_EXPECT(ctx, grown == old && live.entry(old, observed) && observed.count == 3u &&
        observed.offset >= 4u && observed.bytes[5] == 8u);

    const CBulkHandle unpopulated = live.create_unpopulated(CStringView{ "Aligned" }, CStringView{ "later" }, 1u, error);
    SMutableBulkEntryView writable;
    TEST_EXPECT(ctx, unpopulated && live.mutable_entry(unpopulated, writable) && writable.count == 1u &&
        writable.byte_count == 2u && writable.bytes.is_ready() && writable.bytes.align() >= 2u);
    if (writable.bytes.is_ready())
    {
        writable.bytes.data()[0] = 0x34u;
        writable.bytes.data()[1] = 0x12u;
    }
    TEST_EXPECT(ctx, live.entry(unpopulated, observed) && observed.bytes[0] == 0x34u && observed.bytes[1] == 0x12u);
    TEST_EXPECT(ctx, !live.create_unpopulated(CStringView{ "Aligned" }, CStringView{ "later" }, 1u, error) &&
        error.reason == EBulkLoadReason::invalid_input);
    const CBulkHandle empty = live.create_unpopulated(CStringView{ "Empty" }, CStringView{ "unused" }, 0u, error);
    TEST_EXPECT(ctx, empty && live.entry(empty, observed) && observed.count == 0u && observed.bytes == nullptr);
    const CBulkHandle huge_empty = live.create_unpopulated(CStringView{ "Empty" }, CStringView{ "huge" }, UINT32_MAX, error);
    TEST_EXPECT(ctx, huge_empty && live.entry(huge_empty, observed) && observed.count == UINT32_MAX &&
        observed.byte_count == 0u && observed.bytes == nullptr);
    const CBulkHandle empty_pair = live.create_unpopulated(CStringView{ "Pair" }, CStringView{ "no_records" }, 0u, error);
    TEST_EXPECT(ctx, empty_pair && live.entry(empty_pair, observed) && observed.count == 0u &&
        observed.stride == 2u && observed.byte_count == 0u);
    TEST_EXPECT(ctx, live.capture(CStringView{ "Pair" }, CStringView{ "no_records" }, {}, 0u, error) == empty_pair &&
        live.entry(empty_pair, observed) && observed.count == 0u && observed.offset == 0u);
    TEST_EXPECT(ctx, live.rename_entry(unpopulated, CStringView{ "renamed" }) &&
        live.find_entry(CStringView{ "Aligned" }, CStringView{ "renamed" }) == unpopulated);
    TEST_EXPECT(ctx, !live.rename_entry(empty, CStringView{ "huge" }) && live.entry(empty, observed));
    TEST_EXPECT(ctx, live.erase_entry(unpopulated) && !live.entry(unpopulated, observed));

    CBakedDocumentBlock records_block;
    TEST_EXPECT(ctx, bake(R"({"good":[{"a":1,"b":2}],"bad":[[1]],"named":[{"": [1,2]}]})", records_block));
    const CBulkDocumentQuery records{ records_block.document() };
    const CBulkHandle good_array = records.object_child(records.root(), CStringView{ "good" });
    const CBulkHandle bad_array = records.object_child(records.root(), CStringView{ "bad" });
    TEST_EXPECT(ctx, live.create_records(CStringView{ "Pair" }, CStringView{ "records" },
        records, good_array, error));
    const CBulkHandle incomplete = live.create_records(CStringView{ "Pair" }, CStringView{ "incomplete" },
        records, bad_array, error);
    TEST_EXPECT(ctx, !incomplete && error.reason == EBulkLoadReason::incomplete_record);
    TEST_EXPECT(ctx, !live.find_entry(CStringView{ "Pair" }, CStringView{ "incomplete" }));
    TEST_EXPECT(ctx, !live.create_records(CStringView{ "Pair" }, CStringView{ "named_outer" },
        records, records.object_child(records.root(), CStringView{ "named" }), error) &&
        error.reason == EBulkLoadReason::invalid_input);
    CLiveDocument live_records;
    TEST_EXPECT(ctx, parse_live(R"({"items":[[3,4]]})", live_records));
    const CBulkDocumentQuery live_record_query{ live_records };
    TEST_EXPECT(ctx, live.create_records(CStringView{ "Pair" }, CStringView{ "from_live" },
        live_record_query, live_record_query.object_child(live_record_query.root(), CStringView{ "items" }), error));

    alignas(128) std::uint8_t filler_bytes[2400]{};
    for (std::size_t index = 0u; index < sizeof(filler_bytes); ++index)
    {
        filler_bytes[index] = static_cast<std::uint8_t>(index & 0xffu);
    }
    TEST_EXPECT(ctx, live.capture(CStringView{ "Pair" }, CStringView{ "filler" },
        CByteConstView{ filler_bytes, sizeof(filler_bytes), 128u }, 1200u, error));
    const CByteConstView self = live.payload_view();
    const std::uint8_t first_byte = self.data()[0];
    const std::uint8_t later_byte = self.data()[5];
    const std::uint32_t self_count = static_cast<std::uint32_t>(self.size() / 2u);
    const CBulkHandle self_capture = live.capture(CStringView{ "Pair" }, CStringView{ "old" }, self, self_count, error);
    TEST_EXPECT(ctx, self_capture == old && live.entry(old, observed) &&
        observed.count == self_count && observed.bytes[0] == first_byte && observed.bytes[5] == later_byte &&
        live.payload_view().size() > 4096u);

    const std::string incompatible_definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"i8"}},{"b":{"type":"u8"}}]},
        "Empty":{"members":[]}}}})";
    CBakedDocumentBlock incompatible_block, mismatch_bulk_block;
    TEST_EXPECT(ctx, bake(incompatible_definitions, incompatible_block));
    CBakedSchema incompatible_schema;
    TEST_EXPECT(ctx, incompatible_schema.set_document(incompatible_block.document()) &&
        incompatible_schema.resolve(schema_error));
    TEST_EXPECT(ctx, bake(R"({"data":{"Pair":{"one":{"locator":{"offset":0,"valid":true,"count":1}}}}})",
        mismatch_bulk_block));
    CBakedBulkData mismatch_source;
    TEST_EXPECT(ctx, mismatch_source.set_document(mismatch_bulk_block.document()) &&
        mismatch_source.bind_schema(source_schema) &&
        mismatch_source.load_supplied(CByteConstView{ replacement, 2u, 128u }, false, error));
    CLiveBulkData rejected;
    TEST_EXPECT(ctx, !mismatch_source.promote(rejected, incompatible_schema, error) &&
        error.reason == EBulkLoadReason::incompatible_schema && !rejected.document_ready());

    CLiveBulkData outliving;
    {
        CLiveDocument temporary_document;
        TEST_EXPECT(ctx, parse_live(source_definitions, temporary_document));
        CLiveSchema temporary;
        TEST_EXPECT(ctx, temporary.try_adopt(std::move(temporary_document)) && temporary.resolve(schema_error));
        CLiveBulkData promoted_to_live_schema;
        TEST_EXPECT(ctx, mismatch_source.promote(promoted_to_live_schema, temporary, error) &&
            promoted_to_live_schema.loaded_ready());
        TEST_EXPECT(ctx, outliving.initialise(temporary));
        TEST_EXPECT(ctx, outliving.create_unpopulated(CStringView{ "Pair" }, CStringView{ "orphan" }, 0u, error));
    }
    TEST_EXPECT(ctx, !outliving.loaded_ready() && outliving.document_query().is_ready() &&
        !outliving.find_entry(CStringView{ "Pair" }, CStringView{ "orphan" }));

    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveDocument schema_document;
            TEST_EXPECT(ctx, parse_live(source_definitions, schema_document));
            CLiveSchema live_schema;
            TEST_EXPECT(ctx, live_schema.try_adopt(std::move(schema_document)) && live_schema.resolve(schema_error));
            CLiveBulkData live_bound;
            TEST_EXPECT(ctx, live_bound.initialise(live_schema));
            const CBulkHandle retained = live_bound.create_unpopulated(CStringView{ "Pair" }, CStringView{ "retained" }, 0u, error);
            TEST_EXPECT(ctx, retained);
            failing.fail_on = failing.calls;
            TEST_EXPECT(ctx, !live_schema.resolve(schema_error) && !live_bound.loaded_ready() &&
                live_bound.document_query().is_ready() && !live_bound.entry(retained, observed));
            failing.fail_on = SIZE_MAX;
            TEST_EXPECT(ctx, live_schema.resolve(schema_error) && live_bound.loaded_ready() &&
                live_bound.entry(retained, observed) && observed.count == 0u);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

static void test_live_bulk_output(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{
        "structures":{
            "Padded":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u32"}}]},
            "Aligned":{"members":[{"word":{"type":"u32"}}]},
            "Empty":{"members":[]}},
        "enumerations":{"Mode":{"storage":"u8","values":{"first":2,"alias":2,"last":7}}}}})";
    const std::string changed_defaults = R"({"types":{
        "structures":{
            "Padded":{"members":[{"a":{"type":"u8","default":9}},{"b":{"type":"u32"}}]},
            "Aligned":{"members":[{"word":{"type":"u32"}}]},
            "Empty":{"members":[]}},
        "enumerations":{"Mode":{"storage":"u8","values":{"first":2,"alias":2,"last":7}}}}})";
    CBakedDocumentBlock schema_block, destination_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block) && bake(changed_defaults, destination_block));
    CBakedSchema schema, destination_schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    TEST_EXPECT(ctx, destination_schema.set_document(destination_block.document()) && destination_schema.resolve(schema_error));
    CLiveBulkData source;
    SBulkDiagnostic error;
    TEST_EXPECT(ctx, source.initialise(schema));
    alignas(128) std::uint8_t first[128] = { 1u, 0xaau, 0xbbu, 0xccu, 2u, 0u, 0u, 0u };
    alignas(128) std::uint8_t replacement[128] = {
        3u, 0x11u, 0x22u, 0x33u, 4u, 0u, 0u, 0u,
        5u, 0x44u, 0x55u, 0x66u, 6u, 0u, 0u, 0u
    };
    alignas(128) std::uint8_t mode[128] = { 2u };
    alignas(128) std::uint8_t aligned[128] = { 0x12u, 0x34u, 0x56u, 0x78u };
    const CBulkHandle old = source.capture(CStringView{ "Padded" }, CStringView{ "old" },
        CByteConstView{ first, 8u, 128u }, 1u, error);
    const CBulkHandle erased = source.capture(CStringView{ "Padded" }, CStringView{ "erased" },
        CByteConstView{ first, 8u, 128u }, 1u, error);
    const CBulkHandle empty = source.create_unpopulated(CStringView{ "Empty" }, CStringView{ "zero" }, 1u, error);
    const CBulkHandle selected_mode = source.capture(CStringView{ "Mode" }, CStringView{ "choice" },
        CByteConstView{ mode, 1u, 128u }, 1u, error);
    const CBulkHandle after = source.capture(CStringView{ "Padded" }, CStringView{ "after" },
        CByteConstView{ first, 8u, 128u }, 1u, error);
    const CBulkHandle aligned_entry = source.capture(CStringView{ "Aligned" }, CStringView{ "word" },
        CByteConstView{ aligned, 4u, 128u }, 1u, error);
    TEST_EXPECT(ctx, old && erased && empty && selected_mode && after && aligned_entry && source.erase_entry(erased));
    TEST_EXPECT(ctx, source.capture(CStringView{ "Padded" }, CStringView{ "old" },
        CByteConstView{ replacement, 16u, 128u }, 2u, error) == old);
    SBulkEntryView source_view;
    TEST_EXPECT(ctx, source.entry(old, source_view) && (source_view.offset > 0u));
    const std::size_t source_size = source.payload_view().size();
    const std::size_t source_bindings = schema.reference_count();
    CLiveDocument prepared;
    CByteBuffer packed;
    TEST_EXPECT(ctx, source.prepare_output(prepared, packed, destination_schema, EDataOutputForm::embedded, error));
    TEST_EXPECT(ctx, (schema.reference_count() == source_bindings) && (destination_schema.reference_count() == 0u) &&
        (source.payload_view().size() == source_size) && (packed.size() == 32u));
    const CBulkDocumentQuery prepared_query{ prepared };
    const CBulkHandle data = prepared_query.object_child(prepared_query.root(), CStringView{ "data" });
    const CBulkHandle padded_group = prepared_query.object_child(data, CStringView{ "Padded" });
    const CBulkHandle output_old = prepared_query.object_child(padded_group, CStringView{ "old" });
    const CBulkHandle output_empty = prepared_query.object_child(
        prepared_query.object_child(data, CStringView{ "Empty" }), CStringView{ "zero" });
    TEST_EXPECT(ctx, prepared_query.child_count(padded_group) == 2u &&
        prepared_query.child_count(prepared_query.object_child(output_old, CStringView{ "data" })) == 2u &&
        prepared_query.child_count(prepared_query.object_child(output_empty, CStringView{ "data" })) == 1u &&
        std::memcmp(packed.data(), replacement, 16u) == 0 &&
        std::memcmp(packed.data() + 16u, first, 8u) == 0 && (packed.data()[24] == 2u) &&
        std::memcmp(packed.data() + 28u, aligned, 4u) == 0);

    CBakedDocumentBlock prepared_block;
    TEST_EXPECT(ctx, document_translation::bake(prepared, prepared_block));
    CBakedBulkData supplied;
    TEST_EXPECT(ctx, supplied.set_document(prepared_block.document()) && supplied.bind_schema(destination_schema) &&
        supplied.load_supplied(packed.const_view(), true, error));
    SBulkEntryView output_view;
    TEST_EXPECT(ctx, supplied.entry(supplied.find_entry(CStringView{ "Padded" }, CStringView{ "old" }), output_view) &&
        (output_view.offset == 0u) && (output_view.count == 2u) &&
        std::memcmp(output_view.bytes, replacement, 16u) == 0);
    TEST_EXPECT(ctx, supplied.entry(supplied.find_entry(CStringView{ "Padded" }, CStringView{ "after" }), output_view) &&
        (output_view.offset == 16u) && (output_view.count == 1u) &&
        std::memcmp(output_view.bytes, first, 8u) == 0);
    TEST_EXPECT(ctx, supplied.entry(supplied.find_entry(CStringView{ "Mode" }, CStringView{ "choice" }), output_view) &&
        (output_view.offset == 24u) && (output_view.count == 1u) && (output_view.bytes[0] == 2u));
    TEST_EXPECT(ctx, supplied.entry(supplied.find_entry(CStringView{ "Aligned" }, CStringView{ "word" }), output_view) &&
        (output_view.offset == 28u) && (output_view.count == 1u) &&
        std::memcmp(output_view.bytes, aligned, 4u) == 0);
    TEST_EXPECT(ctx, supplied.entry(supplied.find_entry(CStringView{ "Empty" }, CStringView{ "zero" }), output_view) &&
        (output_view.offset == 0u) && (output_view.count == 1u) && (output_view.byte_count == 0u));

    for (const EDocumentWriteMode write_mode : { EDocumentWriteMode::strict_json, EDocumentWriteMode::morphic })
    {
        CDocumentWriteOptions options;
        options.mode = write_mode;
        const CDocumentWriteResult written = document_writer::write(prepared_block.document(), options);
        TEST_EXPECT(ctx, written.report.succeeded());
        if (!written.report.succeeded())
        {
            continue;
        }
        const std::string text{ reinterpret_cast<const char*>(written.output.data()),
            written.report.logical_text_byte_size };
        CLiveDocument reparsed;
        CBakedDocumentBlock reparsed_block;
        TEST_EXPECT(ctx, parse_live(text, reparsed) && document_translation::bake(reparsed, reparsed_block));
        CBakedBulkData reloaded;
        CByteBuffer materialised;
        TEST_EXPECT(ctx, reloaded.set_document(reparsed_block.document()) && reloaded.bind_schema(destination_schema) &&
            reloaded.materialise(materialised, error));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Padded" }, CStringView{ "old" }), output_view) &&
            (output_view.count == 2u) && detail::compare_encoded(*destination_schema.resolved(), output_view.type,
                output_view.bytes, 8u, replacement, 8u) &&
            detail::compare_encoded(*destination_schema.resolved(), output_view.type,
                (output_view.bytes + 8u), 8u, (replacement + 8u), 8u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Padded" }, CStringView{ "after" }), output_view) &&
            (output_view.offset == 16u) && detail::compare_encoded(*destination_schema.resolved(), output_view.type,
                output_view.bytes, 8u, first, 8u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Empty" }, CStringView{ "zero" }), output_view) &&
            (output_view.offset == 0u) && (output_view.count == 1u) && (output_view.byte_count == 0u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Mode" }, CStringView{ "choice" }), output_view) &&
            (output_view.offset == 24u) && (output_view.bytes[0] == 2u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Aligned" }, CStringView{ "word" }), output_view) &&
            (output_view.offset == 28u) && std::memcmp(output_view.bytes, aligned, 4u) == 0);
    }

    CBakedDocumentBlock demoted_block;
    CByteBuffer demoted_payload;
    CBakedBulkData demoted;
    const std::size_t destination_bindings = destination_schema.reference_count();
    TEST_EXPECT(ctx, source.demote(demoted_block, demoted_payload, demoted, destination_schema,
        EDataOutputForm::external, error));
    TEST_EXPECT(ctx, demoted.loaded_ready() &&
        (destination_schema.reference_count() == destination_bindings + 1u) &&
        (schema.reference_count() == source_bindings) &&
        (demoted.payload_view().data() == demoted_payload.data()) &&
        (demoted.document_query().object_child(demoted.find_entry(CStringView{ "Padded" }, CStringView{ "old" }),
            CStringView{ "data" }) == CBulkHandle{}));
    for (const EDocumentWriteMode write_mode : { EDocumentWriteMode::strict_json, EDocumentWriteMode::morphic })
    {
        CDocumentWriteOptions options;
        options.mode = write_mode;
        const CDocumentWriteResult written = document_writer::write(demoted_block.document(), options);
        TEST_EXPECT(ctx, written.report.succeeded());
        if (!written.report.succeeded())
        {
            continue;
        }
        const std::string text{ reinterpret_cast<const char*>(written.output.data()),
            written.report.logical_text_byte_size };
        CLiveDocument reparsed;
        CBakedDocumentBlock reparsed_block;
        TEST_EXPECT(ctx, parse_live(text, reparsed) && document_translation::bake(reparsed, reparsed_block));
        CBakedBulkData reloaded;
        TEST_EXPECT(ctx, reloaded.set_document(reparsed_block.document()) && reloaded.bind_schema(destination_schema) &&
            reloaded.load_supplied(demoted_payload.const_view(), false, error));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Padded" }, CStringView{ "after" }), output_view) &&
            (output_view.offset == 16u) && std::memcmp(output_view.bytes, first, 8u) == 0);
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Mode" }, CStringView{ "choice" }), output_view) &&
            (output_view.offset == 24u) && (output_view.bytes[0] == 2u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Aligned" }, CStringView{ "word" }), output_view) &&
            (output_view.offset == 28u) && std::memcmp(output_view.bytes, aligned, 4u) == 0);
    }
    const std::uint8_t* const stable_address = demoted_payload.data();
    CByteBuffer transferred_payload = std::move(demoted_payload);
    CBakedDocumentBlock transferred_block = std::move(demoted_block);
    TEST_EXPECT(ctx, (demoted.payload_view().data() == stable_address) &&
        (transferred_payload.data() == stable_address) && demoted.document_query().is_ready());
    source.clear();
    TEST_EXPECT(ctx, demoted.loaded_ready() && demoted.entry(
        demoted.find_entry(CStringView{ "Padded" }, CStringView{ "old" }), output_view) &&
        (output_view.count == 2u) && (output_view.bytes[0] == 3u));
}

static void test_live_bulk_output_boundaries(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{
        "structures":{
            "Single":{"members":[{"only":{"type":"u8"}}]},
            "Nested":{"members":[
                {"norms":{"type":{"element":"Norm","count":2}}},
                {"singles":{"type":{"element":"Single","count":2}}}]}},
        "enumerations":{"Mode":{"storage":"u8","values":{"first":2,"alias":2}}},
        "bit_structures":{"Norm":{"storage":"u8","members":[{"value":{
            "type":"i8","mask":3,"interpretation":"snorm"}}]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    SBulkDiagnostic error;
    CLiveBulkData empty;
    TEST_EXPECT(ctx, empty.initialise(schema));
    CLiveDocument empty_document;
    CByteBuffer empty_payload;
    TEST_EXPECT(ctx, empty.prepare_output(empty_document, empty_payload, schema, EDataOutputForm::embedded, error) &&
        empty_document.is_ready() && (empty_payload.size() == 0u));
    CBakedDocumentBlock empty_block;
    CByteBuffer empty_demoted_payload;
    CBakedBulkData empty_role;
    TEST_EXPECT(ctx, empty.demote(empty_block, empty_demoted_payload, empty_role, schema,
        EDataOutputForm::external, error) && empty_role.loaded_ready());

    CLiveBulkData source;
    TEST_EXPECT(ctx, source.initialise(schema));
    alignas(128) std::uint8_t signed_zero[128] = { 0u, 0u, 0u, 0x80u };
    alignas(128) std::uint8_t nan[128] = { 1u, 0u, 0xc0u, 0x7fu };
    alignas(128) std::uint8_t norm[128] = { 2u };
    alignas(128) std::uint8_t known[128] = { 2u };
    alignas(128) std::uint8_t nested[128] = { 2u, 1u, 7u, 8u };
    TEST_EXPECT(ctx, source.capture(CStringView{ "f32" }, CStringView{ "negative_zero" },
        CByteConstView{ signed_zero, 4u, 128u }, 1u, error));
    TEST_EXPECT(ctx, source.capture(CStringView{ "f32" }, CStringView{ "nan" },
        CByteConstView{ nan, 4u, 128u }, 1u, error));
    TEST_EXPECT(ctx, source.capture(CStringView{ "Norm" }, CStringView{ "raw_code" },
        CByteConstView{ norm, 1u, 128u }, 1u, error));
    TEST_EXPECT(ctx, source.capture(CStringView{ "Mode" }, CStringView{ "alias" },
        CByteConstView{ known, 1u, 128u }, 1u, error));
    TEST_EXPECT(ctx, source.capture(CStringView{ "Nested" }, CStringView{ "arrays" },
        CByteConstView{ nested, 4u, 128u }, 1u, error));
    CLiveDocument embedded;
    CByteBuffer embedded_payload;
    TEST_EXPECT(ctx, source.prepare_output(embedded, embedded_payload, schema, EDataOutputForm::embedded, error));
    CBakedDocumentBlock embedded_block;
    TEST_EXPECT(ctx, document_translation::bake(embedded, embedded_block));
    CBakedBulkData embedded_role;
    TEST_EXPECT(ctx, embedded_role.set_document(embedded_block.document()) && embedded_role.bind_schema(schema) &&
        embedded_role.load_supplied(embedded_payload.const_view(), true, error));
    const CBulkDocumentQuery query{ embedded };
    const CBulkHandle data = query.object_child(query.root(), CStringView{ "data" });
    const CBulkHandle mode_entry = query.object_child(query.object_child(data, CStringView{ "Mode" }),
        CStringView{ "alias" });
    const CBulkHandle mode_array = query.object_child(mode_entry, CStringView{ "data" });
    TEST_EXPECT(ctx, query.string_value(query.array_at(mode_array, 0u)) == CStringView{ "first" });
    const CBulkHandle norm_entry = query.object_child(query.object_child(data, CStringView{ "Norm" }),
        CStringView{ "raw_code" });
    const CBulkHandle norm_record = query.array_at(query.object_child(norm_entry, CStringView{ "data" }), 0u);
    std::int64_t raw_code{};
    TEST_EXPECT(ctx, query.signed_integer_value(query.array_at(norm_record, 0u), raw_code) &&
        (raw_code == -2));
    SBulkEntryView view;
    TEST_EXPECT(ctx, embedded_role.entry(embedded_role.find_entry(CStringView{ "f32" },
        CStringView{ "negative_zero" }), view) && std::memcmp(view.bytes, signed_zero, 4u) == 0);
    for (const EDocumentWriteMode write_mode : { EDocumentWriteMode::strict_json, EDocumentWriteMode::morphic })
    {
        CDocumentWriteOptions options;
        options.mode = write_mode;
        const CDocumentWriteResult written = document_writer::write(embedded_block.document(), options);
        TEST_EXPECT(ctx, written.report.succeeded());
        if (!written.report.succeeded())
        {
            continue;
        }
        const std::string text{ reinterpret_cast<const char*>(written.output.data()),
            written.report.logical_text_byte_size };
        CLiveDocument reparsed;
        CBakedDocumentBlock reparsed_block;
        TEST_EXPECT(ctx, parse_live(text, reparsed) && document_translation::bake(reparsed, reparsed_block));
        CBakedBulkData reloaded;
        CByteBuffer materialised;
        TEST_EXPECT(ctx, reloaded.set_document(reparsed_block.document()) && reloaded.bind_schema(schema) &&
            reloaded.materialise(materialised, error));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "f32" },
            CStringView{ "negative_zero" }), view) && std::memcmp(view.bytes, signed_zero, 4u) == 0);
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "f32" }, CStringView{ "nan" }), view) &&
            detail::compare_encoded(*schema.resolved(), view.type, view.bytes, 4u, nan, 4u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Norm" },
            CStringView{ "raw_code" }), view) && (view.bytes[0] == 2u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Mode" },
            CStringView{ "alias" }), view) && (view.bytes[0] == 2u));
        TEST_EXPECT(ctx, reloaded.entry(reloaded.find_entry(CStringView{ "Nested" },
            CStringView{ "arrays" }), view) &&
            detail::compare_encoded(*schema.resolved(), view.type, view.bytes, 4u, nested, 4u));
    }

    alignas(128) std::uint8_t noncanonical_bool[128] = { 2u };
    const CBulkHandle bad_bool = source.capture(CStringView{ "b8" }, CStringView{ "bad" },
        CByteConstView{ noncanonical_bool, 1u, 128u }, 1u, error);
    CLiveDocument rejected;
    CByteBuffer rejected_payload;
    TEST_EXPECT(ctx, bad_bool && !source.prepare_output(rejected, rejected_payload, schema,
        EDataOutputForm::embedded, error) && (error.reason == EBulkLoadReason::unrepresentable_value) &&
        (error.occurrence == bad_bool) && !rejected.is_ready() && !rejected_payload.is_ready());
    TEST_EXPECT(ctx, source.prepare_output(rejected, rejected_payload, schema, EDataOutputForm::external, error));
    rejected.deallocate();
    rejected_payload.deallocate();
    TEST_EXPECT(ctx, source.erase_entry(bad_bool));
    alignas(128) std::uint8_t unknown_code[128] = { 9u };
    const CBulkHandle bad_enum = source.capture(CStringView{ "Mode" }, CStringView{ "unknown" },
        CByteConstView{ unknown_code, 1u, 128u }, 1u, error);
    TEST_EXPECT(ctx, bad_enum && !source.prepare_output(rejected, rejected_payload, schema,
        EDataOutputForm::embedded, error) && (error.reason == EBulkLoadReason::unrepresentable_value) &&
        (error.occurrence == bad_enum) && !rejected.is_ready() && !rejected_payload.is_ready());
    TEST_EXPECT(ctx, source.prepare_output(rejected, rejected_payload, schema, EDataOutputForm::external, error));
    rejected.deallocate();
    rejected_payload.deallocate();

    const CBulkHandle placeholder = source.create_unpopulated(CStringView{ "Mode" }, CStringView{ "zero" }, 0u, error);
    TEST_EXPECT(ctx, placeholder && !source.prepare_output(rejected, rejected_payload, schema,
        EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::invalid_input) &&
        (error.occurrence == placeholder));
    TEST_EXPECT(ctx, source.erase_entry(placeholder));
    TEST_EXPECT(ctx, !source.prepare_output(rejected, rejected_payload, schema,
        static_cast<EDataOutputForm>(255u), error) && (error.reason == EBulkLoadReason::invalid_input));

    const std::string incompatible = R"({"types":{
        "structures":{
            "Single":{"members":[{"only":{"type":"u8"}}]},
            "Nested":{"members":[
                {"norms":{"type":{"element":"Norm","count":2}}},
                {"singles":{"type":{"element":"Single","count":2}}}]}},
        "enumerations":{"Mode":{"storage":"u8","values":{"first":3,"alias":3}}},
        "bit_structures":{"Norm":{"storage":"u8","members":[{"value":{
            "type":"i8","mask":3,"interpretation":"snorm"}}]}}}})";
    CBakedDocumentBlock incompatible_block;
    CBakedSchema incompatible_schema;
    TEST_EXPECT(ctx, bake(incompatible, incompatible_block) &&
        incompatible_schema.set_document(incompatible_block.document()) && incompatible_schema.resolve(schema_error));
    TEST_EXPECT(ctx, !source.prepare_output(rejected, rejected_payload, incompatible_schema,
        EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::incompatible_schema));
    TEST_EXPECT(ctx, !rejected.is_ready() && !rejected_payload.is_ready());

    CLiveDocument occupied_document;
    TEST_EXPECT(ctx, parse_live(R"({"sentinel":17})", occupied_document));
    TEST_EXPECT(ctx, !source.prepare_output(occupied_document, rejected_payload, schema,
        EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::invalid_input) &&
        occupied_document.object_child(occupied_document.root(), CStringView{ "sentinel" }));
    CByteBuffer occupied_prepared_payload;
    TEST_EXPECT(ctx, occupied_prepared_payload.allocate(1u, 128u) && occupied_prepared_payload.set_size(1u));
    TEST_EXPECT(ctx, !source.prepare_output(rejected, occupied_prepared_payload, schema,
        EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::invalid_input) &&
        !rejected.is_ready() && (occupied_prepared_payload.size() == 1u));
    CBakedSchema unresolved_schema;
    TEST_EXPECT(ctx, unresolved_schema.set_document(schema_block.document()) &&
        !source.prepare_output(rejected, rejected_payload, unresolved_schema,
            EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::invalid_input));

    CLiveDocument live_schema_document;
    CLiveSchema live_schema;
    TEST_EXPECT(ctx, parse_live(definitions, live_schema_document) && live_schema.try_adopt(std::move(live_schema_document)) &&
        live_schema.resolve(schema_error));
    TEST_EXPECT(ctx, source.prepare_output(rejected, rejected_payload, live_schema,
        EDataOutputForm::external, error));
    CBakedDocumentBlock occupied_block;
    CByteBuffer occupied_payload;
    CBakedBulkData occupied_role;
    TEST_EXPECT(ctx, source.demote(occupied_block, occupied_payload, occupied_role, live_schema,
        EDataOutputForm::external, error));
    const std::size_t bindings = live_schema.reference_count();
    TEST_EXPECT(ctx, !source.demote(occupied_block, occupied_payload, occupied_role, live_schema,
        EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::invalid_input) &&
        (live_schema.reference_count() == bindings) && occupied_role.loaded_ready());
    CBakedDocumentBlock blocked_block;
    CByteBuffer blocked_payload;
    CBakedBulkData blocked_role;
    TEST_EXPECT(ctx, !source.demote(blocked_block, blocked_payload, blocked_role, incompatible_schema,
        EDataOutputForm::external, error) && (error.reason == EBulkLoadReason::incompatible_schema) &&
        !blocked_block.is_ready() && !blocked_payload.is_ready() && !blocked_role.document_ready());
}

static void test_live_bulk_output_allocations(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(definitions, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    CLiveBulkData source;
    SBulkDiagnostic error;
    alignas(128) std::uint8_t bytes[128] = { 5u, 7u };
    TEST_EXPECT(ctx, source.initialise(schema));
    const CBulkHandle original = source.capture(CStringView{ "Pair" }, CStringView{ "original" },
        CByteConstView{ bytes, 2u, 128u }, 1u, error);
    TEST_EXPECT(ctx, original);
    const std::size_t bindings = schema.reference_count();
    std::size_t prepare_failures{}, prepare_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 48u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            failing.fail_on = failing.calls + failure_offset;
            CLiveDocument output;
            CByteBuffer payload;
            const bool prepared = source.prepare_output(output, payload, schema, EDataOutputForm::embedded, error);
            SBulkEntryView observed;
            TEST_EXPECT(ctx, source.loaded_ready() && source.entry(original, observed) &&
                (observed.bytes[0] == 5u) && (schema.reference_count() == bindings));
            if (prepared)
            {
                ++prepare_successes;
                TEST_EXPECT(ctx, output.is_ready() && (payload.size() == 2u));
            }
            else
            {
                ++prepare_failures;
                TEST_EXPECT(ctx, (error.reason == EBulkLoadReason::allocation_failed) &&
                    !output.is_ready() && !payload.is_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, (prepare_failures >= 3u) && (prepare_successes >= 1u));

    std::size_t demote_failures{}, demote_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 80u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            failing.fail_on = failing.calls + failure_offset;
            CBakedDocumentBlock block;
            CByteBuffer payload;
            CBakedBulkData role;
            const bool demoted = source.demote(block, payload, role, schema, EDataOutputForm::external, error);
            TEST_EXPECT(ctx, source.loaded_ready());
            if (demoted)
            {
                ++demote_successes;
                TEST_EXPECT(ctx, block.is_ready() && role.loaded_ready() &&
                    (role.payload_view().data() == payload.data()) &&
                    (schema.reference_count() == bindings + 1u));
            }
            else
            {
                ++demote_failures;
                TEST_EXPECT(ctx, (error.reason == EBulkLoadReason::allocation_failed) &&
                    !block.is_ready() && !payload.is_ready() && !role.document_ready() &&
                    (schema.reference_count() == bindings));
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && (schema.reference_count() == bindings));
    }
    TEST_EXPECT(ctx, (demote_failures >= 3u) && (demote_successes >= 1u));
}

static void test_live_bulk_failures_and_matching(TTestContext& ctx)
{
    const std::string pair_schema = R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Aligned":{"members":[{"v":{"type":"u16"}}]}}}})";
    CBakedDocumentBlock schema_block;
    TEST_EXPECT(ctx, bake(pair_schema, schema_block));
    CBakedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, schema.set_document(schema_block.document()) && schema.resolve(schema_error));
    alignas(128) std::uint8_t bytes[128] = { 1u, 2u, 3u, 4u, 5u, 6u };
    CLiveBulkData boundaries;
    SBulkDiagnostic boundary_error;
    TEST_EXPECT(ctx, boundaries.initialise(schema));
    TEST_EXPECT(ctx, !boundaries.capture(CStringView{ "Aligned" }, CStringView{ "short" },
        CByteConstView{ bytes, 1u, 128u }, 1u, boundary_error) &&
        boundary_error.reason == EBulkLoadReason::invalid_range);
    TEST_EXPECT(ctx, !boundaries.capture(CStringView{ "Aligned" }, CStringView{ "misaligned" },
        CByteConstView{ bytes + 1u, 2u, 1u }, 1u, boundary_error) &&
        boundary_error.reason == EBulkLoadReason::invalid_range);
    TEST_EXPECT(ctx, !boundaries.create_unpopulated(CStringView{ "Pair" }, CStringView{ "overflow" },
        UINT32_MAX, boundary_error) && boundary_error.reason == EBulkLoadReason::invalid_range);
    std::size_t rejected{}, accepted{};
    for (std::size_t failure_offset = 0u; failure_offset < 20u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveBulkData attempt;
            SBulkDiagnostic error;
            TEST_EXPECT(ctx, attempt.initialise(schema));
            const CBulkHandle prior = attempt.capture(CStringView{ "Pair" }, CStringView{ "prior" },
                CByteConstView{ bytes, 2u, 128u }, 1u, error);
            TEST_EXPECT(ctx, prior);
            failing.fail_on = failing.calls + failure_offset;
            const CBulkHandle next = attempt.capture(CStringView{ "Pair" }, CStringView{ "next" },
                CByteConstView{ bytes, 6u, 128u }, 3u, error);
            SBulkEntryView before;
            TEST_EXPECT(ctx, attempt.entry(prior, before) && before.count == 1u && before.bytes[0] == 1u);
            if (next)
            {
                ++accepted;
                TEST_EXPECT(ctx, attempt.entry(next, before) && before.count == 3u);
            }
            else
            {
                ++rejected;
                TEST_EXPECT(ctx, error.reason == EBulkLoadReason::allocation_failed &&
                    !attempt.find_entry(CStringView{ "Pair" }, CStringView{ "next" }));
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, rejected >= 3u && accepted >= 1u);

    std::size_t replacement_failures{}, replacement_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 24u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveBulkData attempt;
            SBulkDiagnostic error;
            TEST_EXPECT(ctx, attempt.initialise(schema));
            const CBulkHandle retained = attempt.capture(CStringView{ "Pair" }, CStringView{ "retained" },
                CByteConstView{ bytes, 2u, 128u }, 1u, error);
            TEST_EXPECT(ctx, retained);
            failing.fail_on = failing.calls + failure_offset;
            const CBulkHandle result = attempt.capture(CStringView{ "Pair" }, CStringView{ "retained" },
                CByteConstView{ bytes, 6u, 128u }, 3u, error);
            SBulkEntryView observed;
            if (result)
            {
                ++replacement_successes;
                TEST_EXPECT(ctx, result == retained && attempt.entry(retained, observed) && observed.count == 3u &&
                    observed.bytes[5] == 6u);
            }
            else
            {
                ++replacement_failures;
                TEST_EXPECT(ctx, error.reason == EBulkLoadReason::allocation_failed && attempt.loaded_ready() &&
                    attempt.entry(retained, observed) && observed.count == 1u && observed.offset == 0u &&
                    observed.bytes[0] == 1u && observed.bytes[1] == 2u);
                const CBulkDocumentQuery query = attempt.document_query();
                const CBulkHandle locator = query.object_child(retained, CStringView{ "locator" });
                std::uint64_t old_count{};
                TEST_EXPECT(ctx, query.unsigned_integer_value(query.object_child(locator, CStringView{ "count" }), old_count) &&
                    old_count == 1u);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, replacement_failures >= 1u && replacement_successes >= 1u);

    const std::string physical = R"({"types":{
        "structures":{"Pair":{"detail":{"alignment":1,"size":2},"members":[
            {"a":{"type":"u8","offset":0}},{"b":{"type":"u8","offset":1}}]}},
        "enumerations":{"Mode":{"storage":"u8","values":{"first":1,"second":2}}},
        "bit_structures":{"Mask":{"storage":"u8","members":[
            {"n":{"type":"u8","mask":3,"interpretation":"unorm"}}]}}}})";
    CBakedDocumentBlock physical_block, bulk_block;
    TEST_EXPECT(ctx, bake(physical, physical_block));
    CBakedSchema physical_schema;
    TEST_EXPECT(ctx, physical_schema.set_document(physical_block.document()) && physical_schema.resolve(schema_error));
    TEST_EXPECT(ctx, bake(R"({"data":{
        "Pair":{"one":{"locator":{"offset":0,"valid":true,"count":1}}},
        "Mode":{"one":{"locator":{"offset":2,"valid":true,"count":1}}},
        "Mask":{"one":{"locator":{"offset":3,"valid":true,"count":1}}}}})", bulk_block));
    CBakedBulkData source;
    SBulkDiagnostic error;
    TEST_EXPECT(ctx, source.set_document(bulk_block.document()) && source.bind_schema(physical_schema) &&
        source.load_supplied(CByteConstView{ bytes, 4u, 128u }, false, error));
    std::size_t promotion_failures{}, promotion_successes{};
    for (std::size_t failure_offset = 0u; failure_offset < 40u; ++failure_offset)
    {
        SFailingAllocator failing{ 0u, SIZE_MAX };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            failing.fail_on = failing.calls + failure_offset;
            CLiveBulkData destination;
            const bool promoted = source.promote(destination, physical_schema, error);
            if (promoted)
            {
                ++promotion_successes;
                SBulkEntryView entry;
                TEST_EXPECT(ctx, destination.entry(destination.find_entry(CStringView{ "Pair" }, CStringView{ "one" }), entry) &&
                    entry.count == 1u && entry.bytes[0] == bytes[0]);
            }
            else
            {
                ++promotion_failures;
                TEST_EXPECT(ctx, error.reason == EBulkLoadReason::allocation_failed &&
                    !destination.document_ready() && source.loaded_ready());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, promotion_failures >= 3u && promotion_successes >= 1u);
    std::string changed_offset = physical;
    const std::size_t first_offset = changed_offset.find("\"offset\":0");
    const std::size_t second_offset = changed_offset.find("\"offset\":1", first_offset + 1u);
    if (first_offset != std::string::npos && second_offset != std::string::npos)
    {
        changed_offset.replace(second_offset, 10u, "\"offset\":0");
        changed_offset.replace(first_offset, 10u, "\"offset\":1");
    }
    std::string changed_enum = physical;
    const std::size_t label = changed_enum.find("\"second\":2");
    if (label != std::string::npos)
    {
        changed_enum.replace(label, 10u, "\"second\":3");
    }
    std::string changed_bit = physical;
    const std::size_t interpretation = changed_bit.find(",\"interpretation\":\"unorm\"");
    if (interpretation != std::string::npos)
    {
        changed_bit.erase(interpretation, 25u);
    }
    for (const std::string& changed : { changed_offset, changed_enum, changed_bit })
    {
        CBakedDocumentBlock changed_block;
        TEST_EXPECT(ctx, bake(changed, changed_block));
        CBakedSchema changed_schema;
        TEST_EXPECT(ctx, changed_schema.set_document(changed_block.document()) && changed_schema.resolve(schema_error));
        CLiveBulkData rejected_destination;
        TEST_EXPECT(ctx, !source.promote(rejected_destination, changed_schema, error) &&
            error.reason == EBulkLoadReason::incompatible_schema && !rejected_destination.document_ready() &&
            source.loaded_ready());
    }

    std::string shared = "{\"types\":{\"structures\":{\"S0\":{\"members\":[]}";
    for (unsigned depth = 1u; depth <= 18u; ++depth)
    {
        const std::string preceding = "S" + std::to_string(depth - 1u);
        shared += ",\"S" + std::to_string(depth) + "\":{\"members\":[{\"a\":{\"type\":\"" +
            preceding + "\"}},{\"b\":{\"type\":\"" + preceding + "\"}}]}";
    }
    shared += "}}}";
    CBakedDocumentBlock shared_block, shared_bulk_block;
    TEST_EXPECT(ctx, bake(shared, shared_block));
    CBakedSchema shared_schema;
    TEST_EXPECT(ctx, shared_schema.set_document(shared_block.document()) && shared_schema.resolve(schema_error));
    TEST_EXPECT(ctx, bake(R"({"data":{"S18":{"many":{"locator":{
        "offset":0,"valid":true,"count":4294967295,"size":0}}}}})", shared_bulk_block));
    CBakedBulkData shared_source;
    TEST_EXPECT(ctx, shared_source.set_document(shared_bulk_block.document()) && shared_source.bind_schema(shared_schema) &&
        shared_source.load_supplied({}, false, error));
    CLiveBulkData shared_destination;
    TEST_EXPECT(ctx, shared_source.promote(shared_destination, shared_schema, error));
    SBulkEntryView many;
    TEST_EXPECT(ctx, shared_destination.entry(shared_destination.find_entry(CStringView{ "S18" }, CStringView{ "many" }),
        many) && many.count == UINT32_MAX && many.byte_count == 0u);
}

static void test_unused_storage_values(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{
        "structures":{
            "Empty":{"members":[]},
            "Inner":{"detail":{"size":4,"alignment":2},"members":[
                {"value":{"type":"u16","offset":0}}]},
            "Outer":{"detail":{"size":16,"alignment":16},"members":[
                {"later":{"type":"Inner","offset":8}},
                {"items":{"type":{"element":"Inner","count":2},"offset":0}}]},
            "ScalarShell":{"detail":{"size":16,"alignment":4},"members":[
                {"nan":{"type":"f32","offset":0}},
                {"negative_zero":{"type":"f32","offset":4}},
                {"boolean":{"type":"b8","offset":8}},
                {"mode":{"type":"Mode","offset":9}},
                {"bits":{"type":"Bits","offset":10}}]}
        },"enumerations":{"Mode":{"storage":"u8","values":{"one":1}}},
        "bit_structures":{
            "Bits":{"storage":"u16","members":[{"low":{"type":"u8","mask":255}}]},
            "Bits8":{"storage":"u8","members":[{"low":{"type":"u8","mask":15}}]},
            "Bits32":{"storage":"u32","members":[{"low":{"type":"u8","mask":255}}]},
            "Bits64":{"storage":"u64","members":[{"low":{"type":"u32","mask":4294967295}}]}
        }}})";
    CBakedDocumentBlock block;
    CResolvedSchema schema;
    if (!resolve(ctx, definitions, block, schema))
    {
        return;
    }
    const CSchemaIndex shell = schema.find_type(CStringView{ "ScalarShell" });
    alignas(16) std::uint8_t bytes[16];
    std::memset(bytes, 0xa5, sizeof(bytes));
    const std::uint8_t nan_bits[]{ 0x01u, 0x00u, 0xc0u, 0x7fu };
    const std::uint8_t negative_zero_bits[]{ 0x00u, 0x00u, 0x00u, 0x80u };
    std::memcpy(bytes, nan_bits, sizeof(nan_bits));
    std::memcpy(bytes + 4u, negative_zero_bits, sizeof(negative_zero_bits));
    bytes[8] = 2u;
    bytes[9] = 0x7fu;
    bytes[10] = 0x5au;
    std::uint8_t addressable[12];
    std::memcpy(addressable, bytes, sizeof(addressable));
    TEST_EXPECT(ctx, clear_unused_storage(schema, shell, CByteView{ bytes, sizeof(bytes), 16u }) &&
        std::memcmp(bytes, addressable, sizeof(addressable)) == 0 &&
        bytes[12] == 0u && bytes[13] == 0u && bytes[14] == 0u && bytes[15] == 0u);
    std::uint8_t once[sizeof(bytes)];
    std::memcpy(once, bytes, sizeof(once));
    TEST_EXPECT(ctx, clear_unused_storage(schema, shell, CByteView{ bytes, sizeof(bytes), 16u }) &&
        std::memcmp(bytes, once, sizeof(bytes)) == 0);
    TEST_EXPECT(ctx, clear_unused_storage(schema, shell, CByteView{ bytes, sizeof(bytes), 16u },
        EUnusedBits::clear) && std::memcmp(bytes, addressable, 11u) == 0 && bytes[11] == 0u);
    bytes[11] = 0xf1u;
    std::memset(bytes + 12u, 0x69, 4u);
    TEST_EXPECT(ctx, clear_unused_storage(schema, shell, CByteView{ bytes, sizeof(bytes), 16u },
        EUnusedBits::clear) && bytes[11] == 0u &&
        std::memcmp(bytes, addressable, 11u) == 0 && std::memcmp(bytes + 12u, once + 12u, 4u) == 0);
    alignas(16) std::uint8_t misaligned[17];
    std::memset(misaligned, 0xa5, sizeof(misaligned));
    TEST_EXPECT(ctx, !clear_unused_storage(schema, shell, CByteView{ misaligned + 1u, 16u, 1u }) &&
        misaligned[1] == 0xa5u && misaligned[16] == 0xa5u);
    bytes[12] = 0xa5u;
    TEST_EXPECT(ctx, !clear_unused_storage(schema, shell, CByteView{ bytes, 15u, 16u }) &&
        bytes[12] == 0xa5u);
    bytes[12] = 0u;
    const std::uint8_t before_invalid = bytes[10];
    TEST_EXPECT(ctx, !clear_unused_storage(schema, shell, CByteView{ bytes, sizeof(bytes), 16u },
        static_cast<EUnusedBits>(2u)) && bytes[10] == before_invalid);

    std::uint8_t bits8{ 0xafu };
    alignas(4) std::uint8_t bits32[]{ 0x5au, 0x91u, 0x82u, 0x73u };
    alignas(8) std::uint8_t bits64[]{ 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u };
    const CSchemaIndex type8 = schema.find_type(CStringView{ "Bits8" });
    const CSchemaIndex type32 = schema.find_type(CStringView{ "Bits32" });
    const CSchemaIndex type64 = schema.find_type(CStringView{ "Bits64" });
    TEST_EXPECT(ctx, clear_unused_storage(schema, type8, CByteView{ &bits8, 1u }) && bits8 == 0xafu &&
        clear_unused_storage(schema, type32, CByteView{ bits32, sizeof(bits32), 4u }) &&
        bits32[1] == 0x91u &&
        clear_unused_storage(schema, type64, CByteView{ bits64, sizeof(bits64), 8u }) &&
        bits64[4] == 0x55u && bits64[7] == 0x88u);
    TEST_EXPECT(ctx, clear_unused_storage(schema, type8, CByteView{ &bits8, 1u }, EUnusedBits::clear) &&
        bits8 == 0x0fu &&
        clear_unused_storage(schema, type32, CByteView{ bits32, sizeof(bits32), 4u }, EUnusedBits::clear) &&
        bits32[0] == 0x5au && bits32[1] == 0u && bits32[2] == 0u && bits32[3] == 0u &&
        clear_unused_storage(schema, type64, CByteView{ bits64, sizeof(bits64), 8u }, EUnusedBits::clear) &&
        bits64[0] == 0x11u && bits64[1] == 0x22u && bits64[2] == 0x33u && bits64[3] == 0x44u &&
        bits64[4] == 0u && bits64[5] == 0u && bits64[6] == 0u && bits64[7] == 0u);

    const CSchemaIndex outer = schema.find_type(CStringView{ "Outer" });
    alignas(16) std::uint8_t nested[16];
    std::memset(nested, 0xa5, sizeof(nested));
    nested[0] = 1u; nested[1] = 2u; nested[4] = 3u;
    nested[5] = 4u; nested[8] = 5u; nested[9] = 6u;
    TEST_EXPECT(ctx, clear_unused_storage(schema, outer, CByteView{ nested, sizeof(nested), 16u }));
    for (std::size_t index = 0u; index < sizeof(nested); ++index)
    {
        const std::uint8_t expected = index == 0u ? 1u : index == 1u ? 2u :
            index == 4u ? 3u : index == 5u ? 4u : index == 8u ? 5u : index == 9u ? 6u : 0u;
        TEST_EXPECT(ctx, nested[index] == expected);
    }
    SMember array_member;
    TEST_EXPECT(ctx, schema.member(schema.find_member(outer, CStringView{ "items" }), array_member));
    alignas(16) std::uint8_t array_bytes[8];
    std::memset(array_bytes, 0xa5, sizeof(array_bytes));
    TEST_EXPECT(ctx, !clear_unused_storage(schema, array_member.type, CByteView{ array_bytes, 4u, 2u }) &&
        !clear_unused_storage(schema, array_member.type, CByteView{ array_bytes, 6u, 2u }) &&
        array_bytes[2] == 0xa5u && array_bytes[6] == 0xa5u);
    TEST_EXPECT(ctx, clear_unused_storage(schema, array_member.type, CByteView{ array_bytes, 8u, 2u }) &&
        array_bytes[0] == 0xa5u && array_bytes[2] == 0u &&
        array_bytes[4] == 0xa5u && array_bytes[6] == 0u);
    std::uint8_t one{ 0x8bu };
    const CSchemaIndex empty = schema.find_type(CStringView{ "Empty" });
    TEST_EXPECT(ctx, clear_unused_storage(schema, empty, CByteView{}) &&
        !clear_unused_storage(schema, empty, CByteView{ &one, 1u }) && one == 0x8bu);
}

static void test_unused_storage_documents(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{
        "structures":{
            "Tiny":{"members":[{"value":{"type":"u8"}}]},
            "Padded":{"detail":{"size":4,"alignment":4},"members":[
                {"bits":{"type":"Bits","offset":0}}]},
            "Aligned":{"detail":{"size":16,"alignment":16},"members":[
                {"nested":{"type":"Padded","offset":0}},
                {"value":{"type":"u8","offset":4}}]},
            "Empty":{"members":[]}
        },"bit_structures":{"Bits":{"storage":"u16","members":[
            {"low":{"type":"u8","mask":255}}]}}}})";
    CBakedDocumentBlock schema_block;
    CBakedSchema schema;
    SDiagnostic schema_error;
    const bool schema_ready = bake(definitions, schema_block) && schema.set_document(schema_block.document()) &&
        schema.resolve(schema_error);
    TEST_EXPECT(ctx, schema_ready);
    if (!schema_ready)
    {
        return;
    }
    const CResolvedSchema* const resolved = schema.resolved();
    const CSchemaIndex aligned_type = resolved->find_type(CStringView{ "Aligned" });
    alignas(128) std::uint8_t first[128]{ 0x11u, 0xe1u, 0xa5u, 0xa5u };
    alignas(128) std::uint8_t replacement[128]{ 0x31u, 0xe1u, 0xa5u, 0xa5u,
        0x42u, 0xf2u, 0xa5u, 0xa5u };
    alignas(128) std::uint8_t tiny[128]{ 0x55u };
    alignas(128) std::uint8_t aligned[128];
    std::memset(aligned, 0xa5, sizeof(aligned));
    aligned[0] = 0x61u;
    aligned[1] = 0xb3u;
    aligned[4] = 0x71u;
    CLiveBulkData bulk;
    SBulkDiagnostic bulk_error;
    const bool bulk_ready = bulk.initialise(schema) &&
        bulk.capture(CStringView{ "Padded" }, CStringView{ "old" },
            CByteConstView{ first, 4u, 128u }, 1u, bulk_error) &&
        bulk.capture(CStringView{ "Tiny" }, CStringView{ "middle" },
            CByteConstView{ tiny, 1u, 128u }, 1u, bulk_error) &&
        bulk.capture(CStringView{ "Padded" }, CStringView{ "old" },
            CByteConstView{ replacement, 8u, 128u }, 2u, bulk_error) &&
        bulk.capture(CStringView{ "Aligned" }, CStringView{ "later" },
            CByteConstView{ aligned, 16u, 128u }, 1u, bulk_error);
    TEST_EXPECT(ctx, bulk_ready);
    if (!bulk_ready)
    {
        return;
    }
    SBulkEntryView old_entry, tiny_entry, aligned_entry;
    TEST_EXPECT(ctx, bulk.entry(bulk.find_entry(CStringView{ "Padded" }, CStringView{ "old" }), old_entry) &&
        bulk.entry(bulk.find_entry(CStringView{ "Tiny" }, CStringView{ "middle" }), tiny_entry) &&
        bulk.entry(bulk.find_entry(CStringView{ "Aligned" }, CStringView{ "later" }), aligned_entry) &&
        old_entry.offset == 8u && old_entry.byte_count == 8u && tiny_entry.offset == 4u &&
        aligned_entry.offset == 16u && bulk.payload_view().size() == 32u);

    CLiveDocument packed_document;
    CByteBuffer packed;
    const bool packed_ready = bulk.prepare_output(packed_document, packed, schema, EDataOutputForm::external, bulk_error);
    TEST_EXPECT(ctx, packed_ready);
    if (!packed_ready)
    {
        return;
    }
    TEST_EXPECT(ctx, packed.size() == 32u && packed.reserve(64u, 128u) && packed.capacity() > packed.size());
    packed.data()[packed.size()] = 0xeeu;
    std::memset(packed.data() + 9u, 0x77, 7u);
    std::memset(packed.data() + 2u, 0x88, 2u);
    std::memset(packed.data() + 6u, 0x88, 2u);
    SRemapDiagnostic remap_error;
    CDataRemapPlan remap;
    const SRemapSourceType remap_source{ resolved, aligned_type };
    alignas(16) std::uint8_t imported[16];
    std::memset(imported, 0x99, sizeof(imported));
    imported[0] = 0x82u;
    imported[1] = 0xd4u;
    imported[4] = 0x93u;
    const CByteConstView imported_view{ imported, sizeof(imported), 16u };
    TEST_EXPECT(ctx, remap.initialise(&remap_source, 1u, *resolved, aligned_type, remap_error) &&
        remap.execute(&imported_view, 1u, packed.view().subview(16u, 16u)) &&
        packed.data()[18] == 0x99u && packed.data()[20] == 0x93u);
    const CBulkDocumentQuery packed_query{ packed_document };
    TEST_EXPECT(ctx, clear_unused_storage(*resolved, packed_query, packed.view()) &&
        packed.data()[0] == 0x31u && packed.data()[1] == 0xe1u &&
        packed.data()[2] == 0u && packed.data()[3] == 0u &&
        packed.data()[4] == 0x42u && packed.data()[5] == 0xf2u &&
        packed.data()[6] == 0u && packed.data()[7] == 0u && packed.data()[8] == 0x55u &&
        packed.data()[16] == 0x82u && packed.data()[17] == 0xd4u &&
        packed.data()[18] == 0u && packed.data()[19] == 0u && packed.data()[20] == 0x93u &&
        packed.data()[packed.size()] == 0xeeu);
    for (std::size_t index = 9u; index < 16u; ++index)
    {
        TEST_EXPECT(ctx, packed.data()[index] == 0u);
    }
    for (std::size_t index = 21u; index < packed.size(); ++index)
    {
        TEST_EXPECT(ctx, packed.data()[index] == 0u);
    }
    std::uint8_t packed_once[32];
    std::memcpy(packed_once, packed.data(), sizeof(packed_once));
    TEST_EXPECT(ctx, clear_unused_storage(*resolved, packed_query, packed.view()) &&
        std::memcmp(packed.data(), packed_once, sizeof(packed_once)) == 0);
    TEST_EXPECT(ctx, clear_unused_storage(*resolved, packed_query, packed.view(), EUnusedBits::clear) &&
        packed.data()[1] == 0u && packed.data()[5] == 0u && packed.data()[17] == 0u &&
        packed.data()[0] == 0x31u && packed.data()[4] == 0x42u && packed.data()[16] == 0x82u);

    CBakedDocumentBlock baked_block;
    CByteBuffer baked_owner;
    CBakedBulkData baked_role;
    const bool baked_ready = bulk.demote(baked_block, baked_owner, baked_role, schema,
        EDataOutputForm::external, bulk_error);
    TEST_EXPECT(ctx, baked_ready);
    if (baked_ready)
    {
        TEST_EXPECT(ctx, baked_role.payload_view().data() == baked_owner.data() && baked_owner.size() == 32u);
        std::memset(baked_owner.data() + 9u, 0x77, 7u);
        baked_owner.data()[2] = 0x99u;
        TEST_EXPECT(ctx, clear_unused_storage(*resolved, baked_role.document_query(), baked_owner.view()) &&
            baked_owner.data()[2] == 0u && baked_owner.data()[9] == 0u &&
            baked_owner.data()[0] == 0x31u && baked_owner.data()[1] == 0xe1u);
    }

    const CBulkHandle zero_count = bulk.create_unpopulated(CStringView{ "Padded" },
        CStringView{ "zero" }, 0u, bulk_error);
    const CBulkHandle huge_empty = bulk.create_unpopulated(CStringView{ "Empty" },
        CStringView{ "huge" }, UINT32_MAX, bulk_error);
    TEST_EXPECT(ctx, zero_count && huge_empty && bulk.clear_unused_storage() &&
        bulk.payload_view().data()[0] == 0u && bulk.payload_view().data()[3] == 0u &&
        bulk.payload_view().data()[4] == 0x55u && bulk.payload_view().data()[5] == 0u &&
        bulk.payload_view().data()[8] == 0x31u && bulk.payload_view().data()[9] == 0xe1u &&
        bulk.payload_view().data()[10] == 0u && bulk.payload_view().data()[11] == 0u);
    TEST_EXPECT(ctx, bulk.clear_unused_storage(EUnusedBits::clear) &&
        bulk.payload_view().data()[9] == 0u && bulk.payload_view().data()[13] == 0u &&
        bulk.payload_view().data()[8] == 0x31u && bulk.payload_view().data()[12] == 0x42u);

    CLiveInstances instances;
    SInstanceDiagnostic instance_error;
    CLiveDocument declarations;
    const bool instances_ready = instances.initialise(schema) &&
        instances.capture_base(CStringView{ "Aligned" }, CStringView{ "base" },
            CByteConstView{ aligned, 16u, 128u }, instance_error) &&
        instances.capture_base(CStringView{ "Tiny" }, CStringView{ "marker" },
            CByteConstView{ tiny, 1u, 128u }, instance_error) &&
        parse_live(R"({"selection":{"value":7}})", declarations);
    TEST_EXPECT(ctx, instances_ready);
    if (!instances_ready)
    {
        return;
    }
    const CInstanceHandle base = instances.find_base(CStringView{ "Aligned" }, CStringView{ "base" });
    const CInstanceDocumentQuery values{ declarations };
    const CInstanceHandle child = instances.create_specialisation(base, CStringView{ "child" }, values,
        values.object_child(values.root(), CStringView{ "selection" }), instance_error);
    SInstanceEntryView base_entry, child_entry, marker_entry;
    TEST_EXPECT(ctx, child && instances.entry(base, base_entry) && instances.entry(child, child_entry) &&
        instances.entry(instances.find_base(CStringView{ "Tiny" }, CStringView{ "marker" }), marker_entry) &&
        base_entry.offset == 0u && marker_entry.offset == 16u && child_entry.offset == 32u &&
        instances.payload_view().size() == 48u);
    const CInstanceHandle declaration = child_entry.declaration;
    const CInstanceDocumentQuery live_query = instances.document_query();
    std::uint64_t selected{};
    TEST_EXPECT(ctx, live_query.unsigned_integer_value(live_query.object_child(
        declaration, CStringView{ "value" }), selected) && selected == 7u &&
        instances.clear_unused_storage() && base_entry.bytes[0] == 0x61u && base_entry.bytes[1] == 0xb3u &&
        base_entry.bytes[2] == 0u && child_entry.bytes[0] == 0x61u && child_entry.bytes[1] == 0xb3u &&
        child_entry.bytes[4] == 7u && child_entry.bytes[5] == 0u &&
        marker_entry.bytes[0] == 0x55u && instances.payload_view().data()[17] == 0u &&
        live_query.unsigned_integer_value(live_query.object_child(
            declaration, CStringView{ "value" }), selected) && selected == 7u);
    TEST_EXPECT(ctx, instances.clear_unused_storage(EUnusedBits::clear) &&
        base_entry.bytes[1] == 0u && child_entry.bytes[1] == 0u &&
        base_entry.bytes[0] == 0x61u && child_entry.bytes[4] == 7u);

    CLiveDocument overlapping;
    TEST_EXPECT(ctx, parse_live(R"({"data":{"Padded":{
        "a":{"locator":{"offset":0,"valid":true,"count":1,"size":4}},
        "b":{"locator":{"offset":0,"valid":true,"count":1,"size":4}}}}})", overlapping));
    alignas(16) std::uint8_t untouched[4]{ 0x41u, 0x99u, 0x88u, 0x77u };
    std::uint8_t saved[sizeof(untouched)];
    std::memcpy(saved, untouched, sizeof(saved));
    TEST_EXPECT(ctx, !clear_unused_storage(*resolved, CBulkDocumentQuery{ overlapping },
        CByteView{ untouched, sizeof(untouched) }) && std::memcmp(saved, untouched, sizeof(saved)) == 0);
    CLiveDocument inconsistent;
    TEST_EXPECT(ctx, parse_live(R"({"data":{"Padded":{"bad":{
        "locator":{"offset":0,"valid":true,"count":2,"size":4}}}}})", inconsistent));
    TEST_EXPECT(ctx, !clear_unused_storage(*resolved, CBulkDocumentQuery{ inconsistent },
        CByteView{ untouched, sizeof(untouched) }) && std::memcmp(saved, untouched, sizeof(saved)) == 0);
    CLiveDocument late;
    TEST_EXPECT(ctx, parse_live(R"({"data":{"Padded":{
        "early":{"locator":{"offset":0,"valid":true,"count":1,"size":4}},
        "late":{"locator":{"offset":4,"valid":true,"count":1,"size":4}}}}})", late));
    TEST_EXPECT(ctx, !clear_unused_storage(*resolved, CBulkDocumentQuery{ late },
        CByteView{ untouched, sizeof(untouched), 16u }) &&
        std::memcmp(saved, untouched, sizeof(saved)) == 0);
    alignas(16) std::uint8_t wrong_address[9];
    std::memset(wrong_address, 0x99, sizeof(wrong_address));
    TEST_EXPECT(ctx, !clear_unused_storage(*resolved, CBulkDocumentQuery{ late },
        CByteView{ wrong_address + 1u, 8u, 1u }) &&
        wrong_address[1] == 0x99u && wrong_address[8] == 0x99u);
    alignas(16) std::uint8_t allocation_target[32];
    std::memcpy(allocation_target, packed.data(), sizeof(allocation_target));
    allocation_target[9] = 0xa5u;
    std::uint8_t allocation_saved[32];
    std::memcpy(allocation_saved, allocation_target, sizeof(allocation_saved));
    {
        SFailingAllocator failing{ 0u, 0u };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            TEST_EXPECT(ctx, !clear_unused_storage(*resolved, packed_query,
                CByteView{ allocation_target, sizeof(allocation_target), 16u }) && failing.calls != 0u &&
                std::memcmp(allocation_saved, allocation_target, sizeof(allocation_saved)) == 0);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

static void test_data_remap(TTestContext& ctx)
{
    const std::string source_text = R"({"types":{"structures":{
        "Empty":{"detail":{"size":0},"members":[]},
        "Src":{"detail":{"size":8,"alignment":2},"members":[
            {"b":{"type":"u16","offset":0}},{"a":{"type":"u8","offset":4}},
            {"marker":{"type":"Empty","offset":8}}]},
        "SrcC":{"members":[{"c":{"type":"u32"}}]},
        "None":{"detail":{"size":4,"alignment":4},"members":[{"q":{"type":"u8"}}]},
        "Zero":{"detail":{"size":0},"members":[{"marker":{"type":"Empty","offset":0}}]}
    }}})";
    const std::string destination_text = R"({"types":{"structures":{
        "Dummy":{"members":[{"x":{"type":"u64"}}]},
        "Empty":{"detail":{"size":0},"members":[]},
        "Dest":{"detail":{"size":12,"alignment":4},"members":[
            {"a":{"type":"u8","offset":0}},{"b":{"type":"u16","offset":4}},
            {"c":{"type":"u32","offset":8}},{"marker":{"type":"Empty","offset":12}}]},
        "Zero":{"detail":{"size":0},"members":[{"marker":{"type":"Empty","offset":0}}]}
    }}})";
    CBakedDocumentBlock source_block, destination_block;
    CResolvedSchema source_schema, destination_schema;
    if (!resolve(ctx, source_text, source_block, source_schema) ||
        !resolve(ctx, destination_text, destination_block, destination_schema))
    {
        return;
    }
    const SRemapSourceType input_types[] = {
        { &source_schema, source_schema.find_type(CStringView{ "Src" }) },
        { &source_schema, source_schema.find_type(CStringView{ "SrcC" }) }
    };
    CDataRemapPlan plan;
    SRemapDiagnostic error;
    TEST_EXPECT(ctx, plan.initialise(input_types, 2u, destination_schema,
        destination_schema.find_type(CStringView{ "Dest" }), error));
    TEST_EXPECT(ctx, plan.is_ready() && plan.source_count() == 2u &&
        plan.matched_member_count() == 4u && plan.copy_range_count() == 3u);

    alignas(8) std::uint8_t source_a[16]{};
    alignas(8) std::uint8_t source_c[8]{};
    alignas(8) std::uint8_t target[36];
    source_a[0] = 0x11u; source_a[1] = 0x22u; source_a[4] = 0x33u;
    source_a[8] = 0x44u; source_a[9] = 0x55u; source_a[12] = 0x66u;
    source_c[0] = 0x71u; source_c[1] = 0x72u; source_c[2] = 0x73u; source_c[3] = 0x74u;
    source_c[4] = 0x81u; source_c[5] = 0x82u; source_c[6] = 0x83u; source_c[7] = 0x84u;
    std::memset(target, 0xcc, sizeof(target));
    const CByteConstView inputs[] = {
        CByteConstView{ source_a, sizeof(source_a), 2u },
        CByteConstView{ source_c, sizeof(source_c), 4u }
    };
    TEST_EXPECT(ctx, plan.execute(inputs, 2u, CByteView{ target, sizeof(target), 4u }));
    for (std::size_t i = 0u; i < 2u; ++i)
    {
        const std::size_t d = i * 12u;
        TEST_EXPECT(ctx, target[d] == source_a[(i * 8u) + 4u] &&
            target[d + 4u] == source_a[i * 8u] && target[d + 5u] == source_a[(i * 8u) + 1u] &&
            std::memcmp(target + d + 8u, source_c + (i * 4u), 4u) == 0);
        TEST_EXPECT(ctx, target[d + 1u] == 0xccu && target[d + 2u] == 0xccu &&
            target[d + 3u] == 0xccu && target[d + 6u] == 0xccu && target[d + 7u] == 0xccu);
    }
    for (std::size_t i = 24u; i < sizeof(target); ++i)
    {
        TEST_EXPECT(ctx, target[i] == 0xccu);
    }
    std::uint8_t saved[sizeof(target)];
    std::memcpy(saved, target, sizeof(target));
    std::memset(target, 0xcc, sizeof(target));
    TEST_EXPECT(ctx, plan.execute(inputs, 2u, CByteView{ target, 12u, 4u }) &&
        std::memcmp(target, saved, 12u) == 0);
    for (std::size_t i = 12u; i < sizeof(target); ++i)
    {
        TEST_EXPECT(ctx, target[i] == 0xccu);
    }
    std::memset(target, 0x6d, sizeof(target));
    std::uint8_t rejected[sizeof(target)];
    std::memcpy(rejected, target, sizeof(rejected));
    TEST_EXPECT(ctx, !plan.execute(inputs, 2u, CByteView{ target, 23u, 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    TEST_EXPECT(ctx, !plan.execute(inputs, 2u, CByteView{ target + 1u, 24u, 1u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    const CByteConstView aliased[] = {
        CByteConstView{ target, 32u, 4u }, inputs[1]
    };
    TEST_EXPECT(ctx, !plan.execute(aliased, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    const CByteConstView misaligned_second[] = {
        inputs[0], CByteConstView{ source_c + 1u, 4u, 1u }
    };
    TEST_EXPECT(ctx, !plan.execute(misaligned_second, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    const CByteConstView short_second[] = {
        inputs[0], CByteConstView{ source_c, 4u, 4u }
    };
    std::memset(target, 0xcc, sizeof(target));
    TEST_EXPECT(ctx, plan.execute(short_second, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(target, saved, 12u) == 0);
    for (std::size_t i = 12u; i < sizeof(target); ++i)
    {
        TEST_EXPECT(ctx, target[i] == 0xccu);
    }
    std::memset(target, 0x6d, sizeof(target));
    const CByteConstView partial_second[] = {
        inputs[0], CByteConstView{ source_c, 7u, 4u }
    };
    TEST_EXPECT(ctx, !plan.execute(partial_second, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    const CByteConstView shorter_than_record[] = {
        inputs[0], CByteConstView{ source_c, 3u, 4u }
    };
    TEST_EXPECT(ctx, !plan.execute(shorter_than_record, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    const CByteConstView zero_then_partial[] = {
        {}, CByteConstView{ source_c, 7u, 4u }
    };
    TEST_EXPECT(ctx, !plan.execute(zero_then_partial, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    const CByteConstView reset_invalid[] = {
        inputs[0], CByteConstView{ nullptr, 8u, 4u }
    };
    TEST_EXPECT(ctx, reset_invalid[1].is_empty() &&
        plan.execute(reset_invalid, 2u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    TEST_EXPECT(ctx, !plan.execute(inputs, 1u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    {
        SFailingAllocator failing{ 0u, 0u };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            TEST_EXPECT(ctx, plan.execute(inputs, 2u, CByteView{ target, sizeof(target), 4u }));
            TEST_EXPECT(ctx, failing.calls == 0u);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }

    {
        SFailingAllocator failing{ 0u, 0u };
        memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CDataRemapPlan failure;
            TEST_EXPECT(ctx, !failure.initialise(input_types, 2u, destination_schema,
                destination_schema.find_type(CStringView{ "Dest" }), error) &&
                error.reason == ERemapReason::allocation_failed && !failure.is_ready());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }

    const SRemapSourceType duplicate_types[] = { input_types[0], input_types[0] };
    TEST_EXPECT(ctx, !plan.initialise(duplicate_types, 2u, destination_schema,
        destination_schema.find_type(CStringView{ "Dest" }), error) &&
        error.reason == ERemapReason::overlap && !plan.is_ready());
    const SRemapSourceType zero_and_populated[] = {
        { &source_schema, source_schema.find_type(CStringView{ "Zero" }) }, input_types[1]
    };
    TEST_EXPECT(ctx, plan.initialise(zero_and_populated, 2u, destination_schema,
        destination_schema.find_type(CStringView{ "Dest" }), error) &&
        plan.matched_member_count() == 2u && plan.copy_range_count() == 1u);
    const CByteConstView mixed_inputs[] = { {}, inputs[1] };
    std::memset(target, 0xcc, sizeof(target));
    TEST_EXPECT(ctx, plan.execute(mixed_inputs, 2u, CByteView{ target, sizeof(target), 4u }));
    for (std::size_t i = 0u; i < sizeof(target); ++i)
    {
        const bool copied_c = ((i >= 8u) && (i < 12u)) || ((i >= 20u) && (i < 24u));
        const std::uint8_t expected = copied_c ? source_c[(i % 12u) - 8u + ((i / 12u) * 4u)] : 0xccu;
        TEST_EXPECT(ctx, target[i] == expected);
    }
    std::memset(target, 0x6d, sizeof(target));
    const SRemapSourceType unmatched{ &source_schema, source_schema.find_type(CStringView{ "None" }) };
    TEST_EXPECT(ctx, plan.initialise(&unmatched, 1u, destination_schema,
        destination_schema.find_type(CStringView{ "Dest" }), error) &&
        plan.matched_member_count() == 0u && plan.copy_range_count() == 0u);
    const CByteConstView empty_input{};
    TEST_EXPECT(ctx, plan.execute(&empty_input, 1u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    alignas(4) const std::uint8_t unmatched_bytes[4]{ 0x91u };
    const CByteConstView partial_unmatched{ unmatched_bytes, 3u, 4u };
    TEST_EXPECT(ctx, !plan.execute(&partial_unmatched, 1u, CByteView{ target, sizeof(target), 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);
    TEST_EXPECT(ctx, !plan.execute(&empty_input, 1u, CByteView{ target, 23u, 4u }) &&
        std::memcmp(rejected, target, sizeof(target)) == 0);

    const SRemapSourceType zero_type{ &source_schema, source_schema.find_type(CStringView{ "Zero" }) };
    TEST_EXPECT(ctx, plan.initialise(&zero_type, 1u, destination_schema,
        destination_schema.find_type(CStringView{ "Zero" }), error) &&
        plan.matched_member_count() == 1u && plan.copy_range_count() == 0u);
    const CByteConstView zero_input{};
    TEST_EXPECT(ctx, plan.execute(&zero_input, 1u, {}));
    CDataRemapPlan moved{ std::move(plan) };
    TEST_EXPECT(ctx, moved.is_ready() && !plan.is_ready() && plan.source_count() == 0u);
}

static void test_data_remap_matching(TTestContext& ctx)
{
    const std::string source_text = R"({"types":{
        "structures":{
            "Inner":{"detail":{"size":8,"alignment":4},"members":[{"x":{"type":"u8","offset":0}}]},
            "Compound":{"members":[{"mode":{"type":"Mode"}},{"value":{"type":"u8"}}]},
            "Source":{"members":[{"scalar":{"type":"u8"}},{"mode":{"type":"Mode"}},
                {"compound":{"type":"Compound"}},{"inner":{"type":"Inner"}},
                {"array":{"type":{"element":"u8","count":2}}}]}
        },"enumerations":{"Mode":{"storage":"u8","values":{"first":1,"last":2}}}}})";
    const std::string destination_text = R"({"types":{
        "structures":{
            "Inner":{"detail":{"size":8,"alignment":4},"members":[{"x":{"type":"u8","offset":0}}]},
            "Compound":{"members":[{"mode":{"type":"Mode"}},{"value":{"type":"u8"}}]},
            "Destination":{"members":[{"scalar":{"type":"u8"}},{"mode":{"type":"Mode"}},
                {"compound":{"type":"Compound"}},{"inner":{"type":"Inner"}},
                {"array":{"type":{"element":"u8","count":2}}}]}
        },"enumerations":{"Mode":{"storage":"u8","values":{"first":1,"last":3}}}}})";
    CBakedDocumentBlock source_block, destination_block;
    CResolvedSchema source, destination;
    if (!resolve(ctx, source_text, source_block, source) ||
        !resolve(ctx, destination_text, destination_block, destination))
    {
        return;
    }
    const SRemapSourceType input{ &source, source.find_type(CStringView{ "Source" }) };
    CDataRemapPlan plan;
    SRemapDiagnostic error;
    TEST_EXPECT(ctx, plan.initialise(&input, 1u, destination,
        destination.find_type(CStringView{ "Destination" }), error));
    TEST_EXPECT(ctx, plan.matched_member_count() == 3u && plan.copy_range_count() == 2u);
    SType source_type, destination_type;
    TEST_EXPECT(ctx, source.type(input.type, source_type) &&
        destination.type(destination.find_type(CStringView{ "Destination" }), destination_type));
    CByteBuffer source_bytes, destination_bytes;
    TEST_EXPECT(ctx, source_bytes.allocate(static_cast<std::size_t>(source_type.size),
        static_cast<std::size_t>(source_type.alignment)) &&
        source_bytes.set_size(static_cast<std::size_t>(source_type.size)) &&
        destination_bytes.allocate(static_cast<std::size_t>(destination_type.size),
        static_cast<std::size_t>(destination_type.alignment)) &&
        destination_bytes.set_size(static_cast<std::size_t>(destination_type.size)));
    source_bytes.zero_fill();
    std::memset(destination_bytes.data(), 0xcc, destination_bytes.size());
    SMember source_inner, destination_inner;
    TEST_EXPECT(ctx, source.member(source.find_member(input.type, CStringView{ "inner" }), source_inner) &&
        destination.member(destination.find_member(destination.find_type(CStringView{ "Destination" }),
            CStringView{ "inner" }), destination_inner));
    for (std::size_t i = 0u; i < static_cast<std::size_t>(source_inner.size); ++i)
    {
        source_bytes.data()[static_cast<std::size_t>(source_inner.offset) + i] = static_cast<std::uint8_t>(0x40u + i);
    }
    const CByteConstView source_view = source_bytes.const_view();
    TEST_EXPECT(ctx, plan.execute(&source_view, 1u, destination_bytes.view()));
    TEST_EXPECT(ctx, std::memcmp(source_bytes.data() + source_inner.offset,
        destination_bytes.data() + destination_inner.offset, static_cast<std::size_t>(source_inner.size)) == 0);
    SMember dm, dc;
    TEST_EXPECT(ctx, destination.member(destination.find_member(destination.find_type(CStringView{ "Destination" }),
        CStringView{ "mode" }), dm) && destination.member(destination.find_member(
        destination.find_type(CStringView{ "Destination" }), CStringView{ "compound" }), dc));
    TEST_EXPECT(ctx, destination_bytes.data()[static_cast<std::size_t>(dm.offset)] == 0xccu &&
        destination_bytes.data()[static_cast<std::size_t>(dc.offset)] == 0xccu);

    const std::string mismatched_destination = R"({"types":{"structures":{
        "Compound":{"members":[{"mode":{"type":"Mode"}},{"value":{"type":"i8"}}]},
        "Destination":{"members":[{"mode":{"type":"Mode"}},{"compound":{"type":"Compound"}},
            {"scalar":{"type":"u16"}}]}
        },"enumerations":{"Mode":{"storage":"u8","values":{"first":1,"last":3}}}}})";
    CBakedDocumentBlock mismatched_block;
    CResolvedSchema mismatched;
    if (resolve(ctx, mismatched_destination, mismatched_block, mismatched))
    {
        const CSchemaIndex destination_index = mismatched.find_type(CStringView{ "Destination" });
        TEST_EXPECT(ctx, plan.initialise(&input, 1u, mismatched, destination_index, error) &&
            plan.is_ready() && plan.matched_member_count() == 0u && plan.copy_range_count() == 0u);
        SType layout;
        TEST_EXPECT(ctx, mismatched.type(destination_index, layout));
        CByteBuffer untouched;
        TEST_EXPECT(ctx, untouched.allocate(static_cast<std::size_t>(layout.size),
            static_cast<std::size_t>(layout.alignment)) &&
            untouched.set_size(static_cast<std::size_t>(layout.size)));
        std::memset(untouched.data(), 0xcc, untouched.size());
        TEST_EXPECT(ctx, plan.execute(&source_view, 1u, untouched.view()));
        for (std::size_t i = 0u; i < untouched.size(); ++i)
        {
            TEST_EXPECT(ctx, untouched.data()[i] == 0xccu);
        }
    }
    const std::string default_source = R"({"types":{"structures":{
        "Inner":{"members":[{"x":{"type":"u8","default":1}}]},
        "Source":{"members":[{"inner":{"type":"Inner"}}]}}}})";
    const std::string default_destination = R"({"types":{"structures":{
        "Inner":{"members":[{"x":{"type":"u8","default":2}}]},
        "Destination":{"members":[{"inner":{"type":"Inner"}}]}}}})";
    CBakedDocumentBlock default_source_block, default_destination_block;
    CResolvedSchema ds, dd;
    if (resolve(ctx, default_source, default_source_block, ds) &&
        resolve(ctx, default_destination, default_destination_block, dd))
    {
        const SRemapSourceType default_input{ &ds, ds.find_type(CStringView{ "Source" }) };
        TEST_EXPECT(ctx, plan.initialise(&default_input, 1u, dd,
            dd.find_type(CStringView{ "Destination" }), error) &&
            plan.matched_member_count() == 0u && plan.copy_range_count() == 0u);
    }

    const std::string category_destination = R"({"types":{"structures":{
        "Destination":{"members":[{"inner":{"type":"u64"}}]}}}})";
    CBakedDocumentBlock category_block;
    CResolvedSchema category;
    if (resolve(ctx, category_destination, category_block, category))
    {
        TEST_EXPECT(ctx, plan.initialise(&input, 1u, category,
            category.find_type(CStringView{ "Destination" }), error) &&
            plan.matched_member_count() == 0u && plan.copy_range_count() == 0u);
    }
}

static void test_data_remap_role_views(TTestContext& ctx)
{
    const std::string definitions = R"({"types":{"structures":{
        "SourceA":{"members":[{"a":{"type":"u8"}}]},
        "SourceB":{"members":[{"b":{"type":"u8"}}]},
        "Destination":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}},
            {"keep":{"type":"u8"}}]},
        "Empty":{"members":[]}}}})";
    CBakedDocumentBlock schema_block;
    CBakedSchema schema;
    SDiagnostic schema_error;
    const bool schema_ready = bake(definitions, schema_block) && schema.set_document(schema_block.document()) &&
        schema.resolve(schema_error);
    TEST_EXPECT(ctx, schema_ready);
    if (!schema_ready)
    {
        return;
    }
    const CResolvedSchema* const resolved = schema.resolved();
    const CSchemaIndex source_a_type = resolved->find_type(CStringView{ "SourceA" });
    const CSchemaIndex source_b_type = resolved->find_type(CStringView{ "SourceB" });
    const CSchemaIndex destination_type = resolved->find_type(CStringView{ "Destination" });
    const CSchemaIndex empty_type = resolved->find_type(CStringView{ "Empty" });
    const SRemapSourceType types[] = { { resolved, source_a_type }, { resolved, source_b_type } };
    CDataRemapPlan plan;
    SRemapDiagnostic remap_error;
    const bool plan_ready = plan.initialise(types, 2u, *resolved, destination_type, remap_error);
    TEST_EXPECT(ctx, plan_ready && plan.matched_member_count() == 2u);
    if (!plan_ready)
    {
        return;
    }

    CBakedDocumentBlock bulk_block;
    CBakedBulkData baked_bulk;
    SBulkDiagnostic bulk_error;
    alignas(128) std::uint8_t baked_bulk_bytes[128]{ 0x11u, 0x22u };
    const bool baked_bulk_document = bake(R"({"data":{"SourceA":{"array":{
        "locator":{"offset":0,"valid":true},"data":[1,2]}}}})", bulk_block);
    TEST_EXPECT(ctx, baked_bulk_document);
    const bool baked_bulk_bound = baked_bulk_document && baked_bulk.set_document(bulk_block.document()) &&
        baked_bulk.bind_schema(schema);
    TEST_EXPECT(ctx, baked_bulk_bound);
    const bool baked_bulk_ready = baked_bulk_bound &&
        baked_bulk.load_supplied(CByteConstView{ baked_bulk_bytes, 2u, 128u }, false, bulk_error);
    TEST_EXPECT(ctx, baked_bulk_ready);
    CLiveBulkData live_bulk;
    alignas(128) std::uint8_t live_bulk_bytes[128]{ 0x31u, 0x32u };
    const bool live_bulk_ready = live_bulk.initialise(schema) && live_bulk.capture(CStringView{ "SourceB" },
        CStringView{ "array" }, CByteConstView{ live_bulk_bytes, 2u, 128u }, 2u, bulk_error);
    TEST_EXPECT(ctx, live_bulk_ready);
    if (!baked_bulk_ready || !live_bulk_ready)
    {
        return;
    }
    const CBulkHandle baked_bulk_handle = baked_bulk.find_entry(CStringView{ "SourceA" }, CStringView{ "array" });
    const CBulkHandle live_bulk_handle = live_bulk.find_entry(CStringView{ "SourceB" }, CStringView{ "array" });
    SBulkEntryView baked_bulk_entry, live_bulk_entry;
    const bool bulk_entries_ready = baked_bulk.entry(baked_bulk_handle, baked_bulk_entry) &&
        live_bulk.entry(live_bulk_handle, live_bulk_entry) &&
        (baked_bulk_entry.type == source_a_type) && (live_bulk_entry.type == source_b_type);
    TEST_EXPECT(ctx, bulk_entries_ready);
    if (!bulk_entries_ready)
    {
        return;
    }
    const CByteConstView bulk_sources[] = {
        CByteConstView{ baked_bulk_entry.bytes, static_cast<std::size_t>(baked_bulk_entry.byte_count) },
        CByteConstView{ live_bulk_entry.bytes, static_cast<std::size_t>(live_bulk_entry.byte_count) }
    };
    std::uint8_t external[9];
    std::memset(external, 0xcc, sizeof(external));
    TEST_EXPECT(ctx, plan.execute(bulk_sources, 2u, CByteView{ external, sizeof(external) }) &&
        external[0] == 0x11u && external[1] == 0x31u && external[2] == 0xccu &&
        external[3] == 0x22u && external[4] == 0x32u && external[5] == 0xccu &&
        external[6] == 0xccu && external[7] == 0xccu && external[8] == 0xccu);

    const CBulkHandle unpopulated = live_bulk.create_unpopulated(CStringView{ "Destination" },
        CStringView{ "output" }, 3u, bulk_error);
    SMutableBulkEntryView bulk_destination;
    const bool bulk_destination_ready = unpopulated && live_bulk.mutable_entry(unpopulated, bulk_destination) &&
        (bulk_destination.type == destination_type) && (bulk_destination.count == 3u);
    TEST_EXPECT(ctx, bulk_destination_ready);
    if (!bulk_destination_ready)
    {
        return;
    }
    SBulkEntryView current_bulk_source;
    const bool current_source_ready = live_bulk.entry(live_bulk_handle, current_bulk_source) &&
        (current_bulk_source.type == source_b_type);
    TEST_EXPECT(ctx, current_source_ready);
    if (!current_source_ready)
    {
        return;
    }
    const CByteConstView current_sources[] = {
        bulk_sources[0], CByteConstView{ current_bulk_source.bytes, static_cast<std::size_t>(current_bulk_source.byte_count) }
    };
    std::memset(bulk_destination.bytes.data(), 0xcc, bulk_destination.bytes.size());
    TEST_EXPECT(ctx, plan.execute(current_sources, 2u, bulk_destination.bytes) &&
        bulk_destination.bytes.data()[0] == 0x11u && bulk_destination.bytes.data()[1] == 0x31u &&
        bulk_destination.bytes.data()[2] == 0xccu && bulk_destination.bytes.data()[3] == 0x22u &&
        bulk_destination.bytes.data()[4] == 0x32u && bulk_destination.bytes.data()[8] == 0xccu);

    alignas(128) std::uint8_t replacement[128]{};
    replacement[0] = 0x41u;
    replacement[1] = 0x42u;
    replacement[2] = 0x43u;
    TEST_EXPECT(ctx, live_bulk.capture(CStringView{ "SourceB" }, CStringView{ "array" },
        CByteConstView{ replacement, 128u, 128u }, 128u, bulk_error) == live_bulk_handle);
    SBulkEntryView refreshed_bulk;
    TEST_EXPECT(ctx, live_bulk.entry(live_bulk_handle, refreshed_bulk) &&
        refreshed_bulk.type == source_b_type && refreshed_bulk.count == 128u &&
        refreshed_bulk.bytes == live_bulk.payload_view().data() + refreshed_bulk.offset);
    const CByteConstView refreshed_sources[] = {
        bulk_sources[0], CByteConstView{ refreshed_bulk.bytes, static_cast<std::size_t>(refreshed_bulk.byte_count) }
    };
    std::memset(external, 0xcc, sizeof(external));
    TEST_EXPECT(ctx, plan.execute(refreshed_sources, 2u, CByteView{ external, sizeof(external) }) &&
        external[0] == 0x11u && external[1] == 0x41u && external[3] == 0x22u && external[4] == 0x42u);
    SMutableBulkEntryView refreshed_destination;
    TEST_EXPECT(ctx, live_bulk.mutable_entry(unpopulated, refreshed_destination) &&
        refreshed_destination.bytes.data() == live_bulk.payload_view().data() + refreshed_destination.offset);

    CBakedDocumentBlock instance_block;
    CBakedInstances baked_instances;
    SInstanceDiagnostic instance_error;
    alignas(128) std::uint8_t baked_instance_bytes[128]{ 0x71u };
    const bool baked_instance_ready = bake(R"({"instances":{"SourceA":{"one":{
        "locator":{"offset":0,"valid":true}}}}})", instance_block) &&
        baked_instances.set_document(instance_block.document()) && baked_instances.bind_schema(schema) &&
        baked_instances.load_supplied(CByteConstView{ baked_instance_bytes, 1u, 128u }, false, instance_error);
    TEST_EXPECT(ctx, baked_instance_ready);
    CLiveInstances live_instances;
    alignas(128) std::uint8_t live_instance_bytes[128]{ 0x81u };
    alignas(128) std::uint8_t destination_bytes[128]{ 0xccu, 0xccu, 0xccu };
    const bool live_instance_ready = live_instances.initialise(schema) &&
        live_instances.capture_base(CStringView{ "SourceB" }, CStringView{ "source" },
            CByteConstView{ live_instance_bytes, 1u, 128u }, instance_error) &&
        live_instances.capture_base(CStringView{ "Destination" }, CStringView{ "base" },
            CByteConstView{ destination_bytes, 3u, 128u }, instance_error);
    TEST_EXPECT(ctx, live_instance_ready);
    if (!baked_instance_ready || !live_instance_ready)
    {
        return;
    }
    CLiveDocument declarations;
    TEST_EXPECT(ctx, parse_live(R"({"child":{"keep":9},"grand":{"keep":7}})", declarations));
    const CInstanceDocumentQuery declaration_query{ declarations };
    const CInstanceHandle base = live_instances.find_base(CStringView{ "Destination" }, CStringView{ "base" });
    const CInstanceHandle child = live_instances.create_specialisation(base, CStringView{ "child" },
        declaration_query, declaration_query.object_child(declaration_query.root(), CStringView{ "child" }), instance_error);
    const CInstanceHandle grand = live_instances.create_specialisation(child, CStringView{ "grand" },
        declaration_query, declaration_query.object_child(declaration_query.root(), CStringView{ "grand" }), instance_error);
    TEST_EXPECT(ctx, base && child && grand);
    if (!base || !child || !grand)
    {
        return;
    }
    SInstanceEntryView baked_source, live_source, child_before, grand_before;
    const bool instance_entries_ready = baked_instances.entry(baked_instances.find_base(
        CStringView{ "SourceA" }, CStringView{ "one" }), baked_source) &&
        live_instances.entry(live_instances.find_base(CStringView{ "SourceB" }, CStringView{ "source" }), live_source) &&
        live_instances.entry(child, child_before) && live_instances.entry(grand, grand_before) &&
        (baked_source.type == source_a_type) && (live_source.type == source_b_type);
    TEST_EXPECT(ctx, instance_entries_ready);
    if (!instance_entries_ready)
    {
        return;
    }
    std::uint8_t child_saved[3], grand_saved[3];
    std::memcpy(child_saved, child_before.bytes, sizeof(child_saved));
    std::memcpy(grand_saved, grand_before.bytes, sizeof(grand_saved));
    const CInstanceHandle child_declaration = child_before.declaration;
    const CInstanceHandle grand_declaration = grand_before.declaration;
    const CInstanceDocumentQuery instance_query = live_instances.document_query();
    std::uint64_t child_selected{}, grand_selected{};
    TEST_EXPECT(ctx, instance_query.unsigned_integer_value(instance_query.object_child(
        child_declaration, CStringView{ "keep" }), child_selected) && child_selected == 9u &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            grand_declaration, CStringView{ "keep" }), grand_selected) && grand_selected == 7u &&
        !instance_query.object_child(child_declaration, CStringView{ "a" }) &&
        !instance_query.object_child(child_declaration, CStringView{ "b" }) &&
        !instance_query.object_child(grand_declaration, CStringView{ "a" }) &&
        !instance_query.object_child(grand_declaration, CStringView{ "b" }));
    const CByteConstView instance_sources[] = {
        CByteConstView{ baked_source.bytes, static_cast<std::size_t>(baked_source.byte_count) },
        CByteConstView{ live_source.bytes, static_cast<std::size_t>(live_source.byte_count) }
    };
    SMutableInstanceEntryView writable;
    const bool base_ready = live_instances.mutable_entry(base, writable) && (writable.type == destination_type) &&
        !writable.parent && writable.declaration && (writable.byte_count == 3u) && writable.bytes.is_ready();
    TEST_EXPECT(ctx, base_ready);
    if (!base_ready)
    {
        return;
    }
    const CInstanceHandle base_declaration = writable.declaration;
    std::uint64_t base_a{}, base_b{}, base_keep{};
    TEST_EXPECT(ctx, instance_query.unsigned_integer_value(instance_query.object_child(
        base_declaration, CStringView{ "a" }), base_a) && base_a == 0xccu &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            base_declaration, CStringView{ "b" }), base_b) && base_b == 0xccu &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            base_declaration, CStringView{ "keep" }), base_keep) && base_keep == 0xccu);
    TEST_EXPECT(ctx, plan.execute(instance_sources, 2u, writable.bytes) &&
        writable.bytes.data()[0] == 0x71u && writable.bytes.data()[1] == 0x81u &&
        writable.bytes.data()[2] == 0xccu &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            base_declaration, CStringView{ "a" }), base_a) && base_a == 0xccu &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            base_declaration, CStringView{ "b" }), base_b) && base_b == 0xccu &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            base_declaration, CStringView{ "keep" }), base_keep) && base_keep == 0xccu);
    SInstanceEntryView child_after, grand_after;
    TEST_EXPECT(ctx, live_instances.entry(child, child_after) && live_instances.entry(grand, grand_after) &&
        std::memcmp(child_after.bytes, child_saved, sizeof(child_saved)) == 0 &&
        std::memcmp(grand_after.bytes, grand_saved, sizeof(grand_saved)) == 0);
    TEST_EXPECT(ctx, live_instances.mutable_entry(child, writable) && writable.type == destination_type &&
        writable.parent == base && writable.declaration == child_declaration &&
        plan.execute(instance_sources, 2u, writable.bytes));
    TEST_EXPECT(ctx, live_instances.entry(child, child_after) && live_instances.entry(grand, grand_after) &&
        child_after.bytes[0] == 0x71u && child_after.bytes[1] == 0x81u && child_after.bytes[2] == 9u &&
        child_after.declaration == child_declaration &&
        std::memcmp(grand_after.bytes, grand_saved, sizeof(grand_saved)) == 0 &&
        grand_after.declaration == grand_declaration &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            child_after.declaration, CStringView{ "keep" }), child_selected) && child_selected == 9u &&
        instance_query.unsigned_integer_value(instance_query.object_child(
            grand_after.declaration, CStringView{ "keep" }), grand_selected) && grand_selected == 7u &&
        !instance_query.object_child(child_after.declaration, CStringView{ "a" }) &&
        !instance_query.object_child(child_after.declaration, CStringView{ "b" }) &&
        !instance_query.object_child(grand_after.declaration, CStringView{ "a" }) &&
        !instance_query.object_child(grand_after.declaration, CStringView{ "b" }));
    const SMutableInstanceEntryView retained = writable;
    TEST_EXPECT(ctx, !live_instances.mutable_entry({}, writable) && writable.bytes.data() == retained.bytes.data() &&
        writable.declaration == retained.declaration &&
        !live_instances.mutable_entry(baked_instances.find_base(CStringView{ "SourceA" }, CStringView{ "one" }), writable));
    const CInstanceHandle empty = live_instances.capture_base(CStringView{ "Empty" }, CStringView{ "zero" }, {}, instance_error);
    TEST_EXPECT(ctx, empty && live_instances.mutable_entry(empty, writable) && writable.type == empty_type &&
        writable.byte_count == 0u && writable.bytes.is_empty() && writable.bytes.data() == nullptr);
    const SMutableInstanceEntryView empty_saved = writable;
    live_instances.clear();
    TEST_EXPECT(ctx, !live_instances.mutable_entry(empty, writable) && writable.type == empty_saved.type &&
        writable.byte_count == empty_saved.byte_count && writable.bytes.is_empty());
}

static void test_data_remap_kernels(TTestContext& ctx)
{
    const std::string text = R"({"types":{"structures":{
        "One":{"members":[{"a":{"type":"u8"}}]},
        "Two":{"detail":{"size":2},"members":[{"a":{"type":"u8","offset":0}}]},
        "Three":{"detail":{"size":3},"members":[
            {"a":{"type":"u8","offset":0}},{"b":{"type":"u8","offset":1}},
            {"c":{"type":"u8","offset":2}}]},
        "Four":{"detail":{"size":4},"members":[
            {"a":{"type":"u8","offset":0}},{"b":{"type":"u8","offset":1}},
            {"c":{"type":"u8","offset":2}}]},
        "ArrayTwo":{"members":[{"array":{"type":{"element":"u8","count":2}}}]},
        "ArrayThree":{"members":[{"array":{"type":{"element":"u8","count":3}}}]},
        "VectorSource":{"members":[{"vector":{"type":{"element":"u32","count":4}}}]},
        "VectorDestination":{"detail":{"size":32,"alignment":16},"members":[
            {"vector":{"type":{"element":"u32","count":4},"offset":0}}]},
        "Reordered":{"detail":{"size":12,"alignment":4},"members":[
            {"a":{"type":"u32","offset":8}},{"b":{"type":"u32","offset":0}},
            {"c":{"type":"u32","offset":4}}]}
    }}})";
    CBakedDocumentBlock block;
    CResolvedSchema schema;
    if (!resolve(ctx, text, block, schema))
    {
        return;
    }
    CDataRemapPlan plan;
    SRemapDiagnostic error;
    const auto initialise = [&](const char* const source_name, const char* const destination_name)
    {
        const SRemapSourceType input{ &schema, schema.find_type(CStringView{ source_name }) };
        return plan.initialise(&input, 1u, schema, schema.find_type(CStringView{ destination_name }), error);
    };
    const std::uint8_t one_source[2]{ 0x21u, 0x42u };
    const std::uint8_t two_source[4]{ 0x21u, 0xeeu, 0x42u, 0xeeu };
    std::uint8_t target[8];
    const auto execute = [&](const std::uint8_t* const bytes, const std::size_t source_size,
        const std::size_t destination_size)
    {
        std::memset(target, 0xcc, sizeof(target));
        const CByteConstView input{ bytes, source_size, 1u };
        return plan.execute(&input, 1u, CByteView{ target, destination_size, 1u });
    };
    TEST_EXPECT(ctx, initialise("One", "One") && execute(one_source, 2u, 2u) &&
        target[0] == 0x21u && target[1] == 0x42u);
    TEST_EXPECT(ctx, initialise("One", "Two") && execute(one_source, 2u, 4u) &&
        target[0] == 0x21u && target[1] == 0xccu && target[2] == 0x42u && target[3] == 0xccu);
    TEST_EXPECT(ctx, initialise("Two", "One") && execute(two_source, 4u, 2u) &&
        target[0] == 0x21u && target[1] == 0x42u);
    TEST_EXPECT(ctx, initialise("Two", "Two") && execute(two_source, 4u, 4u) &&
        target[0] == 0x21u && target[1] == 0xccu && target[2] == 0x42u && target[3] == 0xccu);
    const std::uint8_t three_source[6]{ 1u, 2u, 3u, 4u, 5u, 6u };
    TEST_EXPECT(ctx, initialise("Three", "Four") && plan.matched_member_count() == 3u &&
        plan.copy_range_count() == 1u && execute(three_source, 6u, 8u) &&
        target[0] == 1u && target[1] == 2u && target[2] == 3u && target[3] == 0xccu &&
        target[4] == 4u && target[5] == 5u && target[6] == 6u && target[7] == 0xccu);
    TEST_EXPECT(ctx, initialise("ArrayTwo", "ArrayThree") &&
        plan.is_ready() && plan.matched_member_count() == 0u && plan.copy_range_count() == 0u);
    alignas(4) const std::uint8_t reordered_source[12]{ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u };
    alignas(4) std::uint8_t reordered_target[12]{};
    const CByteConstView reordered_view{ reordered_source, 12u, 4u };
    TEST_EXPECT(ctx, initialise("Reordered", "Reordered") && plan.matched_member_count() == 3u &&
        plan.copy_range_count() == 1u && plan.execute(&reordered_view, 1u,
            CByteView{ reordered_target, 12u, 4u }) &&
        std::memcmp(reordered_source, reordered_target, 12u) == 0);
    alignas(16) std::uint8_t vector_source[32];
    alignas(16) std::uint8_t vector_target[64];
    for (std::size_t i = 0u; i < sizeof(vector_source); ++i)
    {
        vector_source[i] = static_cast<std::uint8_t>(i + 1u);
    }
    std::memset(vector_target, 0xcc, sizeof(vector_target));
    const CByteConstView vector_view{ vector_source, 32u, 4u };
    TEST_EXPECT(ctx, initialise("VectorSource", "VectorDestination") &&
        plan.execute(&vector_view, 1u, CByteView{ vector_target, 64u, 16u }) &&
        std::memcmp(vector_source, vector_target, 16u) == 0 &&
        std::memcmp(vector_source + 16u, vector_target + 32u, 16u) == 0);
    for (std::size_t i = 16u; i < 32u; ++i)
    {
        TEST_EXPECT(ctx, vector_target[i] == 0xccu && vector_target[i + 32u] == 0xccu);
    }

    CDataRemapPlan detached;
    {
        const std::string temporary_text = R"({"types":{
            "structures":{
                "Source":{"members":[{"bits":{"type":"Bits"}}]},
                "Destination":{"members":[{"bits":{"type":"Bits"}}]}
            },"bit_structures":{"Bits":{"storage":"u8","members":[
                {"flag":{"type":"b8","mask":1}}]}}}})";
        CBakedDocumentBlock temporary_block;
        CResolvedSchema temporary;
        if (!resolve(ctx, temporary_text, temporary_block, temporary))
        {
            return;
        }
        const SRemapSourceType input{ &temporary, temporary.find_type(CStringView{ "Source" }) };
        TEST_EXPECT(ctx, detached.initialise(&input, 1u, temporary,
            temporary.find_type(CStringView{ "Destination" }), error));
    }
    const std::uint8_t bit_source[]{ 0xf3u };
    std::uint8_t bit_destination{};
    const CByteConstView bit_view{ bit_source, 1u, 1u };
    TEST_EXPECT(ctx, detached.execute(&bit_view, 1u, CByteView{ &bit_destination, 1u, 1u }) &&
        bit_destination == 0xf3u);
}

static void test_data_remap_type_exclusions(TTestContext& ctx)
{
    const std::string category_text = R"({"types":{"structures":{
        "Wrapper":{"members":[{"x":{"type":"u8"}}]},
        "Source":{"members":[{"item":{"type":"Wrapper"}},{"keep":{"type":"u8"}}]},
        "Destination":{"members":[{"item":{"type":"u8"}},{"keep":{"type":"u8"}}]}
    }}})";
    CBakedDocumentBlock category_block;
    CResolvedSchema category;
    if (!resolve(ctx, category_text, category_block, category))
    {
        return;
    }
    const SRemapSourceType source{ &category, category.find_type(CStringView{ "Source" }) };
    CDataRemapPlan plan;
    SRemapDiagnostic error;
    TEST_EXPECT(ctx, plan.initialise(&source, 1u, category,
        category.find_type(CStringView{ "Destination" }), error) &&
        plan.matched_member_count() == 1u && plan.copy_range_count() == 1u);
    const std::uint8_t source_bytes[]{ 0x44u, 0x55u };
    std::uint8_t destination_bytes[]{ 0xccu, 0xccu };
    const CByteConstView input{ source_bytes, 2u, 1u };
    TEST_EXPECT(ctx, plan.execute(&input, 1u, CByteView{ destination_bytes, 2u, 1u }) &&
        destination_bytes[0] == 0xccu && destination_bytes[1] == 0x55u);

    const std::string source_text = R"({"types":{"structures":{
        "Inner":{"members":[{"x":{"type":"u8"}}]},
        "Source":{"members":[{"item":{"type":"Inner"}}]}
    }}})";
    const std::string destination_text = R"({"types":{"structures":{
        "Inner":{"detail":{"size":2},"members":[{"x":{"type":"u8","offset":0}}]},
        "Destination":{"members":[{"item":{"type":"Inner"}}]}
    }}})";
    CBakedDocumentBlock source_block, destination_block;
    CResolvedSchema source_schema, destination_schema;
    if (!resolve(ctx, source_text, source_block, source_schema) ||
        !resolve(ctx, destination_text, destination_block, destination_schema))
    {
        return;
    }
    const SRemapSourceType changed{ &source_schema, source_schema.find_type(CStringView{ "Source" }) };
    SFailingAllocator failing{ 0u, 1u };
    memory::CMemoryAllocator allocator{ &failing, &allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CDataRemapPlan empty;
        TEST_EXPECT(ctx, empty.initialise(&changed, 1u, destination_schema,
            destination_schema.find_type(CStringView{ "Destination" }), error) &&
            empty.matched_member_count() == 0u && empty.copy_range_count() == 0u);
        TEST_EXPECT(ctx, failing.calls == 1u);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_data_remap_wide_enum(TTestContext& ctx)
{
    const std::string source_text = R"({"types":{
        "structures":{
            "Nested":{"members":[{"mode":{"type":"Mode"}},{"tail":{"type":"u8"}}]},
            "Source":{"members":[{"nested":{"type":"Nested"}},
                {"array":{"type":{"element":"Mode","count":2}}},
                {"bits":{"type":"Bits"}},{"plain":{"type":"u8"}}]}
        },"enumerations":{"Mode":{"storage":"u8","values":{"first":1}}},
        "bit_structures":{"Bits":{"storage":"u16","members":[
            {"mode":{"type":"Mode","mask":14}}]}}}})";
    const std::string destination_text = R"({"types":{
        "structures":{
            "Nested":{"members":[{"mode":{"type":"Mode"}},{"tail":{"type":"u8"}}]},
            "Destination":{"members":[{"nested":{"type":"Nested"}},
                {"array":{"type":{"element":"Mode","count":2}}},
                {"bits":{"type":"Bits"}},{"plain":{"type":"u8"}}]}
        },"enumerations":{"Mode":{"storage":"u16","values":{"first":1}}},
        "bit_structures":{"Bits":{"storage":"u16","members":[
            {"mode":{"type":"Mode","mask":14}}]}}}})";
    CBakedDocumentBlock source_block, destination_block;
    CResolvedSchema source, destination;
    if (!resolve(ctx, source_text, source_block, source) ||
        !resolve(ctx, destination_text, destination_block, destination))
    {
        return;
    }
    const SRemapSourceType input{ &source, source.find_type(CStringView{ "Source" }) };
    CDataRemapPlan plan;
    SRemapDiagnostic error;
    TEST_EXPECT(ctx, plan.initialise(&input, 1u, destination,
        destination.find_type(CStringView{ "Destination" }), error) &&
        plan.matched_member_count() == 1u && plan.copy_range_count() == 1u);
    SType st, dt;
    SMember sm, dm;
    TEST_EXPECT(ctx, source.type(input.type, st) &&
        destination.type(destination.find_type(CStringView{ "Destination" }), dt) &&
        source.member(source.find_member(input.type, CStringView{ "plain" }), sm) &&
        destination.member(destination.find_member(destination.find_type(CStringView{ "Destination" }),
            CStringView{ "plain" }), dm));
    CByteBuffer source_bytes, destination_bytes;
    TEST_EXPECT(ctx, source_bytes.allocate(static_cast<std::size_t>(st.size),
        static_cast<std::size_t>(st.alignment)) &&
        source_bytes.set_size(static_cast<std::size_t>(st.size)) &&
        destination_bytes.allocate(static_cast<std::size_t>(dt.size),
        static_cast<std::size_t>(dt.alignment)) &&
        destination_bytes.set_size(static_cast<std::size_t>(dt.size)));
    source_bytes.zero_fill();
    source_bytes.data()[static_cast<std::size_t>(sm.offset)] = 0x5au;
    std::memset(destination_bytes.data(), 0xcc, destination_bytes.size());
    const CByteConstView view = source_bytes.const_view();
    TEST_EXPECT(ctx, plan.execute(&view, 1u, destination_bytes.view()));
    for (std::size_t i = 0u; i < destination_bytes.size(); ++i)
    {
        TEST_EXPECT(ctx, destination_bytes.data()[i] == (i == dm.offset ? 0x5au : 0xccu));
    }

    const std::string changed_compound_text = R"({"types":{
        "structures":{
            "Nested":{"members":[{"mode":{"type":"Mode"}},{"tail":{"type":"i8"}}]},
            "Destination":{"members":[{"nested":{"type":"Nested"}},
                {"array":{"type":{"element":"Mode","count":2}}},{"plain":{"type":"u8"}}]}
        },"enumerations":{"Mode":{"storage":"u16","values":{"first":1}}}}})";
    CBakedDocumentBlock changed_compound_block;
    CResolvedSchema changed_compound;
    if (resolve(ctx, changed_compound_text, changed_compound_block, changed_compound))
    {
        TEST_EXPECT(ctx, plan.initialise(&input, 1u, changed_compound,
            changed_compound.find_type(CStringView{ "Destination" }), error) &&
            plan.matched_member_count() == 1u && plan.copy_range_count() == 1u);
    }
}

}   // namespace schema_tests

int run_schema_tests()
{
    tests::TTestContext ctx;
    schema_tests::test_success(ctx);
    schema_tests::test_empty_types(ctx);
    schema_tests::test_explicit_layout(ctx);
    schema_tests::test_failures(ctx);
    schema_tests::test_schema_sample(ctx);
    schema_tests::test_unorm_defaults(ctx);
    schema_tests::test_snorm_defaults(ctx);
    schema_tests::test_scalars_and_limits(ctx);
    schema_tests::test_record_layout(ctx);
    schema_tests::test_review_regressions(ctx);
    schema_tests::test_representation(ctx);
    schema_tests::test_local_type_references(ctx);
    schema_tests::test_allocations(ctx);
    schema_tests::test_document_read_boundary(ctx);
    schema_tests::test_instance_document_query(ctx);
    schema_tests::test_live_baked_resolution_parity(ctx);
    schema_tests::test_live_resolution_move_failure_and_depth(ctx);
    schema_tests::test_schema_wrapper_queries_and_transfer(ctx);
    schema_tests::test_schema_wrapper_editor(ctx);
    schema_tests::test_schema_wrapper_bindings(ctx);
    schema_tests::test_schema_wrapper_rejections(ctx);
    schema_tests::test_schema_wrapper_edit_allocations(ctx);
    schema_tests::test_schema_root_member_translation(ctx);
    schema_tests::test_schema_conversion(ctx);
    schema_tests::test_schema_conversion_allocations(ctx);
    schema_tests::test_value_codec(ctx);
    schema_tests::test_baked_bulk(ctx);
    schema_tests::test_live_bulk(ctx);
    schema_tests::test_live_bulk_output(ctx);
    schema_tests::test_live_bulk_output_boundaries(ctx);
    schema_tests::test_live_bulk_output_allocations(ctx);
    schema_tests::test_live_bulk_failures_and_matching(ctx);
    schema_tests::test_baked_instances(ctx);
    schema_tests::test_live_instances(ctx);
    schema_tests::test_live_instance_output(ctx);
    schema_tests::test_live_instance_output_boundaries(ctx);
    schema_tests::test_live_instance_boundaries(ctx);
    schema_tests::test_baked_instance_rejections(ctx);
    schema_tests::test_baked_instance_boundaries(ctx);
    schema_tests::test_baked_instance_staging(ctx);
    schema_tests::test_bulk_encoded_comparison(ctx);
    schema_tests::test_unused_storage_values(ctx);
    schema_tests::test_unused_storage_documents(ctx);
    schema_tests::test_data_remap(ctx);
    schema_tests::test_data_remap_role_views(ctx);
    schema_tests::test_data_remap_matching(ctx);
    schema_tests::test_data_remap_kernels(ctx);
    schema_tests::test_data_remap_type_exclusions(ctx);
    schema_tests::test_data_remap_wide_enum(ctx);
    const schema::SRecordSizes sizes = schema::CResolvedSchema::record_sizes();
    std::cout << "Schema record bytes: type=" << sizes.type << " member=" << sizes.member << " label=" << sizes.label
              << " field=" << sizes.field << " default=" << sizes.default_value << " mapping=" << sizes.mapping << '\n';
    std::cout << "Schema: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed == 0 ? 0 : 1;
}
