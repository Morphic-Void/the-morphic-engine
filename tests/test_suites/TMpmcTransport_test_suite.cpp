
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  TMpmcTransport_test_suite.cpp
//
//  Standalone test suite for threading::transports::TMpmc transport family.

#include <cstdint>
#include <iostream>
#include <string>

#include "threading/transports/TMpmcTransport.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/system/performance_counter.hpp"
#include "tests/test_suites/TMpmcTransport_test_suite.hpp"
#include "tests/support/test_context.hpp"

using threading::transports::EMpmcTransportStatus;
using threading::transports::TAcquiredArenaSlot;
using threading::transports::TMpmcArenaTransport;
using threading::transports::TMpmcIndexRing;
using threading::transports::TMpmcJobTransport;
using threading::transports::TReservedArenaSlot;

namespace threading::transports
{

struct SMpmcTransportTestAccess
{
    using Transport = TMpmcArenaTransport<std::uint32_t, 16u>;

    //  Model a pop paused after claiming position zero and before releasing
    //  its cell. All later operations use the production transport methods.
    static std::atomic<std::uint32_t>& pause_first_pop(Transport& transport, const bool supplier)
    {
        auto& ring = supplier ? transport.m_supplier_ring : transport.m_populated_ring;
        ring.m_dequeue_position.value.store(1u);
        if (supplier)
        {
            transport.m_outstanding_count.value.fetch_add(1u);
        }
        return ring.m_slots[0].sequence;
    }

    static void recycle_first_slot(Transport& transport)
    {
        std::uint32_t sequence = 0u;
        (void)transport.recycle(&transport.m_arena[0], sequence);
    }
};

}   // namespace threading::transports

namespace tests
{

static void print_summary(const char* const suite_name, const TTestContext& ctx)
{
    std::cout
        << suite_name
        << ": passed=" << ctx.passed
        << " failed=" << ctx.failed
        << '\n';
}

void test_raw_ring_capacity_conditioning(TTestContext& ctx)
{
    TEST_EXPECT_EQ(ctx, TMpmcIndexRing<0u>::k_capacity, 16u);
    TEST_EXPECT_EQ(ctx, TMpmcIndexRing<1u>::k_capacity, 16u);
    TEST_EXPECT_EQ(ctx, TMpmcIndexRing<16u>::k_capacity, 16u);
    TEST_EXPECT_EQ(ctx, TMpmcIndexRing<17u>::k_capacity, 32u);
    TEST_EXPECT_EQ(ctx, TMpmcIndexRing<1000000u>::k_capacity, 1048576u);
}

void test_raw_ring_empty_and_full_start(TTestContext& ctx)
{
    TMpmcIndexRing<16u> empty_ring;
    TMpmcIndexRing<16u> full_ring(true);

    TEST_EXPECT_TRUE(ctx, empty_ring.is_valid());
    TEST_EXPECT_EQ(ctx, empty_ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, empty_ring.writable_count(), 16u);

    TEST_EXPECT_TRUE(ctx, full_ring.is_valid());
    TEST_EXPECT_EQ(ctx, full_ring.readable_count(), 16u);
    TEST_EXPECT_EQ(ctx, full_ring.writable_count(), 0u);

    std::uint32_t payload = 999u;
    std::uint32_t sequence = 999u;
    TEST_EXPECT_TRUE(ctx, full_ring.pop(payload, sequence));
    TEST_EXPECT_EQ(ctx, payload, 0u);
    TEST_EXPECT_EQ(ctx, sequence, 0u);
}

void test_raw_ring_sequence_identity_and_wrap(TTestContext& ctx)
{
    TMpmcIndexRing<16u> ring;
    std::uint32_t ignored_sequence = 0u;

    for (std::uint32_t value = 0u; value < 16u; ++value)
    {
        std::uint32_t push_sequence = 0u;
        TEST_EXPECT_TRUE(ctx, ring.push(100u + value, push_sequence));
        TEST_EXPECT_EQ(ctx, push_sequence, value);
    }

    TEST_EXPECT_FALSE(ctx, ring.push(999u, ignored_sequence));

    for (std::uint32_t value = 0u; value < 16u; ++value)
    {
        std::uint32_t payload = 0u;
        std::uint32_t pop_sequence = 0u;
        TEST_EXPECT_TRUE(ctx, ring.pop(payload, pop_sequence));
        TEST_EXPECT_EQ(ctx, payload, 100u + value);
        TEST_EXPECT_EQ(ctx, pop_sequence, value);
    }

    for (std::uint32_t value = 0u; value < 20u; ++value)
    {
        std::uint32_t push_sequence = 0u;
        std::uint32_t pop_sequence = 0u;
        std::uint32_t payload = 0u;

        TEST_EXPECT_TRUE(ctx, ring.push(200u + value, push_sequence));
        TEST_EXPECT_TRUE(ctx, ring.pop(payload, pop_sequence));
        TEST_EXPECT_EQ(ctx, payload, 200u + value);
        TEST_EXPECT_EQ(ctx, pop_sequence, push_sequence);
    }
}

void test_arena_transport_basic_pipeline(TTestContext& ctx)
{
    TMpmcArenaTransport<std::uint32_t, 16u> transport;
    std::uint32_t reserve_index = 0u;
    std::uint32_t reserve_sequence = 0u;
    std::uint32_t publish_sequence = 0u;
    std::uint32_t acquire_index = 0u;
    std::uint32_t acquire_sequence = 0u;
    std::uint32_t recycle_sequence = 0u;

    TEST_EXPECT_TRUE(ctx, transport.is_valid());
    TEST_EXPECT_TRUE(ctx, transport.is_open());
    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), 0u);

    std::uint32_t* reserved = transport.reserve(reserve_index, reserve_sequence);
    TEST_EXPECT_TRUE(ctx, reserved != nullptr);
    TEST_EXPECT_EQ(ctx, reserve_index, 0u);
    TEST_EXPECT_EQ(ctx, reserve_sequence, 0u);
    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), 1u);

    *reserved = 4242u;
    TEST_EXPECT_TRUE(ctx, transport.publish(reserved, publish_sequence));

    std::uint32_t* acquired = transport.acquire(acquire_index, acquire_sequence);
    TEST_EXPECT_TRUE(ctx, acquired != nullptr);
    TEST_EXPECT_EQ(ctx, acquire_index, reserve_index);
    TEST_EXPECT_EQ(ctx, *acquired, 4242u);

    TEST_EXPECT_TRUE(ctx, transport.recycle(acquired, recycle_sequence));
    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), 0u);
    TEST_EXPECT_TRUE(ctx, transport.is_open());
}

void test_arena_transport_closing_and_closed(TTestContext& ctx)
{
    TMpmcArenaTransport<std::uint32_t, 16u> transport;
    std::uint32_t reserve_index = 0u;
    std::uint32_t reserve_sequence = 0u;
    std::uint32_t publish_sequence = 0u;
    std::uint32_t acquire_index = 0u;
    std::uint32_t acquire_sequence = 0u;
    std::uint32_t recycle_sequence = 0u;

    std::uint32_t* reserved = transport.reserve(reserve_index, reserve_sequence);
    TEST_EXPECT_TRUE(ctx, reserved != nullptr);
    *reserved = 7u;

    TEST_EXPECT_TRUE(ctx, transport.begin_closing());
    TEST_EXPECT_TRUE(ctx, transport.is_closing());
    TEST_EXPECT_TRUE(ctx, transport.reserve(reserve_index, reserve_sequence) == nullptr);

    TEST_EXPECT_TRUE(ctx, transport.publish(reserved, publish_sequence));
    std::uint32_t* acquired = transport.acquire(acquire_index, acquire_sequence);
    TEST_EXPECT_TRUE(ctx, acquired != nullptr);
    TEST_EXPECT_EQ(ctx, *acquired, 7u);
    TEST_EXPECT_TRUE(ctx, transport.recycle(acquired, recycle_sequence));

    TEST_EXPECT_TRUE(ctx, transport.is_closed());
    TEST_EXPECT_TRUE(ctx, transport.reserve(reserve_index, reserve_sequence) == nullptr);
    TEST_EXPECT_TRUE(ctx, transport.acquire(acquire_index, acquire_sequence) == nullptr);
    TEST_EXPECT_FALSE(ctx, transport.publish(acquired, publish_sequence));
    TEST_EXPECT_FALSE(ctx, transport.recycle(acquired, recycle_sequence));
}

void test_arena_transport_immediate_close_when_idle(TTestContext& ctx)
{
    TMpmcArenaTransport<std::uint32_t, 16u> transport;

    TEST_EXPECT_TRUE(ctx, transport.begin_closing());
    TEST_EXPECT_TRUE(ctx, transport.is_closed());
}

void test_arena_transport_shutdown(TTestContext& ctx)
{
    TMpmcArenaTransport<std::uint32_t, 16u> transport;
    std::uint32_t index = 0u;
    std::uint32_t sequence = 0u;

    transport.shutdown();

    TEST_EXPECT_TRUE(ctx, transport.is_shutdown());
    TEST_EXPECT_TRUE(ctx, transport.reserve(index, sequence) == nullptr);
    TEST_EXPECT_TRUE(ctx, transport.acquire(index, sequence) == nullptr);
}

void test_arena_transport_scoped_wrappers(TTestContext& ctx)
{
    TMpmcArenaTransport<std::uint32_t, 16u> transport;

    {
        TReservedArenaSlot<std::uint32_t, 16u> reserved(transport);
        TEST_EXPECT_TRUE(ctx, reserved.is_ready());
        *reserved = 55u;
    }

    {
        TAcquiredArenaSlot<std::uint32_t, 16u> acquired(transport);
        TEST_EXPECT_TRUE(ctx, acquired.is_ready());
        TEST_EXPECT_EQ(ctx, *acquired, 55u);
    }

    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), 0u);
}

void test_job_transport_composition(TTestContext& ctx)
{
    TMpmcJobTransport<std::uint32_t, 16u, std::uint64_t, 32u> transport;
    TEST_EXPECT_TRUE(ctx, transport.is_valid());
    TEST_EXPECT_EQ(ctx, decltype(transport.work)::k_capacity, 16u);
    TEST_EXPECT_EQ(ctx, decltype(transport.feedback)::k_capacity, 32u);
}

struct SArenaCompletion
{
    using Access = threading::transports::SMpmcTransportTestAccess;
    Access::Transport& transport;
    bool recycling;
    std::atomic<bool> started{ false };
    std::atomic<bool> finished{ false };
    bool completed = false;

    static std::uint32_t MV_STD_ABI_CALL run(void* const data) noexcept
    {
        auto& self = *static_cast<SArenaCompletion*>(data);
        auto& transport = self.transport;
        if (self.recycling)
        {
            TReservedArenaSlot<std::uint32_t, 16u> reserved(transport);
            *reserved = 42u;
            (void)reserved.publish();
            TAcquiredArenaSlot<std::uint32_t, 16u> acquired(transport);
            self.started.store(true, std::memory_order_release);
            self.completed = acquired.recycle();
        }
        else
        {
            {
                TAcquiredArenaSlot<std::uint32_t, 16u> acquired(transport);
            }
            TReservedArenaSlot<std::uint32_t, 16u> reserved(transport);
            *reserved = 42u;
            self.started.store(true, std::memory_order_release);
            self.completed = reserved.publish();
        }
        self.finished.store(true, std::memory_order_release);
        return 0u;
    }
};

void test_arena_completion_waits_for_pop(TTestContext& ctx, const bool recycling)
{
    using Access = threading::transports::SMpmcTransportTestAccess;
    Access::Transport transport;
    if (!recycling)
    {
        for (std::uint32_t index = 0u; index < transport.k_capacity; ++index)
        {
            TReservedArenaSlot<std::uint32_t, 16u> reserved(transport);
            *reserved = index;
        }
    }
    auto& paused_sequence = Access::pause_first_pop(transport, recycling);
    SArenaCompletion state{ transport, recycling };
    platform::threading::CThread completion;
    const bool created = completion.create(&SArenaCompletion::run, &state);
    TEST_EXPECT_TRUE(ctx, created);
    if (!created) return;
    while (!state.started.load(std::memory_order_acquire))
    {
        platform::threading::processor_relax();
    }
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT_TRUE(ctx, conversion.init());
    platform::system::CPerfCounter timer;
    (void)timer.update();
    while (!state.finished.load(std::memory_order_acquire) &&
        (timer.query_delta() < conversion.query_ticks_per_second() / 20u))
    {
        platform::threading::processor_relax();
    }
    TEST_EXPECT_FALSE(ctx, state.finished.load(std::memory_order_acquire));
    paused_sequence.store(transport.k_capacity, std::memory_order_release);
    TEST_EXPECT_TRUE(ctx, completion.join_and_close());
    TEST_EXPECT_TRUE(ctx, state.completed);
    Access::recycle_first_slot(transport);
    while (true)
    {
        TAcquiredArenaSlot<std::uint32_t, 16u> acquired(transport);
        if (!acquired) break;
    }
    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), 0u);

    //  Verify that no slot was silently lost, rather than just observing an
    //  apparently successful completion or a decremented submission credit.
    for (std::uint32_t index = 0u; index < transport.k_capacity; ++index)
    {
        TReservedArenaSlot<std::uint32_t, 16u> reserved(transport);
        TEST_EXPECT_TRUE(ctx, reserved.is_ready());
    }
    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), transport.k_capacity);
    while (true)
    {
        TAcquiredArenaSlot<std::uint32_t, 16u> acquired(transport);
        if (!acquired) break;
    }
    TEST_EXPECT_EQ(ctx, transport.outstanding_count(), 0u);
}

int test_mpmc_transport()
{
    TTestContext ctx;

    test_raw_ring_capacity_conditioning(ctx);
    test_raw_ring_empty_and_full_start(ctx);
    test_raw_ring_sequence_identity_and_wrap(ctx);
    test_arena_transport_basic_pipeline(ctx);
    test_arena_transport_closing_and_closed(ctx);
    test_arena_transport_immediate_close_when_idle(ctx);
    test_arena_transport_shutdown(ctx);
    test_arena_transport_scoped_wrappers(ctx);
    test_job_transport_composition(ctx);
    test_arena_completion_waits_for_pop(ctx, true);
    test_arena_completion_waits_for_pop(ctx, false);

    print_summary("TMpmcTransport", ctx);
    return ctx.exit_code();
}

}   // namespace tests

int run_mpmc_transport_tests()
{
    return tests::test_mpmc_transport();
}
