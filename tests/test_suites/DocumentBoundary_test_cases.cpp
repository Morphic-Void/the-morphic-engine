
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentBoundary_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Aggregate composition, explicit runtime import and transactional boundaries.

#include "tests/test_suites/DocumentBoundary_test_cases.hpp"
#include "schema/schema.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_translation.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
using namespace schema;
using tests::TTestContext;

static const char* const k_types = R"({"types":{
    "enumerations":{"E":{"storage":"u8","values":{"red":0,"blue":1}}},
    "structures":{
        "Pair":{"members":[{"a":{"type":"u32","default":7}},{"b":{"type":"u32","default":9}}]},
        "Empty":{"members":[]},
        "Array":{"members":[{"a":{"type":{"element":"E","count":2}}}]}
    }
}})";

static bool load(const std::string& text, CSchema& target)
{
    CLiveDocument document;
    SSchemaDiagnostic error;
    return tests::parse_document(text, document) && target.try_adopt(std::move(document), error);
}

static void test_composition(TTestContext& ctx)
{
    CLiveDocument types, instances, bulk;
    TEST_EXPECT(ctx, tests::parse_document(k_types, types));
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"Unrelated":{"members":[]}}},"instances":{"Pair":{"a":{"declaration":{"a":0x0007},"specialisation":{}}}},"data":{}})", instances));
    TEST_EXPECT(ctx, tests::parse_document(R"({"data":{"Pair":{"r":{"data":[[1,2],[3,4]]}}}})", bulk));
    CBakedDocumentBlock tb, ib, bb;
    TEST_EXPECT(ctx, document_translation::bake(types, tb) && document_translation::bake(instances, ib) && document_translation::bake(bulk, bb));
    for (unsigned representation = 0u; representation < 8u; ++representation)
    {
        const CSchemaDocumentQuery tq = representation & 1u ? CSchemaDocumentQuery{ tb.document() } : CSchemaDocumentQuery{ types };
        const CSchemaDocumentQuery iq = representation & 2u ? CSchemaDocumentQuery{ ib.document() } : CSchemaDocumentQuery{ instances };
        const CSchemaDocumentQuery bq = representation & 4u ? CSchemaDocumentQuery{ bb.document() } : CSchemaDocumentQuery{ bulk };
        for (unsigned sections = 0u; sections < 8u; ++sections)
        {
            CSchema composed;
            SSchemaDiagnostic error;
            TEST_EXPECT(ctx, composed.compose_from(sections & 1u ? tq : CSchemaDocumentQuery{},
                sections & 2u ? iq : CSchemaDocumentQuery{}, sections & 4u ? bq : CSchemaDocumentQuery{}, error));
            const auto query = composed.document_query();
            TEST_EXPECT(ctx, static_cast<bool>(query.object_child(query.root(), CStringView{ "types" })) == static_cast<bool>(sections & 1u));
            TEST_EXPECT(ctx, static_cast<bool>(query.object_child(query.root(), CStringView{ "instances" })) == static_cast<bool>(sections & 2u));
            TEST_EXPECT(ctx, static_cast<bool>(query.object_child(query.root(), CStringView{ "data" })) == static_cast<bool>(sections & 4u));
            TEST_EXPECT(ctx, composed.validate(error) == static_cast<bool>(sections & 1u));
            if (!(sections & 1u)) TEST_EXPECT(ctx, error.reason == ESchemaReason::missing_types);
            CBakedDocumentBlock saved;
            TEST_EXPECT(ctx, composed.save(saved, error));
            TEST_EXPECT(ctx, composed.compose_from(sections & 1u ? query : CSchemaDocumentQuery{},
                sections & 2u ? query : CSchemaDocumentQuery{}, sections & 4u ? query : CSchemaDocumentQuery{}, error) &&
                tests::same_baked_document(composed.document(), saved));
        }
    }
    TEST_EXPECT(ctx, tests::same_baked_document(types, tb) && tests::same_baked_document(instances, ib) && tests::same_baked_document(bulk, bb));
    CSchema empty, composed;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load("{}", empty) && composed.compose_from({}, {}, {}, error));
    TEST_EXPECT(ctx, composed.document().child_count(composed.document().root()) == 0u);
    CBakedDocumentBlock empty_block;
    TEST_EXPECT(ctx, empty.save(empty_block, error));
    TEST_EXPECT(ctx, composed.compose_from(CSchemaDocumentQuery{ types }, CSchemaDocumentQuery{ instances }, CSchemaDocumentQuery{ bulk }, error));
    CBakedDocumentBlock sentinel;
    TEST_EXPECT(ctx, composed.save(sentinel, error));
    const auto retained_root = composed.document_query().root();
    for (const bool baked : { false, true })
    {
        const auto missing = baked ? CSchemaDocumentQuery{ empty_block.document() } : empty.document_query();
        for (unsigned index = 0u; index < 3u; ++index)
        {
            CSchemaDocumentQuery inputs[3]{};
            inputs[index] = missing;
            TEST_EXPECT(ctx, !composed.compose_from(inputs[0], inputs[1], inputs[2], error) &&
                error.stage == ESchemaStage::structure && error.reason == ESchemaReason::invalid_source &&
                error.source == static_cast<ESchemaSource>(index + 1u) && missing.contains(error.first));
            TEST_EXPECT(ctx, composed.document_query().root() == retained_root && tests::same_baked_document(composed.document(), sentinel));
        }
    }
    TEST_EXPECT(ctx, load(R"({"types":{},"instances":{},"data":{}})", empty) &&
        composed.compose_from(empty.document_query(), empty.document_query(), empty.document_query(), error));
    TEST_EXPECT(ctx, composed.document().child_count(composed.document().root()) == 3u);
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"Later":{"members":[{"v":{"type":"Missing","default":null}}]}}},"data":{"Later":{"r":{}}}})", empty));
    TEST_EXPECT(ctx, composed.compose_from(empty.document_query(), {}, empty.document_query(), error));
    CBakedDocumentBlock saved;
    TEST_EXPECT(ctx, composed.save(saved, error) && !composed.validate(error));
    CLiveDocument malformed;
    TEST_EXPECT(ctx, tests::parse_document(R"({"unexpected":true})", malformed));
    for (unsigned index = 0u; index < 3u; ++index)
    {
        CSchemaDocumentQuery inputs[]{ CSchemaDocumentQuery{ types }, CSchemaDocumentQuery{ instances }, CSchemaDocumentQuery{ bulk } };
        inputs[index] = CSchemaDocumentQuery{ malformed };
        TEST_EXPECT(ctx, !composed.compose_from(inputs[0], inputs[1], inputs[2], error) &&
            error.source == static_cast<ESchemaSource>(index + 1u) && inputs[index].contains(error.first) &&
            tests::same_baked_document(composed.document(), saved));
    }
}

template<bool Bulk>
static void expect_import(TTestContext& ctx, CLiveTypes& types, const std::string& text,
    const CByteConstView payload, const std::string& expected,
    const std::conditional_t<Bulk, EBulkDataLoadReason, EInstanceLoadReason> failure = {})
{
    using Role = std::conditional_t<Bulk, CBakedBulkData, CBakedInstances>;
    using Live = std::conditional_t<Bulk, CLiveBulkData, CLiveInstances>;
    using Diagnostic = std::conditional_t<Bulk, SBulkDataDiagnostic, SInstanceDiagnostic>;
    CLiveDocument input, wanted;
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, tests::parse_document(text, input) && document_translation::bake(input, block));
    if (!expected.empty()) TEST_EXPECT(ctx, tests::parse_document(expected, wanted));
    for (const bool promote : { false, true })
    {
        Role role;
        Live live;
        Diagnostic detail;
        SSchemaDiagnostic error;
        CSchema output;
        TEST_EXPECT(ctx, load(R"({"instances":{"Sentinel":{}}})", output));
        CBakedDocumentBlock before;
        TEST_EXPECT(ctx, output.save(before, error));
        const auto retained = output.document_query().first_child(output.document_query().root());
        bool accepted = role.set_document(block.document()) && role.attach_types(types, payload, detail);
        if (accepted && promote) accepted = role.promote(live, detail);
        if (accepted) accepted = promote ? output.import_from(live, error) : output.import_from(role, error);
        TEST_EXPECT(ctx, accepted == !expected.empty());
        if (accepted)
        {
            const schema::detail::CDocumentRead actual{ output.document() }, match{ wanted };
            TEST_EXPECT(ctx, tests::equal_document_values(actual, actual.root(), match, match.root()));
            TEST_EXPECT(ctx, output.document().check_integrity());
        }
        else
        {
            const auto reason = detail.reason != decltype(detail.reason)::none ? detail.reason :
                [&]() { if constexpr (Bulk) return error.bulk_data.reason; else return error.instances.reason; }();
            TEST_EXPECT(ctx, reason == failure && tests::same_baked_document(output.document(), before));
            TEST_EXPECT(ctx, output.document_query().contains(retained));
        }
        TEST_EXPECT(ctx, tests::same_baked_document(input, block));
    }
}

static void test_runtime_import(TTestContext& ctx)
{
    CLiveDocument definitions;
    CLiveTypes types;
    SDiagnostic error;
    TEST_EXPECT(ctx, tests::parse_document(k_types, definitions) && types.try_adopt(std::move(definitions)) && types.resolve(error));
    alignas(128) std::uint32_t bytes[]{ 7u, 9u, 11u, 9u, 7u, 9u, 0xCCCCCCCCu, 0xCCCCCCCCu };
    const CByteConstView payload{ reinterpret_cast<const std::uint8_t*>(bytes), sizeof(bytes) };
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"Pair":{"base":{"locator":{"offset":0,"valid":true},"specialisation":{"child":{"locator":{"offset":8,"valid":true},"declaration":{"a":true},"specialisation":{"restore":{"locator":{"offset":16,"valid":true},"declaration":{"a":true},"specialisation":{}}}}}}}}})", payload,
        R"({"instances":{"Pair":{"base":{"specialisation":{"child":{"declaration":{"a":11},"specialisation":{"restore":{"declaration":{"a":7},"specialisation":{}}}}}}}}})");
    expect_import<true>(ctx, types, R"({"stripped":true,"data":{"Pair":{"x":{"locator":{"offset":0,"valid":true,"count":2}}}}})", payload,
        R"({"data":{"Pair":{"x":{"data":[{"a":7,"b":9},{"a":11,"b":9}]}}}})");
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"Pair":{"x":{"locator":{"offset":8,"valid":true}}}}})", payload, {}, EInstanceLoadReason::inheritance_mismatch);
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true},"declaration":{"a":false}}}}})", payload, {}, EInstanceLoadReason::invalid_declaration);
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true},"declaration":{"unknown":true}}}}})", payload, {}, EInstanceLoadReason::invalid_declaration);
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"Empty":{"x":{"locator":{"offset":0,"valid":true},"declaration":{}}}}})", {},
        R"({"instances":{"Empty":{"x":{"declaration":{}}}}})");
    expect_import<true>(ctx, types, R"({"stripped":true,"data":{"Empty":{"x":{"locator":{"offset":0,"valid":true,"count":2,"size":0}}}}})", {},
        R"({"data":{"Empty":{"x":{"data":[{},{}]}}}})");
    expect_import<true>(ctx, types, R"({"stripped":true,"data":{"Empty":{"x":{"locator":{"offset":0,"valid":true,"size":0}}}}})", {}, {}, EBulkDataLoadReason::invalid_count);
    for (const std::string& locator : { R"({"offset":0,"valid":false})", R"({"offset":1,"valid":true})",
        R"({"offset":32,"valid":true})", R"({"offset":0,"valid":true,"size":7})", R"({"offset":0,"valid":true,"count":2})" })
    {
        const auto reason = locator.find("false") != std::string::npos ? EInstanceLoadReason::invalid_locator :
            (locator.find("count") != std::string::npos ? EInstanceLoadReason::invalid_count : EInstanceLoadReason::invalid_range);
        expect_import<false>(ctx, types, "{\"stripped\":true,\"instances\":{\"Pair\":{\"x\":{\"locator\":" + locator + "}}}}", payload, {}, reason);
    }
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"Pair":{"a":{"locator":{"offset":0,"valid":true}},"b":{"locator":{"offset":4,"valid":true}}}}})", payload, {}, EInstanceLoadReason::overlap);
    alignas(128) std::uint8_t unknown = 9u;
    expect_import<false>(ctx, types, R"({"stripped":true,"instances":{"E":{"x":{"locator":{"offset":0,"valid":true},"declaration":true}}}})",
        CByteConstView{ &unknown, 1u }, {}, EInstanceLoadReason::unrepresentable_value);
    expect_import<true>(ctx, types, R"({"stripped":true,"data":{"E":{"x":{"locator":{"offset":0,"valid":true,"count":1}}}}})",
        CByteConstView{ &unknown, 1u }, {}, EBulkDataLoadReason::unrepresentable_value);
}

static void test_workflow(TTestContext& ctx)
{
    CSchema authoring, new_defaults, instance_export, bulk_export, types_export, merged;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"One":{"members":[{"v":{"type":"u32"}}]}}},
        "instances":{"One":{"adopt":{},"explicit":{"declaration":0},"base":{"declaration":11,"specialisation":{"child":{"declaration":0}}}}},
        "data":{"One":{"records":{"data":[11,0]}}}})", authoring));
    CLiveTypes types;
    CLiveInstances instances;
    CLiveBulkData bulk;
    TEST_EXPECT(ctx, authoring.materialise(types, instances, bulk, error));
    TEST_EXPECT(ctx, types_export.import_from(types, error) && instance_export.import_from(instances, error) && bulk_export.import_from(bulk, error));
    TEST_EXPECT(ctx, !instance_export.validate(error) && error.reason == ESchemaReason::missing_types);
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"One":{"members":[{"v":{"type":"u32","default":7}}]}}}})", new_defaults));
    TEST_EXPECT(ctx, types_export.merge_from(types_export.document_query(), new_defaults.document_query(), error));
    TEST_EXPECT(ctx, merged.compose_from(types_export.document_query(), instance_export.document_query(), bulk_export.document_query(), error) && merged.validate(error));
    CLiveTypes replacement;
    CLiveInstances materialised;
    CLiveBulkData records;
    TEST_EXPECT(ctx, merged.materialise(replacement, materialised, records, error));
    SInstanceEntryView value;
    for (const auto& item : { std::pair<const char*, std::uint32_t>{ "adopt", 7u }, { "explicit", 0u }, { "base", 11u } })
    {
        TEST_EXPECT(ctx, materialised.entry(materialised.find_base(CStringView{ "One" }, CStringView{ item.first }), value) && read_scalar_bits(value.bytes, 4u) == item.second);
    }
    TEST_EXPECT(ctx, materialised.entry(materialised.find_specialisation(materialised.find_base(CStringView{ "One" }, CStringView{ "base" }), CStringView{ "child" }), value) && read_scalar_bits(value.bytes, 4u) == 0u);
    SBulkDataEntryView data;
    TEST_EXPECT(ctx, records.entry(records.find_entry(CStringView{ "One" }, CStringView{ "records" }), data) && data.count == 2u &&
        read_scalar_bits(data.bytes, 4u) == 11u && read_scalar_bits(data.bytes + 4u, 4u) == 0u);
}

static void test_failures(TTestContext& ctx)
{
    CSchema source;
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"V":{"members":[{"a":{"type":"u32"}},{"b":{"type":"u32"}}]}}},
        "instances":{"V":{"base":{"declaration":{"b":2,"a":1},"specialisation":{"child":{"declaration":{"a":3}}}}}},
        "data":{"V":{"r":{"data":[[1,2],[3,4]]}}}})", source));
    CLiveTypes types;
    CLiveInstances instances;
    CLiveBulkData bulk;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, source.materialise(types, instances, bulk, error));
    CBakedDocumentBlock type_block;
    CBakedTypes baked_types;
    TEST_EXPECT(ctx, document_translation::bake(source.document(), type_block) &&
        baked_types.set_document(type_block.document()));
    for (unsigned operation = 0u; operation < 7u; ++operation)
    {
        bool completed = false;
        for (std::size_t fail = 0u; fail < 2048u && !completed; ++fail)
        {
            CSchema output;
            TEST_EXPECT(ctx, load(R"({"instances":{"Sentinel":{}}})", output));
            CBakedDocumentBlock before;
            TEST_EXPECT(ctx, output.save(before, error));
            const auto retained = output.document_query().first_child(output.document_query().root());
            tests::TAllocationFailureFixture failures{ 0u, fail };
            memory::CMemoryAllocator allocator{ &failures, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            {
                tests::TMemoryContextScope scope{ &context };
                if (operation == 0u) completed = output.compose_from(source.document_query(), source.document_query(), source.document_query(), error);
                else if (operation == 1u) completed = output.import_from(instances, error);
                else if (operation == 2u) completed = output.import_from(bulk, error);
                else if (operation == 3u)
                {
                    CLiveTypes t;
                    CLiveInstances i;
                    CLiveBulkData b;
                    completed = source.materialise(t, i, b, error);
                    if (!completed) TEST_EXPECT(ctx, !t.document_ready() && !i.document_ready() && !b.document_ready());
                }
                else if (operation == 4u)
                {
                    CBakedDataBlock block;
                    completed = source.bake(block, error);
                    if (!completed) TEST_EXPECT(ctx, !block.loaded_ready());
                }
                else
                {
                    completed = operation == 5u ? output.import_from(types, error) : output.import_from(baked_types, error);
                    if (!completed) TEST_EXPECT(ctx, fail == 0u ? error.stage == ESchemaStage::copying :
                        error.stage != ESchemaStage::none);
                }
            }
            if (!completed)
            {
                TEST_EXPECT(ctx, error.reason != ESchemaReason::none && tests::same_baked_document(output.document(), before));
                TEST_EXPECT(ctx, output.document_query().contains(retained));
                TEST_EXPECT(ctx, !error.instances.occurrence || instances.document_query().contains(error.instances.occurrence) || source.instances_query().contains(error.instances.occurrence));
                TEST_EXPECT(ctx, !error.bulk_data.occurrence || bulk.document_query().contains(error.bulk_data.occurrence) || source.bulk_data_query().contains(error.bulk_data.occurrence));
                if (operation == 3u || operation == 4u)
                {
                    TEST_EXPECT(ctx, !error.first || source.document_query().contains(error.first));
                    for (const auto location : { error.types.occurrence, error.types.enclosing_type, error.types.enclosing_member, error.types.related })
                    {
                        TEST_EXPECT(ctx, !location || source.types_query().contains(location));
                    }
                    TEST_EXPECT(ctx, error.block.location.kind == EDataBlockLocationKind::none);
                }
            }
            output.clear();
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed);
    }
}

static void test_import_depth(TTestContext& ctx)
{
    CLiveDocument document;
    CLiveTypes types;
    SDiagnostic type_error;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{}})", document) && types.try_adopt(std::move(document)) && types.resolve(type_error));
    CLiveInstances instances;
    SInstanceDiagnostic instance_error;
    TEST_EXPECT(ctx, instances.initialise(types));
    auto parent = instances.create_base(CStringView{ "u8" }, CStringView{ "root" }, {}, {}, instance_error);
    for (unsigned i = 0u; i < 1024u; ++i)
    {
        parent = instances.create_specialisation(parent, CStringView{ "child" }, {}, {}, instance_error);
        TEST_EXPECT(ctx, parent);
    }
    CSchema output;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"types":{}})", output));
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, output.save(before, error));
    TEST_EXPECT(ctx, !output.import_from(instances, error) && error.reason == ESchemaReason::storage_limit);
    TEST_EXPECT(ctx, tests::same_baked_document(output.document(), before) && instances.document_query().contains(error.instances.occurrence));
}

}   // namespace

void run_document_boundary_cases(tests::TTestContext& context)
{
    test_composition(context);
    test_runtime_import(context);
    test_workflow(context);
    test_failures(context);
    test_import_depth(context);
}
