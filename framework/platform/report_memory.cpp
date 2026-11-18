
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_memory.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Charges conditioned byte counts, including alignment and allocation bookkeeping.

#include "framework/platform/report_memory.hpp"

#include <algorithm>
#include <cstring>

namespace platform::reports
{

struct SAllocationHeader { void* base; std::size_t alignment, bytes; };

CReportMemory::CReportMemory(const std::uint64_t limit) noexcept :
    m_parent{ memory::get_ambient_memory_context() }, m_allocator{ this, &allocate, &deallocate },
    m_context{ m_allocator }, m_limit{ limit } {}

bool CReportMemory::reserve(const std::uint64_t bytes) noexcept
{
    if (bytes > (m_limit - m_live))
    {
        m_limited = true;
        return false;
    }
    m_live += bytes;
    m_peak = std::max(m_peak, m_live);
    return true;
}

void* MV_STD_ABI_CALL CReportMemory::allocate(void* const context, const std::size_t alignment, const std::size_t bytes) noexcept
{
    CReportMemory& memory = *static_cast<CReportMemory*>(context);
    const std::size_t align = std::max(alignment, alignof(SAllocationHeader));
    const std::size_t prefix = (sizeof(SAllocationHeader) + (align - 1u)) & ~(align - 1u);
    if (!memory.m_parent || (bytes > (SIZE_MAX - prefix)))
    {
        memory.m_allocation_failed = true;
        return nullptr;
    }
    const std::size_t total = memory.m_parent->condition_bytes(align, (bytes + prefix));
    if (!total || !memory.reserve(total))
    {
        memory.m_limited = true;
        return nullptr;
    }
    void* const base = memory.m_parent->allocate(align, total);
    if (!base)
    {
        memory.m_allocation_failed = true;
        memory.m_live -= total;
        return nullptr;
    }
    std::uint8_t* const data = static_cast<std::uint8_t*>(base) + prefix;
    const SAllocationHeader header{ base, align, total };
    std::memcpy((data - sizeof(header)), &header, sizeof(header));
    return data;
}

bool MV_STD_ABI_CALL CReportMemory::deallocate(void* const context, const std::size_t alignment, void* const pointer) noexcept
{
    (void)alignment;
    CReportMemory& memory = *static_cast<CReportMemory*>(context);
    SAllocationHeader header;
    std::memcpy(&header, (static_cast<std::uint8_t*>(pointer) - sizeof(header)), sizeof(header));
    memory.m_parent->deallocate(header.alignment, header.bytes, header.base);
    memory.m_live -= header.bytes;
    return true;
}

}   //  namespace platform::reports
