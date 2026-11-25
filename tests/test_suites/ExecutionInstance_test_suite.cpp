
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File: ExecutionInstance_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date: 25 Nov 26
//
//  Checks allocation failure, over-alignment and teardown under another context.

#include "framework/module/TExecutionInstance.hpp"
#include "containers/TInstance.hpp"
#include "tests/test_suites/ExecutionInstance_test_suite.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"

namespace
{

struct SObservation
{
    unsigned constructions{}, destructions{};
    std::uint32_t allocations_at_destruction{};
};

struct alignas(128) SExecution
{
    SExecution(const memory::CMemoryContext& context, SObservation& observation, TInstance<int>&& child) noexcept
        : context{ context }, observation{ observation }, child{ std::move(child) }
    {
        ++observation.constructions;
    }

    ~SExecution() noexcept
    {
        ++observation.destructions;
        observation.allocations_at_destruction = context.get_live_allocation_count();
    }

    SExecution(const SExecution&) = delete;
    SExecution& operator=(const SExecution&) = delete;
    //  The helper must obtain the actual address without invoking operator&.
    SExecution* operator&() = delete;

    const memory::CMemoryContext& context;
    SObservation& observation;
    TInstance<int> child;
};

using TExecution = modules::TExecutionInstance<SExecution>;

void test_explicit_context_and_teardown(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext source{ allocator }, other{ allocator };
    SObservation observation;
    TInstance<int> child{ source };
    TEST_EXPECT(ctx, child.emplace(37));
    const std::size_t attempts = fixture.attempts;
    memory::CMemoryContext* const previous = memory::set_thread_memory_context(&other);
    void* const handle = TExecution::create_in(source, source, observation, std::move(child));
    TEST_EXPECT(ctx, (handle != nullptr));
    TEST_EXPECT(ctx, (fixture.attempts == (attempts + 1u)));
    TEST_EXPECT(ctx, (source.get_live_allocation_count() == 2u));
    TEST_EXPECT(ctx, other.is_attribution_empty());
    SExecution* const execution = TExecution::get(handle);
    if (execution)
    {
        TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(execution) & (alignof(SExecution) - 1u)) == 0u));
        TEST_EXPECT(ctx, (*execution->child == 37));
        TEST_EXPECT(ctx, child.is_empty());
        TEST_EXPECT(ctx, (observation.constructions == 1u));
    }
    TExecution::destroy(handle);
    (void)memory::set_thread_memory_context(previous);
    TEST_EXPECT(ctx, (observation.destructions == 1u));
    TEST_EXPECT(ctx, (observation.allocations_at_destruction == 2u));
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, other.is_attribution_empty());
}

void test_failure_and_ambient_context(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    SObservation observation;
    TInstance<int> child{ context };
    TEST_EXPECT(ctx, child.emplace(19));
    memory::CMemoryContext* const previous = memory::set_thread_memory_context(&context);
    fixture.reject_all = true;
    void* const failed = TExecution::create(context, observation, std::move(child));
    TEST_EXPECT(ctx, (failed == nullptr));
    TEST_EXPECT(ctx, (observation.constructions == 0u));
    TEST_EXPECT(ctx, child.is_ready());
    TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
    TExecution::destroy(failed);
    fixture.reject_all = false;
    void* const handle = TExecution::create(context, observation, std::move(child));
    TEST_EXPECT(ctx, (handle != nullptr));
    TEST_EXPECT(ctx, (context.get_live_allocation_count() == 2u));
    TExecution::destroy(handle);
    (void)memory::set_thread_memory_context(previous);
    TEST_EXPECT(ctx, (observation.constructions == 1u));
    TEST_EXPECT(ctx, (observation.destructions == 1u));
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_absent_context_and_null_handle(tests::TTestContext& ctx)
{
    memory::CMemoryContext* const previous_thread = memory::set_thread_memory_context(nullptr);
    memory::CMemoryContext* const previous_module = memory::set_module_memory_context(nullptr);
    void* const handle = modules::TExecutionInstance<int>::create(42);
    (void)memory::set_module_memory_context(previous_module);
    (void)memory::set_thread_memory_context(previous_thread);
    TEST_EXPECT(ctx, (handle == nullptr));
    TEST_EXPECT(ctx, (TExecution::get(nullptr) == nullptr));
    TExecution::destroy(nullptr);
}

}   //  namespace

int run_execution_instance_tests()
{
    tests::TTestContext ctx;
    test_explicit_context_and_teardown(ctx);
    test_failure_and_ambient_context(ctx);
    test_absent_context_and_null_handle(ctx);
    return ctx.report("ExecutionInstance");
}
