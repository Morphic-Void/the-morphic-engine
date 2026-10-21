//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DataMerge_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Oct 26
//
//  Prepared data aggregation, admission parity and transactional publication.

#include "tests/test_suites/DataMerge_test_cases.hpp"
#include "schema/data_merge.hpp"
#include "schema/schema_merge.hpp"
#include "schema/value_codec.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "data_model/document_translation.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
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
using detail::CDocumentRead;
using detail::SOccurrence;

static_assert(std::is_trivially_copyable_v<SInstanceMergeDiagnostic>);
static_assert(std::is_trivially_copyable_v<SBulkDataMergeDiagnostic>);

static const char* const k_schema = R"({"types":{
    "enumerations":{"E":{"storage":"u8","values":{"red":0,"blue":1}}},
    "structures":{
        "Pair":{"members":[{"a":{"type":"u32","default":7}},{"b":{"type":"u32","default":9}}]},
        "One":{"members":[{"v":{"type":"u32"}}]},
        "Empty":{"members":[]},
        "Array":{"members":[{"data":{"type":{"element":"u32","count":2}}}]},
        "Nested":{"members":[{"v":{"type":"One"}}]}
    },
    "bit_structures":{"Bits":{"storage":"u8","members":[{"v":{"type":"u8","mask":3}}]},
        "Normal":{"storage":"u8","members":[{"v":{"type":"u8","mask":15,"interpretation":"unorm"}}]}}
}})";

template<bool Bulk>
using TQuery = std::conditional_t<Bulk, CBulkDataDocumentQuery, CInstanceDocumentQuery>;
template<bool Bulk>
using TDiagnostic = std::conditional_t<Bulk, SBulkDataMergeDiagnostic, SInstanceMergeDiagnostic>;

template<bool Bulk>
static bool merge(const TQuery<Bulk>& first, const TQuery<Bulk>& second, const CResolvedSchema& schema,
    CLiveDocument& output, TDiagnostic<Bulk>& diagnostic)
{
    if constexpr (Bulk)
    {
        return merge_bulk_data(first, second, schema, output, diagnostic);
    }
    else
    {
        return merge_instances(first, second, schema, output, diagnostic);
    }
}

template<bool Bulk>
static void expect_merge(TTestContext& ctx, const CResolvedSchema& schema, const std::string& first,
    const std::string& second, const std::string& expected,
    const EDataMergeStage stage = EDataMergeStage::none, const EDataMergeReason reason = EDataMergeReason::none)
{
    for (unsigned form = 0u; form < 4u; ++form)
    {
        CLiveDocument a, b, output, result;
        TEST_EXPECT(ctx, tests::parse_document(first, a) && tests::parse_document(second, b) && tests::parse_document(R"({"sentinel":17})", output));
        CBakedDocumentBlock ab, bb, before;
        TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb) && document_translation::bake(output, before));
        const TQuery<Bulk> aq = (form & 1u) ? TQuery<Bulk>{ ab.document() } : TQuery<Bulk>{ a };
        const TQuery<Bulk> bq = (form & 2u) ? TQuery<Bulk>{ bb.document() } : TQuery<Bulk>{ b };
        TDiagnostic<Bulk> diagnostic;
        const bool merged = merge<Bulk>(aq, bq, schema, output, diagnostic);
        if ((merged != !expected.empty()) || (diagnostic.stage != stage) || (diagnostic.reason != reason))
        {
            std::cerr << "Data merge bulk=" << Bulk << " stage=" << static_cast<unsigned>(diagnostic.stage)
                << " reason=" << static_cast<unsigned>(diagnostic.reason) << " first=" << first << " second=" << second << '\n';
        }
        TEST_EXPECT(ctx, merged == !expected.empty());
        TEST_EXPECT(ctx, diagnostic.stage == stage && diagnostic.reason == reason);
        TEST_EXPECT(ctx, tests::same_baked_document(a, ab) && tests::same_baked_document(b, bb));
        if (merged)
        {
            TEST_EXPECT(ctx, tests::parse_document(expected, result));
            const CDocumentRead actual{ output }, wanted{ result };
            TEST_EXPECT(ctx, tests::equal_document_values(actual, actual.root(), wanted, wanted.root()));
            TEST_EXPECT(ctx, output.check_integrity() && output.is_complete());
            TEST_EXPECT(ctx, !diagnostic.first && !diagnostic.second && !diagnostic.validation.occurrence);
        }
        else
        {
            TEST_EXPECT(ctx, tests::same_baked_document(output, before));
            if (stage == EDataMergeStage::first_source_validation)
            {
                TEST_EXPECT(ctx, aq.contains(diagnostic.first) && !diagnostic.second);
            }
            if (stage == EDataMergeStage::second_source_validation)
            {
                TEST_EXPECT(ctx, bq.contains(diagnostic.second) && !diagnostic.first);
            }
            if (reason == EDataMergeReason::conflicting_value)
            {
                TEST_EXPECT(ctx, aq.contains(diagnostic.first) && bq.contains(diagnostic.second));
            }
            if (stage == EDataMergeStage::result_validation)
            {
                TEST_EXPECT(ctx, !diagnostic.first && !diagnostic.second && !diagnostic.validation.occurrence);
            }
        }
    }
}

static void test_rules(TTestContext& ctx, const CResolvedSchema& schema)
{
    expect_merge<false>(ctx, schema, "{}", "{}", R"({"instances":{}})");
    expect_merge<true>(ctx, schema, "{}", "{}", R"({"data":{}})");
    const std::string a = R"({"instances":{"Pair":{"base":{"declaration":{"a":1},"specialisation":{"child":{"declaration":{"b":3}}}}}}})";
    const std::string b = R"({"instances":{"Pair":{"base":{"declaration":{"b":2},"specialisation":{"child":{"declaration":{"a":4}},"new":{}}},"other":{}},"One":{}}})";
    const std::string combined = R"({"instances":{"Pair":{"base":{"declaration":{"a":1,"b":2},"specialisation":{"child":{"declaration":{"b":3,"a":4}},"new":{}}},"other":{}},"One":{}}})";
    expect_merge<false>(ctx, schema, a, b, combined);
    expect_merge<false>(ctx, schema, a, "{}", a);
    expect_merge<false>(ctx, schema, "{}", a, a);
    expect_merge<false>(ctx, schema, R"({"instances":{"Empty":{"e":{"declaration":{}}}}})", "{}",
        R"({"instances":{"Empty":{"e":{"declaration":{}}}}})");
    const std::string short_bulk = R"({"data":{"One":{"x":{"data":[{"v":1}]}}}})";
    const std::string long_bulk = R"({"data":{"One":{"x":{"data":[{"v":0x0001},{"v":2}]}}}})";
    expect_merge<true>(ctx, schema, short_bulk, long_bulk, long_bulk);
    expect_merge<true>(ctx, schema, long_bulk, short_bulk, long_bulk);
    expect_merge<true>(ctx, schema, "{}", long_bulk, long_bulk);
    expect_merge<true>(ctx, schema, long_bulk, "{}", long_bulk);
    expect_merge<true>(ctx, schema, R"({"data":{"One":{"x":{"data":[{"v":1}]}}}})", "{}",
        R"({"data":{"One":{"x":{"data":[{"v":1}]}}}})");
    const std::string short_array = R"({"instances":{"Array":{"x":{"declaration":{"data":[1]}}}}})";
    const std::string long_array = R"({"instances":{"Array":{"x":{"declaration":{"data":[1,2]}}}}})";
    expect_merge<false>(ctx, schema, short_array, long_array, {}, EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);
    expect_merge<false>(ctx, schema, R"({"instances":{"One":{"x":{"declaration":{"v":1}}}}})",
        R"({"instances":{"One":{"x":{"declaration":[1]}}}})", {}, EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);
    expect_merge<true>(ctx, schema, short_bulk, R"({"data":{"One":{"x":{"data":[{"v":2}]}}}})", {},
        EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);
    expect_merge<true>(ctx, schema, R"({"data":{"Array":{"x":{"data":[{"data":[1,2]}]}}}})",
        R"({"data":{"Array":{"x":{"data":[{"data":[1,3]}]}}}})", {}, EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);

    const char* invalid_instances[] = {
        R"({"instances":[]})", R"({"types":{}})", R"({"stripped":false})", R"({"instances":{"Missing":{}}})",
        R"({"instances":{"One":[]}})", R"({"instances":{"One":{"x":1}}})",
        R"({"instances":{"One":{"x":{"locator":{}}}}})", R"({"instances":{"One":{"x":{"declaration":null}}}})",
        R"({"instances":{"One":{"x":{"specialisation":[]}}}})", R"({"instances":{"One":{"x":{"declaration":{"v":-1}}}}})"
    };
    for (const auto text : invalid_instances)
    {
        expect_merge<false>(ctx, schema, text, "{}", {}, EDataMergeStage::first_source_validation, EDataMergeReason::invalid_source);
        expect_merge<false>(ctx, schema, "{}", text, {}, EDataMergeStage::second_source_validation, EDataMergeReason::invalid_source);
    }
    const char* invalid_bulk[] = {
        R"({"data":[]})", R"({"instances":{}})", R"({"stripped":true})", R"({"data":{"Missing":{}}})",
        R"({"data":{"One":{"x":{}}}})", R"({"data":{"One":{"x":{"data":{}}}}})",
        R"({"data":{"One":{"x":{"data":[]}}}})", R"({"data":{"One":{"x":{"data":[null]}}}})",
        R"({"data":{"Pair":{"x":{"data":[{"a":1}]}}}})", R"({"data":{"u32":{"x":{"data":[{"v":1}]}}}})",
        R"({"data":{"One":{"x":{"locator":{},"data":[1]}}}})", R"({"data":{"Array":{"x":{"data":[{"data":[1]}]}}}})"
    };
    for (const auto text : invalid_bulk)
    {
        expect_merge<true>(ctx, schema, text, "{}", {}, EDataMergeStage::first_source_validation, EDataMergeReason::invalid_source);
        expect_merge<true>(ctx, schema, "{}", text, {}, EDataMergeStage::second_source_validation, EDataMergeReason::invalid_source);
    }
    //  Two incomplete records must fail independently, even if union could fill them.
    expect_merge<true>(ctx, schema, R"({"data":{"Pair":{"x":{"data":[{"a":1}]}}}})",
        R"({"data":{"Pair":{"x":{"data":[{"b":2}]}}}})", {}, EDataMergeStage::first_source_validation, EDataMergeReason::invalid_source);
}

static void test_value_parity(TTestContext& ctx, const CResolvedSchema& schema)
{
    struct SCase { const char* type; const char* value; bool instance, bulk; };
    const SCase cases[] = {
        { "Pair", "{}", true, false }, { "Pair", R"({"b":2,"a":1})", true, true },
        { "Pair", "[1]", true, false }, { "Pair", "[1,2]", true, true }, { "Pair", "[1,2,3]", false, false },
        { "Pair", R"({"unknown":1})", false, false }, { "One", "3", true, true },
        { "One", "[null]", false, false }, { "One", R"([{"v":1}])", false, false },
        { "One", "-1", false, false }, { "One", "4294967296", false, false },
        { "Nested", "1", false, false }, { "Nested", R"([{"v":1}])", true, true },
        { "Nested", R"({"v":{"v":1}})", true, true },
        { "Normal", "0.5", true, true }, { "Normal", "1.5", true, true }, { "Normal", "15", true, true },
        { "Normal", "16", false, false }, { "Bits", "3", true, true }, { "Bits", "4", false, false }, { "Bits", "[]", true, false },
        { "Bits", R"([{"v":1}])", false, false }, { "Bits", R"({"v":2})", true, true },
        { "Array", R"({"data":[1]})", true, false }, { "Array", R"({"data":[1,2]})", true, true },
        { "Array", R"({"data":[{"v":1}]})", false, false }, { "Empty", "{}", true, true },
        { "u64", "18446744073709551615", true, true }, { "i64", "-9223372036854775808", true, true },
        { "E", R"("red")", true, true }, { "E", R"("missing")", false, false }, { "E", "0", false, false },
        { "f32", R"("nan")", true, true }, { "f32", "1.5", true, true }, { "b8", "true", true, true }
    };
    for (const auto& test : cases)
    {
        CLiveDocument document;
        TEST_EXPECT(ctx, tests::parse_document(std::string{ "{\"value\":" } + test.value + "}", document));
        CBakedDocumentBlock baked;
        TEST_EXPECT(ctx, document_translation::bake(document, baked));
        for (const auto& read : { CDocumentRead{ document }, CDocumentRead{ baked.document() } })
        {
            for (const bool bulk : { false, true })
            {
                const CSchemaIndex type = schema.find_type(CStringView{ test.type });
                const SOccurrence value = read.first_child(read.root());
                const auto mode = bulk ? detail::EConstructionMode::complete_bulk_data : detail::EConstructionMode::instance;
                detail::SValueDiagnostic validation, construction, comparison;
                SType layout;
                TEST_EXPECT(ctx, schema.type(type, layout));
                alignas(64) std::uint8_t bytes[256]{};
                const bool valid = detail::validate_declaration(schema, read, type, value, mode, validation);
                const bool built = detail::construct_value(schema, read, type, value, bytes, sizeof(bytes), mode, construction);
                bool matches{};
                const bool checked = detail::check_value(schema, read, type, value, bytes, sizeof(bytes), mode, matches, comparison);
                TEST_EXPECT(ctx, valid == (bulk ? test.bulk : test.instance));
                TEST_EXPECT(ctx, valid == built && valid == checked);
                TEST_EXPECT(ctx, construction.reason == comparison.reason && construction.occurrence == comparison.occurrence);
                if (valid) TEST_EXPECT(ctx, matches);
            }
        }
    }
}

template<bool Bulk>
static void test_aliases(TTestContext& ctx, CLiveDocument& schema_document, const CResolvedSchema& schema)
{
    const char* text = Bulk ? R"({"data":{"One":{"x":{"data":[1,2]}}}})" : R"({"instances":{"One":{"x":{"declaration":1}}}})";
    for (unsigned alias = 0u; alias < 4u; ++alias)
    {
        CLiveDocument a, b, separate;
        TEST_EXPECT(ctx, tests::parse_document(text, a) && tests::parse_document(text, b));
        CLiveDocument& output = (alias == 0u) ? separate : ((alias == 2u) ? b : a);
        TDiagnostic<Bulk> diagnostic;
        TEST_EXPECT(ctx, merge<Bulk>(TQuery<Bulk>{ a }, TQuery<Bulk>{ (alias == 3u) ? a : b }, schema, output, diagnostic));
        TEST_EXPECT(ctx, output.check_integrity());
    }
    CLiveDocument input, output;
    TEST_EXPECT(ctx, tests::parse_document(text, input));
    TDiagnostic<Bulk> diagnostic;
    TEST_EXPECT(ctx, !merge<Bulk>({}, TQuery<Bulk>{ input }, schema, output, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == EDataMergeStage::first_source_validation && !diagnostic.first);
    TEST_EXPECT(ctx, !merge<Bulk>(TQuery<Bulk>{ input }, {}, schema, output, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == EDataMergeStage::second_source_validation && !diagnostic.second);
    CResolvedSchema unready;
    TEST_EXPECT(ctx, !merge<Bulk>(TQuery<Bulk>{ input }, TQuery<Bulk>{ input }, unready, output, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == EDataMergeStage::preconditions && diagnostic.reason == EDataMergeReason::invalid_schema);
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(schema_document, before));
    TEST_EXPECT(ctx, !merge<Bulk>(TQuery<Bulk>{ input }, TQuery<Bulk>{ input }, schema, schema_document, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == EDataMergeStage::preconditions && diagnostic.reason == EDataMergeReason::invalid_schema);
    TEST_EXPECT(ctx, tests::same_baked_document(schema_document, before) && schema.find_type(CStringView{ "Pair" }));
    TEST_EXPECT(ctx, merge<Bulk>(TQuery<Bulk>{ input }, TQuery<Bulk>{ input }, schema, output, diagnostic));
    TEST_EXPECT(ctx, diagnostic.reason == EDataMergeReason::none && !diagnostic.first && !diagnostic.second);
}

template<bool Bulk>
static void test_failures(TTestContext& ctx, const CResolvedSchema& schema)
{
    const char* text = Bulk ? R"({"data":{"Pair":{"x":{"data":[{"b":2,"a":1},{"a":3,"b":4}]}}}})" :
        R"({"instances":{"Pair":{"x":{"declaration":{"b":2,"a":1},"specialisation":{"s":{"declaration":{"a":4}}}}}}})";
    bool saw_final = false;
    for (unsigned alias = 0u; alias < 4u; ++alias)
    {
        bool completed = false;
        for (std::size_t fail = 0u; (fail < 1024u) && !completed; ++fail)
        {
            CLiveDocument a, b, separate;
            TEST_EXPECT(ctx, tests::parse_document(text, a) && tests::parse_document(text, b) && tests::parse_document(R"({"sentinel":17})", separate));
            CLiveDocument& output = (alias == 0u) ? separate : ((alias == 2u) ? b : a);
            CBakedDocumentBlock ab, bb, before;
            TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb) && document_translation::bake(output, before));
            const CNodeKey retained = output.first_child(output.root());
            tests::TAllocationFailureFixture fixture{ 0u, fail };
            memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            TDiagnostic<Bulk> diagnostic;
            {
                tests::TMemoryContextScope scope{ &context };
                completed = merge<Bulk>(TQuery<Bulk>{ a }, TQuery<Bulk>{ (alias == 3u) ? a : b }, schema, output, diagnostic);
            }
            if (!completed)
            {
                TEST_EXPECT(ctx, diagnostic.reason != EDataMergeReason::none);
                TEST_EXPECT(ctx, tests::same_baked_document(a, ab) && tests::same_baked_document(b, bb) && tests::same_baked_document(output, before));
                TEST_EXPECT(ctx, output.contains(retained));
                if (diagnostic.stage == EDataMergeStage::result_validation)
                {
                    saw_final = true;
                    TEST_EXPECT(ctx, !diagnostic.first && !diagnostic.second && !diagnostic.validation.occurrence);
                }
            }
            output.deallocate();
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed);
    }
    if constexpr (!Bulk) TEST_EXPECT(ctx, saw_final);
}


static CNodeKey first_value(CLiveDocument& document, const bool bulk)
{
    CNodeKey node = document.first_child(document.root());
    for (unsigned i = 0u; i < 3u; ++i) node = document.first_child(node);
    return bulk ? document.first_child(node) : node;
}

static void test_presentation(TTestContext& ctx, const CResolvedSchema& schema)
{
    //  Equal scalar spellings retain the first storage domain while presentation
    //  comes from the strongest input, including through bulk common prefixes.
    expect_merge<false>(ctx, schema, R"({"instances":{"u64":{"x":{"declaration":18446744073709551615}}}})",
        R"({"instances":{"u64":{"x":{"declaration":0xffffffffffffffff}}}})",
        R"({"instances":{"u64":{"x":{"declaration":18446744073709551615}}}})");
    expect_merge<false>(ctx, schema, R"({"instances":{"f64":{"x":{"declaration":0.0}}}})",
        R"({"instances":{"f64":{"x":{"declaration":-0.0}}}})", {}, EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);
    expect_merge<false>(ctx, schema, R"({"instances":{"f64":{"x":{"declaration":1}}}})",
        R"({"instances":{"f64":{"x":{"declaration":1.0}}}})", {}, EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);
    expect_merge<true>(ctx, schema, R"({"data":{"f64":{"x":{"data":["nan"]}}}})",
        R"({"data":{"f64":{"x":{"data":["NaN"]}}}})", {}, EDataMergeStage::document_merge, EDataMergeReason::conflicting_value);
    for (const bool bulk : { false, true })
    {
        for (unsigned form = 0u; form < 4u; ++form)
        {
            for (unsigned flags = 0u; flags < 4u; ++flags)
            {
                for (unsigned copy = 0u; copy < 3u; ++copy)
                {
                    CLiveDocument a, b, output;
                    const char* text = bulk ? R"({"data":{"E":{"x":{"data":["red"]}}}})" :
                        R"({"instances":{"E":{"x":{"declaration":"red"}}}})";
                    TEST_EXPECT(ctx, tests::parse_document((copy == 1u) ? "{}" : text, a) && tests::parse_document((copy == 2u) ? "{}" : text, b));
                    if (copy != 1u) TEST_EXPECT(ctx, a.set_newline_escaping_suppressed(first_value(a, bulk), (flags & 1u) != 0u));
                    if (copy != 2u) TEST_EXPECT(ctx, b.set_newline_escaping_suppressed(first_value(b, bulk), (flags & 2u) != 0u));
                    CBakedDocumentBlock ab, bb;
                    TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb));
                    if (bulk)
                    {
                        const CBulkDataDocumentQuery aq = (form & 1u) ? CBulkDataDocumentQuery{ ab.document() } : CBulkDataDocumentQuery{ a };
                        const CBulkDataDocumentQuery bq = (form & 2u) ? CBulkDataDocumentQuery{ bb.document() } : CBulkDataDocumentQuery{ b };
                        SBulkDataMergeDiagnostic diagnostic;
                        TEST_EXPECT(ctx, merge_bulk_data(aq, bq, schema, output, diagnostic));
                    }
                    else
                    {
                        const CInstanceDocumentQuery aq = (form & 1u) ? CInstanceDocumentQuery{ ab.document() } : CInstanceDocumentQuery{ a };
                        const CInstanceDocumentQuery bq = (form & 2u) ? CInstanceDocumentQuery{ bb.document() } : CInstanceDocumentQuery{ b };
                        SInstanceMergeDiagnostic diagnostic;
                        TEST_EXPECT(ctx, merge_instances(aq, bq, schema, output, diagnostic));
                    }
                    const bool expected = (copy == 1u) ? ((flags & 2u) != 0u) : ((copy == 2u) ? ((flags & 1u) != 0u) : (flags == 3u));
                    TEST_EXPECT(ctx, output.suppresses_newline_escaping(first_value(output, bulk)) == expected);
                }
            }
            CLiveDocument a, b, output;
            TEST_EXPECT(ctx, tests::parse_document(bulk ? R"({"data":{"u32":{"x":{"data":[1]}}}})" :
                R"({"instances":{"u32":{"x":{"declaration":1}}}})", a));
            TEST_EXPECT(ctx, tests::parse_document(bulk ? R"({"data":{"u32":{"x":{"data":[0x00000001,2]}}}})" :
                R"({"instances":{"u32":{"x":{"declaration":0x00000001}}}})", b));
            CBakedDocumentBlock ab, bb;
            TEST_EXPECT(ctx, document_translation::bake(a, ab) && document_translation::bake(b, bb));
            if (bulk)
            {
                SBulkDataMergeDiagnostic diagnostic;
                TEST_EXPECT(ctx, merge_bulk_data((form & 1u) ? CBulkDataDocumentQuery{ ab.document() } : CBulkDataDocumentQuery{ a },
                    (form & 2u) ? CBulkDataDocumentQuery{ bb.document() } : CBulkDataDocumentQuery{ b }, schema, output, diagnostic));
            }
            else
            {
                SInstanceMergeDiagnostic diagnostic;
                TEST_EXPECT(ctx, merge_instances((form & 1u) ? CInstanceDocumentQuery{ ab.document() } : CInstanceDocumentQuery{ a },
                    (form & 2u) ? CInstanceDocumentQuery{ bb.document() } : CInstanceDocumentQuery{ b }, schema, output, diagnostic));
            }
            CIntegerMetadata metadata;
            TEST_EXPECT(ctx, output.integer_metadata(first_value(output, bulk), metadata) && metadata.notation == EIntegerNotation::hexadecimal_8);
        }
    }
}

static void test_limits(TTestContext& ctx)
{
    CLiveDocument schema_document;
    CResolvedSchema schema;
    SDiagnostic error;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"Big":{"detail":{"size":1073741825,"alignment":1},"members":[{"v":{"type":"u8","offset":0}}]}}}})", schema_document));
    TEST_EXPECT(ctx, schema.resolve(schema_document, error));
    expect_merge<false>(ctx, schema, R"({"instances":{"Big":{"a":{}}}})", R"({"instances":{"Big":{"b":{}}}})", {},
        EDataMergeStage::result_validation, EDataMergeReason::storage_limit);
    expect_merge<true>(ctx, schema, R"({"data":{"Big":{"a":{"data":[0]}}}})", R"({"data":{"Big":{"b":{"data":[0]}}}})", {},
        EDataMergeStage::result_validation, EDataMergeReason::storage_limit);
    expect_merge<true>(ctx, schema, R"({"data":{"Big":{"a":{"data":[0,0]}}}})", "{}", {},
        EDataMergeStage::first_source_validation, EDataMergeReason::storage_limit);

    //  Dependency-first resolution caches shallow resolutions. The implicit
    //  value path can therefore exceed construction depth without invalid schema.
    std::string text = R"({"types":{"structures":{"T0":{"members":[]})";
    for (unsigned i = 1u; i <= 260u; ++i)
    {
        text += ",\"T" + std::to_string(i) + "\":{\"members\":[{\"v\":{\"type\":\"T" + std::to_string(i - 1u) + "\"}}]}";
    }
    text += R"(,"Skipped":{"members":[{"v":{"type":{"element":"T260","count":4000000000}}}]}}}})";
    TEST_EXPECT(ctx, tests::parse_document(text, schema_document) && schema.resolve(schema_document, error));
    CLiveDocument a, empty, merged;
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"T260":{"x":{}},"Skipped":{"x":{}}}})", a) && tests::parse_document("{}", empty));
    SInstanceMergeDiagnostic diagnostic;
    TEST_EXPECT(ctx, merge_instances(CInstanceDocumentQuery{ a }, CInstanceDocumentQuery{ empty }, schema, merged, diagnostic));
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(merged, before));
    detail::SValueDiagnostic value_error;
    TEST_EXPECT(ctx, !detail::construct_value(schema, CDocumentRead{ merged }, schema.find_type(CStringView{ "T260" }), {},
        nullptr, 0u, detail::EConstructionMode::instance, value_error));
    TEST_EXPECT(ctx, value_error.reason == EReason::storage_limit && tests::same_baked_document(merged, before));
    TEST_EXPECT(ctx, detail::construct_value(schema, CDocumentRead{ merged }, schema.find_type(CStringView{ "Skipped" }), {},
        nullptr, 0u, detail::EConstructionMode::instance, value_error));

    //  Explicit declarations remain bounded. Construct directly so parser depth
    //  is not the limiting factor, and test both shared and copied branches.
    for (const unsigned depth : { 251u, 252u, 253u, 256u })
    {
        CLiveDocument declarations;
        TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"T260":{"x":{"declaration":{}}}}})", declarations));
        const CNodeKey declaration = first_value(declarations, false);
        CNodeKey root = declaration;
        for (unsigned i = 0u; i < depth; ++i)
        {
            const CNodeKey child = declarations.create_object(CStringView{ "v" });
            TEST_EXPECT(ctx, child && declarations.append_child(root, child).succeeded());
            root = child;
        }
        const auto type = schema.find_type(CStringView{ "T260" });
        const CDocumentRead read{ declarations };
        const bool valid = detail::validate_declaration(schema, read, type, SOccurrence{ declaration },
            detail::EConstructionMode::instance, value_error);
        TEST_EXPECT(ctx, valid == (depth < 256u));
        if (!valid) TEST_EXPECT(ctx, value_error.reason == EReason::storage_limit);
        for (const bool shared : { false, true })
        {
            CLiveDocument result;
            SInstanceMergeDiagnostic merge_error;
            const bool accepted = merge_instances(CInstanceDocumentQuery{ declarations },
                CInstanceDocumentQuery{ shared ? declarations : empty }, schema, result, merge_error);
            TEST_EXPECT(ctx, accepted == (depth <= 252u));
            if (!accepted)
            {
                TEST_EXPECT(ctx, merge_error.reason == EDataMergeReason::storage_limit && !result.is_ready());
                TEST_EXPECT(ctx, merge_error.stage == ((depth == 256u) ? EDataMergeStage::first_source_validation : EDataMergeStage::document_merge));
            }
        }
    }
}

static void test_specialisation_depth(TTestContext& ctx, const CResolvedSchema& schema)
{
    for (const unsigned depth : { 0u, 1u, 126u, 127u, 256u })
    {
        CLiveDocument source, empty;
        TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"One":{"base":{}}}})", source) && tests::parse_document("{}", empty));
        CNodeKey entry = source.first_child(source.first_child(source.first_child(source.root())));
        for (unsigned i = 0u; i < depth; ++i)
        {
            const CNodeKey children = source.create_object(CStringView{ "specialisation" });
            const CNodeKey child = source.create_object(CStringView{ "child" });
            TEST_EXPECT(ctx, source.append_child(entry, children).succeeded());
            TEST_EXPECT(ctx, source.append_child(children, child).succeeded());
            //  Pending siblings exercise ascent and reuse of the hierarchy stack.
            TEST_EXPECT(ctx, source.append_child(children, source.create_object(CStringView{ "after" })).succeeded());
            entry = child;
        }
        TEST_EXPECT(ctx, source.append_child(entry, source.create_unsigned_integer(1u, CStringView{ "declaration" })).succeeded());
        TEST_EXPECT(ctx, source.maximum_depth() == ((2u * depth) + 4u));
        CBakedDocumentBlock baked, baked_empty;
        TEST_EXPECT(ctx, document_translation::bake(source, baked) && document_translation::bake(empty, baked_empty));
        for (unsigned form = 0u; form < 4u; ++form)
        {
            for (unsigned copy = 0u; copy < 3u; ++copy)
            {
                const CInstanceDocumentQuery first = (form & 1u) ?
                    CInstanceDocumentQuery{ (copy == 1u) ? baked_empty.document() : baked.document() } :
                    CInstanceDocumentQuery{ (copy == 1u) ? empty : source };
                const CInstanceDocumentQuery second = (form & 2u) ?
                    CInstanceDocumentQuery{ (copy == 2u) ? baked_empty.document() : baked.document() } :
                    CInstanceDocumentQuery{ (copy == 2u) ? empty : source };
                CLiveDocument output;
                CBakedDocumentBlock before;
                TEST_EXPECT(ctx, tests::parse_document(R"({"sentinel":17})", output) && document_translation::bake(output, before));
                SInstanceMergeDiagnostic diagnostic;
                const bool accepted = merge_instances(first, second, schema, output, diagnostic);
                TEST_EXPECT(ctx, accepted == (depth <= 126u));
                if (accepted)
                {
                    const CDocumentRead actual{ output }, expected{ source };
                    TEST_EXPECT(ctx, tests::equal_document_values(actual, actual.root(), expected, expected.root()));
                    TEST_EXPECT(ctx, output.maximum_depth() == source.maximum_depth());
                }
                else
                {
                    TEST_EXPECT(ctx, diagnostic.reason == EDataMergeReason::storage_limit && tests::same_baked_document(output, before));
                    const EDataMergeStage stage = (depth == 256u) ?
                        ((copy == 1u) ? EDataMergeStage::second_source_validation : EDataMergeStage::first_source_validation) :
                        EDataMergeStage::document_merge;
                    TEST_EXPECT(ctx, diagnostic.stage == stage);
                    TEST_EXPECT(ctx, (copy == 1u) ? (second.contains(diagnostic.second) && !diagnostic.first) :
                        (first.contains(diagnostic.first) && !diagnostic.second));
                }
            }
        }
    }
}

static void test_integration(TTestContext& ctx)
{
    CLiveDocument schema_document, first, second, merged;
    TEST_EXPECT(ctx, tests::parse_document(k_schema, schema_document));
    CLiveDocument without_defaults, merged_schema;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"Pair":{"members":[{"a":{"type":"u32"}},{"b":{"type":"u32"}}]}}}})", without_defaults));
    SSchemaMergeDiagnostic schema_merge_error;
    TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ without_defaults }, CSchemaDocumentQuery{ schema_document }, merged_schema, schema_merge_error));
    CLiveSchema owner;
    SDiagnostic error;
    TEST_EXPECT(ctx, owner.try_adopt(std::move(merged_schema)) && owner.resolve(error));
    //  Borrow via the established binding, without giving the result a binding.
    CSchemaBinding binding;
    TEST_EXPECT(ctx, binding.bind(owner));
    const CResolvedSchema& schema = *binding.resolved();
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"Pair":{"base":{"specialisation":{"child":{}}},"explicit":{"declaration":{"a":7}}}}})", first));
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"Pair":{"base":{"declaration":{"a":3}}}}})", second));
    SInstanceMergeDiagnostic diagnostic;
    TEST_EXPECT(ctx, merge_instances(CInstanceDocumentQuery{ first }, CInstanceDocumentQuery{ second }, schema, merged, diagnostic));
    first.deallocate();
    second.deallocate();
    CInstanceDocumentQuery query{ merged };
    const auto group = query.object_child(query.first_child(query.root()), CStringView{ "Pair" });
    const auto base_node = query.object_child(group, CStringView{ "base" });
    const auto explicit_node = query.object_child(group, CStringView{ "explicit" });
    const auto child_node = query.first_child(query.object_child(base_node, CStringView{ "specialisation" }));
    CLiveInstances instances;
    SInstanceDiagnostic instance_error;
    TEST_EXPECT(ctx, instances.initialise(owner));
    const auto base = instances.create_base(CStringView{ "Pair" }, CStringView{ "base" }, query,
        query.object_child(base_node, CStringView{ "declaration" }), instance_error);
    const auto child = instances.create_specialisation(base, CStringView{ "child" }, query,
        query.object_child(child_node, CStringView{ "declaration" }), instance_error);
    const auto explicit_value = instances.create_base(CStringView{ "Pair" }, CStringView{ "explicit" }, query,
        query.object_child(explicit_node, CStringView{ "declaration" }), instance_error);
    SInstanceEntryView view;
    for (const auto entry : { base, child, explicit_value })
    {
        std::uint32_t values[2]{};
        TEST_EXPECT(ctx, instances.entry(entry, view) && view.byte_count == sizeof(values));
        if (view.byte_count == sizeof(values)) std::memcpy(values, view.bytes, sizeof(values));
        TEST_EXPECT(ctx, values[0] == ((entry == explicit_value) ? 7u : 3u) && values[1] == 9u);
    }
    TEST_EXPECT(ctx, !query.object_child(child_node, CStringView{ "declaration" }));
    TEST_EXPECT(ctx, query.object_child(explicit_node, CStringView{ "declaration" }));

    TEST_EXPECT(ctx, tests::parse_document(R"({"data":{"Pair":{"records":{"data":[[1,2]]}}}})", first));
    TEST_EXPECT(ctx, tests::parse_document(R"({"data":{"Pair":{"records":{"data":[[1,2],[3,4]]}}}})", second));
    SBulkDataMergeDiagnostic bulk_error;
    TEST_EXPECT(ctx, merge_bulk_data(CBulkDataDocumentQuery{ first }, CBulkDataDocumentQuery{ second }, schema, merged, bulk_error));
    CBulkDataDocumentQuery bulk_query{ merged };
    const auto records = bulk_query.object_child(bulk_query.first_child(bulk_query.first_child(bulk_query.first_child(bulk_query.root()))), CStringView{ "data" });
    CLiveBulkData bulk;
    SBulkDataDiagnostic construction;
    TEST_EXPECT(ctx, bulk.initialise(owner));
    const auto entry = bulk.create_records(CStringView{ "Pair" }, CStringView{ "records" }, bulk_query, records, construction);
    SBulkDataEntryView bulk_view;
    TEST_EXPECT(ctx, bulk.entry(entry, bulk_view) && bulk_view.count == 2u && bulk_view.byte_count == 16u);
    std::uint32_t values[4]{};
    if (bulk_view.byte_count == sizeof(values)) std::memcpy(values, bulk_view.bytes, sizeof(values));
    TEST_EXPECT(ctx, values[0] == 1u && values[1] == 2u && values[2] == 3u && values[3] == 4u);
    bulk.clear();
    instances.clear();
    binding.release();
    TEST_EXPECT(ctx, owner.reset());
    first.deallocate();
    second.deallocate();
    TEST_EXPECT(ctx, merged.check_integrity() && merged.is_complete());
}

static void test_growth(TTestContext& ctx, const CResolvedSchema& schema)
{
    for (const bool bulk : { false, true })
    {
        for (const unsigned count : { 7u, 8u, 9u, 128u, 512u, 2048u })
        {
            const auto source = [&](const bool reverse)
            {
                std::string text = bulk ? R"({"data":{"E":{"x":{"data":[)" : R"({"instances":{"E":{)";
                for (unsigned i = 0u; i < count; ++i)
                {
                    if (i) text += ',';
                    if (bulk)
                    {
                        text += (i & 1u) ? "\"blue\"" : "\"red\"";
                    }
                    else
                    {
                        text += "\"n" + std::to_string(reverse ? (count - i - 1u) : i) + R"(":{"declaration":"red"})";
                    }
                }
                return text + (bulk ? "]}}}}" : "}}}");
            };
            CLiveDocument a, b, output;
            TEST_EXPECT(ctx, tests::parse_document(source(false), a) && tests::parse_document(source(true), b));
            tests::TAllocationFailureFixture fixture;
            memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            platform::system::CPerfCounter timer;
            (void)timer.update();
            {
                tests::TMemoryContextScope scope{ &context };
                if (bulk)
                {
                    SBulkDataMergeDiagnostic diagnostic;
                    TEST_EXPECT(ctx, merge_bulk_data(CBulkDataDocumentQuery{ a }, CBulkDataDocumentQuery{ b }, schema, output, diagnostic));
                }
                else
                {
                    SInstanceMergeDiagnostic diagnostic;
                    TEST_EXPECT(ctx, merge_instances(CInstanceDocumentQuery{ a }, CInstanceDocumentQuery{ b }, schema, output, diagnostic));
                }
            }
            const std::uint64_t elapsed = timer.query_delta();
            platform::system::CPerfCountConversion conversion;
            std::uint64_t micros{};
            (void)conversion.init();
            (void)conversion.to_microseconds_u64(elapsed, micros);
            const auto group = output.first_child(output.first_child(output.root()));
            if (bulk)
            {
                TEST_EXPECT(ctx, output.child_count(output.first_child(output.first_child(group))) == count);
            }
            else
            {
                TEST_EXPECT(ctx, output.child_count(group) == count);
                auto child = output.first_child(group);
                for (unsigned i = 0u; i < count; ++i)
                {
                    const std::string name = "n" + std::to_string(i);
                    TEST_EXPECT(ctx, output.name(child) == CStringView{ name.c_str() });
                    child = output.next_sibling(child);
                }
            }
            TEST_EXPECT(ctx, output.check_integrity());
            if (count >= 128u) std::cout << "Data merge scaling: " << (bulk ? "bulk " : "instances ") << count
                << ", " << micros << " us, " << fixture.attempts << " allocations, " << fixture.bytes << " requested bytes\n";
            output.deallocate();
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
    }
    //  Holding declarations constant must keep validation and merge allocations
    //  independent of omitted physical array extent.
    std::size_t baseline_attempts{}, baseline_bytes{};
    for (const unsigned count : { 1u, 100000000u })
    {
        CLiveDocument schema_document, input, output;
        CResolvedSchema large;
        SDiagnostic error;
        TEST_EXPECT(ctx, tests::parse_document("{\"types\":{\"structures\":{\"Large\":{\"members\":[{\"v\":{\"type\":{\"element\":\"u8\",\"count\":" +
            std::to_string(count) + "}}}]}}}}", schema_document) && large.resolve(schema_document, error));
        TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"Large":{"x":{"declaration":{"v":[]}}}}})", input));
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        SInstanceMergeDiagnostic diagnostic;
        {
            tests::TMemoryContextScope scope{ &context };
            TEST_EXPECT(ctx, merge_instances(CInstanceDocumentQuery{ input }, CInstanceDocumentQuery{ input }, large, output, diagnostic));
        }
        if (count == 1u)
        {
            baseline_attempts = fixture.attempts;
            baseline_bytes = fixture.bytes;
        }
        else
        {
            TEST_EXPECT(ctx, fixture.attempts == baseline_attempts && fixture.bytes == baseline_bytes);
        }
        output.deallocate();
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

static void test_sparse_wide_structures(TTestContext& ctx)
{
    std::string text = R"({"instances":{"Wide":{)";
    for (unsigned i = 0u; i < 256u; ++i)
    {
        if (i) text += ',';
        text += "\"n" + std::to_string(i) + R"(":{"declaration":{"selected":7}})";
    }
    text += "}}}";
    CLiveDocument input;
    TEST_EXPECT(ctx, tests::parse_document(text, input));
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(input, baked));
    std::size_t baseline_attempts{}, baseline_bytes{};
    for (const unsigned width : { 1u, 128u, 2048u })
    {
        std::string types = R"({"types":{"structures":{"Wide":{"members":[)";
        for (unsigned i = 1u; i < width; ++i)
        {
            types += "{\"m" + std::to_string(i) + "\":{\"type\":\"u32\"}},";
        }
        //  The only supplied member is last, making a scan of all schema members
        //  particularly costly. Declarations and result remain fixed as width grows.
        types += R"({"selected":{"type":"u32"}}]}}}})";
        CLiveDocument schema_document;
        CResolvedSchema schema;
        SDiagnostic error;
        TEST_EXPECT(ctx, tests::parse_document(types, schema_document) && schema.resolve(schema_document, error));
        for (const bool baked_input : { false, true })
        {
            const CInstanceDocumentQuery query = baked_input ? CInstanceDocumentQuery{ baked.document() } : CInstanceDocumentQuery{ input };
            CLiveDocument output;
            tests::TAllocationFailureFixture fixture;
            memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            SInstanceMergeDiagnostic diagnostic;
            platform::system::CPerfCounter timer;
            (void)timer.update();
            {
                tests::TMemoryContextScope scope{ &context };
                TEST_EXPECT(ctx, merge_instances(query, query, schema, output, diagnostic));
            }
            const std::uint64_t elapsed = timer.query_delta();
            const CDocumentRead actual{ output }, expected{ input };
            TEST_EXPECT(ctx, tests::equal_document_values(actual, actual.root(), expected, expected.root()));
            if ((width == 1u) && !baked_input)
            {
                baseline_attempts = fixture.attempts;
                baseline_bytes = fixture.bytes;
            }
            else
            {
                TEST_EXPECT(ctx, (fixture.attempts == baseline_attempts) && (fixture.bytes == baseline_bytes));
            }
            platform::system::CPerfCountConversion conversion;
            std::uint64_t micros{};
            (void)conversion.init();
            (void)conversion.to_microseconds_u64(elapsed, micros);
            std::cout << "Sparse data merge: " << width << " schema members, " << (baked_input ? "baked " : "live ")
                << micros << " us, " << fixture.attempts << " allocations, " << fixture.bytes << " requested bytes\n";
            output.deallocate();
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
    }
}

}

void run_data_merge_cases(tests::TTestContext& ctx)
{
    CLiveDocument schema_document;
    CResolvedSchema schema;
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, tests::parse_document(k_schema, schema_document) && schema.resolve(schema_document, diagnostic));
    test_rules(ctx, schema);
    test_limits(ctx);
    test_specialisation_depth(ctx, schema);
    test_integration(ctx);
    test_growth(ctx, schema);
    test_sparse_wide_structures(ctx);
    test_value_parity(ctx, schema);
    test_presentation(ctx, schema);
    test_aliases<false>(ctx, schema_document, schema);
    test_aliases<true>(ctx, schema_document, schema);
    test_failures<false>(ctx, schema);
    test_failures<true>(ctx, schema);
}
