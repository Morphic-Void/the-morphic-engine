
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ShaderReflection_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  First-stage adapter, independent result lifetime and failure publication.

#include "tests/test_suites/ShaderReflection_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/file_helpers.hpp"
#include "tests/environment/test_paths.hpp"
#include "containers/TInstance.hpp"
#include "assets/internal/bundle_builder_io.hpp"
#include "rendering/runtime/shader_compiler.hpp"
#include "rendering/runtime/shader_reflection.hpp"
#include "rendering/runtime/shader_reflection_spirv.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace rendering::shader_reflection::detail
{

struct SModuleTestAccess
{
    static CModule* active() noexcept
    {
        return CModule::active();
    }
    static bool created(const CModule& module) noexcept
    {
        return module.m_created;
    }
    static void omit_code_release(CModule& module) noexcept
    {
        //  Simulate a library-side cleanup omission. The original allocation
        //  stays in the arena and is reclaimed with the whole operation.
        module.m_module._internal->spirv_code = nullptr;
    }
    static void interior_code_release(CModule& module) noexcept
    {
        ++module.m_module._internal->spirv_code;
    }
    static void unknown_code_release(CModule& module) noexcept
    {
        module.m_module._internal->spirv_code = reinterpret_cast<std::uint32_t*>(std::uintptr_t{ 1u });
    }
};

}   //  namespace rendering::shader_reflection::detail

namespace rendering::shader_reflection
{

struct SReflectionTestAccess
{
    static const detail::CModule* module(const CReflectJob& job) noexcept
    {
        return reinterpret_cast<const detail::CModule*>(job.m_reflection.m_module_storage.data());
    }
};

}   //  namespace rendering::shader_reflection

namespace shader_reflection_tests
{

using namespace rendering::shader_reflection;

struct SContextScope
{
    explicit SContextScope(memory::CMemoryContext& context) noexcept
        : previous{ memory::set_thread_memory_context(&context) }
    {
    }

    ~SContextScope() noexcept
    {
        (void)memory::set_thread_memory_context(previous);
    }
    memory::CMemoryContext* const previous;
};

static bool compile(shader::CShaderCompileReport& report, const char* const body = "[numthreads(8,4,1)] void main() {}\n",
    const shader::EStage stage = shader::EStage::compute, const shader::EDebug debug = shader::EDebug::source_debug)
{
    //  Compile defaults embed source. Retained-name limits must not reject it.
    const std::string source = "/*" + std::string(9000u, 'x') + "*/\n" + body;
    CLiveDocument inventory;
    if (!tests::parse_document(R"({"main.hlsl":{}})", inventory))
    {
        return false;
    }
    const bundle::SBuildSource item{ inventory.object_child(inventory.root(), CStringView{ "main.hlsl" }), source.c_str() };
    TPodVector<bundle::SBuildSource> files;
    if (!files.push_back(item))
    {
        return false;
    }
    const bundle::detail::SBuildIo io{ nullptr,
        [](void*, const char* const name, std::size_t& size) noexcept
        {
            size = std::strlen(name);
            return true;
        },
        [](void*, const char* const name, const CByteView& output) noexcept
        {
            std::memcpy(output.data(), name, output.size());
            return true;
        }
    };
    CBakedDocumentBlock block;
    bundle::CBundleView sources;
    if (!bundle::detail::build(inventory, inventory.root(), files.const_view(), block, sources, io).succeeded())
    {
        return false;
    }
    shader::CCompileRequest request;
    if (!request.entries.initialise(1u))
    {
        return false;
    }
    auto& entry = request.entries[0u];
    entry.stage = stage;
    entry.options.debug = debug;
    if (!entry.source_path.set("main.hlsl") || !entry.entry_point.set("main"))
    {
        return false;
    }
    if (!rendering::shader_compiler::initialise_compiler())
    {
        return false;
    }
    rendering::shader_compiler::compile(request, 0u, sources, {}, report);
    if (report.status != shader::EStatus::success)
    {
        std::cerr << "Reflection fixture compilation: " << report.diagnostics.cstring() << '\n';
    }
    return report.status == shader::EStatus::success;
}

static void check_schema(tests::TTestContext& ctx, const CResult& result)
{
    TEST_EXPECT(ctx, result.status() == EStatus::success);
    TEST_EXPECT(ctx, result.schema() != nullptr);
    TEST_EXPECT(ctx, result.storage_statistics().reflection_arena_consumed != 0u);
    TEST_EXPECT(ctx, result.storage_statistics().reflection_arena_consumed <= result.storage_statistics().reflection_arena_capacity);
    if (result.schema() == nullptr)
    {
        return;
    }
    schema::SSchemaDiagnostic error;
    TEST_EXPECT(ctx, result.schema()->validate(error));
    const auto& document = result.schema()->document();
    const auto types = document.object_child(document.root(), CStringView{ "types" });
    const auto interfaces = document.object_child(types, CStringView{ "interfaces" });
    const auto component = document.object_child(interfaces, CStringView{ "component" });
    const auto decorations = document.object_child(component, CStringView{ "decorations" });
    TEST_EXPECT(ctx, document.string_value(document.object_child(decorations, CStringView{ "rendering.shader.stage" })) == CStringView{ "compute" });
    TEST_EXPECT(ctx, document.string_value(document.object_child(decorations, CStringView{ "rendering.spirv.execution.LocalSize.x" })) == CStringView{ "8" });
    TEST_EXPECT(ctx, document.string_value(document.object_child(decorations, CStringView{ "rendering.spirv.execution.LocalSize.y" })) == CStringView{ "4" });
    TEST_EXPECT(ctx, document.string_value(document.object_child(decorations, CStringView{ "rendering.spirv.execution.LocalSize.z" })) == CStringView{ "1" });
    for (const auto* const name : { "resources", "inputs", "outputs" })
    {
        TEST_EXPECT(ctx, document.object_child(interfaces, CStringView{ name }).is_valid());
    }
}

static void allocation_failures(tests::TTestContext& ctx, const shader::CShaderCompileReport& report);
static void qualifiers_and_modes(tests::TTestContext& ctx, const shader::CShaderCompileReport& report);
static void draw_index_mask(tests::TTestContext& ctx, const shader::CShaderCompileReport& report);

static void stage_interfaces(tests::TTestContext& ctx)
{
    static const char* const sources[]{
        R"(
struct V { float4 position : SV_Position; noperspective float2 uv : TEXCOORD0; nointerpolation uint code : CODE0; float weights[2] : WEIGHTS0; float2x3 basis : MATRIX0; };
V main(float3 position : POSITION0, uint id : SV_VertexID) {
    V v; v.position = float4(position, 1); v.uv = position.xy; v.code = id;
    v.weights[0] = position.x; v.weights[1] = position.y;
    v.basis = float2x3(position, position + 1); return v;
})",
        R"(
struct V { float4 position : SV_Position; float2 weights[2] : TEXCOORD0; };
struct P { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; float coeff[2] : PATCH0; };
P patch(InputPatch<V,3> p) { P v; v.edges[0]=2; v.edges[1]=3; v.edges[2]=4; v.inside=5; v.coeff[0]=p[0].weights[0].x; v.coeff[1]=p[1].weights[1].y; return v; }
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("patch")]
V main(InputPatch<V,3> p, uint id : SV_OutputControlPointID, uint primitive : SV_PrimitiveID) { V v=p[id]; v.position.x += primitive; return v; }
)",
        R"(
struct V { float4 position : SV_Position; float2 weights[2] : TEXCOORD0; };
struct P { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; float coeff[2] : PATCH0; };
[domain("tri")]
float4 main(P p, float3 bary : SV_DomainLocation, const OutputPatch<V,3> points, uint primitive : SV_PrimitiveID) : SV_Position {
    return points[0].position*bary.x + points[1].position*bary.y + points[2].position*bary.z + p.coeff[0] + p.edges[1] + p.inside + points[0].weights[1].x + primitive;
})",
        R"(
struct V { float4 position : SV_Position; float2 weights[2] : TEXCOORD0; };
[instance(2)][maxvertexcount(3)]
void main(triangle V vertices[3], uint invocation : SV_GSInstanceID, inout TriangleStream<V> stream) {
    for (uint i=0; i<3; ++i) { V v=vertices[i]; v.position.x += invocation; stream.Append(v); } stream.RestartStrip();
})",
        R"(
struct P { float4 colour : SV_Target1; float depth : SV_DepthGreaterEqual; };
[earlydepthstencil]
P main(noperspective centroid float2 uv : TEXCOORD0, nointerpolation uint code : CODE0, bool front : SV_IsFrontFace, float4 pos : SV_Position) {
    P p; p.colour = float4(uv, code, front ? 1 : 0); p.depth = pos.z; return p;
})",
        R"(
RWStructuredBuffer<uint> values : register(u0);
[numthreads(8,4,1)] void main(uint3 global : SV_DispatchThreadID, uint3 local : SV_GroupThreadID, uint index : SV_GroupIndex, uint3 group : SV_GroupID) {
    values[global.x] = local.y + index + group.z;
})"
    };
    static const char* const stages[]{ "vertex", "tessellation_control", "tessellation_evaluation", "geometry", "fragment", "compute" };
    for (std::uint32_t stage = 0u; stage < 6u; ++stage)
    {
        shader::CShaderCompileReport report;
        const bool ready = compile(report, sources[stage], static_cast<shader::EStage>(stage));
        TEST_EXPECT(ctx, ready);
        if (!ready)
        {
            continue;
        }
        CResult result;
        const auto status = reflect({ &report, CStringView{ stages[stage] } }, {}, result);
        if (status != EStatus::success)
        {
            std::cerr << "Stage " << stages[stage] << " status " << static_cast<int>(status) << " reason " << static_cast<int>(result.diagnostic().reason)
                << " id " << result.diagnostic().result_id << " member " << result.diagnostic().member << '\n';
        }
        TEST_EXPECT(ctx, status == EStatus::success);
        if (result.schema() == nullptr)
        {
            continue;
        }
        const auto& doc = result.schema()->document();
        const auto at = [&](const CNodeKey node, const char* key)
        {
            return doc.object_child(node, CStringView{ key });
        };
        const auto text = [&](const CNodeKey node, const char* key)
        {
            return doc.string_value(at(node, key));
        };
        const auto types = at(doc.root(), "types"), interfaces = at(types, "interfaces"), associations = at(types, "associations");
        const auto root_details = at(at(interfaces, "component"), "decorations");
        TEST_EXPECT(ctx, text(root_details, "rendering.shader.stage") == CStringView{ stages[stage] });
        if (stage == 1u)
        {
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.OutputVertices") == CStringView{ "3" });
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.SpacingEqual") == CStringView{ "true" });
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.VertexOrderCw") == CStringView{ "true" });
        }
        if ((stage == 1u) || (stage == 2u) || (stage == 3u))
        {
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.Triangles") == CStringView{ "true" });
        }
        if (stage == 3u)
        {
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.Invocations") == CStringView{ "2" });
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.OutputVertices") == CStringView{ "3" });
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.OutputTriangleStrip") == CStringView{ "true" });
        }
        if (stage == 4u)
        {
            for (const auto* key : { "OriginUpperLeft", "EarlyFragmentTests", "DepthReplacing", "DepthGreater" })
            {
                const auto full = std::string("rendering.spirv.execution.") + key;
                TEST_EXPECT(ctx, text(root_details, full.c_str()) == CStringView{ "true" });
            }
        }
        if (stage == 5u)
        {
            TEST_EXPECT(ctx, text(root_details, "rendering.spirv.execution.LocalSize.x") == CStringView{ "8" });
        }
        std::uint32_t wrappers = 0u, patch_arrays = 0u, inner_arrays = 0u, semantics = 0u, builtins = 0u, primitive_ids = 0u;
        for (const auto* direction : { "inputs", "outputs" })
        {
            std::int32_t previous = -1;
            const auto groups = at(at(interfaces, direction), "members");
            for (auto group = doc.first_child(groups); group.is_valid(); group = doc.next_sibling(group))
            {
                const auto declaration = doc.object_child(interfaces, text(group, "type"));
                const auto domain = text(at(declaration, "decorations"), "rendering.interface.domain");
                static const char* const domains[]{ "vertex", "patch", "fragment", "target", "system" };
                std::int32_t ordinal = -1;
                for (std::int32_t i = 0; i < 5; ++i)
                {
                    if (domain == CStringView{ domains[i] })
                    {
                        ordinal = i;
                    }
                }
                TEST_EXPECT(ctx, ordinal > previous);
                previous = ordinal;
                TEST_EXPECT(ctx, text(at(declaration, "decorations"), "rendering.interface.kind") == CStringView{ "value_group" });
                const auto entries = at(declaration, "members");
                TEST_EXPECT(ctx, doc.child_count(entries) != 0u);
                for (auto entry = doc.first_child(entries); entry.is_valid(); entry = doc.next_sibling(entry))
                {
                    const auto details = at(entry, "decorations");
                    if (at(details, "rendering.hlsl.semantic").is_valid())
                    {
                        ++semantics;
                    }
                    if (at(details, "rendering.spirv.builtin").is_valid())
                    {
                        ++builtins;
                    }
                    if (text(details, "rendering.spirv.builtin") == CStringView{ "PrimitiveId" })
                    {
                        ++primitive_ids;
                        TEST_EXPECT(ctx, domain == CStringView{ "system" });
                        if (stage == 2u)
                        {
                            TEST_EXPECT(ctx, text(details, "rendering.spirv.decoration.Patch") == CStringView{ "true" });
                        }
                    }
                    const auto wrapper = doc.object_child(associations, text(entry, "type"));
                    auto shape = doc.object_child(interfaces, text(entry, "type"));
                    if (wrapper.is_valid())
                    {
                        ++wrappers;
                        TEST_EXPECT(ctx, domain == CStringView{ "vertex" });
                        const auto metadata = at(wrapper, "decorations");
                        TEST_EXPECT(ctx, text(metadata, "rendering.sequence.kind") == CStringView{ "vertex_array" });
                        TEST_EXPECT(ctx, text(metadata, "rendering.sequence.extent") == CStringView{ "3" });
                        TEST_EXPECT(ctx, text(metadata, "rendering.sequence.source_dimension") == CStringView{ "0" });
                        TEST_EXPECT(ctx, !at(metadata, "rendering.spirv.builtin").is_valid());
                        shape = doc.object_child(interfaces, text(wrapper, "target"));
                        if (text(at(shape, "decorations"), "rendering.shape.kind") == CStringView{ "array" })
                        {
                            ++inner_arrays;
                        }
                    }
                    if ((domain == CStringView{ "patch" }) && (text(at(shape, "decorations"), "rendering.shape.kind") == CStringView{ "array" }))
                    {
                        ++patch_arrays;
                    }
                    if (stage == 4u)
                    {
                        const auto semantic = text(details, "rendering.hlsl.semantic");
                        if (semantic == CStringView{ "TEXCOORD0" })
                        {
                            TEST_EXPECT(ctx, text(details, "rendering.spirv.decoration.NoPerspective") == CStringView{ "true" });
                            TEST_EXPECT(ctx, text(details, "rendering.spirv.decoration.Centroid") == CStringView{ "true" });
                        }
                        if (semantic == CStringView{ "CODE0" })
                        {
                            TEST_EXPECT(ctx, text(details, "rendering.spirv.decoration.Flat") == CStringView{ "true" });
                        }
                    }
                }
            }
        }
        TEST_EXPECT(ctx, (semantics != 0u) && (builtins != 0u));
        TEST_EXPECT(ctx, ((stage >= 1u) && (stage <= 3u)) ? (wrappers != 0u) : (wrappers == 0u));
        if ((stage >= 1u) && (stage <= 3u))
        {
            TEST_EXPECT(ctx, inner_arrays != 0u);
        }
        if ((stage == 1u) || (stage == 2u))
        {
            TEST_EXPECT(ctx, patch_arrays != 0u);
        }
        if ((stage == 1u) || (stage == 2u))
        {
            TEST_EXPECT(ctx, primitive_ids == 1u);
        }
        std::uint32_t declarations = 0u, members = 0u;
        for (const auto* category : { "interfaces", "associations", "structures" })
        {
            const auto collection = at(types, category);
            declarations += doc.child_count(collection);
            for (auto declaration = doc.first_child(collection); declaration.is_valid(); declaration = doc.next_sibling(declaration))
            {
                const auto entries = at(declaration, "members");
                members += doc.child_count(entries);
                if (std::strcmp(category, "interfaces") != 0)
                {
                    continue;
                }
                TEST_EXPECT(ctx, !at(declaration, "detail").is_valid());
                const auto kind = text(at(declaration, "decorations"), "rendering.shape.kind");
                for (auto entry = doc.first_child(entries); entry.is_valid(); entry = doc.next_sibling(entry))
                {
                    for (const auto* field : { "offset", "stride", "alignment" })
                    {
                        TEST_EXPECT(ctx, !at(entry, field).is_valid());
                    }
                }
                if ((kind == CStringView{ "vector" }) || (kind == CStringView{ "matrix" }) || (kind == CStringView{ "array" }))
                {
                    TEST_EXPECT(ctx, doc.child_count(entries) == 1u);
                }
                if (kind == CStringView{ "matrix" })
                {
                    TEST_EXPECT(ctx, text(at(declaration, "decorations"), "rendering.matrix.basis") == CStringView{ "spirv" });
                    TEST_EXPECT(ctx, !at(at(declaration, "decorations"), "rendering.matrix.storage_major").is_valid());
                }
            }
        }
        SLimits exact;
        exact.declarations = declarations;
        exact.members = members;
        CResult boundary;
        TEST_EXPECT(ctx, reflect({ &report, CStringView{ stages[stage] } }, exact, boundary) == EStatus::success);
        for (std::uint32_t limit = 0u; limit < 2u; ++limit)
        {
            SLimits policy;
            if (limit == 0u)
            {
                policy.declarations = declarations - 1u;
            }
            else
            {
                policy.members = members - 1u;
            }
            CResult rejected;
            TEST_EXPECT(ctx, reflect({ &report, CStringView{ stages[stage] } }, policy, rejected) == EStatus::limit_exceeded);
            TEST_EXPECT(ctx, rejected.schema() == nullptr);
        }
        allocation_failures(ctx, report);
        if (stage == 4u)
        {
            qualifiers_and_modes(ctx, report);
        }
        if (stage < 4u)
        {
            draw_index_mask(ctx, report);
        }
    }
}

static void draw_index_mask(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    const bool vertex = report.stage == shader::EStage::vertex;
    const auto original_builtin = vertex ? SpvBuiltInVertexIndex :
        ((report.stage == shader::EStage::domain) ? SpvBuiltInPrimitiveId : SpvBuiltInInvocationId);
    const auto original = report.shader.view();
    std::vector<std::uint32_t> words(original.size() / 4u);
    std::memcpy(words.data(), original.data(), original.size());
    bool changed = false;
    for (std::size_t offset = 5u; offset < words.size(); offset += words[offset] >> 16u)
    {
        if (((words[offset] & 0xffffu) == SpvOpDecorate) && (words[offset + 2u] == SpvDecorationBuiltIn) &&
            (words[offset + 3u] == original_builtin))
        {
            words[offset + 3u] = SpvBuiltInDrawIndex;
            changed = true;
        }
    }
    TEST_EXPECT(ctx, changed);
    const std::uint32_t capability[]{ (2u << 16u) | SpvOpCapability, SpvCapabilityDrawParameters };
    words.insert(words.begin() + 5u, std::begin(capability), std::end(capability));
    shader::CShaderCompileReport altered;
    altered.status = shader::EStatus::success;
    altered.stage = altered.shader.stage = report.stage;
    TEST_EXPECT(ctx, altered.entry_point.set("main"));
    TEST_EXPECT(ctx, altered.shader.bytes.resize(words.size() * 4u, 4u));
    std::memcpy(altered.shader.bytes.data(), words.data(), words.size() * 4u);
    CResult result;
    TEST_EXPECT(ctx, reflect({ &altered, CStringView{ "draw index" } }, {}, result) == (vertex ? EStatus::success : EStatus::unsupported_feature));
    if (!vertex)
    {
        TEST_EXPECT(ctx, result.diagnostic().reason == EReason::unsupported_interface);
        TEST_EXPECT(ctx, result.diagnostic().has_result_id);
        TEST_EXPECT(ctx, result.schema() == nullptr);
    }
}

static void qualifiers_and_modes(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    rendering::shader_reflection::detail::CModule module;
    bool size_rejected = false;
    TEST_EXPECT(ctx, module.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
    std::uint32_t input = 0u, output = 0u;
    for (std::uint32_t i = 0u; i < module.value().input_variable_count; ++i)
    {
        const auto& variable = *module.value().input_variables[i];
        if ((variable.semantic != nullptr) && (std::strcmp(variable.semantic, "TEXCOORD0") == 0))
        {
            input = variable.spirv_id;
        }
    }
    for (std::uint32_t i = 0u; i < module.value().output_variable_count; ++i)
    {
        if (module.value().output_variables[i]->location == 1u)
        {
            output = module.value().output_variables[i]->spirv_id;
        }
    }
    TEST_EXPECT(ctx, (input != 0u) && (output != 0u));
    module.close();
    if ((input == 0u) || (output == 0u))
    {
        return;
    }
    for (std::uint32_t test = 0u; test < 4u; ++test)
    {
        const auto original = report.shader.view();
        std::vector<std::uint32_t> words(original.size() / 4u);
        std::memcpy(words.data(), original.data(), original.size());
        std::size_t annotation = 0u, mode = 0u, centroid = 0u;
        for (std::size_t offset = 5u; offset < words.size(); offset += words[offset] >> 16u)
        {
            const auto opcode = words[offset] & 0xffffu;
            if ((annotation == 0u) && (opcode == SpvOpDecorate))
            {
                annotation = offset;
            }
            if ((mode == 0u) && (opcode == SpvOpExecutionMode))
            {
                mode = offset;
            }
            if ((opcode == SpvOpDecorate) && (words[offset + 1u] == output) && (words[offset + 2u] == SpvDecorationLocation))
            {
                words[offset + 3u] = 0u;
            }
            if ((opcode == SpvOpDecorate) && (words[offset + 1u] == input) && (words[offset + 2u] == SpvDecorationCentroid))
            {
                centroid = offset;
            }
        }
        if (test != 1u)
        {
            TEST_EXPECT(ctx, centroid != 0u);
            //  NoPerspective combines with either interpolation site. Sample
            //  and Centroid together are forbidden by SPIR-V shader rules.
            if ((test == 0u) && (centroid != 0u))
            {
                words.erase(words.begin() + centroid, words.begin() + centroid + 3u);
            }
            const std::uint32_t decorations[]{ (3u << 16u) | SpvOpDecorate, input, static_cast<std::uint32_t>((test == 3u) ? SpvDecorationFlat : SpvDecorationSample),
                (4u << 16u) | SpvOpDecorate, output, SpvDecorationComponent, 0u,
                (4u << 16u) | SpvOpDecorate, output, SpvDecorationIndex, 1u };
            words.insert(words.begin() + annotation, std::begin(decorations), std::end(decorations));
            const std::uint32_t capability[]{ (2u << 16u) | SpvOpCapability, SpvCapabilitySampleRateShading };
            words.insert(words.begin() + 5u, std::begin(capability), std::end(capability));
        }
        else
        {
            //  The compiled shader already has DepthGreater. Preserve both
            //  facts in the input and require the checked mode table to reject.
            const std::uint32_t conflict[]{ (3u << 16u) | SpvOpExecutionMode, words[mode + 1u], SpvExecutionModeDepthLess };
            words.insert(words.begin() + mode, std::begin(conflict), std::end(conflict));
        }
        shader::CShaderCompileReport altered;
        altered.status = shader::EStatus::success;
        altered.stage = altered.shader.stage = shader::EStage::pixel;
        TEST_EXPECT(ctx, altered.entry_point.set("main"));
        TEST_EXPECT(ctx, altered.shader.bytes.resize(words.size() * 4u, 4u));
        std::memcpy(altered.shader.bytes.data(), words.data(), words.size() * 4u);
        CResult result;
        const auto status = reflect({ &altered, CStringView{ "qualifiers" } }, {}, result);
        if (test != 0u)
        {
            TEST_EXPECT(ctx, status == EStatus::unsupported_feature);
            TEST_EXPECT(ctx, result.diagnostic().reason == ((test == 1u) ? EReason::unsupported_execution : EReason::unsupported_interface));
            TEST_EXPECT(ctx, result.schema() == nullptr);
            continue;
        }
        TEST_EXPECT(ctx, status == EStatus::success);
        if (result.schema() == nullptr)
        {
            continue;
        }
        const auto& doc = result.schema()->document();
        const auto at = [&](const CNodeKey node, const char* key)
        {
            return doc.object_child(node, CStringView{ key });
        };
        const auto text = [&](const CNodeKey node, const char* key)
        {
            return doc.string_value(at(node, key));
        };
        const auto interfaces = at(at(doc.root(), "types"), "interfaces");
        std::uint32_t found = 0u;
        for (auto declaration = doc.first_child(interfaces); declaration.is_valid(); declaration = doc.next_sibling(declaration))
        {
            for (auto member = doc.first_child(at(declaration, "members")); member.is_valid(); member = doc.next_sibling(member))
            {
                const auto details = at(member, "decorations");
                if (text(details, "rendering.hlsl.semantic") == CStringView{ "TEXCOORD0" })
                {
                    ++found;
                    for (const auto* flag : { "Sample", "NoPerspective" })
                    {
                        const auto key = std::string("rendering.spirv.decoration.") + flag;
                        TEST_EXPECT(ctx, text(details, key.c_str()) == CStringView{ "true" });
                    }
                    TEST_EXPECT(ctx, !at(details, "rendering.spirv.decoration.Centroid").is_valid());
                }
                if (text(details, "rendering.vulkan.index") == CStringView{ "1" })
                {
                    ++found;
                    TEST_EXPECT(ctx, text(details, "rendering.vulkan.location") == CStringView{ "0" });
                    TEST_EXPECT(ctx, text(details, "rendering.vulkan.component") == CStringView{ "0" });
                }
            }
        }
        TEST_EXPECT(ctx, found == 2u);
        allocation_failures(ctx, altered);
    }
}

static void stage_aggregates(tests::TTestContext& ctx)
{
    //  Small, explicit SPIR-V interface blocks retain shapes DXC may flatten.
    //  IDs are fixed so assertions inspect source owners independently of the
    //  importer's private names. Cases 0/1/2 are homogeneous legal interfaces;
    //  case 3 combines fragment ClipDistance and FragCoord in one built-in block.
    for (std::uint32_t test = 0u; test < 8u; ++test)
    {
        const bool builtin_block = test != 0u;
        const bool fragment = (test == 2u) || (test == 3u);
        std::vector<std::uint32_t> words{ SpvMagicNumber, 0x00010000u, 0u, 17u, 0u };
        const auto op = [&](const SpvOp opcode, std::initializer_list<std::uint32_t> operands)
        {
            words.push_back((static_cast<std::uint32_t>(operands.size() + 1u) << 16u) | opcode);
            words.insert(words.end(), operands);
        };
        const auto name = [&](const SpvOp opcode, std::initializer_list<std::uint32_t> prefix, const char* text)
        {
            const auto size = (std::strlen(text) + 4u) / 4u;
            words.push_back((static_cast<std::uint32_t>(prefix.size() + size + 1u) << 16u) | opcode);
            words.insert(words.end(), prefix);
            const auto offset = words.size();
            words.resize(offset + size);
            std::memcpy(words.data() + offset, text, std::strlen(text));
        };
        op(SpvOpCapability, { SpvCapabilityShader });
        if (builtin_block)
        {
            op(SpvOpCapability, { SpvCapabilityClipDistance });
        }
        op(SpvOpMemoryModel, { SpvAddressingModelLogical, SpvMemoryModelGLSL450 });
        //  "main" occupies two words including the trailing NUL.
        op(SpvOpEntryPoint, { static_cast<std::uint32_t>(fragment ? SpvExecutionModelFragment : SpvExecutionModelVertex), 15u, 0x6e69616du, 0u, 14u });
        if (fragment)
        {
            op(SpvOpExecutionMode, { 15u, SpvExecutionModeOriginUpperLeft });
        }
        name(SpvOpName, { 12u }, "OriginalBlock");
        name(SpvOpName, { 14u }, "OriginalOccurrence");
        name(SpvOpMemberName, { 12u, 0u }, "first");
        name(SpvOpMemberName, { 12u, 1u }, "second");
        if (!builtin_block)
        {
            name(SpvOpMemberName, { 12u, 2u }, "third");
        }
        op(SpvOpDecorate, { 12u, SpvDecorationBlock });
        if (builtin_block)
        {
            op(SpvOpMemberDecorate, { 12u, 0u, SpvDecorationBuiltIn, static_cast<std::uint32_t>(fragment ? SpvBuiltInFragCoord : SpvBuiltInPosition) });
            op(SpvOpMemberDecorate, { 12u, 1u, SpvDecorationBuiltIn,
                (test == 5u) ? 0xffffu : ((test == 3u) ? SpvBuiltInClipDistance : (fragment ? SpvBuiltInPointCoord : SpvBuiltInPointSize)) });
            if (test == 4u)
            {
                op(SpvOpMemberDecorate, { 12u, 0u, 0xffffu });
            }
            if (test == 7u)
            {
                op(SpvOpMemberDecorate, { 12u, 0u, SpvDecorationPatch });
            }
            if (!fragment)
            {
                op(SpvOpMemberDecorate, { 12u, 0u, SpvDecorationInvariant });
            }
            if (test == 6u)
            {
                op(SpvOpMemberDecorate, { 12u, 0u, SpvDecorationLocation, 0u });
            }
        }
        else
        {
            op(SpvOpMemberDecorate, { 12u, 0u, SpvDecorationLocation, 0u });
            op(SpvOpMemberDecorate, { 12u, 1u, SpvDecorationLocation, 1u });
            op(SpvOpMemberDecorate, { 12u, 2u, SpvDecorationLocation, 3u });
            op(SpvOpMemberDecorate, { 12u, 0u, SpvDecorationRelaxedPrecision });
        }
        op(SpvOpTypeVoid, { 1u });
        op(SpvOpTypeFunction, { 2u, 1u });
        op(SpvOpTypeFloat, { 3u, 32u });
        op(SpvOpTypeVector, { 4u, 3u, 4u });
        op(SpvOpTypeVector, { 5u, 3u, fragment ? 2u : 3u });
        op(SpvOpTypeMatrix, { 6u, 5u, 2u });
        op(SpvOpTypeInt, { 7u, 32u, 0u });
        op(SpvOpConstant, { 7u, 8u, 2u });
        op(SpvOpConstant, { 7u, 9u, 1u });
        op(SpvOpTypeArray, { 10u, 3u, 8u });
        op(SpvOpTypeArray, { 11u, 10u, 9u });
        if (!builtin_block)
        {
            op(SpvOpTypeStruct, { 12u, 4u, 6u, 11u });
        }
        else
        {
            op(SpvOpTypeStruct, { 12u, 4u, (test == 3u) ? 10u : (fragment ? 5u : 3u) });
        }
        const auto storage = static_cast<std::uint32_t>((!builtin_block || fragment) ? SpvStorageClassInput : SpvStorageClassOutput);
        op(SpvOpTypePointer, { 13u, storage, 12u });
        op(SpvOpVariable, { 13u, 14u, storage });
        op(SpvOpFunction, { 1u, 15u, SpvFunctionControlMaskNone, 2u });
        op(SpvOpLabel, { 16u });
        op(SpvOpReturn, {});
        op(SpvOpFunctionEnd, {});
        shader::CShaderCompileReport report;
        report.status = shader::EStatus::success;
        report.stage = report.shader.stage = fragment ? shader::EStage::pixel : shader::EStage::vertex;
        TEST_EXPECT(ctx, report.entry_point.set("main"));
        TEST_EXPECT(ctx, report.shader.bytes.resize(words.size() * 4u, 4u));
        std::memcpy(report.shader.bytes.data(), words.data(), words.size() * 4u);
        CResult result;
        const auto status = reflect({ &report, CStringView{ "aggregate" } }, {}, result);
        if (test >= 3u)
        {
            TEST_EXPECT(ctx, status == EStatus::unsupported_feature);
            TEST_EXPECT(ctx, result.diagnostic().reason == EReason::unsupported_interface);
            TEST_EXPECT(ctx, result.schema() == nullptr);
            if ((test == 3u) || (test == 4u) || (test == 6u))
            {
                TEST_EXPECT(ctx, result.diagnostic().result_id == 12u);
                TEST_EXPECT(ctx, result.diagnostic().has_member);
                TEST_EXPECT(ctx, result.diagnostic().member == ((test == 3u) ? 1u : 0u));
            }
            continue;
        }
        if (status != EStatus::success)
        {
            std::cerr << "Aggregate " << test << " reason " << static_cast<int>(result.diagnostic().reason) << '\n';
        }
        TEST_EXPECT(ctx, status == EStatus::success);
        if (result.schema() == nullptr)
        {
            continue;
        }
        const auto& doc = result.schema()->document();
        const auto at = [&](const CNodeKey node, const char* key)
        {
            return doc.object_child(node, CStringView{ key });
        };
        const auto text = [&](const CNodeKey node, const char* key)
        {
            return doc.string_value(at(node, key));
        };
        const auto interfaces = at(at(doc.root(), "types"), "interfaces");
        const auto groups = at(at(interfaces, (storage == SpvStorageClassInput) ? "inputs" : "outputs"), "members");
        TEST_EXPECT(ctx, doc.child_count(groups) == 1u);
        const auto group = doc.object_child(interfaces, text(doc.first_child(groups), "type"));
        TEST_EXPECT(ctx, text(at(group, "decorations"), "rendering.interface.domain") == CStringView{ fragment ? "system" : "vertex" });
        const auto occurrence = doc.first_child(at(group, "members"));
        const auto structure = doc.object_child(interfaces, text(occurrence, "type"));
        const auto details = at(structure, "decorations");
        const auto entries = at(structure, "members");
        TEST_EXPECT(ctx, text(details, "rendering.shape.kind") == CStringView{ "structure" });
        TEST_EXPECT(ctx, text(details, "rendering.source.name") == CStringView{ "OriginalBlock" });
        TEST_EXPECT(ctx, text(at(occurrence, "decorations"), "rendering.source.name") == CStringView{ "OriginalOccurrence" });
        TEST_EXPECT(ctx, !at(at(occurrence, "decorations"), "rendering.spirv.builtin").is_valid());
        const auto first = doc.first_child(entries), second = doc.next_sibling(first);
        TEST_EXPECT(ctx, text(at(first, "decorations"), "rendering.source.name") == CStringView{ "first" });
        if (builtin_block)
        {
            TEST_EXPECT(ctx, text(at(first, "decorations"), "rendering.spirv.builtin") == CStringView{ fragment ? "FragCoord" : "Position" });
            TEST_EXPECT(ctx, text(at(second, "decorations"), "rendering.spirv.builtin") == CStringView{ fragment ? "PointCoord" : "PointSize" });
            if (!fragment)
            {
                TEST_EXPECT(ctx, text(at(first, "decorations"), "rendering.spirv.decoration.Invariant") == CStringView{ "true" });
            }
        }
        else
        {
            TEST_EXPECT(ctx, text(at(first, "decorations"), "rendering.vulkan.location") == CStringView{ "0" });
            TEST_EXPECT(ctx, text(at(first, "decorations"), "rendering.spirv.decoration.RelaxedPrecision") == CStringView{ "true" });
            const auto matrix = doc.object_child(interfaces, text(second, "type"));
            const auto columns = doc.first_child(at(matrix, "members"));
            const auto vector = doc.object_child(interfaces, text(columns, "type"));
            TEST_EXPECT(ctx, text(at(matrix, "decorations"), "rendering.shape.kind") == CStringView{ "matrix" });
            std::uint64_t count = 0u;
            TEST_EXPECT(ctx, doc.unsigned_integer_value(at(columns, "count"), count) && (count == 2u));
            TEST_EXPECT(ctx, doc.unsigned_integer_value(at(doc.first_child(at(vector, "members")), "count"), count) && (count == 3u));
            auto array = doc.object_child(interfaces, text(doc.next_sibling(second), "type"));
            for (const auto extent : { 1u, 2u })
            {
                TEST_EXPECT(ctx, text(at(array, "decorations"), "rendering.shape.kind") == CStringView{ "array" });
                const auto element = doc.first_child(at(array, "members"));
                TEST_EXPECT(ctx, doc.unsigned_integer_value(at(element, "count"), count) && (count == extent));
                array = doc.object_child(interfaces, text(element, "type"));
            }
        }
        allocation_failures(ctx, report);
    }
}

static void execution_rejections(tests::TTestContext& ctx, const shader::CShaderCompileReport& source)
{
    for (std::uint32_t test = 0u; test < 8u; ++test)
    {
        shader::CShaderCompileReport report;
        report.status = shader::EStatus::success;
        report.stage = report.shader.stage = shader::EStage::compute;
        TEST_EXPECT(ctx, report.entry_point.set("main"));
        const auto original = source.shader.view();
        std::vector<std::uint32_t> words(original.size() / 4u);
        std::memcpy(words.data(), original.data(), original.size());
        bool changed = false;
        for (std::size_t offset = 5u; offset < words.size(); offset += words[offset] >> 16u)
        {
            if ((words[offset] & 0xffffu) != SpvOpExecutionMode)
            {
                continue;
            }
            changed = true;
            if (test == 0u)
            {
                words[offset + 3u] = 0u;
            }
            if (test == 1u)
            {
                words[offset + 2u] = SpvExecutionModeOutputVertices;
            }
            if (test == 2u)
            {
                words[offset + 2u] = 0xffffu;
            }
            if (test == 3u)
            {
                const std::vector<std::uint32_t> duplicate(words.begin() + offset, words.begin() + offset + 6u);
                words.insert(words.begin() + offset, duplicate.begin(), duplicate.end());
            }
            if (test == 4u)
            {
                words[offset] = (6u << 16u) | SpvOpExecutionModeId;
            }
            if (test == 5u)
            {
                //  Known flag with an unexpected operand, also wrong for compute.
                words[offset + 2u] = SpvExecutionModeOriginUpperLeft;
                words[offset] = (4u << 16u) | SpvOpExecutionMode;
                words.erase(words.begin() + offset + 4u, words.begin() + offset + 6u);
            }
            if (test == 6u)
            {
                words[offset] = (5u << 16u) | SpvOpExecutionMode;
                words.erase(words.begin() + offset + 5u);
            }
            if (test == 7u)
            {
                words.erase(words.begin() + offset, words.begin() + offset + 6u);
            }
            break;
        }
        TEST_EXPECT(ctx, changed);
        TEST_EXPECT(ctx, report.shader.bytes.resize(words.size() * 4u, 4u));
        std::memcpy(report.shader.bytes.data(), words.data(), words.size() * 4u);
        CResult rejected;
        TEST_EXPECT(ctx, reflect({ &report, CStringView{ "execution rejection" } }, {}, rejected) == EStatus::unsupported_feature);
        TEST_EXPECT(ctx, rejected.diagnostic().reason == EReason::unsupported_execution);
        TEST_EXPECT(ctx, rejected.diagnostic().has_result_id && rejected.diagnostic().has_word_offset);
        TEST_EXPECT(ctx, rejected.schema() == nullptr);
    }
}

static void limits(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    const SRequest request{ &report, CStringView{ "opaque/library?label=\xc2\xa3" } };
    CResult good;
    TEST_EXPECT(ctx, reflect(request, {}, good) == EStatus::success);
    const auto estimate = good.storage_statistics().estimated_working_bytes;
    const auto retained = good.storage_statistics().retained_bytes;
    for (std::uint32_t test = 0u; test < 10u; ++test)
    {
        SLimits policy;
        switch (test)
        {
            case (0u):
            {
                policy.artifact_bytes = report.shader.view().size() - 1u;
                break;
            }
            case (1u):
            {
                policy.id_bound = 1u;
                break;
            }
            case (2u):
            {
                policy.instructions = 1u;
                break;
            }
            case (3u):
            {
                policy.nesting = 1u;
                break;
            }
            case (4u):
            {
                policy.string_bytes = 2u;
                break;
            }
            case (5u):
            {
                policy.document_nodes = 1u;
                break;
            }
            case (6u):
            {
                policy.declarations = 3u;
                break;
            }
            case (7u):
            {
                policy.retained_bytes = static_cast<std::size_t>(retained - 1u);
                break;
            }
            case (8u):
            {
                policy.estimated_working_bytes = static_cast<std::size_t>(estimate - 1u);
                break;
            }
            case (9u):
            {
                policy.strings_bytes = 1u;
                break;
            }
        }
        CResult rejected;
        TEST_EXPECT(ctx, reflect(request, policy, rejected) == EStatus::limit_exceeded);
        TEST_EXPECT(ctx, rejected.schema() == nullptr);
    }
    SLimits exact;
    exact.artifact_bytes = report.shader.view().size();
    exact.estimated_working_bytes = static_cast<std::size_t>(estimate);
    exact.retained_bytes = static_cast<std::size_t>(retained);
    CResult boundary;
    TEST_EXPECT(ctx, reflect(request, exact, boundary) == EStatus::success);
    TEST_EXPECT(ctx, reflect(request, {}, good) == EStatus::invalid_input);
    check_schema(ctx, good);
}

static void allocation_failures(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    tests::TAllocationFailureFixture failure;
    memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    const auto* const previous = memory::get_ambient_memory_context();
    std::size_t attempts = 0u;
    {
        SContextScope scope{ context };
        CResult result;
        TEST_EXPECT(ctx, reflect({ &report, CStringView{ "failure-sweep" } }, {}, result) == EStatus::success);
        std::cout << "ReflectionArena fixture input=" << report.shader.view().size()
            << " consumed=" << result.storage_statistics().reflection_arena_consumed
            << " capacity=" << result.storage_statistics().reflection_arena_capacity << '\n';
        attempts = failure.attempts;
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    TEST_EXPECT(ctx, (attempts > 5u) && (attempts < 2000u));
    if (attempts >= 2000u)
    {
        return;
    }
    for (std::size_t index = 0u; index < attempts; ++index)
    {
        failure = {};
        failure.fail_on = index;
        {
            SContextScope scope{ context };
            CResult result;
            const auto status = reflect({ &report, CStringView{ "failure-sweep" } }, {}, result);
            TEST_EXPECT(ctx, failure.failed);
            TEST_EXPECT(ctx, status != EStatus::success);
            TEST_EXPECT(ctx, result.schema() == nullptr);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, memory::get_ambient_memory_context() == previous);
}

static void early_failure_retention(tests::TTestContext& ctx, const shader::CShaderCompileReport& compiled)
{
    shader::CShaderCompileReport report;
    const CStringView expected{ "opaque / failure?label=\xc2\xa3" };
    for (std::uint32_t test = 0u; test < 4u; ++test)
    {
        CResult result;
        char identity[]{ "opaque / failure?label=\xc2\xa3" };
        SRequest request{ &report, CStringView{ identity } };
        SLimits policy;
        EStatus status = EStatus::blocked_by_compilation;
        EReason reason = EReason::compilation_failed;
        if (test == 1u)
        {
            request.compilation = nullptr;
            status = EStatus::invalid_input;
            reason = EReason::invalid_request;
        }
        if (test == 2u)
        {
            report.status = shader::EStatus::success;
            status = EStatus::invalid_input;
            reason = EReason::context_mismatch;
        }
        if (test == 3u)
        {
            request.compilation = &compiled;
            policy.artifact_bytes = 1u;
            status = EStatus::limit_exceeded;
            reason = EReason::artifact_limit;
        }
        TEST_EXPECT(ctx, reflect(request, policy, result) == status);
        identity[0u] = 'X';
        TEST_EXPECT(ctx, result.component_identity() == expected);
        TEST_EXPECT(ctx, result.diagnostic().reason == reason);
        TEST_EXPECT(ctx, result.schema() == nullptr);
        TEST_EXPECT(ctx, result.storage_statistics().retained_bytes > sizeof(CResult) + expected.length());
        TEST_EXPECT(ctx, !result.diagnostics_truncated());
    }
    report.status = shader::EStatus::not_executed;
    //  Zero allowance loses identity and text; one retains identity but forces
    //  the failure-only diagnostic allocation to fail persistently.
    for (std::size_t allowance = 0u; allowance <= 1u; ++allowance)
    {
        tests::TAllocationAllowanceFixture failure{ allowance };
        memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_allowance, tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            SContextScope scope{ context };
            CResult result;
            TEST_EXPECT(ctx, reflect({ &report, expected }, {}, result) == EStatus::blocked_by_compilation);
            TEST_EXPECT(ctx, result.diagnostic().reason == EReason::compilation_failed);
            TEST_EXPECT(ctx, result.schema() == nullptr);
            TEST_EXPECT(ctx, result.diagnostics().length() == 0u);
            TEST_EXPECT(ctx, result.diagnostics_truncated());
            TEST_EXPECT(ctx, result.component_identity() == ((allowance == 0u) ? CStringView{} : expected));
            TEST_EXPECT(ctx, (result.storage_statistics().retained_bytes == sizeof(CResult)) == (allowance == 0u));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    SLimits policy;
    policy.retained_bytes = sizeof(CResult);
    CResult capped;
    TEST_EXPECT(ctx, reflect({ &report, expected }, policy, capped) == EStatus::blocked_by_compilation);
    TEST_EXPECT(ctx, capped.diagnostic().reason == EReason::compilation_failed);
    TEST_EXPECT(ctx, capped.diagnostics_truncated());
    TEST_EXPECT(ctx, capped.storage_statistics().retained_bytes == sizeof(CResult));
}

static void resources(tests::TTestContext& ctx)
{
    shader::CShaderCompileReport report;
    const bool ready = compile(report, R"(
        [[vk::binding(1, 2)]] Texture2D<float4> textures[6];
        [[vk::binding(2, 2)]] SamplerState sampling[1];
        [[vk::binding(3, 2)]] globallycoherent RWTexture2D<float4> output_image;
        [[vk::binding(4, 2)]] Buffer<uint> input_texels;
        [[vk::binding(5, 2)]] RWBuffer<uint> output_texels;
        [numthreads(8,4,1)] void main()
        {
            output_image[uint2(0,0)] = textures[5].SampleLevel(sampling[0], float2(.25,.5), 0);
            output_texels[0] = input_texels[0];
        }
    )");
    TEST_EXPECT(ctx, ready);
    if (!ready)
    {
        return;
    }
    CResult result;
    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "image resources" } }, {}, result) == EStatus::success);
    if (result.schema() == nullptr)
    {
        std::cerr << "Resource reflection reason: " << static_cast<unsigned>(result.diagnostic().reason) <<
            " id " << result.diagnostic().result_id << '\n';
        return;
    }
    check_schema(ctx, result);
    const auto& document = result.schema()->document();
    const auto types = document.object_child(document.root(), CStringView{ "types" });
    const auto associations = document.object_child(types, CStringView{ "associations" });
    const auto interfaces = document.object_child(types, CStringView{ "interfaces" });
    const auto group = document.object_child(interfaces, CStringView{ "resources" });
    const auto members = document.object_child(group, CStringView{ "members" });
    static const char* const names[]{ "textures", "sampling", "output_image", "input_texels", "output_texels" };
    static const char* const kinds[]{ "sampled_image", "sampler", "storage_image", "uniform_texel_buffer", "storage_texel_buffer" };
    auto occurrence = document.first_child(members);
    for (std::uint32_t index = 0u; index < 5u; ++index)
    {
        char name[]{ "r_0" };
        name[2u] = static_cast<char>('0' + index);
        const auto details = document.object_child(occurrence, CStringView{ "decorations" });
        const auto declaration = document.object_child(associations, CStringView{ name });
        const auto resource = document.object_child(declaration, CStringView{ "decorations" });
        TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.source.name" })) == CStringView{ names[index] });
        TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.vulkan.set" })) == CStringView{ "2" });
        char binding[]{ "1" };
        binding[0u] = static_cast<char>('1' + index);
        TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.vulkan.binding" })) == CStringView{ binding });
        TEST_EXPECT(ctx, document.string_value(document.object_child(resource, CStringView{ "rendering.resource.kind" })) == CStringView{ kinds[index] });
        if (index == 0u)
        {
            TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.descriptor_array.rank" })) == CStringView{ "1" });
            TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.descriptor_array.extent.0" })) == CStringView{ "6" });
            std::uint64_t count = 0u;
            TEST_EXPECT(ctx, document.unsigned_integer_value(document.object_child(occurrence, CStringView{ "count" }), count) && count == 6u);
            TEST_EXPECT(ctx, document.string_value(document.object_child(resource, CStringView{ "rendering.spirv.image_format" })) == CStringView{ "Unknown" });
        }
        if (index >= 3u)
        {
            TEST_EXPECT(ctx, document.string_value(document.object_child(resource, CStringView{ "rendering.image.dimension" })) == CStringView{ "buffer" });
            TEST_EXPECT(ctx, document.string_value(document.object_child(resource, CStringView{ "rendering.image.sampled_scalar" })) == CStringView{ "u32" });
            TEST_EXPECT(ctx, document.string_value(document.object_child(resource, CStringView{ "rendering.buffer.interpretation" })) == CStringView{ "typed" });
        }
        if (index == 1u)
        {
            TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.descriptor_array.rank" })) == CStringView{ "1" });
            TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.descriptor_array.extent.0" })) == CStringView{ "1" });
        }
        if (index == 2u)
        {
            TEST_EXPECT(ctx, document.string_value(document.object_child(details, CStringView{ "rendering.spirv.decoration.Coherent" })) == CStringView{ "true" });
            TEST_EXPECT(ctx, !document.object_child(resource, CStringView{ "rendering.spirv.decoration.Coherent" }).is_valid());
        }
        occurrence = document.next_sibling(occurrence);
    }
    for (std::uint32_t test = 0u; test < 3u; ++test)
    {
        SLimits policy;
        if (test == 0u)
        {
            policy.resources = 4u;
        }
        if (test == 1u)
        {
            policy.array_rank = 0u;
        }
        if (test == 2u)
        {
            policy.aggregate_members = 4u;
        }
        CResult rejected;
        TEST_EXPECT(ctx, reflect({ &report, CStringView{ "resource limits" } }, policy, rejected) == EStatus::limit_exceeded);
        TEST_EXPECT(ctx, rejected.schema() == nullptr);
    }
    allocation_failures(ctx, report);
}

static void buffer_layouts(tests::TTestContext& ctx)
{
    shader::CShaderCompileReport report;
    const bool ready = compile(report, R"(
        struct Scalar { float value; };
        struct Vec { float3 value; };
        struct Nested { float head; Scalar s; float between; Vec v; float tail; };
        struct Arrays { Scalar scalars[2]; Vec vectors[2]; };
        struct Matrices { row_major float2x3 row; column_major float2x3 column; };
        [[vk::binding(0)]] ConstantBuffer<Nested> un;
        [[vk::binding(1)]] ConstantBuffer<Arrays> ua;
        [[vk::binding(2)]] ConstantBuffer<Scalar> us;
        [[vk::binding(3)]] ConstantBuffer<Vec> uv;
        [[vk::binding(4)]] StructuredBuffer<Nested> sn;
        [[vk::binding(5)]] StructuredBuffer<Arrays> sa;
        [[vk::binding(6)]] StructuredBuffer<Scalar> ss;
        [[vk::binding(7)]] StructuredBuffer<Vec> sv;
        [[vk::binding(8)]] RWStructuredBuffer<float> output;
        [[vk::binding(9)]] ConstantBuffer<Matrices> um;
        [[vk::binding(10)]] StructuredBuffer<Matrices> sm;
        [numthreads(8,4,1)] void main()
        {
            output[0] = un.head + un.s.value + un.between + un.v.value.x + un.tail +
                ua.scalars[1].value + ua.vectors[1].value.x + us.value + uv.value.x +
                sn[0].head + sn[0].s.value + sn[0].between + sn[0].v.value.x + sn[0].tail +
                sa[0].scalars[1].value + sa[0].vectors[1].value.x + ss[1].value + sv[1].value.x +
                um.row[1][2] + um.column[1][2] + sm[0].row[1][2] + sm[0].column[1][2];
        }
    )");
    TEST_EXPECT(ctx, ready);
    if (!ready)
    {
        return;
    }
    CResult result;
    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "buffer layouts" } }, {}, result) == EStatus::success);
    if (result.schema() == nullptr)
    {
        std::cerr << "Buffer reflection reason: " << static_cast<unsigned>(result.diagnostic().reason) <<
            " id " << result.diagnostic().result_id << " member " << result.diagnostic().member << '\n';
        return;
    }
    check_schema(ctx, result);
    const auto& document = result.schema()->document();
    const auto types = document.object_child(document.root(), CStringView{ "types" });
    const auto associations = document.object_child(types, CStringView{ "associations" });
    const auto structures = document.object_child(types, CStringView{ "structures" });
    const auto interfaces = document.object_child(types, CStringView{ "interfaces" });
    const auto property = [&](const CNodeKey node, const char* key)
    {
        return document.object_child(node, CStringView{ key });
    };
    const auto number = [&](const CNodeKey node, const char* key, const std::uint64_t expected)
    {
        std::uint64_t value = 0u;
        TEST_EXPECT(ctx, document.unsigned_integer_value(property(node, key), value) && value == expected);
    };
    const auto member = [&](const CNodeKey node, const std::uint32_t ordinal)
    {
        auto value = document.first_child(property(node, "members"));
        for (std::uint32_t index = 0u; index < ordinal; ++index)
        {
            value = document.next_sibling(value);
        }
        return value;
    };
    CNodeKey physical[11]{};
    static const std::uint32_t sizes[]{ 80u, 64u, 16u, 16u, 48u, 48u, 4u, 16u, 4u, 80u, 64u };
    for (std::uint32_t index = 0u; index < 11u; ++index)
    {
        char name[32]{};
        const auto generated = std::string("r_") + std::to_string(index);
        std::memcpy(name, generated.c_str(), generated.size() + 1u);
        const auto resource = document.object_child(associations, CStringView{ name });
        auto target = document.string_value(property(resource, "target"));
        if (((index >= 4u) && (index <= 8u)) || (index == 10u))
        {
            const auto layout = document.object_child(interfaces, target);
            const auto region = member(layout, 0u);
            const auto sequence = document.object_child(associations, document.string_value(property(region, "type")));
            const auto details = property(sequence, "decorations");
            TEST_EXPECT(ctx, document.string_value(property(details, "rendering.sequence.kind")) == CStringView{ "runtime" });
            const auto stride = std::to_string(sizes[index]);
            TEST_EXPECT(ctx, document.string_value(property(details, "rendering.sequence.stride")) == CStringView{ stride.c_str() });
            if (index != 8u)
            {
                TEST_EXPECT(ctx, document.string_value(property(property(region, "decorations"), "rendering.spirv.decoration.NonWritable")) == CStringView{ "true" });
            }
            TEST_EXPECT(ctx, !property(property(resource, "decorations"), "rendering.spirv.decoration.NonWritable").is_valid());
            target = document.string_value(property(sequence, "target"));
        }
        if (index == 8u)
        {
            TEST_EXPECT(ctx, target == CStringView{ "f32" });
            continue;
        }
        physical[index] = document.object_child(structures, target);
        const auto details = property(physical[index], "detail");
        number(details, "size", sizes[index]);
        number(details, "alignment", (index == 6u) ? 4u : 16u);
    }
    static const std::uint32_t uniform_offsets[]{ 0u, 16u, 32u, 48u, 64u };
    static const std::uint32_t storage_offsets[]{ 0u, 4u, 8u, 16u, 32u };
    for (std::uint32_t index = 0u; index < 5u; ++index)
    {
        number(member(physical[0u], index), "offset", uniform_offsets[index]);
        number(member(physical[4u], index), "offset", storage_offsets[index]);
    }
    //  The same raw Scalar type is used under both contexts, but its physical
    //  declaration cannot be shared: UBO extent/alignment16 versus storage4.
    const auto uniform_scalar = document.string_value(property(member(physical[0u], 1u), "type"));
    const auto storage_scalar = document.string_value(property(member(physical[4u], 1u), "type"));
    TEST_EXPECT(ctx, !(uniform_scalar == storage_scalar));
    number(property(document.object_child(structures, uniform_scalar), "detail"), "size", 16u);
    number(property(document.object_child(structures, storage_scalar), "detail"), "size", 4u);
    number(property(member(physical[1u], 0u), "type"), "stride", 16u);
    number(property(member(physical[5u], 0u), "type"), "stride", 4u);
    number(member(physical[5u], 1u), "offset", 16u);
    for (const auto index : { 9u, 10u })
    {
        const auto row = property(member(physical[index], 0u), "type");
        const auto column = property(member(physical[index], 1u), "type");
        number(row, "count", 2u);
        number(property(row, "element"), "count", 3u);
        number(row, "stride", 16u);
        number(column, "count", 3u);
        number(property(column, "element"), "count", 2u);
        number(column, "stride", (index == 9u) ? 16u : 8u);
        TEST_EXPECT(ctx, document.string_value(property(property(row, "decorations"), "rendering.matrix.basis")) == CStringView{ "spirv" });
        TEST_EXPECT(ctx, document.string_value(property(property(row, "decorations"), "rendering.matrix.storage_major")) == CStringView{ "column" });
        TEST_EXPECT(ctx, document.string_value(property(property(column, "decorations"), "rendering.matrix.storage_major")) == CStringView{ "row" });
    }
    //  Exercise the existing CPU construction path, including aggregate tails.
    schema::CLiveTypes live_types;
    schema::CLiveInstances instances;
    schema::CLiveBulkData bulk;
    schema::SSchemaDiagnostic schema_error;
    const bool materialised = result.schema()->materialise(live_types, instances, bulk, schema_error);
    TEST_EXPECT(ctx, materialised);
    if (materialised)
    {
        instances.clear();
        TEST_EXPECT(ctx, instances.initialise(live_types));
        CLiveDocument literal;
        TEST_EXPECT(ctx, tests::parse_document("[1.0,[2.0],3.0,[[4.0,5.0,6.0]],7.0]", literal));
        const schema::CInstanceDocumentQuery values{ literal };
        schema::SInstanceDiagnostic instance_error;
        for (const auto index : { 0u, 4u })
        {
            const auto instance = instances.create_base(document.name(physical[index]), CStringView{ "cpu" }, values, values.root(), instance_error);
            schema::SInstanceEntryView entry;
            const bool constructed = instances.entry(instance, entry);
            TEST_EXPECT(ctx, constructed && entry.byte_count == sizes[index]);
            if (!constructed)
            {
                std::cerr << "Reflected CPU construction reason: " << static_cast<unsigned>(instance_error.reason) << '\n';
            }
            if (!constructed)
            {
                continue;
            }
            const auto* const offsets = (index == 0u) ? uniform_offsets : storage_offsets;
            for (std::uint32_t field = 0u; field < 5u; ++field)
            {
                float value = 0.0f;
                std::memcpy(&value, entry.bytes + offsets[field], sizeof(value));
                TEST_EXPECT(ctx, value == ((field == 4u) ? 7.0f : static_cast<float>(field + 1u)));
            }
            for (auto offset = offsets[4u] + 4u; offset < sizes[index]; ++offset)
            {
                TEST_EXPECT(ctx, entry.bytes[offset] == 0u);
            }
        }
    }
    rendering::shader_reflection::detail::CModule reflected;
    bool size_rejected = false;
    TEST_EXPECT(ctx, reflected.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
    const auto& raw = reflected.value();
    if (raw.descriptor_binding_count == 11u)
    {
        const auto& uniform = raw.descriptor_bindings[0u].block;
        const auto& storage = raw.descriptor_bindings[4u].block;
        TEST_EXPECT(ctx, uniform.member_count == 5u && storage.member_count == 1u && storage.members[0u].member_count == 5u);
        if (uniform.member_count == 5u && storage.member_count == 1u && storage.members[0u].member_count == 5u)
        {
            TEST_EXPECT(ctx, uniform.members[1u].type_description->id == storage.members[0u].members[1u].type_description->id);
        }
        //  Move the following member into a structure's rounded tail, then into
        //  an array's last stride slot. Neither may be clipped to make it fit.
        for (const auto binding : { 0u, 1u })
        {
            auto* words = reinterpret_cast<std::uint32_t*>(report.shader.bytes.data());
            const auto count = report.shader.view().size() / 4u;
            const auto id = raw.descriptor_bindings[binding].block.type_description->id;
            const auto member_index = (binding == 0u) ? 2u : 1u;
            bool mutated = false;
            for (std::size_t offset = 5u; offset < count; offset += words[offset] >> 16u)
            {
                if ((words[offset] == ((5u << 16u) | SpvOpMemberDecorate)) && (words[offset + 1u] == id) &&
                    (words[offset + 2u] == member_index) && (words[offset + 3u] == SpvDecorationOffset))
                {
                    const auto original = words[offset + 4u];
                    words[offset + 4u] = (binding == 0u) ? 20u : 16u;
                    CResult rejected;
                    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "overlapping tail" } }, {}, rejected) == EStatus::unrepresentable_layout);
                    TEST_EXPECT(ctx, rejected.diagnostic().reason == EReason::layout_conflict);
                    TEST_EXPECT(ctx, rejected.schema() == nullptr);
                    words[offset + 4u] = original;
                    mutated = true;
                    break;
                }
            }
            TEST_EXPECT(ctx, mutated);
        }
    }
    allocation_failures(ctx, report);
}

static void packed_buffers(tests::TTestContext& ctx)
{
    shader::CShaderCompileReport report;
    const bool ready = compile(report, R"(
        struct Packed { float a; float3 b; float3 c; float d; float2 e; float2 f; float grid[2][3]; };
        [[vk::binding(0)]] ConstantBuffer<Packed> u;
        [[vk::binding(1)]] StructuredBuffer<Packed> s;
        [[vk::binding(2)]] RWByteAddressBuffer output;
        [[vk::binding(3)]] ByteAddressBuffer input;
        [numthreads(8,4,1)] void main()
        {
            output.Store(0, input.Load(0) + asuint(u.a + u.b.x + u.c.x + u.d + u.e.x + u.f.x + u.grid[1][2] +
                s[0].a + s[0].b.x + s[0].c.x + s[0].d + s[0].e.x + s[0].f.x + s[0].grid[1][2]));
        }
    )");
    TEST_EXPECT(ctx, ready);
    if (!ready)
    {
        return;
    }
    CResult result;
    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "packed / raw" } }, {}, result) == EStatus::success);
    if (result.schema() == nullptr)
    {
        return;
    }
    const auto& doc = result.schema()->document();
    const auto property = [&](const CNodeKey node, const char* key)
    {
        return doc.object_child(node, CStringView{ key });
    };
    const auto types = property(doc.root(), "types");
    const auto associations = property(types, "associations");
    const auto structures = property(types, "structures");
    const auto interfaces = property(types, "interfaces");
    for (std::uint32_t index = 0u; index < 4u; ++index)
    {
        char name[]{ "r_0" };
        name[2u] += static_cast<char>(index);
        const auto resource = property(associations, name);
        const auto details = property(resource, "decorations");
        if (index >= 2u)
        {
            TEST_EXPECT(ctx, doc.string_value(property(details, "rendering.buffer.interpretation")) == CStringView{ "raw" });
            continue;
        }
        auto target = doc.string_value(property(resource, "target"));
        if (index == 1u)
        {
            const auto block = doc.object_child(interfaces, target);
            const auto region = doc.first_child(property(block, "members"));
            const auto sequence = doc.object_child(associations, doc.string_value(property(region, "type")));
            TEST_EXPECT(ctx, doc.string_value(property(property(sequence, "decorations"), "rendering.sequence.stride")) == CStringView{ "80" });
            target = doc.string_value(property(sequence, "target"));
        }
        const auto structure = doc.object_child(structures, target);
        std::uint64_t size = 0u;
        TEST_EXPECT(ctx, doc.unsigned_integer_value(property(property(structure, "detail"), "size"), size) && size == ((index == 0u) ? 144u : 80u));
        auto field = doc.first_child(property(structure, "members"));
        static const std::uint32_t offsets[]{ 0u, 4u, 16u, 28u, 32u, 40u, 48u };
        for (std::uint32_t ordinal = 0u; ordinal < 7u; ++ordinal)
        {
            std::uint64_t offset = 0u;
            TEST_EXPECT(ctx, doc.unsigned_integer_value(property(field, "offset"), offset) && offset == offsets[ordinal]);
            if (ordinal == 6u)
            {
                const auto outer = property(field, "type");
                const auto inner = property(outer, "element");
                std::uint64_t stride = 0u;
                TEST_EXPECT(ctx, doc.unsigned_integer_value(property(outer, "stride"), stride) && stride == ((index == 0u) ? 48u : 12u));
                TEST_EXPECT(ctx, doc.unsigned_integer_value(property(inner, "stride"), stride) && stride == ((index == 0u) ? 16u : 4u));
            }
            field = doc.next_sibling(field);
        }
    }
}

static void fixed_prefix_rejection(tests::TTestContext& ctx, const shader::CShaderCompileReport& source)
{
    rendering::shader_reflection::detail::CModule module;
    bool size_rejected = false;
    TEST_EXPECT(ctx, module.create(source.shader.view().size(), source.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
    TEST_EXPECT(ctx, module.value().descriptor_binding_count == 4u);
    if (module.value().descriptor_binding_count != 4u)
    {
        return;
    }
    const auto& binding = module.value().descriptor_bindings[3u];
    const auto block = binding.block.type_description->id;
    const auto* const words = reinterpret_cast<const std::uint32_t*>(source.shader.view().data());
    const auto count = source.shader.view().size() / 4u;
    std::uint32_t vector = 0u, one = 0u;
    for (std::size_t offset = 5u; offset < count; offset += words[offset] >> 16u)
    {
        if ((words[offset] == ((4u << 16u) | SpvOpTypeVector)) && (words[offset + 3u] == 3u))
        {
            vector = words[offset + 1u];
        }
        if ((words[offset] == ((4u << 16u) | SpvOpConstant)) && (words[offset + 3u] == 1u))
        {
            one = words[offset + 2u];
        }
    }
    TEST_EXPECT(ctx, vector != 0u && one != 0u);
    if (vector == 0u || one == 0u)
    {
        return;
    }
    //  Controlled operand fixture: insert float3 at offset0 before the existing
    //  runtime float array at offset12. Adjust accesses to the moved array.
    //  This exercises explicit rejection, not arbitrary-binary admission.
    std::vector<std::uint32_t> modified(words, words + 5u);
    bool annotation = false, structure = false, access = false;
    for (std::size_t offset = 5u; offset < count; offset += words[offset] >> 16u)
    {
        const auto opcode = words[offset] & 0xffffu;
        const auto length = words[offset] >> 16u;
        if (!annotation && (opcode >= SpvOpTypeVoid) && (opcode <= SpvOpTypeForwardPointer))
        {
            const std::uint32_t added[]{ (5u << 16u) | SpvOpMemberDecorate, block, 0u, SpvDecorationOffset, 0u };
            modified.insert(modified.end(), added, added + 5u);
            annotation = true;
        }
        if (opcode == SpvOpTypeStruct && words[offset + 1u] == block)
        {
            TEST_EXPECT(ctx, length == 3u);
            const std::uint32_t replacement[]{ (4u << 16u) | SpvOpTypeStruct, block, vector, words[offset + 2u] };
            modified.insert(modified.end(), replacement, replacement + 4u);
            structure = true;
            continue;
        }
        const auto start = modified.size();
        modified.insert(modified.end(), words + offset, words + offset + length);
        if (((opcode == SpvOpMemberDecorate) || (opcode == SpvOpMemberName) || (opcode == SpvOpMemberDecorateStringGOOGLE)) &&
            (words[offset + 1u] == block))
        {
            modified[start + 2u] = 1u;
            if (opcode == SpvOpMemberDecorate && words[offset + 3u] == SpvDecorationOffset)
            {
                modified[start + 4u] = 12u;
            }
        }
        if (((opcode == SpvOpAccessChain) || (opcode == SpvOpInBoundsAccessChain)) && length >= 6u && words[offset + 3u] == binding.spirv_id)
        {
            modified[start + 4u] = one;
            access = true;
        }
    }
    TEST_EXPECT(ctx, annotation && structure && access);
    shader::CShaderCompileReport report;
    report.status = source.status;
    report.stage = report.shader.stage = source.stage;
    report.options = source.options;
    report.shader.target = source.shader.target;
    TEST_EXPECT(ctx, report.entry_point.set(source.entry_point.cstring()));
    const bool copied = report.shader.bytes.resize(modified.size() * 4u, 4u);
    TEST_EXPECT(ctx, copied);
    if (!copied)
    {
        return;
    }
    std::memcpy(report.shader.bytes.data(), modified.data(), modified.size() * 4u);
    CResult rejected;
    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "fixed prefix" } }, {}, rejected) == EStatus::unsupported_feature);
    TEST_EXPECT(ctx, rejected.diagnostic().reason == EReason::unsupported_resource);
    TEST_EXPECT(ctx, rejected.diagnostic().has_result_id && rejected.diagnostic().result_id == block);
    TEST_EXPECT(ctx, rejected.schema() == nullptr);
}

static void runtime_elements(tests::TTestContext& ctx)
{
    shader::CShaderCompileReport report;
    const bool ready = compile(report, R"(
        typedef row_major float2x3 Row;
        typedef column_major float2x3 Column;
        typedef float3 FloatTriples[2][3];
        [[vk::binding(0)]] StructuredBuffer<Row> rows;
        [[vk::binding(1)]] StructuredBuffer<Column> columns;
        [[vk::binding(2)]] StructuredBuffer<FloatTriples> arrays;
        [[vk::binding(3)]] RWStructuredBuffer<float> output;
        [numthreads(8,4,1)] void main()
        {
            output[0] = rows[0][1][2] + columns[0][1][2] + arrays[0][1][2].x;
        }
    )");
    TEST_EXPECT(ctx, ready);
    if (!ready)
    {
        return;
    }
    CResult result;
    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "runtime elements" } }, {}, result) == EStatus::success);
    if (result.schema() == nullptr)
    {
        return;
    }
    check_schema(ctx, result);
    const auto& doc = result.schema()->document();
    const auto property = [&](const CNodeKey node, const char* key)
    {
        return doc.object_child(node, CStringView{ key });
    };
    const auto number = [&](const CNodeKey node, const char* key, const std::uint64_t expected)
    {
        std::uint64_t value = 0u;
        TEST_EXPECT(ctx, doc.unsigned_integer_value(property(node, key), value) && value == expected);
    };
    const auto types = property(doc.root(), "types");
    const auto associations = property(types, "associations");
    const auto structures = property(types, "structures");
    const auto interfaces = property(types, "interfaces");
    schema::CLiveTypes live_types;
    schema::CLiveInstances instances;
    schema::CLiveBulkData bulk;
    schema::SSchemaDiagnostic error;
    const bool materialised = result.schema()->materialise(live_types, instances, bulk, error);
    TEST_EXPECT(ctx, materialised);
    if (!materialised)
    {
        return;
    }
    instances.clear();
    TEST_EXPECT(ctx, instances.initialise(live_types));
    static const std::uint32_t sizes[]{ 32u, 24u, 96u };
    static const char* const strides[]{ "32", "24", "96" };
    static const char* const literals[]{
        R"({"storage":[[1.0,2.0,3.0],[4.0,5.0,6.0]]})",
        R"({"storage":[[1.0,2.0],[3.0,4.0],[5.0,6.0]]})",
        R"({"storage":[[[1.0,2.0,3.0],[4.0,5.0,6.0],[7.0,8.0,9.0]],[[10.0,11.0,12.0],[13.0,14.0,15.0],[16.0,17.0,18.0]]]})"
    };
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        char name[]{ "r_0" };
        name[2u] += static_cast<char>(index);
        const auto association = property(associations, name);
        const auto block = doc.object_child(interfaces, doc.string_value(property(association, "target")));
        const auto region = doc.first_child(property(block, "members"));
        const auto sequence = doc.object_child(associations, doc.string_value(property(region, "type")));
        const auto wrapper = doc.object_child(structures, doc.string_value(property(sequence, "target")));
        const auto storage = doc.first_child(property(wrapper, "members"));
        const auto array = property(storage, "type");
        TEST_EXPECT(ctx, doc.string_value(property(property(sequence, "decorations"), "rendering.sequence.stride")) == CStringView{ strides[index] });
        TEST_EXPECT(ctx, doc.child_count(property(wrapper, "members")) == 1u && doc.name(storage) == CStringView{ "storage" });
        TEST_EXPECT(ctx, !property(wrapper, "decorations").is_valid() && !property(storage, "decorations").is_valid());
        number(storage, "offset", 0u);
        number(property(wrapper, "detail"), "size", sizes[index]);
        number(property(wrapper, "detail"), "alignment", (index == 1u) ? 8u : 16u);
        number(array, "count", (index == 1u) ? 3u : 2u);
        number(property(array, "element"), "count", (index == 1u) ? 2u : 3u);
        number(array, "stride", (index == 0u) ? 16u : ((index == 1u) ? 8u : 48u));
        number(property(array, "element"), "stride", (index == 2u) ? 16u : 4u);
        const auto matrix = property(array, "decorations");
        if (index < 2u)
        {
            TEST_EXPECT(ctx, doc.string_value(property(matrix, "rendering.matrix.basis")) == CStringView{ "spirv" });
            TEST_EXPECT(ctx, doc.string_value(property(matrix, "rendering.matrix.storage_major")) == CStringView{ (index == 0u) ? "column" : "row" });
        }
        else
        {
            TEST_EXPECT(ctx, !property(matrix, "rendering.matrix.basis").is_valid());
        }
        CLiveDocument literal;
        TEST_EXPECT(ctx, tests::parse_document(literals[index], literal));
        const schema::CInstanceDocumentQuery values{ literal };
        schema::SInstanceDiagnostic instance_error;
        const auto instance = instances.create_base(doc.name(wrapper), CStringView{ "cpu" }, values, values.root(), instance_error);
        schema::SInstanceEntryView entry;
        const bool constructed = instances.entry(instance, entry);
        TEST_EXPECT(ctx, constructed && entry.byte_count == sizes[index]);
        if (!constructed)
        {
            continue;
        }
        const auto elements = (index == 2u) ? 18u : 6u;
        for (std::uint32_t scalar = 0u; scalar < elements; ++scalar)
        {
            const auto offset = (index == 1u) ? scalar * 4u : ((scalar / 3u) * 16u) + ((scalar % 3u) * 4u);
            float value = 0.0f;
            std::memcpy(&value, entry.bytes + offset, sizeof(value));
            TEST_EXPECT(ctx, value == static_cast<float>(scalar + 1u));
        }
        if (index != 1u)
        {
            for (std::uint32_t slot = 0u; slot < sizes[index]; slot += 16u)
            {
                for (std::uint32_t padding = 12u; padding < 16u; ++padding)
                {
                    TEST_EXPECT(ctx, entry.bytes[slot + padding] == 0u);
                }
            }
        }
    }
    //  The synthesized storage wrappers consume the same budgets as authored
    //  declarations/members. Count the complete result independently.
    const auto declarations = doc.child_count(interfaces) + doc.child_count(associations) + doc.child_count(structures);
    std::uint32_t members = 0u;
    for (const auto category : { interfaces, structures })
    {
        for (auto declaration = doc.first_child(category); declaration.is_valid(); declaration = doc.next_sibling(declaration))
        {
            members += doc.child_count(property(declaration, "members"));
        }
    }
    SLimits exact;
    exact.declarations = declarations;
    exact.members = members;
    CResult boundary;
    TEST_EXPECT(ctx, reflect({ &report, CStringView{ "exact wrapper limits" } }, exact, boundary) == EStatus::success);
    for (std::uint32_t test = 0u; test < 2u; ++test)
    {
        SLimits policy;
        if (test == 0u)
        {
            policy.declarations = declarations - 1u;
        }
        else
        {
            policy.members = members - 1u;
        }
        CResult rejected;
        TEST_EXPECT(ctx, reflect({ &report, CStringView{ "wrapper limit" } }, policy, rejected) == EStatus::limit_exceeded);
        TEST_EXPECT(ctx, rejected.schema() == nullptr);
        TEST_EXPECT(ctx, rejected.diagnostic().reason == ((test == 0u) ? EReason::declaration_limit : EReason::member_limit));
    }
    allocation_failures(ctx, report);
    //  A legal larger runtime stride does not enlarge the reusable element.
    rendering::shader_reflection::detail::CModule module;
    bool size_rejected = false;
    TEST_EXPECT(ctx, module.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
    if (module.value().descriptor_binding_count != 0u)
    {
        const auto block_id = module.value().descriptor_bindings[0u].block.type_description->id;
        auto* const words = reinterpret_cast<std::uint32_t*>(report.shader.bytes.data());
        const auto count = report.shader.view().size() / 4u;
        std::uint32_t array_id = 0u;
        for (std::size_t offset = 5u; offset < count; offset += words[offset] >> 16u)
        {
            if (words[offset] == ((3u << 16u) | SpvOpTypeStruct) && words[offset + 1u] == block_id)
            {
                array_id = words[offset + 2u];
            }
        }
        bool changed = false;
        for (std::size_t offset = 5u; offset < count; offset += words[offset] >> 16u)
        {
            if (words[offset] != ((4u << 16u) | SpvOpDecorate) || words[offset + 1u] != array_id ||
                words[offset + 2u] != SpvDecorationArrayStride)
            {
                continue;
            }
            const auto original = words[offset + 3u];
            words[offset + 3u] = 64u;
            CResult spaced;
            TEST_EXPECT(ctx, reflect({ &report, CStringView{ "independent stride" } }, {}, spaced) == EStatus::success);
            if (spaced.schema() != nullptr)
            {
                const auto& spaced_doc = spaced.schema()->document();
                const auto at = [&](const CNodeKey node, const char* key)
                {
                    return spaced_doc.object_child(node, CStringView{ key });
                };
                const auto catalogue = at(spaced_doc.root(), "types");
                const auto sequence = at(at(catalogue, "associations"), "q_0");
                TEST_EXPECT(ctx, spaced_doc.string_value(at(at(sequence, "decorations"), "rendering.sequence.stride")) == CStringView{ "64" });
                const auto wrapper = spaced_doc.object_child(at(catalogue, "structures"), spaced_doc.string_value(at(sequence, "target")));
                std::uint64_t size = 0u, alignment = 0u;
                TEST_EXPECT(ctx, spaced_doc.unsigned_integer_value(at(at(wrapper, "detail"), "size"), size) && size == 32u);
                TEST_EXPECT(ctx, spaced_doc.unsigned_integer_value(at(at(wrapper, "detail"), "alignment"), alignment) && alignment == 16u);
            }
            words[offset + 3u] = original;
            changed = true;
            break;
        }
        TEST_EXPECT(ctx, changed);
    }
    fixed_prefix_rejection(ctx, report);
}

static void preflight_rejections(tests::TTestContext& ctx, const shader::CShaderCompileReport& source)
{
    //  Controlled mutations exercise rejection before the upstream parser.
    //  They are not an external-module admission/validation test suite.
    for (std::uint32_t test = 0u; test < 5u; ++test)
    {
        shader::CShaderCompileReport report;
        report.status = shader::EStatus::success;
        report.stage = report.shader.stage = shader::EStage::compute;
        TEST_EXPECT(ctx, report.entry_point.set("main"));
        const auto original = source.shader.view();
        TEST_EXPECT(ctx, report.shader.bytes.resize(original.size() + 56u, 4u));
        if (!report.shader.view().is_ready())
        {
            return;
        }
        std::memcpy(report.shader.bytes.data(), original.data(), original.size());
        auto* const words = reinterpret_cast<std::uint32_t*>(report.shader.bytes.data());
        const auto id = words[3];
        words[3] += 3u;
        auto cursor = original.size() / 4u;
        // OpTypeInt %id 32 0; OpSpecConstant %id %spec 2.
        const std::uint32_t spec[]{ (4u << 16u) | 21u, id, 32u, 0u, (4u << 16u) | 50u, id, id + 1u, 2u };
        std::memcpy(words + cursor, spec, sizeof(spec));
        cursor += 8u;
        if (test == 1u)
        {
            const std::uint32_t array[]{ (4u << 16u) | 28u, id + 2u, id, id + 1u };
            std::memcpy(words + cursor, array, sizeof(array));
            cursor += 4u;
        }
        if (test == 2u)
        {
            // OpExecutionModeId (preflight classifies symbolic execution).
            const std::uint32_t mode[]{ (6u << 16u) | 331u, 1u, 38u, id + 1u, id + 1u, id + 1u };
            std::memcpy(words + cursor, mode, sizeof(mode));
            cursor += 6u;
        }
        if (test == 3u)
        {
            words[5u] = 0u;
        }
        if (test == 4u)
        {
            words[0u] = 0u;
        }
        TEST_EXPECT(ctx, report.shader.bytes.resize(cursor * 4u, 4u));
        CResult result;
        SLimits limits;
        limits.diagnostic_bytes = 7u;
        const auto status = reflect({ &report, CStringView{ "preflight" } }, limits, result);
        TEST_EXPECT(ctx, status == ((test < 3u) ? EStatus::unsupported_feature : EStatus::invalid_input));
        if (test < 3u)
        {
            const EReason reasons[]{ EReason::unsupported_specialization, EReason::specialization_shape, EReason::specialization_execution };
            TEST_EXPECT(ctx, result.diagnostic().reason == reasons[test]);
            TEST_EXPECT(ctx, result.diagnostic().has_word_offset);
        }
        TEST_EXPECT(ctx, result.schema() == nullptr);
        TEST_EXPECT(ctx, result.diagnostics_truncated());
        TEST_EXPECT(ctx, result.diagnostics().length() == 7u);
    }
    //  Child types appear before parents, so cached dependency heights must
    //  still enforce the implementation ceiling when policy requests more.
    shader::CShaderCompileReport deep;
    deep.status = shader::EStatus::success;
    deep.stage = deep.shader.stage = shader::EStage::compute;
    TEST_EXPECT(ctx, deep.entry_point.set("main"));
    const auto original = source.shader.view();
    TEST_EXPECT(ctx, deep.shader.bytes.resize(original.size() + ((4u + (33u * 3u)) * 4u), 4u));
    if (!deep.shader.view().is_ready())
    {
        return;
    }
    std::memcpy(deep.shader.bytes.data(), original.data(), original.size());
    auto* const words = reinterpret_cast<std::uint32_t*>(deep.shader.bytes.data());
    const auto base = words[3];
    words[3] += 34u;
    auto cursor = original.size() / 4u;
    const std::uint32_t scalar[]{ (4u << 16u) | 21u, base, 32u, 0u };
    std::memcpy(words + cursor, scalar, sizeof(scalar));
    cursor += 4u;
    for (std::uint32_t index = 1u; index <= 33u; ++index)
    {
        words[cursor++] = (3u << 16u) | 30u;
        words[cursor++] = base + index;
        words[cursor++] = base + index - 1u;
    }
    SLimits large_depth;
    large_depth.nesting = 100u;
    CResult rejected;
    TEST_EXPECT(ctx, reflect({ &deep, CStringView{ "deep" } }, large_depth, rejected) == EStatus::limit_exceeded);
    TEST_EXPECT(ctx, rejected.diagnostic().reason == EReason::nesting_limit);
    TEST_EXPECT(ctx, rejected.schema() == nullptr);
}

struct SNestedAllocation
{
    const shader::CShaderCompileReport* report;
    std::size_t attempts{ 0u };
    EStatus nested_status{ EStatus::not_executed };
    bool nest_on_close{ false };
    EStatus close_nested_status{ EStatus::not_executed };
};

static void* MV_STD_ABI_CALL nested_allocate(void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& nested = *static_cast<SNestedAllocation*>(state);
    //  The outer operation calls CModule::create directly: its first allocation
    //  is inside the adapter scope regardless of importer allocation order.
    if (nested.attempts++ == 0u)
    {
        CResult result;
        nested.nested_status = reflect({ nested.report, CStringView{ "nested" } }, {}, result);
    }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

static bool MV_STD_ABI_CALL nested_deallocate(void* const state, const std::size_t alignment, void* const pointer) noexcept
{
    auto& nested = *static_cast<SNestedAllocation*>(state);
    if (nested.nest_on_close)
    {
        nested.nest_on_close = false;
        CResult result;
        nested.close_nested_status = reflect({ nested.report, CStringView{ "nested-close" } }, {}, result);
    }
    return tests::deallocate_test_memory(nullptr, alignment, pointer);
}

static void allocation_arena(tests::TTestContext& ctx)
{
    using namespace rendering::shader_reflection::detail;
    tests::TAllocationFailureFixture failure;
    memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        SContextScope scope{ context };
        SAllocationOwner owner{ nullptr };
        owner.capacity = 16384u;
        TEST_EXPECT(ctx, owner.arena.allocate(owner.capacity, alignof(std::max_align_t)));
        if (!owner.arena.is_ready())
        {
            return;
        }
        std::memset(owner.arena.data(), 0xa5, owner.capacity);
        const auto attempts = failure.attempts;
        void* addresses[96]{};
        std::size_t previous_end = 0u;
        for (std::size_t i = 0u; i < 96u; ++i)
        {
            addresses[i] = owner.allocate(i);
            const auto offset = static_cast<unsigned char*>(addresses[i]) - owner.arena.data();
            TEST_EXPECT(ctx, reinterpret_cast<std::uintptr_t>(addresses[i]) % alignof(std::max_align_t) == 0u);
            TEST_EXPECT(ctx, static_cast<std::size_t>(offset) >= previous_end);
            previous_end = static_cast<std::size_t>(offset) + std::max(i, std::size_t{ 1u });
            TEST_EXPECT(ctx, owner.consumed == previous_end);
            std::memset(addresses[i], static_cast<int>(i), std::max(i, std::size_t{ 1u }));
        }
        const auto* const zeroed = static_cast<unsigned char*>(owner.allocate_zeroed(17u, 3u));
        for (std::size_t i = 0u; i < 51u; ++i)
        {
            TEST_EXPECT(ctx, zeroed[i] == 0u);
        }
        TEST_EXPECT(ctx, owner.allocate_zeroed(0u, memory::k_byte_size_ceiling) != nullptr);
        for (std::size_t i = 0u; i < 96u; ++i)
        {
            TEST_EXPECT(ctx, *static_cast<unsigned char*>(addresses[i]) == i);
        }
        TEST_EXPECT(ctx, failure.attempts == attempts);
        TEST_EXPECT(ctx, owner.consumed <= owner.capacity);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    {
        SContextScope scope{ context };
        CModule module;
        bool size_rejected = false;
        const std::uint32_t empty[]{ 0x07230203u, 0x00010000u, 0u, 1u, 0u };
        failure.reject_all = true;
        TEST_EXPECT(ctx, module.create(sizeof(empty), empty, size_rejected) == SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED);
        TEST_EXPECT(ctx, failure.failed && !size_rejected);
        TEST_EXPECT(ctx, module.arena_consumed() == 0u && module.value()._internal == nullptr);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    std::size_t capacity = 0u;
    TEST_EXPECT(ctx, SAllocationOwner::capacity_for(0u, capacity) && (capacity != 0u));
    TEST_EXPECT(ctx, !SAllocationOwner::capacity_for(memory::k_byte_size_ceiling, capacity) && (capacity == 0u));
    TEST_EXPECT(ctx, !SAllocationOwner::capacity_for(static_cast<std::size_t>(-1), capacity));
}

struct SCleanupFault
{
    rendering::shader_reflection::detail::CModule* module{ nullptr };
    std::uint32_t mode{ 0u };
    bool injected{ false };
};

static void* MV_STD_ABI_CALL cleanup_fault_allocate(void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    using TAccess = rendering::shader_reflection::detail::SModuleTestAccess;
    auto& fault = *static_cast<SCleanupFault*>(state);
    if (auto* const active = TAccess::active(); active != nullptr)
    {
        fault.module = active;
    }
    else if (!fault.injected && (fault.module != nullptr) && TAccess::created(*fault.module))
    {
        //  First extraction allocation: construction is complete, but the
        //  module and its arena are still alive. No production fault hook.
        fault.injected = true;
        if (fault.mode == 1u)
        {
            TAccess::interior_code_release(*fault.module);
        }
        else if (fault.mode == 2u)
        {
            TAccess::unknown_code_release(*fault.module);
        }
        else
        {
            TAccess::omit_code_release(*fault.module);
        }
    }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

static void ignored_vendor_frees(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    for (std::uint32_t mode = 0u; mode < 4u; ++mode)
    {
        SCleanupFault fault{ nullptr, mode };
        memory::CMemoryAllocator allocator{ &fault, &cleanup_fault_allocate, tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            SContextScope scope{ context };
            CResult result;
            SLimits policy;
            if (mode == 3u)
            {
                policy.string_bytes = 24u;
            }
            const auto status = reflect({ &report, CStringView{ "cleanup" } }, policy, result);
            TEST_EXPECT(ctx, fault.injected);
            TEST_EXPECT(ctx, status == ((mode == 3u) ? EStatus::limit_exceeded : EStatus::success));
            TEST_EXPECT(ctx, result.diagnostic().reason == ((mode == 3u) ? EReason::string_limit : EReason::none));
            TEST_EXPECT(ctx, (result.schema() == nullptr) == (mode == 3u));
            TEST_EXPECT(ctx, result.component_identity() == CStringView{ "cleanup" });
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

struct SCloseWork
{
    rendering::shader_reflection::detail::CModule* module;
    bool closed{ false };
    bool routing_restored{ false };
};

static std::uint32_t MV_STD_ABI_CALL close_on_worker(void* const argument) noexcept
{
    auto& work = *static_cast<SCloseWork*>(argument);
    using TAccess = rendering::shader_reflection::detail::SModuleTestAccess;
    auto* const previous = TAccess::active();
    work.module->close();
    work.closed = true;
    work.routing_restored = TAccess::active() == previous;
    return 0u;
}

static void adapter_cleanup(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    using namespace rendering::shader_reflection::detail;
    SNestedAllocation nested{ &report };
    memory::CMemoryAllocator allocator{ &nested, &nested_allocate, &nested_deallocate };
    memory::CMemoryContext context{ allocator };
    {
        SContextScope scope{ context };
        rendering::shader_reflection::detail::CModule module;
        bool size_rejected = false;
        TEST_EXPECT(ctx, module.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
        TEST_EXPECT(ctx, nested.nested_status == EStatus::success);
        TEST_EXPECT(ctx, SModuleTestAccess::active() == nullptr);
        const auto consumed = module.arena_consumed();
        const auto capacity = module.arena_capacity();
        nested.nest_on_close = true;
        module.close();
        TEST_EXPECT(ctx, module.arena_consumed() == consumed && module.arena_capacity() == capacity);
        TEST_EXPECT(ctx, SModuleTestAccess::active() == nullptr);
        TEST_EXPECT(ctx, nested.close_nested_status == EStatus::success);
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    {
        SContextScope scope{ context };
        rendering::shader_reflection::detail::CModule module;
        bool size_rejected = false;
        const std::uint32_t empty[]{ 0x07230203u, 0x00010000u, 0u, 1u, 0u };
        const auto ordinary = module.create(sizeof(empty), empty, size_rejected);
        TEST_EXPECT(ctx, (ordinary != SPV_REFLECT_RESULT_SUCCESS) && !size_rejected);
        TEST_EXPECT(ctx, module.value()._internal == nullptr);
        TEST_EXPECT(ctx, context.is_attribution_empty());
        const auto oversized = module.create(memory::k_byte_size_ceiling + 1u, empty, size_rejected);
        TEST_EXPECT(ctx, (oversized == SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED) && size_rejected);
        TEST_EXPECT(ctx, module.value()._internal == nullptr);
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    {
        SContextScope scope{ context };
        rendering::shader_reflection::detail::CModule module;
        bool size_rejected = false;
        TEST_EXPECT(ctx, module.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
        TEST_EXPECT(ctx, !context.is_attribution_empty());
        if (module.value()._internal != nullptr)
        {
            rendering::shader_reflection::detail::SModuleTestAccess::omit_code_release(module);
        }
        module.close();
        module.close();
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    {
        SContextScope scope{ context };
        CModule module;
        bool size_rejected = false;
        TEST_EXPECT(ctx, module.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
        SCloseWork work{ &module };
        platform::threading::CThread worker;
        const bool started = worker.create(&close_on_worker, &work);
        TEST_EXPECT(ctx, started);
        if (started)
        {
            TEST_EXPECT(ctx, worker.join_and_close());
            TEST_EXPECT(ctx, work.closed && work.routing_restored);
        }
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    {
        SContextScope scope{ context };
        CModule module;
        bool size_rejected = false;
        TEST_EXPECT(ctx, module.create(report.shader.view().size(), report.shader.view().data(), size_rejected) == SPV_REFLECT_RESULT_SUCCESS);
        //  No explicit close: noexcept fallback reclaims only the arena.
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

struct SThreadWork
{
    memory::CMemoryContext* context;
    CReflectJob* job;
};

static std::uint32_t MV_STD_ABI_CALL run_reflection(void* const argument) noexcept
{
    const auto& work = *static_cast<SThreadWork*>(argument);
    SContextScope scope{ *work.context };
    work.job->run();
    return 0u;
}

struct SJobAllocations
{
    const detail::CModule* expected{ nullptr };
    bool observed{ false };
    bool mismatch{ false };
    bool owner_released{ false };
};

static void* MV_STD_ABI_CALL job_allocate(void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& observation = *static_cast<SJobAllocations*>(state);
    const auto* const active = detail::SModuleTestAccess::active();
    if (active != nullptr)
    {
        observation.observed = true;
        observation.mismatch = observation.mismatch || (active != observation.expected);
    }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

static bool MV_STD_ABI_CALL job_deallocate(void* const state, const std::size_t alignment, void* const pointer) noexcept
{
    auto& observation = *static_cast<SJobAllocations*>(state);
    if (pointer == observation.expected)
    {
        observation.owner_released = true;
    }
    return tests::deallocate_test_memory(nullptr, alignment, pointer);
}

static void lifetime(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    //  Each observation has one worker and is read only after join. The requester
    //  constructs both owners before lending either job to its worker.
    SJobAllocations a_allocations;
    SJobAllocations b_allocations;
    memory::CMemoryAllocator a_allocator{ &a_allocations, job_allocate, job_deallocate };
    memory::CMemoryAllocator b_allocator{ &b_allocations, job_allocate, job_deallocate };
    memory::CMemoryContext a_context{ a_allocator };
    memory::CMemoryContext b_context{ b_allocator };
    CResult first, second;
    {
        CReflectJob a_job{ { &report, CStringView{ "first" } } };
        CReflectJob b_job{ { &report, CStringView{ "second" } } };
        a_job.run();
        TEST_EXPECT(ctx, a_job.result.status() == EStatus::not_executed);
        {
            SContextScope scope{ a_context };
            TEST_EXPECT(ctx, a_job.prepare());
            TEST_EXPECT(ctx, !a_job.prepare());
        }
        {
            SContextScope scope{ b_context };
            TEST_EXPECT(ctx, b_job.prepare());
        }
        a_allocations.expected = SReflectionTestAccess::module(a_job);
        b_allocations.expected = SReflectionTestAccess::module(b_job);
        TEST_EXPECT(ctx, (a_allocations.expected != nullptr) && (b_allocations.expected != nullptr));
        if ((a_allocations.expected == nullptr) || (b_allocations.expected == nullptr))
        {
            return;
        }
        TEST_EXPECT(ctx, a_allocations.expected != b_allocations.expected);
        TEST_EXPECT(ctx, !detail::SModuleTestAccess::created(*a_allocations.expected));
        TEST_EXPECT(ctx, !detail::SModuleTestAccess::created(*b_allocations.expected));
        TEST_EXPECT(ctx, !a_context.is_attribution_empty() && !b_context.is_attribution_empty());
        SThreadWork a_work{ &a_context, &a_job };
        SThreadWork b_work{ &b_context, &b_job };
        platform::threading::CThread a, b;
        const bool a_started = a.create(&run_reflection, &a_work);
        const bool b_started = b.create(&run_reflection, &b_work);
        TEST_EXPECT(ctx, a_started && b_started);
        if (a_started)
        {
            TEST_EXPECT(ctx, a.join_and_close());
        }
        if (b_started)
        {
            TEST_EXPECT(ctx, b.join_and_close());
        }
        TEST_EXPECT(ctx, a_allocations.observed && !a_allocations.mismatch);
        TEST_EXPECT(ctx, b_allocations.observed && !b_allocations.mismatch);
        TEST_EXPECT(ctx, SReflectionTestAccess::module(a_job) == a_allocations.expected);
        TEST_EXPECT(ctx, SReflectionTestAccess::module(b_job) == b_allocations.expected);
        TEST_EXPECT(ctx, !detail::SModuleTestAccess::created(*a_allocations.expected));
        TEST_EXPECT(ctx, !detail::SModuleTestAccess::created(*b_allocations.expected));
        TEST_EXPECT(ctx, !a_allocations.owner_released && !b_allocations.owner_released);
        first = std::move(a_job.result);
        second = std::move(b_job.result);
        a_job.run();
        TEST_EXPECT(ctx, a_job.result.status() == EStatus::not_executed);
    }
    TEST_EXPECT(ctx, a_allocations.owner_released && b_allocations.owner_released);
    TEST_EXPECT(ctx, !a_context.is_attribution_empty() && !b_context.is_attribution_empty());
    check_schema(ctx, first);
    check_schema(ctx, second);
    TEST_EXPECT(ctx, first.component_identity() == CStringView{ "first" });
    TEST_EXPECT(ctx, second.component_identity() == CStringView{ "second" });
    first = std::move(second);
    TEST_EXPECT(ctx, second.status() == EStatus::not_executed);
    TEST_EXPECT(ctx, first.component_identity() == CStringView{ "second" });
    check_schema(ctx, first);
    first.clear();
    TEST_EXPECT(ctx, a_context.is_attribution_empty() && b_context.is_attribution_empty());
    tests::TAllocationFailureFixture failure;
    failure.reject_all = true;
    memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        SContextScope scope{ context };
        CReflectJob job{ { &report, CStringView{ "unprepared" } } };
        TEST_EXPECT(ctx, !job.prepare());
        job.run();
        TEST_EXPECT(ctx, job.result.status() == EStatus::not_executed);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void arena_breadcrumbs(tests::TTestContext& ctx, const shader::CShaderCompileReport& report)
{
    auto owner = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());
    if (!owner)
    {
        return;
    }
    const auto events = test_environment::test_log_path("reflection-events");
    const auto direct = test_environment::test_log_path("reflection-direct");
    auto& service = *owner;
    TEST_EXPECT(ctx, service.configure_log_paths(events.c_str(), direct.c_str()));
    TEST_EXPECT(ctx, service.open_logs());
    TEST_EXPECT(ctx, debug_system::install_service(&service));
    TEST_EXPECT(ctx, service.start());
    service.publish_configuration(0u);
    TEST_EXPECT(ctx, !service.informational_event_enabled(debug_system::EEventLevel::info));
    {
        CResult result;
        TEST_EXPECT(ctx, reflect({ &report, CStringView{ "breadcrumb-fixture" } }, {}, result) == EStatus::success);
    }
    TEST_EXPECT(ctx, service.stop());
    TEST_EXPECT(ctx, debug_system::uninstall_service(&service));
    for (const auto* phase : { "create begin", "create complete", "destroy begin", "destroy complete" })
    {
        const auto expected = std::string{ "SPIRV-Reflect " } + phase + ": identity=breadcrumb-fixture";
        TEST_EXPECT(ctx, tests::file_contains(direct.c_str(), expected.c_str()));
    }
}

static void arena_measurements(tests::TTestContext& ctx)
{
    //  Generated scaling fixtures: many bindings, and a large structured layout
    //  with live arithmetic. Debug-free binaries avoid hiding expansion behind
    //  embedded source size. These samples establish margins, not a proof.
    for (std::uint32_t shape = 0u; shape < 3u; ++shape)
    {
        std::string source = "RWStructuredBuffer<float4> output : register(u0);\n";
        std::string body = "[numthreads(8,1,1)] void main(uint3 id : SV_DispatchThreadID) { float4 value = 0;\n";
        if (shape == 0u)
        {
            for (std::uint32_t i = 0u; i < 256u; ++i)
            {
                const auto name = std::to_string(i);
                source += "StructuredBuffer<float4> input" + name + " : register(t" + name + ");\n";
                body += "value += input" + name + "[id.x];\n";
            }
        }
        else
        {
            const std::uint32_t fields = (shape == 1u) ? 128u : 512u;
            source += "struct Payload {\n";
            for (std::uint32_t i = 0u; i < fields; ++i)
            {
                source += "float4 field" + std::to_string(i) + ";\n";
                body += "value += input[id.x].field" + std::to_string(i) + ";\n";
            }
            source += "}; StructuredBuffer<Payload> input : register(t0);\n";
        }
        source += body + "output[id.x] = value; }\n";
        for (const auto debug : { shader::EDebug::none, shader::EDebug::source_debug })
        {
            shader::CShaderCompileReport report;
            const bool compiled = compile(report, source.c_str(), shader::EStage::compute, debug);
            TEST_EXPECT(ctx, compiled);
            if (!compiled)
            {
                continue;
            }
            const auto identity = "arena-shape-" + std::to_string(shape) + "-debug-" + std::to_string(static_cast<unsigned int>(debug));
            CResult result;
            const auto status = reflect({ &report, CStringView{ identity.c_str() } }, {}, result);
            TEST_EXPECT(ctx, status == EStatus::success);
            const auto& usage = result.storage_statistics();
            TEST_EXPECT(ctx, usage.reflection_arena_consumed != 0u);
            TEST_EXPECT(ctx, usage.reflection_arena_consumed <= usage.reflection_arena_capacity);
            std::cout << "ReflectionArena " << identity << " input=" << report.shader.view().size()
                << " consumed=" << usage.reflection_arena_consumed << " capacity=" << usage.reflection_arena_capacity << '\n';
        }
    }
}

}   //  namespace shader_reflection_tests

int run_shader_reflection_arena_fatal(const char* const mode)
{
    //  Invoked only by an explicit child-process command. Never called by the
    //  ordinary suite, whose allocator failure sweeps remain recoverable.
    auto service = TInstance<debug_system::CDebugServiceState>::create();
    const auto events = test_environment::test_log_path("reflection-arena-events");
    const auto direct = test_environment::test_log_path("reflection-arena-direct");
    if (!service || !service->configure_log_paths(events.c_str(), direct.c_str()) ||
        !service->open_logs() || !debug_system::install_service(&*service))
    {
        return 2;
    }
    rendering::shader_reflection::detail::SAllocationOwner owner{ nullptr };
    owner.identity = CStringView{ "arena-fatal-fixture" };
    owner.input_bytes = 20u;
    owner.capacity = 64u;
    if (!owner.arena.allocate(owner.capacity, alignof(std::max_align_t)))
    {
        return 3;
    }
    if (std::strcmp(mode, "capacity") == 0)
    {
        (void)owner.allocate(owner.capacity);
        (void)owner.allocate(1u);
    }
    else if (std::strcmp(mode, "alignment") == 0)
    {
        (void)owner.allocate(owner.capacity - 1u);
        (void)owner.allocate(1u);
    }
    else if (std::strcmp(mode, "overflow") == 0)
    {
        (void)owner.allocate_zeroed(static_cast<std::size_t>(-1), 2u);
    }
    return 4;  //  Reaching here is a failed death test, not a normal test result.
}

int run_shader_reflection_tests()
{
    using namespace shader_reflection_tests;
    tests::TTestContext ctx;
    CResult retained;
    {
        shader::CShaderCompileReport report;
        const bool ready = compile(report);
        TEST_EXPECT(ctx, ready);
        if (ready)
        {
            TEST_EXPECT(ctx, report.shader.view().size() > 4096u);
            const auto before = report.shader.bytes.const_view();
            const std::string bytes{ reinterpret_cast<const char*>(before.data()), before.size() };
            TEST_EXPECT(ctx, reflect({ &report, CStringView{ "caller identity / untouched" } }, {}, retained) == EStatus::success);
            check_schema(ctx, retained);
            limits(ctx, report);
            preflight_rejections(ctx, report);
            lifetime(ctx, report);
            adapter_cleanup(ctx, report);
            allocation_arena(ctx);
            ignored_vendor_frees(ctx, report);
            arena_breadcrumbs(ctx, report);
            allocation_failures(ctx, report);
            early_failure_retention(ctx, report);
            resources(ctx);
            buffer_layouts(ctx);
            packed_buffers(ctx);
            runtime_elements(ctx);
            stage_interfaces(ctx);
            stage_aggregates(ctx);
            arena_measurements(ctx);
            execution_rejections(ctx, report);
            TEST_EXPECT(ctx, report.status == shader::EStatus::success);
            TEST_EXPECT(ctx, (report.shader.view().size() == bytes.size()) &&
                (std::memcmp(report.shader.view().data(), bytes.data(), bytes.size()) == 0));
        }
    }
    if (retained.schema() != nullptr)
    {
        check_schema(ctx, retained);
    }
    CResult blocked;
    shader::CShaderCompileReport unsuccessful;
    TEST_EXPECT(ctx, reflect({ &unsuccessful, CStringView{ "blocked" } }, {}, blocked) == EStatus::blocked_by_compilation);
    CResult invalid;
    TEST_EXPECT(ctx, reflect({}, {}, invalid) == EStatus::invalid_input);
    return ctx.report("ShaderReflection");
}
