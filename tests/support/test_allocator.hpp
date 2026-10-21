
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    test_allocator.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#pragma once

#ifndef MORPHIC_TEST_ALLOCATOR_HPP_INCLUDED
#define MORPHIC_TEST_ALLOCATOR_HPP_INCLUDED

#include <cstddef>
#include <limits>

#include "platform/platform_defines.hpp"

namespace tests
{

struct TAllocatorFixture
{
    bool reject_allocation{ false };
};

//  fail_on is a zero-based attempt index; the default disables injection.
//  Request statistics include failed attempts. failed records injected failures,
//  rather than failures returned by the underlying allocator.
struct TAllocationFailureFixture
{
    std::size_t attempts{ 0u };
    std::size_t fail_on{ std::numeric_limits<std::size_t>::max() };
    bool failed{ false };
    bool reject_all{ false };
    std::size_t bytes{ 0u };
    std::size_t largest{ 0u };
};

//  Unlike a single failed attempt, exhaustion rejects every subsequent request.
struct TAllocationAllowanceFixture
{
    std::size_t remaining{ std::numeric_limits<std::size_t>::max() };
};

[[nodiscard]] void* MV_STD_ABI_CALL allocate_with_failure(
    void* state, std::size_t alignment, std::size_t bytes) noexcept;
[[nodiscard]] void* MV_STD_ABI_CALL allocate_with_allowance(
    void* state, std::size_t alignment, std::size_t bytes) noexcept;

[[nodiscard]] void* MV_STD_ABI_CALL allocate_test_memory(
    void* state, std::size_t alignment, std::size_t bytes) noexcept;
[[nodiscard]] bool MV_STD_ABI_CALL deallocate_test_memory(
    void* state, std::size_t alignment, void* pointer) noexcept;

}   //  namespace tests

#endif  //  MORPHIC_TEST_ALLOCATOR_HPP_INCLUDED
