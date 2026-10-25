
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaModel_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Aggregate authoring, deferred validation and selection-preserving runtime cases.

#include "tests/test_suites/SchemaModel_test_cases.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "schema/schema.hpp"
#include "schema/instance_selection.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/document_construction.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "containers/TInstance.hpp"

#include <string>
#include <utility>

namespace
{

using namespace schema;

bool load(const char* text, CSchema& result)
{
    CLiveDocument document;
    SSchemaDiagnostic error;
    return tests::parse_document(text, document) && result.try_adopt(std::move(document), error);
}

std::string saved(const CSchema& source)
{
    CBakedDocumentBlock block;
    SSchemaDiagnostic error;
    if (!source.save(block, error)) return {};
    const auto text = document_writer::write(block.document());
    return text.report.succeeded() ? std::string{ reinterpret_cast<const char*>(text.output.data()), text.report.logical_text_byte_size } : std::string{};
}

void authoring(tests::TTestContext& ctx)
{
    CSchema data;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"instances":{"i32":{"base":{"declaration":7}}}})", data));
    TEST_EXPECT(ctx, !data.validate(error) && error.reason == ESchemaReason::missing_types);
    const std::string text = saved(data);
    TEST_EXPECT(ctx, !text.empty() && text.find("types") == std::string::npos);
    CSchema copied;
    TEST_EXPECT(ctx, copied.copy_from(data.document_query(), error) && saved(copied) == text);
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, copied.save(block, error));
    const auto original_bytes = block.buffer_bytes();
    const std::string snapshot{ reinterpret_cast<const char*>(original_bytes.data()), original_bytes.size() };
    CSchema replacement;
    TEST_EXPECT(ctx, load(R"({"types":{}})", replacement));
    TEST_EXPECT(ctx, !replacement.save(block, error) && error.stage == ESchemaStage::saving && error.reason == ESchemaReason::invalid_source);
    TEST_EXPECT(ctx, block.buffer_bytes().data() == original_bytes.data() && block.buffer_bytes().size() == snapshot.size() &&
        std::memcmp(block.buffer_bytes().data(), snapshot.data(), snapshot.size()) == 0);
    TEST_EXPECT(ctx, replacement.validate(error));
    CSchema loaded;
    TEST_EXPECT(ctx, loaded.copy_from(CSchemaDocumentQuery{ block.document() }, error) && saved(loaded) == text);

    CSchema empty, definitions, combined;
    TEST_EXPECT(ctx, load("{}", empty) && !empty.validate(error) && error.reason == ESchemaReason::missing_types);
    TEST_EXPECT(ctx, load(R"({"types":{}})", definitions) && definitions.validate(error));
    TEST_EXPECT(ctx, combined.merge_from(definitions.document_query(), data.document_query(), error) && combined.validate(error));
    TEST_EXPECT(ctx, !combined.document_query().object_child(combined.document_query().root(), CStringView{ "data" }));
    TEST_EXPECT(ctx, empty.merge_from(empty.document_query(), empty.document_query(), error) && empty.document_query().child_count(empty.document_query().root()) == 0u);

    CSchema incomplete;
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"Broken":{"members":[{"x":{"type":"Missing"}}]}}}})", incomplete));
    TEST_EXPECT(ctx, !saved(incomplete).empty() && !incomplete.validate(error) && error.reason == ESchemaReason::invalid_types);
    TEST_EXPECT(ctx, load(R"({"types":{},"data":{"u32":{"records":{}}}})", incomplete));
    TEST_EXPECT(ctx, !saved(incomplete).empty() && !incomplete.validate(error) && error.stage == ESchemaStage::bulk_data);

    CLiveDocument invalid;
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"i32":{"base":{"locator":{"offset":0,"valid":true}}}}})", invalid));
    const std::string before = saved(combined);
    TEST_EXPECT(ctx, !combined.try_adopt(std::move(invalid), error) && error.reason == ESchemaReason::unknown_property && invalid.is_ready());
    TEST_EXPECT(ctx, saved(combined) == before);
    TEST_EXPECT(ctx, combined.try_adopt(std::move(combined.edit()), error) && saved(combined) == before);

    CLiveDocument deep;
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"i32":{"x":{"declaration":[]}}}})", deep));
    auto leaf = deep.object_child(deep.object_child(deep.object_child(deep.object_child(deep.root(), CStringView{ "instances" }),
        CStringView{ "i32" }), CStringView{ "x" }), CStringView{ "declaration" });
    for (unsigned i = 0u; i < 256u; ++i)
    {
        const auto child = deep.create_array();
        TEST_EXPECT(ctx, deep.append_child(leaf, child).succeeded());
        leaf = child;
    }
    TEST_EXPECT(ctx, !combined.try_adopt(std::move(deep), error) && error.reason == ESchemaReason::storage_limit && saved(combined) == before);
}

void merging(tests::TTestContext& ctx)
{
    CSchema a, b, result;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"S":{"members":[{"x":{"type":"Missing"}}]}}}})", a));
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"S":{"decorations":{"stage":"vertex"},"members":[{"x":{"type":"Missing","default":10}}]}}}})", b));
    TEST_EXPECT(ctx, result.merge_from(a.document_query(), b.document_query(), error));
    TEST_EXPECT(ctx, saved(result).find("stage") != std::string::npos && !result.validate(error));
    const std::string before = saved(result);
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"S":{"members":[{"y":{"type":"Missing"}}]}}}})", b));
    TEST_EXPECT(ctx, !result.merge_from(a.document_query(), b.document_query(), error) && error.reason == ESchemaReason::incompatible_definition && error.first && error.second);
    TEST_EXPECT(ctx, saved(result) == before);
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"S":{}}}})", a));
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"S":{"members":[]}}}})", b));
    TEST_EXPECT(ctx, !result.merge_from(a.document_query(), b.document_query(), error));
    TEST_EXPECT(ctx, load(R"({"types":{"associations":{"S":{"target":null}}}})", b));
    TEST_EXPECT(ctx, !result.merge_from(a.document_query(), b.document_query(), error));

    TEST_EXPECT(ctx, load(R"({"instances":{"S":{"base":{"declaration":{"samples":[null,7]},"specialisation":{"child":{"declaration":{"samples":[null,9]}}}}}}})", a));
    TEST_EXPECT(ctx, load(R"({"instances":{"S":{"base":{"declaration":{"samples":[3]},"specialisation":{"child":{"declaration":{"samples":[4]}}}}}}})", b));
    TEST_EXPECT(ctx, a.merge_from(a.document_query(), b.document_query(), error));
    const auto query = a.document_query();
    const auto instances = query.object_child(query.root(), CStringView{ "instances" });
    const auto group = query.object_child(instances, CStringView{ "S" });
    const auto base = query.object_child(group, CStringView{ "base" });
    const auto declaration = query.object_child(base, CStringView{ "declaration" });
    const auto samples = query.object_child(declaration, CStringView{ "samples" });
    std::uint64_t first{}, second{};
    TEST_EXPECT(ctx, query.child_count(samples) == 2u && query.unsigned_integer_value(query.array_at(samples, 0u), first) && first == 3 &&
        query.unsigned_integer_value(query.array_at(samples, 1u), second) && second == 7);
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"S":{"members":[{"samples":{"type":{"element":"i32","count":2}}}]}}}})", b));
    TEST_EXPECT(ctx, result.merge_from(a.document_query(), b.document_query(), error) && result.validate(error));

    TEST_EXPECT(ctx, load(R"({"types":{"interfaces":{"I":{"members":[{"x":{"type":"Missing"}}]}}}})", a));
    for (const char* count : { "1", "+1", "1.0" })
    {
        const std::string text = std::string{ R"({"types":{"interfaces":{"I":{"members":[{"x":{"type":"Missing","count":)" } + count + "}}]}}}}";
        TEST_EXPECT(ctx, load(text.c_str(), b));
        TEST_EXPECT(ctx, result.merge_from(a.document_query(), b.document_query(), error));
        CBakedDocumentBlock baked;
        TEST_EXPECT(ctx, b.save(baked, error) && result.merge_from(a.document_query(), CSchemaDocumentQuery{ baked.document() }, error));
    }
}

void selections(tests::TTestContext& ctx)
{
    using namespace schema::detail;
    CLiveDocument source, values, masks;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{
        "Pair":{"members":[{"a":{"type":"i32","default":1}},{"b":{"type":"i32","default":2}}]},
        "Outer":{"members":[{"inner":{"type":"Pair"}},{"samples":{"type":{"element":"i32","count":3}}},{"flag":{"type":"b8"}}],"detail":{"size":32,"alignment":16}}
    }}})", source));
    CResolvedTypes types;
    SDiagnostic error;
    TEST_EXPECT(ctx, types.resolve(source, error));
    const auto type = types.find_type(CStringView{ "Outer" });
    TEST_EXPECT(ctx, tests::parse_document(R"({"v":[{"b":7},[null,9],false]})", values) && masks.initialise());
    const CDocumentRead literals{ values }, output{ masks };
    const auto value = literals.object_child(literals.root(), CStringView{ "v" });
    SValueDiagnostic diagnostic;
    alignas(128) std::uint8_t bytes[32]{}, baseline[32]{}, changed[32]{};
    TEST_EXPECT(ctx, construct_value(types, literals, type, value, bytes, sizeof(bytes), EConstructionMode::instance, diagnostic));
    TEST_EXPECT(ctx, construct_value(types, literals, type, {}, baseline, sizeof(baseline), EConstructionMode::instance, diagnostic));
    const auto mask = strip_selection(masks, types, type, { &literals, value }, CStringView{ "selection" }, diagnostic);
    TEST_EXPECT(ctx, mask && diagnostic.reason == EReason::none);
    const SSelectionView selected{ &output, SOccurrence{ mask } };
    TEST_EXPECT(ctx, validate_selection(types, type, selected, diagnostic));
    const auto inner = output.object_child(SOccurrence{ mask }, CStringView{ "inner" });
    TEST_EXPECT(ctx, output.object_child(inner, CStringView{ "b" }) && !output.object_child(inner, CStringView{ "a" }));
    const auto samples = output.object_child(SOccurrence{ mask }, CStringView{ "samples" });
    TEST_EXPECT(ctx, output.child_count(samples) == 2u && inherited_hole(output, output.array_at(samples, 0u)));
    bool matches{};
    TEST_EXPECT(ctx, check_selection(types, type, selected, baseline, bytes, matches, diagnostic) && matches);
    std::memcpy(changed, bytes, sizeof(bytes));
    std::memset(changed + 21u, 0xA5, 11u);
    write_scalar_bits(baseline, 4u, 30u);
    write_scalar_bits(baseline + 4u, 4u, 40u);
    TEST_EXPECT(ctx, inherit_selection(types, type, selected, SSelectionView::complete(), baseline, changed, diagnostic));
    TEST_EXPECT(ctx, read_scalar_bits(changed, 4u) == 30u && read_scalar_bits(changed + 4u, 4u) == 7u && changed[31] == 0xA5);
    const auto compensated = extend_selection(masks, types, type, selected, SSelectionView::complete(), baseline, bytes, false,
        CStringView{ "compensated" }, diagnostic);
    TEST_EXPECT(ctx, compensated && check_selection(types, type, { &output, SOccurrence{ compensated } }, baseline, bytes, matches, diagnostic) && matches);
    const auto exported = export_selection(masks, types, type, selected, bytes, CStringView{ "exported" }, diagnostic);
    TEST_EXPECT(ctx, exported && check_alternative(types, output, type, SOccurrence{ exported }, bytes, sizeof(bytes), bytes, sizeof(bytes), matches, diagnostic) && matches);
    bytes[20] = 2u;
    TEST_EXPECT(ctx, validate_selection(types, type, selected, diagnostic));
    TEST_EXPECT(ctx, !export_selection(masks, types, type, selected, bytes, CStringView{ "bad_export" }, diagnostic) && diagnostic.reason == EReason::invalid_input);
    const auto invalid = masks.create_boolean(true);
    TEST_EXPECT(ctx, !validate_selection(types, type, { &output, SOccurrence{ invalid } }, diagnostic));
}

void instance_cascade(tests::TTestContext& ctx)
{
    CLiveDocument definitions, literals;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"V":{"members":[{"x":{"type":"i32","default":10}},{"y":{"type":"i32"}}],"detail":{"alignment":16,"size":16}}}}})", definitions));
    CLiveTypes types;
    SDiagnostic type_error;
    TEST_EXPECT(ctx, types.try_adopt(std::move(definitions)) && types.resolve(type_error));
    TEST_EXPECT(ctx, tests::parse_document(R"({"equal":{"x":10},"next":30,"partial":{"y":9}})", literals));
    CInstanceDocumentQuery input{ literals };
    CLiveInstances instances;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, instances.initialise(types));
    const auto a = instances.create_base(CStringView{ "V" }, CStringView{ "a" }, input,
        input.object_child(input.root(), CStringView{ "equal" }), error);
    const auto b = instances.create_specialisation(a, CStringView{ "b" }, {}, {}, error);
    const auto c = instances.create_specialisation(b, CStringView{ "c" }, {}, {}, error);
    TEST_EXPECT(ctx, a && b && c);
    SInstanceEntryView av, bv, cv;
    TEST_EXPECT(ctx, instances.entry(a, av) && instances.entry(b, bv) && instances.entry(c, cv));
    const auto* allocation = instances.payload_view().data();
    alignas(128) std::uint8_t replacement[16]{};
    write_scalar_bits(replacement, 4u, 20u);
    write_scalar_bits(replacement + 4u, 4u, 7u);
    std::memset(replacement + 8u, 0xC3, 8u);
    TEST_EXPECT(ctx, instances.overwrite(a, CByteConstView{ replacement, sizeof(replacement) }, EInstanceDescendantPolicy::preserve, error));
    TEST_EXPECT(ctx, instances.entry(a, av) && instances.entry(b, bv) && instances.entry(c, cv));
    TEST_EXPECT(ctx, read_scalar_bits(av.bytes, 4u) == 20u && read_scalar_bits(bv.bytes, 4u) == 10u && read_scalar_bits(cv.bytes, 4u) == 10u);
    TEST_EXPECT(ctx, bv.declaration && !cv.declaration && av.bytes[15] == 0xC3 && instances.payload_view().data() == allocation);
    const SInstanceSelectionStep x{ EInstanceSelectionStepKind::member, CStringView{ "x" }, 0u };
    TEST_EXPECT(ctx, instances.specialise(a, &x, 1u, input, input.object_child(input.root(), CStringView{ "next" }), EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(ctx, instances.entry(a, av) && instances.entry(b, bv) && read_scalar_bits(av.bytes, 4u) == 30u && read_scalar_bits(bv.bytes, 4u) == 10u);
    TEST_EXPECT(ctx, instances.remove_selection(b, &x, 1u, EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(ctx, instances.entry(b, bv) && instances.entry(c, cv) && read_scalar_bits(bv.bytes, 4u) == 30u && read_scalar_bits(cv.bytes, 4u) == 30u);
    TEST_EXPECT(ctx, instances.specialise(a, nullptr, 0u, input, input.object_child(input.root(), CStringView{ "partial" }), EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(ctx, instances.entry(a, av) && read_scalar_bits(av.bytes, 4u) == 30u && read_scalar_bits(av.bytes + 4u, 4u) == 9u && av.bytes[15] == 0xC3);

    CInstanceEditToken token;
    SMutableInstanceEntryView writable;
    TEST_EXPECT(ctx, instances.begin_edit(a, token, writable, error) && instances.pending_edit() && !instances.loaded_ready());
    write_scalar_bits(writable.bytes.data(), 4u, 40u);
    TEST_EXPECT(ctx, !instances.overwrite(a, CByteConstView{ replacement, sizeof(replacement) }, EInstanceDescendantPolicy::preserve, error));
    TEST_EXPECT(ctx, !instances.complete_edit(token, static_cast<EInstanceDescendantPolicy>(255u), error) && instances.pending_edit());
    TEST_EXPECT(ctx, instances.complete_edit(token, EInstanceDescendantPolicy::propagate, error) && !instances.pending_edit());
    TEST_EXPECT(ctx, instances.entry(c, cv) && read_scalar_bits(cv.bytes, 4u) == 40u);
    TEST_EXPECT(ctx, !instances.complete_edit(token, EInstanceDescendantPolicy::preserve, error));
    TEST_EXPECT(ctx, instances.remove_selection(a, nullptr, 0u, EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(ctx, instances.entry(a, av) && instances.entry(c, cv) && read_scalar_bits(av.bytes, 4u) == 10u && read_scalar_bits(cv.bytes, 4u) == 10u && av.bytes[15] == 0xC3);
    instances.clear();
    TEST_EXPECT(ctx, instances.initialise(types));
    const auto fresh = instances.create_base(CStringView{ "V" }, CStringView{ "a" }, {}, {}, error);
    CInstanceEditToken new_token;
    TEST_EXPECT(ctx, instances.begin_edit(fresh, new_token, writable, error));
    TEST_EXPECT(ctx, !instances.complete_edit(token, EInstanceDescendantPolicy::propagate, error) && instances.pending_edit());
    TEST_EXPECT(ctx, instances.complete_edit(new_token, EInstanceDescendantPolicy::propagate, error));

    //  Reusing an owner's address must not make an old token current again.
    auto reused = TInstance<CLiveInstances>::create();
    TEST_EXPECT(ctx, reused && reused->initialise(types));
    if (!reused) return;
    const auto* reused_address = &*reused;
    auto reused_base = reused->create_base(CStringView{ "V" }, CStringView{ "base" }, {}, {}, error);
    CInstanceEditToken old_address_token, current_address_token;
    TEST_EXPECT(ctx, reused->begin_edit(reused_base, old_address_token, writable, error));
    TEST_EXPECT(ctx, reused.emplace() && &*reused == reused_address && reused->initialise(types));
    reused_base = reused->create_base(CStringView{ "V" }, CStringView{ "base" }, {}, {}, error);
    TEST_EXPECT(ctx, reused->begin_edit(reused_base, current_address_token, writable, error) &&
        !reused->complete_edit(old_address_token, EInstanceDescendantPolicy::propagate, error) && reused->pending_edit());
    TEST_EXPECT(ctx, reused->complete_edit(current_address_token, EInstanceDescendantPolicy::propagate, error));
}

void sparse_empty_roundtrip(tests::TTestContext& ctx)
{
    CSchema authored;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]},
        "Rows":{"members":[{"rows":{"type":{"element":"Pair","count":4}}}]}}},
        "instances":{"Rows":{"base":{"declaration":{"rows":[null,{},null,{"a":7}]}}}}})", authored));
    CLiveTypes types;
    CLiveInstances instances;
    CLiveBulkData bulk;
    TEST_EXPECT(ctx, authored.materialise(types, instances, bulk, error));
    const auto base = instances.find_base(CStringView{ "Rows" }, CStringView{ "base" });
    const SInstanceSelectionStep path[] = {
        { EInstanceSelectionStepKind::member, CStringView{ "rows" } },
        { EInstanceSelectionStepKind::element, {}, 3u }
    };
    SInstanceDiagnostic edit_error;
    TEST_EXPECT(ctx, instances.remove_selection(base, path, 2u, EInstanceDescendantPolicy::propagate, edit_error));
    CSchema exported, definitions, combined;
    TEST_EXPECT(ctx, exported.import_from(instances, error) && definitions.import_from(types, error) &&
        combined.compose_from(definitions.document_query(), exported.document_query(), {}, error));
    CBakedDataBlock block;
    CBakedInstances baked;
    CLiveInstances promoted;
    TEST_EXPECT(ctx, combined.bake(block, error) && baked.attach_block(block, types, edit_error) && baked.promote(promoted, edit_error));
    const auto query = promoted.document_query();
    SInstanceEntryView view;
    TEST_EXPECT(ctx, promoted.entry(promoted.find_base(CStringView{ "Rows" }, CStringView{ "base" }), view));
    const auto rows = query.object_child(view.declaration, CStringView{ "rows" });
    TEST_EXPECT(ctx, query.child_count(rows) == 2u && query.value_kind(query.array_at(rows, 0u)) == EDocumentValueKind::null_value &&
        query.value_kind(query.array_at(rows, 1u)) == EDocumentValueKind::object && query.child_count(query.array_at(rows, 1u)) == 0u &&
        view.bytes[6] == 0u);
}

void runtime_conversion(tests::TTestContext& ctx)
{
    CSchema source;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"V":{"members":[{"x":{"type":"i32","default":1}}]},"Empty":{"members":[]}}},
        "instances":{"V":{"a":{"declaration":{"x":1},"specialisation":{"b":{}}}},"Empty":{}},
        "data":{"u8":{"bytes":{"data":[3,4]}},"Empty":{"empty":{"data":[{},{}]}}}})", source));
    CLiveTypes types;
    CLiveInstances instances;
    CLiveBulkData bulk;
    TEST_EXPECT(ctx, source.materialise(types, instances, bulk, error) && types.reference_count() == 2u);
    TEST_EXPECT(ctx, instances.find_base(CStringView{ "V" }, CStringView{ "a" }));
    SBulkDataEntryView records;
    TEST_EXPECT(ctx, bulk.entry(bulk.find_entry(CStringView{ "Empty" }, CStringView{ "empty" }), records) && records.count == 2u && records.byte_count == 0u);
    CSchema exported_types, exported_instances, exported_bulk, recombined;
    TEST_EXPECT(ctx, exported_types.import_from(types, error) && exported_instances.import_from(instances, error) && exported_bulk.import_from(bulk, error));
    TEST_EXPECT(ctx, saved(exported_instances).find("types") == std::string::npos && saved(exported_bulk).find("types") == std::string::npos);
    TEST_EXPECT(ctx, !exported_instances.validate(error) && error.reason == ESchemaReason::missing_types);
    TEST_EXPECT(ctx, recombined.merge_from(exported_types.document_query(), exported_instances.document_query(), error) &&
        recombined.merge_from(recombined.document_query(), exported_bulk.document_query(), error) && recombined.validate(error));
    CBakedDataBlock block;
    TEST_EXPECT(ctx, source.bake(block, error) && block.has_types() && block.instance_binary_available() && block.bulk_binary_available());
    const auto query = block.document();
    std::uint64_t version{};
    bool stripped{};
    TEST_EXPECT(ctx, query.unsigned_integer_value(query.object_child(query.root(), CStringView{ "version" }), version) && version == 2u &&
        query.boolean_value(query.object_child(query.root(), CStringView{ "stripped" }), stripped) && stripped);
    CBakedTypes baked_types;
    SDiagnostic type_error;
    TEST_EXPECT(ctx, baked_types.set_block(block) && baked_types.resolve(type_error));
    CBakedInstances baked_instances;
    SInstanceDiagnostic instance_error;
    TEST_EXPECT(ctx, baked_instances.attach_block(block, baked_types, instance_error));
    CSchema exported_baked;
    TEST_EXPECT(ctx, exported_baked.import_from(baked_instances, error) && saved(exported_baked) == saved(exported_instances));
    source.clear();
    TEST_EXPECT(ctx, instances.loaded_ready() && bulk.loaded_ready());

    CSchema missing;
    TEST_EXPECT(ctx, load(R"({"instances":{}})", missing));
    CLiveTypes empty_types;
    CLiveInstances empty_instances;
    CLiveBulkData empty_bulk;
    CBakedDataBlock empty_block;
    TEST_EXPECT(ctx, !missing.materialise(empty_types, empty_instances, empty_bulk, error) && error.reason == ESchemaReason::missing_types);
    TEST_EXPECT(ctx, !missing.bake(empty_block, error) && error.reason == ESchemaReason::missing_types && !empty_block.loaded_ready());
    TEST_EXPECT(ctx, load(R"({"types":{},"instances":{},"data":{}})", missing) && missing.bake(empty_block, error));
    TEST_EXPECT(ctx, empty_block.instance_binary_available() && empty_block.bulk_binary_available() && empty_block.instance_payload_view().size() == 0u);
}

void construction_diagnostics(tests::TTestContext& ctx)
{
    const char* cases[]{
        R"({"types":{"structures":{"S":{"members":[{"x":{"type":"Missing"}}]}}}})",
        R"({"types":{"structures":{"S":{"members":[{"a":{"type":"u32","offset":0}},{"b":{"type":"u32","offset":0}}]}}}})",
        R"({"types":{"structures":{"S":{"members":[{"x":{"type":"u8","default":256}}]}}}})",
        R"({"types":{},"instances":{"u8":{"a":{"declaration":256}}},"data":{"u8":{"r":{"data":[256]}}}})",
        R"({"types":{"structures":{"S":{"members":[{"a":{"type":"u32"}},{"b":{"type":"u32"}}]}}},"data":{"S":{"r":{"data":[{"a":1}]}}}})"
    };
    for (const auto text : cases)
    {
        CSchema source;
        SSchemaDiagnostic expected, actual;
        TEST_EXPECT(ctx, load(text, source) && !source.validate(expected));
        for (const bool bake : { false, true })
        {
            CLiveTypes types;
            CLiveInstances instances;
            CLiveBulkData bulk;
            CBakedDataBlock block;
            TEST_EXPECT(ctx, !(bake ? source.bake(block, actual) : source.materialise(types, instances, bulk, actual)));
            TEST_EXPECT(ctx, actual.stage == expected.stage && actual.reason == expected.reason && actual.first == expected.first);
            TEST_EXPECT(ctx, actual.types.reason == expected.types.reason && actual.types.stage == expected.types.stage &&
                actual.types.occurrence == expected.types.occurrence && actual.types.enclosing_type == expected.types.enclosing_type &&
                actual.types.enclosing_member == expected.types.enclosing_member && actual.types.related == expected.types.related);
            TEST_EXPECT(ctx, actual.types.ranges_available == expected.types.ranges_available && actual.types.range_begin == expected.types.range_begin &&
                actual.types.range_end == expected.types.range_end && actual.types.related_begin == expected.types.related_begin &&
                actual.types.related_end == expected.types.related_end);
            TEST_EXPECT(ctx, actual.instances.reason == expected.instances.reason && actual.instances.occurrence == expected.instances.occurrence &&
                actual.bulk_data.reason == expected.bulk_data.reason && actual.bulk_data.occurrence == expected.bulk_data.occurrence);
            TEST_EXPECT(ctx, !types.document_ready() && !instances.document_ready() && !bulk.document_ready() && !block.loaded_ready());
        }
    }
    //  Empty-string property names are valid and differ from null CStringViews.
    //  Preserve them through both construction paths and runtime lookup.
    for (const auto text : {
        R"({"types":{},"instances":{"u8":{"":{"declaration":1}}}})",
        R"({"types":{},"instances":{"u8":{"base":{"specialisation":{"":{}}}}}})",
        R"({"types":{},"data":{"u8":{"":{"data":[1]}}}})" })
    {
        CSchema source;
        SSchemaDiagnostic error;
        TEST_EXPECT(ctx, load(text, source) && source.validate(error));
        CLiveTypes types;
        CLiveInstances instances;
        CLiveBulkData bulk;
        CBakedDataBlock block;
        TEST_EXPECT(ctx, source.materialise(types, instances, bulk, error) && source.bake(block, error));
        if (instances.document_ready())
        {
            const auto base = instances.find_base(CStringView{ "u8" }, CStringView{ "base" });
            TEST_EXPECT(ctx, base ? instances.find_specialisation(base, CStringView{ "" }).is_valid() :
                instances.find_base(CStringView{ "u8" }, CStringView{ "" }).is_valid());
        }
        else TEST_EXPECT(ctx, bulk.find_entry(CStringView{ "u8" }, CStringView{ "" }));
        CSchema restored;
        TEST_EXPECT(ctx, (instances.document_ready() ? restored.import_from(instances, error) : restored.import_from(bulk, error)) &&
            saved(restored).find("\"\"") != std::string::npos);
    }
}

void construction_fidelity(tests::TTestContext& ctx)
{
    CSchema source, authored;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(R"({"types":{
        "enumerations":{"E":{"storage":"u8","values":{"first":0x0001,"alias":+1}}},
        "structures":{"Empty":{"members":[]},"V":{"decorations":{"purpose":"test"},"members":[{"x":{"type":"u32","default":0x0007}}]}}},
        "instances":{"u8":{"lead":{"declaration":3}},"Empty":{"zero":{}},"u16":{},
            "V":{"base":{"specialisation":{"pin":{"declaration":{"x":7},"specialisation":{}}}}}},
        "data":{"u8":{"bytes":{"data":[3,4]}},"Empty":{"zero":{"data":[{},{}]}}}})", source));
    TEST_EXPECT(ctx, authored.compose_from(source.document_query(), {}, {}, error));
    const std::string original_types = saved(authored);
    CLiveTypes types;
    CLiveInstances instances;
    CLiveBulkData bulk;
    TEST_EXPECT(ctx, source.materialise(types, instances, bulk, error));
    CSchema extracted;
    TEST_EXPECT(ctx, extracted.import_from(types, error) && saved(extracted) == original_types);
    CBakedDataBlock block;
    TEST_EXPECT(ctx, source.bake(block, error));
    CBakedTypes baked_types;
    SDiagnostic type_error;
    TEST_EXPECT(ctx, baked_types.set_block(block) && extracted.import_from(baked_types, error) && saved(extracted) == original_types);
    const auto query = instances.document_query();
    const auto section = query.object_child(query.root(), CStringView{ "instances" });
    const auto empty_group = query.object_child(section, CStringView{ "u16" });
    TEST_EXPECT(ctx, empty_group && query.child_count(empty_group) == 0u);
    const auto base = instances.find_base(CStringView{ "V" }, CStringView{ "base" });
    const auto pin = instances.find_specialisation(base, CStringView{ "pin" });
    const auto children = query.object_child(pin, CStringView{ "specialisation" });
    TEST_EXPECT(ctx, children && query.child_count(children) == 0u);
    SInstanceEntryView zero;
    SBulkDataEntryView bulk_zero;
    TEST_EXPECT(ctx, instances.entry(instances.find_base(CStringView{ "Empty" }, CStringView{ "zero" }), zero) && zero.offset == 1u && zero.byte_count == 0u);
    TEST_EXPECT(ctx, bulk.entry(bulk.find_entry(CStringView{ "Empty" }, CStringView{ "zero" }), bulk_zero) && bulk_zero.offset == 0u && bulk_zero.byte_count == 0u);
    TEST_EXPECT(ctx, instances.payload_view().size() == block.instance_payload_view().size() &&
        std::memcmp(instances.payload_view().data(), block.instance_payload_view().data(), instances.payload_view().size()) == 0);
    TEST_EXPECT(ctx, bulk.payload_view().size() == block.bulk_payload_view().size() &&
        std::memcmp(bulk.payload_view().data(), block.bulk_payload_view().data(), bulk.payload_view().size()) == 0);
    //  Replace the aggregate's storage and defaults, then exercise the extracted
    //  resolver again. Its IDs/default locations must belong to its own tree.
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u32","default":99}}]}}}})", source));
    SInstanceDiagnostic instance_error;
    const auto later = instances.create_base(CStringView{ "V" }, CStringView{ "later" }, {}, {}, instance_error);
    SInstanceEntryView value;
    TEST_EXPECT(ctx, later && instances.entry(later, value) && read_scalar_bits(value.bytes, 4u) == 7u);
    TEST_EXPECT(ctx, extracted.import_from(types, error) && saved(extracted) == original_types);
}

void construction_paths(tests::TTestContext& ctx)
{
    std::string text = R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u32","default":7}}]}}},"instances":{"V":{)";
    for (unsigned i = 0u; i < 64u; ++i)
    {
        if (i) text += ',';
        text += "\"v" + std::to_string(i) + R"(":{"specialisation":{"child":{}}})";
    }
    text += R"(}},"data":{"u32":{"records":{"data":[)";
    for (unsigned i = 0u; i < 4096u; ++i)
    {
        if (i) text += ',';
        text += std::to_string(i);
    }
    text += "]}}}}";
    CSchema source;
    SSchemaDiagnostic error;
    TEST_EXPECT(ctx, load(text.c_str(), source));
    std::string expected_document, expected_instances, expected_bulk;
    for (const bool direct : { false, true })
    {
        tests::TAllocationFailureFixture allocations;
        memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory_context{ allocator };
        CBakedDataBlock block;
        bool built{};
        {
            tests::TMemoryContextScope scope{ &memory_context };
            if (direct) built = source.bake(block, error);
            else
            {
                CLiveTypes types;
                CLiveInstances instances;
                CLiveBulkData bulk;
                built = source.materialise(types, instances, bulk, error);
                if (built)
                {
                    const detail::CDocumentRead type_source{ detail::SDocumentQueryAccess::read(types.document_query()) };
                    const detail::CDocumentRead instance_source{ detail::SDocumentQueryAccess::read(instances.document_query()) };
                    const detail::CDocumentRead bulk_source{ detail::SDocumentQueryAccess::read(bulk.document_query()) };
                    const detail::SDocumentSection sections[]{
                        { &type_source, type_source.object_child(type_source.root(), CStringView{ "types" }) },
                        { &instance_source, instance_source.object_child(instance_source.root(), CStringView{ "instances" }) },
                        { &bulk_source, bulk_source.object_child(bulk_source.root(), CStringView{ "data" }) }
                    };
                    CLiveDocument description;
                    detail::SDocumentBuildDiagnostic copy_error;
                    SDataBlockDiagnostic block_error;
                    const auto instance_bytes = instances.payload_view(), bulk_bytes = bulk.payload_view();
                    built = detail::copy_document_sections(sections, 3u, description, copy_error) &&
                        description.append_child(description.root(), description.create_boolean(true, CStringView{ "stripped" })).succeeded() &&
                        block.assemble(description, { &instance_bytes, &bulk_bytes }, block_error);
                }
            }
        }
        TEST_EXPECT(ctx, built);
        std::cout << "Schema construction " << (direct ? "direct bake" : "via live owners") << ": " << allocations.attempts <<
            " allocations, " << allocations.bytes << " requested bytes, " << allocations.largest << " largest request\n";
        if (built)
        {
            const auto written = document_writer::write(block.document());
            TEST_EXPECT(ctx, written.report.succeeded());
            const std::string document{ reinterpret_cast<const char*>(written.output.data()), written.report.logical_text_byte_size };
            const std::string instances{ reinterpret_cast<const char*>(block.instance_payload_view().data()), block.instance_payload_view().size() };
            const std::string bulk{ reinterpret_cast<const char*>(block.bulk_payload_view().data()), block.bulk_payload_view().size() };
            if (direct) TEST_EXPECT(ctx, document == expected_document && instances == expected_instances && bulk == expected_bulk);
            else
            {
                expected_document = document;
                expected_instances = instances;
                expected_bulk = bulk;
            }
        }
        block.clear();
        TEST_EXPECT(ctx, memory_context.is_attribution_empty());
    }
}

void definition_proof(tests::TTestContext& ctx)
{
    CSchema a, b, merged;
    SSchemaDiagnostic error;
    const char* natural = R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u32","default":999999999999}}]},"Unrelated":{"members":[{"x":{"type":"Missing"}}]}}}})";
    const char* explicit_layout = R"({"types":{"structures":{"V":{"detail":{"size":4,"alignment":4},"members":[{"x":{"type":"u32","offset":0}}]}}}})";
    TEST_EXPECT(ctx, load(natural, a) && load(explicit_layout, b));
    TEST_EXPECT(ctx, merged.merge_from(a.document_query(), b.document_query(), error));
    TEST_EXPECT(ctx, !merged.validate(error));
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"V":{"detail":{"alignment":8},"members":[{"x":{"type":"u32"}}]}}}})", b));
    TEST_EXPECT(ctx, !merged.merge_from(a.document_query(), b.document_query(), error) && error.reason == ESchemaReason::incompatible_definition);
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"V":{"members":[{"x":{"type":"Missing"}}]}}}})", a) &&
        load(R"({"types":{"structures":{"V":{"members":[{"x":{"type":"Missing","offset":0}}]}}}})", b));
    TEST_EXPECT(ctx, !merged.merge_from(a.document_query(), b.document_query(), error) && error.reason == ESchemaReason::unestablished_compatibility &&
        a.document_query().contains(error.first) && b.document_query().contains(error.second));
    TEST_EXPECT(ctx, load(R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u32","default":1}}]}}}})", a) &&
        load(R"({"types":{"structures":{"V":{"detail":{"size":4},"members":[{"x":{"type":"u32","offset":0,"default":2}}]}}}})", b));
    const bool accepted = merged.merge_from(a.document_query(), b.document_query(), error);
    if (accepted || error.reason != ESchemaReason::conflicting_value) std::cerr << "Definition proof conflict: " << accepted << ' ' << static_cast<unsigned>(error.reason) << '\n';
    TEST_EXPECT(ctx, !accepted && error.reason == ESchemaReason::conflicting_value);
}

void binary_components(tests::TTestContext& ctx)
{
    CLiveDocument definitions;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{
        "enumerations":{"E":{"storage":"u8","values":{"zero":0}}},
        "bit_structures":{"Flags":{"storage":"u8","members":[{"s":{"type":"i8","mask":7}},{"u":{"type":"u8","mask":24}}]}},
        "structures":{"V":{"detail":{"alignment":16,"size":16},"members":[{"e":{"type":"E"}},{"flags":{"type":"Flags"}}]}}
    }})", definitions));
    CLiveTypes types;
    SDiagnostic type_error;
    TEST_EXPECT(ctx, types.try_adopt(std::move(definitions)) && types.resolve(type_error));
    CLiveInstances instances;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, instances.initialise(types));
    alignas(128) std::uint8_t bytes[16]{};
    bytes[0] = 9u;
    bytes[1] = 0xD8u;
    bytes[15] = 0xAEu;
    const auto root = instances.capture_base(CStringView{ "V" }, CStringView{ "a" }, CByteConstView{ bytes, sizeof(bytes) }, error);
    const auto child = instances.create_specialisation(root, CStringView{ "b" }, {}, {}, error);
    TEST_EXPECT(ctx, root && child);
    const SInstanceSelectionStep enumeration{ EInstanceSelectionStepKind::member, CStringView{ "e" } };
    TEST_EXPECT(ctx, instances.specialise(child, &enumeration, 1u, CByteConstView{ bytes, 1u }, EInstanceDescendantPolicy::propagate, error));
    bytes[0] = 0u;
    TEST_EXPECT(ctx, instances.overwrite(root, CByteConstView{ bytes, sizeof(bytes) }, EInstanceDescendantPolicy::propagate, error));
    SInstanceEntryView entry;
    TEST_EXPECT(ctx, instances.entry(child, entry) && entry.bytes[0] == 9u && entry.declaration);
    const SInstanceSelectionStep field[]{ { EInstanceSelectionStepKind::member, CStringView{ "flags" } },
        { EInstanceSelectionStepKind::member, CStringView{ "s" } } };
    const std::uint8_t negative = 0xFDu, outside = 4u;
    TEST_EXPECT(ctx, instances.specialise(root, field, 2u, CByteConstView{ &negative, 1u }, EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(ctx, instances.entry(root, entry) && entry.bytes[1] == 0xDDu && entry.bytes[15] == 0xAEu);
    TEST_EXPECT(ctx, !instances.specialise(root, field, 2u, CByteConstView{ &outside, 1u }, EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(ctx, instances.entry(root, entry) && entry.bytes[1] == 0xDDu);
    CBakedDataBlock persisted;
    SInstancesBlockDiagnostic block_error;
    TEST_EXPECT(ctx, instances.bake(persisted, block_error) && !persisted.has_types());
    CBakedInstances baked;
    CLiveInstances promoted;
    TEST_EXPECT(ctx, baked.attach_block(persisted, types, error) && baked.promote(promoted, error));
    TEST_EXPECT(ctx, promoted.entry(promoted.find_specialisation(promoted.find_base(CStringView{ "V" }, CStringView{ "a" }), CStringView{ "b" }), entry) && entry.bytes[0] == 9u);
    CSchema exported;
    SSchemaDiagnostic export_error;
    TEST_EXPECT(ctx, !exported.import_from(promoted, export_error) && export_error.instances.reason == EInstanceLoadReason::unrepresentable_value);
}

void default_binding(tests::TTestContext& ctx)
{
    const auto make_types = [&](CLiveTypes& types, unsigned value)
    {
        CLiveDocument document;
        SDiagnostic error;
        const auto text = std::string{ R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u32","default":)" } +
            std::to_string(value) + R"(}}]}}}})";
        return tests::parse_document(text, document) && types.try_adopt(std::move(document)) && types.resolve(error);
    };
    CLiveTypes original, next;
    TEST_EXPECT(ctx, make_types(original, 1u) && make_types(next, 2u));
    CLiveInstances live;
    SInstanceDiagnostic error;
    TEST_EXPECT(ctx, live.initialise(original));
    const auto root = live.create_base(CStringView{ "V" }, CStringView{ "a" }, {}, {}, error);
    const auto child = live.create_specialisation(root, CStringView{ "b" }, {}, {}, error);
    const auto leaf = live.create_specialisation(child, CStringView{ "c" }, {}, {}, error);
    alignas(128) const std::uint32_t pinned = 1u;
    const SInstanceSelectionStep x{ EInstanceSelectionStepKind::member, CStringView{ "x" } };
    TEST_EXPECT(ctx, live.specialise(child, &x, 1u, CByteConstView{ reinterpret_cast<const std::uint8_t*>(&pinned), sizeof(pinned) },
        EInstanceDescendantPolicy::propagate, error));
    const auto* allocation = live.payload_view().data();
    TEST_EXPECT(ctx, !live.rebind_types(next, ETypeBindMode::asserted_recovery, error));
    TEST_EXPECT(ctx, live.rebind_types(next, ETypeBindMode::checked, error) && live.payload_view().data() == allocation);
    SInstanceEntryView a, b, c;
    TEST_EXPECT(ctx, live.entry(root, a) && live.entry(child, b) && live.entry(leaf, c) &&
        read_scalar_bits(a.bytes, 4u) == 2u && read_scalar_bits(b.bytes, 4u) == 1u && read_scalar_bits(c.bytes, 4u) == 1u);
    CBakedDataBlock block;
    SInstancesBlockDiagnostic block_error;
    TEST_EXPECT(ctx, live.bake(block, block_error));
    CBakedInstances baked;
    TEST_EXPECT(ctx, baked.attach_block(block, next, error));
    CByteBuffer replacement;
    TEST_EXPECT(ctx, !baked.rebind_types(original, ETypeBindMode::checked, error));
    TEST_EXPECT(ctx, baked.rebind_types(original, replacement, ETypeBindMode::checked, error));
    TEST_EXPECT(ctx, baked.entry(baked.find_base(CStringView{ "V" }, CStringView{ "a" }), a) && read_scalar_bits(a.bytes, 4u) == 1u &&
        read_scalar_bits(block.instance_payload_view().data(), 4u) == 2u && a.bytes == replacement.data());
    //  A dead prior owner permits explicitly asserted recovery, including a
    //  separately owned baked working payload whose source header has old types.
    live.clear();
    TEST_EXPECT(ctx, next.clear());
    TEST_EXPECT(ctx, original.reference_count() == 1u);
    baked.clear();
    CLiveInstances recovered;
    {
        CLiveTypes temporary;
        TEST_EXPECT(ctx, make_types(temporary, 3u) && recovered.initialise(temporary));
        TEST_EXPECT(ctx, recovered.create_base(CStringView{ "V" }, CStringView{ "r" }, {}, {}, error));
    }
    TEST_EXPECT(ctx, !recovered.loaded_ready() && !recovered.rebind_types(original, ETypeBindMode::checked, error));
    TEST_EXPECT(ctx, recovered.rebind_types(original, ETypeBindMode::asserted_recovery, error));
    TEST_EXPECT(ctx, recovered.entry(recovered.find_base(CStringView{ "V" }, CStringView{ "r" }), a) && read_scalar_bits(a.bytes, 4u) == 1u);
}

}   // namespace

void run_schema_model_cases(tests::TTestContext& context)
{
    authoring(context);
    merging(context);
    selections(context);
    instance_cascade(context);
    sparse_empty_roundtrip(context);
    runtime_conversion(context);
    construction_diagnostics(context);
    construction_fidelity(context);
    construction_paths(context);
    definition_proof(context);
    binary_components(context);
    default_binding(context);
}

int run_schema_model_tests()
{
    tests::TTestContext context;
    run_schema_model_cases(context);
    return context.report("SchemaModel");
}
