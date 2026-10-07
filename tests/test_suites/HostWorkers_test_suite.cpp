//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostWorkers_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Oct 26

#include <cstdint>
#include <iostream>
#include <limits>

#include "host/runtime/worker_policy.hpp"
#include "tests/test_suites/HostWorkers_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

void test_hardware_limits(TTestContext& ctx)
{
    struct SLimit { std::uint32_t hardware; std::uint32_t workers; };
    const SLimit limits[]{
        { 0u, 1u }, { 1u, 1u }, { 4u, 1u }, { 7u, 1u },
        { 8u, 2u }, { 9u, 2u }, { 10u, 2u }, { 11u, 3u },
        { 12u, 4u }, { 16u, 8u }, { 17u, 9u }, { 18u, 9u },
        { 64u, 9u }, { 128u, 9u }, { std::numeric_limits<std::uint32_t>::max(), 9u } };
    for (const auto& limit : limits)
    {
        TEST_EXPECT(ctx, host::worker_count_limit(limit.hardware) == limit.workers);
    }
    TEST_EXPECT(ctx, host::k_default_worker_count == 2u);
    TEST_EXPECT(ctx, host::k_max_worker_count - 1u == host::k_max_conditioning_threads);
}

void test_count_parsing(TTestContext& ctx)
{
    std::uint32_t count = 71u;
    const char* invalid_values[]{ nullptr, "", "0", "000", "-1", "+2", " 2", "2 ", "1x", "1.5", "4294967296", "99999999999999999999" };
    for (const char* invalid : invalid_values)
    {
        TEST_EXPECT(ctx, !host::parse_worker_count(invalid, count));
        TEST_EXPECT(ctx, count == 71u);
    }
    TEST_EXPECT(ctx, host::parse_worker_count("1", count) && (count == 1u));
    TEST_EXPECT(ctx, host::parse_worker_count("0002", count) && (count == 2u));
    TEST_EXPECT(ctx, host::parse_worker_count("128", count) && (count == 128u));
    TEST_EXPECT(ctx, host::parse_worker_count("4294967295", count) && (count == std::numeric_limits<std::uint32_t>::max()));
}

void test_dispatch_and_out_of_order_completion(TTestContext& ctx)
{
    host::CConditioningSchedule schedule;
    TEST_EXPECT(ctx, schedule.select() == -1);
    TEST_EXPECT(ctx, !schedule.initialise(0u));
    TEST_EXPECT(ctx, !schedule.initialise(host::k_max_conditioning_threads + 1u));
    TEST_EXPECT(ctx, schedule.initialise(3u));

    //  Selection without an accepted post changes neither load nor tie order.
    TEST_EXPECT(ctx, schedule.select() == 0);
    TEST_EXPECT(ctx, schedule.select() == 0);
    TEST_EXPECT(ctx, !schedule.record_post(3u));
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        TEST_EXPECT(ctx, schedule.select() == static_cast<std::int32_t>(index));
        TEST_EXPECT(ctx, schedule.record_post(index));
    }
    TEST_EXPECT(ctx, schedule.select() == 0);
    TEST_EXPECT(ctx, schedule.record_post(0u)); //  Loads: 2, 1, 1.
    TEST_EXPECT(ctx, schedule.select() == 1);

    //  An idle worker wins even when the round-robin cursor points elsewhere.
    TEST_EXPECT(ctx, schedule.record_completion(2u));
    TEST_EXPECT(ctx, schedule.select() == 2);
    TEST_EXPECT(ctx, !schedule.record_completion(2u));
    TEST_EXPECT(ctx, !schedule.record_completion(3u));
    TEST_EXPECT(ctx, schedule.record_post(2u));
    TEST_EXPECT(ctx, schedule.select() == 1);
    TEST_EXPECT(ctx, schedule.record_post(1u)); //  Loads: 2, 2, 1.
    TEST_EXPECT(ctx, schedule.select() == 2);
    TEST_EXPECT(ctx, schedule.record_completion(0u));
    TEST_EXPECT(ctx, schedule.record_completion(0u));
    TEST_EXPECT(ctx, schedule.select() == 0);
}

void test_single_and_full_groups(TTestContext& ctx)
{
    host::CConditioningSchedule shared;
    TEST_EXPECT(ctx, shared.initialise(1u));
    TEST_EXPECT(ctx, shared.select() == 0);
    TEST_EXPECT(ctx, shared.record_post(0u));
    TEST_EXPECT(ctx, shared.record_post(0u));
    TEST_EXPECT(ctx, shared.select() == 0);
    TEST_EXPECT(ctx, shared.record_completion(0u));
    TEST_EXPECT(ctx, shared.record_completion(0u));
    TEST_EXPECT(ctx, !shared.record_completion(0u));

    host::CConditioningSchedule full;
    TEST_EXPECT(ctx, full.initialise(host::k_max_conditioning_threads));
    for (std::uint32_t index = 0u; index < host::k_max_conditioning_threads; ++index)
    {
        TEST_EXPECT(ctx, full.select() == static_cast<std::int32_t>(index));
        TEST_EXPECT(ctx, full.record_post(index));
    }
    TEST_EXPECT(ctx, full.select() == 0);
    TEST_EXPECT(ctx, full.record_completion(7u));
    TEST_EXPECT(ctx, full.select() == 7);
}

}   //  namespace

int run_host_workers_tests()
{
    TTestContext ctx;
    test_hardware_limits(ctx);
    test_count_parsing(ctx);
    test_dispatch_and_out_of_order_completion(ctx);
    test_single_and_full_groups(ctx);
    std::cout << "HostWorkers: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
