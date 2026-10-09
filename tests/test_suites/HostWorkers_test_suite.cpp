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
        { 8u, 2u }, { 9u, 2u }, { 10u, 2u }, { 11u, 2u },
        { 12u, 2u }, { 16u, 2u }, { 17u, 2u }, { 18u, 2u },
        { 64u, 2u }, { 128u, 2u }, { std::numeric_limits<std::uint32_t>::max(), 2u } };
    for (const auto& limit : limits)
    {
        TEST_EXPECT(ctx, host::worker_count_limit(limit.hardware) == limit.workers);
    }
    TEST_EXPECT(ctx, host::k_default_worker_count == 2u);
    TEST_EXPECT(ctx, host::k_max_worker_count == 2u);
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

}   //  namespace

int run_host_workers_tests()
{
    TTestContext ctx;
    test_hardware_limits(ctx);
    test_count_parsing(ctx);
    std::cout << "HostWorkers: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
