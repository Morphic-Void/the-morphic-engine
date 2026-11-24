
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DirectConnections_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    05 Nov 26
//
//  Deterministic service/Host interleavings using the production pipes and helpers.

#include "framework/system/system_ids.hpp"
#include "foundation/threading/transports/CBulkBufferTransport.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/test_allocator.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/thread_lifetime.hpp"

#include <cstring>

namespace
{
static void bulk_backing_ownership(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext owner{ allocator }, participant{ allocator };
    {
        tests::TMemoryContextScope scope{ &owner };
        threading::transports::CBulkBufferTransport bulk;
        TEST_EXPECT(ctx, !bulk.initialise(0u));
        TEST_EXPECT(ctx, !bulk.initialise(UINT64_MAX));
        TEST_EXPECT(ctx, !bulk.initialise(memory::k_byte_size_ceiling));
        TEST_EXPECT(ctx, bulk.initialise(129u));
        TEST_EXPECT(ctx, (bulk.memory_context() == &owner));
        TEST_EXPECT(ctx, (owner.get_live_allocated_bytes() == 768u));
        TEST_EXPECT(ctx, !bulk.initialise(257u));
        const std::size_t attempts = allocation.attempts;
        tests::TMemoryContextScope participant_scope{ &participant };
        for (unsigned index = 0u; index < 32u; ++index)
        {
            const memory::CMemoryView output = bulk.producer_data();
            TEST_EXPECT(ctx, (output.bytes() == 129u));
            if (!output) return;
            std::memset(output.data(), static_cast<int>(index), output.bytes());
            (void)bulk.publish();
            TEST_EXPECT(ctx, bulk.discard_and_acquire());
            const memory::CMemoryConstView input = bulk.consumer_data();
            TEST_EXPECT(ctx, (input && (*static_cast<const unsigned char*>(input.data()) == index)));
        }
        TEST_EXPECT(ctx, ((allocation.attempts == attempts) && participant.is_attribution_empty()));
    }
    TEST_EXPECT(ctx, (owner.is_attribution_empty() && participant.is_attribution_empty()));
}

struct SConcurrentBulk
{
    threading::transports::CBulkBufferTransport bulk;
    std::atomic<bool> done{ false };
    static std::uint32_t MV_STD_ABI_CALL write(void* const context) noexcept
    {
        SConcurrentBulk& shared = *static_cast<SConcurrentBulk*>(context);
        for (unsigned index = 1u; index <= 20000u; ++index)
        {
            const memory::CMemoryView output = shared.bulk.producer_data();
            std::memset(output.data(), static_cast<int>(index & 255u), output.bytes());
            (void)shared.bulk.publish();
        }
        shared.done.store(true, std::memory_order_release);
        return 0u;
    }
};

static void bulk_concurrent_exchange(tests::TTestContext& ctx)
{
    SConcurrentBulk shared;
    TEST_EXPECT(ctx, shared.bulk.initialise(129u));
    if (!shared.bulk.capacity()) return;
    platform::threading::CThread producer;
    const bool created = producer.create(&SConcurrentBulk::write, &shared);
    TEST_EXPECT(ctx, created);
    if (!created) return;
    bool consistent = true;
    unsigned acquired = 0u;
    do
    {
        if (shared.bulk.discard_and_acquire())
        {
            ++acquired;
            const memory::CMemoryConstView input = shared.bulk.consumer_data();
            const unsigned char* const bytes = static_cast<const unsigned char*>(input.data());
            const unsigned char expected = bytes[0];
            platform::threading::processor_relax();
            for (std::size_t byte = 0u; byte < input.bytes(); ++byte)
            {
                if (bytes[byte] != expected) consistent = false;
            }
        }
        else
        {
            platform::threading::processor_relax();
        }
    } while (!shared.done.load(std::memory_order_acquire) || shared.bulk.has_staged_data());
    TEST_EXPECT(ctx, producer.join_and_close());
    TEST_EXPECT(ctx, (consistent && (acquired != 0u)));
}

}   //  namespace

void run_common_bulk_connection_tests(tests::TTestContext& ctx);
void run_common_establishment_tests(tests::TTestContext& ctx);
void run_common_pod_connection_tests(tests::TTestContext& ctx);
void run_common_native_join_tests(tests::TTestContext& ctx);
void run_common_multi_connection_tests(tests::TTestContext& ctx);

void run_direct_connection_tests(tests::TTestContext& ctx)
{
    run_common_bulk_connection_tests(ctx);
    run_common_establishment_tests(ctx);
    run_common_pod_connection_tests(ctx);
    run_common_native_join_tests(ctx);
    run_common_multi_connection_tests(ctx);
    bulk_backing_ownership(ctx);
    bulk_concurrent_exchange(ctx);
}
