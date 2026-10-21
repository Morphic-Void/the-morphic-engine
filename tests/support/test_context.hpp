//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    test_context.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#pragma once

#ifndef MORPHIC_TEST_CONTEXT_HPP_INCLUDED
#define MORPHIC_TEST_CONTEXT_HPP_INCLUDED

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace tests
{

struct TTestContext
{
    void expect(
        const bool condition,
        const char* const expression,
        const int line,
        const char* const file = nullptr)
    {
        if (condition)
        {
            ++passed;
            return;
        }
        fail(expression, file, line);
    }

    void fail(
        const char* const expression,
        const char* const file,
        const int line,
        const std::string& message = {},
        const char* const case_name = nullptr)
    {
        ++failed;
        if ((file != nullptr) && (file[0] != 0))
        {
            std::cerr << file << '(' << line << "): FAIL: ";
        }
        else
        {
            std::cerr << "Test failure at line " << line << ": ";
        }
        if ((case_name != nullptr) && (case_name[0] != 0))
        {
            std::cerr << '[' << case_name << "] ";
        }
        std::cerr << expression;
        if (!message.empty())
        {
            std::cerr << " : " << message;
        }
        std::cerr << '\n';
    }

    void pass() noexcept { ++passed; }
    [[nodiscard]] int exit_code() const noexcept
    {
        return (failed == 0u) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    [[nodiscard]] int report(const char* const suite_name) const
    {
        std::cout << suite_name << ": " << passed << " passed, " << failed << " failed\n";
        return exit_code();
    }

    std::uint32_t passed{ 0u };
    std::uint32_t failed{ 0u };
};

}   //  namespace tests

#define TEST_EXPECT(ctx, expression) \
    (ctx).expect(!!(expression), #expression, __LINE__, __FILE__)

#define TEST_EXPECT_TRUE(ctx, expression) \
    do { if (expression) { (ctx).pass(); } else { (ctx).fail(#expression, __FILE__, __LINE__); } } while (false)

#define TEST_EXPECT_FALSE(ctx, expression) \
    do { if (!(expression)) { (ctx).pass(); } else { (ctx).fail("!(" #expression ")", __FILE__, __LINE__); } } while (false)

#define TEST_EXPECT_EQ(ctx, lhs, rhs) \
    do { \
        const auto test_lhs_value = (lhs); \
        const auto test_rhs_value = (rhs); \
        if (test_lhs_value == test_rhs_value) { (ctx).pass(); } \
        else { (ctx).fail(#lhs " == " #rhs, __FILE__, __LINE__); } \
    } while (false)

#define TEST_CASE_EXPECT_TRUE(ctx, case_name, expression) \
    do { if (expression) { (ctx).pass(); } else { (ctx).fail(#expression, __FILE__, __LINE__, {}, (case_name)); } } while (false)

#define TEST_CASE_EXPECT_FALSE(ctx, case_name, expression) \
    do { if (!(expression)) { (ctx).pass(); } else { (ctx).fail("!(" #expression ")", __FILE__, __LINE__, {}, (case_name)); } } while (false)

#define TEST_CASE_EXPECT_EQ(ctx, case_name, lhs, rhs) \
    do { \
        const auto test_lhs_value = (lhs); \
        const auto test_rhs_value = (rhs); \
        if (test_lhs_value == test_rhs_value) { (ctx).pass(); } \
        else { (ctx).fail(#lhs " == " #rhs, __FILE__, __LINE__, {}, (case_name)); } \
    } while (false)

#endif  //  MORPHIC_TEST_CONTEXT_HPP_INCLUDED
