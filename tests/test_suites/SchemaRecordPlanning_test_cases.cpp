
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaRecordPlanning_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    17 Nov 26
//
//  Structural planning stays transactional and separate from payload admission.

#include "tests/test_suites/SchemaRecordPlanning_test_cases.hpp"
#include "tests/support/byte_helpers.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "schema/records/baked_record_plan.hpp"
#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"
#include "data_model/live_document.hpp"

#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using namespace schema;
using namespace schema::detail;

constexpr char k_types[] = R"({"types":{"structures":{
    "V":{"members":[{"x":{"type":"u16","default":7}}]},
    "Empty":{"members":[]},"Ref":{"members":[{"text":{"type":{"kind":"text"}}}]}
}}})";

constexpr char k_instances[] = R"({"stripped":true,"instances":{
    "V":{"base":{"declaration":{"x":true},"locator":{"offset":2,"valid":true},
        "specialisation":{"child":{"locator":{"offset":6,"valid":true}}}},
        "next":{"locator":{"offset":10,"valid":true}}},
    "Empty":{"zero":{"locator":{"offset":0,"valid":true}}}
}})";

constexpr char k_bulk[] = R"({"stripped":true,"data":{
    "V":{"first":{"locator":{"offset":2,"valid":true,"count":2}},
        "next":{"locator":{"offset":8,"valid":true,"size":4}}},
    "Empty":{"zero":{"locator":{"offset":0,"valid":true,"count":3,"size":0}}}
}})";

bool bake(const char* const text, CBakedDocumentBlock& output)
{
    CLiveDocument source;
    return tests::parse_document(text, source) && document_translation::bake(source, output);
}

CBakedValueIndex at(const CBakedDocument& document, const std::initializer_list<const char*> path)
{
    CBakedValueIndex result = document.root();
    for (const char* const name : path)
    {
        result = document.object_child(result, CStringView{ name });
    }
    return result;
}

bool same_record(const SBakedInstanceRecord& a, const SBakedInstanceRecord& b)
{
    return (a.entry == b.entry) && (a.declaration == b.declaration) && (a.type == b.type) &&
        (a.parent == b.parent) && (a.offset == b.offset) && (a.extent == b.extent);
}

bool same_record(const SBakedBulkRecord& a, const SBakedBulkRecord& b)
{
    return (a.entry == b.entry) && (a.type == b.type) && (a.count == b.count) &&
        (a.offset == b.offset) && (a.extent == b.extent) && (a.stride == b.stride);
}

template<class TPlan>
class TPlanSnapshot
{
    using TRecord = std::remove_reference_t<decltype(std::declval<TPlan&>().records[0u])>;
public:
    explicit TPlanSnapshot(const TPlan& source) :
        m_pointer(source.records.data()), m_payload_size(source.payload_size),
        m_records(source.records.data(), (source.records.data() + source.records.size())) {}

    bool unchanged(const TPlan& source) const
    {
        if ((source.records.data() != m_pointer) || (source.payload_size != m_payload_size) ||
            (source.records.size() != m_records.size()))
        {
            return false;
        }
        for (std::size_t index = 0u; index < m_records.size(); ++index)
        {
            if (!same_record(source.records[index], m_records[index]))
            {
                return false;
            }
        }
        return true;
    }

private:
    const TRecord* m_pointer;
    std::size_t m_payload_size;
    std::vector<TRecord> m_records;
};

void record_contents(tests::TTestContext& context, const CResolvedTypes& types,
    const CBakedDocument& instances, const CBakedDocument& bulk)
{
    SBakedRecordPlanDiagnostic diagnostic;
    SBakedInstancePlan instance_plan;
    SBakedBulkPlan bulk_plan;
    TEST_EXPECT(context, plan_baked_instances(instances, types, 16u, instance_plan, diagnostic));
    TEST_EXPECT(context, (instance_plan.records.size() == 4u) && (instance_plan.payload_size == 12u));
    if (instance_plan.records.size() != 4u) return;
    const SBakedInstanceRecord& base = instance_plan.records[0u];
    const SBakedInstanceRecord& child = instance_plan.records[1u];
    const SBakedInstanceRecord& sibling = instance_plan.records[2u];
    const SBakedInstanceRecord& empty = instance_plan.records[3u];
    TEST_EXPECT(context, (base.entry == at(instances, { "instances", "V", "base" })) &&
        (base.declaration == at(instances, { "instances", "V", "base", "declaration" })) &&
        (base.type == types.find_type(CStringView{ "V" })) && (base.parent == UINT32_MAX) &&
        (base.offset == 2u) && (base.extent == 2u));
    TEST_EXPECT(context, (child.entry == at(instances, { "instances", "V", "base", "specialisation", "child" })) &&
        (child.parent == 0u) && !child.declaration && (child.offset == 6u) && (child.extent == 2u));
    TEST_EXPECT(context, (sibling.entry == at(instances, { "instances", "V", "next" })) &&
        (sibling.parent == UINT32_MAX) && (sibling.offset == 10u));
    TEST_EXPECT(context, (empty.entry == at(instances, { "instances", "Empty", "zero" })) &&
        (empty.parent == UINT32_MAX) && (empty.offset == 0u) && (empty.extent == 0u));

    TEST_EXPECT(context, plan_baked_bulk_data(bulk, types, 16u, bulk_plan, diagnostic));
    TEST_EXPECT(context, (bulk_plan.records.size() == 3u) && (bulk_plan.payload_size == 12u));
    if (bulk_plan.records.size() != 3u) return;
    const SBakedBulkRecord& first = bulk_plan.records[0u];
    const SBakedBulkRecord& derived = bulk_plan.records[1u];
    const SBakedBulkRecord& zero = bulk_plan.records[2u];
    TEST_EXPECT(context, (first.entry == at(bulk, { "data", "V", "first" })) &&
        (first.type == types.find_type(CStringView{ "V" })) && (first.count == 2u) &&
        (first.offset == 2u) && (first.extent == 4u) && (first.stride == 2u));
    TEST_EXPECT(context, (derived.entry == at(bulk, { "data", "V", "next" })) &&
        (derived.count == 2u) && (derived.offset == 8u) && (derived.extent == 4u) && (derived.stride == 2u));
    TEST_EXPECT(context, (zero.entry == at(bulk, { "data", "Empty", "zero" })) &&
        (zero.count == 3u) && (zero.offset == 0u) && (zero.extent == 0u) && (zero.stride == 0u));
}

template<class TPlan, class TPlanner>
void transactional_plan(tests::TTestContext& context, const CResolvedTypes& types,
    const CBakedDocumentBlock& source, const char* const invalid_text, const TPlanner& planner)
{
    CBakedDocumentBlock invalid, absent, empty;
    TEST_EXPECT(context, bake(invalid_text, invalid));
    TEST_EXPECT(context, bake(R"({"stripped":true})", absent));
    TEST_EXPECT(context, bake(R"({"stripped":true,"instances":{},"data":{}})", empty));
    if (!invalid.is_ready() || !absent.is_ready() || !empty.is_ready())
    {
        return;
    }
    SBakedRecordPlanDiagnostic diagnostic;
    TPlan output;
    TEST_EXPECT(context, planner(source.document(), types, 16u, output, diagnostic));
    if (!output.records.size()) return;
    const TPlanSnapshot<TPlan> snapshot{ output };
    diagnostic = { EBakedRecordPlanReason::unknown_type, source.document().root() };
    TEST_EXPECT(context, !planner(invalid.document(), types, 16u, output, diagnostic));
    TEST_EXPECT(context, (diagnostic.reason == EBakedRecordPlanReason::overlap) && snapshot.unchanged(output));
    const CBakedDocument unavailable;
    const CResolvedTypes unresolved;
    TEST_EXPECT(context, !planner(unavailable, types, 16u, output, diagnostic));
    TEST_EXPECT(context, (diagnostic.reason == EBakedRecordPlanReason::invalid_input) && snapshot.unchanged(output));
    TEST_EXPECT(context, !planner(source.document(), unresolved, 16u, output, diagnostic));
    TEST_EXPECT(context, (diagnostic.reason == EBakedRecordPlanReason::invalid_input) && snapshot.unchanged(output));
    for (const CBakedDocumentBlock* const replacement : { &absent, &empty })
    {
        TEST_EXPECT(context, planner(source.document(), types, 16u, output, diagnostic));
        diagnostic = { EBakedRecordPlanReason::unknown_type, source.document().root() };
        TEST_EXPECT(context, planner(replacement->document(), types, 0u, output, diagnostic));
        TEST_EXPECT(context, (output.records.size() == 0u) && (output.payload_size == 0u) &&
            (diagnostic.reason == EBakedRecordPlanReason::none) && !diagnostic.occurrence);
    }

    const std::string source_snapshot{ reinterpret_cast<const char*>(source.bytes().data()), source.bytes().size() };
    bool completed{};
    for (std::size_t fail_offset = 0u; (fail_offset < 256u) && !completed; ++fail_offset)
    {
        tests::TAllocationFailureFixture failure;
        memory::CMemoryAllocator allocator{ &failure, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory_context{ allocator };
        {
            tests::TMemoryContextScope scope{ &memory_context };
            TPlan retained;
            TEST_EXPECT(context, planner(source.document(), types, 16u, retained, diagnostic));
            if (retained.records.size())
            {
                const TPlanSnapshot<TPlan> previous{ retained };
                failure.fail_on = failure.attempts + fail_offset;
                diagnostic = { EBakedRecordPlanReason::unknown_type, source.document().root() };
                const bool success = planner(source.document(), types, 16u, retained, diagnostic);
                completed = success && !failure.failed;
                if (!success)
                {
                    TEST_EXPECT(context, failure.failed && (diagnostic.reason == EBakedRecordPlanReason::allocation_failed));
                    TEST_EXPECT(context, previous.unchanged(retained));
                }
                else
                {
                    TEST_EXPECT(context, (diagnostic.reason == EBakedRecordPlanReason::none) && !diagnostic.occurrence &&
                        (retained.payload_size == 12u));
                }
            }
        }
        TEST_EXPECT(context, memory_context.is_attribution_empty());
        TEST_EXPECT(context, tests::same_bytes(source.bytes(), source_snapshot));
    }
    TEST_EXPECT(context, completed);
}

void diagnostic_boundaries(tests::TTestContext& context, CBakedTypes& types)
{
    alignas(128) std::uint8_t payload[16]{};
    const char* const instance_cases[]{
        R"({"stripped":true,"instances":{"V":{"bad":{"declaration":{"missing":true}}}}})",
        R"({"stripped":true,"instances":{"V":{"base":{"locator":{"offset":0,"valid":true}},
            "bad":{"locator":{"offset":1,"valid":true}}}}})"
    };
    const EBakedRecordPlanReason instance_reasons[]{ EBakedRecordPlanReason::invalid_declaration, EBakedRecordPlanReason::overlap };
    const EInstanceLoadReason role_reasons[]{ EInstanceLoadReason::invalid_declaration, EInstanceLoadReason::overlap };
    for (std::size_t index = 0u; index < 2u; ++index)
    {
        CBakedDocumentBlock block;
        TEST_EXPECT(context, bake(instance_cases[index], block));
        if (!block.is_ready()) continue;
        const CBakedValueIndex expected = index ? at(block.document(), { "instances", "V", "bad", "locator" }) :
            at(block.document(), { "instances", "V", "bad", "declaration", "missing" });
        SBakedInstancePlan plan;
        SBakedRecordPlanDiagnostic error;
        TEST_EXPECT(context, !plan_baked_instances(block.document(), *types.resolved(), sizeof(payload), plan, error));
        TEST_EXPECT(context, (error.reason == instance_reasons[index]) && (error.occurrence == expected));
        CBakedInstances role;
        SInstanceDiagnostic role_error;
        TEST_EXPECT(context, role.set_document(block.document()));
        TEST_EXPECT(context, !role.attach_types(types, CByteConstView{ payload, sizeof(payload), 128u }, role_error));
        TEST_EXPECT(context, (role_error.reason == role_reasons[index]) &&
            (SInstanceHandleAccess::occurrence(role_error.occurrence).baked == expected));
    }
    CBakedDocumentBlock block;
    TEST_EXPECT(context, bake(R"({"stripped":true,"data":{"V":{"bad":{
        "locator":{"offset":0,"valid":false,"count":0}}}}})", block));
    if (!block.is_ready()) return;
    const CBakedValueIndex expected = at(block.document(), { "data", "V", "bad", "locator", "count" });
    SBakedBulkPlan plan;
    SBakedRecordPlanDiagnostic error;
    TEST_EXPECT(context, !plan_baked_bulk_data(block.document(), *types.resolved(), sizeof(payload), plan, error));
    TEST_EXPECT(context, (error.reason == EBakedRecordPlanReason::invalid_count) && (error.occurrence == expected));
    CBakedBulkData role;
    SBulkDataDiagnostic role_error;
    TEST_EXPECT(context, role.set_document(block.document()));
    TEST_EXPECT(context, !role.attach_types(types, CByteConstView{ payload, sizeof(payload), 128u }, role_error));
    TEST_EXPECT(context, (role_error.reason == EBulkDataLoadReason::invalid_count) &&
        (SBulkDataHandleAccess::occurrence(role_error.occurrence).baked == expected));
}

void payload_admission_is_separate(tests::TTestContext& context, CBakedTypes& types)
{
    CBakedDocumentBlock inherited, references;
    TEST_EXPECT(context, bake(R"({"stripped":true,"instances":{"V":{"base":{
        "locator":{"offset":0,"valid":true}}}}})", inherited));
    TEST_EXPECT(context, bake(R"({"stripped":true,
        "instances":{"Ref":{"base":{"locator":{"offset":0,"valid":true}}}},
        "data":{"Ref":{"array":{"locator":{"offset":0,"valid":true,"count":1}}}}})", references));
    if (!inherited.is_ready() || !references.is_ready())
    {
        return;
    }
    SBakedInstancePlan instances;
    SBakedBulkPlan bulk;
    SBakedRecordPlanDiagnostic diagnostic;
    TEST_EXPECT(context, plan_baked_instances(inherited.document(), *types.resolved(), 2u, instances, diagnostic));
    alignas(128) std::uint8_t bytes[4]{};
    CBakedInstances rejected_values;
    SInstanceDiagnostic instance_error;
    TEST_EXPECT(context, rejected_values.set_document(inherited.document()));
    TEST_EXPECT(context, !rejected_values.attach_types(types, CByteConstView{ bytes, 2u, 128u }, instance_error));
    TEST_EXPECT(context, (instance_error.reason == EInstanceLoadReason::inheritance_mismatch));

    TEST_EXPECT(context, plan_baked_instances(references.document(), *types.resolved(), 4u, instances, diagnostic));
    TEST_EXPECT(context, plan_baked_bulk_data(references.document(), *types.resolved(), 4u, bulk, diagnostic));
    TEST_EXPECT(context, (instances.records.size() == 1u) && (bulk.records.size() == 1u));
    CBakedInstances rejected_instances;
    CBakedBulkData rejected_bulk;
    SBulkDataDiagnostic bulk_error;
    TEST_EXPECT(context, rejected_instances.set_document(references.document()) && rejected_bulk.set_document(references.document()));
    TEST_EXPECT(context, !rejected_instances.attach_types(types, CByteConstView{ bytes, sizeof(bytes), 128u }, instance_error));
    TEST_EXPECT(context, !rejected_bulk.attach_types(types, CByteConstView{ bytes, sizeof(bytes), 128u }, bulk_error));
    TEST_EXPECT(context, (instance_error.reason == EInstanceLoadReason::reference_context_required) &&
        (bulk_error.reason == EBulkDataLoadReason::reference_context_required));
}

}   // namespace

void run_schema_record_planning_cases(tests::TTestContext& context)
{
    CBakedDocumentBlock definitions, instances, bulk;
    TEST_EXPECT(context, bake(k_types, definitions) && bake(k_instances, instances) && bake(k_bulk, bulk));
    CBakedTypes types;
    SDiagnostic diagnostic;
    TEST_EXPECT(context, types.set_document(definitions.document()) && types.resolve(diagnostic));
    if (!types.resolved_ready() || !instances.is_ready() || !bulk.is_ready())
    {
        return;
    }
    record_contents(context, *types.resolved(), instances.document(), bulk.document());
    transactional_plan<SBakedInstancePlan>(context, *types.resolved(), instances,
        R"({"stripped":true,"instances":{"V":{"base":{"locator":{"offset":0,"valid":true}},
            "bad":{"locator":{"offset":0,"valid":true}}}}})", plan_baked_instances);
    transactional_plan<SBakedBulkPlan>(context, *types.resolved(), bulk,
        R"({"stripped":true,"data":{"V":{"first":{"locator":{"offset":0,"valid":true,"count":2}},
            "bad":{"locator":{"offset":0,"valid":true,"count":1}}}}})", plan_baked_bulk_data);
    diagnostic_boundaries(context, types);
    payload_admission_is_separate(context, types);
}
