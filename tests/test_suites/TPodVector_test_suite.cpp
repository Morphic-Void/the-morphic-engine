
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TPodVector_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    13 Jul 26

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "containers/TPodVector.hpp"
#include "tests/test_suites/TPodVector_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

void test_constness_and_limits(TTestContext& ctx)
{
    using TVector = TPodVector<std::uint32_t>;

    static_assert(std::is_same_v<decltype(std::declval<TVector&>().data()), std::uint32_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const TVector&>().data()), const std::uint32_t*>);
    static_assert(std::is_same_v<decltype(std::declval<TVector&>().view()), TPodView<std::uint32_t>>);
    static_assert(std::is_same_v<decltype(std::declval<const TVector&>().view()), TPodConstView<std::uint32_t>>);
    static_assert(std::is_same_v<decltype(std::declval<const TPodView<std::uint32_t>&>().data()), std::uint32_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const TPodConstView<std::uint32_t>&>().data()), const std::uint32_t*>);

    TEST_EXPECT(ctx, memory::t_max_elements<std::uint32_t>() ==
        (memory::k_byte_size_ceiling / sizeof(std::uint32_t)));
    TEST_EXPECT(ctx, TVector::k_max_elements == memory::t_max_elements<std::uint32_t>());
}

void test_allocation_and_resizing(TTestContext& ctx)
{
    TPodVector<std::uint32_t> vector;
    TEST_EXPECT(ctx, vector.is_valid());
    TEST_EXPECT(ctx, vector.is_empty());
    TEST_EXPECT(ctx, !vector.is_ready());
    TEST_EXPECT(ctx, !vector.view().is_valid());

    TEST_EXPECT(ctx, vector.allocate(8u));
    TEST_EXPECT(ctx, vector.is_valid());
    TEST_EXPECT(ctx, vector.is_ready());
    TEST_EXPECT(ctx, vector.is_empty());
    TEST_EXPECT(ctx, vector.capacity() == 8u);
    TEST_EXPECT(ctx, !vector.view().is_valid());

    TEST_EXPECT(ctx, vector.resize(3u));
    TEST_EXPECT(ctx, vector.size() == 3u);
    TEST_EXPECT(ctx, vector[0] == 0u);
    TEST_EXPECT(ctx, vector[1] == 0u);
    TEST_EXPECT(ctx, vector[2] == 0u);

    vector[0] = 10u;
    vector[1] = 20u;
    vector[2] = 30u;
    TEST_EXPECT(ctx, vector.reserve(20u));
    TEST_EXPECT(ctx, vector.size() == 3u);
    TEST_EXPECT(ctx, vector[0] == 10u);
    TEST_EXPECT(ctx, vector[1] == 20u);
    TEST_EXPECT(ctx, vector[2] == 30u);

    TEST_EXPECT(ctx, vector.resize(5u));
    TEST_EXPECT(ctx, vector[3] == 0u);
    TEST_EXPECT(ctx, vector[4] == 0u);
    TEST_EXPECT(ctx, vector.reallocate(2u, 2u));
    TEST_EXPECT(ctx, vector.size() == 2u);
    TEST_EXPECT(ctx, vector.capacity() == 2u);
    TEST_EXPECT(ctx, vector[0] == 10u);
    TEST_EXPECT(ctx, vector[1] == 20u);
    TEST_EXPECT(ctx, vector.shrink_to_fit());
}

void test_vector_operations(TTestContext& ctx)
{
    TPodVector<std::uint32_t> vector;
    const std::uint32_t initial[]{ 1u, 2u, 3u };
    const std::uint32_t inserted[]{ 7u, 8u };

    TEST_EXPECT(ctx, vector.push_back(initial, 3u));
    TEST_EXPECT(ctx, vector.insert(1u, inserted, 2u));
    TEST_EXPECT(ctx, vector.size() == 5u);
    TEST_EXPECT(ctx, vector[0] == 1u);
    TEST_EXPECT(ctx, vector[1] == 7u);
    TEST_EXPECT(ctx, vector[2] == 8u);
    TEST_EXPECT(ctx, vector[3] == 2u);
    TEST_EXPECT(ctx, vector[4] == 3u);

    TEST_EXPECT(ctx, vector.erase(2u, 2u));
    TEST_EXPECT(ctx, vector.size() == 3u);
    TEST_EXPECT(ctx, vector[0] == 1u);
    TEST_EXPECT(ctx, vector[1] == 7u);
    TEST_EXPECT(ctx, vector[2] == 3u);

    std::uint32_t popped[2]{};
    TEST_EXPECT(ctx, vector.pop_back_preserve_order(popped, 2u));
    TEST_EXPECT(ctx, popped[0] == 7u);
    TEST_EXPECT(ctx, popped[1] == 3u);
    TEST_EXPECT(ctx, vector.size() == 1u);
}

void test_discard_operations(TTestContext& ctx)
{
    TPodVector<std::uint32_t> vector;
    const std::uint32_t values[]{ 1u, 2u, 3u, 4u };

    TEST_EXPECT(ctx, vector.push_back(values, 4u));
    TEST_EXPECT(ctx, vector.discard_back(2u));
    TEST_EXPECT(ctx, vector.size() == 2u);
    TEST_EXPECT(ctx, vector[0] == 1u);
    TEST_EXPECT(ctx, vector[1] == 2u);
    TEST_EXPECT(ctx, !vector.discard_back(3u));
    TEST_EXPECT(ctx, vector.discard_back(0u));
    TEST_EXPECT(ctx, vector.size() == 2u);
    TEST_EXPECT(ctx, vector.discard_back());
    TEST_EXPECT(ctx, vector.size() == 1u);
    TEST_EXPECT(ctx, vector.try_discard_back(3u) == 1u);
    TEST_EXPECT(ctx, vector.is_empty());
    TEST_EXPECT(ctx, vector.try_discard_back() == 0u);
}

void test_behavioral_consistency(TTestContext& ctx)
{
    using TVector = TPodVector<std::uint32_t>;

    TVector vector;
    TPodConstView<std::uint32_t> invalid_src;
    TPodView<std::uint32_t> invalid_dst;

    TEST_EXPECT(ctx, vector.reserve(0u));
    TEST_EXPECT(ctx, vector.ensure_free(0u));
    TEST_EXPECT(ctx, !vector.is_ready());
    TEST_EXPECT(ctx, vector.capacity() == 0u);
    TEST_EXPECT(ctx, vector.push_back(static_cast<const std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, vector.pop_back(static_cast<std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, vector.pop_back_preserve_order(static_cast<std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, vector.discard_back(0u));
    TEST_EXPECT(ctx, !vector.push_back(invalid_src));
    TEST_EXPECT(ctx, !vector.pop_back(invalid_dst));
    TEST_EXPECT(ctx, !vector.pop_back_preserve_order(invalid_dst));
    TEST_EXPECT(ctx, vector.try_push_back(invalid_src) == 0u);
    TEST_EXPECT(ctx, vector.try_pop_back(invalid_dst) == 0u);

    const std::uint32_t values[]{ 1u, 2u };
    TEST_EXPECT(ctx, vector.allocate(4u));
    TEST_EXPECT(ctx, vector.reserve(0u));
    TEST_EXPECT(ctx, vector.capacity() == 4u);
    TEST_EXPECT(ctx, vector.push_back(static_cast<const std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, vector.pop_back(static_cast<std::uint32_t*>(nullptr), 0u));
    TEST_EXPECT(ctx, vector.discard_back(0u));
    TEST_EXPECT(ctx, !vector.push_back(invalid_src));
    TEST_EXPECT(ctx, !vector.pop_back(invalid_dst));
    TEST_EXPECT(ctx, vector.push_back(values, 2u));

    std::uint32_t* const internal = vector.data();
    TEST_EXPECT(ctx, !vector.push_back(internal, 1u));
    TEST_EXPECT(ctx, !vector.pop_back(internal, 1u));
    TEST_EXPECT(ctx, !vector.pop_back_preserve_order(internal, 1u));
    TEST_EXPECT(ctx, vector.size() == 2u);
    TEST_EXPECT(ctx, vector[0] == 1u);
    TEST_EXPECT(ctx, vector[1] == 2u);
}

void test_views_and_slicing(TTestContext& ctx)
{
    alignas(16) std::uint32_t storage[]{ 11u, 22u, 33u, 44u, 55u };
    TPodView<std::uint32_t> view{ storage, 5u };

    TEST_EXPECT(ctx, view.is_valid());
    TEST_EXPECT(ctx, view.size() == 5u);
    TEST_EXPECT(ctx, view.data() == storage);

    TPodView<std::uint32_t> middle = view.subview(1u, 3u);
    TEST_EXPECT(ctx, middle.is_valid());
    TEST_EXPECT(ctx, middle.size() == 3u);
    TEST_EXPECT(ctx, middle.data() == (storage + 1u));
    middle[1] = 99u;
    TEST_EXPECT(ctx, storage[2] == 99u);

    TEST_EXPECT(ctx, view.head_to(2u).size() == 2u);
    TEST_EXPECT(ctx, view.tail_from(3u).size() == 2u);
    TEST_EXPECT(ctx, !view.subview(5u, 1u).is_valid());
    TEST_EXPECT(ctx, !view.subview(1u, 0u).is_valid());
    TEST_EXPECT(ctx, !view.subview(4u, 2u).is_valid());

    const TPodConstView<std::uint32_t> const_view = view.const_view();
    TEST_EXPECT(ctx, const_view.is_valid());
    TEST_EXPECT(ctx, const_view.data() == storage);
    TEST_EXPECT(ctx, const_view[2] == 99u);
    TEST_EXPECT(ctx, const_view.subview(2u, 2u).data() == (storage + 2u));

    CByteView byte_view{ reinterpret_cast<std::uint8_t*>(storage), sizeof(storage), 16u };
    TPodView<std::uint32_t> adopted{ byte_view };
    TEST_EXPECT(ctx, adopted.is_valid());
    TEST_EXPECT(ctx, adopted.size() == 5u);
    TEST_EXPECT(ctx, adopted.data() == storage);

    CByteView partial_bytes{ reinterpret_cast<std::uint8_t*>(storage), sizeof(storage) - 1u, 16u };
    TEST_EXPECT(ctx, !TPodView<std::uint32_t>{ partial_bytes }.is_valid());
    TPodView<std::uint32_t> misaligned{
        reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(storage) + 1u), 1u };
    TEST_EXPECT(ctx, !misaligned.is_valid());
}

void test_move_and_reuse(TTestContext& ctx)
{
    TPodVector<std::uint32_t> source;
    const std::uint32_t values[]{ 4u, 5u, 6u };
    TEST_EXPECT(ctx, source.push_back(values, 3u));

    TPodVector<std::uint32_t> destination{ std::move(source) };
    TEST_EXPECT(ctx, destination.is_valid());
    TEST_EXPECT(ctx, destination.size() == 3u);
    TEST_EXPECT(ctx, destination[0] == 4u);
    TEST_EXPECT(ctx, destination[2] == 6u);
    TEST_EXPECT(ctx, source.is_valid());
    TEST_EXPECT(ctx, source.is_empty());
    TEST_EXPECT(ctx, source.push_back(9u));
    TEST_EXPECT(ctx, source.size() == 1u);
    TEST_EXPECT(ctx, source[0] == 9u);

    TPodVector<std::uint32_t> assigned;
    TEST_EXPECT(ctx, assigned.push_back(100u));
    assigned = std::move(destination);
    TEST_EXPECT(ctx, assigned.size() == 3u);
    TEST_EXPECT(ctx, assigned[1] == 5u);
    TEST_EXPECT(ctx, destination.is_valid());
    TEST_EXPECT(ctx, destination.push_back(12u));
    TEST_EXPECT(ctx, destination[0] == 12u);
}

}   //  namespace

int run_pod_vector_tests()
{
    TTestContext ctx;
    test_constness_and_limits(ctx);
    test_allocation_and_resizing(ctx);
    test_vector_operations(ctx);
    test_discard_operations(ctx);
    test_behavioral_consistency(ctx);
    test_views_and_slicing(ctx);
    test_move_and_reuse(ctx);

    return ctx.report("TPodVector");
}
