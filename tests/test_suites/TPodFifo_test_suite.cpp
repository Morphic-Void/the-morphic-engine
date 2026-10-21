
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TPodFifo_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    14 Jul 26

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "containers/TPodFifo.hpp"
#include "tests/test_suites/TPodFifo_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

using TFifo = TPodFifo<std::uint32_t>;

void test_constness_and_limits(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<TFifo&>().data()), std::uint32_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const TFifo&>().data()), const std::uint32_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const TPodView<std::uint32_t>&>().data()), std::uint32_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const TPodConstView<std::uint32_t>&>().data()), const std::uint32_t*>);
    static_assert(memory::t_max_elements<std::uint32_t>() ==
        (memory::k_byte_size_ceiling / sizeof(std::uint32_t)));
    static_assert(TFifo::k_max_elements == memory::t_max_elements<std::uint32_t>());

    TFifo fifo;
    TEST_EXPECT(ctx, fifo.is_valid());
    TEST_EXPECT(ctx, fifo.is_empty());
    TEST_EXPECT(ctx, !fifo.is_ready());
    TEST_EXPECT(ctx, fifo.data() == nullptr);
    TEST_EXPECT(ctx, !fifo.allocate(TFifo::k_max_elements + 1u));
}

void test_allocation_and_fifo_order(TTestContext& ctx)
{
    TFifo fifo;
    const std::uint32_t values[]{ 10u, 20u, 30u, 40u };
    std::uint32_t popped[3]{};

    TEST_EXPECT(ctx, fifo.allocate(4u));
    TEST_EXPECT(ctx, fifo.is_ready());
    TEST_EXPECT(ctx, fifo.capacity() == 4u);
    TEST_EXPECT(ctx, fifo.size() == 0u);

    TEST_EXPECT(ctx, fifo.push_back(values, 4u));
    TEST_EXPECT(ctx, fifo.size() == 4u);
    TEST_EXPECT(ctx, !fifo.push_back(50u));
    TEST_EXPECT(ctx, fifo.pop_front(popped, 3u));
    TEST_EXPECT(ctx, popped[0] == 10u);
    TEST_EXPECT(ctx, popped[1] == 20u);
    TEST_EXPECT(ctx, popped[2] == 30u);
    TEST_EXPECT(ctx, fifo.size() == 1u);

    std::uint32_t last = 0u;
    TEST_EXPECT(ctx, fifo.pop_front(&last));
    TEST_EXPECT(ctx, last == 40u);
    TEST_EXPECT(ctx, fifo.is_empty());
}

void test_wraparound_and_same_capacity_pack(TTestContext& ctx)
{
    TFifo fifo;
    const std::uint32_t initial[]{ 1u, 2u, 3u, 4u };
    const std::uint32_t wrapped[]{ 5u, 6u, 7u };
    std::uint32_t popped[4]{};

    TEST_EXPECT(ctx, fifo.allocate(5u));
    TEST_EXPECT(ctx, fifo.push_back(initial, 4u));
    TEST_EXPECT(ctx, fifo.discard_front(2u));
    TEST_EXPECT(ctx, fifo.push_back(wrapped, 3u));
    TEST_EXPECT(ctx, fifo.size() == 5u);
    TEST_EXPECT(ctx, fifo.data()[0] == 6u);
    TEST_EXPECT(ctx, fifo.data()[1] == 7u);

    fifo.pack();
    TEST_EXPECT(ctx, fifo.data()[0] == 3u);
    TEST_EXPECT(ctx, fifo.data()[1] == 4u);
    TEST_EXPECT(ctx, fifo.data()[2] == 5u);
    TEST_EXPECT(ctx, fifo.data()[3] == 6u);
    TEST_EXPECT(ctx, fifo.data()[4] == 7u);

    TEST_EXPECT(ctx, fifo.discard_front(2u));
    TEST_EXPECT(ctx, fifo.push_back(initial, 2u));
    TEST_EXPECT(ctx, fifo.reallocate(5u));
    TEST_EXPECT(ctx, fifo.size() == 5u);
    TEST_EXPECT(ctx, fifo.data()[0] == 5u);
    TEST_EXPECT(ctx, fifo.data()[1] == 6u);
    TEST_EXPECT(ctx, fifo.data()[2] == 7u);
    TEST_EXPECT(ctx, fifo.data()[3] == 1u);
    TEST_EXPECT(ctx, fifo.data()[4] == 2u);
    TEST_EXPECT(ctx, fifo.pop_front(popped, 4u));
    TEST_EXPECT(ctx, popped[0] == 5u);
    TEST_EXPECT(ctx, popped[1] == 6u);
    TEST_EXPECT(ctx, popped[2] == 7u);
    TEST_EXPECT(ctx, popped[3] == 1u);
}

void test_wrapped_pack_path_boundaries(TTestContext& ctx)
{
    const std::uint32_t initial[]{ 1u, 2u, 3u, 4u, 5u, 6u, 7u };
    const std::uint32_t wrapped[]{ 8u, 9u, 10u, 11u };

    TFifo two_move;
    TEST_EXPECT(ctx, two_move.allocate(8u));
    TEST_EXPECT(ctx, two_move.push_back(initial, 7u));
    TEST_EXPECT(ctx, two_move.discard_front(5u));
    TEST_EXPECT(ctx, two_move.push_back(wrapped, 3u));
    TEST_EXPECT(ctx, two_move.size() == 5u);
    two_move.pack();
    TEST_EXPECT(ctx, two_move.data()[0] == 6u);
    TEST_EXPECT(ctx, two_move.data()[1] == 7u);
    TEST_EXPECT(ctx, two_move.data()[2] == 8u);
    TEST_EXPECT(ctx, two_move.data()[3] == 9u);
    TEST_EXPECT(ctx, two_move.data()[4] == 10u);

    TFifo cycle;
    TEST_EXPECT(ctx, cycle.allocate(8u));
    TEST_EXPECT(ctx, cycle.push_back(initial, 7u));
    TEST_EXPECT(ctx, cycle.discard_front(5u));
    TEST_EXPECT(ctx, cycle.push_back(wrapped, 4u));
    TEST_EXPECT(ctx, cycle.size() == 6u);
    cycle.pack();
    TEST_EXPECT(ctx, cycle.data()[0] == 6u);
    TEST_EXPECT(ctx, cycle.data()[1] == 7u);
    TEST_EXPECT(ctx, cycle.data()[2] == 8u);
    TEST_EXPECT(ctx, cycle.data()[3] == 9u);
    TEST_EXPECT(ctx, cycle.data()[4] == 10u);
    TEST_EXPECT(ctx, cycle.data()[5] == 11u);
}

void test_growth_and_shrink_preserve_sequence(TTestContext& ctx)
{
    TFifo fifo;
    const std::uint32_t seed[]{ 11u, 22u, 33u, 44u, 55u, 66u };
    std::uint32_t popped[6]{};

    TEST_EXPECT(ctx, fifo.allocate(6u));
    TEST_EXPECT(ctx, fifo.push_back(seed, 5u));
    TEST_EXPECT(ctx, fifo.discard_front(3u));
    TEST_EXPECT(ctx, fifo.push_back(seed + 5u, 1u));
    TEST_EXPECT(ctx, fifo.reserve(9u));
    TEST_EXPECT(ctx, fifo.capacity() >= 9u);
    TEST_EXPECT(ctx, fifo.data()[0] == 44u);
    TEST_EXPECT(ctx, fifo.data()[1] == 55u);
    TEST_EXPECT(ctx, fifo.data()[2] == 66u);

    TEST_EXPECT(ctx, fifo.pop_front(popped, 3u));
    TEST_EXPECT(ctx, popped[0] == 44u);
    TEST_EXPECT(ctx, popped[1] == 55u);
    TEST_EXPECT(ctx, popped[2] == 66u);
    TEST_EXPECT(ctx, fifo.is_empty());

    TEST_EXPECT(ctx, fifo.push_back(seed, 4u));
    TEST_EXPECT(ctx, fifo.shrink_to_fit());
    TEST_EXPECT(ctx, fifo.capacity() == 4u);
    TEST_EXPECT(ctx, fifo.data()[0] == 11u);
    TEST_EXPECT(ctx, fifo.data()[3] == 44u);
}

void test_bounds_and_failure_cases(TTestContext& ctx)
{
    TFifo fifo;
    std::uint32_t output[2]{};

    TEST_EXPECT(ctx, !fifo.push_back(static_cast<const std::uint32_t*>(nullptr), 1u));
    TEST_EXPECT(ctx, !fifo.pop_front(output, 1u));
    TEST_EXPECT(ctx, fifo.allocate(3u));
    TEST_EXPECT(ctx, fifo.push_back(99u));
    TEST_EXPECT(ctx, !fifo.push_back(static_cast<const std::uint32_t*>(nullptr), 1u));
    TEST_EXPECT(ctx, !fifo.pop_front(static_cast<std::uint32_t*>(nullptr), 1u));
    TEST_EXPECT(ctx, !fifo.pop_front(output, 2u));
    TEST_EXPECT(ctx, !fifo.discard_front(2u));
    TEST_EXPECT(ctx, !fifo.reallocate(0u));
    TEST_EXPECT(ctx, !fifo.reserve(TFifo::k_max_elements + 1u));
    TEST_EXPECT(ctx, !fifo.ensure_free(TFifo::k_max_elements));
}

void test_behavioral_consistency(TTestContext& ctx)
{
    TFifo fifo;
    TPodConstView<std::uint32_t> invalid_src;
    TPodView<std::uint32_t> invalid_dst;

    TEST_EXPECT(ctx, fifo.reserve(0u));
    TEST_EXPECT(ctx, fifo.ensure_free(0u));
    TEST_EXPECT(ctx, !fifo.is_ready());
    TEST_EXPECT(ctx, fifo.capacity() == 0u);
    TEST_EXPECT(ctx, fifo.push_back(static_cast<const std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, fifo.pop_front(static_cast<std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, fifo.discard_front(0u));
    TEST_EXPECT(ctx, !fifo.push_back(invalid_src));
    TEST_EXPECT(ctx, !fifo.pop_front(invalid_dst));

    const std::uint32_t values[]{ 1u, 2u, 3u, 4u, 5u, 6u };
    TEST_EXPECT(ctx, fifo.allocate(5u));
    TEST_EXPECT(ctx, fifo.push_back(static_cast<const std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, fifo.pop_front(static_cast<std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, fifo.discard_front(0u));
    TEST_EXPECT(ctx, !fifo.push_back(invalid_src));
    TEST_EXPECT(ctx, !fifo.pop_front(invalid_dst));
    TEST_EXPECT(ctx, fifo.push_back(values, 4u));

    std::uint32_t* const internal = fifo.data();
    TEST_EXPECT(ctx, !fifo.push_back(internal, 1u));
    TEST_EXPECT(ctx, !fifo.pop_front(internal, 1u));
    TEST_EXPECT(ctx, fifo.size() == 4u);

    TEST_EXPECT(ctx, fifo.discard_front(3u));
    TEST_EXPECT(ctx, fifo.push_back(values + 4u, 2u));
    std::uint32_t popped[3]{};
    TEST_EXPECT(ctx, fifo.pop_front(popped, 3u));
    TEST_EXPECT(ctx, popped[0] == 4u);
    TEST_EXPECT(ctx, popped[1] == 5u);
    TEST_EXPECT(ctx, popped[2] == 6u);
    TEST_EXPECT(ctx, fifo.is_valid());
    TEST_EXPECT(ctx, fifo.push_back(99u));
    TEST_EXPECT(ctx, fifo.data()[0] == 99u);

    TEST_EXPECT(ctx, fifo.allocate(5u));
    TEST_EXPECT(ctx, fifo.push_back(values, 4u));
    TEST_EXPECT(ctx, fifo.discard_front(3u));
    TEST_EXPECT(ctx, fifo.push_back(values + 4u, 2u));
    TEST_EXPECT(ctx, fifo.discard_front(3u));
    TEST_EXPECT(ctx, fifo.is_valid());
    TEST_EXPECT(ctx, fifo.push_back(88u));
    TEST_EXPECT(ctx, fifo.data()[0] == 88u);
}

void test_view_based_operations(TTestContext& ctx)
{
    TFifo fifo;
    alignas(16) std::uint32_t src_storage[]{ 7u, 8u, 9u, 10u };
    alignas(16) std::uint32_t dst_storage[]{ 0u, 0u, 0u, 0u };

    TPodConstView<std::uint32_t> src_view{ src_storage, 4u };
    TPodView<std::uint32_t> dst_view{ dst_storage, 2u };

    TEST_EXPECT(ctx, fifo.allocate(4u));
    TEST_EXPECT(ctx, fifo.push_back(src_view.head_to(3u)));
    TEST_EXPECT(ctx, fifo.size() == 3u);
    TEST_EXPECT(ctx, fifo.pop_front(dst_view));
    TEST_EXPECT(ctx, dst_storage[0] == 7u);
    TEST_EXPECT(ctx, dst_storage[1] == 8u);
    TEST_EXPECT(ctx, fifo.size() == 1u);

    std::uint32_t tail = 0u;
    TEST_EXPECT(ctx, fifo.pop_front(&tail));
    TEST_EXPECT(ctx, tail == 9u);
}

void test_move_and_source_reuse(TTestContext& ctx)
{
    TFifo source;
    const std::uint32_t values[]{ 3u, 6u, 9u, 12u };
    std::uint32_t popped[3]{};

    TEST_EXPECT(ctx, source.allocate(4u));
    TEST_EXPECT(ctx, source.push_back(values, 4u));
    TEST_EXPECT(ctx, source.discard_front(1u));

    TFifo moved{ std::move(source) };
    TEST_EXPECT(ctx, moved.is_valid());
    TEST_EXPECT(ctx, moved.is_ready());
    TEST_EXPECT(ctx, moved.size() == 3u);
    TEST_EXPECT(ctx, moved.pop_front(popped, 3u));
    TEST_EXPECT(ctx, popped[0] == 6u);
    TEST_EXPECT(ctx, popped[1] == 9u);
    TEST_EXPECT(ctx, popped[2] == 12u);

    TEST_EXPECT(ctx, source.is_valid());
    TEST_EXPECT(ctx, source.is_empty());
    TEST_EXPECT(ctx, source.allocate(2u));
    TEST_EXPECT(ctx, source.push_back(42u));
    TEST_EXPECT(ctx, source.data()[0] == 42u);

    TFifo assigned;
    TEST_EXPECT(ctx, assigned.allocate(2u));
    TEST_EXPECT(ctx, assigned.push_back(100u));
    assigned = std::move(source);
    TEST_EXPECT(ctx, assigned.size() == 1u);
    TEST_EXPECT(ctx, assigned.data()[0] == 42u);
    TEST_EXPECT(ctx, source.is_valid());
    TEST_EXPECT(ctx, source.allocate(3u));
    TEST_EXPECT(ctx, source.push_back(values, 2u));
}

}   //  namespace

int run_pod_fifo_tests()
{
    TTestContext ctx;
    test_constness_and_limits(ctx);
    test_allocation_and_fifo_order(ctx);
    test_wraparound_and_same_capacity_pack(ctx);
    test_wrapped_pack_path_boundaries(ctx);
    test_growth_and_shrink_preserve_sequence(ctx);
    test_bounds_and_failure_cases(ctx);
    test_behavioral_consistency(ctx);
    test_view_based_operations(ctx);
    test_move_and_source_reuse(ctx);

    return ctx.report("TPodFifo");
}

