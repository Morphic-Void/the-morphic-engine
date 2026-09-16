
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   CMemoryToken_test_suite.cpp
//  Author: Ritchie Brannan
//  Date:   13 Jul 26

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <cstdio>
#include <cstring>
#include <string>

#include "containers/TInstance.hpp"
#include "data_model/live_document.hpp"
#include "memory/memory_token.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/path/native_path.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/file_helpers.hpp"
#include "tests/support/memory_context_test_access.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/CMemoryToken_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using memory::CMemoryToken;

constexpr std::size_t k_stable_stride = sizeof(std::uint32_t);
constexpr std::size_t k_stable_alignment = alignof(std::uint32_t);
constexpr std::size_t k_buffer_capacity_hint = 3u;
constexpr std::size_t k_buffer_capacity = 4u;

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

std::size_t round_up_to_pow2(std::size_t value) noexcept
{
    std::size_t result = 1u;
    while (result < value)
    {
        result <<= 1u;
    }
    return result;
}

std::size_t stable_buffer_count(const std::size_t count, const std::size_t per_buffer_capacity) noexcept
{
    return (count == 0u) ? 0u : (1u + ((count - 1u) / per_buffer_capacity));
}

std::size_t stable_directory_capacity(const std::size_t buffer_count) noexcept
{
    if (buffer_count <= 1u)
    {
        return 0u;
    }
    return (buffer_count <= CMemoryToken::k_min_directory_capacity) ?
        CMemoryToken::k_min_directory_capacity :
        round_up_to_pow2(buffer_count);
}

std::uint32_t expected_stable_allocation_count(
    const std::size_t count,
    const std::size_t per_buffer_capacity) noexcept
{
    const std::size_t buffer_count = stable_buffer_count(count, per_buffer_capacity);
    return static_cast<std::uint32_t>(buffer_count + ((buffer_count > 1u) ? 1u : 0u));
}

std::uint64_t expected_stable_allocated_bytes(
    const memory::CMemoryContext& context,
    const std::size_t stride,
    const std::size_t storage_alignment,
    const std::size_t per_buffer_capacity,
    const std::size_t count) noexcept
{
    const std::size_t buffer_count = stable_buffer_count(count, per_buffer_capacity);
    if (buffer_count == 0u)
    {
        return 0u;
    }

    const std::size_t conditioned_buffer_alignment = context.condition_alignment(storage_alignment);
    const std::size_t conditioned_buffer_bytes = context.condition_bytes(
        conditioned_buffer_alignment, per_buffer_capacity * stride);
    std::uint64_t total = static_cast<std::uint64_t>(buffer_count) * conditioned_buffer_bytes;
    if (buffer_count > 1u)
    {
        const std::size_t conditioned_directory_alignment = context.condition_alignment(alignof(void*));
        total += context.condition_bytes(
            conditioned_directory_alignment,
            stable_directory_capacity(buffer_count) * sizeof(void*));
    }
    return total;
}

void write_value(CMemoryToken& token, const std::size_t index, const std::uint32_t value)
{
    *static_cast<std::uint32_t*>(token.index_ptr(index)) = value;
}

std::uint32_t read_value(const CMemoryToken& token, const std::size_t index)
{
    return *static_cast<const std::uint32_t*>(token.index_ptr(index));
}

void test_stable_map_index_growth_and_slack(TTestContext& ctx, memory::CMemoryContext& context)
{
    CMemoryToken token{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &context };
    TEST_EXPECT(ctx, token.is_stable());
    TEST_EXPECT(ctx, token.per_buffer_capacity() == k_buffer_capacity);
    TEST_EXPECT(ctx, token.storage_alignment() == k_stable_alignment);
    TEST_EXPECT(ctx, token.count() == 0u);
    TEST_EXPECT(ctx, token.bytes() == 0u);

    std::array<void*, 9u> addresses{};
    std::array<std::uint32_t, 9u> values{};

    for (std::size_t index = 0u; index < addresses.size(); ++index)
    {
        addresses[index] = token.map_index(index, false);
        values[index] = static_cast<std::uint32_t>(0x100u + index);
        TEST_EXPECT(ctx, addresses[index] != nullptr);
        TEST_EXPECT(ctx, token.count() == (index + 1u));
        write_value(token, index, values[index]);

        for (std::size_t preserved = 0u; preserved <= index; ++preserved)
        {
            TEST_EXPECT(ctx, token.index_ptr(preserved) == addresses[preserved]);
            TEST_EXPECT(ctx, read_value(token, preserved) == values[preserved]);
        }
    }

    TEST_EXPECT(ctx, addresses[0] == token.index_ptr(0u));
    TEST_EXPECT(ctx, addresses[3] == token.index_ptr(3u));
    TEST_EXPECT(ctx, addresses[4] == token.index_ptr(4u));
    TEST_EXPECT(ctx, addresses[8] == token.index_ptr(8u));
    TEST_EXPECT(ctx, token.count() == 9u);
    TEST_EXPECT(ctx, token.bytes() == (9u * k_stable_stride));
    TEST_EXPECT(ctx, token.bytes() < expected_stable_allocated_bytes(
        context, token.stride(), token.storage_alignment(), token.per_buffer_capacity(), token.count()));
    TEST_EXPECT(ctx, token.contains_index(8u));
    TEST_EXPECT(ctx, !token.contains_index(9u));
    TEST_EXPECT(ctx, token.index_ptr(9u) == nullptr);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == expected_stable_allocation_count(9u, k_buffer_capacity));
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == expected_stable_allocated_bytes(
        context, token.stride(), token.storage_alignment(), token.per_buffer_capacity(), 9u));

    token.deallocate();
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

void test_stable_requested_count_and_internal_slack(TTestContext& ctx, memory::CMemoryContext& context)
{
    CMemoryToken token{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &context };

    TEST_EXPECT(ctx, token.allocate(5u, true));
    TEST_EXPECT(ctx, token.count() == 5u);
    TEST_EXPECT(ctx, token.bytes() == (5u * k_stable_stride));
    TEST_EXPECT(ctx, token.per_buffer_capacity() == k_buffer_capacity);
    TEST_EXPECT(ctx, token.index_ptr(4u) != nullptr);
    TEST_EXPECT(ctx, token.index_ptr(5u) == nullptr);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == expected_stable_allocation_count(5u, k_buffer_capacity));
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == expected_stable_allocated_bytes(
        context, token.stride(), token.storage_alignment(), token.per_buffer_capacity(), 5u));
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() > token.bytes());

    token.deallocate();
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

void test_stable_bounds_and_max_count(TTestContext& ctx, memory::CMemoryContext& context)
{
    CMemoryToken token{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &context };
    const std::size_t max_count = token.max_count();

    TEST_EXPECT(ctx, max_count == memory::max_elements(k_stable_stride));
    TEST_EXPECT(ctx, max_count == (memory::k_byte_size_ceiling / k_stable_stride));
    TEST_EXPECT(ctx, token.can_grow_to(0u));
    TEST_EXPECT(ctx, token.can_grow_to(max_count));
    TEST_EXPECT(ctx, !token.can_grow_to(max_count + 1u));
    TEST_EXPECT(ctx, token.index_ptr(0u) == nullptr);
    TEST_EXPECT(ctx, token.map_index(max_count, true) == nullptr);
    TEST_EXPECT(ctx, !token.allocate(max_count + 1u, true));
    TEST_EXPECT(ctx, !token.grow_to(max_count + 1u, true));
    TEST_EXPECT(ctx, token.count() == 0u);
    TEST_EXPECT(ctx, !token.owns_storage());
}

void test_growth_policy_ceiling(TTestContext& ctx)
{
    TEST_EXPECT(ctx, memory::vector_growth_policy(1u, 1u) == 1u);
    TEST_EXPECT(ctx, memory::vector_growth_policy(1u, 100u) == 32u);
    TEST_EXPECT(ctx, memory::vector_growth_policy(100u, 100u) == 100u);
    TEST_EXPECT(ctx, memory::buffer_growth_policy(1u, 2048u) == 2048u);
    TEST_EXPECT(ctx, memory::default_growth_policy(1u, 16u) == 16u);
}

void test_stable_clone_preserves_content_and_configuration(TTestContext& ctx, memory::CMemoryContext& context)
{
    CMemoryToken source{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &context };
    TEST_EXPECT(ctx, source.allocate(9u, false));
    for (std::size_t index = 0u; index < source.count(); ++index)
    {
        write_value(source, index, static_cast<std::uint32_t>(0x200u + index));
    }

    CMemoryToken clone;
    TEST_EXPECT(ctx, clone.clone(source));
    TEST_EXPECT(ctx, clone.is_stable());
    TEST_EXPECT(ctx, clone.context() == &context);
    TEST_EXPECT(ctx, clone.count() == source.count());
    TEST_EXPECT(ctx, clone.stride() == source.stride());
    TEST_EXPECT(ctx, clone.storage_alignment() == source.storage_alignment());
    TEST_EXPECT(ctx, clone.per_buffer_capacity() == source.per_buffer_capacity());
    TEST_EXPECT(ctx, clone.owns_storage());
    TEST_EXPECT(ctx, context.get_live_allocation_count() ==
        (2u * expected_stable_allocation_count(source.count(), source.per_buffer_capacity())));
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() ==
        (2u * expected_stable_allocated_bytes(
            context, source.stride(), source.storage_alignment(), source.per_buffer_capacity(), source.count())));

    for (std::size_t index = 0u; index < source.count(); ++index)
    {
        TEST_EXPECT(ctx, clone.index_ptr(index) != nullptr);
        TEST_EXPECT(ctx, clone.index_ptr(index) != source.index_ptr(index));
        TEST_EXPECT(ctx, read_value(clone, index) == read_value(source, index));
    }

    write_value(clone, 4u, 0xfeedu);
    TEST_EXPECT(ctx, read_value(clone, 4u) == 0xfeedu);
    TEST_EXPECT(ctx, read_value(source, 4u) == 0x204u);

    clone.deallocate();
    source.deallocate();
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

void test_stable_clone_to_compatible_alternate_context(
    TTestContext& ctx,
    memory::CMemoryContext& source_context,
    memory::CMemoryContext& alternate_context)
{
    CMemoryToken source{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &source_context };
    TEST_EXPECT(ctx, source.allocate(5u, false));
    for (std::size_t index = 0u; index < source.count(); ++index)
    {
        write_value(source, index, static_cast<std::uint32_t>(0x300u + index));
    }

    const std::uint32_t expected_allocations =
        expected_stable_allocation_count(source.count(), source.per_buffer_capacity());
    const std::uint64_t expected_bytes = expected_stable_allocated_bytes(
        source_context, source.stride(), source.storage_alignment(), source.per_buffer_capacity(), source.count());

    CMemoryToken clone;
    TEST_EXPECT(ctx, clone.clone(source, &alternate_context));
    TEST_EXPECT(ctx, clone.is_stable());
    TEST_EXPECT(ctx, clone.context() == &alternate_context);
    TEST_EXPECT(ctx, clone.count() == source.count());
    TEST_EXPECT(ctx, clone.per_buffer_capacity() == source.per_buffer_capacity());
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == expected_allocations);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == expected_bytes);
    TEST_EXPECT(ctx, alternate_context.get_live_allocation_count() == expected_allocations);
    TEST_EXPECT(ctx, alternate_context.get_live_allocated_bytes() == expected_bytes);

    for (std::size_t index = 0u; index < source.count(); ++index)
    {
        TEST_EXPECT(ctx, clone.index_ptr(index) != source.index_ptr(index));
        TEST_EXPECT(ctx, read_value(clone, index) == read_value(source, index));
    }

    write_value(clone, 1u, 0xbeefu);
    TEST_EXPECT(ctx, read_value(source, 1u) == 0x301u);

    clone.deallocate();
    source.deallocate();
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, alternate_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, alternate_context.get_live_allocated_bytes() == 0u);
}

void test_stable_reattribute_between_compatible_contexts(
    TTestContext& ctx,
    memory::CMemoryContext& source_context,
    memory::CMemoryContext& target_context)
{
    CMemoryToken token{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &source_context };
    TEST_EXPECT(ctx, token.allocate(9u, true));

    const std::uint32_t expected_allocations =
        expected_stable_allocation_count(token.count(), token.per_buffer_capacity());
    const std::uint64_t expected_bytes = expected_stable_allocated_bytes(
        source_context, token.stride(), token.storage_alignment(), token.per_buffer_capacity(), token.count());

    TEST_EXPECT(ctx, token.can_reattribute_to(&target_context));
    TEST_EXPECT(ctx, token.reattribute(&target_context));
    TEST_EXPECT(ctx, token.context() == &target_context);
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == expected_allocations);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == expected_bytes);

    token.deallocate();
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == 0u);
}

void test_aggregate_accounting_and_context_replacement(
    TTestContext& ctx,
    memory::CMemoryContext& source_context,
    memory::CMemoryContext& target_context,
    memory::CMemoryContext& incompatible_context)
{
    CMemoryToken relocatable{ sizeof(std::uint64_t), alignof(std::uint64_t), &source_context };
    CMemoryToken stable{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &source_context };
    CMemoryToken empty{ 1u, 1u, &source_context };
    TEST_EXPECT(ctx, relocatable.allocate(7u, true));
    TEST_EXPECT(ctx, stable.allocate(9u, true));

    void* const relocatable_address = relocatable.data();
    void* const stable_address = stable.index_ptr(4u);
    const std::uint32_t expected_allocations = source_context.get_live_allocation_count();
    const std::uint64_t expected_bytes = source_context.get_live_allocated_bytes();

    TEST_EXPECT(ctx, relocatable.memory_token_count() == 1u);
    TEST_EXPECT(ctx, stable.memory_token_count() == 1u);
    TEST_EXPECT(ctx, empty.memory_token_count() == 1u);
    TEST_EXPECT(ctx, (relocatable.memory_allocation_count() + stable.memory_allocation_count()) == expected_allocations);
    TEST_EXPECT(ctx, (relocatable.memory_allocation_size() + stable.memory_allocation_size()) == expected_bytes);
    TEST_EXPECT(ctx, relocatable.can_reattribute_to(&target_context));
    TEST_EXPECT(ctx, stable.can_reattribute_to(&target_context));
    TEST_EXPECT(ctx, !relocatable.can_reattribute_to(&incompatible_context));
    TEST_EXPECT(ctx, !stable.can_reattribute_to(&incompatible_context));
    TEST_EXPECT(ctx, relocatable.context() == &source_context);
    TEST_EXPECT(ctx, stable.context() == &source_context);
    TEST_EXPECT(ctx, empty.context() == &source_context);
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == expected_allocations);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == expected_bytes);
    TEST_EXPECT(ctx, incompatible_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, incompatible_context.get_live_allocated_bytes() == 0u);

    TEST_EXPECT(ctx, memory::reattribute(source_context, target_context, expected_allocations, expected_bytes));
    relocatable.unsafe_replace_context_without_accounting(&source_context, &target_context);
    stable.unsafe_replace_context_without_accounting(&source_context, &target_context);
    empty.unsafe_replace_context_without_accounting(&source_context, &target_context);
    TEST_EXPECT(ctx, relocatable.context() == &target_context);
    TEST_EXPECT(ctx, stable.context() == &target_context);
    TEST_EXPECT(ctx, empty.context() == &target_context);
    TEST_EXPECT(ctx, relocatable.data() == relocatable_address);
    TEST_EXPECT(ctx, stable.index_ptr(4u) == stable_address);
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == expected_allocations);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == expected_bytes);

    relocatable.deallocate();
    stable.deallocate();
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == 0u);
}

void test_stable_deallocate_preserves_configuration_and_reuse(TTestContext& ctx, memory::CMemoryContext& context)
{
    CMemoryToken token{ k_stable_stride, k_stable_alignment, k_buffer_capacity_hint, &context };
    TEST_EXPECT(ctx, token.allocate(6u, true));

    token.deallocate();
    TEST_EXPECT(ctx, token.is_stable());
    TEST_EXPECT(ctx, token.context() == &context);
    TEST_EXPECT(ctx, token.stride() == k_stable_stride);
    TEST_EXPECT(ctx, token.storage_alignment() == k_stable_alignment);
    TEST_EXPECT(ctx, token.per_buffer_capacity() == k_buffer_capacity);
    TEST_EXPECT(ctx, token.is_empty());
    TEST_EXPECT(ctx, !token.owns_storage());
    TEST_EXPECT(ctx, token.bytes() == 0u);
    TEST_EXPECT(ctx, token.index_ptr(0u) == nullptr);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);

    TEST_EXPECT(ctx, token.map_index(2u, true) != nullptr);
    TEST_EXPECT(ctx, token.count() == 3u);
    TEST_EXPECT(ctx, token.owns_storage());
    TEST_EXPECT(ctx, context.get_live_allocation_count() == expected_stable_allocation_count(3u, token.per_buffer_capacity()));
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == expected_stable_allocated_bytes(
        context, token.stride(), token.storage_alignment(), token.per_buffer_capacity(), 3u));

    token.deallocate();
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

}   //  namespace

namespace accounting_tests
{

using Access = memory::SMemoryContextTestAccess;
using TTestContext = tests::TTestContext;
constexpr std::uint32_t k_count_high = 0x80000000u;
constexpr std::uint64_t k_bytes_high = 0x8000000000000000ull;

struct SAllocator
{
    unsigned allocations{};
    unsigned deallocations{};
    unsigned outstanding{};
    bool reject_allocate{};
    bool reject_deallocate{};
    bool misalign{};
    void* retained{};

    static void* MV_STD_ABI_CALL allocate(void* const data, const std::size_t alignment, const std::size_t bytes) noexcept
    {
        auto& self = *static_cast<SAllocator*>(data);
        ++self.allocations;
        if (self.reject_allocate)
        {
            return nullptr;
        }
        void* const base = ::operator new(bytes + alignment, std::align_val_t{ alignment }, std::nothrow);
        if (base != nullptr)
        {
            ++self.outstanding;
        }
        if (self.misalign && (base != nullptr))
        {
            self.retained = base;
            return static_cast<char*>(base) + 1u;
        }
        return base;
    }

    static bool MV_STD_ABI_CALL deallocate(void* const data, const std::size_t alignment, void* const pointer) noexcept
    {
        auto& self = *static_cast<SAllocator*>(data);
        ++self.deallocations;
        if (self.reject_deallocate)
        {
            return false;
        }
        ::operator delete(self.misalign ? self.retained : pointer, std::align_val_t{ alignment });
        self.retained = nullptr;
        --self.outstanding;
        return true;
    }
};

void test_boundaries(TTestContext& ctx, memory::CMemoryContext& context, debug_system::CDebugServiceState& service)
{
    struct SCase
    {
        std::uint32_t count_before;
        std::uint64_t bytes_before;
        std::uint32_t count_delta;
        std::uint64_t bytes_delta;
        bool subtract;
        std::uint32_t count_after;
        std::uint64_t bytes_after;
        std::uint32_t reports;
    };
    constexpr SCase cases[]{
        {0u, 0u, 1u, 1u, false, 1u, 1u, 0u},
        {1u, 1u, 1u, 1u, true, 0u, 0u, 0u},
        {k_count_high - 1u, 7u, 1u, 0u, false, k_count_high, 7u, 1u},
        {7u, k_bytes_high - 1u, 0u, 1u, false, 7u, k_bytes_high, 1u},
        {k_count_high, k_bytes_high, 1u, 1u, true, k_count_high - 1u, k_bytes_high - 1u, 2u},
        {k_count_high, k_bytes_high, 1u, 1u, false, k_count_high + 1u, k_bytes_high + 1u, 0u},
        {k_count_high + 1u, k_bytes_high + 1u, 1u, 1u, true, k_count_high, k_bytes_high, 0u},
        {0u, 0u, 1u, 1u, true, UINT32_MAX, UINT64_MAX, 2u},
        {UINT32_MAX, UINT64_MAX, 1u, 1u, false, 0u, 0u, 2u},
        {k_count_high, k_bytes_high, 0u, 0u, false, k_count_high, k_bytes_high, 0u},
        {k_count_high, k_bytes_high, k_count_high, k_bytes_high, false, 0u, 0u, 2u},
        {0u, 0u, k_count_high, k_bytes_high, true, k_count_high, k_bytes_high, 2u},
        //  These wrap while retaining the high bit: deliberately outside the heuristic.
        {k_count_high + 1u, k_bytes_high + 1u, UINT32_MAX, UINT64_MAX, false, k_count_high, k_bytes_high, 0u},
        {k_count_high, k_bytes_high, UINT32_MAX, UINT64_MAX, true, k_count_high + 1u, k_bytes_high + 1u, 0u}
    };
    for (const SCase& test : cases)
    {
        Access::seed(context, test.count_before, test.bytes_before);
        const std::uint32_t incident = service.allocate_incident_id();
        if (test.subtract)
        {
            Access::sub(context, test.count_delta, test.bytes_delta);
        }
        else
        {
            Access::add(context, test.count_delta, test.bytes_delta);
        }
        TEST_EXPECT(ctx, context.get_live_allocation_count() == test.count_after);
        TEST_EXPECT(ctx, context.get_live_allocated_bytes() == test.bytes_after);
        TEST_EXPECT(ctx, service.allocate_incident_id() == incident + test.reports + 1u);
        TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
    }
    Access::seed(context, 0u, 0u);
}

void test_checked_sums(TTestContext& ctx, memory::CMemoryContext& source,
    memory::CMemoryAllocator& allocator, debug_system::CDebugServiceState& service)
{
    struct SCase
    {
        std::uint32_t left_count;
        std::uint32_t right_count;
        std::uint32_t count;
        std::uint64_t left_bytes;
        std::uint64_t right_bytes;
        std::uint64_t bytes;
        std::uint32_t reports;
    };
    constexpr SCase cases[]{
        {0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {UINT32_MAX - 1u, 1u, UINT32_MAX, UINT64_MAX - 1u, 1u, UINT64_MAX, 0u},
        {UINT32_MAX, 1u, 0u, 0u, 16u, 16u, 1u},
        {0u, 1u, 1u, UINT64_MAX, 1u, 0u, 1u},
        {UINT32_MAX, 1u, 0u, UINT64_MAX, 1u, 0u, 2u},
        {UINT32_MAX, 3u, 2u, UINT64_MAX, 17u, 16u, 2u},
        {UINT32_MAX, UINT32_MAX, UINT32_MAX - 1u, UINT64_MAX, UINT64_MAX, UINT64_MAX - 1u, 2u}
    };
    memory::CMemoryContext target{allocator};
    for (const auto& test : cases)
    {
        const auto incident = service.allocate_incident_id();
        const auto count = memory::add_accounting_counts(test.left_count, test.right_count);
        const auto bytes = memory::add_accounting_bytes(test.left_bytes, test.right_bytes);
        TEST_EXPECT(ctx, count == test.count && bytes == test.bytes);
        TEST_EXPECT(ctx, service.allocate_incident_id() == incident + test.reports + 1u);
        //  Zero/modulo totals remain valid compatible adjustments after overflow.
        Access::seed(source, count, bytes);
        TEST_EXPECT(ctx, memory::reattribute(source, target, count, bytes));
        TEST_EXPECT(ctx, source.is_attribution_empty());
        TEST_EXPECT(ctx, target.get_live_allocation_count() == count);
        TEST_EXPECT(ctx, target.get_live_allocated_bytes() == bytes);
        TEST_EXPECT(ctx, memory::reattribute(target, source, count, bytes));
        TEST_EXPECT(ctx, target.is_attribution_empty());
        Access::seed(source, 0u, 0u);
        TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
    }
    const auto incident = service.allocate_incident_id();
    const auto child_count = memory::add_accounting_counts(UINT32_MAX, 3u);
    const auto child_bytes = memory::add_accounting_bytes(UINT64_MAX, 17u);
    TEST_EXPECT(ctx, memory::add_accounting_counts(1u, child_count) == 3u);
    TEST_EXPECT(ctx, memory::add_accounting_bytes(16u, child_bytes) == 32u);
    //  The parent sum does not wrap; the child's two losses were still reported.
    TEST_EXPECT(ctx, service.allocate_incident_id() == incident + 3u);
}

struct SObservedOwner
{
    memory::SMemoryAttribution attribution;
    mutable unsigned observations{};
    unsigned replacements{};
    memory::CMemoryContext* replaced_source{};
    memory::CMemoryContext* replaced_target{};

    memory::SMemoryAttribution memory_attribution() const noexcept
    {
        ++observations;
        return attribution;
    }

    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        ++replacements;
        replaced_source = source;
        replaced_target = target;
        if (attribution.source_state == memory::EMemorySourceState::coherent) attribution.source = target;
    }
};

void test_attribution_contract(TTestContext& ctx, memory::CMemoryContext& source,
    memory::CMemoryAllocator& allocator, debug_system::CDebugServiceState& service)
{
    using State = memory::EMemorySourceState;
    using Record = memory::SMemoryAttribution;
    memory::CMemoryContext target{allocator};
    memory::CMemoryAllocator other_allocator{nullptr, nullptr, nullptr};
    memory::CMemoryContext other{other_allocator};
    const Record records[]{
        {State::empty, nullptr, 1u, 0u, 0u},
        {State::coherent, &source, 2u, 2u, 32u},
        {State::coherent, &target, 3u, 3u, 48u},
        {State::mixed, nullptr, 4u, 4u, 64u}
    };
    const State expected[4][4]{
        {State::empty, State::coherent, State::coherent, State::mixed},
        {State::coherent, State::coherent, State::mixed, State::mixed},
        {State::coherent, State::mixed, State::coherent, State::mixed},
        {State::mixed, State::mixed, State::mixed, State::mixed}
    };
    for (unsigned left = 0u; left < 4u; ++left)
    {
        for (unsigned right = 0u; right < 4u; ++right)
        {
            const auto result = memory::combine_memory_attribution(records[left], records[right]);
            TEST_EXPECT(ctx, result.source_state == expected[left][right]);
            auto* const expected_source = (result.source_state == State::coherent)
                ? ((left == 0u) ? records[right].source : records[left].source) : nullptr;
            TEST_EXPECT(ctx, result.source == expected_source);
            TEST_EXPECT(ctx, result.token_count == records[left].token_count + records[right].token_count);
            TEST_EXPECT(ctx, result.allocation_count == records[left].allocation_count + records[right].allocation_count);
            TEST_EXPECT(ctx, result.allocation_size == records[left].allocation_size + records[right].allocation_size);
        }
    }

    const Record large{State::coherent, &source, 1u, UINT32_MAX, UINT64_MAX};
    const Record one{State::coherent, &source, 1u, 1u, 1u};
    const auto incident = service.allocate_incident_id();
    const auto child = memory::combine_memory_attribution(large, one);
    const auto parent = memory::combine_memory_attribution(records[1], child);
    TEST_EXPECT(ctx, child.source_state == State::coherent && child.source == &source);
    TEST_EXPECT(ctx, child.allocation_count == 0u && child.allocation_size == 0u);
    TEST_EXPECT(ctx, parent.allocation_count == 2u && parent.allocation_size == 32u);
    TEST_EXPECT(ctx, service.allocate_incident_id() == incident + 3u);
    const auto mixed = memory::combine_memory_attribution(child, records[2]);
    TEST_EXPECT(ctx, mixed.source_state == State::mixed && mixed.source == nullptr);
    TEST_EXPECT(ctx, mixed.allocation_count == 3u && mixed.allocation_size == 48u);

    SObservedOwner owner{records[1]};
    Access::seed(source, 2u, 32u);
    TEST_EXPECT(ctx, memory::can_reattribute_to(owner, &target));
    TEST_EXPECT(ctx, owner.observations == 1u && owner.replacements == 0u);
    TEST_EXPECT(ctx, source.get_live_allocation_count() == 2u && target.is_attribution_empty());
    //  An earlier successful query cannot substitute for the transfer's preflight.
    owner.attribution = mixed;
    TEST_EXPECT(ctx, !memory::reattribute(owner, &target));
    TEST_EXPECT(ctx, owner.observations == 2u && owner.replacements == 0u);
    TEST_EXPECT(ctx, source.get_live_allocation_count() == 2u && target.is_attribution_empty());
    owner.attribution = records[1];
    TEST_EXPECT(ctx, !memory::can_reattribute_to(owner, &other));
    TEST_EXPECT(ctx, !memory::reattribute(owner, &other));
    TEST_EXPECT(ctx, owner.observations == 4u && owner.replacements == 0u);
    TEST_EXPECT(ctx, memory::reattribute(owner, &target));
    TEST_EXPECT(ctx, owner.observations == 5u && owner.replacements == 1u);
    TEST_EXPECT(ctx, owner.replaced_source == &source && owner.replaced_target == &target);
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, target.get_live_allocation_count() == 2u && target.get_live_allocated_bytes() == 32u);
    TEST_EXPECT(ctx, memory::reattribute(owner, &target));
    TEST_EXPECT(ctx, owner.observations == 6u && owner.replacements == 2u);
    TEST_EXPECT(ctx, target.get_live_allocation_count() == 2u && target.get_live_allocated_bytes() == 32u);
    TEST_EXPECT(ctx, memory::reattribute(owner, &source));
    Access::seed(source, 0u, 0u);

    SObservedOwner wrapped{child};
    TEST_EXPECT(ctx, !memory::reattribute(wrapped, &other));
    TEST_EXPECT(ctx, wrapped.observations == 1u && wrapped.replacements == 0u);
    TEST_EXPECT(ctx, memory::reattribute(wrapped, &target));
    TEST_EXPECT(ctx, wrapped.observations == 2u && wrapped.replacements == 1u);
    TEST_EXPECT(ctx, wrapped.replaced_source == &source && wrapped.replaced_target == &target);
    SObservedOwner empty{records[0]};
    TEST_EXPECT(ctx, memory::reattribute(empty, &other));
    TEST_EXPECT(ctx, empty.observations == 1u && empty.replacements == 1u);
    TEST_EXPECT(ctx, empty.replaced_source == nullptr && empty.replaced_target == &other);
    {
        tests::TMemoryContextScope scope{nullptr};
        auto* const previous_module = memory::set_module_memory_context(nullptr);
        const bool can_transfer = memory::can_reattribute_to(empty);
        const bool transferred = memory::reattribute(empty);
        (void)memory::set_module_memory_context(previous_module);
        TEST_EXPECT(ctx, !can_transfer && !transferred);
        TEST_EXPECT(ctx, empty.observations == 1u && empty.replacements == 1u);
    }
    {
        tests::TMemoryContextScope scope{&target};
        TEST_EXPECT(ctx, memory::can_reattribute_to(empty));
        TEST_EXPECT(ctx, memory::reattribute(empty));
        TEST_EXPECT(ctx, empty.observations == 3u && empty.replacements == 2u);
        TEST_EXPECT(ctx, empty.replaced_target == &target);
    }
    TEST_EXPECT(ctx, source.is_attribution_empty() && target.is_attribution_empty() && other.is_attribution_empty());
    TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
}

void test_document_with_accounting_discrepancies(TTestContext& ctx,
    memory::CMemoryContext& source, memory::CMemoryAllocator& allocator,
    debug_system::CDebugServiceState& service)
{
    memory::CMemoryContext target{allocator};
    tests::TMemoryContextScope scope{&source};
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const auto key = document.create_string(CStringView{"payload"}, CStringView{"property"});
    TEST_EXPECT(ctx, key.is_valid());
    const auto* const pointer = document.string_value(key).string();
    const auto count = document.memory_attribution().allocation_count;
    const auto bytes = document.memory_attribution().allocation_size;
    Access::seed(source, 0u, 0u);
    Access::seed(target, 0u - count, 0ull - bytes);
    TEST_EXPECT(ctx, memory::can_reattribute_to(document, &target));
    TEST_EXPECT(ctx, memory::reattribute(document, &target));
    TEST_EXPECT(ctx, document.memory_attribution().source_state == memory::EMemorySourceState::coherent);
    TEST_EXPECT(ctx, document.memory_attribution().source == &target);
    TEST_EXPECT(ctx, document.string_value(key).string() == pointer);
    TEST_EXPECT(ctx, target.is_attribution_empty());
    TEST_EXPECT(ctx, source.get_live_allocation_count() == 0u - count);
    TEST_EXPECT(ctx, source.get_live_allocated_bytes() == 0ull - bytes);
    TEST_EXPECT(ctx, document.check_integrity());
    document.deallocate();
    TEST_EXPECT(ctx, target.get_live_allocation_count() == 0u - count);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == 0ull - bytes);
    TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
    //  Undo only the deliberately injected diagnostic offsets after real cleanup.
    Access::seed(source, 0u, 0u);
    Access::seed(target, 0u, 0u);
}

struct SConcurrentAdjustment
{
    memory::CMemoryContext& context;
    std::atomic<bool> start{false};
    bool subtract{};

    static std::uint32_t MV_STD_ABI_CALL execute(void* const data) noexcept
    {
        auto& state = *static_cast<SConcurrentAdjustment*>(data);
        while (!state.start.load(std::memory_order_acquire))
        {
            platform::threading::processor_relax();
        }
        for (unsigned i = 0u; i < 16u; ++i)
        {
            if (state.subtract)
            {
                Access::sub(state.context, 1u, 1u);
            }
            else
            {
                Access::add(state.context, 1u, 1u);
            }
        }
        return 0u;
    }
};

void test_concurrent_adjustments(TTestContext& ctx, memory::CMemoryContext& context, debug_system::CDebugServiceState& service)
{
    MV_WARNING("Accounting concurrent begin");
    Access::seed(context, k_count_high - 32u, k_bytes_high - 32u);
    for (const bool subtract : {false, true})
    {
        const std::uint32_t incident = service.allocate_incident_id();
        SConcurrentAdjustment state{context};
        state.subtract = subtract;
        std::array<platform::threading::CThread, 4u> workers;
        for (auto& worker : workers)
        {
            TEST_EXPECT(ctx, worker.create(&SConcurrentAdjustment::execute, &state));
        }
        state.start.store(true, std::memory_order_release);
        for (auto& worker : workers)
        {
            TEST_EXPECT(ctx, worker.join_and_close());
        }
        TEST_EXPECT(ctx, service.allocate_incident_id() == incident + 3u);
        TEST_EXPECT(ctx, context.get_live_allocation_count() == (subtract ? k_count_high - 32u : k_count_high + 32u));
        TEST_EXPECT(ctx, context.get_live_allocated_bytes() == (subtract ? k_bytes_high - 32u : k_bytes_high + 32u));
    }
    Access::seed(context, 0u, 0u);
    MV_WARNING("Accounting concurrent end");
}

void test_real_operations(TTestContext& ctx, memory::CMemoryContext& context, SAllocator& fixture)
{
    Access::seed(context, UINT32_MAX, UINT64_MAX - 15u);
    void* const pointer = context.allocate(16u, 16u);
    TEST_EXPECT(ctx, pointer != nullptr);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    const unsigned deallocations = fixture.deallocations;
    context.deallocate(16u, 16u, pointer);
    TEST_EXPECT(ctx, fixture.deallocations == deallocations + 1u);
    TEST_EXPECT(ctx, fixture.outstanding == 0u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == UINT32_MAX);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == UINT64_MAX - 15u);

    fixture.reject_allocate = true;
    TEST_EXPECT(ctx, context.allocate(16u, 16u) == nullptr);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == UINT32_MAX);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == UINT64_MAX - 15u);
    fixture.reject_allocate = false;
    Access::seed(context, 0u, 0u);

    void* const retained = context.allocate(16u, 16u);
    Access::seed(context, 0u, 0u);
    fixture.reject_deallocate = true;
    const unsigned before_failure = fixture.deallocations;
    context.deallocate(16u, 16u, retained);
    TEST_EXPECT(ctx, fixture.deallocations == before_failure + 1u);
    TEST_EXPECT(ctx, fixture.outstanding == 1u);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    fixture.reject_deallocate = false;
    context.deallocate(16u, 16u, retained);
    TEST_EXPECT(ctx, fixture.outstanding == 0u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == UINT32_MAX);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == UINT64_MAX - 15u);
    Access::seed(context, 0u, 0u);

    fixture.misalign = true;
    TEST_EXPECT(ctx, context.allocate(16u, 16u) == nullptr);
    TEST_EXPECT(ctx, fixture.outstanding == 0u);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    fixture.reject_deallocate = true;
    TEST_EXPECT(ctx, context.allocate(16u, 16u) == nullptr);
    TEST_EXPECT(ctx, fixture.outstanding == 1u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 16u);
    fixture.reject_deallocate = false;
    TEST_EXPECT(ctx, SAllocator::deallocate(&fixture, 16u, nullptr));
    Access::sub(context, 1u, 16u);
    fixture.misalign = false;

    const unsigned allocations = fixture.allocations;
    const unsigned frees = fixture.deallocations;
    TEST_EXPECT(ctx, context.allocate(16u, 0u) == nullptr);
    context.deallocate(16u, 16u, nullptr);
    TEST_EXPECT(ctx, fixture.allocations == allocations);
    TEST_EXPECT(ctx, fixture.deallocations == frees);
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_transfer(TTestContext& ctx, memory::CMemoryContext& source, memory::CMemoryAllocator& allocator)
{
    memory::CMemoryContext target{allocator, system_ids::host};
    memory::CMemoryAllocator other_allocator{nullptr, &test_allocate, &test_deallocate};
    memory::CMemoryContext other{other_allocator};
    memory::CMemoryToken token{1u, 16u, &source};
    TEST_EXPECT(ctx, token.allocate(16u));
    void* const address = token.data();
    Access::seed(source, 0u, 0u);
    Access::seed(target, UINT32_MAX, UINT64_MAX - 15u);
    TEST_EXPECT(ctx, !token.reattribute(&other));
    TEST_EXPECT(ctx, token.context() == &source);
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, other.is_attribution_empty());
    TEST_EXPECT(ctx, token.reattribute(&target));
    TEST_EXPECT(ctx, token.context() == &target);
    TEST_EXPECT(ctx, token.data() == address);
    TEST_EXPECT(ctx, source.get_live_allocation_count() == UINT32_MAX);
    TEST_EXPECT(ctx, source.get_live_allocated_bytes() == UINT64_MAX - 15u);
    TEST_EXPECT(ctx, target.is_attribution_empty());
    token.deallocate();
    TEST_EXPECT(ctx, target.get_live_allocation_count() == UINT32_MAX);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == UINT64_MAX - 15u);
    Access::seed(source, 0u, 0u);
    Access::seed(target, 0u, 0u);

    TEST_EXPECT(ctx, !memory::reattribute(source, other, 0u, 0u));
    TEST_EXPECT(ctx, memory::reattribute(source, target, 0u, 0u));
    TEST_EXPECT(ctx, memory::reattribute(source, target, 0u, 16u));
    TEST_EXPECT(ctx, source.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == 16u);
    TEST_EXPECT(ctx, memory::reattribute(target, source, 0u, 16u));
    TEST_EXPECT(ctx, memory::reattribute(source, target, 1u, 0u));
    TEST_EXPECT(ctx, target.get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, memory::reattribute(target, source, 1u, 0u));
    if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t))
    {
        const auto wide = static_cast<std::size_t>(std::uint64_t{UINT32_MAX} + 2u);
        TEST_EXPECT(ctx, memory::reattribute(source, target, wide, 0u));
        TEST_EXPECT(ctx, target.get_live_allocation_count() == 1u);
        TEST_EXPECT(ctx, memory::reattribute(target, source, wide, 0u));
    }
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, target.is_attribution_empty());
}

std::FILE* open_test_log(const std::string& path)
{
    return platform::filesystem::openFile(platform::path::makeNativePath(path.c_str()));
}

void test_closed_logs(TTestContext& ctx)
{
    SAllocator fixture;
    fixture.reject_allocate = true;
    memory::CMemoryAllocator allocator{&fixture, &SAllocator::allocate, &SAllocator::deallocate};
    memory::CMemoryContext context{allocator, system_ids::host};
    auto owner = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());
    if (!owner.is_ready()) return;
    auto& service = *owner;
    const std::string event_path = test_environment::test_log_path("accounting_unopened_events");
    const std::string direct_path = test_environment::test_log_path("accounting_unopened_direct");
    TEST_EXPECT(ctx, service.configure_log_paths(event_path.c_str(), direct_path.c_str()));
    service.publish_configuration(debug_system::k_critical_shutdown_enabled);
    {
        tests::TMemoryContextScope scope{&context};
        TEST_EXPECT(ctx, debug_system::install_service(&service));
        TEST_EXPECT(ctx, !service.start());
        for (std::uint32_t i = 0u; i < debug_system::CEventTransport::k_capacity; ++i)
        {
            TEST_EXPECT(ctx, service.submit_text("Fill unopened service"));
        }
        TEST_EXPECT(ctx, (!debug_system::submit_event<debug_system::EEventLevel::error, debug_system::EEventType::event>(
            MV_INTERNAL_USAGE_POINT, "Closed structured fallback")));
        TEST_EXPECT(ctx, !debug_system::report_immediate(MV_INTERNAL_USAGE_POINT, "Closed immediate fallback"));
        char oversized[debug_system::k_event_format_capacity + 1u];
        std::memset(oversized, 'z', sizeof(oversized) - 1u);
        oversized[sizeof(oversized) - 1u] = 0;
        TEST_EXPECT(ctx, !service.submit_text(oversized));
        Access::sub(context, 1u, 1u);
        Access::add(context, 1u, 1u);
        TEST_EXPECT(ctx, fixture.allocations == 0u);
        TEST_EXPECT(ctx, fixture.deallocations == 0u);
        TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
        TEST_EXPECT(ctx, debug_system::uninstall_service(&service));
        //  Explicit provisioning can still fail for an actual allocator failure.
        TEST_EXPECT(ctx, !service.open_logs());
        TEST_EXPECT(ctx, fixture.allocations != 0u);
        TEST_EXPECT(ctx, !service.start());
    }
    for (const std::string* path : {&event_path, &direct_path})
    {
        std::FILE* const file = open_test_log(*path);
        TEST_EXPECT(ctx, file == nullptr);
        if (file != nullptr) std::fclose(file);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void run(TTestContext& ctx)
{
    SAllocator fixture;
    memory::CMemoryAllocator allocator{&fixture, &SAllocator::allocate, &SAllocator::deallocate};
    memory::CMemoryContext context{allocator, system_ids::host};
    auto service_owner = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, service_owner.is_ready());
    if (!service_owner.is_ready()) return;
    auto& service = *service_owner;
    const std::string event_path = test_environment::test_log_path("accounting_events");
    const std::string direct_path = test_environment::test_log_path("accounting_direct");
    TEST_EXPECT(ctx, service.configure_log_paths(event_path.c_str(), direct_path.c_str()));
    TEST_EXPECT(ctx, service.open_logs());
    service.publish_configuration(debug_system::k_critical_shutdown_enabled);
    TEST_EXPECT(ctx, debug_system::install_service(&service));

    test_boundaries(ctx, context, service);
    test_concurrent_adjustments(ctx, context, service);
    test_real_operations(ctx, context, fixture);
    test_transfer(ctx, context, allocator);
    test_checked_sums(ctx, context, allocator, service);
    test_attribution_contract(ctx, context, allocator, service);
    test_document_with_accounting_discrepancies(ctx, context, allocator, service);

    //  With no writer running, fill the bounded queue to exercise direct fallback.
    for (std::uint32_t i = 0u; i < debug_system::CEventTransport::k_capacity; ++i)
    {
        MV_WARNING("Accounting fixture queue fill");
    }
    fixture.reject_allocate = true;
    const unsigned allocations = fixture.allocations;
    const unsigned deallocations = fixture.deallocations;
    {
        tests::TMemoryContextScope scope{&context};
        test_boundaries(ctx, context, service);
        test_checked_sums(ctx, context, allocator, service);
        test_attribution_contract(ctx, context, allocator, service);
    }
    TEST_EXPECT(ctx, fixture.allocations == allocations);
    TEST_EXPECT(ctx, fixture.deallocations == deallocations);
    TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
    TEST_EXPECT(ctx, service.start());
    TEST_EXPECT(ctx, service.stop());
    TEST_EXPECT(ctx, debug_system::uninstall_service(&service));
    std::FILE* const concurrent_log = open_test_log(event_path);
    TEST_EXPECT(ctx, concurrent_log != nullptr);
    bool in_concurrent = false;
    unsigned concurrent_reports = 0u;
    char log_line[2048]{};
    while ((concurrent_log != nullptr) && (std::fgets(log_line, sizeof(log_line), concurrent_log) != nullptr))
    {
        const std::string line{log_line};
        if (line.find("Accounting concurrent begin") != std::string::npos) in_concurrent = true;
        if (line.find("Accounting concurrent end") != std::string::npos) in_concurrent = false;
        if (!in_concurrent || (line.find("Memory accounting") == std::string::npos)) continue;
        ++concurrent_reports;
        const bool count = line.find("count ") != std::string::npos;
        const bool add = line.find(" add:") != std::string::npos;
        const char* const expected = count
            ? (add ? "before 2147483647 after 2147483648 adjustment 1" : "before 2147483648 after 2147483647 adjustment 1")
            : (add ? "before 9223372036854775807 after 9223372036854775808 adjustment 1"
                   : "before 9223372036854775808 after 9223372036854775807 adjustment 1");
        TEST_EXPECT(ctx, line.find(expected) != std::string::npos);
    }
    if (concurrent_log != nullptr) std::fclose(concurrent_log);
    TEST_EXPECT(ctx, concurrent_reports == 4u);
    for (const std::string* path : {&event_path, &direct_path})
    {
        TEST_EXPECT(ctx, tests::file_contains(path->c_str(), "before 2147483647 after 2147483648 adjustment 1"));
        TEST_EXPECT(ctx, tests::file_contains(path->c_str(), "before 9223372036854775808 after 9223372036854775807 adjustment 1"));
        TEST_EXPECT(ctx, tests::file_contains(path->c_str(), "Memory accounting count add: context"));
        TEST_EXPECT(ctx, tests::file_contains(path->c_str(), "Memory accounting bytes sub: context"));
        TEST_EXPECT(ctx, tests::file_contains(path->c_str(),
            "Memory accounting count sum overflow: left 4294967295 right 3 total 2"));
        TEST_EXPECT(ctx, tests::file_contains(path->c_str(),
            "Memory accounting bytes sum overflow: left 18446744073709551615 right 17 total 16"));
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    TEST_EXPECT(ctx, fixture.outstanding == 0u);

    //  Missing service: diagnostics still cannot suppress or undo the adjustment.
    Access::sub(context, 1u, 16u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == UINT32_MAX);
    Access::add(context, 1u, 16u);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_closed_logs(ctx);
}

}   //  namespace accounting_tests

int run_memory_token_tests()
{
    TTestContext ctx;
    memory::CMemoryAllocator allocator{ nullptr, &test_allocate, &test_deallocate };
    memory::CMemoryAllocator incompatible_allocator{ nullptr, &test_allocate, &test_deallocate };
    memory::CMemoryContext context{ allocator };
    memory::CMemoryContext alternate_context{ allocator };
    memory::CMemoryContext incompatible_context{ incompatible_allocator };

    test_stable_map_index_growth_and_slack(ctx, context);
    test_stable_requested_count_and_internal_slack(ctx, context);
    test_stable_bounds_and_max_count(ctx, context);
    test_growth_policy_ceiling(ctx);
    test_stable_clone_preserves_content_and_configuration(ctx, context);
    test_stable_clone_to_compatible_alternate_context(ctx, context, alternate_context);
    test_stable_reattribute_between_compatible_contexts(ctx, context, alternate_context);
    test_aggregate_accounting_and_context_replacement(ctx, context, alternate_context, incompatible_context);
    test_stable_deallocate_preserves_configuration_and_reuse(ctx, context);
    accounting_tests::run(ctx);

    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, alternate_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, alternate_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, incompatible_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, incompatible_context.get_live_allocated_bytes() == 0u);
    std::cout << "CMemoryToken: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
