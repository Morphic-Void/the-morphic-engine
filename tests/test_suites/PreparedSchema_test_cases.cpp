
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PreparedSchema_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    06 Nov 26
//
//  Prepared ownership, checked limits and portable input installation validation.

#include "schema/prepared_schema.hpp"
#include "schema/value_codec.hpp"
#include "platform/runtime/input_configuration.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/input_schema.hpp"
#include "tests/support/test_context.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <utility>

namespace
{
using tests::TTestContext;
using namespace schema;

constexpr char k_value_schema[] = R"({"types":{"structures":{"Value":{"members":[{"n":{"type":"u8","default":2}}]}}},
    "instances":{"Value":{"base":{"specialisation":{"child":{"declaration":{"n":7}}}}}},
    "data":{"u32":{"code":{"data":[1,2,3]}}}})";

bool prepare(CPreparedSchema& owner, const std::string& source, SPreparationDiagnostic& diagnostic,
    const SPreparationLimits limits = {})
{
    const bool result = owner.prepare({ reinterpret_cast<const std::uint8_t*>(source.data()), source.size() }, {}, limits, diagnostic);
    if (!result && (diagnostic.status != EPreparationStatus::limit_exceeded))
    {
        std::printf("Prepared schema: status %u, stage %u, reason %u, line %u, column %u\n",
            static_cast<unsigned>(diagnostic.status), diagnostic.stage, diagnostic.reason, diagnostic.line, diagnostic.column);
    }
    return result;
}

void prepared_ownership(TTestContext& ctx)
{
    CPreparedSchema owner;
    SPreparationDiagnostic diagnostic;
    std::string source = k_value_schema;
    TEST_EXPECT(ctx, prepare(owner, source, diagnostic));
    source.assign(source.size(), 'x');
    CPreparedSchemaView view{ owner.descriptor() };
    TEST_EXPECT(ctx, (view.is_ready() && (view.instance_count() == 2u) && (view.bulk_count() == 1u)));
    if (!view.is_ready()) return;
    const SPreparedInstance* const base = view.instance_at(0u);
    const SPreparedInstance* const child = view.instance_at(1u);
    const SPreparedBulk* const bulk = view.bulk_at(0u);
    TEST_EXPECT(ctx, (base && child && bulk));
    if (!base || !child || !bulk)
    {
        return;
    }
    TEST_EXPECT(ctx, ((base->bytes.data()[0] == 2u) && (child->bytes.data()[0] == 7u) && (child->parent == 0u)));
    TEST_EXPECT(ctx, ((child->name.length() == 5u) && (std::memcmp(child->name.cstring(), "child", 5u) == 0)));
    TEST_EXPECT(ctx, ((bulk->count == 3u) && (bulk->stride == 4u) && (bulk->bytes.size() == 12u)));
    TEST_EXPECT(ctx, (view.instances_of(base->type).count == 2u));
    TEST_EXPECT(ctx, (view.bulk_of(bulk->type).count == 1u));
    TEST_EXPECT(ctx, (!view.instance_at(2u) && !view.bulk_at(1u) && !view.instances_of({}).count));
    const std::uint64_t retained = owner.descriptor()->storage_bytes;
    for (unsigned index = 0u; index < 100u; ++index)
    {
        TEST_EXPECT(ctx, (view.instance_at(1u) == child));
    }
    TEST_EXPECT(ctx, ((retained > owner.descriptor()->payload_bytes) && (owner.descriptor()->storage_bytes == retained)));
    owner.clear();
    TEST_EXPECT(ctx, !owner.descriptor());
}

void explicit_writer_budgets(TTestContext& ctx)
{
    using namespace schema::detail;
    CLiveDocument source, values;
    CResolvedTypes types;
    SDiagnostic diagnostic;
    TEST_EXPECT(ctx, tests::parse_document(k_value_schema, source));
    TEST_EXPECT(ctx, types.resolve(source, diagnostic));
    TEST_EXPECT(ctx, tests::parse_document(R"({"value":{"n":7}})", values));
    const CTypesIndex type = types.find_type(CStringView{ "Value" });
    const CDocumentRead document{ values };
    const SOccurrence declaration = document.object_child(document.root(), CStringView{ "value" });
    for (unsigned operation = 0u; operation < 4u; ++operation)
    {
        for (const std::uint64_t allowance : { 1u, 2u })
        {
            CValueWorkBudget work_budget{ allowance };
            const std::uint8_t baseline[]{ 2u };
            std::uint8_t bytes[]{ 7u };
            SValueDiagnostic error;
            bool matches = false;
            bool success = false;
            switch (operation)
            {
                case 0u:
                {
                    success = construct_value(work_budget, types, document, type, declaration, bytes, sizeof(bytes),
                        EConstructionMode::instance, error);
                    break;
                }
                case 1u:
                {
                    success = construct_alternative(work_budget, types, document, type, declaration, baseline, sizeof(baseline),
                        bytes, sizeof(bytes), error);
                    break;
                }
                case 2u:
                {
                    success = check_value(work_budget, types, document, type, declaration, bytes, sizeof(bytes),
                        EConstructionMode::instance, matches, error);
                    break;
                }
                default:
                {
                    success = check_alternative(work_budget, types, document, type, declaration, baseline, sizeof(baseline),
                        bytes, sizeof(bytes), matches, error);
                    break;
                }
            }
            TEST_EXPECT(ctx, (success == (allowance == 2u)));
            TEST_EXPECT(ctx, work_budget.exhausted());
            TEST_EXPECT(ctx, (error.reason == (success ? EReason::none : EReason::storage_limit)));
            if (success)
            {
                TEST_EXPECT(ctx, (bytes[0] == 7u));
                if (operation >= 2u) TEST_EXPECT(ctx, matches);
            }
            //  A second writer must see the exhausted allowance, even after success.
            TEST_EXPECT(ctx, !construct_value(work_budget, types, document, type, declaration, bytes, sizeof(bytes),
                EConstructionMode::instance, error));
            TEST_EXPECT(ctx, (error.reason == EReason::storage_limit));
        }
    }
}

void explicit_materialisation_budgets(TTestContext& ctx)
{
    CLiveDocument source;
    CSchema schema;
    SSchemaDiagnostic diagnostic;
    TEST_EXPECT(ctx, tests::parse_document(k_value_schema, source));
    TEST_EXPECT(ctx, schema.try_adopt(std::move(source), diagnostic));
    //  Base and specialisation each cost two visits; the three bulk scalars cost three.
    for (const std::uint64_t allowance : { 0u, 2u, 4u, 6u, 7u })
    {
        CLiveTypes types;
        CLiveInstances instances;
        CLiveBulkData bulk;
        CValueWorkBudget work_budget{ allowance };
        const bool success = schema.materialise(types, instances, bulk, diagnostic, work_budget);
        TEST_EXPECT(ctx, (success == (allowance == 7u)));
        TEST_EXPECT(ctx, work_budget.exhausted());
        if (!success)
        {
            TEST_EXPECT(ctx, ((diagnostic.stage == ESchemaStage::materialisation) && (diagnostic.reason == ESchemaReason::storage_limit)));
            TEST_EXPECT(ctx, (!types.document_ready() && !instances.document_ready() && !bulk.document_ready()));
            CValueWorkBudget retry_budget{ 7u };
            TEST_EXPECT(ctx, schema.materialise(types, instances, bulk, diagnostic, retry_budget));
            TEST_EXPECT(ctx, retry_budget.exhausted());
        }
    }
    CValueWorkBudget exhausted_budget{ 0u };
    CValueWorkBudget independent_budget{ 7u };
    CValueWorkBudget unbounded_budget = CValueWorkBudget::unbounded();
    for (unsigned operation = 0u; operation < 5u; ++operation)
    {
        CLiveTypes types;
        CLiveInstances instances;
        CLiveBulkData bulk;
        bool success = false;
        switch (operation)
        {
            case 0u:
            {
                success = schema.materialise(types, instances, bulk, diagnostic);
                break;
            }
            case 1u:
            case 3u:
            {
                success = schema.materialise(types, instances, bulk, diagnostic, independent_budget);
                break;
            }
            case 2u:
            {
                success = schema.materialise(types, instances, bulk, diagnostic, exhausted_budget);
                break;
            }
            default:
            {
                success = schema.materialise(types, instances, bulk, diagnostic, unbounded_budget);
                break;
            }
        }
        TEST_EXPECT(ctx, (success == ((operation < 2u) || (operation == 4u))));
        TEST_EXPECT(ctx, exhausted_budget.exhausted());
        TEST_EXPECT(ctx, !unbounded_budget.exhausted());
        if (!success) TEST_EXPECT(ctx, (diagnostic.reason == ESchemaReason::storage_limit));
    }
}

void prepared_limits(TTestContext& ctx)
{
    const std::string source = tests::input_schema();
    CPreparedSchema owner;
    SPreparationDiagnostic diagnostic;
    for (unsigned scenario = 0u; scenario < 4u; ++scenario)
    {
        SPreparationLimits limits;
        switch (scenario)
        {
            case 0u:
            {
                limits.source_bytes = 16u;
                break;
            }
            case 1u:
            {
                limits.storage_bytes = 1024u;
                break;
            }
            case 2u:
            {
                limits.payload_bytes = 1u;
                break;
            }
            default:
            {
                limits.nodes = 2u;
                break;
            }
        }
        TEST_EXPECT(ctx, !prepare(owner, source, diagnostic, limits));
        TEST_EXPECT(ctx, ((diagnostic.status == EPreparationStatus::limit_exceeded) && !owner.descriptor()));
    }
    std::string expansion = R"({"types":{"structures":{"Z":{"members":[]})";
    std::string prior = "Z";
    for (unsigned depth = 0u; depth < 23u; ++depth)
    {
        const std::string name = "S" + std::to_string(depth);
        expansion += ",\"" + name + "\":{\"members\":[{\"a\":{\"type\":\"" + prior +
            "\"}},{\"b\":{\"type\":\"" + prior + "\"}}]}";
        prior = name;
    }
    expansion += R"(}},"instances":{"S22":{"value":{}}}})";
    TEST_EXPECT(ctx, !prepare(owner, expansion, diagnostic));
    TEST_EXPECT(ctx, ((diagnostic.status == EPreparationStatus::limit_exceeded) && !owner.descriptor()));
    TEST_EXPECT(ctx, prepare(owner, source, diagnostic));
    owner.clear();
    TEST_EXPECT(ctx, !prepare(owner, "{bad", diagnostic));
    TEST_EXPECT(ctx, (diagnostic.status == EPreparationStatus::invalid_source));
}

void configuration_edge_cases(TTestContext& ctx)
{
    using namespace platform::runtime;
    using platform::input::EConfigurationError;
    for (unsigned scenario = 0u; scenario < 5u; ++scenario)
    {
        std::string source = tests::input_schema();
        const auto replace = [&](const char* const before, const std::string& after)
        {
            const std::size_t position = source.find(before);
            TEST_EXPECT(ctx, (position != std::string::npos));
            if (position != std::string::npos) source.replace(position, std::strlen(before), after);
        };
        EConfigurationError expected = EConfigurationError::combination;
        if (scenario < 3u)
        {
            replace("\"kind\":\"digital_state\"", "\"kind\":\"pulse\"");
            replace("\"activation\":\"regular\"", "\"activation\":\"start_press\"");
            replace("\"native_repeat\":false", "\"native_repeat\":true");
            replace("\"control\":\"key_a\"", (scenario == 0u) ? "\"control\":\"up\"" :
                ((scenario == 1u) ? "\"control\":\"pause\"" : "\"control\":\"insert\""));
            if (!scenario) expected = EConfigurationError::none;
        }
        else if (scenario == 3u)
        {
            const std::size_t begin = source.find("\"stick_binding\"");
            const std::size_t end = source.find("}}", begin) + 2u;
            std::string duplicate = source.substr(begin, (end - begin));
            duplicate.replace(duplicate.find("stick_binding"), 13u, "other_binding");
            duplicate.replace(duplicate.find("\"id\":11"), 7u, "\"id\":12");
            source.insert(end, ("," + duplicate));
        }
        else
        {
            std::string additions;
            for (unsigned index = 0u; index < 1024u; ++index)
            {
                additions += "\"limit_" + std::to_string(index) + "\":{\"declaration\":{\"id\":" +
                    std::to_string(100u + index) + R"(,"kind":"digital_state"}},)";
            }
            replace("\"held\":", additions + "\"held\":");
            expected = EConfigurationError::limit;
        }
        CPreparedSchema owner;
        SPreparationDiagnostic diagnostic;
        const bool valid = prepare(owner, source, diagnostic);
        TEST_EXPECT(ctx, valid);
        if (!valid) continue;
        CInputConfiguration installation;
        TEST_EXPECT(ctx, installation.begin(owner.descriptor()));
        EConfigurationBuild result = EConfigurationBuild::building;
        for (unsigned step = 0u; (step < 100000u) && (result == EConfigurationBuild::building); ++step) result = installation.step();
        TEST_EXPECT(ctx, (installation.diagnostic().error == expected));
        TEST_EXPECT(ctx, (result == ((expected == EConfigurationError::none) ? EConfigurationBuild::ready : EConfigurationBuild::failed)));
        while (!installation.clear_step()) {}
    }
}

void configuration_validation(TTestContext& ctx)
{
    using namespace platform::runtime;
    using platform::input::EConfigurationError;
    struct SChange { const char* before; const char* after; EConfigurationError error; };
    const SChange changes[]{
        { "\"format_version\":1", "\"format_version\":2", EConfigurationError::version },
        { "\"id\":9,\"kind\"", "\"id\":7,\"kind\"", EConfigurationError::duplicate },
        { "\"source\":6", "\"source\":99", EConfigurationError::reference },
        { "\"activation\":\"direct\"", "\"activation\":\"regular\"", EConfigurationError::combination },
        { "\"home\":70", "\"home\":999", EConfigurationError::layout },
        { "\"reserved\":0", "\"reserved\":1", EConfigurationError::combination },
        { "\"profile\":8", "\"profile\":3", EConfigurationError::combination }
    };
    for (unsigned scenario = 0u; scenario <= (sizeof(changes) / sizeof(changes[0])); ++scenario)
    {
        std::string source = tests::input_schema(scenario ? 0u : 300u);
        if (scenario)
        {
            const SChange& change = changes[scenario - 1u];
            const std::size_t position = source.find(change.before);
            TEST_EXPECT(ctx, (position != std::string::npos));
            if (position == std::string::npos) continue;
            source.replace(position, std::strlen(change.before), change.after);
        }
        CPreparedSchema owner;
        SPreparationDiagnostic diagnostic;
        const bool prepared = prepare(owner, source, diagnostic);
        TEST_EXPECT(ctx, prepared);
        if (!prepared) continue;
        CInputConfiguration installation;
        TEST_EXPECT(ctx, installation.begin(owner.descriptor()));
        EConfigurationBuild result = EConfigurationBuild::building;
        unsigned steps = 0u;
        while ((result == EConfigurationBuild::building) && (++steps < 100000u)) result = installation.step();
        TEST_EXPECT(ctx, (steps < 100000u));
        if (scenario)
        {
            TEST_EXPECT(ctx, ((result == EConfigurationBuild::failed) && (installation.diagnostic().error == changes[scenario - 1u].error)));
        }
        else
        {
            TEST_EXPECT(ctx, ((result == EConfigurationBuild::ready) && (steps > 300u)));
            installation.detach();
            owner.clear();
            installation.publish({ 71u });
            const SInputInstallation* const desktop = installation.profile(3u, platform::input::EConnection::desktop);
            const SInputInstallation* const pad = installation.profile(8u, platform::input::EConnection::controller);
            TEST_EXPECT(ctx, (desktop && pad && desktop->contains(4u) && pad->contains(4u)));
            TEST_EXPECT(ctx, (desktop && (desktop->token.value == 71u) && (desktop->subjects.size() == 1u)));
            TEST_EXPECT(ctx, !installation.profile(8u, platform::input::EConnection::desktop));
            TEST_EXPECT(ctx, (installation.binding_at(0u).values[1] == 3u));
        }
        unsigned cleanup = 0u;
        while (!installation.clear_step() && (++cleanup < 10000u)) {}
        TEST_EXPECT(ctx, (cleanup < 10000u));
    }
}
}

void run_prepared_schema_cases(tests::TTestContext& ctx)
{
    prepared_ownership(ctx);
    explicit_writer_budgets(ctx);
    explicit_materialisation_budgets(ctx);
    prepared_limits(ctx);
    configuration_validation(ctx);
    configuration_edge_cases(ctx);
}
