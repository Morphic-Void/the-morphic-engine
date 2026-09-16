//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#pragma once

#include "memory/memory_context.hpp"

namespace memory
{

struct SMemoryContextTestAccess
{
    //  Isolated fixture setup/cleanup only; never seed a concurrently used context.
    static void seed(CMemoryContext& context, const std::uint32_t count, const std::uint64_t bytes) noexcept
    {
        context.m_live_allocations.store(count, std::memory_order_relaxed);
        context.m_live_allocated_bytes.store(bytes, std::memory_order_relaxed);
    }

    static void add(CMemoryContext& context, const std::uint32_t count, const std::uint64_t bytes) noexcept
    {
        context.add(count, bytes);
    }

    static void sub(CMemoryContext& context, const std::uint32_t count, const std::uint64_t bytes) noexcept
    {
        context.sub(count, bytes);
    }
};

}   //  namespace memory
