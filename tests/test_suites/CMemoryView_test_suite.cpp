
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CMemoryView_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    13 Jul 26

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <type_traits>
#include <utility>

#include "memory/memory_token.hpp"
#include "memory/memory_view.hpp"
#include "tests/test_suites/CMemoryView_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using memory::CMemoryConstView;
using memory::CMemoryToken;
using memory::CMemoryView;

using TTestContext = tests::TTestContext;

void* MV_STD_ABI_CALL test_allocate(
    void*,
    const std::size_t alignment,
    const std::size_t bytes) noexcept
{
    return ::operator new(bytes, std::align_val_t{ alignment }, std::nothrow);
}

bool MV_STD_ABI_CALL test_deallocate(
    void*,
    const std::size_t alignment,
    void* const ptr) noexcept
{
    ::operator delete(ptr, std::align_val_t{ alignment });
    return true;
}

void test_empty_and_invalid_shapes(TTestContext& ctx)
{
    alignas(64) std::uint8_t storage[64]{};

    const CMemoryView empty;
    TEST_EXPECT(ctx, !empty.is_valid());
    TEST_EXPECT(ctx, empty.is_empty());
    TEST_EXPECT(ctx, empty.data() == nullptr);
    TEST_EXPECT(ctx, empty.count() == 0u);
    TEST_EXPECT(ctx, empty.stride() == 0u);
    TEST_EXPECT(ctx, empty.bytes() == 0u);
    TEST_EXPECT(ctx, empty.storage_alignment() == 0u);
    TEST_EXPECT(ctx, empty.element_alignment() == 0u);

    TEST_EXPECT(ctx, (!CMemoryView{ storage, 0u, 1u, 16u }.is_valid()));
    TEST_EXPECT(ctx, (!CMemoryView{ storage, 1u, 0u, 16u }.is_valid()));
    TEST_EXPECT(ctx, (!CMemoryView{ nullptr, 1u, 1u, 16u }.is_valid()));
    TEST_EXPECT(ctx, (CMemoryView{ storage, memory::max_elements(2u), 2u, 16u }.is_valid()));
    TEST_EXPECT(ctx, (!CMemoryView{ storage, memory::max_elements(2u) + 1u, 2u, 16u }.is_valid()));
}

void test_byte_view_and_constness(TTestContext& ctx)
{
    alignas(64) std::uint8_t storage[64]{};
    CMemoryView view{ storage, 64u, 1u, 16u };

    TEST_EXPECT(ctx, view.is_valid());
    TEST_EXPECT(ctx, view.data() == storage);
    TEST_EXPECT(ctx, view.count() == 64u);
    TEST_EXPECT(ctx, view.stride() == 1u);
    TEST_EXPECT(ctx, view.bytes() == 64u);
    TEST_EXPECT(ctx, view.storage_alignment() == 16u);
    TEST_EXPECT(ctx, view.element_alignment() == 1u);
    TEST_EXPECT(ctx, view.index_ptr(7u) == (storage + 7u));
    TEST_EXPECT(ctx, view.index_ptr(64u) == nullptr);

    *static_cast<std::uint8_t*>(view.index_ptr(7u)) = 0x5au;
    TEST_EXPECT(ctx, storage[7] == 0x5au);

    const CMemoryConstView const_view = view.const_view();
    TEST_EXPECT(ctx, const_view.is_valid());
    TEST_EXPECT(ctx, const_view.data() == storage);
    TEST_EXPECT(ctx, const_view.index_ptr(7u) == (storage + 7u));
    TEST_EXPECT(ctx, const_view.storage_alignment() == 16u);
    TEST_EXPECT(ctx, const_view.element_alignment() == 1u);

    static_assert(std::is_same_v<decltype(std::declval<CMemoryView>().data()), void*>);
    static_assert(std::is_same_v<decltype(std::declval<CMemoryConstView>().data()), const void*>);
    static_assert(std::is_same_v<decltype(std::declval<const CMemoryToken&>().view()), CMemoryConstView>);
    static_assert(std::is_same_v<decltype(std::declval<CMemoryToken&>().view()), CMemoryView>);
}

void test_strided_slicing_and_alignment(TTestContext& ctx)
{
    alignas(64) std::uint8_t storage[128]{};
    const CMemoryView view{ storage, 10u, 6u, 16u };

    TEST_EXPECT(ctx, view.is_valid());
    TEST_EXPECT(ctx, view.bytes() == 60u);
    TEST_EXPECT(ctx, view.storage_alignment() == 16u);
    TEST_EXPECT(ctx, view.element_alignment() == 2u);
    TEST_EXPECT(ctx, view.index_ptr(3u) == (storage + 18u));

    const CMemoryView at_one = view.subview(1u);
    TEST_EXPECT(ctx, at_one.data() == (storage + 6u));
    TEST_EXPECT(ctx, at_one.count() == 9u);
    TEST_EXPECT(ctx, at_one.storage_alignment() == 2u);
    TEST_EXPECT(ctx, at_one.element_alignment() == 2u);

    const CMemoryView at_two = view.subview(2u, 3u);
    TEST_EXPECT(ctx, at_two.data() == (storage + 12u));
    TEST_EXPECT(ctx, at_two.count() == 3u);
    TEST_EXPECT(ctx, at_two.stride() == 6u);
    TEST_EXPECT(ctx, at_two.bytes() == 18u);
    TEST_EXPECT(ctx, at_two.storage_alignment() == 4u);
    TEST_EXPECT(ctx, at_two.element_alignment() == 2u);

    const CMemoryView at_four = view.subview(4u);
    TEST_EXPECT(ctx, at_four.storage_alignment() == 8u);
    TEST_EXPECT(ctx, at_four.element_alignment() == 2u);

    TEST_EXPECT(ctx, !view.subview(10u).is_valid());
    TEST_EXPECT(ctx, !view.subview(2u, 0u).is_valid());
    TEST_EXPECT(ctx, !view.subview(8u, 3u).is_valid());
    TEST_EXPECT(ctx, !view.contains_range(2u, 0u));
    TEST_EXPECT(ctx, view.contains_range(2u, 8u));
}

void test_intent_alignment_is_conservative(TTestContext& ctx)
{
    alignas(64) std::uint8_t storage[64]{};

    const CMemoryConstView declared_eight{ storage, 8u, 4u, 8u };
    TEST_EXPECT(ctx, declared_eight.storage_alignment() == 8u);
    TEST_EXPECT(ctx, declared_eight.element_alignment() == 4u);

    const CMemoryConstView reduced_by_address{ storage + 4u, 8u, 4u, 16u };
    TEST_EXPECT(ctx, reduced_by_address.storage_alignment() == 4u);
    TEST_EXPECT(ctx, reduced_by_address.element_alignment() == 4u);
}

void test_token_views(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &test_allocate, &test_deallocate };
    memory::CMemoryContext context{ allocator };

    {
        CMemoryToken token{ 6u, 16u, &context };
        TEST_EXPECT(ctx, token.is_configured());
        TEST_EXPECT(ctx, token.allocate(10u, true));

        CMemoryView view = token.view();
        TEST_EXPECT(ctx, view.is_valid());
        TEST_EXPECT(ctx, view.data() == token.data());
        TEST_EXPECT(ctx, view.count() == token.count());
        TEST_EXPECT(ctx, view.stride() == token.stride());
        TEST_EXPECT(ctx, view.storage_alignment() == token.storage_alignment());
        TEST_EXPECT(ctx, view.element_alignment() == token.element_alignment());

        const CMemoryToken& const_token = token;
        const CMemoryConstView const_view = const_token.view();
        TEST_EXPECT(ctx, const_view.is_valid());
        TEST_EXPECT(ctx, const_view.data() == const_token.data());
        TEST_EXPECT(ctx, const_token.const_view().is_valid());
    }

    {
        CMemoryToken token{ 6u, 16u, 4u, &context };
        TEST_EXPECT(ctx, token.is_stable());
        TEST_EXPECT(ctx, token.allocate(10u, true));
        TEST_EXPECT(ctx, !token.view().is_valid());

        const CMemoryToken& const_token = token;
        TEST_EXPECT(ctx, !const_token.view().is_valid());
    }

    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

}   //  namespace

int run_memory_view_tests()
{
    TTestContext ctx;
    test_empty_and_invalid_shapes(ctx);
    test_byte_view_and_constness(ctx);
    test_strided_slicing_and_alignment(ctx);
    test_intent_alignment_is_conservative(ctx);
    test_token_views(ctx);

    return ctx.report("CMemoryView");
}
