
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    test_allocator.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#include "tests/support/test_allocator.hpp"

#include <new>

namespace tests
{

void* MV_STD_ABI_CALL allocate_with_failure(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& fixture = *static_cast<TAllocationFailureFixture*>(state);
    fixture.bytes += bytes;
    if (bytes > fixture.largest) fixture.largest = bytes;
    const std::size_t attempt = fixture.attempts++;
    if (fixture.reject_all || (attempt == fixture.fail_on))
    {
        fixture.failed = true;
        return nullptr;
    }
    return allocate_test_memory(nullptr, alignment, bytes);
}

void* MV_STD_ABI_CALL allocate_with_allowance(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& fixture = *static_cast<TAllocationAllowanceFixture*>(state);
    if (fixture.remaining == 0u) return nullptr;
    --fixture.remaining;
    return allocate_test_memory(nullptr, alignment, bytes);
}

void* MV_STD_ABI_CALL allocate_test_memory(
    void* const state,
    const std::size_t alignment,
    const std::size_t bytes) noexcept
{
    if ((state != nullptr) &&
        static_cast<TAllocatorFixture*>(state)->reject_allocation)
    {
        return nullptr;
    }
    return ::operator new[](bytes, std::align_val_t{ alignment }, std::nothrow);
}

bool MV_STD_ABI_CALL deallocate_test_memory(
    void*, const std::size_t alignment, void* const pointer) noexcept
{
    if (pointer == nullptr)
    {
        return false;
    }
    ::operator delete[](pointer, std::align_val_t{ alignment });
    return true;
}

}   //  namespace tests
