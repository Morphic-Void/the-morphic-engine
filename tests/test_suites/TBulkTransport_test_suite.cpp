
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TBulkTransport_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    15 Nov 26
//
//  Regression coverage for empty polling, publication history and held buffers.

#include "framework/system/system_ids.hpp"
#include "threading/transports/TBulkTransport.hpp"
#include "tests/test_suites/TBulkTransport_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using threading::transports::TBulk;

struct SPayload
{
    std::uint32_t value{ 0u };
};

static void initial_publication_acknowledgement(tests::TTestContext& ctx)
{
    for (std::uint32_t polls = 0u; polls < 4u; ++polls)
    {
        TBulk<SPayload> bulk;
        threading::transports::TBulkProducerEndpoint<SPayload> producer{ bulk };
        threading::transports::TBulkConsumerEndpoint<SPayload> consumer{ bulk };
        for (std::uint32_t poll = 0u; poll < polls; ++poll)
        {
            TEST_EXPECT(ctx, !consumer.discard_and_acquire());
            TEST_EXPECT(ctx, !bulk.has_staged_data());
            TEST_EXPECT(ctx, bulk.validate());
        }
        producer.producer_data()->value = 1u;
        TEST_EXPECT(ctx, !producer.publish());
        TEST_EXPECT(ctx, bulk.has_staged_data());
        TEST_EXPECT(ctx, consumer.discard_and_acquire());
        const SPayload* const held = consumer.consumer_data();
        TEST_EXPECT(ctx, (held && (held->value == 1u)));
        producer.producer_data()->value = 2u;
        TEST_EXPECT(ctx, producer.publish());
        TEST_EXPECT(ctx, (held && (held->value == 1u)));
        producer.producer_data()->value = 3u;
        TEST_EXPECT(ctx, !producer.publish());
        TEST_EXPECT(ctx, consumer.discard_and_acquire());
        const SPayload* const latest = consumer.consumer_data();
        TEST_EXPECT(ctx, (latest && (latest->value == 3u)));
    }
}

static void quiescent_validation(tests::TTestContext& ctx)
{
    TBulk<SPayload> bulk;
    TEST_EXPECT(ctx, bulk.validate());
    bulk.producer_data()->value = 1u;
    TEST_EXPECT(ctx, !bulk.publish());
    TEST_EXPECT(ctx, bulk.validate());
    TEST_EXPECT(ctx, bulk.discard_and_acquire());
    TEST_EXPECT(ctx, bulk.validate()); //  Consumer acquired before producer acknowledgement.
    const SPayload* const held = bulk.consumer_data();
    bulk.producer_data()->value = 2u;
    TEST_EXPECT(ctx, bulk.validate());
    TEST_EXPECT(ctx, bulk.publish());
    TEST_EXPECT(ctx, bulk.validate()); //  Held slot and a pending publication coexist.
    bulk.producer_data()->value = 3u;
    TEST_EXPECT(ctx, !bulk.publish());
    TEST_EXPECT(ctx, bulk.validate());
    TEST_EXPECT(ctx, (held && (held->value == 1u)));
    TEST_EXPECT(ctx, bulk.discard_and_acquire());
    TEST_EXPECT(ctx, bulk.validate());
    TEST_EXPECT(ctx, !bulk.discard_and_acquire());
    TEST_EXPECT(ctx, !bulk.consumer_data());
    TEST_EXPECT(ctx, bulk.validate());
    TEST_EXPECT(ctx, !bulk.discard_and_acquire());
    TEST_EXPECT(ctx, bulk.validate());
    bulk.producer_data()->value = 4u;
    TEST_EXPECT(ctx, bulk.publish());
    TEST_EXPECT(ctx, bulk.validate());
    TEST_EXPECT(ctx, bulk.discard_and_acquire());
    TEST_EXPECT(ctx, bulk.validate());
}

static void bounded_operation_sequences(tests::TTestContext& ctx)
{
    constexpr std::uint32_t operation_count = 10u;
    constexpr std::uint32_t sequence_count = 1u << operation_count;
    bool consistent = true;
    for (std::uint32_t sequence = 0u; sequence < sequence_count; ++sequence)
    {
        TBulk<SPayload> bulk;
        std::uint32_t serial = 0u, pending = 0u, held_value = 0u;
        bool previous_acquired = false;
        const SPayload* held = nullptr;
        for (std::uint32_t operation = 0u; operation < operation_count; ++operation)
        {
            if ((sequence & (1u << operation)) != 0u)
            {
                SPayload* const output = bulk.producer_data();
                if (held && (output == held))
                {
                    consistent = false;
                }
                output->value = ++serial;
                const bool acknowledged = bulk.publish();
                if (acknowledged != previous_acquired)
                {
                    consistent = false;
                }
                pending = serial;
                previous_acquired = false;
                if (held && (held->value != held_value))
                {
                    consistent = false;
                }
            }
            else
            {
                const bool expected = pending != 0u;
                const bool acquired = bulk.discard_and_acquire();
                if (acquired != expected) consistent = false;
                held_value = pending;
                pending = 0u;
                if (expected) previous_acquired = true;
                held = bulk.consumer_data();
                if (expected)
                {
                    if (!held || (held->value != held_value))
                    {
                        consistent = false;
                    }
                }
                else if (held) consistent = false;
            }
            if (bulk.has_staged_data() != (pending != 0u))
            {
                consistent = false;
            }
            if (!bulk.validate()) consistent = false;
        }
    }
    TEST_EXPECT(ctx, consistent);
}

}   //  namespace

int run_bulk_transport_tests()
{
    tests::TTestContext ctx;
    initial_publication_acknowledgement(ctx);
    quiescent_validation(ctx);
    bounded_operation_sequences(ctx);
    return ctx.report("TBulkTransport");
}
