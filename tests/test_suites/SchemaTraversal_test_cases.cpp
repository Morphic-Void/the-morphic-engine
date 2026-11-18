
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaTraversal_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    17 Nov 26
//
//  Deep dependency/value walks, deterministic rejection and workspace failures.

#include "tests/test_suites/SchemaTraversal_test_cases.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "schema/resolved_types.hpp"
#include "schema/values/value_codec.hpp"
#include "schema/operations/data_operation_internal.hpp"
#include "data_model/document_translation.hpp"

#include <string>
#include <cstdio>

namespace
{

enum class EChain { structure, span, array, association, interface };

std::string chain_text(const EChain kind, const unsigned count, const bool cycle)
{
    const char* const group = (kind == EChain::association) ? "associations" :
        ((kind == EChain::interface) ? "interfaces" : "structures");
    std::string text = "{\"types\":{\"" + std::string{ group } + "\":{";
    for (unsigned index = 0u; index < count; ++index)
    {
        if (index) text += ',';
        const std::string target = ((index + 1u) < count) ? ("T" + std::to_string(index + 1u)) :
            (cycle ? "T0" : "u32");
        text += "\"T" + std::to_string(index) + "\":{";
        if (kind == EChain::association)
        {
            text += "\"target\":\"" + target + "\"}";
            continue;
        }
        text += "\"members\":[{\"value\":{\"type\":";
        switch (kind)
        {
            case EChain::span:
            {
                text += "{\"kind\":\"span\",\"element\":\"" + target + "\"}";
                break;
            }
            case EChain::array:
            {
                text += "{\"element\":\"" + target + "\",\"count\":1}";
                break;
            }
            default:
            {
                text += '"' + target + '"';
                break;
            }
        }
        text += "}}]}";
    }
    text += "}}}";
    return text;
}

void dependency_depth(tests::TTestContext& context)
{
    for (const EChain kind : { EChain::structure, EChain::span, EChain::array, EChain::association, EChain::interface })
    {
        const unsigned boundary = ((kind == EChain::association) || (kind == EChain::interface)) ? 256u :
            ((kind == EChain::structure) ? 128u : 85u);
        for (const unsigned count : { boundary, (boundary + 1u), 16u })
        {
            const bool cycle = count == 16u;
            CLiveDocument source;
            TEST_EXPECT(context, tests::parse_document(chain_text(kind, count, cycle), source));
            CBakedDocumentBlock baked;
            TEST_EXPECT(context, document_translation::bake(source, baked));
            for (const bool binary : { false, true })
            {
                schema::CResolvedTypes types;
                schema::SDiagnostic error;
                const bool result = binary ? types.resolve(baked.document(), error) : types.resolve(source, error);
                const bool expected = !cycle && (count == boundary);
                TEST_EXPECT(context, (result == expected));
                if (expected)
                {
                    TEST_EXPECT(context, types.find_declaration(CStringView{ "T0" }).is_valid());
                    if ((kind != EChain::association) && (kind != EChain::interface))
                    {
                        schema::SType layout;
                        TEST_EXPECT(context, (types.type(types.find_type(CStringView{ "T0" }), layout) &&
                            (layout.size == ((kind == EChain::span) ? 12u : 4u))));
                    }
                }
                else
                {
                    TEST_EXPECT(context, (!types.is_ready() && error.occurrence.is_valid()));
                    TEST_EXPECT(context, (error.reason == (cycle ? schema::EReason::cycle : schema::EReason::storage_limit)));
                }
            }
        }
    }
    CLiveDocument ordered;
    TEST_EXPECT(context, tests::parse_document(R"({"types":{"structures":{
        "Root":{"members":[{"a":{"type":"u32","offset":0}},{"b":{"type":"Missing"}}]}}}})", ordered));
    schema::CResolvedTypes types;
    schema::SDiagnostic error;
    TEST_EXPECT(context, (!types.resolve(ordered, error) && (error.reason == schema::EReason::invalid_layout)));
}

bool nested_defaults(CLiveDocument& source)
{
    if (!tests::parse_document(R"({"types":{"structures":{"A":{"members":[{"v":{}}]}}}})", source))
    {
        return false;
    }
    const CNodeKey types = source.object_child(source.root(), CStringView{ "types" });
    const CNodeKey structures = source.object_child(types, CStringView{ "structures" });
    const CNodeKey owner = source.object_child(structures, CStringView{ "A" });
    const CNodeKey member = source.first_child(source.object_child(owner, CStringView{ "members" }));
    CNodeKey type = source.create_object(CStringView{ "type" });
    CNodeKey value = source.create_array(CStringView{ "default" });
    if (!source.append_child(member, type).succeeded() || !source.append_child(member, value).succeeded())
    {
        return false;
    }
    for (unsigned level = 0u; level < 180u; ++level)
    {
        if (!source.append_child(type, source.create_unsigned_integer(1u, CStringView{ "count" })).succeeded())
        {
            return false;
        }
        const bool last = level == 179u;
        const CNodeKey element = last ? source.create_string(CStringView{ "u32" }, CStringView{ "element" }) :
            source.create_object(CStringView{ "element" });
        const CNodeKey child = last ? source.create_unsigned_integer(7u) : source.create_array();
        if (!source.append_child(type, element).succeeded() || !source.append_child(value, child).succeeded())
        {
            return false;
        }
        type = element;
        value = child;
    }
    return true;
}

void resolver_workspace_failures(tests::TTestContext& context)
{
    for (unsigned kind = 0u; kind < 3u; ++kind)
    {
        CLiveDocument source;
        TEST_EXPECT(context, ((kind == 0u) ? nested_defaults(source) :
            tests::parse_document(chain_text(((kind == 1u) ? EChain::span : EChain::interface), 32u, false), source)));
        bool complete{};
        std::size_t failures{};
        for (std::size_t attempt = 0u; (attempt < 512u) && !complete; ++attempt)
        {
            tests::TAllocationFailureFixture injection;
            injection.fail_on = attempt;
            memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext memory{ allocator };
            {
                const tests::TMemoryContextScope scope{ &memory };
                schema::CResolvedTypes types;
                schema::SDiagnostic error;
                const bool resolved = types.resolve(source, error);
                if (!resolved)
                {
                    ++failures;
                    TEST_EXPECT(context, (injection.failed && !types.is_ready() &&
                        (error.reason == schema::EReason::allocation_failed)));
                }
                else if (kind == 0u)
                {
                    schema::SMember member;
                    TEST_EXPECT(context, types.member(types.find_member(types.find_type(CStringView{ "A" }), CStringView{ "v" }), member));
                    schema::CTypesIndex type = member.type, description = member.default_description;
                    for (unsigned level = 0u; level < 180u; ++level)
                    {
                        schema::SType layout;
                        schema::CTypesIndex child;
                        TEST_EXPECT(context, (types.type(type, layout) && (layout.category == schema::ECategory::array)));
                        TEST_EXPECT(context, types.default_element(type, description, 0u, child));
                        type = layout.element_or_storage;
                        description = child;
                    }
                    schema::SDefault value;
                    TEST_EXPECT(context, (types.default_value(type, description, value) &&
                        (value.scalar.value.unsigned_value == 7u)));
                }
                if (!injection.failed) complete = resolved;
            }
            TEST_EXPECT(context, memory.is_attribution_empty());
        }
        std::printf("Schema traversal allocation sweep %u: %zu failures, complete=%u\n", kind, failures, (complete ? 1u : 0u));
        TEST_EXPECT(context, (complete && (failures != 0u)));
    }
}

void declaration_depth_and_failures(tests::TTestContext& context)
{
    //  Dependency-first declarations resolve shallowly; the value can still
    //  traverse the full supported depth. Siblings check resumption order.
    std::string text = R"({"types":{"structures":{"T0":{"members":[]})";
    for (unsigned index = 1u; index <= 260u; ++index)
    {
        text += ",\"T" + std::to_string(index) + "\":{\"members\":[{\"before\":{\"type\":\"u32\"}},";
        text += "{\"v\":{\"type\":\"T" + std::to_string(index - 1u) + "\"}},{\"after\":{\"type\":\"u32\"}}]}";
    }
    text += "}}}";
    CLiveDocument catalogue;
    schema::CResolvedTypes types;
    schema::SDiagnostic type_error;
    TEST_EXPECT(context, (tests::parse_document(text, catalogue) && types.resolve(catalogue, type_error)));
    for (const unsigned depth : { 255u, 256u })
    {
        CLiveDocument source;
        TEST_EXPECT(context, tests::parse_document(R"({"value":{}})", source));
        const CNodeKey root = source.object_child(source.root(), CStringView{ "value" });
        CNodeKey parent = root;
        for (unsigned level = 0u; level < depth; ++level)
        {
            const CNodeKey child = source.create_object(CStringView{ "v" });
            TEST_EXPECT(context, source.append_child(parent, source.create_unsigned_integer(1u, CStringView{ "before" })).succeeded());
            TEST_EXPECT(context, source.append_child(parent, child).succeeded());
            TEST_EXPECT(context, source.append_child(parent, source.create_unsigned_integer(2u, CStringView{ "after" })).succeeded());
            parent = child;
        }
        bool complete{};
        for (std::size_t attempt = 0u; (attempt < 32u) && !complete; ++attempt)
        {
            tests::TAllocationFailureFixture injection;
            injection.fail_on = attempt;
            memory::CMemoryAllocator allocator{ &injection, &tests::allocate_with_failure, &tests::deallocate_test_memory };
            memory::CMemoryContext memory{ allocator };
            {
                const tests::TMemoryContextScope scope{ &memory };
                schema::detail::SValueDiagnostic error;
                const bool result = schema::detail::validate_declaration(types, schema::detail::CDocumentRead{ source },
                    types.find_type(CStringView{ "T260" }), schema::detail::SOccurrence{ root },
                    schema::detail::EConstructionMode::instance, error);
                if (injection.failed)
                {
                    TEST_EXPECT(context, (!result && (error.reason == schema::EReason::allocation_failed)));
                }
                else
                {
                    TEST_EXPECT(context, (result == (depth == 255u)));
                    TEST_EXPECT(context, (result || (error.reason == schema::EReason::storage_limit)));
                    complete = true;
                }
            }
            TEST_EXPECT(context, memory.is_attribution_empty());
        }
        TEST_EXPECT(context, (complete && (source.child_count(root) == 3u)));
        schema::SDataOperationLimits limits;
        limits.depth = 32u;
        schema::detail::CDataOperation operation{ limits };
        {
            const schema::detail::CDataOperationScope scope{ operation };
            schema::detail::SValueDiagnostic error;
            TEST_EXPECT(context, !schema::detail::validate_declaration(types, schema::detail::CDocumentRead{ source },
                types.find_type(CStringView{ "T260" }), schema::detail::SOccurrence{ root },
                schema::detail::EConstructionMode::instance, error));
        }
        schema::SDataOperationDiagnostic error;
        operation.diagnose(error);
        TEST_EXPECT(context, ((error.reason == schema::EDataOperationReason::limit_exceeded) &&
            (error.limit == schema::EDataOperationLimit::depth)));
    }
}

}   // namespace

void run_schema_traversal_cases(tests::TTestContext& context)
{
    dependency_depth(context);
    resolver_workspace_failures(context);
    declaration_depth_and_failures(context);
}

int run_schema_traversal_tests()
{
    tests::TTestContext context;
    run_schema_traversal_cases(context);
    return context.report("SchemaTraversal");
}
