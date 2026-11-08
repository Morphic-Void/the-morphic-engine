
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TrackedAllocator_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    07 Nov 26
//
//  Exact-address ownership, descriptor growth, failure recovery and attribution.

#include <array>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

#include "allocators/CTrackedAllocator.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "TrackedAllocator_test_suite.hpp"

namespace
{

using tests::TTestContext;
using EFreeResult = CTrackedAllocator::EFreeResult;

struct SFixture
{
    tests::TAllocationFailureFixture state;
    memory::CMemoryAllocator allocator{ &state, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
};

struct SRecord
{
    std::uint8_t* pointer{ nullptr };
    std::size_t bytes{ 0u };
    std::size_t alignment{ 0u };
    std::uint8_t tag{ 0u };
};

void expect_attribution(TTestContext& ctx, const CTrackedAllocator& owner, const memory::CMemoryContext& context)
{
    const memory::SMemoryAttribution attribution = owner.memory_attribution();
    TEST_EXPECT(ctx, (attribution.allocation_count == (owner.is_ready() ? (owner.allocation_count() + 3u) : 0u)));
    TEST_EXPECT(ctx, (attribution.token_count == (owner.allocation_count() + 3u)));
    TEST_EXPECT(ctx, (attribution.allocation_count == context.get_live_allocation_count()));
    TEST_EXPECT(ctx, (attribution.allocation_size == context.get_live_allocated_bytes()));
    TEST_EXPECT(ctx, (owner.total_owned_size() == attribution.allocation_size));
    TEST_EXPECT(ctx, (attribution.source == (owner.is_ready() ? &context : nullptr)));
}

void test_initialisation(TTestContext& ctx)
{
    static_assert(!std::is_copy_constructible_v<CTrackedAllocator>);
    static_assert(!std::is_copy_assignable_v<CTrackedAllocator>);
    static_assert(std::is_nothrow_move_constructible_v<CTrackedAllocator>);
    static_assert(std::is_nothrow_move_assignable_v<CTrackedAllocator>);
    SFixture fixture;
    {
        CTrackedAllocator owner;
        std::uint8_t outside = 0u;
        TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
        TEST_EXPECT(ctx, (owner.allocate(1u) == nullptr));
        TEST_EXPECT(ctx, (owner.free(nullptr) == EFreeResult::no_action));
        TEST_EXPECT(ctx, (owner.free(&outside) == EFreeResult::invalid_allocation));
        TEST_EXPECT(ctx, (owner.allocation_size(&outside) == 0u));
        TEST_EXPECT(ctx, (owner.allocation_alignment(nullptr) == 0u));
        TEST_EXPECT(ctx, (owner.allocated_size() == 0u));
        std::size_t visited = 0u;
        owner.visit_allocations([&](const void*, const std::size_t, const std::size_t) { ++visited; });
        TEST_EXPECT(ctx, (visited == 0u));
        owner.reset();
        owner.deallocate();
        expect_attribution(ctx, owner, fixture.context);
        TEST_EXPECT(ctx, !owner.initialise(fixture.context, std::numeric_limits<std::size_t>::max()));
        TEST_EXPECT(ctx, (fixture.state.attempts == 0u));
        for (std::size_t failure = 0u; failure < 3u; ++failure)
        {
            const std::size_t attempts = fixture.state.attempts;
            fixture.state.fail_on = attempts + failure;
            fixture.state.failed = false;
            TEST_EXPECT(ctx, !owner.initialise(fixture.context));
            TEST_EXPECT(ctx, (fixture.state.failed && (fixture.state.attempts == (attempts + failure + 1u))));
            TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
            expect_attribution(ctx, owner, fixture.context);
        }
        fixture.state.fail_on = std::numeric_limits<std::size_t>::max();
        TEST_EXPECT(ctx, owner.initialise(fixture.context));
        const std::size_t attempts = fixture.state.attempts;
        const std::uint64_t cost = owner.total_owned_size();
        TEST_EXPECT(ctx, !owner.initialise(fixture.context));
        TEST_EXPECT(ctx, (owner.allocate(0u) == nullptr));
        TEST_EXPECT(ctx, (owner.allocate(1u, 0u) == nullptr));
        TEST_EXPECT(ctx, (owner.allocate(1u, 3u) == nullptr));
        TEST_EXPECT(ctx, (owner.allocate(std::numeric_limits<std::size_t>::max()) == nullptr));
        TEST_EXPECT(ctx, (owner.allocate(1u, std::numeric_limits<std::size_t>::max()) == nullptr));
        TEST_EXPECT(ctx, ((fixture.state.attempts == attempts) && (owner.total_owned_size() == cost)));
        void* const block = owner.allocate(13u);
        TEST_EXPECT(ctx, (block != nullptr));
        TEST_EXPECT(ctx, (owner.allocation_size(block) == 13u));
        TEST_EXPECT(ctx, (owner.allocation_alignment(block) == alignof(std::max_align_t)));
        TEST_EXPECT(ctx, !owner.initialise(fixture.context));
        expect_attribution(ctx, owner, fixture.context);
        owner.deallocate();
        TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
        expect_attribution(ctx, owner, fixture.context);
        TEST_EXPECT(ctx, owner.initialise(fixture.context, 48u));
        TEST_EXPECT(ctx, (owner.allocate(1u) != nullptr));
    }
    TEST_EXPECT(ctx, fixture.context.is_attribution_empty());
}

void check_records(TTestContext& ctx, const CTrackedAllocator& owner, const std::array<SRecord, 96u>& records)
{
    std::size_t count = 0u;
    std::uint64_t bytes = 0u;
    for (const SRecord& record : records)
    {
        if (record.pointer == nullptr) continue;
        ++count;
        bytes += record.bytes;
        TEST_EXPECT(ctx, (owner.allocation_size(record.pointer) == record.bytes));
        TEST_EXPECT(ctx, (owner.allocation_alignment(record.pointer) == record.alignment));
        for (std::size_t i = 0u; i < record.bytes; ++i)
        {
            TEST_EXPECT(ctx, (record.pointer[i] == record.tag));
        }
    }
    TEST_EXPECT(ctx, ((owner.allocation_count() == count) && (owner.allocated_size() == bytes)));
    std::uintptr_t previous = 0u;
    std::size_t visited = 0u;
    owner.visit_allocations([&](const void* const pointer, const std::size_t size, const std::size_t alignment)
    {
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
        TEST_EXPECT(ctx, (address > previous));
        previous = address;
        std::size_t matches = 0u;
        for (const SRecord& record : records)
        {
            if (record.pointer == pointer)
            {
                ++matches;
                TEST_EXPECT(ctx, ((size == record.bytes) && (alignment == record.alignment)));
            }
        }
        TEST_EXPECT(ctx, (matches == 1u));
        ++visited;
    });
    TEST_EXPECT(ctx, (visited == count));
}

void test_mixed_allocations(TTestContext& ctx)
{
    SFixture fixture;
    {
        CTrackedAllocator owner;
        TEST_EXPECT(ctx, owner.initialise(fixture.context));
        std::array<SRecord, 96u> records{};
        std::uint32_t random = 0x12345678u;
        for (std::size_t step = 0u; step < 768u; ++step)
        {
            random = (random * 1664525u) + 1013904223u;
            const std::size_t index = (step < records.size()) ? step : (random % records.size());
            SRecord& record = records[index];
            if (record.pointer != nullptr)
            {
                TEST_EXPECT(ctx, (owner.free(record.pointer) == EFreeResult::released));
                TEST_EXPECT(ctx, (owner.free(record.pointer) == EFreeResult::invalid_allocation));
                TEST_EXPECT(ctx, (owner.allocation_size(record.pointer) == 0u));
                TEST_EXPECT(ctx, (owner.allocation_alignment(record.pointer) == 0u));
                record = SRecord{};
            }
            else
            {
                record.bytes = 2u + (random % 253u);
                record.alignment = std::size_t{ 1u } << (index % 9u);
                record.tag = static_cast<std::uint8_t>(index + 1u);
                record.pointer = static_cast<std::uint8_t*>(owner.allocate(record.bytes, record.alignment));
                TEST_EXPECT(ctx, (record.pointer != nullptr));
                if (record.pointer == nullptr) return;
                const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(record.pointer);
                TEST_EXPECT(ctx, ((address & (record.alignment - 1u)) == 0u));
                std::memset(record.pointer, record.tag, record.bytes);
                const std::uint64_t cost = owner.total_owned_size();
                const std::uint64_t bytes = owner.allocated_size();
                TEST_EXPECT(ctx, (owner.free(record.pointer + 1u) == EFreeResult::invalid_allocation));
                TEST_EXPECT(ctx, (owner.allocation_size(record.pointer + 1u) == 0u));
                TEST_EXPECT(ctx, (owner.allocation_alignment(record.pointer + 1u) == 0u));
                TEST_EXPECT(ctx, ((owner.total_owned_size() == cost) && (owner.allocated_size() == bytes)));
            }
            if (((step + 1u) % 96u) == 0u)
            {
                check_records(ctx, owner, records);
                expect_attribution(ctx, owner, fixture.context);
            }
        }
        std::uint8_t outside = 0u;
        TEST_EXPECT(ctx, (owner.free(&outside) == EFreeResult::invalid_allocation));
        owner.reset();
        TEST_EXPECT(ctx, (owner.is_ready() && owner.is_empty() && (owner.allocated_size() == 0u)));
        expect_attribution(ctx, owner, fixture.context);
        for (const SRecord& record : records)
        {
            TEST_EXPECT(ctx, (owner.allocation_size(record.pointer) == 0u));
        }
        //  Reuse keeps the three index allocations, but every new payload allocates.
        fixture.state.reject_all = true;
        const std::uint64_t retained = owner.total_owned_size();
        TEST_EXPECT(ctx, (owner.allocate(1u) == nullptr));
        TEST_EXPECT(ctx, (owner.total_owned_size() == retained));
        fixture.state.reject_all = false;
        const std::size_t attempts = fixture.state.attempts;
        TEST_EXPECT(ctx, (owner.allocate(1u) != nullptr));
        TEST_EXPECT(ctx, (fixture.state.attempts == (attempts + 1u)));
    }
    TEST_EXPECT(ctx, fixture.context.is_attribution_empty());
}

void test_growth_failures(TTestContext& ctx)
{
    //  Private payload, token backing, key backing, then index metadata.
    for (std::size_t failure = 0u; failure < 4u; ++failure)
    {
        SFixture fixture;
        {
            CTrackedAllocator owner;
            TEST_EXPECT(ctx, owner.initialise(fixture.context, 32u));
            std::uint8_t* pointers[32]{};
            for (std::size_t i = 0u; i < 32u; ++i)
            {
                pointers[i] = static_cast<std::uint8_t*>(owner.allocate((i + 1u), 16u));
                TEST_EXPECT(ctx, (pointers[i] != nullptr));
                if (pointers[i] == nullptr) return;
                std::memset(pointers[i], static_cast<int>(i + 1u), (i + 1u));
            }
            const std::uint64_t before = owner.total_owned_size();
            const std::size_t attempts = fixture.state.attempts;
            fixture.state.fail_on = attempts + failure;
            TEST_EXPECT(ctx, (owner.allocate(65u, 64u) == nullptr));
            TEST_EXPECT(ctx, (fixture.state.failed && (fixture.state.attempts == (attempts + failure + 1u))));
            TEST_EXPECT(ctx, ((owner.allocation_count() == 32u) && (owner.allocated_size() == 528u)));
            //  Later growth failure can retain expanded index storage.
            TEST_EXPECT(ctx, (owner.total_owned_size() >= before));
            expect_attribution(ctx, owner, fixture.context);
            for (std::size_t i = 0u; i < 32u; ++i)
            {
                TEST_EXPECT(ctx, (owner.allocation_size(pointers[i]) == (i + 1u)));
                TEST_EXPECT(ctx, ((pointers[i][0] == (i + 1u)) && (pointers[i][i] == (i + 1u))));
            }
            fixture.state.fail_on = std::numeric_limits<std::size_t>::max();
            void* const added = owner.allocate(65u, 64u);
            TEST_EXPECT(ctx, (added != nullptr));
            TEST_EXPECT(ctx, ((owner.allocation_count() == 33u) && (owner.allocated_size() == 593u)));
            expect_attribution(ctx, owner, fixture.context);
            for (std::uint8_t* const pointer : pointers)
            {
                TEST_EXPECT(ctx, (owner.free(pointer) == EFreeResult::released));
            }
            TEST_EXPECT(ctx, (owner.free(added) == EFreeResult::released));
            TEST_EXPECT(ctx, owner.is_empty());
        }
        TEST_EXPECT(ctx, fixture.context.is_attribution_empty());
    }
}

void test_moves(TTestContext& ctx)
{
    SFixture first;
    SFixture second;
    {
        CTrackedAllocator source;
        TEST_EXPECT(ctx, source.initialise(first.context));
        std::uint8_t* const pointer = static_cast<std::uint8_t*>(source.allocate(77u, 128u));
        TEST_EXPECT(ctx, (pointer != nullptr));
        if (pointer == nullptr) return;
        std::memset(pointer, 0x5a, 77u);
        const std::uint64_t cost = source.total_owned_size();
        first.state.reject_all = true;
        const std::size_t first_attempts = first.state.attempts;
        CTrackedAllocator moved{ std::move(source) };
        TEST_EXPECT(ctx, (!source.is_ready() && source.is_empty()));
        TEST_EXPECT(ctx, (source.allocation_size(pointer) == 0u));
        CTrackedAllocator destination;
        TEST_EXPECT(ctx, destination.initialise(second.context));
        TEST_EXPECT(ctx, (destination.allocate(33u) != nullptr));
        second.state.reject_all = true;
        const std::size_t second_attempts = second.state.attempts;
        destination = std::move(moved);
        CTrackedAllocator& self = destination;
        destination = std::move(self);
        TEST_EXPECT(ctx, (!moved.is_ready() && moved.is_empty()));
        TEST_EXPECT(ctx, second.context.is_attribution_empty());
        TEST_EXPECT(ctx, ((first.state.attempts == first_attempts) && (second.state.attempts == second_attempts)));
        TEST_EXPECT(ctx, (destination.total_owned_size() == cost));
        TEST_EXPECT(ctx, ((destination.allocation_size(pointer) == 77u) && (destination.allocation_alignment(pointer) == 128u)));
        TEST_EXPECT(ctx, ((pointer[0] == 0x5a) && (pointer[76] == 0x5a)));
        expect_attribution(ctx, destination, first.context);
        first.state.reject_all = false;
        TEST_EXPECT(ctx, (destination.allocate(3u) != nullptr));
        expect_attribution(ctx, destination, first.context);
        second.state.reject_all = false;
        TEST_EXPECT(ctx, source.initialise(second.context));
        TEST_EXPECT(ctx, (source.allocate(5u) != nullptr));
        destination.deallocate();
        TEST_EXPECT(ctx, first.context.is_attribution_empty());
        expect_attribution(ctx, source, second.context);
    }
    TEST_EXPECT(ctx, (first.context.is_attribution_empty() && second.context.is_attribution_empty()));
}

void test_no_ambient(TTestContext& ctx)
{
    SFixture fixture;
    memory::CMemoryContext* const previous_module = memory::set_module_memory_context(nullptr);
    {
        const tests::TMemoryContextScope no_thread{ nullptr };
        CTrackedAllocator owner;
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
        TEST_EXPECT(ctx, owner.initialise(fixture.context));
        for (std::size_t i = 0u; i < 65u; ++i)
        {
            TEST_EXPECT(ctx, (owner.allocate((i + 1u), 32u) != nullptr));
        }
        expect_attribution(ctx, owner, fixture.context);
        owner.reset();
        TEST_EXPECT(ctx, (owner.allocate(17u) != nullptr));
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
    }
    (void)memory::set_module_memory_context(previous_module);
    TEST_EXPECT(ctx, fixture.context.is_attribution_empty());
}

}   //  namespace

int run_tracked_allocator_tests()
{
    TTestContext ctx;
    SFixture ambient;
    ambient.state.reject_all = true;
    const tests::TMemoryContextScope scope{ &ambient.context };
    test_initialisation(ctx);
    test_mixed_allocations(ctx);
    test_growth_failures(ctx);
    test_moves(ctx);
    test_no_ambient(ctx);
    TEST_EXPECT(ctx, (ambient.state.attempts == 0u));
    TEST_EXPECT(ctx, ambient.context.is_attribution_empty());
    return ctx.report("TrackedAllocator");
}
