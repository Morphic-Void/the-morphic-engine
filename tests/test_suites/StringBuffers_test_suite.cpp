
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   StringBuffers_test_suite.cpp
//  Primary implementation: OpenAI tools
//  Used, occasionally adjusted, and accepted by: Ritchie Brannan
//  Date:   14 Jul 26

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>
#include <type_traits>
#include <utility>

#include "containers/StringBuffers.hpp"
#include "tests/test_suites/StringBuffers_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

void* MV_STD_ABI_CALL attribution_test_allocate(
    void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    return ::operator new(bytes, std::align_val_t{ alignment }, std::nothrow);
}

bool MV_STD_ABI_CALL attribution_test_deallocate(
    void*, const std::size_t alignment, void* const ptr) noexcept
{
    ::operator delete(ptr, std::align_val_t{ alignment });
    return true;
}

void test_string_view_null_empty_and_terminators(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<const CStringView&>().string()), const std::uint8_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const CStringView&>().cstring()), const char*>);

    CStringView null_view;
    TEST_EXPECT(ctx, null_view.empty());
    TEST_EXPECT(ctx, null_view.length() == 0u);
    TEST_EXPECT(ctx, null_view.string() == nullptr);

    const std::uint8_t zero_only[]{ 0u };
    CStringView empty_present{ zero_only, 0u };
    TEST_EXPECT(ctx, !empty_present.empty());
    TEST_EXPECT(ctx, empty_present.length() == 0u);
    TEST_EXPECT(ctx, empty_present.string() == zero_only);
    TEST_EXPECT(ctx, empty_present.cstring()[0] == '\0');
    TEST_EXPECT(ctx, empty_present == null_view);

    const std::uint8_t scanned_empty[]{ 0u, 99u };
    CStringView implicit_empty{ scanned_empty };
    TEST_EXPECT(ctx, !implicit_empty.empty());
    TEST_EXPECT(ctx, implicit_empty.length() == 0u);
    TEST_EXPECT(ctx, implicit_empty.string() == scanned_empty);
    TEST_EXPECT(ctx, implicit_empty == empty_present);

    const std::uint8_t explicit_bytes[]{ 'a', '\0', 'b' };
    CStringView explicit_view{ explicit_bytes, 3u };
    TEST_EXPECT(ctx, explicit_view.length() == 3u);
    TEST_EXPECT(ctx, explicit_view.string()[1] == 0u);

    const std::uint8_t scanned_bytes[]{ 'a', 'b', 0u, 'x' };
    CStringView scanned_view{ scanned_bytes };
    TEST_EXPECT(ctx, scanned_view.length() == 2u);
    TEST_EXPECT(ctx, scanned_view.cstring()[2] == '\0');
    TEST_EXPECT(ctx, null_view < scanned_view);
    TEST_EXPECT(ctx, empty_present < scanned_view);
    TEST_EXPECT(ctx, explicit_view < scanned_view);
}

void test_simple_string_set_move_and_reset(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<const CSimpleString&>().string()), const std::uint8_t*>);
    static_assert(std::is_same_v<decltype(std::declval<const CSimpleString&>().view()), CStringView>);

    CSimpleString string;
    TEST_EXPECT(ctx, string.is_empty());
    TEST_EXPECT(ctx, string.length() == 0u);

    TEST_EXPECT(ctx, string.set(reinterpret_cast<const std::uint8_t*>("parser")));
    TEST_EXPECT(ctx, !string.is_empty());
    TEST_EXPECT(ctx, string.length() == 6u);
    TEST_EXPECT(ctx, std::memcmp(string.string(), "parser", 6u) == 0);
    TEST_EXPECT(ctx, string.cstring()[6] == '\0');

    const std::uint8_t explicit_bytes[]{ 'x', '\0', 'y' };
    TEST_EXPECT(ctx, string.set(explicit_bytes, 3u));
    TEST_EXPECT(ctx, string.length() == 3u);
    TEST_EXPECT(ctx, string.string()[1] == 0u);
    TEST_EXPECT(ctx, string.string()[3] == 0u);

    const std::uint8_t zero_only[]{ 0u };
    TEST_EXPECT(ctx, string.set(zero_only, 0u));
    TEST_EXPECT(ctx, !string.is_empty());
    TEST_EXPECT(ctx, string.length() == 0u);
    TEST_EXPECT(ctx, string.string() != nullptr);
    TEST_EXPECT(ctx, string.cstring()[0] == '\0');
    TEST_EXPECT(ctx, (string.view() == CStringView{ zero_only, 0u }));

    CSimpleString moved{ std::move(string) };
    TEST_EXPECT(ctx, !moved.is_empty());
    TEST_EXPECT(ctx, moved.length() == 0u);
    TEST_EXPECT(ctx, moved.cstring()[0] == '\0');
    TEST_EXPECT(ctx, string.is_empty());
    TEST_EXPECT(ctx, string.set(reinterpret_cast<const std::uint8_t*>("reuse")));
    TEST_EXPECT(ctx, string.length() == 5u);

    CSimpleString assigned;
    TEST_EXPECT(ctx, assigned.set(reinterpret_cast<const std::uint8_t*>("assigned")));
    assigned = std::move(moved);
    TEST_EXPECT(ctx, !assigned.is_empty());
    TEST_EXPECT(ctx, assigned.length() == 0u);
    TEST_EXPECT(ctx, assigned.cstring()[0] == '\0');
    TEST_EXPECT(ctx, moved.is_empty());

    assigned.deallocate();
    TEST_EXPECT(ctx, assigned.is_empty());
    TEST_EXPECT(ctx, assigned.length() == 0u);
    TEST_EXPECT(ctx, !assigned.set(static_cast<const std::uint8_t*>(nullptr)));
}

void test_string_buffer_offsets_and_storage(TTestContext& ctx)
{
    CStringBuffer buffer;
    TEST_EXPECT(ctx, buffer.empty());
    TEST_EXPECT(ctx, buffer.check_invariants());

    TEST_EXPECT(ctx, buffer.reserve(32u));
    const std::size_t first_offset = buffer.append(reinterpret_cast<const std::uint8_t*>("cat"));
    TEST_EXPECT(ctx, first_offset == 1u);
    TEST_EXPECT(ctx, buffer.is_valid_offset(first_offset));
    TEST_EXPECT(ctx, std::memcmp(buffer.string(first_offset), "cat", 3u) == 0);
    TEST_EXPECT(ctx, buffer.view(first_offset).length() == 3u);
    TEST_EXPECT(ctx, buffer.storage_overlaps(buffer.string(first_offset), 3u));
    TEST_EXPECT(ctx, buffer.storage_overlaps(buffer.string(first_offset) + 1u, 1u));
    TEST_EXPECT(ctx, !buffer.storage_overlaps(buffer.string(first_offset), 0u));
    TEST_EXPECT(ctx, !buffer.storage_overlaps(nullptr, 3u));
    TEST_EXPECT(ctx, !buffer.storage_overlaps(
        reinterpret_cast<const std::uint8_t*>("external"), 8u));

    const std::uint8_t embedded[]{ 'a', 0u, 'b' };
    const std::size_t embedded_offset = buffer.append(embedded, 3u);
    TEST_EXPECT(ctx, embedded_offset == 6u);
    TEST_EXPECT(ctx, buffer.is_valid_offset(embedded_offset));
    TEST_EXPECT(ctx, buffer.string(embedded_offset)[0] == 'a');
    TEST_EXPECT(ctx, buffer.string(embedded_offset)[1] == 0u);
    TEST_EXPECT(ctx, buffer.string(embedded_offset)[2] == 'b');
    TEST_EXPECT(ctx, buffer.string(embedded_offset)[3] == 0u);
    TEST_EXPECT(ctx, buffer.view(embedded_offset).length() == 1u);

    const std::uint8_t zero_only[]{ 0u };
    const std::size_t empty_offset = buffer.append(zero_only, 0u);
    TEST_EXPECT(ctx, buffer.is_valid_offset(empty_offset));
    TEST_EXPECT(ctx, !buffer.view(empty_offset).empty());
    TEST_EXPECT(ctx, buffer.view(empty_offset).length() == 0u);
    TEST_EXPECT(ctx, buffer.string(empty_offset)[0] == 0u);

    const std::size_t size_before_reserve_exact = buffer.size();
    TEST_EXPECT(ctx, !buffer.reserve_exact(size_before_reserve_exact - 1u));
    TEST_EXPECT(ctx, buffer.reserve_exact(size_before_reserve_exact));
    TEST_EXPECT(ctx, buffer.capacity() == size_before_reserve_exact);
    TEST_EXPECT(ctx, buffer.shrink_to_fit());
    TEST_EXPECT(ctx, buffer.capacity() == buffer.size());
    TEST_EXPECT(ctx, buffer.check_invariants());
    TEST_EXPECT(ctx, !buffer.is_valid_offset(0u));
    TEST_EXPECT(ctx, buffer.string(buffer.size()) == nullptr);
}

void test_stable_strings_lookup_duplicates_and_sort(TTestContext& ctx)
{
    CStableStrings table;
    TEST_EXPECT(ctx, table.initialise(8u, 32u));
    TEST_EXPECT(ctx, table.check_integrity());

    const std::uint8_t zero_only[]{ 0u };
    const std::size_t pear_id = table.append(reinterpret_cast<const std::uint8_t*>("pear"));
    const std::size_t empty_id = table.append(zero_only, 0u);
    const std::size_t apple_id = table.append(reinterpret_cast<const std::uint8_t*>("apple"));
    const std::size_t banana_id = table.append(reinterpret_cast<const std::uint8_t*>("banana"));
    const std::size_t apple_dup_id = table.append(reinterpret_cast<const std::uint8_t*>("apple"));

    TEST_EXPECT(ctx, pear_id != CStableStrings::k_invalid_id);
    TEST_EXPECT(ctx, empty_id != CStableStrings::k_invalid_id);
    TEST_EXPECT(ctx, apple_id != CStableStrings::k_invalid_id);
    TEST_EXPECT(ctx, banana_id != CStableStrings::k_invalid_id);
    TEST_EXPECT(ctx, apple_dup_id == apple_id);

    TEST_EXPECT(ctx, table.find_id(reinterpret_cast<const std::uint8_t*>("pear")) == pear_id);
    TEST_EXPECT(ctx, table.find_id(zero_only, 0u) == empty_id);
    TEST_EXPECT(ctx, table.find_id(reinterpret_cast<const std::uint8_t*>("apple")) == apple_id);
    TEST_EXPECT(ctx, table.find_id(reinterpret_cast<const std::uint8_t*>("missing")) == CStableStrings::k_invalid_id);

    TEST_EXPECT(ctx, table.view(empty_id).length() == 0u);
    TEST_EXPECT(ctx, !table.view(empty_id).empty());
    TEST_EXPECT(ctx, table.view(pear_id).length() == 4u);
    TEST_EXPECT(ctx, std::memcmp(table.view(apple_id).string(), "apple", 5u) == 0);
    TEST_EXPECT(ctx, table.storage_overlaps(table.view(pear_id).string(), 4u));
    TEST_EXPECT(ctx, table.storage_overlaps(table.view(apple_id).string() + 2u, 2u));
    TEST_EXPECT(ctx, !table.storage_overlaps(table.view(pear_id).string(), 0u));
    TEST_EXPECT(ctx, !table.storage_overlaps(nullptr, 4u));
    TEST_EXPECT(ctx, !table.storage_overlaps(
        reinterpret_cast<const std::uint8_t*>("external"), 8u));

    const std::size_t pear_ref_before = table.id_to_ref_index(pear_id);
    const std::size_t empty_ref_before = table.id_to_ref_index(empty_id);
    TEST_EXPECT(ctx, pear_ref_before != CStableStrings::k_invalid_ref_index);
    TEST_EXPECT(ctx, empty_ref_before != CStableStrings::k_invalid_ref_index);
    TEST_EXPECT(ctx, table.ref_index_to_id(pear_ref_before) == pear_id);
    TEST_EXPECT(ctx, table.ref_index_to_rank(empty_ref_before) == 1u);

    TEST_EXPECT(ctx, table.sort());
    TEST_EXPECT(ctx, table.check_integrity());
    TEST_EXPECT(ctx, table.find_id(reinterpret_cast<const std::uint8_t*>("pear")) == pear_id);
    TEST_EXPECT(ctx, table.id_to_ref_index(empty_id) == 1u);
    TEST_EXPECT(ctx, table.id_to_ref_index(apple_id) == 2u);
    TEST_EXPECT(ctx, table.id_to_ref_index(banana_id) == 3u);
    TEST_EXPECT(ctx, table.id_to_ref_index(pear_id) == 4u);
    TEST_EXPECT(ctx, table.rank_to_ref_index(1u) == 1u);
    TEST_EXPECT(ctx, table.rank_to_ref_index(4u) == 4u);
    TEST_EXPECT(ctx, table.ref_index_to_rank(4u) == 4u);
    TEST_EXPECT(ctx, table.ref_index_to_id(4u) == pear_id);
}

void test_stable_strings_reserve_shrink_and_invariants(TTestContext& ctx)
{
    CStableStrings table;
    TEST_EXPECT(ctx, table.append(reinterpret_cast<const std::uint8_t*>("gamma")) == 1u);
    TEST_EXPECT(ctx, table.append(reinterpret_cast<const std::uint8_t*>("alpha")) == 2u);
    TEST_EXPECT(ctx, table.append(reinterpret_cast<const std::uint8_t*>("beta")) == 3u);
    TEST_EXPECT(ctx, table.check_integrity());

    TEST_EXPECT(ctx, table.ensure_free(12u));
    TEST_EXPECT(ctx, table.check_integrity());
    TEST_EXPECT(ctx, table.shrink_to_fit());
    TEST_EXPECT(ctx, table.check_integrity());
    TEST_EXPECT(ctx, table.sort());
    TEST_EXPECT(ctx, table.check_integrity());
    TEST_EXPECT(ctx, table.view(table.find_id(reinterpret_cast<const std::uint8_t*>("alpha"))) <
        table.view(table.find_id(reinterpret_cast<const std::uint8_t*>("beta"))));

    table.deallocate();
    TEST_EXPECT(ctx, table.view(1u).empty());
    TEST_EXPECT(ctx, !table.is_valid_id(1u));
}

void test_stable_strings_direct_storage_reattribution(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &attribution_test_allocate, &attribution_test_deallocate };
    memory::CMemoryAllocator incompatible_allocator{ nullptr, &attribution_test_allocate, &attribution_test_deallocate };
    memory::CMemoryContext source_context{ allocator };
    memory::CMemoryContext target_context{ allocator };
    memory::CMemoryContext incompatible_context{ incompatible_allocator };
    memory::CMemoryContext* const previous_context = memory::set_thread_memory_context(&source_context);

    CStableStrings table;
    TEST_EXPECT(ctx, table.initialise(8u, 64u));
    const std::size_t pear_id = table.append(reinterpret_cast<const std::uint8_t*>("pear"));
    const std::size_t apple_id = table.append(reinterpret_cast<const std::uint8_t*>("apple"));
    TEST_EXPECT(ctx, pear_id != CStableStrings::k_invalid_id);
    TEST_EXPECT(ctx, apple_id != CStableStrings::k_invalid_id);
    TEST_EXPECT(ctx, table.sort());
    TEST_EXPECT(ctx, table.check_integrity());

    const std::uint8_t* const pear_address = table.view(pear_id).string();
    const std::uint32_t allocation_count = source_context.get_live_allocation_count();
    const std::uint64_t allocation_size = source_context.get_live_allocated_bytes();
    TEST_EXPECT(ctx, table.memory_token_count() == 5u);
    TEST_EXPECT(ctx, table.memory_allocation_count() == allocation_count);
    TEST_EXPECT(ctx, table.memory_allocation_size() == allocation_size);
    TEST_EXPECT(ctx, table.can_reattribute_to(&target_context));
    TEST_EXPECT(ctx, !table.can_reattribute_to(&incompatible_context));

    TEST_EXPECT(ctx, !table.reattribute(&incompatible_context));
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == allocation_count);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == allocation_size);
    TEST_EXPECT(ctx, incompatible_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, incompatible_context.get_live_allocated_bytes() == 0u);

    TEST_EXPECT(ctx, table.reattribute(&target_context));
    TEST_EXPECT(ctx, table.view(pear_id).string() == pear_address);
    TEST_EXPECT(ctx, table.find_id(reinterpret_cast<const std::uint8_t*>("apple")) == apple_id);
    TEST_EXPECT(ctx, table.check_integrity());
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == allocation_count);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == allocation_size);

    table.deallocate();
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == 0u);
    (void)memory::set_thread_memory_context(previous_context);
}

}   //  namespace

int run_string_buffer_tests()
{
    TTestContext ctx;
    test_string_view_null_empty_and_terminators(ctx);
    test_simple_string_set_move_and_reset(ctx);
    test_string_buffer_offsets_and_storage(ctx);
    test_stable_strings_lookup_duplicates_and_sort(ctx);
    test_stable_strings_reserve_shrink_and_invariants(ctx);
    test_stable_strings_direct_storage_reattribution(ctx);

    std::cout << "StringBuffers: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
