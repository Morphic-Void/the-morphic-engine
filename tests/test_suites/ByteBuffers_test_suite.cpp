
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ByteBuffers_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    14 Jul 26

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>

#include "containers/ByteBuffers.hpp"
#include "memory/memory_token.hpp"
#include "memory/memory_view.hpp"
#include "tests/test_suites/ByteBuffers_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using TTestContext = tests::TTestContext;

void test_byte_buffer_constness_and_limits(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<const CByteBuffer&>().data()), std::uint8_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const CByteBuffer&>().view()), CByteView>);
    static_assert(std::is_same_v<decltype(std::declval<const CByteView&>().data()), std::uint8_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const CByteConstView&>().data()), const std::uint8_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const CByteRectBuffer&>().view()), CByteRectView>);
    static_assert(std::is_same_v<decltype(std::declval<const CByteRectView&>().data()), std::uint8_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const CByteRectConstView&>().data()), const std::uint8_t*>);

    CByteBuffer buffer;
    TEST_EXPECT(ctx, buffer.is_valid());
    TEST_EXPECT(ctx, buffer.is_empty());
    TEST_EXPECT(ctx, !buffer.is_ready());
    TEST_EXPECT(ctx, !buffer.view().is_valid());
    TEST_EXPECT(ctx, !buffer.const_view().is_valid());
    TEST_EXPECT(ctx, !buffer.allocate(memory::k_byte_size_ceiling + 1u));
}

void test_byte_buffer_allocation_and_resize(TTestContext& ctx)
{
    CByteBuffer buffer;
    const std::uint8_t bytes[]{ 1u, 2u, 3u, 4u };

    TEST_EXPECT(ctx, buffer.allocate(8u, 16u));
    TEST_EXPECT(ctx, buffer.is_ready());
    TEST_EXPECT(ctx, buffer.size() == 0u);
    TEST_EXPECT(ctx, buffer.capacity() == 8u);
    TEST_EXPECT(ctx, buffer.align() == 16u);
    TEST_EXPECT(ctx, !buffer.view().is_valid());

    TEST_EXPECT(ctx, buffer.append(bytes, 4u));
    TEST_EXPECT(ctx, buffer.size() == 4u);
    TEST_EXPECT(ctx, std::memcmp(buffer.data(), bytes, 4u) == 0);

    TEST_EXPECT(ctx, buffer.reserve(20u));
    TEST_EXPECT(ctx, buffer.capacity() >= 20u);
    TEST_EXPECT(ctx, std::memcmp(buffer.data(), bytes, 4u) == 0);

    TEST_EXPECT(ctx, buffer.resize(6u));
    TEST_EXPECT(ctx, buffer.size() == 6u);
    TEST_EXPECT(ctx, buffer.data()[4] == 0u);
    TEST_EXPECT(ctx, buffer.data()[5] == 0u);

    TEST_EXPECT(ctx, buffer.set_size(2u));
    TEST_EXPECT(ctx, buffer.size() == 2u);
    TEST_EXPECT(ctx, buffer.shrink_to_fit());
    TEST_EXPECT(ctx, buffer.capacity() == 2u);
    TEST_EXPECT(ctx, buffer.size() == 2u);
    TEST_EXPECT(ctx, buffer.data()[0] == 1u);
    TEST_EXPECT(ctx, buffer.data()[1] == 2u);
}

void test_byte_buffer_reservation_failures(TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    fixture.reject_all = true;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CByteBuffer buffer;
        const std::uint8_t value = 0x5au;
        TEST_EXPECT(ctx, buffer.append(nullptr, 0u));
        TEST_EXPECT(ctx, buffer.append(std::size_t{ 0u }, false));
        TEST_EXPECT(ctx, !buffer.append(nullptr, 1u));
        TEST_EXPECT(ctx, buffer.is_valid() && !buffer.is_ready() && !buffer.size() && !buffer.capacity());
        TEST_EXPECT(ctx, fixture.attempts == 0u);
        TEST_EXPECT(ctx, !buffer.reserve(0u) && !buffer.ensure_free(0u));
        TEST_EXPECT(ctx, (fixture.attempts == 2u) && context.is_attribution_empty());
        TEST_EXPECT(ctx, !buffer.append(&value, 0u));
        TEST_EXPECT(ctx, (fixture.attempts == 3u) && context.is_attribution_empty());
        TEST_EXPECT(ctx, !buffer.reserve(1u) && context.is_attribution_empty());
        TEST_EXPECT(ctx, buffer.is_valid() && !buffer.is_ready());

        fixture.reject_all = false;
        TEST_EXPECT(ctx, buffer.append(&value, 0u));
        TEST_EXPECT(ctx, buffer.is_ready() && (buffer.size() == 0u) && (buffer.capacity() == 4096u));
        buffer.deallocate();
        TEST_EXPECT(ctx, buffer.reserve(0u));
        TEST_EXPECT(ctx, buffer.is_ready() && (buffer.size() == 0u) && (buffer.capacity() == 4096u));
        buffer.deallocate();
        TEST_EXPECT(ctx, buffer.ensure_free(0u, 64u));
        TEST_EXPECT(ctx, buffer.is_ready() && (buffer.size() == 0u) && (buffer.capacity() == 4096u) && (buffer.align() == 64u));
        buffer.deallocate();
        TEST_EXPECT(ctx, buffer.allocate(4096u, 16u) && buffer.set_size(16u));
        std::memset(buffer.data(), value, buffer.size());
        auto* const original = buffer.data();
        const auto attempts = fixture.attempts;
        fixture.reject_all = true;
        TEST_EXPECT(ctx, buffer.reserve(0u) && buffer.reserve(4096u));
        TEST_EXPECT(ctx, buffer.ensure_free(4080u));
        TEST_EXPECT(ctx, buffer.append(std::size_t{ 4080u }, true));
        TEST_EXPECT(ctx, buffer.append(&value, 0u) && buffer.ensure_free(0u));
        TEST_EXPECT(ctx, (fixture.attempts == attempts) && (buffer.data() == original) && (buffer.capacity() == 4096u));
        TEST_EXPECT(ctx, !buffer.reserve(4097u) && !buffer.ensure_free(1u));
        TEST_EXPECT(ctx, !buffer.append(&value, 1u) && !buffer.resize(4097u));
        TEST_EXPECT(ctx, !buffer.reserve(0u, 64u) && !buffer.ensure_free(0u, 64u));
        TEST_EXPECT(ctx, (fixture.attempts == (attempts + 6u)) && (buffer.data() == original));
        TEST_EXPECT(ctx, (buffer.size() == 4096u) && (buffer.capacity() == 4096u) && (buffer.align() == 16u));
        TEST_EXPECT(ctx, !buffer.reserve(memory::k_byte_size_ceiling + 1u));
        TEST_EXPECT(ctx, !buffer.ensure_free(memory::k_byte_size_ceiling));
        TEST_EXPECT(ctx, fixture.attempts == (attempts + 6u));
        bool preserved = true;
        for (std::size_t i = 0u; i < buffer.size(); ++i)
        {
            preserved = preserved && (buffer.data()[i] == ((i < 16u) ? value : 0u));
        }
        TEST_EXPECT(ctx, preserved && (context.get_live_allocation_count() == 1u));

        fixture.reject_all = false;
        TEST_EXPECT(ctx, buffer.reserve(0u, 64u));
        TEST_EXPECT(ctx, (buffer.size() == 4096u) && (buffer.capacity() == 4096u) && (buffer.align() == 64u));
        TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(buffer.data()) & 63u) == 0u);
        TEST_EXPECT(ctx, buffer.resize(16u));
        auto* const aligned = buffer.data();
        fixture.reject_all = true;
        TEST_EXPECT(ctx, !buffer.shrink_to_fit() && (buffer.data() == aligned) && (buffer.capacity() == 4096u));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, buffer.shrink_to_fit() && (buffer.capacity() == 16u) && (buffer.align() == 64u));
        auto* const small = buffer.data();
        const auto small_attempts = fixture.attempts;
        fixture.reject_all = true;
        TEST_EXPECT(ctx, buffer.append(nullptr, 0u) && buffer.append(std::size_t{ 0u }, false));
        TEST_EXPECT(ctx, fixture.attempts == small_attempts);
        TEST_EXPECT(ctx, !buffer.reserve(0u) && !buffer.reserve(16u));
        TEST_EXPECT(ctx, !buffer.append(&value, 0u));
        TEST_EXPECT(ctx, (fixture.attempts == (small_attempts + 3u)) && (buffer.data() == small));
        TEST_EXPECT(ctx, (buffer.size() == 16u) && (buffer.capacity() == 16u) && (buffer.align() == 64u));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, buffer.append(&value, 0u));
        TEST_EXPECT(ctx, (buffer.size() == 16u) && (buffer.capacity() == 4096u) && (buffer.align() == 64u));
        for (std::size_t i = 0u; i < buffer.size(); ++i)
        {
            TEST_EXPECT(ctx, buffer.data()[i] == value);
        }
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_byte_buffer_growth_and_bulk_reservation(TTestContext& ctx)
{
    constexpr std::size_t total = 1024u * 1024u;
    CByteBuffer source;
    if (!source.allocate(total) || !source.set_size(total))
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    for (std::size_t i = 0u; i < total; ++i) source.data()[i] = static_cast<std::uint8_t>(i);
    for (const std::size_t chunk : { 1u, 4096u, 65536u, 1048576u })
    {
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CByteBuffer buffer;
            std::uint64_t copied{};
            bool completed = true;
            for (std::size_t offset = 0u; offset < total; offset += chunk)
            {
                const auto attempts = fixture.attempts;
                if (!buffer.append((source.data() + offset), chunk))
                {
                    completed = false;
                    break;
                }
                //  Each backing allocation preserves the previous logical prefix.
                if (fixture.attempts != attempts) copied += offset;
            }
            TEST_EXPECT(ctx, completed && (buffer.size() == total));
            TEST_EXPECT(ctx, completed && (std::memcmp(buffer.data(), source.data(), total) == 0));
            TEST_EXPECT(ctx, fixture.attempts <= ((chunk == total) ? 1u : ((chunk == 65536u) ? 12u : 32u)));
            TEST_EXPECT(ctx, copied < (16u * total));
            std::cout << "Byte buffer growth: chunk " << chunk << ", " << fixture.attempts <<
                " allocations, " << copied << " preserved prefix bytes\n";
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    for (const bool exact : { false, true })
    {
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CByteBuffer buffer;
            TEST_EXPECT(ctx, exact ? buffer.allocate(total) : buffer.reserve(total));
            const auto attempts = fixture.attempts;
            auto* const original = buffer.data();
            fixture.reject_all = true;
            //  Fill in bulk, then reach the exact boundary with individual appends.
            TEST_EXPECT(ctx, buffer.append(source.data(), (total - 2u)));
            TEST_EXPECT(ctx, buffer.append((source.data() + total - 2u), 1u));
            TEST_EXPECT(ctx, buffer.append((source.data() + total - 1u), 1u));
            TEST_EXPECT(ctx, (fixture.attempts == attempts) && (buffer.data() == original) && (buffer.size() == total));
            TEST_EXPECT(ctx, (buffer.size() == total) && (std::memcmp(buffer.data(), source.data(), total) == 0));
            TEST_EXPECT(ctx, exact ? (buffer.capacity() == total) : (buffer.capacity() >= total));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

void test_byte_views_and_slicing(TTestContext& ctx)
{
    alignas(16) std::uint8_t storage[]{ 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u };

    CByteView empty_view;
    TEST_EXPECT(ctx, !empty_view.is_valid());
    TEST_EXPECT(ctx, empty_view.is_empty());
    TEST_EXPECT(ctx, (!CByteView{ storage, 0u, 16u }.is_valid()));
    TEST_EXPECT(ctx, (!CByteConstView{ storage, 0u, 16u }.is_valid()));

    CByteView view{ storage, 8u, 16u };
    TEST_EXPECT(ctx, view.is_valid());
    TEST_EXPECT(ctx, view.size() == 8u);
    TEST_EXPECT(ctx, view.align() == 16u);

    CByteView middle = view.subview(2u, 4u);
    TEST_EXPECT(ctx, middle.is_valid());
    TEST_EXPECT(ctx, middle.data() == (storage + 2u));
    TEST_EXPECT(ctx, middle.size() == 4u);
    TEST_EXPECT(ctx, middle.align() == 2u);

    middle.data()[1] = 99u;
    TEST_EXPECT(ctx, storage[3] == 99u);
    TEST_EXPECT(ctx, view.head_to(3u).size() == 3u);
    TEST_EXPECT(ctx, view.tail_from(5u).size() == 3u);
    TEST_EXPECT(ctx, !view.head_to(0u).is_valid());
    TEST_EXPECT(ctx, !view.subview(7u, 2u).is_valid());
    TEST_EXPECT(ctx, !view.tail_from(8u).is_valid());

    const CByteConstView const_view = view.const_view();
    TEST_EXPECT(ctx, const_view.is_valid());
    TEST_EXPECT(ctx, const_view.data() == storage);
    TEST_EXPECT(ctx, const_view.subview(1u, 3u).data() == (storage + 1u));

    memory::CMemoryView raw_view{ storage, 8u, 1u, 16u };
    CByteView adopted{ raw_view, MetaByteView{ 6u } };
    TEST_EXPECT(ctx, adopted.is_valid());
    TEST_EXPECT(ctx, adopted.size() == 6u);
    TEST_EXPECT(ctx, adopted.data() == storage);
    TEST_EXPECT(ctx, (!CByteView{ raw_view, MetaByteView{ 9u } }.is_valid()));
}

void test_byte_buffer_move_and_disown(TTestContext& ctx)
{
    CByteBuffer source;
    const std::uint8_t bytes[]{ 21u, 22u, 23u, 24u };

    TEST_EXPECT(ctx, source.reallocate(4u, 8u, 8u));
    TEST_EXPECT(ctx, std::memcpy(source.data(), bytes, 4u) == source.data());

    CByteBuffer moved{ std::move(source) };
    TEST_EXPECT(ctx, moved.is_ready());
    TEST_EXPECT(ctx, moved.size() == 4u);
    TEST_EXPECT(ctx, moved.capacity() == 8u);
    TEST_EXPECT(ctx, std::memcmp(moved.data(), bytes, 4u) == 0);
    TEST_EXPECT(ctx, source.is_valid());
    TEST_EXPECT(ctx, source.is_empty());
    TEST_EXPECT(ctx, source.allocate(2u));

    MetaByteBuffer meta;
    memory::CMemoryToken token = moved.disown(meta);
    TEST_EXPECT(ctx, meta.size == 4u);
    TEST_EXPECT(ctx, meta.capacity == 8u);
    TEST_EXPECT(ctx, token.is_relocatable());
    TEST_EXPECT(ctx, token.count() == 8u);
    TEST_EXPECT(ctx, token.storage_alignment() == 8u);
    TEST_EXPECT(ctx, std::memcmp(token.data(), bytes, 4u) == 0);
    TEST_EXPECT(ctx, moved.is_valid());
    TEST_EXPECT(ctx, moved.is_empty());
    TEST_EXPECT(ctx, moved.append(bytes, 2u));
    TEST_EXPECT(ctx, moved.size() == 2u);
}

void test_byte_rect_layout_and_contiguity(TTestContext& ctx)
{
    CByteRectBuffer rect;
    TEST_EXPECT(ctx, rect.is_valid());
    TEST_EXPECT(ctx, rect.is_empty());
    TEST_EXPECT(ctx, !rect.is_ready());

    TEST_EXPECT(ctx, rect.allocate(5u, 3u, 8u, false));
    TEST_EXPECT(ctx, rect.is_ready());
    TEST_EXPECT(ctx, rect.row_pitch() == 8u);
    TEST_EXPECT(ctx, rect.row_width() == 5u);
    TEST_EXPECT(ctx, rect.row_count() == 3u);
    TEST_EXPECT(ctx, rect.align() == 8u);
    TEST_EXPECT(ctx, !rect.is_contiguous());
    TEST_EXPECT(ctx, !rect.byte_view().is_valid());

    std::memset(rect.data(), 0xff, rect.row_pitch() * rect.row_count());
    rect.zero_fill();
    for (std::size_t y = 0u; y < rect.row_count(); ++y)
    {
        TEST_EXPECT(ctx, rect.row_data(y) == (rect.data() + (y * rect.row_pitch())));
        for (std::size_t x = 0u; x < rect.row_width(); ++x)
        {
            TEST_EXPECT(ctx, rect.row_data(y)[x] == 0u);
        }
        for (std::size_t x = rect.row_width(); x < rect.row_pitch(); ++x)
        {
            TEST_EXPECT(ctx, rect.row_data(y)[x] == 0xffu);
        }
    }

    CByteRectView sub = rect.view().subview(1u, 1u, 3u, 2u);
    TEST_EXPECT(ctx, sub.is_valid());
    TEST_EXPECT(ctx, sub.row_pitch() == 8u);
    TEST_EXPECT(ctx, sub.row_width() == 3u);
    TEST_EXPECT(ctx, sub.row_count() == 2u);
    TEST_EXPECT(ctx, sub.data() == (rect.row_data(1u) + 1u));
    TEST_EXPECT(ctx, !rect.view().subview(4u, 2u, 2u, 2u).is_valid());

    TEST_EXPECT(ctx, rect.reallocate(4u, 2u, 1u, true));
    TEST_EXPECT(ctx, rect.is_contiguous());
    TEST_EXPECT(ctx, rect.row_pitch() == 4u);
    TEST_EXPECT(ctx, rect.byte_view().is_valid());
    TEST_EXPECT(ctx, rect.byte_view().size() == 8u);
}

void test_byte_rect_copy_and_view_adoption(TTestContext& ctx)
{
    alignas(8) std::uint8_t storage[10u]{ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u };

    CByteRectConstView src{ storage, 5u, 5u, 2u, 8u };
    TEST_EXPECT(ctx, src.is_valid());
    TEST_EXPECT(ctx, src.is_contiguous());

    CByteRectBuffer copied;
    TEST_EXPECT(ctx, copied.construct_and_copy_from(src));
    TEST_EXPECT(ctx, copied.is_ready());
    TEST_EXPECT(ctx, copied.row_width() == 5u);
    TEST_EXPECT(ctx, copied.row_count() == 2u);
    TEST_EXPECT(ctx, copied.row_pitch() == 8u);
    TEST_EXPECT(ctx, copied.align() == 8u);
    TEST_EXPECT(ctx, !copied.is_contiguous());
    TEST_EXPECT(ctx, std::memcmp(copied.row_data(0u), storage, 5u) == 0);
    TEST_EXPECT(ctx, std::memcmp(copied.row_data(1u), storage + 5u, 5u) == 0);
    TEST_EXPECT(ctx, copied.row_data(0u)[5] == 0u);
    TEST_EXPECT(ctx, copied.row_data(0u)[6] == 0u);
    TEST_EXPECT(ctx, copied.row_data(0u)[7] == 0u);

    memory::CMemoryView raw_rect{ storage, 10u, 1u, 8u };
    CByteRectView adopted{ raw_rect, MetaByteRectView{ 5u, 5u, 2u } };
    TEST_EXPECT(ctx, adopted.is_valid());
    TEST_EXPECT(ctx, adopted.row_pitch() == 5u);
    TEST_EXPECT(ctx, adopted.row_width() == 5u);
    TEST_EXPECT(ctx, adopted.row_count() == 2u);
    TEST_EXPECT(ctx, adopted.is_contiguous());
}

}   //  namespace

int run_byte_buffer_tests()
{
    TTestContext ctx;
    test_byte_buffer_constness_and_limits(ctx);
    test_byte_buffer_allocation_and_resize(ctx);
    test_byte_buffer_reservation_failures(ctx);
    test_byte_buffer_growth_and_bulk_reservation(ctx);
    test_byte_views_and_slicing(ctx);
    test_byte_buffer_move_and_disown(ctx);
    test_byte_rect_layout_and_contiguity(ctx);
    test_byte_rect_copy_and_view_adoption(ctx);

    return ctx.report("ByteBuffers");
}

