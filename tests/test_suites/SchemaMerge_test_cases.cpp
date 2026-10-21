//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaMerge_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Oct 26
//
//  Type-schema merge semantics, transactional failure and construction growth.

#include "tests/test_suites/SchemaMerge_test_cases.hpp"
#include "schema/schema_merge.hpp"
#include "schema/schema_wrappers.hpp"
#include "schema/live_instances.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/live_document.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "platform/system/performance_counter.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>

namespace
{
using namespace schema;
using tests::TTestContext;

static_assert(std::is_trivially_copyable_v<SSchemaMergeDiagnostic>);

static bool parse(const std::string& text, CLiveDocument& document)
{
    return document_parser::parse(CByteConstView{
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), 1u }, document).accepted();
}

static std::string schema_text(const std::string& descriptor, const std::string& detail = {})
{
    return R"({"types":{"structures":{"Item":{)" + detail + R"("members":[{"v":)" + descriptor + "}]}}}}";
}

static std::string scalar_schema(const std::string& type, const std::string& value = {})
{
    return schema_text("{\"type\":\"" + type + "\"" + (value.empty() ? "" : ",\"default\":" + value) + "}");
}

template<class TDocument>
static auto member_default(const TDocument& query)
{
    const auto types = query.object_child(query.root(), CStringView{ "types" });
    const auto structures = query.object_child(types, CStringView{ "structures" });
    const auto item = query.object_child(structures, CStringView{ "Item" });
    const auto members = query.object_child(item, CStringView{ "members" });
    return query.object_child(query.first_child(members), CStringView{ "default" });
}

static bool same_document(const CLiveDocument& document, const CBakedDocumentBlock& expected)
{
    CBakedDocumentBlock actual;
    return document_translation::bake(document, actual) && (actual.bytes().size() == expected.bytes().size()) &&
        (std::memcmp(actual.bytes().data(), expected.bytes().data(), actual.bytes().size()) == 0);
}

static void expect_merge(TTestContext& ctx, const std::string& first, const std::string& second,
    const ESchemaMergeReason reason = ESchemaMergeReason::none)
{
    CLiveDocument a, b, output;
    TEST_EXPECT(ctx, parse(first, a) && parse(second, b));
    TEST_EXPECT(ctx, parse(R"({"sentinel":17})", output));
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(output, before));
    const CNodeKey sentinel = output.first_child(output.root());
    const CSchemaDocumentQuery aq{ a }, bq{ b };
    SSchemaMergeDiagnostic diagnostic;
    const bool merged = merge_type_schemas(aq, bq, output, diagnostic);
    if (merged != (reason == ESchemaMergeReason::none) || diagnostic.reason != reason)
    {
        std::cerr << "Merge reason=" << static_cast<unsigned>(diagnostic.reason) << " stage=" << static_cast<unsigned>(diagnostic.stage)
            << " expected=" << static_cast<unsigned>(reason) << " first=" << first << " second=" << second << '\n';
    }
    TEST_EXPECT(ctx, merged == (reason == ESchemaMergeReason::none));
    TEST_EXPECT(ctx, diagnostic.reason == reason);
    if (merged)
    {
        TEST_EXPECT(ctx, output.check_integrity() && output.is_complete());
        TEST_EXPECT(ctx, output.child_count(output.root()) == 1u);
        TEST_EXPECT(ctx, diagnostic.stage == ESchemaMergeStage::none && !diagnostic.first && !diagnostic.second);
        CResolvedSchema resolved;
        SDiagnostic validation;
        TEST_EXPECT(ctx, resolved.resolve(output, validation));
    }
    else
    {
        TEST_EXPECT(ctx, output.contains(sentinel) && same_document(output, before));
        if ((reason == ESchemaMergeReason::conflicting_value) || (reason == ESchemaMergeReason::incompatible_type))
        {
            TEST_EXPECT(ctx, aq.contains(diagnostic.first) && bq.contains(diagnostic.second));
            TEST_EXPECT(ctx, diagnostic.validation.reason == EReason::none);
        }
    }
}

static void test_rules(TTestContext& ctx)
{
    const std::string empty = R"({"types":{}})";
    expect_merge(ctx, empty, empty);
    expect_merge(ctx, scalar_schema("u32"), scalar_schema("u32", "10"));
    expect_merge(ctx, scalar_schema("u32", "10"), scalar_schema("u32"));
    expect_merge(ctx, scalar_schema("u32", "0"), scalar_schema("u32", "10"), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, scalar_schema("u32", "10"), scalar_schema("i32", "10"), ESchemaMergeReason::incompatible_type);
    expect_merge(ctx, scalar_schema("f64", "0.0"), scalar_schema("f64", "-0.0"), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, scalar_schema("f64", "1"), scalar_schema("f64", "1.0"), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, scalar_schema("u64", "18446744073709551615"), scalar_schema("u64", "0xffffffffffffffff"));
    expect_merge(ctx, scalar_schema("i64", "-1"), scalar_schema("i64", "-0x01"));
    expect_merge(ctx, scalar_schema("f64", "-1"), scalar_schema("f64", "18446744073709551615"), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, scalar_schema("f64", "\"nan\""), scalar_schema("f64", "\"NaN\""), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, scalar_schema("u32"), schema_text(R"({"type":"u32","offset":0})", R"("detail":{"size":4,"alignment":4},)"));
    expect_merge(ctx, scalar_schema("u32"), schema_text(R"({"type":"u32"})", R"("detail":{"alignment":8},)"), ESchemaMergeReason::incompatible_type);
    expect_merge(ctx, schema_text(R"({"type":"u32"})", R"("detail":{"internal":true},)"),
        schema_text(R"({"type":"u32"})", R"("detail":{"internal":false},)"), ESchemaMergeReason::conflicting_value);
    const std::string array = R"({"type":{"element":"u32","count":3})";
    expect_merge(ctx, schema_text(array + "}"), schema_text(array + R"(,"default":[1,2]})"));
    expect_merge(ctx, schema_text(array + R"(,"default":[1]})"), schema_text(array + R"(,"default":[1,2]})"), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, schema_text(array + R"(,"default":[1,2]})"), schema_text(array + R"(,"default":[1,3]})"), ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, schema_text(R"({"type":{"element":{"element":"u32","count":2},"count":2},"default":[[1],[]]})"),
        schema_text(R"({"type":{"element":{"element":"u32","count":2},"count":2}})"));
    expect_merge(ctx, schema_text(array + "}"), schema_text(R"({"type":{"element":"u32","count":4}})"), ESchemaMergeReason::incompatible_type);

    const std::string order_a = R"({"types":{"structures":{"Item":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]}}}})";
    const std::string order_b = R"({"types":{"structures":{"Item":{"members":[{"b":{"type":"u8"}},{"a":{"type":"u8"}}]}}}})";
    expect_merge(ctx, order_a, order_b, ESchemaMergeReason::incompatible_type);
    expect_merge(ctx, R"({"types":{"enumerations":{"E":{"storage":"u8","values":{"a":0,"b":1}}}}})",
        R"({"types":{"enumerations":{"E":{"storage":"u8","values":{"b":1,"a":0}}}}})", ESchemaMergeReason::incompatible_type);
    expect_merge(ctx, R"({"types":{"enumerations":{"Item":{"storage":"u8","values":{"a":0}}}}})",
        scalar_schema("u8"), ESchemaMergeReason::incompatible_type);
    const std::string references = R"({"types":{"structures":{"A":{"members":[]},"B":{"members":[]},"Item":{"members":[{"v":{"type":")";
    expect_merge(ctx, references + "A\"}}]}}}}", references + "B\"}}]}}}}", ESchemaMergeReason::incompatible_type);
    const std::string fields = R"({"types":{"bit_structures":{"Flags":{"storage":"u8","members":[{"v":{"type":"u8","mask":3)";
    expect_merge(ctx, fields + "}}]}}}}", fields + ",\"default\":2}}]}}}}");
    expect_merge(ctx, fields + ",\"default\":1}}]}}}}", fields + ",\"default\":2}}]}}}}", ESchemaMergeReason::conflicting_value);
    expect_merge(ctx, fields + "}}]}}}}", R"({"types":{"bit_structures":{"Flags":{"storage":"u8","members":[{"v":{"type":"u8","mask":7}}]}}}})", ESchemaMergeReason::incompatible_type);
    expect_merge(ctx, scalar_schema("Missing"), R"({"types":{"structures":{"Missing":{"members":[]}}}})", ESchemaMergeReason::invalid_source);
    expect_merge(ctx, empty, scalar_schema("u8", "300"), ESchemaMergeReason::invalid_source);
    expect_merge(ctx, empty, schema_text(R"({"type":"u32","description":"text"})"), ESchemaMergeReason::invalid_source);
    expect_merge(ctx, empty, R"({"types":{"structures":{"Inner":{"members":[]},"Item":{"members":[{"v":{"type":"Inner","default":{}}}]}}}})", ESchemaMergeReason::invalid_source);
    expect_merge(ctx, R"({"types":{},"instances":{},"data":{},"stripped":true})", empty);

    CLiveDocument a, result;
    TEST_EXPECT(ctx, parse(empty, a));
    SSchemaMergeDiagnostic diagnostic;
    TEST_EXPECT(ctx, !merge_type_schemas({}, CSchemaDocumentQuery{ a }, result, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == ESchemaMergeStage::first_source_validation && !result.is_ready());
    TEST_EXPECT(ctx, !merge_type_schemas(CSchemaDocumentQuery{ a }, {}, result, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == ESchemaMergeStage::second_source_validation);
}

static void test_presentation(TTestContext& ctx)
{
    struct SCase { const char* first; const char* second; EIntegerNotation expected; };
    const SCase cases[] = {
        { "1", "0x01", EIntegerNotation::hexadecimal_2 },
        { "0x01", "0x00000001", EIntegerNotation::hexadecimal_8 },
        { "0x0001", "0b0000000000000001", EIntegerNotation::binary_16 },
        { "0x00000001", "0b0000000000000001", EIntegerNotation::hexadecimal_8 },
        { "0x0001", "0b00000000000000000000000000000001", EIntegerNotation::binary_32 }
    };
    for (const auto& item : cases)
    {
        for (unsigned reverse = 0u; reverse < 2u; ++reverse)
        {
            CLiveDocument a, b, result;
            TEST_EXPECT(ctx, parse(scalar_schema("u32", reverse ? item.second : item.first), a));
            TEST_EXPECT(ctx, parse(scalar_schema("u32", reverse ? item.first : item.second), b));
            SSchemaMergeDiagnostic diagnostic;
            TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ b }, result, diagnostic));
            CIntegerMetadata metadata;
            const CSchemaDocumentQuery query{ result };
            TEST_EXPECT(ctx, query.integer_metadata(member_default(query), metadata));
            TEST_EXPECT(ctx, metadata.notation == item.expected);
            TEST_EXPECT(ctx, result.check_integrity());
        }
    }

    struct SMetadataCase
    {
        EIntegerNotation first, second, expected;
        EIntegerPrefix first_prefix, second_prefix, expected_prefix;
    };
    const SMetadataCase metadata_cases[] = {
        { EIntegerNotation::decimal, EIntegerNotation::decimal_or_hexadecimal, EIntegerNotation::decimal_or_hexadecimal,
          EIntegerPrefix::standard, EIntegerPrefix::alternate, EIntegerPrefix::alternate },
        { EIntegerNotation::decimal_or_hexadecimal, EIntegerNotation::hexadecimal_2, EIntegerNotation::hexadecimal_2,
          EIntegerPrefix::alternate, EIntegerPrefix::standard, EIntegerPrefix::standard },
        { EIntegerNotation::decimal_or_hexadecimal, EIntegerNotation::decimal_or_hexadecimal, EIntegerNotation::decimal_or_hexadecimal,
          EIntegerPrefix::alternate, EIntegerPrefix::alternate, EIntegerPrefix::alternate },
        { EIntegerNotation::hexadecimal_2, EIntegerNotation::hexadecimal_8, EIntegerNotation::hexadecimal_8,
          EIntegerPrefix::standard, EIntegerPrefix::alternate, EIntegerPrefix::alternate }
    };
    for (const auto& item : metadata_cases)
    {
        CLiveDocument a, b, result;
        TEST_EXPECT(ctx, parse(scalar_schema("u32", "128"), a) && parse(scalar_schema("u32", "128"), b));
        //  Give equal values different domains and measured widths. Presentation
        //  selection must neither change the value nor copy an incompatible width.
        const auto replace = [&](CLiveDocument& document, const bool first)
        {
            const CNodeKey types = document.object_child(document.root(), CStringView{ "types" });
            const CNodeKey structures = document.object_child(types, CStringView{ "structures" });
            const CNodeKey members = document.object_child(document.first_child(structures), CStringView{ "members" });
            const CNodeKey value = document.object_child(document.first_child(members), CStringView{ "default" });
            const CIntegerMetadata metadata{
                first ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value,
                first ? EIntegerWidth::bits_16 : EIntegerWidth::bits_8,
                first ? item.first : item.second,
                first ? item.first_prefix : item.second_prefix };
            const CNodeKey replacement = first ? document.create_signed_integer(128, metadata) : document.create_unsigned_integer(128u, metadata);
            return replacement && document.erase_payload(value) && document.attach_payload(value, replacement);
        };
        TEST_EXPECT(ctx, replace(a, true) && replace(b, false));
        SSchemaMergeDiagnostic diagnostic;
        TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ b }, result, diagnostic));
        const CSchemaDocumentQuery query{ result };
        CIntegerMetadata metadata;
        TEST_EXPECT(ctx, query.integer_metadata(member_default(query), metadata));
        TEST_EXPECT(ctx, metadata.notation == item.expected && metadata.prefix == item.expected_prefix);
        TEST_EXPECT(ctx, metadata.domain == EIntegerDomain::signed_value && metadata.width == EIntegerWidth::bits_16);
        TEST_EXPECT(ctx, result.check_integrity());
    }
}

static void test_newline_presentation(TTestContext& ctx)
{
    for (unsigned flags = 0u; flags < 4u; ++flags)
    {
        const bool first_suppresses = (flags & 1u) != 0u;
        const bool second_suppresses = (flags & 2u) != 0u;
        for (unsigned form = 0u; form < 4u; ++form)
        {
            CLiveDocument a, b, result;
            //  The schema grammar admits special floating-point strings. Their
            //  presentation flags are retained even without newline characters.
            TEST_EXPECT(ctx, parse(scalar_schema("f64", "\"nan\""), a));
            TEST_EXPECT(ctx, parse(scalar_schema("f64", "\"nan\""), b));
            TEST_EXPECT(ctx, a.set_newline_escaping_suppressed(member_default(a), first_suppresses));
            TEST_EXPECT(ctx, b.set_newline_escaping_suppressed(member_default(b), second_suppresses));
            CBakedDocumentBlock ab, bb;
            TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb));
            const CSchemaDocumentQuery aq = (form & 1u) ? CSchemaDocumentQuery{ ab.document() } : CSchemaDocumentQuery{ a };
            const CSchemaDocumentQuery bq = (form & 2u) ? CSchemaDocumentQuery{ bb.document() } : CSchemaDocumentQuery{ b };
            TEST_EXPECT(ctx, aq.suppresses_newline_escaping(member_default(aq)) == first_suppresses);
            TEST_EXPECT(ctx, bq.suppresses_newline_escaping(member_default(bq)) == second_suppresses);

            SSchemaMergeDiagnostic diagnostic;
            TEST_EXPECT(ctx, merge_type_schemas(aq, bq, result, diagnostic));
            const CSchemaDocumentQuery query{ result };
            const CSchemaHandle value = member_default(query);
            TEST_EXPECT(ctx, query.string_value(value) == CStringView{ "nan" });
            TEST_EXPECT(ctx, query.suppresses_newline_escaping(value) == (first_suppresses && second_suppresses));
            TEST_EXPECT(ctx, result.check_integrity());
        }
    }
}

static void test_one_sided_presentation(TTestContext& ctx)
{
    const CIntegerMetadata expected{ EIntegerDomain::signed_value, EIntegerWidth::bits_16,
        EIntegerNotation::hexadecimal_8, EIntegerPrefix::alternate };
    for (const bool suppress : { false, true })
    {
        for (const bool empty : { false, true })
        {
            for (unsigned form = 0u; form < 4u; ++form)
            {
                for (const bool reverse : { false, true })
                {
                    CLiveDocument source, other, result;
                    TEST_EXPECT(ctx, parse(schema_text(R"({"type":{"element":"f64","count":3},"default":[128,"nan"]})"), source));
                    TEST_EXPECT(ctx, parse(empty ? R"({"types":{}})" :
                        schema_text(R"({"type":{"element":"f64","count":3}})"), other));
                    const CNodeKey number = source.first_child(member_default(source));
                    const CNodeKey string = source.next_sibling(number);
                    const CNodeKey replacement = source.create_signed_integer(128, expected);
                    TEST_EXPECT(ctx, replacement && source.erase_payload(number) && source.attach_payload(number, replacement));
                    TEST_EXPECT(ctx, source.set_newline_escaping_suppressed(string, suppress));
                    CBakedDocumentBlock source_baked, other_baked;
                    TEST_EXPECT(ctx, document_translation::bake(source, source_baked) && document_translation::bake(other, other_baked));
                    const CSchemaDocumentQuery supplied = (form & 1u) ? CSchemaDocumentQuery{ source_baked.document() } : CSchemaDocumentQuery{ source };
                    const CSchemaDocumentQuery absent = (form & 2u) ? CSchemaDocumentQuery{ other_baked.document() } : CSchemaDocumentQuery{ other };

                    //  Exercise both whole-subtree copying and a missing default
                    //  inside a shared type, with the supplied source on either side.
                    SSchemaMergeDiagnostic diagnostic;
                    TEST_EXPECT(ctx, merge_type_schemas((reverse ? absent : supplied), (reverse ? supplied : absent), result, diagnostic));
                    const CSchemaDocumentQuery query{ result };
                    const CSchemaHandle defaults = member_default(query);
                    TEST_EXPECT(ctx, query.child_count(defaults) == 2u);
                    const CSchemaHandle copied_number = query.first_child(defaults);
                    const CSchemaHandle copied_string = query.next_sibling(copied_number);
                    CIntegerMetadata actual;
                    std::int64_t value{};
                    TEST_EXPECT(ctx, query.signed_integer_value(copied_number, value) && (value == 128));
                    TEST_EXPECT(ctx, query.integer_metadata(copied_number, actual));
                    TEST_EXPECT(ctx, (actual.domain == expected.domain) && (actual.width == expected.width));
                    TEST_EXPECT(ctx, (actual.notation == expected.notation) && (actual.prefix == expected.prefix));
                    TEST_EXPECT(ctx, query.string_value(copied_string) == CStringView{ "nan" });
                    TEST_EXPECT(ctx, query.suppresses_newline_escaping(copied_string) == suppress);
                    TEST_EXPECT(ctx, !query.next_sibling(copied_string));
                    TEST_EXPECT(ctx, result.check_integrity());
                }
            }
        }
    }
}

static void test_forms_and_aliasing(TTestContext& ctx)
{
    for (unsigned form = 0u; form < 4u; ++form)
    {
        CLiveDocument a, b, result;
        TEST_EXPECT(ctx, parse(scalar_schema("u32"), a));
        TEST_EXPECT(ctx, parse(scalar_schema("u32", "10"), b));
        CBakedDocumentBlock ab, bb;
        TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb));
        const CSchemaDocumentQuery aq = (form & 1u) ? CSchemaDocumentQuery{ ab.document() } : CSchemaDocumentQuery{ a };
        const CSchemaDocumentQuery bq = (form & 2u) ? CSchemaDocumentQuery{ bb.document() } : CSchemaDocumentQuery{ b };
        SSchemaMergeDiagnostic diagnostic;
        TEST_EXPECT(ctx, merge_type_schemas(aq, bq, result, diagnostic));
        a.deallocate();
        b.deallocate();
        ab = {};
        bb = {};
        CResolvedSchema resolved;
        SDiagnostic validation;
        TEST_EXPECT(ctx, resolved.resolve(result, validation));
        SMember member;
        SDefault value;
        TEST_EXPECT(ctx, resolved.member(resolved.find_member(resolved.find_type(CStringView{ "Item" }), CStringView{ "v" }), member));
        TEST_EXPECT(ctx, resolved.default_value(member.type, member.default_description, value));
        TEST_EXPECT(ctx, value.scalar.value.unsigned_value == 10u);
    }
    for (unsigned alias = 0u; alias < 4u; ++alias)
    {
        CLiveDocument a, b, separate;
        TEST_EXPECT(ctx, parse(scalar_schema("u32", "10"), a));
        TEST_EXPECT(ctx, parse(scalar_schema("u32"), b));
        CLiveDocument& result = (alias == 0u) ? separate : ((alias == 2u) ? b : a);
        SSchemaMergeDiagnostic diagnostic;
        TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ (alias == 3u) ? a : b }, result, diagnostic));
        TEST_EXPECT(ctx, result.check_integrity());
        CSchemaDocumentQuery query{ result };
        std::uint64_t value{};
        TEST_EXPECT(ctx, query.unsigned_integer_value(member_default(query), value) && (value == 10u));
    }
}

static std::string wide_schema(const unsigned count, const bool reverse)
{
    std::string text = R"({"types":{"structures":{)";
    for (unsigned i = 0u; i < count; ++i)
    {
        if (i) text += ',';
        text += "\"T" + std::to_string(reverse ? (count - i - 1u) : i) + R"(":{"members":[{"v":{"type":"u32","default":7}}]})";
    }
    return text + "}}}";
}

static void test_order_and_growth(TTestContext& ctx)
{
    for (const unsigned count : { 0u, 1u, 7u, 8u, 9u, 128u, 512u, 2048u })
    {
        CLiveDocument a, b, result;
        TEST_EXPECT(ctx, parse(wide_schema(count, true), a));
        TEST_EXPECT(ctx, parse(wide_schema((count + 1u), false), b));
        SSchemaMergeDiagnostic diagnostic;
        platform::system::CPerfCounter timer;
        (void)timer.update();
        TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ b }, result, diagnostic));
        const std::uint64_t elapsed = timer.query_delta();
        platform::system::CPerfCountConversion conversion;
        std::uint64_t micros{};
        (void)conversion.init();
        (void)conversion.to_microseconds_u64(elapsed, micros);
        const CNodeKey types = result.object_child(result.root(), CStringView{ "types" });
        const CNodeKey structures = result.object_child(types, CStringView{ "structures" });
        TEST_EXPECT(ctx, result.child_count(structures) == (count + 1u));
        CNodeKey child = result.first_child(structures);
        for (unsigned i = 0u; i < count; ++i)
        {
            const std::string expected = "T" + std::to_string(count - i - 1u);
            TEST_EXPECT(ctx, result.name(child) == CStringView{ expected.c_str() });
            child = result.next_sibling(child);
        }
        const std::string last = "T" + std::to_string(count);
        TEST_EXPECT(ctx, result.name(child) == CStringView{ last.c_str() });
        TEST_EXPECT(ctx, result.check_integrity());
        if (count >= 128u) std::cout << "Schema merge scaling: " << count << " overlapping types, " << micros << " us\n";
    }
}

static void test_depth(TTestContext& ctx)
{
    //  types=0, structures=1, Item=2, members=3, v=4, type=5.
    //  Each array descriptor adds one edge to its scalar element leaf.
    for (const unsigned arrays : { 250u, 251u })
    {
        std::string type = "\"u8\"";
        for (unsigned i = 0u; i < arrays; ++i)
        {
            type = "{\"count\":1,\"element\":" + type + "}";
        }
        for (unsigned shared = 0u; shared < 2u; ++shared)
        {
            CLiveDocument a, b, output;
            const std::string deep = schema_text("{\"type\":" + type + "}");
            TEST_EXPECT(ctx, parse(deep, a));
            TEST_EXPECT(ctx, parse(shared ? deep : R"({"types":{}})", b));
            CResolvedSchema source;
            SDiagnostic validation;
            TEST_EXPECT(ctx, source.resolve(a, validation));
            TEST_EXPECT(ctx, parse(R"({"sentinel":4})", output));
            const CNodeKey sentinel = output.first_child(output.root());
            SSchemaMergeDiagnostic diagnostic;
            const bool merged = merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ b }, output, diagnostic);
            TEST_EXPECT(ctx, merged == (arrays == 250u));
            if (!merged)
            {
                TEST_EXPECT(ctx, diagnostic.stage == ESchemaMergeStage::document_merge && diagnostic.reason == ESchemaMergeReason::storage_limit);
                TEST_EXPECT(ctx, CSchemaDocumentQuery{ a }.contains(diagnostic.first) && output.contains(sentinel));
            }
            else
            {
                TEST_EXPECT(ctx, output.check_integrity());
            }
        }
    }
}

static void test_failures(TTestContext& ctx)
{
    bool saw_result_failure = false;
    for (unsigned alias = 0u; alias < 4u; ++alias)
    {
        bool completed = false;
        for (std::size_t fail = 0u; (fail < 2048u) && !completed; ++fail)
        {
            CLiveDocument a, b, separate;
            TEST_EXPECT(ctx, parse(wide_schema(9u, true), a));
            TEST_EXPECT(ctx, parse(wide_schema(10u, false), b));
            TEST_EXPECT(ctx, parse(R"({"sentinel":4})", separate));
            CLiveDocument& destination = (alias == 0u) ? separate : ((alias == 2u) ? b : a);
            CBakedDocumentBlock before_a, before_b, before_output;
            TEST_EXPECT(ctx, document_translation::bake(a, before_a) && document_translation::bake(b, before_b));
            TEST_EXPECT(ctx, document_translation::bake(destination, before_output));
            const CNodeKey retained = destination.first_child(destination.root());
            tests::TAllocationFailureFixture fixture{ 0u, fail };
            memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            SSchemaMergeDiagnostic diagnostic;
            {
                tests::TMemoryContextScope scope{ &context };
                completed = merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ (alias == 3u) ? a : b }, destination, diagnostic);
            }
            if (!completed)
            {
                TEST_EXPECT(ctx, diagnostic.reason != ESchemaMergeReason::none);
                TEST_EXPECT(ctx, same_document(a, before_a) && same_document(b, before_b));
                TEST_EXPECT(ctx, destination.contains(retained) && same_document(destination, before_output));
                if (diagnostic.stage == ESchemaMergeStage::result_validation)
                {
                    saw_result_failure = true;
                    TEST_EXPECT(ctx, !diagnostic.first && !diagnostic.second && !diagnostic.validation.occurrence &&
                        !diagnostic.validation.enclosing_type && !diagnostic.validation.enclosing_member && !diagnostic.validation.related);
                    TEST_EXPECT(ctx, !diagnostic.validation.ranges_available && (diagnostic.validation.range_begin == 0u) &&
                        (diagnostic.validation.range_end == 0u) && (diagnostic.validation.related_begin == 0u) && (diagnostic.validation.related_end == 0u));
                }
            }
            destination.deallocate();
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed);
    }
    TEST_EXPECT(ctx, saw_result_failure);
}

static void test_integration(TTestContext& ctx)
{
    CLiveDocument a, b, merged;
    TEST_EXPECT(ctx, parse(scalar_schema("u32"), a) && parse(scalar_schema("u32", "10"), b));
    SSchemaMergeDiagnostic diagnostic;
    TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ b }, merged, diagnostic));
    CLiveSchema schema;
    SDiagnostic validation;
    TEST_EXPECT(ctx, schema.try_adopt(std::move(merged)) && schema.resolve(validation));
    CBakedDocumentBlock baked;
    CBakedSchema baked_schema;
    TEST_EXPECT(ctx, schema.bake(baked, baked_schema));
    CLiveInstances instances;
    SInstanceDiagnostic instance_error;
    TEST_EXPECT(ctx, instances.initialise(schema));
    const CInstanceHandle implicit = instances.create_base(CStringView{ "Item" }, CStringView{ "implicit" }, {}, {}, instance_error);
    CLiveDocument declaration;
    TEST_EXPECT(ctx, parse(R"({"v":0})", declaration));
    const CInstanceDocumentQuery query{ declaration };
    const CInstanceHandle explicit_value = instances.create_base(CStringView{ "Item" }, CStringView{ "explicit" }, query, query.root(), instance_error);
    SInstanceEntryView entry;
    std::uint32_t value{};
    TEST_EXPECT(ctx, instances.entry(implicit, entry) && (entry.byte_count == sizeof(value)));
    if (entry.byte_count == sizeof(value)) std::memcpy(&value, entry.bytes, sizeof(value));
    TEST_EXPECT(ctx, value == 10u);
    TEST_EXPECT(ctx, instances.entry(explicit_value, entry) && (entry.byte_count == sizeof(value)));
    if (entry.byte_count == sizeof(value)) std::memcpy(&value, entry.bytes, sizeof(value));
    TEST_EXPECT(ctx, value == 0u);
    tests::TAssertionTestScope assertions{ ctx };
    assertions.expect_unconditional_assertion(ctx, [&]() { TEST_EXPECT(ctx, !schema.reset()); });
}

static void test_shared_dependencies_and_omissions(TTestContext& ctx)
{
    std::string text = R"({"types":{"structures":{"T0":{"members":[]})";
    for (unsigned i = 1u; i <= 128u; ++i)
    {
        const std::string previous = "T" + std::to_string(i - 1u);
        text += ",\"T" + std::to_string(i) + "\":{\"members\":[{\"a\":{\"type\":\"" + previous +
            "\"}},{\"b\":{\"type\":\"" + previous + "\"}}]}";
    }
    text += "}}}";
    expect_merge(ctx, text, text);

    CLiveDocument a, b, result;
    TEST_EXPECT(ctx, parse(scalar_schema("u32"), a) && parse(scalar_schema("u32"), b));
    SSchemaMergeDiagnostic diagnostic;
    TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ a }, CSchemaDocumentQuery{ b }, result, diagnostic));
    const CSchemaDocumentQuery query{ result };
    TEST_EXPECT(ctx, !member_default(query));
    const auto types = query.object_child(query.root(), CStringView{ "types" });
    const auto structures = query.object_child(types, CStringView{ "structures" });
    TEST_EXPECT(ctx, !query.object_child(query.first_child(structures), CStringView{ "detail" }));

    //  Conflicts retain handles in the correct source representation as well.
    for (unsigned form = 0u; form < 4u; ++form)
    {
        TEST_EXPECT(ctx, parse(scalar_schema("u32", "10"), a) && parse(scalar_schema("u32", "11"), b));
        CBakedDocumentBlock ab, bb, before;
        TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb));
        TEST_EXPECT(ctx, document_translation::bake(result, before));
        const CSchemaDocumentQuery aq = (form & 1u) ? CSchemaDocumentQuery{ ab.document() } : CSchemaDocumentQuery{ a };
        const CSchemaDocumentQuery bq = (form & 2u) ? CSchemaDocumentQuery{ bb.document() } : CSchemaDocumentQuery{ b };
        TEST_EXPECT(ctx, !merge_type_schemas(aq, bq, result, diagnostic));
        TEST_EXPECT(ctx, diagnostic.reason == ESchemaMergeReason::conflicting_value);
        TEST_EXPECT(ctx, diagnostic.first == member_default(aq) && diagnostic.second == member_default(bq));
        TEST_EXPECT(ctx, same_document(result, before));
    }
}
}

void run_schema_merge_cases(tests::TTestContext& context)
{
    test_rules(context);
    test_presentation(context);
    test_newline_presentation(context);
    test_one_sided_presentation(context);
    test_forms_and_aliasing(context);
    test_order_and_growth(context);
    test_depth(context);
    test_failures(context);
    test_integration(context);
    test_shared_dependencies_and_omissions(context);
}
