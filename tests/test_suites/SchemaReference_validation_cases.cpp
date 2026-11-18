
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaReference_validation_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    17 Nov 26
//
//  Independent malformed descriptors, dependency identity, rollback and limits.

#include "tests/test_suites/SchemaReference_test_cases.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/schema_fixtures.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "schema/data_block_edit.hpp"
#include "schema/data_copy.hpp"
#include "schema/value_codec.hpp"
#include "schema/data_remap.hpp"
#include "schema/data_operation_internal.hpp"
#include "schema/data_reference_context.hpp"
#include "schema/data_block_format.hpp"
#include "schema/operation_meter.hpp"
#include "schema/prepared_schema.hpp"
#include "schema/reference_types.hpp"
#include "data_model/document_writer.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr char k_graph[] = R"({
 "types":{"structures":{
   "Leaf":{"members":[{"label":{"type":{"kind":"text"}}},{"flag":{"type":"b8"}},{"value":{"type":"u32"}}]},
   "Group":{"members":[{"leaves":{"type":{"kind":"span","element":"Leaf"}}}]},
   "Report":{"members":[{"title":{"type":{"kind":"text","nullable":true},"default":"Inherited"}},
     {"groups":{"type":{"kind":"span","element":"Group"}}}]}
 }},
 "instances":{"Report":{"root":{"declaration":{"groups":{"collection":"groups","first":0,"count":2}},
   "specialisation":{"child":{}}}}},
 "data":{
   "Leaf":{"leaves":{"data":[{"label":"A","flag":true,"value":1},{"label":"B","flag":false,"value":2},{"label":"C","flag":true,"value":3}]},
     "spare":{"data":[{"label":"unused","flag":true,"value":9}]}},
   "Group":{"groups":{"data":[{"leaves":{"collection":"leaves","first":0,"count":2}},{"leaves":{"collection":"leaves","first":1,"count":2}}]}}
 }
})";

bool fixture(tests::TTestContext& context, schema::CBakedDataBlock& block, schema::CDataBlockView& view)
{
    schema::SSchemaDiagnostic authored;
    schema::SDataOperationDiagnostic error;
    const bool success = tests::bake_schema_document(k_graph, block, authored) && view.bind(block, {}, error);
    if (!success)
    {
        std::cerr << "Reference fixture: schema=" << static_cast<unsigned>(authored.reason)
            << " types=" << static_cast<unsigned>(authored.types.reason)
            << " instance=" << static_cast<unsigned>(authored.instances.reason)
            << " bulk=" << static_cast<unsigned>(authored.bulk_data.reason)
            << " operation=" << static_cast<unsigned>(authored.operation.reason)
            << " limit=" << static_cast<unsigned>(authored.operation.limit)
            << " entry=" << authored.operation.entry
            << " bind=" << static_cast<unsigned>(error.reason) << '\n';
    }
    TEST_EXPECT(context, success);
    return success;
}

void malformed(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    const schema::CDataEntryHandle root = view.find_instance(CStringView{ "Report" }, CStringView{ "root" });
    schema::CDataValueView value, title, span;
    CByteConstView title_bytes, span_bytes;
    TEST_EXPECT(context, view.value(root, 0u, value) && view.member(value, CStringView{ "title" }, title) &&
        view.member(value, CStringView{ "groups" }, span) && view.bytes(title, title_bytes) && view.bytes(span, span_bytes));
    const CByteConstView file = source.file_bytes();
    const std::size_t title_offset = static_cast<std::size_t>(title_bytes.data() - file.data());
    const std::size_t span_offset = static_cast<std::size_t>(span_bytes.data() - file.data());
    const std::uint32_t valid_id = static_cast<std::uint32_t>(schema::read_scalar_bits(span_bytes.data(), 4u));
    struct SBad { bool text; std::uint32_t word, value; };
    const SBad cases[]{
        { true, 0u, 0xfffffffeu }, { true, 0u, 0x7fffffffu },
        { false, 0u, 0xfffffffeu }, { false, 0u, 0x7fffffffu },
        { false, 0u, 0u }, { false, 0u, UINT32_MAX },
        { false, 1u, 0x80000000u }, { false, 1u, 0x7fffffffu },
        { false, 2u, 0u }, { false, 2u, 0x80000000u }, { false, 2u, UINT32_MAX }
    };
    for (const SBad bad : cases)
    {
        CByteBuffer bytes;
        TEST_EXPECT(context, bytes.allocate(file.size(), 128u) && bytes.set_size(file.size()));
        std::memcpy(bytes.data(), file.data(), file.size());
        schema::write_scalar_bits((bytes.data() + (bad.text ? title_offset : span_offset) + (bad.word << 2u)), 4u, bad.value);
        CBakedDocumentBlock generic;
        schema::CBakedDataBlock mutated;
        schema::SDataBlockDiagnostic storage_error;
        TEST_EXPECT(context, generic.adopt_buffer(std::move(bytes)) && mutated.adopt_local(std::move(generic), storage_error));
        schema::CDataBlockView rejected;
        schema::SDataOperationDiagnostic error;
        TEST_EXPECT(context, !rejected.bind(mutated, {}, error) && !rejected.is_ready());
        TEST_EXPECT(context, error.reason == schema::EDataOperationReason::invalid_reference);
    }
    TEST_EXPECT(context, schema::read_scalar_bits(span_bytes.data(), 4u) == valid_id);

    const char* const invalid_values[]{ "null", "{\"absent\":true}", "{\"empty\":false}",
        "{\"collection\":\"missing\",\"first\":0,\"count\":0}",
        "{\"collection\":\"groups\",\"first\":3,\"count\":0}",
        "{\"collection\":\"groups\",\"first\":-1,\"count\":1}",
        "{\"collection\":\"groups\",\"first\":0,\"count\":2147483649}",
        "{\"collection\":\"\",\"first\":0,\"count\":0}" };
    const std::string original{ k_graph };
    const std::string marker{ "{\"collection\":\"groups\",\"first\":0,\"count\":2}" };
    const std::size_t position = original.find(marker);
    TEST_EXPECT(context, position != std::string::npos);
    for (const char* const replacement : invalid_values)
    {
        std::string text = original;
        text.replace(position, marker.size(), replacement);
        schema::CBakedDataBlock output;
        schema::SSchemaDiagnostic error;
        TEST_EXPECT(context, !tests::bake_schema_document(text, output, error) && !output.loaded_ready());
    }
    std::string empty = original;
    empty.replace(position, marker.size(), "{\"collection\":\"groups\",\"first\":2,\"count\":0}");
    schema::CBakedDataBlock canonical;
    schema::SSchemaDiagnostic error;
    TEST_EXPECT(context, tests::bake_schema_document(empty, canonical, error));
    schema::CDataBlockView empty_view;
    schema::SDataOperationDiagnostic bound;
    TEST_EXPECT(context, empty_view.bind(canonical, {}, bound));
    schema::SDataRangeView range;
    TEST_EXPECT(context, empty_view.value(empty_view.find_instance(CStringView{ "Report" }, CStringView{ "root" }), 0u, value) &&
        empty_view.member(value, CStringView{ "groups" }, span) && empty_view.span(span, range) && !range.absent && !range.count && !range.first);
}

void dependency_identity(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    const schema::CDataBlockView* const sources[]{ &view };
    const schema::SDataCopyRoot root[]{ { 0u, view.find_instance(CStringView{ "Report" }, CStringView{ "root" }) } };
    schema::CBakedDataBlock copied;
    schema::SDataOperationDiagnostic error;
    TEST_EXPECT(context, schema::copy_data(sources, 1u, root, 1u, nullptr, 0u, {}, copied, error));
    schema::CDataBlockView result;
    TEST_EXPECT(context, result.bind(copied, {}, error));
    TEST_EXPECT(context, result.entry_count(schema::EDataEntryKind::collection) == 2u);
    const schema::CDataEntryHandle leaves = result.find_collection(CStringView{ "Leaf" }, CStringView{ "leaves" });
    schema::SDataEntryInfo info;
    TEST_EXPECT(context, result.entry(leaves, info) && (info.count == 3u));
    TEST_EXPECT(context, !result.find_collection(CStringView{ "Leaf" }, CStringView{ "spare" }));
    schema::CDataValueView first, second, first_range, second_range;
    const schema::CDataEntryHandle groups = result.find_collection(CStringView{ "Group" }, CStringView{ "groups" });
    schema::SDataRangeView a, b;
    TEST_EXPECT(context, result.value(groups, 0u, first) && result.value(groups, 1u, second) &&
        result.member(first, CStringView{ "leaves" }, first_range) && result.member(second, CStringView{ "leaves" }, second_range) &&
        result.span(first_range, a) && result.span(second_range, b));
    TEST_EXPECT(context, (a.first == 0u) && (b.first == 1u) && (a.count == 2u) && (b.count == 2u));
    schema::CDataValueView overlap_a, overlap_b;
    CByteConstView bytes_a, bytes_b;
    TEST_EXPECT(context, result.element(a, 1u, overlap_a) && result.element(b, 0u, overlap_b) &&
        result.bytes(overlap_a, bytes_a) && result.bytes(overlap_b, bytes_b) && (bytes_a.data() == bytes_b.data()));

    schema::CBakedDataBlock other;
    schema::CDataBlockView other_view;
    std::string different{ k_graph };
    different.replace(different.find("\"A\""), 3u, "\"Different\"");
    schema::SSchemaDiagnostic authored;
    TEST_EXPECT(context, tests::bake_schema_document(different, other, authored) && other_view.bind(other, {}, error));
    if (!other_view.is_ready()) return;
    const schema::CDataBlockView* const two[]{ &view, &other_view };
    const schema::SDataCopyRoot both[]{ { 0u, view.find_collection(CStringView{ "Leaf" }, CStringView{ "leaves" }) },
        { 1u, other_view.find_collection(CStringView{ "Leaf" }, CStringView{ "leaves" }) } };
    schema::CBakedDataBlock conflict;
    TEST_EXPECT(context, !schema::copy_data(two, 2u, both, 2u, nullptr, 0u, {}, conflict, error));
    TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::naming_conflict) && !conflict.loaded_ready());
    const schema::SDataCopyName rename[]{ { 1u, both[1].entry, CStringView{ "other leaves" } } };
    TEST_EXPECT(context, schema::copy_data(two, 2u, both, 2u, rename, 1u, {}, conflict, error));
    schema::CDataBlockView distinct;
    TEST_EXPECT(context, distinct.bind(conflict, {}, error) && (distinct.entry_count(schema::EDataEntryKind::collection) == 2u));
    schema::CDataValueView record, label;
    schema::SDataTextView text;
    TEST_EXPECT(context, distinct.value(distinct.find_collection(CStringView{ "Leaf" }, CStringView{ "leaves" }), 0u, record) &&
        distinct.member(record, CStringView{ "label" }, label) && distinct.text(label, text) && (text.stored_text == CStringView{ "A" }));
    TEST_EXPECT(context, distinct.value(distinct.find_collection(CStringView{ "Leaf" }, CStringView{ "other leaves" }), 0u, record) &&
        distinct.member(record, CStringView{ "label" }, label) && distinct.text(label, text) && (text.stored_text == CStringView{ "Different" }));
}

void rollback_and_limits(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    schema::SDataOperationDiagnostic error;
    schema::CDataBlockEdit edit;
    TEST_EXPECT(context, edit.begin(view, {}, error));
    const schema::CDataEditHandle leaves = edit.find(CStringView{ "Leaf" }, schema::EDataEntryKind::collection, CStringView{ "leaves" });
    CLiveDocument input;
    TEST_EXPECT(context, tests::parse_document("{\"records\":[{\"label\":\"short\",\"flag\":true,\"value\":1}],\"bad\":[{\"label\":17}]}", input));
    TEST_EXPECT(context, !edit.replace_collection(leaves, input, input.object_child(input.root(), CStringView{ "bad" }), error));
    TEST_EXPECT(context, edit.replace_collection(leaves, input, input.object_child(input.root(), CStringView{ "records" }), error));
    schema::CBakedDataBlock rejected;
    TEST_EXPECT(context, !edit.commit(rejected, error) && edit.is_ready() && !rejected.loaded_ready());
    TEST_EXPECT(context, error.reason == schema::EDataOperationReason::invalid_reference);
    TEST_EXPECT(context, tests::parse_document("{\"records\":[{\"label\":\"one\",\"flag\":true,\"value\":1},{\"label\":\"two\",\"flag\":false,\"value\":2},{\"label\":\"three\",\"flag\":true,\"value\":3}]}", input));
    TEST_EXPECT(context, edit.replace_collection(leaves, input, input.object_child(input.root(), CStringView{ "records" }), error));
    TEST_EXPECT(context, edit.commit(rejected, error) && !edit.is_ready());

    const schema::EDataOperationLimit kinds[]{ schema::EDataOperationLimit::inspected_source_bytes, schema::EDataOperationLimit::owned_bytes,
        schema::EDataOperationLimit::document_nodes, schema::EDataOperationLimit::records, schema::EDataOperationLimit::work_credits,
        schema::EDataOperationLimit::depth, schema::EDataOperationLimit::result_bytes };
    for (const schema::EDataOperationLimit kind : kinds)
    {
        schema::SDataOperationLimits limits;
        switch (kind)
        {
            case schema::EDataOperationLimit::inspected_source_bytes:
            {
                limits.inspected_source_bytes = 0u;
                break;
            }
            case schema::EDataOperationLimit::owned_bytes:
            {
                limits.owned_bytes = 0u;
                break;
            }
            case schema::EDataOperationLimit::document_nodes:
            {
                limits.document_nodes = 0u;
                break;
            }
            case schema::EDataOperationLimit::records:
            {
                limits.records = 0u;
                break;
            }
            case schema::EDataOperationLimit::work_credits:
            {
                limits.work_credits = 0u;
                break;
            }
            case schema::EDataOperationLimit::depth:
            {
                limits.depth = 1u;
                break;
            }
            case schema::EDataOperationLimit::result_bytes:
            {
                limits.result_bytes = 1u;
                break;
            }
            default:
            {
                break;
            }
        }
        schema::CBakedDataBlock output;
        TEST_EXPECT(context, !schema::repack_data(view, limits, output, error) && !output.loaded_ready());
        TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::limit_exceeded) && (error.limit == kind));
    }
    schema::SDataOperationLimits retry_limits;
    retry_limits.records = 8u;
    schema::CBakedDataBlock boundary;
    TEST_EXPECT(context, schema::repack_data(view, retry_limits, boundary, error));
    retry_limits.records = 7u;
    schema::CBakedDataBlock over_boundary;
    TEST_EXPECT(context, !schema::repack_data(view, retry_limits, over_boundary, error) &&
        (error.limit == schema::EDataOperationLimit::records) && (error.requested == 8u));
    retry_limits = {};
    retry_limits.work_credits = 131072u;
    schema::CDataBlockEdit retry;
    TEST_EXPECT(context, retry.begin(view, retry_limits, error));
    const schema::CDataEditHandle renamed = retry.find(CStringView{ "Leaf" }, schema::EDataEntryKind::collection, CStringView{ "spare" });
    bool exhausted{};
    for (unsigned attempt = 0u; attempt < 4096u; ++attempt)
    {
        if (!retry.rename(renamed, CStringView{ (attempt & 1u) ? "spare" : "again" }, error))
        {
            exhausted = error.reason == schema::EDataOperationReason::limit_exceeded;
            break;
        }
    }
    TEST_EXPECT(context, exhausted && retry.is_ready());
    TEST_EXPECT(context, !retry.rename(renamed, CStringView{ "once more" }, error));
    TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::limit_exceeded) &&
        (error.limit == schema::EDataOperationLimit::work_credits));
    schema::CBakedDataBlock output;
    TEST_EXPECT(context, !retry.commit(output, error) && !output.loaded_ready());
    retry.cancel();
    TEST_EXPECT(context, !retry.is_ready());
}

bool observes_graph(const schema::CDataBlockView& view)
{
    schema::CDataValueView root, field, group, leaf;
    schema::SDataTextView text;
    schema::SDataRangeView groups, leaves;
    return view.value(view.find_instance(CStringView{ "Report" }, CStringView{ "root" }), 0u, root) &&
        view.member(root, CStringView{ "title" }, field) && view.text(field, text) &&
        !text.absent && (text.stored_text == CStringView{ "Inherited" }) &&
        view.member(root, CStringView{ "groups" }, field) && view.span(field, groups) && (groups.count == 2u) &&
        view.element(groups, 0u, group) && view.member(group, CStringView{ "leaves" }, field) &&
        view.span(field, leaves) && (leaves.count == 2u) && view.element(leaves, 0u, leaf) &&
        view.member(leaf, CStringView{ "label" }, field) && view.text(field, text) &&
        !text.absent && (text.stored_text == CStringView{ "A" });
}

void owner_lifetimes(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView original;
    if (!fixture(context, source, original)) return;
    tests::TAllocationFailureFixture original_allocations, unrelated_allocations;
    unrelated_allocations.reject_all = true;
    memory::CMemoryAllocator original_allocator{ &original_allocations,
        &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryAllocator unrelated_allocator{ &unrelated_allocations,
        &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext original_memory{ original_allocator }, unrelated_memory{ unrelated_allocator };
    for (const bool publish : { false, true })
    {
        {
            schema::CBakedDataBlock output;
            schema::SDataOperationDiagnostic error;
            {
                const tests::TMemoryContextScope unrelated_scope{ &unrelated_memory };
                schema::CDataBlockView view;
                schema::CDataBlockEdit edit;
                {
                    const tests::TMemoryContextScope original_scope{ &original_memory };
                    TEST_EXPECT(context, view.bind(source, {}, error) && edit.begin(view, {}, error));
                }
                TEST_EXPECT(context, !original_memory.is_attribution_empty() && observes_graph(view));
                const schema::CDataEditHandle spare = edit.find(CStringView{ "Leaf" },
                    schema::EDataEntryKind::collection, CStringView{ "spare" });
                TEST_EXPECT(context, edit.rename(spare, CStringView{ "renamed" }, error));
                TEST_EXPECT(context, memory::get_ambient_memory_context() == &unrelated_memory);
                if (publish)
                {
                    TEST_EXPECT(context, edit.commit(output, error) && !edit.is_ready());
                }
                else
                {
                    view.clear();
                    view.clear();
                    edit.cancel();
                    edit.cancel();
                    TEST_EXPECT(context, !view.is_ready() && !edit.is_ready() && original_memory.is_attribution_empty());
                }
                //  In the publication case the view is destroyed under the
                //  unrelated ambient context, after its original scope ended.
            }
            TEST_EXPECT(context, !unrelated_allocations.attempts && unrelated_memory.is_attribution_empty());
            if (publish)
            {
                //  The edit operation and input view have both been destroyed;
                //  published bytes retain only the still-live parent allocator.
                TEST_EXPECT(context, !original_memory.is_attribution_empty());
                schema::CDataBlockView result;
                TEST_EXPECT(context, result.bind(output, {}, error) && observes_graph(result));
                TEST_EXPECT(context, result.find_collection(CStringView{ "Leaf" }, CStringView{ "renamed" }));
            }
        }
        TEST_EXPECT(context, original_memory.is_attribution_empty());
    }
}

void nested_operation_scopes(tests::TTestContext& context)
{
    memory::CMemoryContext* const prior_memory = memory::get_ambient_memory_context();
    const bool prior_active = schema::detail::data_active();
    schema::SDataOperationLimits limits;
    limits.work_credits = 3u;
    schema::detail::CDataOperation outer{ limits };
    {
        const schema::detail::CDataOperationScope outer_scope{ outer };
        TEST_EXPECT(context, schema::detail::data_work());
        limits.work_credits = 2u;
        schema::detail::CDataOperation inner{ limits };
        {
            const schema::detail::CDataOperationScope inner_scope{ inner };
            TEST_EXPECT(context, schema::detail::data_work());
            {
                const schema::detail::CDataOperationScope parent_scope{ prior_memory };
                TEST_EXPECT(context, (memory::get_ambient_memory_context() == prior_memory) &&
                    schema::detail::data_work());
            }
            TEST_EXPECT(context, (memory::get_ambient_memory_context() == &inner.memory_context()) &&
                !schema::detail::data_work());
        }
        TEST_EXPECT(context, (memory::get_ambient_memory_context() == &outer.memory_context()) &&
            schema::detail::data_healthy() && schema::detail::data_work(2u) && !schema::detail::data_work());
    }
    TEST_EXPECT(context, (memory::get_ambient_memory_context() == prior_memory) &&
        (schema::detail::data_active() == prior_active));
}

void shared_checked_state_limits(tests::TTestContext& context)
{
    schema::CBakedDataBlock source, distinct;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    schema::SSchemaDiagnostic authored;
    TEST_EXPECT(context, tests::bake_schema_document(k_graph, distinct, authored));
    tests::TAllocationFailureFixture allocations;
    memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    {
        const tests::TMemoryContextScope memory_scope{ &memory };
        schema::SDataOperationLimits limits;
        limits.inspected_source_bytes = source.file_bytes().size();
        schema::detail::CDataOperation operation{ limits };
        const schema::detail::CDataOperationScope operation_scope{ operation };
        schema::detail::SDataViewState first{ operation }, alias{ operation }, other{ operation };
        TEST_EXPECT(context, first.load(source));
        const std::uint64_t first_storage = operation.memory_context().get_live_allocated_bytes();
        TEST_EXPECT(context, alias.load(source) &&
            (operation.memory_context().get_live_allocated_bytes() > first_storage));
        TEST_EXPECT(context, !other.load(distinct));
        schema::SDataOperationDiagnostic error;
        operation.diagnose(error);
        TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::limit_exceeded) &&
            (error.limit == schema::EDataOperationLimit::inspected_source_bytes) &&
            (error.requested == (source.file_bytes().size() + distinct.file_bytes().size())));
    }
    TEST_EXPECT(context, memory.is_attribution_empty());
    {
        const tests::TMemoryContextScope memory_scope{ &memory };
        schema::SDataOperationLimits limits;
        limits.document_nodes = source.document().value_count();
        schema::detail::CDataOperation operation{ limits };
        const schema::detail::CDataOperationScope operation_scope{ operation };
        schema::detail::SDataViewState first{ operation }, second{ operation };
        TEST_EXPECT(context, first.load(source) && !second.load(source));
        schema::SDataOperationDiagnostic error;
        operation.diagnose(error);
        TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::limit_exceeded) &&
            (error.limit == schema::EDataOperationLimit::document_nodes) &&
            (error.requested == (static_cast<std::uint64_t>(limits.document_nodes) << 1u)));
    }
    TEST_EXPECT(context, memory.is_attribution_empty());
}

void queries_have_no_lifetime_budget(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView original;
    if (!fixture(context, source, original)) return;
    schema::SDataOperationLimits limits;
    std::uint64_t lower{}, upper = limits.work_credits;
    schema::SDataOperationDiagnostic error;
    //  Find this fixture's exact binding allowance without fixing the test to
    //  today's implementation cost. Successful queries must need no remainder.
    while (lower < upper)
    {
        limits.work_credits = lower + ((upper - lower) >> 1u);
        schema::CDataBlockView probe;
        if (probe.bind(source, limits, error)) upper = limits.work_credits;
        else
        {
            TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::limit_exceeded) &&
                (error.limit == schema::EDataOperationLimit::work_credits));
            lower = limits.work_credits + 1u;
        }
    }
    limits.work_credits = upper;
    schema::CDataBlockView view;
    TEST_EXPECT(context, view.bind(source, limits, error));
    for (unsigned attempt = 0u; attempt < 32u; ++attempt)
    {
        TEST_EXPECT(context, observes_graph(view));
    }
    limits.work_credits = 0u;
    schema::detail::CDataOperation unrelated{ limits };
    {
        const schema::detail::CDataOperationScope scope{ unrelated };
        TEST_EXPECT(context, !observes_graph(view));
        unrelated.diagnose(error);
        TEST_EXPECT(context, (error.reason == schema::EDataOperationReason::limit_exceeded) &&
            (error.limit == schema::EDataOperationLimit::work_credits) &&
            (memory::get_ambient_memory_context() == &unrelated.memory_context()));
    }
    TEST_EXPECT(context, observes_graph(view));
}

void allocation_failures(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    const std::vector<std::uint8_t> snapshot(source.file_bytes().data(), source.file_bytes().data() + source.file_bytes().size());
    bool complete{};
    for (std::size_t attempt = 0u; attempt < 1024u; ++attempt)
    {
        tests::TAllocationFailureFixture injection;
        injection.fail_on = attempt;
        memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        {
            const tests::TMemoryContextScope scope{ &memory };
            schema::CBakedDataBlock output;
            schema::SDataOperationDiagnostic error;
            const bool success = schema::repack_data(view, {}, output, error);
            if (!injection.failed)
            {
                TEST_EXPECT(context, success && output.loaded_ready());
                complete = success;
            }
            else if (!success)
            {
                TEST_EXPECT(context, !output.loaded_ready());
                TEST_EXPECT(context, error.reason == schema::EDataOperationReason::allocation_failed);
            }
            else TEST_EXPECT(context, output.loaded_ready());
        }
        TEST_EXPECT(context, memory.is_attribution_empty());
        TEST_EXPECT(context, std::memcmp(snapshot.data(), source.file_bytes().data(), snapshot.size()) == 0);
        if (complete) break;
    }
    TEST_EXPECT(context, complete);
}

void view_bind_failures(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView original;
    if (!fixture(context, source, original)) return;
    const std::vector<std::uint8_t> snapshot(source.file_bytes().data(), (source.file_bytes().data() + source.file_bytes().size()));
    bool completed{};
    for (std::size_t attempt = 0u; (attempt < 1024u) && !completed; ++attempt)
    {
        tests::TAllocationFailureFixture injection;
        injection.fail_on = attempt;
        memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        {
            const tests::TMemoryContextScope scope{ &memory };
            schema::CDataBlockView view;
            schema::SDataOperationDiagnostic error;
            const bool success = view.bind(source, {}, error);
            completed = success && !injection.failed;
            if (!success)
            {
                TEST_EXPECT(context, injection.failed && !view.is_ready() &&
                    (error.reason == schema::EDataOperationReason::allocation_failed));
                injection.fail_on = SIZE_MAX;
                TEST_EXPECT(context, view.bind(source, {}, error));
            }
            schema::CDataValueView root, field;
            schema::SDataRangeView range;
            TEST_EXPECT(context, view.is_ready() &&
                view.value(view.find_instance(CStringView{ "Report" }, CStringView{ "root" }), 0u, root) &&
                view.member(root, CStringView{ "groups" }, field) && view.span(field, range) && (range.count == 2u));
            view.clear();
            view.clear();
            TEST_EXPECT(context, !view.is_ready());
        }
        TEST_EXPECT(context, memory.is_attribution_empty());
        TEST_EXPECT(context, std::memcmp(snapshot.data(), source.file_bytes().data(), snapshot.size()) == 0);
    }
    TEST_EXPECT(context, completed);
}

void edit_begin_failures(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    const std::vector<std::uint8_t> snapshot(source.file_bytes().data(), (source.file_bytes().data() + source.file_bytes().size()));
    const schema::CTypesDocumentQuery types{ *view.document() };
    const CLiveDocument input;
    for (unsigned phase = 0u; phase < 2u; ++phase)
    {
        const bool empty = phase == 1u;
        bool completed{};
        for (std::size_t attempt = 0u; (attempt < 1024u) && !completed; ++attempt)
        {
            tests::TAllocationFailureFixture injection;
            injection.fail_on = attempt;
            memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext memory{ allocator };
            {
                const tests::TMemoryContextScope scope{ &memory };
                schema::CDataBlockEdit edit;
                schema::SDataOperationDiagnostic error;
                const auto begin = [&]()
                {
                    return empty ? edit.begin_empty(types, {}, error) : edit.begin(view, {}, error);
                };
                const bool success = begin();
                completed = success && !injection.failed;
                injection.fail_on = SIZE_MAX;
                TEST_EXPECT(context, memory::get_ambient_memory_context() == &memory);
                if (!success)
                {
                    TEST_EXPECT(context, injection.failed && !edit.is_ready() &&
                        (error.reason == schema::EDataOperationReason::allocation_failed));
                    //  Setup must clean itself up before cancellation or retry.
                    TEST_EXPECT(context, memory.is_attribution_empty());
                    TEST_EXPECT(context, begin());
                    TEST_EXPECT(context, memory::get_ambient_memory_context() == &memory);
                }
                TEST_EXPECT(context, edit.is_ready() && (error.reason == schema::EDataOperationReason::none));
                const schema::CDataEditHandle existing = edit.find(CStringView{ "Report" },
                    schema::EDataEntryKind::instance, CStringView{ "root" });
                if (empty)
                {
                    TEST_EXPECT(context, !existing);
                    TEST_EXPECT(context, edit.create_instance(CStringView{ "Leaf" }, CStringView{ "created" }, input, {}, {}, error));
                }
                else TEST_EXPECT(context, existing);
                edit.cancel();
                edit.cancel();
                TEST_EXPECT(context, !edit.is_ready() && memory.is_attribution_empty());
            }
            TEST_EXPECT(context, memory.is_attribution_empty());
            TEST_EXPECT(context, std::memcmp(snapshot.data(), source.file_bytes().data(), snapshot.size()) == 0);
        }
        TEST_EXPECT(context, completed);
    }
}

void invalid_operation_limits(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    const std::vector<std::uint8_t> snapshot(source.file_bytes().data(), (source.file_bytes().data() + source.file_bytes().size()));
    const schema::CTypesDocumentQuery types{ *view.document() };
    const CLiveDocument input;
    CLiveDocument document;
    schema::CSchema authored;
    schema::SSchemaDiagnostic authored_error;
    const bool admitted = tests::parse_document(k_graph, document) && authored.try_adopt(std::move(document), authored_error);
    TEST_EXPECT(context, admitted);
    if (!admitted) return;

    schema::SDataOperationLimits invalid[7];
    invalid[0].inspected_source_bytes = UINT64_MAX;
    invalid[1].owned_bytes = UINT64_MAX;
    invalid[2].result_bytes = UINT64_MAX;
    invalid[3].work_credits = UINT64_MAX;
    invalid[4].document_nodes = UINT32_MAX;
    invalid[5].records = UINT32_MAX;
    invalid[6].depth = UINT32_MAX;
    memory::CMemoryContext* const prior_memory = memory::get_ambient_memory_context();
    const bool prior_active = schema::detail::data_active();
    for (const schema::SDataOperationLimits limits : invalid)
    {
        for (const bool empty : { false, true })
        {
            tests::TAllocationFailureFixture allocations;
            memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext memory{ allocator };
            {
                const tests::TMemoryContextScope scope{ &memory };
                schema::CDataBlockEdit edit;
                schema::SDataOperationDiagnostic error;
                const bool began = empty ? edit.begin_empty(types, limits, error) : edit.begin(view, limits, error);
                TEST_EXPECT(context, !began && !edit.is_ready() &&
                    (error.reason == schema::EDataOperationReason::invalid_input) &&
                    (error.limit == schema::EDataOperationLimit::none));
                //  Reach operation validation after allocating the session;
                //  cleanup must finish before any explicit cancel or retry.
                TEST_EXPECT(context, (allocations.attempts > 0u) && !allocations.failed && memory.is_attribution_empty());
                TEST_EXPECT(context, (memory::get_ambient_memory_context() == &memory) &&
                    (schema::detail::data_active() == prior_active));
                TEST_EXPECT(context, empty ? edit.begin_empty(types, {}, error) : edit.begin(view, {}, error));
                TEST_EXPECT(context, edit.is_ready() && (error.reason == schema::EDataOperationReason::none));
                const schema::CDataEditHandle existing = edit.find(CStringView{ "Report" },
                    schema::EDataEntryKind::instance, CStringView{ "root" });
                if (empty)
                {
                    TEST_EXPECT(context, !existing);
                    TEST_EXPECT(context, edit.create_instance(CStringView{ "Leaf" }, CStringView{ "created" }, input, {}, {}, error));
                }
                else TEST_EXPECT(context, existing);
                edit.cancel();
                edit.cancel();
                TEST_EXPECT(context, !edit.is_ready() && memory.is_attribution_empty());
                TEST_EXPECT(context, (memory::get_ambient_memory_context() == &memory) &&
                    (schema::detail::data_active() == prior_active));
            }
            TEST_EXPECT(context, memory.is_attribution_empty());
            TEST_EXPECT(context, (memory::get_ambient_memory_context() == prior_memory) &&
                (schema::detail::data_active() == prior_active));
        }

        tests::TAllocationFailureFixture allocations;
        memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        {
            const tests::TMemoryContextScope scope{ &memory };
            schema::CBakedDataBlock output;
            TEST_EXPECT(context, !authored.bake(output, limits, authored_error) && !output.loaded_ready());
            TEST_EXPECT(context, (authored_error.reason == schema::ESchemaReason::construction_failed) &&
                (authored_error.operation.reason == schema::EDataOperationReason::invalid_input) &&
                (authored_error.operation.limit == schema::EDataOperationLimit::none));
            TEST_EXPECT(context, memory.is_attribution_empty());
            TEST_EXPECT(context, (memory::get_ambient_memory_context() == &memory) &&
                (schema::detail::data_active() == prior_active));
            TEST_EXPECT(context, authored.bake(output, {}, authored_error) && output.loaded_ready());
            TEST_EXPECT(context, (authored_error.reason == schema::ESchemaReason::none) &&
                (authored_error.operation.reason == schema::EDataOperationReason::none));
            output.clear();
            TEST_EXPECT(context, memory.is_attribution_empty());
        }
        TEST_EXPECT(context, memory.is_attribution_empty());
        TEST_EXPECT(context, (memory::get_ambient_memory_context() == prior_memory) &&
            (schema::detail::data_active() == prior_active));
    }
    TEST_EXPECT(context, std::memcmp(snapshot.data(), source.file_bytes().data(), snapshot.size()) == 0);
}

void transactions(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    schema::CDataBlockEdit edit;
    schema::SDataOperationDiagnostic error;
    TEST_EXPECT(context, edit.begin_empty(schema::CTypesDocumentQuery{ *view.document() }, {}, error));
    CLiveDocument input;
    TEST_EXPECT(context, tests::parse_document(R"({
        "root":{"groups":{"collection":"future groups","first":0,"count":1}},
        "groups":[{"leaves":{"collection":"future leaves","first":0,"count":1}}],
        "leaves":[{"label":"Created","flag":true,"value":42}],
        "title":"Changed", "absent":{"absent":true}, "empty":{"empty":true}
    })", input));
    const schema::CDataEditHandle root = edit.create_instance(CStringView{ "Report" }, CStringView{ "created" },
        input, input.object_child(input.root(), CStringView{ "root" }), {}, error);
    TEST_EXPECT(context, root);
    schema::CBakedDataBlock output;
    TEST_EXPECT(context, !edit.commit(output, error) && !output.loaded_ready() && edit.is_ready());
    const schema::CDataEditHandle groups = edit.create_collection(CStringView{ "Group" }, CStringView{ "future groups" },
        input, input.object_child(input.root(), CStringView{ "groups" }), error);
    const schema::CDataEditHandle leaves = edit.create_collection(CStringView{ "Leaf" }, CStringView{ "future leaves" },
        input, input.object_child(input.root(), CStringView{ "leaves" }), error);
    TEST_EXPECT(context, groups && leaves);
    const schema::CDataEditHandle child = edit.create_instance(CStringView{ "Report" }, CStringView{ "child" }, input, {}, root, error);
    TEST_EXPECT(context, child);
    const schema::SInstanceSelectionStep title[]{ { schema::EInstanceSelectionStepKind::member, CStringView{ "title" }, 0u } };
    TEST_EXPECT(context, edit.write(root, 0u, title, 1u, input, input.object_child(input.root(), CStringView{ "title" }),
        schema::EInstanceDescendantPolicy::propagate, error));
    TEST_EXPECT(context, edit.commit(output, error));
    schema::CDataBlockView published;
    TEST_EXPECT(context, published.bind(output, {}, error));
    const schema::CDataEntryHandle base = published.find_instance(CStringView{ "Report" }, CStringView{ "created" });
    const schema::CDataEntryHandle inherited = published.find_instance(CStringView{ "Report" }, CStringView{ "child" }, base);
    schema::CDataValueView value, field;
    schema::SDataTextView text;
    TEST_EXPECT(context, published.value(inherited, 0u, value) && published.member(value, CStringView{ "title" }, field) &&
        published.text(field, text) && (text.stored_text == CStringView{ "Changed" }));
    TEST_EXPECT(context, edit.begin(published, {}, error));
    TEST_EXPECT(context, !edit.rename(root, CStringView{ "stale" }, error) && (error.reason == schema::EDataOperationReason::stale_handle));
    const schema::CDataEditHandle current = edit.find(CStringView{ "Report" }, schema::EDataEntryKind::instance, CStringView{ "created" });
    schema::CDataBlockEdit foreign;
    TEST_EXPECT(context, foreign.begin(published, {}, error));
    TEST_EXPECT(context, !foreign.erase(current, error) && (error.reason == schema::EDataOperationReason::stale_handle));
    foreign.cancel();
    TEST_EXPECT(context, edit.write(current, 0u, title, 1u, input, input.object_child(input.root(), CStringView{ "absent" }),
        schema::EInstanceDescendantPolicy::preserve, error));
    const schema::CDataEditHandle removed = edit.find(CStringView{ "Leaf" }, schema::EDataEntryKind::collection, CStringView{ "future leaves" });
    TEST_EXPECT(context, edit.erase(removed, error));
    TEST_EXPECT(context, !edit.rename(removed, CStringView{ "erased" }, error));
    const schema::CDataEditHandle replacement = edit.create_collection(CStringView{ "Leaf" }, CStringView{ "future leaves" },
        input, input.object_child(input.root(), CStringView{ "leaves" }), error);
    TEST_EXPECT(context, replacement);
    schema::CBakedDataBlock repaired;
    //  Reusing a name does not revive the erased target identity.
    TEST_EXPECT(context, !edit.commit(repaired, error) && !repaired.loaded_ready());
    const schema::CDataEditHandle old_groups = edit.find(CStringView{ "Group" }, schema::EDataEntryKind::collection, CStringView{ "future groups" });
    TEST_EXPECT(context, edit.replace_collection(old_groups, input, input.object_child(input.root(), CStringView{ "groups" }), error));
    TEST_EXPECT(context, edit.commit(repaired, error));
    schema::CDataBlockView result;
    TEST_EXPECT(context, result.bind(repaired, {}, error));
    const schema::CDataEntryHandle final_base = result.find_instance(CStringView{ "Report" }, CStringView{ "created" });
    TEST_EXPECT(context, result.value(final_base, 0u, value) && result.member(value, CStringView{ "title" }, field) &&
        result.text(field, text) && text.absent);
    TEST_EXPECT(context, result.value(result.find_instance(CStringView{ "Report" }, CStringView{ "child" }, final_base), 0u, value) &&
        result.member(value, CStringView{ "title" }, field) && result.text(field, text) && (text.stored_text == CStringView{ "Changed" }));
    TEST_EXPECT(context, edit.begin(result, {}, error));
    const schema::CDataEditHandle cancelled = edit.find(CStringView{ "Report" }, schema::EDataEntryKind::instance, CStringView{ "created" });
    TEST_EXPECT(context, edit.erase(cancelled, error));
    edit.cancel();
    TEST_EXPECT(context, result.value(final_base, 0u, value));
}

void raw_fidelity_and_boundaries(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    const schema::CDataEntryHandle leaves = view.find_collection(CStringView{ "Leaf" }, CStringView{ "leaves" });
    schema::CDataValueView value;
    CByteConstView record;
    TEST_EXPECT(context, view.value(leaves, 0u, value) && view.bytes(value, record) && (record.size() == 12u));
    const CByteConstView file = source.file_bytes();
    const std::size_t offset = static_cast<std::size_t>(record.data() - file.data());
    CByteBuffer bytes;
    TEST_EXPECT(context, bytes.allocate(file.size(), 128u) && bytes.set_size(file.size()));
    std::memcpy(bytes.data(), file.data(), file.size());
    constexpr std::uint8_t raw[]{ 2u, 0xaau, 0xbbu, 0xccu, 1u, 0u, 0u, 0u };
    std::memcpy((bytes.data() + offset + 4u), raw, sizeof(raw));
    CBakedDocumentBlock generic;
    schema::CBakedDataBlock mutated;
    schema::SDataBlockDiagnostic storage_error;
    TEST_EXPECT(context, generic.adopt_buffer(std::move(bytes)) && mutated.adopt_local(std::move(generic), storage_error));
    schema::CDataBlockView raw_view;
    schema::SDataOperationDiagnostic error;
    TEST_EXPECT(context, raw_view.bind(mutated, {}, error));
    schema::CBakedDataBlock copied;
    TEST_EXPECT(context, schema::repack_data(raw_view, {}, copied, error));
    schema::CDataBlockView result;
    TEST_EXPECT(context, result.bind(copied, {}, error));
    TEST_EXPECT(context, result.value(result.find_collection(CStringView{ "Leaf" }, CStringView{ "leaves" }), 0u, value) && result.bytes(value, record));
    TEST_EXPECT(context, std::memcmp((record.data() + 4u), raw, sizeof(raw)) == 0);
    CBakedDocumentBlock exported;
    TEST_EXPECT(context, !schema::export_data(result, {}, exported, error) && !exported.is_ready());
    TEST_EXPECT(context, error.reason == schema::EDataOperationReason::unrepresentable_value);
    schema::CBakedTypes types;
    schema::SDiagnostic types_error;
    TEST_EXPECT(context, types.set_block(source) && types.resolve(types_error));
    schema::CBakedInstances instances;
    schema::CBakedBulkData bulk;
    schema::SInstanceDiagnostic instance_error;
    schema::SBulkDataDiagnostic bulk_error;
    TEST_EXPECT(context, !instances.attach_block(source, types, instance_error) &&
        (instance_error.reason == schema::EInstanceLoadReason::reference_context_required));
    TEST_EXPECT(context, !bulk.attach_block(source, types, bulk_error) &&
        (bulk_error.reason == schema::EBulkDataLoadReason::reference_context_required));
    schema::CBakedDataBlock unbound;
    const CByteConstView instance_bytes = source.instance_payload_view(), bulk_bytes = source.bulk_payload_view();
    TEST_EXPECT(context, !unbound.assemble(source.document(), { &instance_bytes, &bulk_bytes }, storage_error) &&
        (storage_error.reason == schema::EDataBlockReason::reference_context_required));
    schema::CLiveBulkData captured;
    TEST_EXPECT(context, captured.initialise(types));
    TEST_EXPECT(context, !captured.capture(CStringView{ "Leaf" }, CStringView{ "unsafe" }, record, 1u, bulk_error) &&
        (bulk_error.reason == schema::EBulkDataLoadReason::reference_context_required));
    const schema::SRemapSourceType remap_source[]{ { types.resolved(), types.resolved()->find_type(CStringView{ "Leaf" }) } };
    schema::CDataRemapPlan remap;
    schema::SRemapDiagnostic remap_error;
    TEST_EXPECT(context, !remap.initialise(remap_source, 1u, *types.resolved(), remap_source[0].type, remap_error) &&
        (remap_error.reason == schema::ERemapReason::reference_context_required));
    schema::CPreparedSchema prepared;
    schema::SPreparationDiagnostic preparation;
    TEST_EXPECT(context, !prepared.prepare(CByteConstView{ reinterpret_cast<const std::uint8_t*>(k_graph), (sizeof(k_graph) - 1u), 1u },
        {}, {}, preparation) && !prepared.descriptor());
}

void independent_storage(tests::TTestContext& context)
{
    //  Build the generic document and the twelve payload bytes independently of
    //  every schema construction/relocation/layout helper.
    for (const std::uint32_t count : { 1u, 0x80000000u })
    {
        const std::string json = R"({"fileType":"morphic-schema-data","version":1,"stripped":true,
            "instancePayloadSize":12,"bulkPayloadSize":0,"retainedText":[],
            "types":{"structures":{"Empty":{"members":[]},"Box":{"members":[
                {"range":{"type":{"kind":"span","element":"Empty"}}}]}}},
            "instances":{"Box":{"root":{"locator":{"offset":0,"valid":true,"size":12},"declaration":{"range":true}}}},
            "data":{"Empty":{"target":{"collectionId":7,"locator":{"offset":0,"valid":true,"size":0,"count":)" +
            std::to_string(count) + "}}}}}";
        CLiveDocument description;
        CBakedDocumentBlock prefix;
        TEST_EXPECT(context, tests::parse_document(json, description) && document_translation::bake(description, prefix));
        const std::size_t offset = (prefix.bytes().size() + 127u) & ~std::size_t{ 127u };
        CByteBuffer bytes;
        TEST_EXPECT(context, bytes.allocate((offset + 12u), 128u) && bytes.set_size(offset + 12u));
        std::memset(bytes.data(), 0, bytes.size());
        std::memcpy(bytes.data(), prefix.bytes().data(), prefix.bytes().size());
        bytes.data()[offset] = 7u;
        //  Little-endian u32 count, including the unsigned high-bit boundary.
        for (unsigned byte = 0u; byte < 4u; ++byte)
        {
            bytes.data()[offset + 8u + byte] = static_cast<std::uint8_t>(count >> (byte << 3u));
        }
        CBakedDocumentBlock generic;
        schema::CBakedDataBlock block;
        schema::SDataBlockDiagnostic storage_error;
        TEST_EXPECT(context, generic.adopt_buffer(std::move(bytes)) && block.adopt_local(std::move(generic), storage_error));
        schema::CDataBlockView view;
        schema::SDataOperationDiagnostic error;
        const bool bound = view.bind(block, {}, error);
        if (!bound && (count == 1u)) std::cerr << "Independent fixture count=" << count << " reason=" << static_cast<unsigned>(error.reason)
            << " stage=" << static_cast<unsigned>(error.stage) << " limit=" << static_cast<unsigned>(error.limit) << " entry=" << error.entry << '\n';
        if (count == 1u)
        {
            TEST_EXPECT(context, bound);
            schema::CDataValueView root, field, empty;
            schema::SDataRangeView range;
            CByteConstView record;
            TEST_EXPECT(context, view.value(view.find_instance(CStringView{ "Box" }, CStringView{ "root" }), 0u, root) &&
                view.member(root, CStringView{ "range" }, field) && view.span(field, range) && (range.count == 1u) &&
                view.element(range, 0u, empty) && view.bytes(empty, record) && !record.size());
        }
        else
        {
            TEST_EXPECT(context, !bound && (error.reason == schema::EDataOperationReason::limit_exceeded) &&
                (error.limit == schema::EDataOperationLimit::records) && (error.requested == (UINT64_C(0x80000000) + 1u)));
        }
    }
}

bool sparse_directory_block(const std::uint32_t middle_id, const std::uint32_t first_target,
    schema::CBakedDataBlock& output)
{
    //  Metadata order and collection-ID order deliberately differ. The payload
    //  is written independently of schema construction and reference relocation.
    const std::string json = R"({"fileType":"morphic-schema-data","version":1,"stripped":true,
        "instancePayloadSize":36,"bulkPayloadSize":12,"retainedText":[],
        "types":{"structures":{"Box":{"members":[{"ranges":{"type":{
            "element":{"kind":"span","element":"u32"},"count":3}}}]}}},
        "instances":{"Box":{"root":{"locator":{"offset":0,"valid":true,"size":36},
            "declaration":{"ranges":[true,true,true]}}}},
        "data":{"u32":{
            "alpha":{"collectionId":50,"locator":{"offset":0,"valid":true,"count":1}},
            "beta":{"collectionId":)" + std::to_string(middle_id) + R"(,"locator":{"offset":4,"valid":true,"count":1}},
            "gamma":{"collectionId":100,"locator":{"offset":8,"valid":true,"count":1}}}}})";
    CLiveDocument description;
    CBakedDocumentBlock prefix;
    if (!tests::parse_document(json, description) || !document_translation::bake(description, prefix))
    {
        return false;
    }
    const std::size_t instance_offset = (prefix.bytes().size() + 127u) & ~std::size_t{ 127u };
    const std::size_t bulk_offset = (instance_offset + 36u + 127u) & ~std::size_t{ 127u };
    CByteBuffer bytes;
    if (!bytes.allocate((bulk_offset + 12u), 128u) || !bytes.set_size(bulk_offset + 12u))
    {
        return false;
    }
    std::memset(bytes.data(), 0, bytes.size());
    std::memcpy(bytes.data(), prefix.bytes().data(), prefix.bytes().size());
    const std::uint32_t targets[]{ first_target, 100u, 50u };
    const std::uint32_t values[]{ 11u, 22u, 33u };
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        schema::write_scalar_bits((bytes.data() + instance_offset + (index * 12u)), 4u, targets[index]);
        schema::write_scalar_bits((bytes.data() + instance_offset + (index * 12u) + 8u), 4u, 1u);
        schema::write_scalar_bits((bytes.data() + bulk_offset + (index << 2u)), 4u, values[index]);
    }
    CBakedDocumentBlock generic;
    schema::SDataBlockDiagnostic diagnostic;
    return generic.adopt_buffer(std::move(bytes)) && output.adopt_local(std::move(generic), diagnostic);
}

void sparse_collection_directory(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    TEST_EXPECT(context, sparse_directory_block(7u, 7u, source));
    if (!source.loaded_ready()) return;
    schema::CDataBlockView view;
    schema::SDataOperationDiagnostic error;
    const bool bound = view.bind(source, {}, error);
    if (!bound)
    {
        std::cerr << "Sparse collection fixture: reason=" << static_cast<unsigned>(error.reason)
            << " stage=" << static_cast<unsigned>(error.stage) << " limit=" << static_cast<unsigned>(error.limit)
            << " entry=" << error.entry << " path=" << error.path << " record=" << error.record
            << " block=" << static_cast<unsigned>(error.block.reason) << '\n';
    }
    TEST_EXPECT(context, bound);
    if (!view.is_ready()) return;
    const auto check = [&context](const schema::CDataBlockView& input)
    {
        schema::CDataValueView root, ranges;
        TEST_EXPECT(context, input.value(input.find_instance(CStringView{ "Box" }, CStringView{ "root" }), 0u, root) &&
            input.member(root, CStringView{ "ranges" }, ranges));
        const char* const names[]{ "beta", "gamma", "alpha" };
        const std::uint32_t expected[]{ 22u, 33u, 11u };
        for (std::uint32_t index = 0u; index < 3u; ++index)
        {
            schema::CDataValueView field, value;
            schema::SDataRangeView range;
            schema::SDataEntryInfo target;
            CByteConstView bytes;
            TEST_EXPECT(context, input.element(ranges, index, field) && input.span(field, range) &&
                (range.count == 1u) && input.entry(range.collection, target) && (target.name == CStringView{ names[index] }) &&
                input.element(range, 0u, value) && input.bytes(value, bytes) &&
                (schema::read_scalar_bits(bytes.data(), bytes.size()) == expected[index]));
        }
    };
    schema::SDataEntryInfo first;
    TEST_EXPECT(context, view.entry(view.entry_at(schema::EDataEntryKind::collection, 0u), first) &&
        (first.name == CStringView{ "alpha" }));
    check(view);
    schema::CBakedDataBlock copied;
    schema::CDataBlockView copied_view;
    TEST_EXPECT(context, schema::repack_data(view, {}, copied, error) && copied_view.bind(copied, {}, error));
    check(copied_view);
    CBakedDocumentBlock exported;
    schema::CSchema restored;
    schema::SSchemaDiagnostic schema_error;
    schema::CBakedDataBlock rebuilt;
    schema::CDataBlockView rebuilt_view;
    TEST_EXPECT(context, schema::export_data(view, {}, exported, error) &&
        restored.copy_from(schema::CSchemaDocumentQuery{ exported.document() }, schema_error) &&
        restored.bake(rebuilt, schema_error) && rebuilt_view.bind(rebuilt, {}, error));
    check(rebuilt_view);
    for (const std::uint32_t invalid_id : { 0u, 50u, 0x80000000u })
    {
        schema::CBakedDataBlock invalid;
        schema::CDataBlockView rejected;
        TEST_EXPECT(context, sparse_directory_block(invalid_id, 7u, invalid));
        TEST_EXPECT(context, !rejected.bind(invalid, {}, error) && !rejected.is_ready() &&
            (error.reason == schema::EDataOperationReason::invalid_reference));
    }
    schema::CBakedDataBlock missing;
    schema::CDataBlockView rejected;
    TEST_EXPECT(context, sparse_directory_block(7u, 8u, missing));
    TEST_EXPECT(context, !rejected.bind(missing, {}, error) && !rejected.is_ready() &&
        (error.reason == schema::EDataOperationReason::invalid_reference));
}

void declaration_contract(tests::TTestContext& context)
{
    schema::CBakedDataBlock empty_domains;
    schema::SSchemaDiagnostic authored_empty;
    TEST_EXPECT(context, tests::bake_schema_document(R"({"types":{"structures":{"Plain":{"members":[{"value":{"type":"u32"}}],
        "decorations":{"kind":"text","type":"span"}}}},"instances":{},"data":{}})", empty_domains, authored_empty));
    schema::CDataBlockView empty_view;
    schema::SDataOperationDiagnostic empty_error;
    schema::CBakedDataBlock repacked_empty;
    TEST_EXPECT(context, empty_view.bind(empty_domains, {}, empty_error) && schema::repack_data(empty_view, {}, repacked_empty, empty_error));
    TEST_EXPECT(context, repacked_empty.has_instances() && repacked_empty.has_bulk_data());
    const char* const invalid[]{
        R"({"kind":"text","nullable":1})", R"({"kind":"text","element":"u8"})",
        R"({"kind":"text","count":4})", R"({"kind":"span"})",
        R"({"kind":"span","element":{"element":"u8","count":2}})",
        R"({"kind":"span","element":"Missing"})", R"({"kind":"span","element":"Box"})",
        R"({"kind":"span","element":"u32","nullable":"true"})"
    };
    for (const char* const descriptor : invalid)
    {
        const std::string json = std::string{ R"({"types":{"structures":{"Box":{"members":[{"value":{"type":)" } +
            descriptor + "}}]}}},\"instances\":{\"Box\":{\"root\":{}}}}";
        schema::CBakedDataBlock output;
        schema::SSchemaDiagnostic error;
        TEST_EXPECT(context, !tests::bake_schema_document(json, output, error) && !output.loaded_ready());
        TEST_EXPECT(context, error.reason != schema::ESchemaReason::none);
    }
    constexpr char defaults[] = R"({"types":{"structures":{"Box":{"members":[
        {"text":{"type":{"element":{"kind":"text","nullable":true},"count":3},"default":["A",{"absent":true},""]}},
        {"ranges":{"type":{"element":{"kind":"span","element":"u32","nullable":true},"count":2},"default":[{"empty":true}]}}
    ]}}},"instances":{"Box":{"root":{}}}})";
    schema::CBakedDataBlock block;
    schema::SSchemaDiagnostic authored;
    const bool built = tests::bake_schema_document(defaults, block, authored);
    if (!built) std::cerr << "Array defaults reason=" << static_cast<unsigned>(authored.reason) << " type=" << static_cast<unsigned>(authored.types.reason)
        << " operation=" << static_cast<unsigned>(authored.operation.reason) << " entry=" << authored.operation.entry << '\n';
    TEST_EXPECT(context, built);
    schema::CDataBlockView view;
    schema::SDataOperationDiagnostic error;
    TEST_EXPECT(context, view.bind(block, {}, error));
    schema::CDataValueView root, field, element;
    schema::SDataTextView text;
    schema::SDataRangeView range;
    TEST_EXPECT(context, view.value(view.find_instance(CStringView{ "Box" }, CStringView{ "root" }), 0u, root));
    TEST_EXPECT(context, view.member(root, CStringView{ "text" }, field) && view.element(field, 1u, element) && view.text(element, text) && text.absent);
    TEST_EXPECT(context, view.element(field, 2u, element) && view.text(element, text) && !text.absent && !text.stored_text.length());
    TEST_EXPECT(context, view.member(root, CStringView{ "ranges" }, field) && view.element(field, 0u, element) && view.span(element, range) && !range.absent && !range.count);
    TEST_EXPECT(context, view.element(field, 1u, element) && view.span(element, range) && range.absent);

    std::string explicit_values{ defaults };
    explicit_values.replace(explicit_values.find("\"root\":{}"), 9u,
        R"("root":{"declaration":{"text":[{"absent":true},"explicit",""],"ranges":[{"empty":true},{"absent":true}]}})");
    schema::CBakedDataBlock explicit_block;
    TEST_EXPECT(context, tests::bake_schema_document(explicit_values, explicit_block, authored));
    schema::CDataBlockView explicit_view;
    TEST_EXPECT(context, explicit_view.bind(explicit_block, {}, error));
    CBakedDocumentBlock exported;
    TEST_EXPECT(context, schema::export_data(explicit_view, {}, exported, error));
    CDocumentWriteOptions options;
    options.mode = EDocumentWriteMode::strict_json;
    const CDocumentWriteResult written = document_writer::write(exported.document(), options);
    schema::CBakedDataBlock reparsed;
    TEST_EXPECT(context, written.report.succeeded() && tests::bake_schema_document(
        std::string{ reinterpret_cast<const char*>(written.output.data()), written.report.logical_text_byte_size }, reparsed, authored));
    explicit_view.clear();
    TEST_EXPECT(context, explicit_view.bind(reparsed, {}, error) &&
        explicit_view.value(explicit_view.find_instance(CStringView{ "Box" }, CStringView{ "root" }), 0u, root));
    TEST_EXPECT(context, explicit_view.member(root, CStringView{ "text" }, field) && explicit_view.element(field, 0u, element) &&
        explicit_view.text(element, text) && text.absent);
    const std::size_t absent = explicit_values.find("[{\"absent\":true},\"explicit\"");
    explicit_values.replace(absent, 16u, "[{\"name\":\"bad\"}");
    schema::CBakedDataBlock invalid_array;
    TEST_EXPECT(context, !tests::bake_schema_document(explicit_values, invalid_array, authored));
}

void expect_validation(tests::TTestContext& context, const std::string& text, const bool expected)
{
    CLiveDocument document;
    schema::CSchema authored;
    schema::SSchemaDiagnostic validated;
    const bool admitted = tests::parse_document(text, document) && authored.try_adopt(std::move(document), validated);
    TEST_EXPECT(context, admitted);
    if (!admitted) return;
    TEST_EXPECT(context, authored.validate(validated) == expected);
    for (const bool bounded : { false, true })
    {
        schema::CBakedDataBlock output;
        schema::SSchemaDiagnostic baked;
        const bool success = bounded ? authored.bake(output, {}, baked) : authored.bake(output, baked);
        TEST_EXPECT(context, (success == expected) && (output.loaded_ready() == expected));
        TEST_EXPECT(context, (validated.reason == baked.reason) && (validated.stage == baked.stage) &&
            (validated.first == baked.first) && (validated.second == baked.second));
        TEST_EXPECT(context, (validated.types.reason == baked.types.reason) && (validated.types.occurrence == baked.types.occurrence) &&
            (validated.instances.reason == baked.instances.reason) && (validated.instances.occurrence == baked.instances.occurrence) &&
            (validated.bulk_data.reason == baked.bulk_data.reason) && (validated.bulk_data.occurrence == baked.bulk_data.occurrence));
        TEST_EXPECT(context, (validated.operation.reason == baked.operation.reason) && (validated.operation.stage == baked.operation.stage) &&
            (validated.operation.limit == baked.operation.limit) && (validated.operation.requested == baked.operation.requested) &&
            (validated.operation.allowed == baked.operation.allowed) && (std::strcmp(validated.operation.entry, baked.operation.entry) == 0) &&
            (std::strcmp(validated.operation.path, baked.operation.path) == 0));
    }
}

void validation_agreement(tests::TTestContext& context)
{
    expect_validation(context, k_graph, true);
    struct SCase { const char* from; const char* to; bool valid; };
    const SCase cases[]{
        { "\"first\":0,\"count\":2", "\"first\":2,\"count\":0", true },
        { "\"collection\":\"groups\"", "\"collection\":\"missing\"", false },
        { "\"collection\":\"groups\"", "\"collection\":\"leaves\"", false },
        { "\"first\":0,\"count\":2", "\"first\":2,\"count\":1", false },
        { "\"label\":\"A\"", "\"label\":{\"absent\":true}", false },
        { "\"value\":1", "\"value\":-1", false },
        { "\"element\":\"Leaf\"", "\"element\":\"Group\"", false }
    };
    for (const SCase& entry : cases)
    {
        std::string text{ k_graph };
        const std::size_t position = text.find(entry.from);
        TEST_EXPECT(context, position != std::string::npos);
        if (position == std::string::npos) continue;
        text.replace(position, std::strlen(entry.from), entry.to);
        expect_validation(context, text, entry.valid);
    }
    const std::string definitions{ R"({"types":{"structures":{"Text":{"members":[{"value":{"type":{"kind":"text"},"default":"kept"}}]}}}})" };
    expect_validation(context, definitions, true);
    std::string empty_sections = definitions;
    empty_sections.insert((empty_sections.size() - 1u), R"(,"instances":{},"data":{})");
    expect_validation(context, empty_sections, true);

    //  The payload fits the default result cap; its complete file does not.
    const schema::SDataOperationLimits limits;
    const std::string large = std::string{ R"({"types":{"structures":{"Large":{"detail":{"size":)" } +
        std::to_string(limits.result_bytes) + R"(},"members":[{"value":{"type":{"kind":"text"},"offset":)" +
        std::to_string(limits.result_bytes - sizeof(schema::STextReference)) + R"(}}]}}},"instances":{"Large":{"root":{}}}})";
    expect_validation(context, large, false);
    CLiveDocument document;
    schema::CSchema authored;
    schema::SSchemaDiagnostic error;
    TEST_EXPECT(context, tests::parse_document(large, document) && authored.try_adopt(std::move(document), error));
    TEST_EXPECT(context, !authored.validate(error) && (error.reason == schema::ESchemaReason::storage_limit) &&
        (error.operation.reason == schema::EDataOperationReason::limit_exceeded) &&
        (error.operation.limit == schema::EDataOperationLimit::result_bytes) &&
        (error.operation.requested > limits.result_bytes) && (error.operation.allowed == limits.result_bytes));
}

void validation_allocations(tests::TTestContext& context)
{
    CLiveDocument document;
    schema::CSchema authored;
    schema::SSchemaDiagnostic error;
    const bool admitted = tests::parse_document(k_graph, document) && authored.try_adopt(std::move(document), error);
    TEST_EXPECT(context, admitted);
    if (!admitted) return;
    CBakedDocumentBlock before;
    TEST_EXPECT(context, authored.save(before, error));
    std::size_t allocations[2]{}, requested[2]{};
    std::size_t published_bytes{};
    for (unsigned phase = 0u; phase < 2u; ++phase)
    {
        tests::TAllocationFailureFixture injection;
        memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        {
            const tests::TMemoryContextScope scope{ &memory };
            schema::CBakedDataBlock output;
            TEST_EXPECT(context, ((phase == 0u) ? authored.validate(error) : authored.bake(output, error)));
            allocations[phase] = injection.attempts;
            requested[phase] = injection.bytes;
            if (phase == 0u) TEST_EXPECT(context, memory.is_attribution_empty());
            else published_bytes = memory.condition_bytes(schema::detail::k_data_block_alignment, output.file_bytes().size());
        }
        TEST_EXPECT(context, memory.is_attribution_empty());
    }
    TEST_EXPECT(context, allocations[1] > allocations[0]);
    TEST_EXPECT(context, (published_bytes != 0u) && (requested[1] >= (requested[0] + published_bytes)));
    std::cout << "Reference validation allocations: validate=" << allocations[0] << '/' << requested[0]
        << " bytes, bake=" << allocations[1] << '/' << requested[1] << " bytes\n";

    //  Reject the first allocation beyond validation: only publication reaches it.
    for (const bool publish : { false, true })
    {
        tests::TAllocationFailureFixture injection;
        injection.fail_on = allocations[0];
        memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        {
            const tests::TMemoryContextScope scope{ &memory };
            schema::CBakedDataBlock output;
            const bool success = publish ? authored.bake(output, error) : authored.validate(error);
            TEST_EXPECT(context, (success == !publish) && (injection.failed == publish) && !output.loaded_ready());
            if (publish)
            {
                TEST_EXPECT(context, (error.reason == schema::ESchemaReason::allocation_failed) &&
                    (error.operation.reason == schema::EDataOperationReason::allocation_failed) &&
                    (error.operation.stage == schema::EDataOperationStage::publication));
            }
            TEST_EXPECT(context, memory.is_attribution_empty());
        }
        TEST_EXPECT(context, memory.is_attribution_empty());
    }

    bool completed{};
    for (std::size_t attempt = 0u; (attempt < 1024u) && !completed; ++attempt)
    {
        tests::TAllocationFailureFixture injection;
        injection.fail_on = attempt;
        memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        {
            const tests::TMemoryContextScope scope{ &memory };
            const bool success = authored.validate(error);
            completed = success && !injection.failed;
            TEST_EXPECT(context, memory.is_attribution_empty() && (memory::get_ambient_memory_context() == &memory));
            if (!success)
            {
                TEST_EXPECT(context, injection.failed && (error.reason != schema::ESchemaReason::none) &&
                    (error.operation.reason == schema::EDataOperationReason::allocation_failed));
                injection.fail_on = SIZE_MAX;
                TEST_EXPECT(context, authored.validate(error) && (error.reason == schema::ESchemaReason::none));
            }
        }
        TEST_EXPECT(context, memory.is_attribution_empty());
    }
    TEST_EXPECT(context, completed);
    CBakedDocumentBlock after;
    TEST_EXPECT(context, authored.save(after, error) && (before.bytes().size() == after.bytes().size()) &&
        (std::memcmp(before.bytes().data(), after.bytes().data(), before.bytes().size()) == 0));
}

void publication_failures(tests::TTestContext& context)
{
    schema::CBakedDataBlock source;
    schema::CDataBlockView view;
    if (!fixture(context, source, view)) return;
    CLiveDocument input;
    schema::CSchema authored;
    schema::SSchemaDiagnostic schema_error;
    TEST_EXPECT(context, tests::parse_document(k_graph, input) && authored.try_adopt(std::move(input), schema_error));
    for (unsigned phase = 0u; phase < 4u; ++phase)
    {
        bool complete{};
        for (std::size_t attempt = 0u; attempt < 1024u; ++attempt)
        {
            tests::TAllocationFailureFixture injection;
            memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext memory{ allocator };
            {
                const tests::TMemoryContextScope scope{ &memory };
                schema::CDataBlockEdit edit;
                schema::SDataOperationDiagnostic error;
                schema::CBakedDataBlock output;
                CBakedDocumentBlock exported;
                schema::CDataEditHandle handle;
                if (phase >= 2u)
                {
                    TEST_EXPECT(context, edit.begin(view, {}, error));
                    handle = edit.find(CStringView{ "Leaf" }, schema::EDataEntryKind::collection, CStringView{ "spare" });
                }
                injection.fail_on = injection.attempts + attempt;
                bool success{};
                if (phase == 0u) success = schema::export_data(view, {}, exported, error);
                else if (phase == 1u)
                {
                    success = authored.bake(output, {}, schema_error);
                    error = schema_error.operation;
                }
                else if (phase == 2u) success = edit.rename(handle, CStringView{ "renamed" }, error);
                else success = edit.commit(output, error);
                if (!injection.failed) complete = success;
                if (!success)
                {
                    TEST_EXPECT(context, injection.failed && !output.loaded_ready() && !exported.is_ready());
                    TEST_EXPECT(context, error.reason == schema::EDataOperationReason::allocation_failed);
                    if (phase >= 2u)
                    {
                        injection.fail_on = SIZE_MAX;
                        TEST_EXPECT(context, edit.is_ready() && edit.find(CStringView{ "Leaf" }, schema::EDataEntryKind::collection, CStringView{ "spare" }));
                        TEST_EXPECT(context, edit.commit(output, error));
                    }
                }
            }
            TEST_EXPECT(context, memory.is_attribution_empty());
            if (complete) break;
        }
        TEST_EXPECT(context, complete);
    }
}

}   // namespace

void run_schema_reference_validation_cases(tests::TTestContext& context)
{
    malformed(context);
    dependency_identity(context);
    rollback_and_limits(context);
    owner_lifetimes(context);
    nested_operation_scopes(context);
    shared_checked_state_limits(context);
    queries_have_no_lifetime_budget(context);
    allocation_failures(context);
    view_bind_failures(context);
    edit_begin_failures(context);
    invalid_operation_limits(context);
    transactions(context);
    raw_fidelity_and_boundaries(context);
    independent_storage(context);
    sparse_collection_directory(context);
    declaration_contract(context);
    validation_agreement(context);
    validation_allocations(context);
    publication_failures(context);
}
