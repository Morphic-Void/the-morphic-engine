
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ShaderConditioning_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Production composition callbacks through the existing batch client. Gates
//  belong to controlled workers, never the callback's inline submission path.

#include "tests/test_suites/ShaderConditioning_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/thread_helpers.hpp"
#include "assets/internal/bundle_builder_io.hpp"
#include "containers/TInstance.hpp"
#include "rendering/runtime/shader_conditioning.hpp"
#include "threading/CBatchWork.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include <atomic>
#include <string>
#include <type_traits>

namespace shader_conditioning_tests
{

using rendering::shader_conditioning::CCompileReflectJob;
using rendering::shader_conditioning::CCompileReflectReport;
using rendering::shader_conditioning::EPreparation;
using RStatus = rendering::shader_reflection::EStatus;
using CStatus = shader::EStatus;

static_assert(!std::is_copy_constructible_v<CCompileReflectJob> && !std::is_move_constructible_v<CCompileReflectJob>);
static_assert(!std::is_copy_constructible_v<CCompileReflectReport> && std::is_nothrow_move_constructible_v<CCompileReflectReport>);

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
    memory::CMemoryContext* previous;
};

struct SInputs
{
    CBakedDocumentBlock backing;
    bundle::CBundleView sources;
    shader::CCompileRequest request;

    bool initialise()
    {
        static const char* const source = R"(
#warning retained compiler diagnostic
struct V { float4 position : SV_Position; };
V vs(uint id : SV_VertexID) { V v; v.position=float4(id,0,0,1); return v; }
float4 ps(V v) : SV_Target { return v.position; }
RWStructuredBuffer<uint> values : register(u0);
[numthreads(2,1,1)] void cs(uint3 id : SV_DispatchThreadID) { values[id.x]=id.x; }
[maxvertexcount(3)] void gs(triangle V input[3], inout TriangleStream<V> stream) { for(uint i=0;i<3;++i) stream.Append(input[i]); }
struct P { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
P patch(InputPatch<V,3> input) { P p; p.edges[0]=1; p.edges[1]=1; p.edges[2]=1; p.inside=1; return p; }
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("patch")]
V hs(InputPatch<V,3> input, uint id : SV_OutputControlPointID) { return input[id]; }
[domain("tri")] V ds(P p, float3 uv : SV_DomainLocation, const OutputPatch<V,3> input) {
 V v; v.position=input[0].position*uv.x+input[1].position*uv.y+input[2].position*uv.z; return v; }
)";
        CLiveDocument inventory;
        if (!tests::parse_document(R"({"main.hlsl":{},"bad.hlsl":{},"limited.hlsl":{}})", inventory))
        {
            return false;
        }
        const char* const names[]{ "main.hlsl", "bad.hlsl", "limited.hlsl" };
        const char* const contents[]{ source, "this is not valid HLSL",
            "#warning retained limit diagnostic\n"
            "RWStructuredBuffer<uint> output:register(u0); [numthreads(1,1,1)] void main(){output[0]=1;}" };
        TPodVector<bundle::SBuildSource> files;
        for (std::uint32_t i = 0u; i < 3u; ++i)
        {
            if (!files.push_back({ inventory.object_child(inventory.root(), CStringView{ names[i] }), contents[i] }))
            {
                return false;
            }
        }
        const bundle::detail::SBuildIo io{ nullptr,
            [](void*, const char* text, std::size_t& size) noexcept
            {
                size = std::strlen(text);
                return true;
            },
            [](void*, const char* text, const CByteView& target) noexcept
            {
                std::memcpy(target.data(), text, target.size());
                return true;
            }
        };
        if (!bundle::detail::build(inventory, inventory.root(), files.const_view(), backing, sources, io).succeeded() ||
            !request.entries.initialise(8u))
        {
            return false;
        }
        const char* const entries[]{ "vs", "hs", "ds", "gs", "ps", "cs", "main", "main" };
        for (std::uint32_t i = 0u; i < 8u; ++i)
        {
            auto& entry = request.entries[i];
            if (!entry.source_path.set(names[(i < 6u) ? 0u : (i - 5u)]) || !entry.entry_point.set(entries[i]))
            {
                return false;
            }
            entry.stage = (i < 6u) ? static_cast<shader::EStage>(i) : ((i == 6u) ? shader::EStage::pixel : shader::EStage::compute);
        }
        return true;
    }
};

static void untouched(tests::TTestContext& ctx, const CCompileReflectJob& job)
{
    TEST_EXPECT(ctx, job.report.compilation.status == CStatus::not_executed);
    TEST_EXPECT(ctx, job.report.reflection.status() == RStatus::not_executed);
    TEST_EXPECT(ctx, job.report.reflection.schema() == nullptr);
}

static void check_report(tests::TTestContext& ctx, const CCompileReflectReport& report, const std::uint32_t member, const char* identity)
{
    TEST_EXPECT(ctx, report.compilation.member == member);
    TEST_EXPECT(ctx, report.reflection.component_identity() == CStringView{ identity });
    if (member == 6u)
    {
        TEST_EXPECT(ctx, report.compilation.status == CStatus::compilation_failed);
        TEST_EXPECT(ctx, report.reflection.status() == RStatus::blocked_by_compilation);
        TEST_EXPECT(ctx, report.reflection.diagnostic().reason == rendering::shader_reflection::EReason::compilation_failed);
        TEST_EXPECT(ctx, !report.compilation.shader.view().is_ready());
    }
    else
    {
        if (report.compilation.status != CStatus::success)
        {
            std::printf("ShaderConditioning member %u: %s\n", member, report.compilation.diagnostics.string());
        }
        TEST_EXPECT(ctx, report.compilation.status == CStatus::success);
        TEST_EXPECT(ctx, report.compilation.shader.view().is_ready());
        TEST_EXPECT(ctx, report.reflection.status() == ((member == 7u) ? RStatus::limit_exceeded : RStatus::success));
    }
    TEST_EXPECT(ctx, report.compilation.diagnostics.length() != 0u);
    if (member < 6u)
    {
        TEST_EXPECT(ctx, report.reflection.schema() != nullptr);
        if (report.reflection.schema() != nullptr)
        {
            schema::SSchemaDiagnostic diagnostic;
            TEST_EXPECT(ctx, report.reflection.schema()->validate(diagnostic));
        }
    }
    else
    {
        TEST_EXPECT(ctx, report.reflection.schema() == nullptr);
    }
}

//  These callbacks are the production rendering sources compiled into the test
//  executable. That code remains resident through every report's destruction;
//  this uses the existing batch client with controlled workers, not a test of
//  unloading a live DLL owner.
struct SBatchFixture
{
    threading::SBatchShared shared;
    threading::CThreadControlState control[2];
    threading::CWaitPredicate wake[2];
    TInstance<threading::CBatchClient> routes[2];
    platform::system::CPerfCountConversion conversion;

    bool initialise(const std::uint32_t runners)
    {
        shared.runner_count = runners;
        if (!conversion.init() || !shared.wake.acquire_control())
        {
            return false;
        }
        for (std::uint32_t i = 0u; i < 2u; ++i)
        {
            if (!wake[i].acquire_control() || !routes[i].emplace(shared, control[i], wake[i], module_ids::executable, this))
            {
                return false;
            }
        }
        return true;
    }
    bool acquire(threading::SBatchWork& work)
    {
        threading::transports::TAcquiredArenaSlot<threading::SBatchWork, threading::k_batch_channel_capacity> slot{ shared.work };
        if (!slot)
        {
            return false;
        }
        work = *slot;
        return slot.recycle();
    }
    ~SBatchFixture() noexcept
    {
        threading::SBatchWork work;
        while (acquire(work))
        {
            (void)work.requester->complete({ work.correlation, threading::EBatchCompletion::discarded_requester_exiting });
        }
        for (std::uint32_t i = 0u; i < 2u; ++i)
        {
            threading::SBatchResponse response;
            if (routes[i])
            {
                while (routes[i]->receive(response))
                {
                }
            }
            routes[i].reset();
            wake[i].release_control();
        }
        shared.wake.release_control();
    }
};

struct SWorker
{
    threading::SBatchWork work;
    platform::threading::CThread thread;
    std::atomic<bool> entered{ false }, release{ false }, finished{ false };
    bool install_context{ true };
    threading::EBatchCompletion completion{ threading::EBatchCompletion::discarded_requester_exiting };

    static std::uint32_t MV_STD_ABI_CALL execute(void* argument) noexcept
    {
        auto& worker = *static_cast<SWorker*>(argument);
        worker.entered.store(true, std::memory_order_release);
        while (!worker.release.load(std::memory_order_acquire))
        {
            platform::threading::processor_relax();
        }
        if (worker.work.requester->exit_requested())
        {
            worker.completion = threading::EBatchCompletion::discarded_requester_exiting;
        }
        else if (!worker.install_context)
        {
            worker.completion = threading::EBatchCompletion::discarded_context_install_failed;
        }
        else
        {
            SContextScope scope{ *worker.work.memory_context };
            worker.work.function(worker.work.object);
            worker.completion = threading::EBatchCompletion::executed;
        }
        worker.finished.store(true, std::memory_order_release);
        return 0u;
    }
    bool start(SBatchFixture& fixture)
    {
        return fixture.acquire(work) && thread.create(&execute, this);
    }
    bool publish() noexcept
    {
        release.store(true, std::memory_order_release);
        if (thread.is_valid() && !thread.join_and_close())
        {
            return false;
        }
        if (work.requester == nullptr)
        {
            return false;
        }
        const bool result = work.requester->complete({ work.correlation, completion });
        work = {};
        return result;
    }
    ~SWorker() noexcept
    {
        if (work.requester != nullptr)
        {
            (void)publish();
        }
    }
};

static bool prepare(CCompileReflectJob& job, const SInputs& input, const std::uint32_t member, const std::string& identity)
{
    rendering::shader_reflection::SLimits limits;
    if (member == 7u)
    {
        limits.resources = 0u;
    }
    return job.prepare(input.request, member, input.sources, CStringView{ identity.c_str() }, {}, limits) == EPreparation::ready;
}

static void setup_and_failures(tests::TTestContext& ctx, const SInputs& input)
{
    for (std::uint32_t test = 0u; test < 6u; ++test)
    {
        tests::TAllocationFailureFixture failure;
        memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            SContextScope scope{ context };
            CCompileReflectJob job;
            TEST_EXPECT(ctx, !job.run());
            rendering::shader_reflection::SLimits limits;
            if (test == 1u)
            {
                limits.string_bytes = 2u;
            }
            if (test == 2u)
            {
                failure.reject_all = true;
            }
            if (test == 5u)
            {
                //  Identity copy succeeds; private reflection-owner setup fails.
                failure.fail_on = 1u;
            }
            const auto expected = (test == 1u) ? EPreparation::limit_exceeded :
                (((test == 2u) || (test == 5u)) ? EPreparation::allocation_failed : EPreparation::invalid_input);
            const auto identity = (test == 0u) ? CStringView{} : ((test == 4u) ? CStringView{ "bad\xff" } : CStringView{ "setup" });
            TEST_EXPECT(ctx, job.prepare(input.request, (test == 3u) ? 99u : 5u, input.sources, identity, {}, limits) == expected);
            TEST_EXPECT(ctx, job.preparation() == expected);
            TEST_EXPECT(ctx, !job.ready() && !job.run());
            untouched(ctx, job);
            //  The submitting owner never calls submit for an unprepared job.
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    {
        CCompileReflectJob job;
        std::string identity{ "copied identity \xc2\xa3" };
        TEST_EXPECT(ctx, prepare(job, input, 5u, identity));
        identity.assign("caller text changed");
        TEST_EXPECT(ctx, job.prepare(input.request, 0u, input.sources, CStringView{ "replacement" }) == EPreparation::invalid_state);
        TEST_EXPECT(ctx, job.run());
        check_report(ctx, job.report, 5u, "copied identity \xc2\xa3");
        CCompileReflectReport retained = std::move(job.report);
        TEST_EXPECT(ctx, !job.ready() && !job.run());
        untouched(ctx, job);
        check_report(ctx, retained, 5u, "copied identity \xc2\xa3");
    }
    {
        CCompileReflectJob job;
        rendering::shader_reflection::SLimits limits;
        limits.resources = 0u;
        TEST_EXPECT(ctx, job.prepare(input.request, 5u, input.sources, CStringView{ "reflection limit" }, {}, limits) == EPreparation::ready);
        TEST_EXPECT(ctx, job.run());
        TEST_EXPECT(ctx, job.report.compilation.status == CStatus::success);
        TEST_EXPECT(ctx, job.report.compilation.shader.view().is_ready());
        TEST_EXPECT(ctx, job.report.compilation.diagnostics.length() != 0u);
        TEST_EXPECT(ctx, job.report.reflection.status() == RStatus::limit_exceeded);
        TEST_EXPECT(ctx, job.report.reflection.schema() == nullptr);
    }
    tests::TAllocationFailureFixture failure;
    memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        SContextScope scope{ context };
        CCompileReflectJob job;
        TEST_EXPECT(ctx, prepare(job, input, 5u, "compile allocation"));
        failure.reject_all = true;
        TEST_EXPECT(ctx, job.run());
        TEST_EXPECT(ctx, job.report.compilation.status == CStatus::allocation_failed);
        TEST_EXPECT(ctx, job.report.reflection.status() == RStatus::blocked_by_compilation);
        TEST_EXPECT(ctx, job.report.reflection.schema() == nullptr);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void inline_jobs(tests::TTestContext& ctx, const SInputs& input)
{
    CCompileReflectJob jobs[8];
    SBatchFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise(0u));
    if (!fixture.routes[0u])
    {
        return;
    }
    for (std::uint32_t i = 0u; i < 8u; ++i)
    {
        const auto identity = std::string("inline-") + std::to_string(i);
        const bool ready = prepare(jobs[i], input, i, identity);
        TEST_EXPECT(ctx, ready);
        if (!ready)
        {
            continue;
        }
        const auto submitted = fixture.routes[0u]->submit(&CCompileReflectJob::execute, &jobs[i], i);
        TEST_EXPECT(ctx, submitted.status == threading::EBatchSubmission::completed_inline);
        TEST_EXPECT(ctx, fixture.routes[0u]->outstanding() == 0u);
        check_report(ctx, jobs[i].report, i, identity.c_str());
    }
}

static void overlapping_groups(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, tests::allocate_test_memory, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    CCompileReflectReport retained[8];
    {
        SContextScope scope{ context };
        SInputs input;
        TEST_EXPECT(ctx, input.initialise());
        if (!input.sources.is_ready())
        {
            return;
        }
        const auto source_bytes = input.backing.buffer_bytes();
        const std::string snapshot{ reinterpret_cast<const char*>(source_bytes.data()), source_bytes.size() };
        struct SGroup
        {
            TInstance<CCompileReflectJob> jobs[4];
            std::uint32_t remaining{ 4u };
        } groups[2];
        SBatchFixture fixture;
        TEST_EXPECT(ctx, fixture.initialise(8u));
        if (!fixture.routes[0u])
        {
            return;
        }
        for (std::uint32_t member = 0u; member < 4u; ++member)
        {
            for (std::uint32_t group = 0u; group < 2u; ++group)
            {
                const auto index = group * 4u + member;
                auto& job = groups[group].jobs[member];
                TEST_EXPECT(ctx, job.emplace());
                const auto identity = std::string("overlap-") + std::to_string(index);
                const bool ready = job && prepare(*job, input, index, identity);
                TEST_EXPECT(ctx, ready);
                if (!ready)
                {
                    return;
                }
                TEST_EXPECT(ctx, fixture.routes[0u]->submit(&CCompileReflectJob::execute, job.operator->(), index).status == threading::EBatchSubmission::queued);
            }
        }
        SWorker workers[8];
        for (auto& worker : workers)
        {
            TEST_EXPECT(ctx, worker.start(fixture));
        }
        TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept
        {
            for (auto& worker : workers)
            {
                if (!worker.entered.load(std::memory_order_acquire))
                {
                    return false;
                }
            }
            return true;
        }));
        for (std::uint32_t i = 0u; i < 7u; ++i)
        {
            workers[i].release.store(true, std::memory_order_release);
        }
        //  Both groups are in flight. The last member of group 1 remains gated
        //  while all of group 0 is transferred and its job owners are destroyed.
        for (const auto slot : { 6u, 2u, 4u, 0u, 5u, 1u, 3u, 7u })
        {
            TEST_EXPECT(ctx, workers[slot].publish());
            threading::SBatchResponse response;
            TEST_EXPECT(ctx, fixture.routes[0u]->receive(response));
            const auto index = response.correlation;
            TEST_EXPECT(ctx, index < 8u);
            if (index >= 8u)
            {
                return;
            }
            auto& group = groups[index / 4u];
            auto& job = group.jobs[index % 4u];
            TEST_EXPECT(ctx, response.completion == threading::EBatchCompletion::executed);
            retained[index] = std::move(job->report);
            job.reset();
            --group.remaining;
            if ((groups[0u].remaining == 0u) && (groups[1u].remaining == 4u))
            {
                TEST_EXPECT(ctx, fixture.routes[0u]->outstanding() == 4u);
                TEST_EXPECT(ctx, !workers[7u].finished.load(std::memory_order_acquire));
                TEST_EXPECT(ctx, input.sources.is_ready());
                TEST_EXPECT(ctx, input.backing.buffer_bytes().data() == source_bytes.data());
            }
        }
        TEST_EXPECT(ctx, (groups[0u].remaining == 0u) && (groups[1u].remaining == 0u));
        TEST_EXPECT(ctx, fixture.routes[0u]->outstanding() == 0u);
        TEST_EXPECT(ctx, std::memcmp(snapshot.data(), source_bytes.data(), snapshot.size()) == 0);
    }
    //  All borrowed input, jobs and batch routes are gone. The captured allocator
    //  stays alive because transferred compiler artifacts and schemas still own it.
    TEST_EXPECT(ctx, !context.is_attribution_empty());
    for (std::uint32_t i = 0u; i < 8u; ++i)
    {
        const auto identity = std::string("overlap-") + std::to_string(i);
        check_report(ctx, retained[i], i, identity.c_str());
        retained[i] = {};
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void discard_routes(tests::TTestContext& ctx, const SInputs& input)
{
    for (std::uint32_t test = 0u; test < 3u; ++test)
    {
        CCompileReflectJob jobs[5];
        SBatchFixture fixture;
        TEST_EXPECT(ctx, fixture.initialise(4u));
        if (!fixture.routes[0u] || !fixture.routes[1u])
        {
            return;
        }
        SWorker workers[4];
        for (std::uint32_t i = 0u; i < 4u; ++i)
        {
            const auto identity = std::string("discard-") + std::to_string(i);
            const bool ready = prepare(jobs[i], input, 5u, identity);
            TEST_EXPECT(ctx, ready);
            if (!ready)
            {
                return;
            }
            const auto route = (test == 1u) ? (i / 2u) : 0u;
            TEST_EXPECT(ctx, fixture.routes[route]->submit(&CCompileReflectJob::execute, &jobs[i], i).status == threading::EBatchSubmission::queued);
            if ((test == 2u) && (i == 3u))
            {
                workers[i].install_context = false;
            }
            TEST_EXPECT(ctx, workers[i].start(fixture));
        }
        if (test == 0u)
        {
            //  Already executed, response not yet published: exit does not
            //  retroactively discard the successful result. Both other groups
            //  on this route are covered by the same requester exit.
            workers[0u].release.store(true, std::memory_order_release);
            TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept
            {
                return workers[0u].finished.load(std::memory_order_acquire);
            }));
        }
        if (test < 2u)
        {
            fixture.control[0u].request_exit();
        }
        TEST_EXPECT(ctx, prepare(jobs[4u], input, 5u, "rejected"));
        if (test < 2u)
        {
            const auto rejected = fixture.routes[0u]->submit(&CCompileReflectJob::execute, &jobs[4u], 4u);
            TEST_EXPECT(ctx, (rejected.status == threading::EBatchSubmission::rejected) && (rejected.reason == threading::EBatchRejection::requester_exiting));
            untouched(ctx, jobs[4u]);
        }
        for (std::uint32_t i = 0u; i < 4u; ++i)
        {
            TEST_EXPECT(ctx, workers[i].publish());
            const auto route = (test == 1u) ? (i / 2u) : 0u;
            threading::SBatchResponse response;
            TEST_EXPECT(ctx, fixture.routes[route]->receive(response));
            TEST_EXPECT(ctx, response.correlation == i);
            const bool executed = ((test == 0u) && (i == 0u)) || ((test == 1u) && (i >= 2u)) || ((test == 2u) && (i != 3u));
            const auto expected = executed ? threading::EBatchCompletion::executed : ((test == 2u) ?
                threading::EBatchCompletion::discarded_context_install_failed : threading::EBatchCompletion::discarded_requester_exiting);
            TEST_EXPECT(ctx, response.completion == expected);
            if (executed)
            {
                const auto identity = std::string("discard-") + std::to_string(i);
                check_report(ctx, jobs[i].report, 5u, identity.c_str());
            }
            else
            {
                untouched(ctx, jobs[i]);
            }
            if ((test == 1u) && (i == 1u))
            {
                TEST_EXPECT(ctx, fixture.routes[0u]->outstanding() == 0u);
                TEST_EXPECT(ctx, fixture.routes[1u]->outstanding() == 2u);
                TEST_EXPECT(ctx, input.sources.is_ready());
            }
        }
        TEST_EXPECT(ctx, (fixture.routes[0u]->outstanding() == 0u) && (fixture.routes[1u]->outstanding() == 0u));
    }
}

static void saturation(tests::TTestContext& ctx, const SInputs& input)
{
    shader::TMembers<CCompileReflectJob> jobs;
    constexpr auto count = threading::k_batch_channel_capacity;
    TEST_EXPECT(ctx, jobs.initialise(count + 1u));
    if (jobs.count() != count + 1u)
    {
        return;
    }
    SBatchFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise(1u));
    if (!fixture.routes[0u])
    {
        return;
    }
    for (std::uint32_t i = 0u; i <= count; ++i)
    {
        const bool ready = prepare(jobs[i], input, 5u, "saturation");
        TEST_EXPECT(ctx, ready);
        if (!ready)
        {
            return;
        }
        const auto submitted = fixture.routes[0u]->submit(&CCompileReflectJob::execute, &jobs[i], i);
        TEST_EXPECT(ctx, submitted.status == ((i == count) ? threading::EBatchSubmission::completed_inline : threading::EBatchSubmission::queued));
    }
    //  submit returned with a full result despite every worker item being held
    //  in the queue. No callback gate depends on this requester's next action.
    check_report(ctx, jobs[count].report, 5u, "saturation");
    TEST_EXPECT(ctx, fixture.routes[0u]->outstanding() == count);
    fixture.control[0u].request_exit();
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        threading::SBatchWork work;
        TEST_EXPECT(ctx, fixture.acquire(work));
        if (work.requester == nullptr)
        {
            return;
        }
        TEST_EXPECT(ctx, work.requester->complete({ work.correlation, threading::EBatchCompletion::discarded_requester_exiting }));
        threading::SBatchResponse response;
        TEST_EXPECT(ctx, fixture.routes[0u]->receive(response));
        TEST_EXPECT(ctx, response.correlation == i);
        untouched(ctx, jobs[i]);
    }
    TEST_EXPECT(ctx, fixture.routes[0u]->outstanding() == 0u);
}

}   //  namespace shader_conditioning_tests

int run_shader_conditioning_tests()
{
    using namespace shader_conditioning_tests;
    tests::TTestContext ctx;
    SInputs input;
    const bool ready = input.initialise();
    TEST_EXPECT(ctx, ready);
    if (ready)
    {
        setup_and_failures(ctx, input);
        inline_jobs(ctx, input);
        overlapping_groups(ctx);
        discard_routes(ctx, input);
        saturation(ctx, input);
    }
    return ctx.report("ShaderConditioning");
}
