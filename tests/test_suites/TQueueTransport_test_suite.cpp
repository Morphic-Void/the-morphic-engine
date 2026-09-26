
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  TQueueTransport_test_suite.cpp
//
//  Standalone test suite for threading::transports::TQueue<T>.

#include <array>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>

#include "threading/transports/TQueueTransport.hpp"
#include "tests/test_suites/TQueueTransport_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "memory/memory_context.hpp"
#include "debug/macros.hpp"

using threading::transports::TQueue;

namespace tests
{

inline std::string make_repeated_string(const char c, const std::uint32_t count)
{
    return std::string(static_cast<std::size_t>(count), c);
}

struct TAllocationGate
{
    bool allow_allocations = true;
};

struct TThreadContextScope
{
    explicit TThreadContextScope(memory::CMemoryContext* const context) noexcept
        : previous(memory::set_thread_memory_context(context))
    {
    }

    TThreadContextScope(const TThreadContextScope&) = delete;
    TThreadContextScope& operator=(const TThreadContextScope&) = delete;

    ~TThreadContextScope() noexcept
    {
        (void)memory::set_thread_memory_context(previous);
    }

    memory::CMemoryContext* previous = nullptr;
};

static void* MV_STD_ABI_CALL allocate_test_memory(void* const context, const std::size_t alignment, const std::size_t bytes) noexcept
{
    const TAllocationGate* const gate = static_cast<const TAllocationGate*>(context);
    if ((gate != nullptr) && !gate->allow_allocations)
    {
        return nullptr;
    }
    const std::size_t effective_alignment = (alignment < alignof(void*)) ? alignof(void*) : alignment;
    return ::operator new(bytes, std::align_val_t{ effective_alignment }, std::nothrow);
}

static bool MV_STD_ABI_CALL deallocate_test_memory(void*, const std::size_t alignment, void* const ptr) noexcept
{
    const std::size_t effective_alignment = (alignment < alignof(void*)) ? alignof(void*) : alignment;
    ::operator delete(ptr, std::align_val_t{ effective_alignment });
    return true;
}

template<typename TTransport>
bool post_string(TTransport& transport, const std::string_view text) noexcept
{
    return transport.post(text.data(), static_cast<std::uint32_t>(text.size()));
}

template<typename TTransport>
bool read_exact_string(TTransport& transport, std::string& out, const std::uint32_t count)
{
    out.resize(static_cast<std::size_t>(count));
    if (!transport.read(out.data(), count))
    {
        out.clear();
        return false;
    }
    return true;
}

template<typename TRing>
std::string drain_ring(TRing& ring)
{
    const std::uint32_t count = ring.readable_count();
    std::string result;
    result.resize(static_cast<std::size_t>(count));
    if ((count != 0u) && !ring.read(result.data(), count))
    {
        return {};
    }
    return result;
}

template<typename TQueue>
std::string drain_queue(TQueue& queue)
{
    std::string result;

    for (;;)
    {
        const std::uint32_t available = queue.refresh_readable_count();
        if (available == 0u)
        {
            break;
        }

        const std::size_t old_size = result.size();
        result.resize(old_size + static_cast<std::size_t>(available));

        if (!queue.read(result.data() + old_size, available))
        {
            return {};
        }
    }

    return result;
}

inline void print_summary(const char* const suite_name, const TTestContext& ctx)
{
    std::cout
        << suite_name
        << ": passed=" << ctx.passed
        << " failed=" << ctx.failed
        << '\n';
}

struct TStep
{
    std::uint32_t count = 0u;
    char fill = '\0';
    bool expect_resize = false;
};

std::string make_payload(const TStep& step)
{
    return make_repeated_string(step.fill, step.count);
}

void test_queue_uninitialised_state(TTestContext& ctx)
{
    TQueue<char> queue;

    TEST_EXPECT_TRUE(ctx, queue.posting_is_valid());
    TEST_EXPECT_TRUE(ctx, queue.reading_is_valid());
    TEST_EXPECT_FALSE(ctx, queue.posting_is_ready());
    TEST_EXPECT_FALSE(ctx, queue.reading_is_ready());
    TEST_EXPECT_FALSE(ctx, queue.posting_poisoned());
    TEST_EXPECT_TRUE(ctx, queue.validate());

    TEST_EXPECT_EQ(ctx, queue.current_readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, queue.refresh_readable_count(), 0u);

    TEST_EXPECT_FALSE(ctx, queue.post(nullptr, 0u)); // not ready
    TEST_EXPECT_FALSE(ctx, queue.read(nullptr, 0u)); // not ready
}

void test_queue_memory_context_configuration(TTestContext& ctx)
{
    TAllocationGate gate;
    memory::CMemoryAllocator allocator_a(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator allocator_b(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context_a(allocator_a, system_ids::host);
    memory::CMemoryContext context_b(allocator_b, system_ids::host);
    const TThreadContextScope thread_context_scope(&context_a);

    {
        TQueue<char> ambient_queue;
        TEST_EXPECT_EQ(ctx, ambient_queue.memory_context(), &context_a);
        TEST_EXPECT_TRUE(ctx, ambient_queue.initialise_fixed(32u, false));
        TEST_EXPECT_EQ(ctx, ambient_queue.memory_context(), &context_a);
    }

    {
        TQueue<char> explicit_ctor_queue(&context_b);
        TEST_EXPECT_EQ(ctx, explicit_ctor_queue.memory_context(), &context_b);
        TEST_EXPECT_TRUE(ctx, explicit_ctor_queue.initialise_fixed(32u, false));
        TEST_EXPECT_EQ(ctx, explicit_ctor_queue.memory_context(), &context_b);
    }

    {
        TQueue<char> explicit_fixed_queue;
        TEST_EXPECT_EQ(ctx, explicit_fixed_queue.memory_context(), &context_a);
        TEST_EXPECT_TRUE(ctx, explicit_fixed_queue.initialise_fixed(32u, false, &context_b));
        TEST_EXPECT_EQ(ctx, explicit_fixed_queue.memory_context(), &context_b);
        explicit_fixed_queue.deallocate();
        TEST_EXPECT_EQ(ctx, explicit_fixed_queue.memory_context(), &context_b);
    }

    {
        TQueue<char> explicit_growable_queue;
        TEST_EXPECT_TRUE(ctx, explicit_growable_queue.initialise_growable(8u, 64u, &context_b));
        TEST_EXPECT_EQ(ctx, explicit_growable_queue.memory_context(), &context_b);
    }
}

void test_queue_initialise_rejection_and_conditioning(TTestContext& ctx)
{
    {
        TQueue<char> queue;
        TEST_EXPECT_FALSE(ctx, queue.initialise_fixed(TQueue<char>::k_max_capacity + 1u));
        TEST_EXPECT_FALSE(ctx, queue.posting_is_ready());
        TEST_EXPECT_TRUE(ctx, queue.validate());
    }

    {
        TQueue<char> queue;
        TEST_EXPECT_FALSE(ctx, queue.initialise_growable(128u, 64u));
        TEST_EXPECT_FALSE(ctx, queue.posting_is_ready());
        TEST_EXPECT_TRUE(ctx, queue.validate());
    }

    {
        TQueue<char> queue;
        TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(0u, false));
        TEST_EXPECT_TRUE(ctx, queue.posting_is_ready());
        TEST_EXPECT_TRUE(ctx, queue.reading_is_ready());
        TEST_EXPECT_TRUE(ctx, queue.validate());

        TEST_EXPECT_FALSE(ctx, queue.initialise_fixed(32u, false));
    }
}

void test_queue_respects_byte_ceiling(TTestContext& ctx)
{
    struct alignas(256) TLargePod
    {
        std::uint8_t bytes[4096];
    };

    TQueue<TLargePod> queue;
    constexpr std::uint32_t max_elements =
        static_cast<std::uint32_t>(std::min<std::size_t>(
            TQueue<TLargePod>::k_max_capacity,
            memory::t_max_elements<TLargePod>()));

    TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(TQueue<TLargePod>::k_min_capacity, false));
    TEST_EXPECT_TRUE(ctx, queue.posting_is_ready());
    TEST_EXPECT_TRUE(ctx, queue.reading_is_ready());
    TEST_EXPECT_TRUE(ctx, queue.validate());

    queue.deallocate();

    if (max_elements < std::numeric_limits<std::uint32_t>::max())
    {
        TEST_EXPECT_FALSE(ctx, queue.initialise_fixed(max_elements + 1u, false));
        TEST_EXPECT_FALSE(ctx, queue.posting_is_ready());
        TEST_EXPECT_FALSE(ctx, queue.reading_is_ready());
        TEST_EXPECT_TRUE(ctx, queue.validate());
    }
}

void test_queue_initial_allocation_failure(TTestContext& ctx)
{
    TAllocationGate gate;
    gate.allow_allocations = false;
    memory::CMemoryAllocator allocator(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TThreadContextScope thread_context_scope(&context);

    TQueue<char> queue;
    TEST_EXPECT_FALSE(ctx, queue.initialise_fixed(32u, false));

    TEST_EXPECT_FALSE(ctx, queue.posting_is_ready());
    TEST_EXPECT_FALSE(ctx, queue.reading_is_ready());
    TEST_EXPECT_FALSE(ctx, queue.posting_poisoned());
    TEST_EXPECT_TRUE(ctx, queue.validate());
}

void test_queue_zero_and_null_behaviour(TTestContext& ctx)
{
    TQueue<char> queue;
    TPodConstView<char> invalid_src;
    TPodView<char> invalid_dst;

    TEST_EXPECT_FALSE(ctx, queue.post(invalid_src));
    TEST_EXPECT_FALSE(ctx, queue.read(invalid_dst));
    TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(32u, false));

    TEST_EXPECT_TRUE(ctx, queue.post(nullptr, 0u));
    TEST_EXPECT_TRUE(ctx, queue.read(nullptr, 0u));

    TEST_EXPECT_FALSE(ctx, queue.post(nullptr, 1u));
    TEST_EXPECT_FALSE(ctx, queue.read(nullptr, 1u));
    TEST_EXPECT_FALSE(ctx, queue.post(invalid_src));
    TEST_EXPECT_FALSE(ctx, queue.read(invalid_dst));

    char src = 'V';
    char dst = '\0';
    TEST_EXPECT_TRUE(ctx, queue.post(TPodConstView<char>{ &src, 1u }));
    TEST_EXPECT_EQ(ctx, queue.refresh_readable_count(), 1u);
    TEST_EXPECT_TRUE(ctx, queue.read(TPodView<char>{ &dst, 1u }));
    TEST_EXPECT_EQ(ctx, dst, src);
}

void test_queue_basic_publication_and_read(TTestContext& ctx)
{
    TQueue<char> queue;
    TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(32u, false));

    TEST_EXPECT_TRUE(ctx, post_string(queue, "HELLO"));
    TEST_EXPECT_EQ(ctx, queue.current_readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, queue.refresh_readable_count(), 5u);
    TEST_EXPECT_EQ(ctx, queue.current_readable_count(), 5u);

    std::string out;
    TEST_EXPECT_TRUE(ctx, read_exact_string(queue, out, 5u));
    TEST_EXPECT_EQ(ctx, out, std::string("HELLO"));
    TEST_EXPECT_EQ(ctx, queue.current_readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, queue.refresh_readable_count(), 0u);

    TEST_EXPECT_TRUE(ctx, queue.validate());
}

void test_queue_multiple_posts_before_read(TTestContext& ctx)
{
    TQueue<char> queue;
    TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(32u, false));

    TEST_EXPECT_TRUE(ctx, post_string(queue, "ABC"));
    TEST_EXPECT_TRUE(ctx, post_string(queue, "DEF"));
    TEST_EXPECT_TRUE(ctx, post_string(queue, "GHI"));

    TEST_EXPECT_EQ(ctx, drain_queue(queue), std::string("ABCDEFGHI"));
    TEST_EXPECT_TRUE(ctx, queue.validate());
}

void test_queue_fixed_no_discard_rejects_overflow(TTestContext& ctx)
{
    TQueue<char> queue;
    TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(32u, false));

    TEST_EXPECT_TRUE(ctx, post_string(queue, make_repeated_string('A', 24u)));
    TEST_EXPECT_FALSE(ctx, post_string(queue, make_repeated_string('B', 16u)));

    TEST_EXPECT_FALSE(ctx, queue.posting_poisoned());
    TEST_EXPECT_TRUE(ctx, queue.posting_is_ready());
    TEST_EXPECT_TRUE(ctx, queue.validate());

    TEST_EXPECT_EQ(ctx, drain_queue(queue), make_repeated_string('A', 24u));
}

void test_queue_fixed_discard_replaces_buffered_data(TTestContext& ctx)
{
    TQueue<char> queue;
    TEST_EXPECT_TRUE(ctx, queue.initialise_fixed(32u, true));

    TEST_EXPECT_TRUE(ctx, post_string(queue, "ABCDEFGHIJKLMNO"));
    TEST_EXPECT_TRUE(ctx, post_string(queue, "PQRSTUVWXYZ123456"));
    TEST_EXPECT_TRUE(ctx, post_string(queue, make_repeated_string('X', 20u)));

    const std::string drained = drain_queue(queue);
    TEST_EXPECT_EQ(ctx, drained, make_repeated_string('X', 20u));
    TEST_EXPECT_TRUE(ctx, queue.validate());
}

//  Reallocation test functions.
//
//  These use the post_would_reallocate(count) function only as a positive predictor
//  for allocation-failure injection. A true result identifies a known producer-side
//  reallocation pressure point. A false result is treated as inconclusive and does
//  not imply that no reallocation could occur on the full post path.

struct TPositiveReallocationPoint
{
    std::size_t step_index = 0u;
    std::uint32_t ordinal = 0u;
};

std::vector<TPositiveReallocationPoint> find_positive_reallocation_points(const std::vector<TStep>& steps)
{
    constexpr std::uint32_t requested_initial_capacity = 8u;
    constexpr std::uint32_t max_capacity = 128u;

    std::vector<TPositiveReallocationPoint> points;

    TQueue<char> queue;
    if (!queue.initialise_growable(requested_initial_capacity, max_capacity))
    {
        return points;
    }

    for (std::size_t i = 0u; i < steps.size(); ++i)
    {
        const TStep& step = steps[i];

        if (queue.post_would_reallocate(step.count))
        {
            points.push_back(TPositiveReallocationPoint{ i, static_cast<std::uint32_t>(points.size() + 1u) });
        }

        const std::string payload = make_payload(step);
        if (!queue.post(payload.data(), step.count))
        {
            break;
        }
    }

    return points;
}

void run_queue_resize_success_script(tests::TTestContext& ctx,
    const std::vector<TStep>& steps,
    const char* const case_name)
{
    constexpr std::uint32_t requested_initial_capacity = 8u;
    constexpr std::uint32_t max_capacity = 128u;

    TAllocationGate gate;
    memory::CMemoryAllocator allocator(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TThreadContextScope thread_context_scope(&context);

    TQueue<char> queue;
    TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.initialise_growable(requested_initial_capacity, max_capacity));

    std::string expected;
    std::uint32_t positive_reallocation_count = 0u;

    for (const TStep& step : steps)
    {
        if (queue.post_would_reallocate(step.count))
        {
            ++positive_reallocation_count;
        }

        const std::string payload = make_payload(step);
        const bool accepted = queue.post(payload.data(), step.count);

        TEST_CASE_EXPECT_TRUE(ctx, case_name, accepted);
        TEST_CASE_EXPECT_FALSE(ctx, case_name, queue.posting_poisoned());
        TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.posting_is_ready());
        TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.reading_is_ready());
        TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.validate());

        if (!accepted)
        {
            return;
        }

        expected += payload;
    }

    TEST_CASE_EXPECT_EQ(ctx, case_name, tests::drain_queue(queue), expected);
    TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.validate());

    //  Optional sanity check: cases named with R* should have exercised at least one positive reallocation point.
    TEST_CASE_EXPECT_TRUE(ctx, case_name, positive_reallocation_count > 0u);
}

void run_queue_resize_failure_script(tests::TTestContext& ctx,
    const std::vector<TStep>& steps,
    const char* const case_name,
    const std::uint32_t fail_point_ordinal)
{
    constexpr std::uint32_t requested_initial_capacity = 8u;
    constexpr std::uint32_t max_capacity = 128u;

    TAllocationGate gate;
    memory::CMemoryAllocator allocator(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TThreadContextScope thread_context_scope(&context);

    TQueue<char> queue;
    TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.initialise_growable(requested_initial_capacity, max_capacity));

    std::uint32_t current_positive_point = 0u;
    std::string expected_before_failure;

    for (std::size_t i = 0u; i < steps.size(); ++i)
    {
        const TStep& step = steps[i];
        const std::string payload = make_payload(step);

        const bool positive_reallocation = queue.post_would_reallocate(step.count);
        if (positive_reallocation)
        {
            ++current_positive_point;
        }

        if (positive_reallocation && (current_positive_point == fail_point_ordinal))
        {
            gate.allow_allocations = false;
            TEST_CASE_EXPECT_FALSE(ctx, case_name, queue.post(payload.data(), step.count));
            gate.allow_allocations = true;

            TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.posting_poisoned());
            TEST_CASE_EXPECT_FALSE(ctx, case_name, queue.posting_is_ready());
            TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.reading_is_ready());
            TEST_CASE_EXPECT_FALSE(ctx, case_name, queue.posting_is_valid());
            TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.reading_is_valid());

            TEST_CASE_EXPECT_FALSE(ctx, case_name, queue.post("Z", 1u));
            TEST_CASE_EXPECT_EQ(ctx, case_name, tests::drain_queue(queue), expected_before_failure);
            return;
        }

        const bool accepted = queue.post(payload.data(), step.count);
        TEST_CASE_EXPECT_TRUE(ctx, case_name, accepted);
        TEST_CASE_EXPECT_FALSE(ctx, case_name, queue.posting_poisoned());
        TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.posting_is_ready());
        TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.reading_is_ready());
        TEST_CASE_EXPECT_TRUE(ctx, case_name, queue.validate());

        if (!accepted)
        {
            return;
        }

        expected_before_failure += payload;
    }

    TEST_CASE_EXPECT_TRUE(ctx, case_name, false);
}

void test_queue_growable_resize_matrix(tests::TTestContext& ctx)
{

    //  Success-path resize scripts must remain logically admissible with allocation
    //  enabled. In particular, cumulative unread payload for these no-read,
    //  no-discard cases must not exceed max_capacity, otherwise post() failure is an
    //  ordinary capacity rejection rather than a resize/allocation failure.

    const std::vector<std::pair<const char*, std::vector<TStep>>> cases =
    {
        { "R1",
            { { 24u, 'a', false }, { 12u, 'b', true } } },

        { "R1_N1_R2",
            { { 24u, 'a', false }, { 12u, 'b', true }, { 1u, 'c', false }, { 28u, 'd', true } } },

        { "R1_N2_R2",
            { { 24u, 'a', false }, { 12u, 'b', true }, { 1u, 'c', false }, { 1u, 'd', false }, { 27u, 'e', true } } },

        { "R1_N3_R2",
            { { 24u, 'a', false }, { 12u, 'b', true }, { 1u, 'c', false }, { 1u, 'd', false }, { 1u, 'e', false }, { 26u, 'f', true } } },

        { "R1_R2",
            { { 24u, 'a', false }, { 12u, 'b', true }, { 29u, 'c', true } } },

            //  Keep cumulative total <= 128 for success-path validity.
            { "R1_R2_R3",
                { { 24u, 'a', false }, { 12u, 'b', true }, { 29u, 'c', true }, { 63u, 'd', true } } },

            { "R1_N1_R2_R3",
                { { 24u, 'a', false }, { 12u, 'b', true }, { 1u, 'c', false }, { 28u, 'd', true }, { 63u, 'e', true } } },
    };

    for (const auto& test_case : cases)
    {
        run_queue_resize_success_script(ctx, test_case.second, test_case.first);

        const std::vector<TPositiveReallocationPoint> points = find_positive_reallocation_points(test_case.second);
        for (std::uint32_t fail_point_ordinal = 1u;
            fail_point_ordinal <= static_cast<std::uint32_t>(points.size());
            ++fail_point_ordinal)
        {
            run_queue_resize_failure_script(ctx, test_case.second, test_case.first, fail_point_ordinal);
        }
    }
}

void test_queue_reading_can_drain_after_poison(tests::TTestContext& ctx)
{
    TAllocationGate gate;
    memory::CMemoryAllocator allocator(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TThreadContextScope thread_context_scope(&context);

    TQueue<char> queue;
    TEST_EXPECT_TRUE(ctx, queue.initialise_growable(8u, 128u));

    TEST_EXPECT_TRUE(ctx, tests::post_string(queue, "ABCDEF"));

    const std::string growth_payload = tests::make_repeated_string('G', 27u);
    TEST_EXPECT_TRUE(ctx, queue.post_would_reallocate(static_cast<std::uint32_t>(growth_payload.size())));

    gate.allow_allocations = false;
    TEST_EXPECT_FALSE(ctx, queue.post(growth_payload.data(), static_cast<std::uint32_t>(growth_payload.size())));
    gate.allow_allocations = true;

    TEST_EXPECT_TRUE(ctx, queue.posting_poisoned());
    TEST_EXPECT_EQ(ctx, tests::drain_queue(queue), std::string("ABCDEF"));
    TEST_EXPECT_EQ(ctx, queue.refresh_readable_count(), 0u);
}

void test_queue_deallocate_restores_empty(TTestContext& ctx)
{
    TQueue<char> queue;
    TEST_EXPECT_TRUE(ctx, queue.initialise_growable(8u, 64u));
    TEST_EXPECT_TRUE(ctx, post_string(queue, "DATA"));

    queue.deallocate();

    TEST_EXPECT_TRUE(ctx, queue.posting_is_valid());
    TEST_EXPECT_TRUE(ctx, queue.reading_is_valid());
    TEST_EXPECT_FALSE(ctx, queue.posting_is_ready());
    TEST_EXPECT_FALSE(ctx, queue.reading_is_ready());
    TEST_EXPECT_FALSE(ctx, queue.posting_poisoned());
    TEST_EXPECT_TRUE(ctx, queue.validate());
    TEST_EXPECT_EQ(ctx, queue.current_readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, queue.refresh_readable_count(), 0u);
}

int test_queue_transport()
{
    TTestContext ctx;
    TAllocationGate gate;
    memory::CMemoryAllocator allocator(&gate, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TThreadContextScope thread_context_scope(&context);

    test_queue_uninitialised_state(ctx);
    test_queue_memory_context_configuration(ctx);
    test_queue_initialise_rejection_and_conditioning(ctx);
    test_queue_respects_byte_ceiling(ctx);
    test_queue_initial_allocation_failure(ctx);
    test_queue_zero_and_null_behaviour(ctx);
    test_queue_basic_publication_and_read(ctx);
    test_queue_multiple_posts_before_read(ctx);
    test_queue_fixed_no_discard_rejects_overflow(ctx);
    test_queue_fixed_discard_replaces_buffered_data(ctx);
    test_queue_growable_resize_matrix(ctx);
    test_queue_reading_can_drain_after_poison(ctx);
    test_queue_deallocate_restores_empty(ctx);

    print_summary("TQueueTransport", ctx);
    return ctx.exit_code();
}

}   //  namespace tests

int run_queue_transport_tests()
{
    return tests::test_queue_transport();
}
