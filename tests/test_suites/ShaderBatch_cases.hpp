
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ShaderBatch_cases.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    22 Oct 26
//
//  Rendering-local shader services using the controlled batch fixture.

#pragma once

#ifndef TESTS_TEST_SUITES_SHADERBATCH_CASES_HPP_INCLUDED
#define TESTS_TEST_SUITES_SHADERBATCH_CASES_HPP_INCLUDED

#include "assets/internal/bundle_builder_io.hpp"
#include "rendering/shader/shader_requests.hpp"
#include "rendering/runtime/shader_compiler.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/resource_service_fixture.hpp"
#include "platform/filesystem/file.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace shader_batch_tests
{

using shader::EStage;
using shader::EStatus;

static const char* const k_shader = R"(
#ifdef USE_INCLUDE_ROOT
#include "a.hlsli"
#else
#include "inc/a.hlsli"
#endif
#include "morphic_shader.hlsli"
struct V { float4 p : SV_Position; };
V vs(uint id : SV_VertexID) { V o; o.p = float4(id, VALUE, 0, 1); return o; }
float4 ps(V i) : SV_Target { return i.p; }
RWStructuredBuffer<uint> output : MV_SHADER_RESOURCE(u, 0, 0);
[numthreads(1,1,1)] void cs(uint3 id : SV_DispatchThreadID) { output[id.x] = VALUE; }
[maxvertexcount(3)] void gs(triangle V input[3], inout TriangleStream<V> stream)
{ for (uint i = 0; i < 3; ++i) stream.Append(input[i]); }
struct Patch { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
Patch patch(InputPatch<V,3> input) { Patch p; p.edges[0]=1; p.edges[1]=1; p.edges[2]=1; p.inside=1; return p; }
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("patch")]
V hs(InputPatch<V,3> input, uint id : SV_OutputControlPointID) { return input[id]; }
[domain("tri")]
V ds(Patch p, float3 location : SV_DomainLocation, const OutputPatch<V,3> input)
{ V o; o.p = input[0].p * location.x + input[1].p * location.y + input[2].p * location.z; return o; }
)";

static const char* const k_unicode_shader =
    "#include \"\xc3\xa9\xf0\x9f\x98\x80.hlsli\"\n"
    "#include \"./\xc3\xa9\xf0\x9f\x98\x80.hlsli\"\n"
    "#warning retained Unicode source warning\n"
    "RWStructuredBuffer<uint> output:register(u0); [numthreads(1,1,1)] void main(){output[0]=UNICODE_VALUE;}";
static const char* const k_unicode_include = "#pragma once\n#define UNICODE_VALUE 7\n";

static bool make_bundle(CBakedDocumentBlock& block, bundle::CBundleView& view)
{
    CLiveDocument inventory;
    if (!tests::parse_document(
            R"({"main.hlsl":{},"inc":{"content":{"a.hlsli":{}}},"shared.hlsli":{},"bad.hlsl":{},"double.hlsl":{},"missing.hlsl":{},"escape.hlsl":{},"warning.hlsl":{},"morphic_shader.hlsli":{},"counter.hlsl":{},"push.hlsl":{},"wave.hlsl":{},"unicode.hlsl":{},"\u00e9\ud83d\ude00.hlsli":{}})",
            inventory))
    {
        return false;
    }
    const auto support = platform::filesystem::loadFile(
        test_environment::repository_path("rendering/shaders/morphic_shader.hlsli").c_str(), 1u);
    if (!support.is_ready())
    {
        return false;
    }
    const std::string support_text{ reinterpret_cast<const char*>(support.data()), support.size() };
    const char* const contents[]{
        k_shader, "#include \"../shared.hlsli\"\n", "#ifndef VALUE\n#define VALUE 3\n#endif\n", "this is not HLSL",
        "double value; float4 main() : SV_Target { return (float)value; }",
        "#include \"absent.hlsli\"\nfloat4 main():SV_Target{return 0;}",
        "#include \"../../outside.hlsli\"\nfloat4 main():SV_Target{return 0;}",
        "#warning development diagnostic\nfloat4 main():SV_Target{return 0;}", support_text.c_str(),
        "AppendStructuredBuffer<uint> output:register(u0); [numthreads(1,1,1)] void main(){output.Append(1);}",
        "struct P{uint x;}; [[vk::push_constant]] P p; RWStructuredBuffer<uint> output:register(u0); "
        "[numthreads(1,1,1)] void main(){output[0]=p.x;}",
        "RWStructuredBuffer<uint> output:register(u0); [numthreads(1,1,1)] void main(){output[0]=WaveGetLaneIndex();}",
        k_unicode_shader, k_unicode_include
    };
    const char* const names[]{ "main.hlsl", "a.hlsli", "shared.hlsli", "bad.hlsl", "double.hlsl", "missing.hlsl",
        "escape.hlsl", "warning.hlsl", "morphic_shader.hlsli", "counter.hlsl", "push.hlsl", "wave.hlsl",
        "unicode.hlsl", "\xc3\xa9\xf0\x9f\x98\x80.hlsli" };
    TPodVector<bundle::SBuildSource> sources;
    for (std::uint32_t i = 0u; i < 14u; ++i)
    {
        CNodeKey parent = inventory.root();
        if (i == 1u)
        {
            parent =
                inventory.object_child(inventory.object_child(parent, CStringView{ "inc" }), CStringView{ "content" });
        }
        if (!sources.push_back({ inventory.object_child(parent, CStringView{ names[i] }), contents[i] }))
        {
            return false;
        }
    }
    const bundle::detail::SBuildIo io{ nullptr,
        [](void*, const char* const source, std::size_t& size) noexcept {
            size = std::strlen(source);
            return true;
        },
        [](void*, const char* const source, const CByteView& output) noexcept {
            std::memcpy(output.data(), source, output.size());
            return true;
        } };
    return bundle::detail::build(inventory, inventory.root(), sources.const_view(), block, view, io).succeeded();
}

static bool populate(shader::CCompileRequest& request)
{
    if (!request.entries.initialise(10u))
    {
        return false;
    }
    const char* const entries[]{ "vs", "hs", "ds", "gs", "ps", "cs", "main", "main", "main", "main" };
    const char* const paths[]{ "main.hlsl", "main.hlsl", "main.hlsl", "main.hlsl", "main.hlsl", "main.hlsl", "bad.hlsl",
        "double.hlsl", "missing.hlsl", "escape.hlsl" };
    for (std::uint32_t i = 0u; i < 10u; ++i)
    {
        auto& entry = request.entries[i];
        if (!entry.source_path.set(paths[i]) || !entry.entry_point.set(entries[i]))
        {
            return false;
        }
        entry.stage = (i < 6u) ? static_cast<EStage>(i) : EStage::pixel;
    }
    return true;
}

static void check_reports(tests::TTestContext& ctx, const shader::CCompileGroup& group, const bool discarded)
{
    TEST_EXPECT(ctx, group.reports.count() == 10u);
    if (group.reports.count() != 10u)
    {
        return;
    }
    TEST_EXPECT(ctx, group.summary.succeeded == 6u);
    TEST_EXPECT(ctx, group.summary.failed == (discarded ? 3u : 4u));
    TEST_EXPECT(ctx, group.summary.not_executed == (discarded ? 1u : 0u));
    for (std::uint32_t i = 0u; i < 10u; ++i)
    {
        const auto& report = group.reports[i];
        if (discarded && (i == 9u))
        {
            TEST_EXPECT(ctx, report.member == i);
            TEST_EXPECT(ctx, report.status == EStatus::not_executed);
            TEST_EXPECT(ctx, group.summary.outcome == shader::EOutcome::interrupted);
            continue;
        }
        if (report.status !=
            ((i < 6u) ? EStatus::success : ((i == 7u) ? EStatus::unsupported_feature : EStatus::compilation_failed)))
        {
            std::cerr << "Shader member " << i << " status " << static_cast<unsigned int>(report.status) << ": "
                      << ((report.diagnostics.cstring() != nullptr) ? report.diagnostics.cstring() : "") << '\n';
        }
        TEST_EXPECT(ctx, report.member == i);
        TEST_EXPECT(ctx, (i < 6u) ? (report.status == EStatus::success) : (report.status != EStatus::success));
        TEST_EXPECT(ctx, (i < 6u) ? report.shader.view().is_ready() : !report.shader.view().is_ready());
        if (i == 7u)
        {
            TEST_EXPECT(ctx, report.status == EStatus::unsupported_feature);
        }
        if (i >= 6u)
        {
            TEST_EXPECT(ctx, report.diagnostics.length() != 0u);
        }
        TEST_EXPECT(ctx, report.source_path.length() != 0u);
    }
}

static void resource_groups(tests::TTestContext& ctx, texture_batch_tests::SFixture& rendering)
{
    filesystem_image::CImage image;
    texture_batch_tests::SResourceFixture fixture{ rendering };
    CLiveDocument document;
    TEST_EXPECT(ctx, tests::parse_document(R"({"roots":{}})", document) && image.adopt(std::move(document)));
    TEST_EXPECT(ctx, fixture.initialise());
    fixture.assets.set_filesystem(image);
    BundlePreparation prepared;
    TEST_EXPECT(ctx, make_bundle(prepared.block, prepared.view) && prepared.directory.prepare(prepared.view));
    std::uint64_t binding = 0u;
    TEST_EXPECT(ctx, image.reserve_bundle("shader:", binding));
    const auto source = fixture.assets.publish_bundle(prepared, binding, *fixture.client);
    TEST_EXPECT(ctx, source.status == EAssetStatus::success);
    for (int group = 0; group < 2; ++group)
    {
        ShaderCompileRequest request;
        request.bundle = source.asset;
        TEST_EXPECT(ctx, request.input.entries.initialise(2u));
        for (std::uint32_t i = 0; i < 2u; ++i)
        {
            auto& entry = request.input.entries[i];
            TEST_EXPECT(ctx, entry.source_path.set(i ? "bad.hlsl" : "main.hlsl"));
            TEST_EXPECT(ctx, entry.entry_point.set(i ? "main" : "cs"));
            entry.stage = EStage::compute;
        }
        TEST_EXPECT(ctx, fixture.submit(std::move(request), 600 + group));
    }
    TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept {
        fixture.pump(); return rendering.package->batch_client()->outstanding() == 4u;
    }));
    texture_batch_tests::SConcurrentJobs workers{ rendering };
    TEST_EXPECT(ctx, workers.acquire());
    fixture.assets.request_disposal({ source.asset }, 602, *fixture.client);
    fixture.assets.complete_disposals();
    TEST_EXPECT(ctx, fixture.messages.empty());
    TEST_EXPECT(ctx, workers.start(4u));
    workers.join();
    for (auto& worker : workers.workers) TEST_EXPECT(ctx, workers.complete(worker, threading::EBatchCompletion::executed));
    rendering::SResourceResult groups[2];
    bool seen[2]{};
    for (int i = 0; i < 2; ++i)
    {
        rendering::SResourceResult result;
        std::int32_t slot = -1;
        const bool received = tests::wait_until(rendering.conversion, [&]() noexcept { return fixture.receive(result, slot); });
        TEST_EXPECT(ctx, received && slot >= 600 && slot < 602);
        if (!received || slot < 600 || slot >= 602) return;
        TEST_EXPECT(ctx, !seen[slot - 600] && result.status == EAssetStatus::success);
        seen[slot - 600] = true;
        groups[slot - 600] = std::move(result);
    }
    BundleDisposedNotification notification;
    std::int32_t slot;
    TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept { return fixture.receive_host(notification, slot); }));
    TEST_EXPECT(ctx, notification.asset == source.asset);
    // Source bundle is gone; successful artifacts and failed-member reports survive.
    for (const auto& result : groups)
    {
        const auto& group = result.shaders;
        TEST_EXPECT(ctx, group.summary.succeeded == 1u && group.summary.failed == 1u);
        TEST_EXPECT(ctx, group.reports[0u].shader.view().is_ready() && group.reports[1u].diagnostics.length() != 0u);
    }
    TEST_EXPECT(ctx, fixture.valid && fixture.assets.is_idle() && !fixture.requests->pending());
}

static void run(tests::TTestContext& ctx, texture_batch_tests::SFixture& fixture)
{
    for (std::uint32_t pass = 0u; pass < 3u; ++pass)
    {
        const bool queued = (pass != 0u);
        const bool discarded = (pass == 2u);
        CBakedDocumentBlock block;
        bundle::CBundleView sources;
        shader::CCompileRequest request;
        TEST_EXPECT(ctx, make_bundle(block, sources));
        TEST_EXPECT(ctx, populate(request));
        if (!sources.is_ready() || (request.entries.count() != 10u))
        {
            return;
        }
        const bool started = fixture.start(queued);
        TEST_EXPECT(ctx, started);
        if (!started)
        {
            return;
        }
        texture_batch_tests::SStopRendering stop{ fixture };
        texture_batch_tests::SLocalWork message;
        message.set_async_slot(501);
        message.assign_payload(ShaderCompileWork{ &request, sources });
        TEST_EXPECT(ctx, fixture.package->post(message));
        if (queued)
        {
            threading::SBatchWork jobs[10];
            for (std::uint32_t i = 0u; i < 10u; ++i)
            {
                const bool acquired =
                    tests::wait_until(fixture.conversion, [&]() noexcept { return fixture.acquire(); });
                TEST_EXPECT(ctx, acquired);
                if (!acquired)
                {
                    return;
                }
                jobs[i] = fixture.held;
                fixture.held = {};
            }
            fixture.package->request_exit();
            // Complete in reverse order; all accepted jobs still run before drain.
            for (std::uint32_t i = 10u; i != 0u; --i)
            {
                auto& job = jobs[i - 1u];
                if (discarded && (i == 10u))
                {
                    TEST_EXPECT(ctx, job.requester->complete({ job.correlation,
                                         threading::EBatchCompletion::discarded_requester_exiting }));
                    continue;
                }
                TEST_EXPECT(ctx, fixture.module.prepare_batch_thread(thread_ids::batch_runner_00, job.memory_context));
                tests::TMemoryContextScope memory{ job.memory_context };
                job.function(job.object);
                TEST_EXPECT(ctx, job.requester->complete({ job.correlation, threading::EBatchCompletion::executed }));
            }
        }
        texture_batch_tests::SLocalReply response;
        const bool received =
            tests::wait_until(fixture.conversion, [&]() noexcept { return fixture.package->read(response); });
        TEST_EXPECT(ctx, received);
        if (!received)
        {
            return;
        }
        block = {};   // Reports must be independent of bundle storage.
        request = {};
        const auto* const result = response.owner().payload<ShaderCompileResult>();
        TEST_EXPECT(ctx, result != nullptr);
        if (result != nullptr)
        {
            TEST_EXPECT(ctx, result->status == EStatus::success);
            check_reports(ctx, result->group, discarded);
        }
        response = {};
        fixture.stop();
        TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
    }
}

static void options_and_admission(tests::TTestContext& ctx, texture_batch_tests::SFixture& fixture)
{
    CBakedDocumentBlock block;
    bundle::CBundleView sources;
    shader::CCompileRequest request;
    TEST_EXPECT(ctx, make_bundle(block, sources));
    TEST_EXPECT(ctx, request.entries.initialise(1u));
    auto& entry = request.entries[0u];
    TEST_EXPECT(ctx, entry.source_path.set("warning.hlsl") && entry.entry_point.set("main"));
    entry.stage = EStage::pixel;
    const bool started = fixture.start(false);
    TEST_EXPECT(ctx, started);
    if (!started)
    {
        return;
    }
    texture_batch_tests::SStopRendering stop{ fixture };
    for (std::uint32_t mode = 0u; mode < 3u; ++mode)
    {
        entry.options.debug = static_cast<shader::EDebug>(mode);
        entry.options.target = static_cast<shader::ETarget>(mode);
        entry.options.optimisation = shader::EOptimisation::level_3;
        entry.options.warnings_as_errors = (mode == 2u);
        auto response = fixture.operation<ShaderCompileWork, ShaderCompileResult>(
            ctx, { &request, sources }, 701, false, threading::EBatchCompletion::executed, false);
        const auto* const result = response.payload<ShaderCompileResult>();
        TEST_EXPECT(ctx, (result != nullptr) && (result->status == EStatus::success));
        if ((result == nullptr) || (result->group.reports.count() != 1u))
        {
            return;
        }
        const auto& report = result->group.reports[0u];
        TEST_EXPECT(ctx, report.status == ((mode == 2u) ? EStatus::compilation_failed : EStatus::success));
        TEST_EXPECT(ctx, report.diagnostics.length() != 0u);
    }
    TEST_EXPECT(ctx, entry.source_path.set("main.hlsl") && entry.entry_point.set("cs"));
    entry.stage = EStage::compute;
    entry.options = {};
    entry.options.target = shader::ETarget::vulkan_1_3;
    TEST_EXPECT(ctx, request.definitions.initialise(1u) && entry.definitions.initialise(1u));
    TEST_EXPECT(ctx, request.definitions[0u].name.set("VALUE") && request.definitions[0u].value.set("0"));
    TEST_EXPECT(ctx, entry.definitions[0u].name.set("VALUE") && entry.definitions[0u].value.set("3"));
    {
        auto response = fixture.operation<ShaderCompileWork, ShaderCompileResult>(
            ctx, { &request, sources }, 702, false, threading::EBatchCompletion::executed, false);
        const auto* const result = response.payload<ShaderCompileResult>();
        TEST_EXPECT(ctx, (result != nullptr) && (result->group.summary.succeeded == 1u));
        if ((result != nullptr) && (result->group.reports.count() == 1u))
        {
            const char* const provenance = result->group.reports[0u].provenance.cstring();
            TEST_EXPECT(ctx, (provenance != nullptr) && (std::strstr(provenance, "define=VALUE=3") != nullptr) &&
                                 (std::strstr(provenance, "define=VALUE=0") == nullptr));
        }
    }
    TEST_EXPECT(ctx, request.include_roots.initialise(2u));
    TEST_EXPECT(ctx, request.include_roots[0u].set("absent") && request.include_roots[1u].set("inc"));
    TEST_EXPECT(ctx, request.definitions[0u].name.set("USE_INCLUDE_ROOT") && request.definitions[0u].value.set("1"));
    {
        auto response = fixture.operation<ShaderCompileWork, ShaderCompileResult>(
            ctx, { &request, sources }, 705, false, threading::EBatchCompletion::executed, false);
        const auto* const result = response.payload<ShaderCompileResult>();
        TEST_EXPECT(ctx, (result != nullptr) && (result->group.summary.succeeded == 1u));
    }
    TEST_EXPECT(ctx, entry.definitions[0u].name.set("MV_SHADER_SPIRV"));
    {
        auto response = fixture.operation<ShaderCompileWork, ShaderCompileResult>(
            ctx, { &request, sources }, 703, false, threading::EBatchCompletion::executed, false);
        const auto* const result = response.payload<ShaderCompileResult>();
        TEST_EXPECT(ctx, (result != nullptr) && (result->status == EStatus::invalid_input));
    }
    shader::CCompileRequest oversized;
    TEST_EXPECT(ctx, oversized.entries.initialise(257u));
    auto response = fixture.operation<ShaderCompileWork, ShaderCompileResult>(
        ctx, { &oversized, sources }, 704, false, threading::EBatchCompletion::executed, false);
    const auto* const result = response.payload<ShaderCompileResult>();
    TEST_EXPECT(ctx, (result != nullptr) && (result->status == EStatus::limit_exceeded));
}

static void poc(tests::TTestContext& ctx, texture_batch_tests::SFixture& fixture)
{
    namespace fs = std::filesystem;
    const fs::path root = test_environment::repository_path("tests/shader/fixtures/poc/hlsl");
    std::vector<std::string> paths;
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file())
        {
            paths.push_back(entry.path().u8string());
        }
    }
    CLiveDocument inventory;
    TEST_EXPECT(ctx, inventory.initialise());
    TPodVector<bundle::SBuildSource> files;
    for (const auto& path : paths)
    {
        const auto relative = fs::relative(fs::u8path(path), root);
        auto parent = inventory.root();
        for (auto part = relative.begin(); part != relative.end(); ++part)
        {
            const auto name = part->u8string();
            auto node = inventory.object_child(parent, CStringView{ name.c_str() });
            if (!node.is_valid())
            {
                node = tests::add_document_child(
                    ctx, inventory, parent, inventory.create_object(CStringView{ name.c_str() }));
            }
            auto next = part;
            if (++next == relative.end())
            {
                TEST_EXPECT(ctx, files.push_back({ node, path.c_str() }));
                break;
            }
            parent = inventory.object_child(node, CStringView{ "content" });
            if (!parent.is_valid())
            {
                parent =
                    tests::add_document_child(ctx, inventory, node, inventory.create_object(CStringView{ "content" }));
            }
        }
    }
    CBakedDocumentBlock block;
    bundle::CBundleView sources;
    TEST_EXPECT(ctx, bundle::build(inventory, inventory.root(), files.const_view(), block, sources).succeeded());
    if (!sources.is_ready())
    {
        return;
    }
    shader::CCompileRequest request;
    TEST_EXPECT(ctx, request.entries.initialise(1u));
    auto& entry = request.entries[0u];
    TEST_EXPECT(ctx, entry.source_path.set("shaders/simple.hlsl"));
    const bool started = fixture.start(false);
    TEST_EXPECT(ctx, started);
    if (!started)
    {
        return;
    }
    texture_batch_tests::SStopRendering stop{ fixture };
    for (const bool pixel : { false, true })
    {
        entry.stage = pixel ? EStage::pixel : EStage::vertex;
        TEST_EXPECT(ctx, entry.entry_point.set(pixel ? "ps" : "vs"));
        auto response = fixture.operation<ShaderCompileWork, ShaderCompileResult>(
            ctx, { &request, sources }, 801, false, threading::EBatchCompletion::executed, false);
        const auto* const result = response.payload<ShaderCompileResult>();
        TEST_EXPECT(ctx, (result != nullptr) && (result->group.reports.count() == 1u));
        if ((result != nullptr) && (result->group.reports.count() == 1u))
        {
            const auto& report = result->group.reports[0u];
            if (report.status != EStatus::success)
            {
                std::cerr << "PoC simple shader: "
                          << ((report.diagnostics.cstring() != nullptr) ? report.diagnostics.cstring()
                                                                        : "no diagnostics")
                          << '\n';
            }
            TEST_EXPECT(ctx, report.status == EStatus::success);
        }
    }
}

static void compiler_scratch(tests::TTestContext& ctx)
{
    using namespace shader;
    using rendering::shader_compiler::SLimits;
    using rendering::shader_compiler::compile;
    using rendering::shader_compiler::validate_request;
    CBakedDocumentBlock block;
    bundle::CBundleView sources;
    CCompileRequest request;
    TEST_EXPECT(ctx, make_bundle(block, sources));
    TEST_EXPECT(ctx, request.entries.initialise(1u));
    auto& entry = request.entries[0u];
    entry.stage = EStage::compute;
    {
        CShaderCompileReport report;
        compile(request, 0u, sources, {}, report);
        TEST_EXPECT(ctx, report.status == EStatus::invalid_input);
        TEST_EXPECT(ctx, !report.shader.view().is_ready());
        TEST_EXPECT(ctx, entry.source_path.set("main.hlsl"));
        compile(request, 0u, sources, {}, report);
        TEST_EXPECT(ctx, report.status == EStatus::invalid_input);
        TEST_EXPECT(ctx, !report.shader.view().is_ready());
    }
    TEST_EXPECT(ctx, entry.source_path.set("./INC/../MAIN.hlsl") && entry.entry_point.set("cs"));
    TEST_EXPECT(ctx, request.include_roots.initialise(1u) && request.include_roots[0u].set("inc"));
    TEST_EXPECT(ctx, request.definitions.initialise(2u));
    TEST_EXPECT(ctx, request.definitions[0u].name.set("VALUE") && request.definitions[0u].value.set("2"));
    TEST_EXPECT(ctx, request.definitions[1u].name.set("USE_INCLUDE_ROOT"));
    TEST_EXPECT(ctx, entry.definitions.initialise(1u));
    TEST_EXPECT(ctx, entry.definitions[0u].name.set("VALUE") && entry.definitions[0u].value.set("4"));

    //  Validation must neither allocate nor accept the document-specific
    //  modified NUL encoding, surrogate encodings, overlong or truncated UTF-8.
    for (const char* const invalid : { "bad\xc0\x80", "bad\xc1\x81", "bad\xed\xa0\x80", "bad\xf4\x90\x80\x80", "bad\xe2\x82" })
    {
        TEST_EXPECT(ctx, request.definitions[1u].value.set(invalid));
        TEST_EXPECT(ctx, validate_request(request, {}) == EStatus::invalid_input);
    }
    TEST_EXPECT(ctx, request.definitions[1u].value.set("x\0y", 3u));
    TEST_EXPECT(ctx, validate_request(request, {}) == EStatus::invalid_input);
    TEST_EXPECT(ctx, request.definitions[1u].value.set("\xc2\xa3\xf0\x9f\x98\x80"));
    tests::TAllocationFailureFixture validation_failure;
    validation_failure.reject_all = true;
    memory::CMemoryAllocator validation_allocator{ &validation_failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext validation_context{ validation_allocator };
    auto* const previous = memory::set_thread_memory_context(&validation_context);
    const auto valid = validate_request(request, {});
    (void)memory::set_thread_memory_context(previous);
    TEST_EXPECT(ctx, valid == EStatus::success && validation_failure.attempts == 0u);
    TEST_EXPECT(ctx, validation_context.is_attribution_empty());
    TEST_EXPECT(ctx, request.definitions[1u].value.set("\"\xc2\xa3\xf0\x9f\x98\x80\""));
    for (const char* const invalid : { "../main.hlsl", "a/../../main.hlsl", "/main.hlsl", "C:\\main.hlsl", "a/..", "." })
    {
        TEST_EXPECT(ctx, entry.source_path.set(invalid));
        TEST_EXPECT(ctx, validate_request(request, {}) == EStatus::invalid_input);
    }
    TEST_EXPECT(ctx, entry.source_path.set("./INC\\../UNICODE.hlsl") && entry.entry_point.set("main"));

    for (const auto scenario : { 0u, 1u, 2u, 3u })
    {
        entry.options.debug = ((scenario & 1u) == 0u) ? EDebug::none : EDebug::source_debug;
        const bool include_limit = scenario >= 2u;
        SLimits limits;
        if (include_limit) limits.source_bytes = std::strlen(k_unicode_shader);
        tests::TAllocationFailureFixture baseline;
        memory::CMemoryAllocator allocator{ &baseline, tests::allocate_with_failure, tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        auto* const saved = memory::set_thread_memory_context(&context);
        {
            CShaderCompileReport report;
            compile(request, 0u, sources, limits, report);
            TEST_EXPECT(ctx, report.status == (include_limit ? EStatus::limit_exceeded : EStatus::success));
            TEST_EXPECT(ctx, report.shader.view().is_ready() == !include_limit);
            const std::string provenance{ (report.provenance.cstring() != nullptr) ? report.provenance.cstring() : "" };
            TEST_EXPECT(ctx, provenance.find("define=VALUE=4\n") != std::string::npos);
            TEST_EXPECT(ctx, provenance.find("define=VALUE=2\n") == std::string::npos);
            const char* const dependency = "source=\xc3\xa9\xf0\x9f\x98\x80.hlsli;";
            const auto first = provenance.find(dependency);
            TEST_EXPECT(ctx, (first != std::string::npos) == !include_limit);
            if (!include_limit) TEST_EXPECT(ctx, provenance.find(dependency, first + 1u) == std::string::npos);
        }
        (void)memory::set_thread_memory_context(saved);
        TEST_EXPECT(ctx, context.is_attribution_empty());
        const auto attempts = baseline.attempts;
        TEST_EXPECT(ctx, attempts != 0u);
        //  Sweep successful compilation and include-limit reporting, including
        //  callbacks, diagnostics/provenance and the final retained artifact.
        for (std::size_t attempt = 0u; attempt < attempts; ++attempt)
        {
            baseline = {};
            baseline.fail_on = attempt;
            auto* const restored = memory::set_thread_memory_context(&context);
            {
                CShaderCompileReport report;
                compile(request, 0u, sources, limits, report);
                TEST_EXPECT(ctx, baseline.failed);
                TEST_EXPECT(ctx, report.status == EStatus::allocation_failed);
                TEST_EXPECT(ctx, !report.shader.view().is_ready());
            }
            (void)memory::set_thread_memory_context(restored);
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
    }
    TEST_EXPECT(ctx, entry.source_path.set("unicode.hlsl") && entry.entry_point.set("main"));
    //  A supplementary code point exercises both directions of DXC's wide
    //  interface. Repeated includes and the preprocessing pass share one budget.
    SLimits unicode_limits;
    unicode_limits.source_bytes = std::strlen(k_unicode_shader) + std::strlen(k_unicode_include);
    CShaderCompileReport report;
    compile(request, 0u, sources, unicode_limits, report);
    TEST_EXPECT(ctx, report.status == EStatus::success && report.shader.view().is_ready());
    const std::string provenance{ (report.provenance.cstring() != nullptr) ? report.provenance.cstring() : "" };
    const char* const dependency = "source=\xc3\xa9\xf0\x9f\x98\x80.hlsli;";
    const auto first = provenance.find(dependency);
    TEST_EXPECT(ctx, first != std::string::npos);
    TEST_EXPECT(ctx, provenance.find(dependency, first + 1u) == std::string::npos);
    TEST_EXPECT(ctx, report.diagnostics.length() != 0u);
}

static void limits(tests::TTestContext& ctx)
{
    using namespace shader;
    using rendering::shader_compiler::SLimits;
    using rendering::shader_compiler::compile;
    using rendering::shader_compiler::initialise_compiler;
    using rendering::shader_compiler::validate_request;
    CBakedDocumentBlock block;
    bundle::CBundleView sources;
    CCompileRequest request;
    TEST_EXPECT(ctx, make_bundle(block, sources));
    TEST_EXPECT(ctx, request.entries.initialise(1u));
    auto& entry = request.entries[0u];
    TEST_EXPECT(ctx, entry.source_path.set("main.hlsl") && entry.entry_point.set("cs"));
    entry.stage = EStage::compute;
    TEST_EXPECT(ctx, initialise_compiler());
    for (std::uint32_t limit = 0u; limit < 3u; ++limit)
    {
        SLimits policy;
        if (limit == 0u)
        {
            policy.file_bytes = 16u;
        }
        if (limit == 1u)
        {
            policy.source_bytes = std::strlen(k_shader);
        }
        if (limit == 2u)
        {
            policy.artifact_bytes = 16u;
        }
        CShaderCompileReport report;
        compile(request, 0u, sources, policy, report);
        TEST_EXPECT(ctx, report.status == EStatus::limit_exceeded);
        TEST_EXPECT(ctx, !report.shader.view().is_ready());
    }
    TEST_EXPECT(ctx, entry.source_path.set("warning.hlsl") && entry.entry_point.set("main"));
    entry.stage = EStage::pixel;
    SLimits policy;
    policy.diagnostic_bytes = 20u;
    CShaderCompileReport report;
    compile(request, 0u, sources, policy, report);
    TEST_EXPECT(ctx, report.status == EStatus::success);
    TEST_EXPECT(ctx, report.diagnostics_truncated && (report.diagnostics.length() <= 20u));
    TEST_EXPECT(ctx, report.shader.view().is_ready());
    policy.request_bytes = 1u;
    TEST_EXPECT(ctx, validate_request(request, policy) == EStatus::limit_exceeded);
    entry.stage = EStage::compute;
    for (const char* const path : { "counter.hlsl", "push.hlsl", "wave.hlsl" })
    {
        TEST_EXPECT(ctx, entry.source_path.set(path));
        compile(request, 0u, sources, SLimits{}, report);
        TEST_EXPECT(ctx, report.status == EStatus::unsupported_feature);
        TEST_EXPECT(ctx, !report.shader.view().is_ready());
    }
}

}   //  namespace shader_batch_tests

#endif   //  TESTS_TEST_SUITES_SHADERBATCH_CASES_HPP_INCLUDED
