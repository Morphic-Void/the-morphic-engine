
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  TRingTransport_test_suite.cpp
//
//  Standalone test suite for threading::transports::TRing<T>.

#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "threading/transports/TRingTransport.hpp"
#include "tests/test_suites/TRingTransport_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "memory/memory_context.hpp"
#include "debug/macros.hpp"

using threading::transports::TRing;

namespace tests
{

inline std::string make_repeated_string(const char c, const std::uint32_t count)
{
    return std::string(static_cast<std::size_t>(count), c);
}

static void* MV_STD_ABI_CALL allocate_test_memory(void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    const std::size_t effective_alignment = (alignment < alignof(void*)) ? alignof(void*) : alignment;
    return ::operator new(bytes, std::align_val_t{ effective_alignment }, std::nothrow);
}

static bool MV_STD_ABI_CALL deallocate_test_memory(void*, const std::size_t alignment, void* const ptr) noexcept
{
    const std::size_t effective_alignment = (alignment < alignof(void*)) ? alignof(void*) : alignment;
    ::operator delete(ptr, std::align_val_t{ effective_alignment });
    return true;
}

static void* MV_STD_ABI_CALL reject_allocation(void*, const std::size_t, const std::size_t) noexcept
{
    return nullptr;
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

void test_ring_uninitialised_state(TTestContext& ctx)
{
    TRing<char> ring;

    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_TRUE(ctx, ring.posting_is_valid());
    TEST_EXPECT_TRUE(ctx, ring.reading_is_valid());

    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);

    TEST_EXPECT_TRUE(ctx, ring.post(nullptr, 0u) == false); // not ready
    TEST_EXPECT_TRUE(ctx, ring.read(nullptr, 0u) == false); // not ready
}

void test_ring_memory_context_configuration(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator_a(nullptr, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator allocator_b(nullptr, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context_a(allocator_a, system_ids::host);
    memory::CMemoryContext context_b(allocator_b, system_ids::host);
    memory::CMemoryContext* const previous_thread_context = memory::set_thread_memory_context(&context_a);

    {
        TRing<char> ambient_ring;
        TEST_EXPECT_EQ(ctx, ambient_ring.memory_context(), &context_a);
        TEST_EXPECT_TRUE(ctx, ambient_ring.initialise(32u));
        TEST_EXPECT_EQ(ctx, ambient_ring.memory_context(), &context_a);
    }

    {
        TRing<char> explicit_ctor_ring(&context_b);
        TEST_EXPECT_EQ(ctx, explicit_ctor_ring.memory_context(), &context_b);
        TEST_EXPECT_TRUE(ctx, explicit_ctor_ring.initialise(32u));
        TEST_EXPECT_EQ(ctx, explicit_ctor_ring.memory_context(), &context_b);
    }

    {
        TRing<char> explicit_init_ring;
        TEST_EXPECT_EQ(ctx, explicit_init_ring.memory_context(), &context_a);
        TEST_EXPECT_TRUE(ctx, explicit_init_ring.initialise(32u, &context_b));
        TEST_EXPECT_EQ(ctx, explicit_init_ring.memory_context(), &context_b);
        explicit_init_ring.deallocate();
        TEST_EXPECT_EQ(ctx, explicit_init_ring.memory_context(), &context_b);
    }

    (void)memory::set_thread_memory_context(previous_thread_context);
}

void test_ring_initialise_and_conditioning(TTestContext& ctx)
{
    {
        TRing<char> ring;
        TEST_EXPECT_TRUE(ctx, ring.initialise(0u));
        TEST_EXPECT_TRUE(ctx, ring.is_ready());
        TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
        TEST_EXPECT_EQ(ctx, ring.writable_count(), TRing<char>::k_min_capacity);
    }

    {
        TRing<char> ring;
        TEST_EXPECT_TRUE(ctx, ring.initialise(33u));
        TEST_EXPECT_EQ(ctx, ring.writable_count(), 64u);
        TEST_EXPECT_FALSE(ctx, ring.initialise(64u));
    }

    {
        TRing<char> ring;
        TEST_EXPECT_FALSE(ctx, ring.initialise(TRing<char>::k_max_capacity + 1u));
        TEST_EXPECT_FALSE(ctx, ring.is_ready());
        TEST_EXPECT_TRUE(ctx, ring.is_valid());
    }

    {
        struct alignas(256) TLargePod
        {
            std::uint8_t bytes[4096];
        };

        TRing<TLargePod> ring;
        constexpr std::uint32_t max_elements =
            static_cast<std::uint32_t>(std::min<std::size_t>(
                TRing<TLargePod>::k_max_capacity,
                memory::t_max_elements<TLargePod>()));

        TEST_EXPECT_TRUE(ctx, ring.initialise(TRing<TLargePod>::k_min_capacity));
        TEST_EXPECT_TRUE(ctx, ring.is_ready());
        TEST_EXPECT_EQ(ctx, ring.writable_count(), TRing<TLargePod>::k_min_capacity);

        ring.deallocate();

        if (max_elements < std::numeric_limits<std::uint32_t>::max())
        {
            TEST_EXPECT_FALSE(ctx, ring.initialise(max_elements + 1u));
            TEST_EXPECT_FALSE(ctx, ring.is_ready());
            TEST_EXPECT_TRUE(ctx, ring.is_valid());
        }
    }
}

void test_ring_initial_allocation_failure(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator(nullptr, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator rejecting_allocator(nullptr, reject_allocation, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext usable_context(allocator, system_ids::host);
    memory::CMemoryContext rejecting_context(rejecting_allocator, system_ids::host);
    memory::CMemoryContext* const previous_thread_context = memory::set_thread_memory_context(&usable_context);

    (void)memory::set_thread_memory_context(&rejecting_context);
    TRing<char> ring;

    TEST_EXPECT_FALSE(ctx, ring.initialise(TRing<char>::k_min_capacity));
    (void)memory::set_thread_memory_context(previous_thread_context);

    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_TRUE(ctx, ring.is_valid());
}

void test_ring_zero_and_null_behaviour(TTestContext& ctx)
{
    TRing<char> ring;
    TPodConstView<char> invalid_src;
    TPodView<char> invalid_dst;

    TEST_EXPECT_FALSE(ctx, ring.post(invalid_src));
    TEST_EXPECT_FALSE(ctx, ring.read(invalid_dst));
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    TEST_EXPECT_TRUE(ctx, ring.post(nullptr, 0u));
    TEST_EXPECT_TRUE(ctx, ring.read(nullptr, 0u));

    TEST_EXPECT_FALSE(ctx, ring.post(nullptr, 1u));
    TEST_EXPECT_FALSE(ctx, ring.read(nullptr, 1u));
    TEST_EXPECT_FALSE(ctx, ring.post(invalid_src));
    TEST_EXPECT_FALSE(ctx, ring.read(invalid_dst));

    char src = 'V';
    char dst = '\0';
    TEST_EXPECT_TRUE(ctx, ring.post(TPodConstView<char>{ &src, 1u }));
    TEST_EXPECT_TRUE(ctx, ring.read(TPodView<char>{ &dst, 1u }));
    TEST_EXPECT_EQ(ctx, dst, src);
}

void test_ring_basic_single_and_bulk_transfer(TTestContext& ctx)
{
    TRing<char> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    TEST_EXPECT_TRUE(ctx, ring.post('A'));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 1u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 31u);

    char c = '\0';
    TEST_EXPECT_TRUE(ctx, ring.read(c));
    TEST_EXPECT_EQ(ctx, c, 'A');
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 32u);

    const std::string text = "HELLO";
    TEST_EXPECT_TRUE(ctx, post_string(ring, text));
    TEST_EXPECT_EQ(ctx, drain_ring(ring), text);
}

void test_ring_full_reject_and_reuse(TTestContext& ctx)
{
    TRing<char> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    const std::string full = make_repeated_string('X', 32u);
    TEST_EXPECT_TRUE(ctx, post_string(ring, full));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 32u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);

    TEST_EXPECT_FALSE(ctx, ring.post('Y'));

    std::string first_half;
    TEST_EXPECT_TRUE(ctx, read_exact_string(ring, first_half, 16u));
    TEST_EXPECT_EQ(ctx, first_half, make_repeated_string('X', 16u));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 16u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 16u);

    const std::string refill = "ABCDEFGHIJKLMNOP";
    TEST_EXPECT_TRUE(ctx, post_string(ring, refill));

    const std::string tail = drain_ring(ring);
    TEST_EXPECT_EQ(ctx, tail, make_repeated_string('X', 16u) + refill);
}

void test_ring_wraparound_write_and_read(TTestContext& ctx)
{
    TRing<char> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    TEST_EXPECT_TRUE(ctx, post_string(ring, "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456"));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 32u);

    std::string prefix;
    TEST_EXPECT_TRUE(ctx, read_exact_string(ring, prefix, 28u));
    TEST_EXPECT_EQ(ctx, prefix, std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ12"));

    TEST_EXPECT_TRUE(ctx, post_string(ring, "abcd"));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 8u);

    const std::string suffix = drain_ring(ring);
    TEST_EXPECT_EQ(ctx, suffix, std::string("3456abcd"));
}

void test_ring_repeated_fill_drain_cycles(TTestContext& ctx)
{
    TRing<char> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    for (std::uint32_t i = 0u; i < 8u; ++i)
    {
        const char c = static_cast<char>('A' + static_cast<char>(i));
        const std::string payload = make_repeated_string(c, 24u);

        TEST_EXPECT_TRUE(ctx, post_string(ring, payload));
        TEST_EXPECT_EQ(ctx, drain_ring(ring), payload);
        TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
        TEST_EXPECT_EQ(ctx, ring.writable_count(), 32u);
        TEST_EXPECT_TRUE(ctx, ring.is_valid());
        TEST_EXPECT_TRUE(ctx, ring.posting_is_valid());
        TEST_EXPECT_TRUE(ctx, ring.reading_is_valid());
    }
}

void test_ring_deallocate_restores_empty(TTestContext& ctx)
{
    TRing<char> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));
    TEST_EXPECT_TRUE(ctx, post_string(ring, "DATA"));

    ring.deallocate();

    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_TRUE(ctx, ring.posting_is_valid());
    TEST_EXPECT_TRUE(ctx, ring.reading_is_valid());
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);
}

int test_ring_transport()
{
    TTestContext ctx;

    test_ring_uninitialised_state(ctx);
    test_ring_memory_context_configuration(ctx);
    test_ring_initialise_and_conditioning(ctx);
    test_ring_initial_allocation_failure(ctx);
    test_ring_zero_and_null_behaviour(ctx);
    test_ring_basic_single_and_bulk_transfer(ctx);
    test_ring_full_reject_and_reuse(ctx);
    test_ring_wraparound_write_and_read(ctx);
    test_ring_repeated_fill_drain_cycles(ctx);
    test_ring_deallocate_restores_empty(ctx);

    print_summary("TRingTransport", ctx);
    return ctx.exit_code();
}

}   //  namespace tests

int run_ring_transport_tests()
{
    return tests::test_ring_transport();
}
