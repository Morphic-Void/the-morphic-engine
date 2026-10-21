
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TOwningTransport_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26
//
//  Standalone test suite for threading::transports::TOwning<T>.

#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "threading/transports/TOwningTransport.hpp"
#include "tests/test_suites/TOwningTransport_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "memory/memory_context.hpp"
#include "debug/macros.hpp"

using threading::transports::TOwning;

namespace tests
{

struct TTrackedValue
{
    static std::uint32_t s_default_constructed;
    static std::uint32_t s_destructed;
    static std::uint32_t s_move_constructed;
    static std::uint32_t s_move_assigned;
    static std::uint32_t s_live_count;

    int value = -1;
    bool engaged = false;

    TTrackedValue() noexcept
        : value(-1)
        , engaged(false)
    {
        ++s_default_constructed;
        ++s_live_count;
    }

    explicit TTrackedValue(const int v) noexcept
        : value(v)
        , engaged(true)
    {
        ++s_default_constructed;
        ++s_live_count;
    }

    TTrackedValue(const TTrackedValue&) = delete;
    TTrackedValue& operator=(const TTrackedValue&) = delete;

    TTrackedValue(TTrackedValue&& other) noexcept
        : value(other.value)
        , engaged(other.engaged)
    {
        ++s_move_constructed;
        ++s_live_count;
        other.value = -1;
        other.engaged = false;
    }

    TTrackedValue& operator=(TTrackedValue&& other) noexcept
    {
        if (this != &other)
        {
            value = other.value;
            engaged = other.engaged;
            other.value = -1;
            other.engaged = false;
        }
        ++s_move_assigned;
        return *this;
    }

    ~TTrackedValue() noexcept
    {
        ++s_destructed;
        --s_live_count;
    }

    static void reset_counters() noexcept
    {
        s_default_constructed = 0u;
        s_destructed = 0u;
        s_move_constructed = 0u;
        s_move_assigned = 0u;
        s_live_count = 0u;
    }
};

struct alignas(64) TLargeAlignedValue
{
    char payload[4096];

    TLargeAlignedValue() noexcept = default;
    TLargeAlignedValue(const TLargeAlignedValue&) = delete;
    TLargeAlignedValue& operator=(const TLargeAlignedValue&) = delete;
    TLargeAlignedValue(TLargeAlignedValue&&) noexcept = default;
    TLargeAlignedValue& operator=(TLargeAlignedValue&&) noexcept { return *this; }
    ~TLargeAlignedValue() noexcept = default;
};

std::uint32_t TTrackedValue::s_default_constructed = 0u;
std::uint32_t TTrackedValue::s_destructed = 0u;
std::uint32_t TTrackedValue::s_move_constructed = 0u;
std::uint32_t TTrackedValue::s_move_assigned = 0u;
std::uint32_t TTrackedValue::s_live_count = 0u;

static void* MV_STD_ABI_CALL allocate_test_memory(void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    return ::operator new(bytes, std::align_val_t{ alignment }, std::nothrow);
}

static bool MV_STD_ABI_CALL deallocate_test_memory(void*, const std::size_t alignment, void* const ptr) noexcept
{
    ::operator delete(ptr, std::align_val_t{ alignment });
    return true;
}

static void* MV_STD_ABI_CALL reject_allocation(void*, const std::size_t, const std::size_t) noexcept
{
    return nullptr;
}

template<typename TTransport>
bool post_value(TTransport& transport, const int value) noexcept
{
    TTrackedValue item(value);
    return transport.post(std::move(item));
}

template<typename TTransport>
bool post_values(TTransport& transport, const std::vector<int>& values) noexcept
{
    for (const int value : values)
    {
        TTrackedValue item(value);
        if (!transport.post(std::move(item)))
        {
            return false;
        }
    }
    return true;
}

template<typename TTransport>
bool read_exact_values(TTransport& transport, std::vector<int>& out, const std::uint32_t count)
{
    out.clear();
    out.reserve(static_cast<std::size_t>(count));

    for (std::uint32_t i = 0u; i < count; ++i)
    {
        TTrackedValue item;
        if (!transport.read(item))
        {
            out.clear();
            return false;
        }
        out.push_back(item.value);
    }

    return true;
}

template<typename TTransport>
std::vector<int> drain_transport(TTransport& transport)
{
    const std::uint32_t count = transport.readable_count();

    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(count));

    for (std::uint32_t i = 0u; i < count; ++i)
    {
        TTrackedValue item;
        if (!transport.read(item))
        {
            return {};
        }
        result.push_back(item.value);
    }

    return result;
}

inline std::vector<int> make_repeated_values(const int value, const std::uint32_t count)
{
    return std::vector<int>(static_cast<std::size_t>(count), value);
}

void test_owning_uninitialised_state(TTestContext& ctx)
{
    TOwning<TTrackedValue> ring;

    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_TRUE(ctx, ring.posting_is_valid());
    TEST_EXPECT_TRUE(ctx, ring.reading_is_valid());

    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);

    TTrackedValue src(123);
    TTrackedValue dst;
    TEST_EXPECT_FALSE(ctx, ring.post(std::move(src)));
    TEST_EXPECT_FALSE(ctx, ring.read(dst));
}

void test_owning_memory_context_configuration(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator_a(nullptr, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator allocator_b(nullptr, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context_a(allocator_a, system_ids::host);
    memory::CMemoryContext context_b(allocator_b, system_ids::host);
    const tests::TMemoryContextScope thread_context_scope{ &context_a };

    {
        TOwning<TTrackedValue> ambient_ring;
        TEST_EXPECT_EQ(ctx, ambient_ring.memory_context(), &context_a);
        TEST_EXPECT_TRUE(ctx, ambient_ring.initialise(32u));
        TEST_EXPECT_EQ(ctx, ambient_ring.memory_context(), &context_a);
    }

    {
        TOwning<TTrackedValue> explicit_ctor_ring(&context_b);
        TEST_EXPECT_EQ(ctx, explicit_ctor_ring.memory_context(), &context_b);
        TEST_EXPECT_TRUE(ctx, explicit_ctor_ring.initialise(32u));
        TEST_EXPECT_EQ(ctx, explicit_ctor_ring.memory_context(), &context_b);
    }

    {
        TOwning<TTrackedValue> explicit_init_ring;
        TEST_EXPECT_EQ(ctx, explicit_init_ring.memory_context(), &context_a);
        TEST_EXPECT_TRUE(ctx, explicit_init_ring.initialise(32u, &context_b));
        TEST_EXPECT_EQ(ctx, explicit_init_ring.memory_context(), &context_b);
        explicit_init_ring.deallocate();
        TEST_EXPECT_EQ(ctx, explicit_init_ring.memory_context(), &context_b);
    }

}

void test_owning_initialise_and_conditioning(TTestContext& ctx)
{
    {
        TTrackedValue::reset_counters();

        TOwning<TTrackedValue> ring;
        TEST_EXPECT_TRUE(ctx, ring.initialise(0u));
        TEST_EXPECT_TRUE(ctx, ring.is_ready());
        TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
        TEST_EXPECT_EQ(ctx, ring.writable_count(), TOwning<TTrackedValue>::k_min_capacity);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_default_constructed, TOwning<TTrackedValue>::k_min_capacity);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, TOwning<TTrackedValue>::k_min_capacity);

        ring.deallocate();
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 0u);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_destructed, TOwning<TTrackedValue>::k_min_capacity);
    }

    {
        TTrackedValue::reset_counters();

        TOwning<TTrackedValue> ring;
        TEST_EXPECT_TRUE(ctx, ring.initialise(33u));
        TEST_EXPECT_EQ(ctx, ring.writable_count(), 64u);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_default_constructed, 64u);
        TEST_EXPECT_FALSE(ctx, ring.initialise(64u));

        ring.deallocate();
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 0u);
    }

    {
        TTrackedValue::reset_counters();

        TOwning<TTrackedValue> ring;
        TEST_EXPECT_FALSE(ctx, ring.initialise(TOwning<TTrackedValue>::k_max_capacity + 1u));
        TEST_EXPECT_FALSE(ctx, ring.is_ready());
        TEST_EXPECT_TRUE(ctx, ring.is_valid());
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_default_constructed, 0u);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 0u);
    }
}

void test_owning_initial_allocation_failure(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    memory::CMemoryAllocator allocator(nullptr, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator rejecting_allocator(nullptr, reject_allocation, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext usable_context(allocator, system_ids::host);
    memory::CMemoryContext rejecting_context(rejecting_allocator, system_ids::host);
    memory::CMemoryContext* const previous_thread_context = memory::set_thread_memory_context(&usable_context);

    (void)memory::set_thread_memory_context(&rejecting_context);
    TOwning<TTrackedValue> ring;

    TEST_EXPECT_FALSE(ctx, ring.initialise(TOwning<TTrackedValue>::k_min_capacity));
    (void)memory::set_thread_memory_context(previous_thread_context);

    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_default_constructed, 0u);
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 0u);
}

void test_owning_respects_byte_ceiling(TTestContext& ctx)
{
    TOwning<TLargeAlignedValue> ring;

    TEST_EXPECT_TRUE(ctx, ring.initialise(TOwning<TLargeAlignedValue>::k_min_capacity));
    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_TRUE(ctx, ring.is_ready());
    TEST_EXPECT_EQ(ctx, ring.writable_count(), TOwning<TLargeAlignedValue>::k_min_capacity);

    ring.deallocate();

    TEST_EXPECT_FALSE(ctx, ring.initialise(TOwning<TLargeAlignedValue>::k_max_capacity));
    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);
}

void test_owning_basic_single_transfer(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    TOwning<TTrackedValue> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    TTrackedValue src(65);
    TEST_EXPECT_TRUE(ctx, ring.post(std::move(src)));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 1u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 31u);
    TEST_EXPECT_FALSE(ctx, src.engaged);
    TEST_EXPECT_EQ(ctx, src.value, -1);

    TTrackedValue dst;
    TEST_EXPECT_TRUE(ctx, ring.read(dst));
    TEST_EXPECT_TRUE(ctx, dst.engaged);
    TEST_EXPECT_EQ(ctx, dst.value, 65);
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 32u);

    ring.deallocate();
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 2u);
}

void test_owning_full_reject_and_reuse(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    TOwning<TTrackedValue> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    TEST_EXPECT_TRUE(ctx, post_values(ring, make_repeated_values(7, 32u)));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 32u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);

    TTrackedValue extra(99);
    TEST_EXPECT_FALSE(ctx, ring.post(std::move(extra)));
    TEST_EXPECT_TRUE(ctx, extra.engaged);
    TEST_EXPECT_EQ(ctx, extra.value, 99);

    std::vector<int> first_half;
    TEST_EXPECT_TRUE(ctx, read_exact_values(ring, first_half, 16u));
    TEST_EXPECT_EQ(ctx, first_half, make_repeated_values(7, 16u));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 16u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 16u);

    const std::vector<int> refill =
    {
        100, 101, 102, 103, 104, 105, 106, 107,
        108, 109, 110, 111, 112, 113, 114, 115
    };

    TEST_EXPECT_TRUE(ctx, post_values(ring, refill));

    const std::vector<int> tail = drain_transport(ring);

    std::vector<int> expected = make_repeated_values(7, 16u);
    expected.insert(expected.end(), refill.begin(), refill.end());

    TEST_EXPECT_EQ(ctx, tail, expected);
}

void test_owning_wraparound_write_and_read(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    TOwning<TTrackedValue> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    std::vector<int> initial;
    initial.reserve(32u);
    for (int i = 1; i <= 32; ++i)
    {
        initial.push_back(i);
    }

    TEST_EXPECT_TRUE(ctx, post_values(ring, initial));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 32u);

    std::vector<int> prefix;
    TEST_EXPECT_TRUE(ctx, read_exact_values(ring, prefix, 28u));

    std::vector<int> expected_prefix;
    expected_prefix.reserve(28u);
    for (int i = 1; i <= 28; ++i)
    {
        expected_prefix.push_back(i);
    }

    TEST_EXPECT_EQ(ctx, prefix, expected_prefix);

    const std::vector<int> refill = { 1001, 1002, 1003, 1004 };
    TEST_EXPECT_TRUE(ctx, post_values(ring, refill));
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 8u);

    const std::vector<int> suffix = drain_transport(ring);
    const std::vector<int> expected_suffix = { 29, 30, 31, 32, 1001, 1002, 1003, 1004 };
    TEST_EXPECT_EQ(ctx, suffix, expected_suffix);
}

void test_owning_repeated_fill_drain_cycles(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    TOwning<TTrackedValue> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));

    for (std::uint32_t cycle = 0u; cycle < 8u; ++cycle)
    {
        const int base = static_cast<int>(cycle * 100u);

        std::vector<int> payload;
        payload.reserve(24u);
        for (int i = 0; i < 24; ++i)
        {
            payload.push_back(base + i);
        }

        TEST_EXPECT_TRUE(ctx, post_values(ring, payload));
        TEST_EXPECT_EQ(ctx, drain_transport(ring), payload);
        TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
        TEST_EXPECT_EQ(ctx, ring.writable_count(), 32u);
        TEST_EXPECT_TRUE(ctx, ring.is_valid());
        TEST_EXPECT_TRUE(ctx, ring.posting_is_valid());
        TEST_EXPECT_TRUE(ctx, ring.reading_is_valid());
    }
}

void test_owning_slot_lifetime_model(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    TOwning<TTrackedValue> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 32u);
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_default_constructed, 32u);

    {
        TTrackedValue src(111);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 33u);

        TEST_EXPECT_TRUE(ctx, ring.post(std::move(src)));
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 33u);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_move_assigned, 1u);
        TEST_EXPECT_FALSE(ctx, src.engaged);
    }

    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 32u);

    {
        TTrackedValue dst;
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 33u);

        TEST_EXPECT_TRUE(ctx, ring.read(dst));
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 33u);
        TEST_EXPECT_EQ(ctx, TTrackedValue::s_move_assigned, 2u);
        TEST_EXPECT_TRUE(ctx, dst.engaged);
        TEST_EXPECT_EQ(ctx, dst.value, 111);
    }

    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 32u);

    ring.deallocate();
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 0u);
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_destructed, TTrackedValue::s_default_constructed);
}

void test_owning_deallocate_restores_empty(TTestContext& ctx)
{
    TTrackedValue::reset_counters();

    TOwning<TTrackedValue> ring;
    TEST_EXPECT_TRUE(ctx, ring.initialise(32u));
    TEST_EXPECT_TRUE(ctx, post_value(ring, 77));

    ring.deallocate();

    TEST_EXPECT_TRUE(ctx, ring.is_valid());
    TEST_EXPECT_FALSE(ctx, ring.is_ready());
    TEST_EXPECT_TRUE(ctx, ring.posting_is_valid());
    TEST_EXPECT_TRUE(ctx, ring.reading_is_valid());
    TEST_EXPECT_EQ(ctx, ring.readable_count(), 0u);
    TEST_EXPECT_EQ(ctx, ring.writable_count(), 0u);
    TEST_EXPECT_EQ(ctx, TTrackedValue::s_live_count, 0u);
}

int test_owning_transport()
{
    TTestContext ctx;

    test_owning_uninitialised_state(ctx);
    test_owning_memory_context_configuration(ctx);
    test_owning_initialise_and_conditioning(ctx);
    test_owning_initial_allocation_failure(ctx);
    test_owning_respects_byte_ceiling(ctx);
    test_owning_basic_single_transfer(ctx);
    test_owning_full_reject_and_reuse(ctx);
    test_owning_wraparound_write_and_read(ctx);
    test_owning_repeated_fill_drain_cycles(ctx);
    test_owning_slot_lifetime_model(ctx);
    test_owning_deallocate_restores_empty(ctx);

    return ctx.report("TOwningTransport");
}

}   //  namespace tests

int run_owning_transport_tests()
{
    return tests::test_owning_transport();
}
