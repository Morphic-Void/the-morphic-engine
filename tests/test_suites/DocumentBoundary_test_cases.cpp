//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentBoundary_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    19 Oct 26
//
//  Document preparation/composition admission, reconstruction and publication.

#include "tests/test_suites/DocumentBoundary_test_cases.hpp"
#include "schema/data_preparation.hpp"
#include "schema/document_composition.hpp"
#include "schema/document_construction.hpp"
#include "schema/data_merge.hpp"
#include "schema/schema_merge.hpp"
#include "schema/value_codec.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"
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

static const char* const k_types = R"({"types":{
    "enumerations":{"E":{"storage":"u8","values":{"red":0,"blue":1}}},
    "structures":{
        "Pair":{"members":[{"a":{"type":"u32","default":7}},{"b":{"type":"u32","default":9}}]},
        "Empty":{"members":[]},
        "Array":{"members":[{"a":{"type":{"element":"E","count":2}}}]}
    }
}})";

template<bool Bulk>
using TQuery = std::conditional_t<Bulk, CBulkDataDocumentQuery, CInstanceDocumentQuery>;
template<bool Bulk>
using TDiagnostic = std::conditional_t<Bulk, SBulkDataPreparationDiagnostic, SInstancePreparationDiagnostic>;
template<bool Bulk>
using TReason = std::conditional_t<Bulk, EBulkDataLoadReason, EInstanceLoadReason>;

template<bool Bulk>
static bool prepare(const TQuery<Bulk>& query, const CResolvedSchema& schema, const CByteConstView* const payload,
    const bool compare, CLiveDocument& output, TDiagnostic<Bulk>& diagnostic)
{
    if constexpr (Bulk)
    {
        return payload ? prepare_bulk_data_document(query, schema, *payload, compare, output, diagnostic) :
            prepare_bulk_data_document(query, schema, output, diagnostic);
    }
    else
    {
        return payload ? prepare_instances_document(query, schema, *payload, compare, output, diagnostic) :
            prepare_instances_document(query, schema, output, diagnostic);
    }
}

template<bool Bulk>
static void expect_preparation(TTestContext& ctx, const CResolvedSchema& schema, const std::string& text,
    const CByteConstView* const payload, const bool compare, const std::string& expected,
    const TReason<Bulk> reason = TReason<Bulk>::none)
{
    CLiveDocument source, wanted;
    TEST_EXPECT(ctx, tests::parse_document(text, source));
    if (!expected.empty()) TEST_EXPECT(ctx, tests::parse_document(expected, wanted));
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(source, baked));
    for (const bool fixed : { false, true })
    {
        const TQuery<Bulk> query = fixed ? TQuery<Bulk>{ baked.document() } : TQuery<Bulk>{ source };
        CLiveDocument output;
        TEST_EXPECT(ctx, tests::parse_document(R"({"sentinel":42})", output));
        CBakedDocumentBlock before;
        TEST_EXPECT(ctx, document_translation::bake(output, before));
        const CNodeKey handle = output.first_child(output.root());
        TDiagnostic<Bulk> diagnostic;
        const bool accepted = prepare<Bulk>(query, schema, payload, compare, output, diagnostic);
        if (accepted != !expected.empty()) std::cout << "Preparation case: " << text << '\n';
        TEST_EXPECT(ctx, accepted == !expected.empty());
        if (accepted)
        {
            const CDocumentRead actual{ output }, match{ wanted };
            TEST_EXPECT(ctx, tests::equal_document_values(actual, actual.root(), match, match.root()));
            TEST_EXPECT(ctx, diagnostic.stage == EDataPreparationStage::none && diagnostic.reason == EDataPreparationReason::none);
            TEST_EXPECT(ctx, output.check_integrity() && output.is_complete());
        }
        else
        {
            TEST_EXPECT(ctx, tests::same_baked_document(output, before) && output.contains(handle));
            TEST_EXPECT(ctx, diagnostic.detail.reason == reason);
            TEST_EXPECT(ctx, !diagnostic.detail.occurrence || query.contains(diagnostic.detail.occurrence));
        }
        TEST_EXPECT(ctx, tests::same_baked_document(source, baked));
    }
}

static void test_preparation(TTestContext& ctx, const CResolvedSchema& schema)
{
    const std::string instances = R"({"instances":{"Pair":{"x":{"declaration":{"a":7}}}}})";
    const std::string bulk = R"({"data":{"Pair":{"x":{"data":[{"a":7,"b":9}]}}}})";
    expect_preparation<false>(ctx, schema, instances, nullptr, false, instances);
    expect_preparation<true>(ctx, schema, bulk, nullptr, false, bulk);
    expect_preparation<false>(ctx, schema, R"({"types":{"nonsense":null},"data":{"unknown":null},"instances":{"Pair":{"x":{"declaration":{"a":7}}}}})", nullptr, false, instances);
    expect_preparation<true>(ctx, schema, R"({"types":{"nonsense":null},"instances":{"unknown":null},"data":{"Pair":{"x":{"data":[{"a":7,"b":9}]}}}})", nullptr, false, bulk);
    expect_preparation<false>(ctx, schema, "{}", nullptr, false, R"({"instances":{}})");
    expect_preparation<true>(ctx, schema, "{}", nullptr, false, R"({"data":{}})");
    expect_preparation<false>(ctx, schema, R"({"types":[],"instances":{}})", nullptr, false, {}, EInstanceLoadReason::invalid_input);
    expect_preparation<false>(ctx, schema, R"({"unknown":{},"instances":{}})", nullptr, false, {}, EInstanceLoadReason::unknown_property);
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{"data":[{"a":7}]}}}})", nullptr, false, {}, EBulkDataLoadReason::incomplete_record);
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{"data":[]}}}})", nullptr, false, {}, EBulkDataLoadReason::invalid_count);
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{}}}})", nullptr, false, {}, EBulkDataLoadReason::binary_required);
    expect_preparation<false>(ctx, schema, R"({"instances":{"Pair":{"x":{"locator":{"offset":3,"valid":false,"count":1,"size":8},"declaration":{"a":7}}}}})", nullptr, false, instances);
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{"locator":{"offset":3,"valid":false,"count":1,"size":8},"data":[{"a":7,"b":9}]}}}})", nullptr, false, bulk);
    expect_preparation<false>(ctx, schema, R"({"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true},"declaration":{"a":7}}}}})", nullptr, false, {}, EInstanceLoadReason::binary_required);

    CByteBuffer payload;
    TEST_EXPECT(ctx, payload.allocate(32u, 128u) && payload.set_size(32u));
    std::memset(payload.data(), 0xCC, payload.size());
    for (unsigned i = 0u; i < 3u; ++i)
    {
        write_scalar_bits((payload.data() + (i * 8u)), 4u, ((i == 1u) ? 11u : 7u));
        write_scalar_bits((payload.data() + (i * 8u) + 4u), 4u, 9u);
    }
    const CByteConstView view = payload.const_view();
    const CByteConstView empty;
    const std::string stripped = R"({"stripped":true,"instances":{"Pair":{"base":{"locator":{"offset":0,"valid":true},"specialisation":{"child":{"locator":{"offset":8,"valid":true},"specialisation":{"restore":{"locator":{"offset":16,"valid":true}}}}}}}}})";
    const std::string restored = R"({"instances":{"Pair":{"base":{"specialisation":{"child":{"declaration":{"a":11},"specialisation":{"restore":{"declaration":{"a":7}}}}}}}}})";
    expect_preparation<false>(ctx, schema, stripped, &view, false, restored);
    expect_preparation<false>(ctx, schema, stripped, nullptr, false, {}, EInstanceLoadReason::binary_required);
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{"locator":{"offset":0,"valid":true,"size":16}}}}})", &view, false,
        R"({"data":{"Pair":{"x":{"data":[{"a":7,"b":9},{"a":11,"b":9}]}}}})");
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{"locator":{"offset":0,"valid":true,"count":1},"data":[[13,9]]},"y":{"locator":{"offset":8,"valid":true,"count":1}}}}})", &view, false,
        R"({"data":{"Pair":{"x":{"data":[[13,9]]},"y":{"data":[{"a":11,"b":9}]}}}})");
    expect_preparation<false>(ctx, schema, instances, &view, false, {}, EInstanceLoadReason::missing_property);
    const std::string retained = R"({"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true},"declaration":{"a":13}}}}})";
    expect_preparation<false>(ctx, schema, retained, &view, false, R"({"instances":{"Pair":{"x":{"declaration":{"a":13}}}}})");
    expect_preparation<false>(ctx, schema, retained, &view, true, {}, EInstanceLoadReason::embedded_mismatch);
    expect_preparation<true>(ctx, schema, R"({"data":{"Pair":{"x":{"locator":{"offset":0,"valid":true,"count":2},"data":[[7,9]]}}}})", &view, false, {}, EBulkDataLoadReason::invalid_count);
    expect_preparation<true>(ctx, schema, R"({"stripped":true,"data":{"Empty":{"x":{"locator":{"offset":0,"valid":true,"count":2,"size":0}}}}})", &empty, false,
        R"({"data":{"Empty":{"x":{"data":[{},{}]}}}})");
    expect_preparation<true>(ctx, schema, R"({"data":{"Empty":{"x":{"locator":{"offset":0,"valid":true,"size":0}}}}})", &empty, false, {}, EBulkDataLoadReason::invalid_count);
    expect_preparation<false>(ctx, schema, R"({"stripped":true,"instances":{"Empty":{"x":{"locator":{"offset":0,"valid":true}}}}})", &empty, false,
        R"({"instances":{"Empty":{"x":{}}}})");

    for (unsigned mask = 1u; mask < 8u; ++mask)
    {
        std::string source = "{";
        if (mask & 1u) source += R"("fileType":"morphic-instance-data",)";
        if (mask & 2u) source += R"("version":1,)";
        if (mask & 4u) source += R"("payloadSize":0,)";
        source += R"("types":{"ignored":null},"instances":{}})";
        expect_preparation<false>(ctx, schema, source, &empty, false, (mask == 7u) ? R"({"instances":{}})" : "",
            (mask == 7u) ? EInstanceLoadReason::none : EInstanceLoadReason::missing_property);
        expect_preparation<false>(ctx, schema, source, nullptr, false, {},
            ((mask == 7u) ? EInstanceLoadReason::binary_required : EInstanceLoadReason::missing_property));
        source = "{";
        if (mask & 1u) source += R"("fileType":"morphic-bulk-data",)";
        if (mask & 2u) source += R"("version":1,)";
        if (mask & 4u) source += R"("payloadSize":0,)";
        source += R"("instances":{"ignored":null},"data":{}})";
        expect_preparation<true>(ctx, schema, source, &empty, false, ((mask == 7u) ? R"({"data":{}})" : ""),
            ((mask == 7u) ? EBulkDataLoadReason::none : EBulkDataLoadReason::missing_property));
        expect_preparation<true>(ctx, schema, source, nullptr, false, {},
            ((mask == 7u) ? EBulkDataLoadReason::binary_required : EBulkDataLoadReason::missing_property));
    }
    expect_preparation<false>(ctx, schema, R"({"fileType":"morphic-bulk-data","version":1,"payloadSize":0,"instances":{}})", &empty, false, {}, EInstanceLoadReason::invalid_input);
    expect_preparation<false>(ctx, schema, R"({"fileType":"morphic-instance-data","version":2,"payloadSize":0,"instances":{}})", &empty, false, {}, EInstanceLoadReason::invalid_input);
    expect_preparation<false>(ctx, schema, R"({"fileType":"morphic-instance-data","version":1,"payloadSize":24,"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true}}}}})", &view, false, {}, EInstanceLoadReason::invalid_range);
    expect_preparation<false>(ctx, schema, R"({"fileType":"morphic-instance-data","version":1,"payloadSize":32,"instances":{"Pair":{"x":{"locator":{"offset":16,"valid":true}}}}})", &view, true,
        R"({"instances":{"Pair":{"x":{}}}})");

    for (const std::string& location : { R"({"offset":0,"valid":false})", R"({"offset":1,"valid":true})",
        R"({"offset":32,"valid":true})", R"({"offset":0,"valid":true,"size":7})", R"({"offset":0,"valid":true,"count":2})" })
    {
        const auto reason = location.find("false") != std::string::npos ? EInstanceLoadReason::invalid_locator :
            ((location.find("count") != std::string::npos) ? EInstanceLoadReason::invalid_count : EInstanceLoadReason::invalid_range);
        expect_preparation<false>(ctx, schema, "{\"instances\":{\"Pair\":{\"x\":{\"locator\":" + location + "}}}}", &view, false, {}, reason);
    }
    expect_preparation<false>(ctx, schema, R"({"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true}},"y":{"locator":{"offset":4,"valid":true}}}}})", &view, false, {}, EInstanceLoadReason::overlap);
    expect_preparation<false>(ctx, schema, R"({"stripped":true,"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true},"declaration":{}}}}})", &view, false, {}, EInstanceLoadReason::invalid_declaration);

    for (const std::string& type : { "b8", "E", "f16" })
    {
        write_scalar_bits(payload.data(), 2u, ((type == "f16") ? 0x7C00u : 2u));
        const std::string entry = R"({"x":{"locator":{"offset":0,"valid":true)" +
            std::string{ type == "f16" ? ",\"size\":2" : ",\"size\":1" } + "}}}";
        expect_preparation<false>(ctx, schema, "{\"stripped\":true,\"instances\":{\"" + type + "\":" + entry + "}}", &view, false, {}, EInstanceLoadReason::unrepresentable_value);
        expect_preparation<true>(ctx, schema, "{\"data\":{\"" + type + "\":{\"x\":{\"locator\":{\"offset\":0,\"valid\":true,\"count\":1}}}}}", &view, false, {}, EBulkDataLoadReason::unrepresentable_value);
    }
    write_scalar_bits(payload.data(), 1u, 0u);
    write_scalar_bits((payload.data() + 1u), 1u, 1u);
    expect_preparation<true>(ctx, schema, R"({"data":{"Array":{"x":{"locator":{"offset":0,"valid":true,"count":1}}}}})", &view, false,
        R"({"data":{"Array":{"x":{"data":[[["red","blue"]]]}}}})");
    expect_preparation<false>(ctx, schema, R"({"stripped":true,"instances":{"Array":{"x":{"locator":{"offset":0,"valid":true}}}}})", &view, false,
        R"({"instances":{"Array":{"x":{"declaration":{"a":["red","blue"]}}}}})");
    const CByteConstView unaligned{ (payload.data() + 1u), 8u, 1u };
    expect_preparation<false>(ctx, schema, retained, &unaligned, false, {}, EInstanceLoadReason::invalid_range);
    expect_preparation<true>(ctx, schema, bulk, &unaligned, false, {}, EBulkDataLoadReason::invalid_range);
    expect_preparation<false>(ctx, schema, R"({"instances":{"Empty":{},"Pair":{"x":{"specialisation":{}}}}})", nullptr, false,
        R"({"instances":{"Empty":{},"Pair":{"x":{"specialisation":{}}}}})");
    CResolvedSchema unready;
    CLiveDocument source, output;
    TEST_EXPECT(ctx, tests::parse_document("{}", source));
    SInstancePreparationDiagnostic error;
    TEST_EXPECT(ctx, !prepare_instances_document(CInstanceDocumentQuery{ source }, unready, output, error));
    TEST_EXPECT(ctx, error.reason == EDataPreparationReason::invalid_schema && !output.is_ready());
}

static void test_composition(TTestContext& ctx)
{
    CLiveDocument types, instances, bulk, wanted;
    TEST_EXPECT(ctx, tests::parse_document(k_types, types));
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"Pair":{"x":{"declaration":{"a":7},"specialisation":{"child":{}}}}}})", instances));
    TEST_EXPECT(ctx, tests::parse_document(R"({"data":{"E":{"x":{"data":["blue","red"]}}}})", bulk));
    CBakedDocumentBlock tb, ib, bb;
    TEST_EXPECT(ctx, document_translation::bake(types, tb) && document_translation::bake(instances, ib) && document_translation::bake(bulk, bb));
    for (unsigned domains = 1u; domains <= 3u; ++domains)
    {
        for (unsigned forms = 0u; forms < 8u; ++forms)
        {
            const CSchemaDocumentQuery t = (forms & 1u) ? CSchemaDocumentQuery{ tb.document() } : CSchemaDocumentQuery{ types };
            const CInstanceDocumentQuery i = (forms & 2u) ? CInstanceDocumentQuery{ ib.document() } : CInstanceDocumentQuery{ instances };
            const CBulkDataDocumentQuery b = (forms & 4u) ? CBulkDataDocumentQuery{ bb.document() } : CBulkDataDocumentQuery{ bulk };
            CLiveDocument output;
            SDocumentCompositionDiagnostic diagnostic;
            const bool success = (domains == 1u) ? compose_document(t, i, output, diagnostic) :
                ((domains == 2u) ? compose_document(t, b, output, diagnostic) : compose_document(t, i, b, output, diagnostic));
            TEST_EXPECT(ctx, success && diagnostic.stage == EDocumentCompositionStage::none);
            const CDocumentRead actual{ output }, rt{ types }, ri{ instances }, rb{ bulk };
            SOccurrence section = actual.first_child(actual.root());
            TEST_EXPECT(ctx, tests::equal_document_values(actual, section, rt, rt.first_child(rt.root())));
            section = actual.next_sibling(section);
            if (domains & 1u)
            {
                TEST_EXPECT(ctx, tests::equal_document_values(actual, section, ri, ri.first_child(ri.root())));
                section = actual.next_sibling(section);
            }
            if (domains & 2u)
            {
                TEST_EXPECT(ctx, tests::equal_document_values(actual, section, rb, rb.first_child(rb.root())));
                section = actual.next_sibling(section);
            }
            TEST_EXPECT(ctx, !section && output.check_integrity());
        }
    }
    const char* invalid_types[]{ "{}", R"({"types":{},"instances":{}})", R"({"types":{"structures":{"Bad":{"members":[{"a":{"type":"Missing"}}]}}}})" };
    for (const auto text : invalid_types)
    {
        CLiveDocument bad, output;
        TEST_EXPECT(ctx, tests::parse_document(text, bad));
        SDocumentCompositionDiagnostic diagnostic;
        TEST_EXPECT(ctx, !compose_document(CSchemaDocumentQuery{ bad }, CInstanceDocumentQuery{ instances }, output, diagnostic));
        TEST_EXPECT(ctx, diagnostic.stage == EDocumentCompositionStage::types_validation && diagnostic.reason == EDocumentCompositionReason::invalid_source);
    }
    for (const bool data : { false, true })
    {
        CLiveDocument bad, output;
        TEST_EXPECT(ctx, tests::parse_document(data ? R"({"data":{"Pair":{"x":{"data":[{}]}}}})" : R"({"instances":{},"types":{}})", bad));
        SDocumentCompositionDiagnostic diagnostic;
        const bool result = data ? compose_document(CSchemaDocumentQuery{ types }, CBulkDataDocumentQuery{ bad }, output, diagnostic) :
            compose_document(CSchemaDocumentQuery{ types }, CInstanceDocumentQuery{ bad }, output, diagnostic);
        TEST_EXPECT(ctx, !result && diagnostic.reason == EDocumentCompositionReason::invalid_source);
        TEST_EXPECT(ctx, diagnostic.stage == (data ? EDocumentCompositionStage::bulk_data_validation : EDocumentCompositionStage::instances_validation));
    }
    CLiveDocument empty, output;
    TEST_EXPECT(ctx, tests::parse_document("{}", empty));
    SDocumentCompositionDiagnostic diagnostic;
    TEST_EXPECT(ctx, compose_document(CSchemaDocumentQuery{ types }, CInstanceDocumentQuery{ empty }, output, diagnostic));
    TEST_EXPECT(ctx, output.child_count(output.root()) == 2u && output.object_child(output.root(), CStringView{ "instances" }));
    TEST_EXPECT(ctx, !output.object_child(output.root(), CStringView{ "data" }));
}


template<bool Bulk>
static void test_preparation_failures(TTestContext& ctx, const CResolvedSchema& schema)
{
    std::string retained_instances;
    if constexpr (!Bulk)
    {
        //  Reserving this child batch grows an entry vector that already contains
        //  the parent, while its following base sibling is still pending.
        retained_instances = R"({"instances":{"Pair":{"x":{"declaration":{"a":11},"specialisation":{)";
        for (unsigned i = 0u; i < 64u; ++i)
        {
            if (i) retained_instances += ',';
            retained_instances += "\"child" + std::to_string(i) + "\":{}";
        }
        retained_instances += R"(}},"following":{"declaration":{"a":13}}}}})";
    }
    CByteBuffer payload;
    TEST_EXPECT(ctx, payload.allocate(16u, k_max_alignment) && payload.set_size(16u));
    const std::uint32_t values[]{ 11u, 9u, 7u, 9u };
    std::memcpy(payload.data(), values, sizeof(values));
    const CByteConstView bytes = payload.const_view();
    for (const bool reconstruct : { false, true })
    {
        for (unsigned alias = 0u; alias < 3u; ++alias)
        {
            bool completed = false;
            bool batch_reservation_failed = false;
            for (std::size_t fail = 0u; (fail < 1024u) && !completed; ++fail)
            {
                const char* const text = Bulk ?
                    (reconstruct ? R"({"data":{"Pair":{"x":{"locator":{"offset":0,"valid":true,"count":2}}}}})" :
                        R"({"data":{"Pair":{"x":{"data":[{"a":11,"b":9}]}}}})") :
                    (reconstruct ? R"({"stripped":true,"instances":{"Pair":{"x":{"locator":{"offset":0,"valid":true},"specialisation":{"child":{"locator":{"offset":8,"valid":true}}}}}}})" :
                        retained_instances.c_str());
                CLiveDocument source, separate;
                TEST_EXPECT(ctx, tests::parse_document(text, source));
                if (alias == 1u) TEST_EXPECT(ctx, tests::parse_document(R"({"sentinel":17})", separate));
                CLiveDocument& output = (alias == 2u) ? source : separate;
                CBakedDocumentBlock original, before;
                TEST_EXPECT(ctx, document_translation::bake(source, original));
                if (output.is_ready()) TEST_EXPECT(ctx, document_translation::bake(output, before));
                const CNodeKey handle = output.first_child(output.root());
                tests::TAllocationFailureFixture fixture{ 0u, fail };
                memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
                memory::CMemoryContext context{ allocator };
                TDiagnostic<Bulk> diagnostic;
                {
                    tests::TMemoryContextScope scope{ &context };
                    completed = prepare<Bulk>(TQuery<Bulk>{ source }, schema, (reconstruct ? &bytes : nullptr), true, output, diagnostic);
                }
                if (!completed)
                {
                    TEST_EXPECT(ctx, diagnostic.reason != EDataPreparationReason::none && tests::same_baked_document(source, original));
                    TEST_EXPECT(ctx, before.is_ready() ? tests::same_baked_document(output, before) : !output.is_ready());
                    TEST_EXPECT(ctx, !handle || output.contains(handle));
                    if (diagnostic.stage == EDataPreparationStage::result_validation)
                    {
                        TEST_EXPECT(ctx, !diagnostic.detail.occurrence);
                    }
                    else if (diagnostic.detail.occurrence)
                    {
                        TEST_EXPECT(ctx, TQuery<Bulk>{ source }.contains(diagnostic.detail.occurrence));
                    }
                    if (!Bulk && !reconstruct && (diagnostic.stage == EDataPreparationStage::source_validation) &&
                        (diagnostic.reason == EDataPreparationReason::allocation_failed) &&
                        (TQuery<Bulk>{ source }.name(diagnostic.detail.occurrence) == CStringView{ "specialisation" }))
                    {
                        batch_reservation_failed = true;
                    }
                }
                else
                {
                    TEST_EXPECT(ctx, diagnostic.reason == EDataPreparationReason::none && output.check_integrity());
                    if (!Bulk && !reconstruct)
                    {
                        const CDocumentRead expected{ original.document() }, actual{ output };
                        TEST_EXPECT(ctx, tests::equal_document_values(expected, expected.root(), actual, actual.root()));
                    }
                }
                TEST_EXPECT(ctx, std::memcmp(payload.data(), values, sizeof(values)) == 0);
                output.deallocate();
                TEST_EXPECT(ctx, context.is_attribution_empty());
            }
            TEST_EXPECT(ctx, completed);
            if (!Bulk && !reconstruct) TEST_EXPECT(ctx, batch_reservation_failed);
        }
    }
    CLiveDocument backing, source;
    CResolvedSchema protected_schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, tests::parse_document(k_types, backing) && protected_schema.resolve(backing, schema_error) && tests::parse_document("{}", source));
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(backing, before));
    TDiagnostic<Bulk> diagnostic;
    TEST_EXPECT(ctx, !prepare<Bulk>(TQuery<Bulk>{ source }, protected_schema, nullptr, false, backing, diagnostic));
    TEST_EXPECT(ctx, diagnostic.stage == EDataPreparationStage::preconditions && diagnostic.reason == EDataPreparationReason::invalid_schema);
    TEST_EXPECT(ctx, protected_schema.is_ready() && tests::same_baked_document(backing, before));
}

static void test_composition_failures(TTestContext& ctx)
{
    for (unsigned alias = 0u; alias < 5u; ++alias)
    {
        bool completed = false;
        for (std::size_t fail = 0u; (fail < 1024u) && !completed; ++fail)
        {
            CLiveDocument types, instances, bulk, separate;
            TEST_EXPECT(ctx, tests::parse_document(k_types, types) && tests::parse_document(R"({"instances":{"Pair":{"x":{"declaration":{"a":11}}}}})", instances) &&
                tests::parse_document(R"({"data":{"Pair":{"x":{"data":[[7,9]]}}}})", bulk));
            if (alias == 1u) TEST_EXPECT(ctx, tests::parse_document(R"({"sentinel":17})", separate));
            CLiveDocument& output = (alias == 2u) ? types : ((alias == 3u) ? instances : ((alias == 4u) ? bulk : separate));
            CBakedDocumentBlock tb, ib, bb, before;
            TEST_EXPECT(ctx, document_translation::bake(types, tb) && document_translation::bake(instances, ib) && document_translation::bake(bulk, bb));
            if (output.is_ready()) TEST_EXPECT(ctx, document_translation::bake(output, before));
            const CNodeKey handle = output.first_child(output.root());
            tests::TAllocationFailureFixture fixture{ 0u, fail };
            memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            SDocumentCompositionDiagnostic diagnostic;
            {
                tests::TMemoryContextScope scope{ &context };
                completed = compose_document(CSchemaDocumentQuery{ types }, CInstanceDocumentQuery{ instances }, CBulkDataDocumentQuery{ bulk }, output, diagnostic);
            }
            if (!completed)
            {
                TEST_EXPECT(ctx, diagnostic.reason != EDocumentCompositionReason::none);
                TEST_EXPECT(ctx, tests::same_baked_document(types, tb) && tests::same_baked_document(instances, ib) && tests::same_baked_document(bulk, bb));
                TEST_EXPECT(ctx, before.is_ready() ? tests::same_baked_document(output, before) : !output.is_ready());
                TEST_EXPECT(ctx, !handle || output.contains(handle));
                TEST_EXPECT(ctx, !diagnostic.types.occurrence || CSchemaDocumentQuery{ types }.contains(diagnostic.types.occurrence));
                TEST_EXPECT(ctx, !diagnostic.instances.occurrence || CInstanceDocumentQuery{ instances }.contains(diagnostic.instances.occurrence));
                TEST_EXPECT(ctx, !diagnostic.bulk_data.occurrence || CBulkDataDocumentQuery{ bulk }.contains(diagnostic.bulk_data.occurrence));
            }
            else
            {
                TEST_EXPECT(ctx, diagnostic.reason == EDocumentCompositionReason::none && output.check_integrity());
                if (&types != &output) types.deallocate();
                if (&instances != &output) instances.deallocate();
                if (&bulk != &output) bulk.deallocate();
                CResolvedSchema resolved;
                SDiagnostic error;
                TEST_EXPECT(ctx, resolved.resolve(output, error));
            }
            output.deallocate();
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed);
    }
}

static CNodeKey first_value(CLiveDocument& document, const bool bulk)
{
    CNodeKey node = document.first_child(document.root());
    for (unsigned i = 0u; i < 3u; ++i) node = document.first_child(node);
    return bulk ? document.first_child(node) : node;
}

static void test_presentation(TTestContext& ctx)
{
    CLiveDocument types;
    CResolvedSchema schema;
    SDiagnostic error;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"Value":{"members":[{"n":{"type":"f64"}},{"f":{"type":"f64"}}]}}}})", types) && schema.resolve(types, error));
    const CIntegerMetadata expected{ EIntegerDomain::signed_value, EIntegerWidth::bits_16,
        EIntegerNotation::hexadecimal_8, EIntegerPrefix::alternate };
    for (const bool bulk : { false, true })
    {
        for (const bool suppress : { false, true })
        {
            for (const bool baked : { false, true })
            {
                CLiveDocument source, prepared, composed;
                TEST_EXPECT(ctx, tests::parse_document(bulk ? R"({"data":{"Value":{"x":{"data":[{"n":128,"f":"nan"}]}}}})" :
                    R"({"instances":{"Value":{"x":{"declaration":{"n":128,"f":"nan"}}}}})", source));
                const CNodeKey number = source.first_child(first_value(source, bulk));
                const CNodeKey string = source.next_sibling(number);
                const CNodeKey replacement = source.create_signed_integer(128, expected);
                TEST_EXPECT(ctx, replacement && source.erase_payload(number) && source.attach_payload(number, replacement));
                TEST_EXPECT(ctx, source.set_newline_escaping_suppressed(string, suppress));
                CBakedDocumentBlock block;
                TEST_EXPECT(ctx, document_translation::bake(source, block));
                SInstancePreparationDiagnostic instances_error;
                SBulkDataPreparationDiagnostic bulk_error;
                TEST_EXPECT(ctx, bulk ? prepare_bulk_data_document((baked ? CBulkDataDocumentQuery{ block.document() } : CBulkDataDocumentQuery{ source }), schema, prepared, bulk_error) :
                    prepare_instances_document((baked ? CInstanceDocumentQuery{ block.document() } : CInstanceDocumentQuery{ source }), schema, prepared, instances_error));
                SDocumentCompositionDiagnostic composition_error;
                TEST_EXPECT(ctx, bulk ? compose_document(CSchemaDocumentQuery{ types }, CBulkDataDocumentQuery{ prepared }, composed, composition_error) :
                    compose_document(CSchemaDocumentQuery{ types }, CInstanceDocumentQuery{ prepared }, composed, composition_error));
                for (CLiveDocument* const result : { &prepared, &composed })
                {
                    CNodeKey node = result->object_child(result->root(), CStringView{ bulk ? "data" : "instances" });
                    for (unsigned i = 0u; i < (bulk ? 5u : 4u); ++i) node = result->first_child(node);
                    CIntegerMetadata actual;
                    TEST_EXPECT(ctx, result->integer_metadata(node, actual));
                    TEST_EXPECT(ctx, actual.domain == expected.domain && actual.width == expected.width &&
                        actual.notation == expected.notation && actual.prefix == expected.prefix);
                    TEST_EXPECT(ctx, result->suppresses_newline_escaping(result->next_sibling(node)) == suppress);
                }
            }
        }
    }
}

static void test_depth(TTestContext& ctx)
{
    std::string text = R"({"types":{"structures":{"T0":{"members":[]})";
    for (unsigned i = 1u; i <= 260u; ++i)
    {
        text += ",\"T" + std::to_string(i) + "\":{\"members\":[{\"v\":{\"type\":\"T" + std::to_string(i - 1u) + "\"}}]}";
    }
    text += "}}}";
    CLiveDocument types;
    CResolvedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, tests::parse_document(text, types) && schema.resolve(types, schema_error));
    for (const unsigned depth : { 0u, 252u, 253u, 256u })
    {
        CLiveDocument source;
        TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"T260":{"x":{"declaration":{}}}}})", source));
        CNodeKey root = first_value(source, false);
        for (unsigned i = 0u; i < depth; ++i)
        {
            const CNodeKey child = source.create_object(CStringView{ "v" });
            TEST_EXPECT(ctx, child && source.append_child(root, child).succeeded());
            root = child;
        }
        CBakedDocumentBlock baked;
        TEST_EXPECT(ctx, document_translation::bake(source, baked));
        for (const bool packed : { false, true })
        {
            const CInstanceDocumentQuery query = packed ? CInstanceDocumentQuery{ baked.document() } : CInstanceDocumentQuery{ source };
            CLiveDocument output;
            SInstancePreparationDiagnostic error;
            TEST_EXPECT(ctx, prepare_instances_document(query, schema, output, error) == (depth <= 252u));
            if (depth > 252u) TEST_EXPECT(ctx, error.reason == EDataPreparationReason::storage_limit && !output.is_ready());
            SDocumentCompositionDiagnostic composition_error;
            TEST_EXPECT(ctx, compose_document(CSchemaDocumentQuery{ types }, query, output, composition_error) == (depth <= 252u));
            if (depth > 252u) TEST_EXPECT(ctx, composition_error.reason == EDocumentCompositionReason::storage_limit && !output.is_ready());
        }
    }
    //  Implicit construction may still fail later: composition never expands defaults.
    const CByteConstView empty;
    expect_preparation<false>(ctx, schema, R"({"stripped":true,"instances":{"T260":{"x":{"locator":{"offset":0,"valid":true}}}}})",
        &empty, false, {}, EInstanceLoadReason::unrepresentable_value);
    for (const unsigned depth : { 126u, 127u })
    {
        CLiveDocument source, output;
        TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"T0":{"base":{}}}})", source));
        CNodeKey entry = source.first_child(source.first_child(source.first_child(source.root())));
        for (unsigned i = 0u; i < depth; ++i)
        {
            const CNodeKey children = source.create_object(CStringView{ "specialisation" });
            const CNodeKey child = source.create_object(CStringView{ "child" });
            TEST_EXPECT(ctx, source.append_child(entry, children).succeeded() && source.append_child(children, child).succeeded());
            entry = child;
        }
        SInstancePreparationDiagnostic diagnostic;
        TEST_EXPECT(ctx, prepare_instances_document(CInstanceDocumentQuery{ source }, schema, output, diagnostic) == (depth == 126u));
        if (depth == 127u) TEST_EXPECT(ctx, diagnostic.reason == EDataPreparationReason::storage_limit);
    }
}


static void test_preallocation(TTestContext& ctx)
{
    using document_translation::detail::CDocumentConstruction;
    constexpr unsigned count = 1100u;
    std::string text = "{\"section\":{";
    std::size_t name_bytes{}, value_bytes{};
    for (unsigned i = 0u; i < count; ++i)
    {
        const std::string name = "name" + std::to_string(i), value = "value" + std::to_string(i);
        name_bytes += name.size() + 2u;
        value_bytes += value.size() + 2u;
        if (i) text += ',';
        text += '"' + name + "\":[\"" + value + "\",\"" + value + "\",\"\",{},[]]";
    }
    text += "}}";
    CLiveDocument source;
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, tests::parse_document(text, source) && document_translation::bake(source, baked));
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CLiveDocument target;
        TEST_EXPECT(ctx, !CDocumentConstruction::initialise_strings(target, true, count, name_bytes));
        TEST_EXPECT(ctx, target.initialise());
        TEST_EXPECT(ctx, CDocumentConstruction::initialise_strings(target, true, count, name_bytes));
        TEST_EXPECT(ctx, CDocumentConstruction::initialise_strings(target, false, count, value_bytes));
        //  An already initialised but still empty domain must not acquire a second sentinel.
        TEST_EXPECT(ctx, !CDocumentConstruction::initialise_strings(target, true, count, name_bytes));
        const auto attempts = fixture.attempts;
        for (unsigned i = 0u; i < count; ++i)
        {
            const std::string name = "name" + std::to_string(i), value = "value" + std::to_string(i);
            const auto name_id = CDocumentConstruction::import_name(target, CStringView{ name.data(), name.size() });
            const auto value_id = CDocumentConstruction::import_string(target, CStringView{ value.data(), value.size() });
            TEST_EXPECT(ctx, name_id && value_id);
            TEST_EXPECT(ctx, CDocumentConstruction::import_name(target, CStringView{ name.data(), name.size() }) == name_id);
            TEST_EXPECT(ctx, CDocumentConstruction::import_string(target, CStringView{ value.data(), value.size() }) == value_id);
        }
        TEST_EXPECT(ctx, CDocumentConstruction::import_name(target, CStringView{ "" }).is_empty());
        TEST_EXPECT(ctx, CDocumentConstruction::import_string(target, CStringView{ "" }).is_empty());
        TEST_EXPECT(ctx, fixture.attempts == attempts);
        TEST_EXPECT(ctx, target.check_integrity());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    for (const bool use_baked : { false, true })
    {
        tests::TMemoryContextScope scope{ &context };
        const CDocumentRead read = use_baked ? CDocumentRead{ baked.document() } : CDocumentRead{ source };
        const auto section = read.first_child(read.root());
        CLiveDocument target;
        detail::CDocumentStrings strings;
        detail::SDocumentBuildDiagnostic error;
        TEST_EXPECT(ctx, strings.collect(read, section, 0u) && strings.initialise(target, 2u) && strings.preload(target));
        const auto attempts = fixture.attempts;
        const auto copied = detail::copy_document_subtree(read, section, target, 0u, error);
        TEST_EXPECT(ctx, copied && CDocumentConstruction::append(target, target.root(), copied));
        TEST_EXPECT(ctx, fixture.attempts == attempts);
        TEST_EXPECT(ctx, target.check_integrity());
        const CDocumentRead result{ target };
        TEST_EXPECT(ctx, tests::equal_document_values(read, read.root(), result, result.root()));
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_scaling(TTestContext& ctx, const CResolvedSchema& schema)
{
    //  Unselected contents must not add traversal, strings or allocation work.
    std::size_t baseline_attempts{}, baseline_bytes{};
    for (const unsigned count : { 0u, 4096u })
    {
        std::string text = R"({"instances":{"Pair":{"x":{"declaration":{"a":7}}}},"types":{)";
        for (unsigned i = 0u; i < count; ++i)
        {
            if (i) text += ',';
            text += "\"unrelated" + std::to_string(i) + "\":\"ignored" + std::to_string(i) + '"';
        }
        text += "}}";
        CLiveDocument source, output;
        TEST_EXPECT(ctx, tests::parse_document(text, source));
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        SInstancePreparationDiagnostic error;
        {
            tests::TMemoryContextScope scope{ &context };
            TEST_EXPECT(ctx, prepare_instances_document(CInstanceDocumentQuery{ source }, schema, output, error));
        }
        if (!count)
        {
            baseline_attempts = fixture.attempts;
            baseline_bytes = fixture.bytes;
        }
        else TEST_EXPECT(ctx, fixture.attempts == baseline_attempts && fixture.bytes == baseline_bytes);
        output.deallocate();
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    //  Retained sparse input must not allocate binary/default scratch.
    for (const unsigned count : { 1u, 100000000u })
    {
        CLiveDocument types, source, output;
        CResolvedSchema large;
        SDiagnostic schema_error;
        TEST_EXPECT(ctx, tests::parse_document("{\"types\":{\"structures\":{\"Large\":{\"members\":[{\"v\":{\"type\":{\"element\":\"u8\",\"count\":" +
            std::to_string(count) + "}}}]}}}}", types) && large.resolve(types, schema_error));
        TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"Large":{"x":{"declaration":{"v":[]}}}}})", source));
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        SInstancePreparationDiagnostic error;
        {
            tests::TMemoryContextScope scope{ &context };
            TEST_EXPECT(ctx, prepare_instances_document(CInstanceDocumentQuery{ source }, large, output, error));
        }
        if (count == 1u)
        {
            baseline_attempts = fixture.attempts;
            baseline_bytes = fixture.bytes;
        }
        else TEST_EXPECT(ctx, fixture.attempts == baseline_attempts && fixture.bytes == baseline_bytes);
        output.deallocate();
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    CLiveDocument types;
    TEST_EXPECT(ctx, tests::parse_document(k_types, types));
    for (const unsigned count : { 32u, 128u, 512u, 2048u })
    {
        CByteBuffer payload;
        TEST_EXPECT(ctx, payload.allocate((count * 2u), k_max_alignment) && payload.set_size(count * 2u));
        std::string text = R"({"data":{"Array":{)";
        for (unsigned i = 0u; i < count; ++i)
        {
            if (i) text += ',';
            text += "\"n" + std::to_string(count - i) + "\":{\"locator\":{\"offset\":" + std::to_string(i * 2u) + ",\"valid\":true,\"count\":1}}";
            payload.data()[i * 2u] = 0u;
            payload.data()[(i * 2u) + 1u] = 1u;
        }
        text += "}}}";
        CLiveDocument source, output, composed;
        TEST_EXPECT(ctx, tests::parse_document(text, source));
        std::size_t preparation_attempts{}, preparation_bytes{};
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        platform::system::CPerfCounter timer;
        (void)timer.update();
        {
            tests::TMemoryContextScope scope{ &context };
            SBulkDataPreparationDiagnostic error;
            TEST_EXPECT(ctx, prepare_bulk_data_document(CBulkDataDocumentQuery{ source }, schema, payload.const_view(), false, output, error));
            preparation_attempts = fixture.attempts;
            preparation_bytes = fixture.bytes;
            SDocumentCompositionDiagnostic composition_error;
            TEST_EXPECT(ctx, compose_document(CSchemaDocumentQuery{ types }, CBulkDataDocumentQuery{ output }, composed, composition_error));
        }
        platform::system::CPerfCountConversion conversion;
        std::uint64_t micros{};
        (void)conversion.init();
        (void)conversion.to_microseconds_u64(timer.query_delta(), micros);
        std::cout << "Document boundaries scaling: " << count << " reconstructed entries, " << fixture.attempts <<
            " allocations, " << fixture.bytes << " requested bytes, " << micros << " us\n";
        std::cout << "  Preparation: " << preparation_attempts << " allocations, " << preparation_bytes << " requested bytes; composition: " <<
            (fixture.attempts - preparation_attempts) << " allocations, " << (fixture.bytes - preparation_bytes) << " requested bytes\n";
        //  Exclude a metadata worklist allocation per entry. The existing
        //  containers cap growth increments, so backing allocation is not
        //  logarithmic and requested bytes are recorded separately above.
        TEST_EXPECT(ctx, fixture.attempts < ((count / 4u) + 192u));
        output.deallocate();
        composed.deallocate();
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

static void test_workflow(TTestContext& ctx)
{
    CLiveDocument original_types, added_types, merged_types, retained, stripped, bulk, a, b, records, merged_instances, merged_bulk, complete;
    CResolvedSchema original, combined;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"One":{"members":[{"v":{"type":"u32"}}]}}}})", original_types) && original.resolve(original_types, schema_error));
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"One":{"members":[{"v":{"type":"u32","default":7}}]}}}})", added_types));
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"One":{"adopt":{},"explicit":{"declaration":0}}}})", retained));
    TEST_EXPECT(ctx, tests::parse_document(R"({"stripped":true,"instances":{"One":{"base":{"locator":{"offset":0,"valid":true},"specialisation":{"child":{"locator":{"offset":4,"valid":true}}}}}}})", stripped));
    TEST_EXPECT(ctx, tests::parse_document(R"({"data":{"One":{"records":{"locator":{"offset":0,"valid":true,"count":2}}}}})", bulk));
    CByteBuffer payload;
    TEST_EXPECT(ctx, payload.allocate(8u, k_max_alignment) && payload.set_size(8u));
    const std::uint32_t snapshots[]{ 11u, 0u };
    std::memcpy(payload.data(), snapshots, sizeof(snapshots));
    SInstancePreparationDiagnostic preparation_error;
    SBulkDataPreparationDiagnostic bulk_error;
    TEST_EXPECT(ctx, prepare_instances_document(CInstanceDocumentQuery{ retained }, original, a, preparation_error));
    TEST_EXPECT(ctx, prepare_instances_document(CInstanceDocumentQuery{ stripped }, original, payload.const_view(), true, b, preparation_error));
    TEST_EXPECT(ctx, prepare_bulk_data_document(CBulkDataDocumentQuery{ bulk }, original, payload.const_view(), true, records, bulk_error));
    SSchemaMergeDiagnostic types_error;
    TEST_EXPECT(ctx, merge_type_schemas(CSchemaDocumentQuery{ original_types }, CSchemaDocumentQuery{ added_types }, merged_types, types_error) && combined.resolve(merged_types, schema_error));
    SInstanceMergeDiagnostic instance_merge_error;
    SBulkDataMergeDiagnostic bulk_merge_error;
    TEST_EXPECT(ctx, merge_instances(CInstanceDocumentQuery{ a }, CInstanceDocumentQuery{ b }, combined, merged_instances, instance_merge_error));
    TEST_EXPECT(ctx, merge_bulk_data(CBulkDataDocumentQuery{ records }, CBulkDataDocumentQuery{ records }, combined, merged_bulk, bulk_merge_error));
    SDocumentCompositionDiagnostic composition_error;
    TEST_EXPECT(ctx, compose_document(CSchemaDocumentQuery{ merged_types }, CInstanceDocumentQuery{ merged_instances }, CBulkDataDocumentQuery{ merged_bulk }, complete, composition_error));
    TEST_EXPECT(ctx, prepare_instances_document(CInstanceDocumentQuery{ complete }, combined, a, preparation_error));
    TEST_EXPECT(ctx, prepare_bulk_data_document(CBulkDataDocumentQuery{ complete }, combined, records, bulk_error));
    const CDocumentRead read{ a };
    const auto group = read.object_child(read.object_child(read.root(), CStringView{ "instances" }), CStringView{ "One" });
    const CSchemaIndex type = combined.find_type(CStringView{ "One" });
    detail::SValueDiagnostic value_error;
    std::uint32_t bytes{};
    for (const char* const name : { "adopt", "explicit", "base" })
    {
        const auto entry = read.object_child(group, CStringView{ name });
        TEST_EXPECT(ctx, detail::construct_value(combined, read, type, read.object_child(entry, CStringView{ "declaration" }),
            reinterpret_cast<std::uint8_t*>(&bytes), sizeof(bytes), detail::EConstructionMode::instance, value_error));
        TEST_EXPECT(ctx, bytes == ((std::strcmp(name, "adopt") == 0) ? 7u : ((std::strcmp(name, "explicit") == 0) ? 0u : 11u)));
    }
    const auto base = read.object_child(group, CStringView{ "base" });
    const auto child = read.first_child(read.object_child(base, CStringView{ "specialisation" }));
    TEST_EXPECT(ctx, detail::construct_value(combined, read, type, read.object_child(child, CStringView{ "declaration" }),
        reinterpret_cast<std::uint8_t*>(&bytes), sizeof(bytes), detail::EConstructionMode::instance, value_error));
    TEST_EXPECT(ctx, bytes == 0u);
    const CDocumentRead bulk_read{ records };
    SOccurrence record = bulk_read.root();
    for (unsigned i = 0u; i < 5u; ++i) record = bulk_read.first_child(record);
    for (unsigned i = 0u; i < 2u; ++i)
    {
        TEST_EXPECT(ctx, detail::construct_value(combined, bulk_read, type, record, reinterpret_cast<std::uint8_t*>(&bytes),
            sizeof(bytes), detail::EConstructionMode::complete_bulk_data, value_error));
        TEST_EXPECT(ctx, bytes == snapshots[i]);
        record = bulk_read.next_sibling(record);
    }
}


static void test_reconstructed_depth(TTestContext& ctx)
{
    std::string text = R"({"types":{"structures":{"T0":{"members":[{"v":{"type":"u8"}}]})";
    for (unsigned i = 1u; i <= 252u; ++i)
    {
        text += ",\"T" + std::to_string(i) + "\":{\"members\":[{\"v\":{\"type\":\"T" + std::to_string(i - 1u) + "\"}}]}";
    }
    text += "}}}";
    CLiveDocument types;
    CResolvedSchema schema;
    SDiagnostic schema_error;
    TEST_EXPECT(ctx, tests::parse_document(text, types) && schema.resolve(types, schema_error));
    CByteBuffer payload;
    TEST_EXPECT(ctx, payload.allocate(1u, k_max_alignment) && payload.set_size(1u));
    payload.data()[0] = 1u;
    for (const unsigned depth : { 250u, 251u, 252u })
    {
        for (const bool bulk : { false, true })
        {
            CLiveDocument source, output;
            TEST_EXPECT(ctx, tests::parse_document(std::string{ bulk ? R"({"data":{)" : R"({"stripped":true,"instances":{)" } +
                "\"T" + std::to_string(depth) + R"(":{"x":{"locator":{"offset":0,"valid":true,"count":1}}}}})", source));
            CBakedDocumentBlock block;
            TEST_EXPECT(ctx, document_translation::bake(source, block));
            for (const bool baked : { false, true })
            {
                SInstancePreparationDiagnostic instances_error;
                SBulkDataPreparationDiagnostic bulk_error;
                const bool accepted = bulk ? prepare_bulk_data_document((baked ? CBulkDataDocumentQuery{ block.document() } : CBulkDataDocumentQuery{ source }),
                    schema, payload.const_view(), false, output, bulk_error) :
                    prepare_instances_document((baked ? CInstanceDocumentQuery{ block.document() } : CInstanceDocumentQuery{ source }),
                        schema, payload.const_view(), false, output, instances_error);
                TEST_EXPECT(ctx, accepted == (depth <= (bulk ? 250u : 251u)));
                if (accepted) TEST_EXPECT(ctx, output.maximum_depth() == (depth + (bulk ? 6u : 5u)));
                else TEST_EXPECT(ctx, (bulk ? bulk_error.reason : instances_error.reason) == EDataPreparationReason::storage_limit);
            }
        }
    }
}

}   // namespace

void run_document_boundary_cases(tests::TTestContext& ctx)
{
    CLiveDocument types;
    CResolvedSchema schema;
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, tests::parse_document(k_types, types) && schema.resolve(types, diagnostic));
    test_preparation(ctx, schema);
    test_composition(ctx);
    test_preparation_failures<false>(ctx, schema);
    test_preparation_failures<true>(ctx, schema);
    test_composition_failures(ctx);
    test_presentation(ctx);
    test_depth(ctx);
    test_reconstructed_depth(ctx);
    test_preallocation(ctx);
    test_scaling(ctx, schema);
    test_workflow(ctx);
}

