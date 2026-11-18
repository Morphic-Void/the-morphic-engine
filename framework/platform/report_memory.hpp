
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_memory.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Accounts simultaneous report storage across native, translation and synchronous schema phases.

#pragma once

#include "memory/memory_context.hpp"

namespace platform::reports
{

class CReportMemory
{
public:
    explicit CReportMemory(const std::uint64_t limit) noexcept;
    [[nodiscard]] memory::CMemoryContext& context() noexcept { return m_context; }
    [[nodiscard]] std::uint64_t peak() const noexcept { return m_peak; }
    [[nodiscard]] bool allocation_failed() const noexcept { return m_allocation_failed; }
    [[nodiscard]] bool limited() const noexcept { return m_limited; }
    [[nodiscard]] bool reserve(const std::uint64_t bytes) noexcept;

private:
    static void* MV_STD_ABI_CALL allocate(void* const context, const std::size_t alignment, const std::size_t bytes) noexcept;
    static bool MV_STD_ABI_CALL deallocate(void* const context, const std::size_t alignment, void* const pointer) noexcept;
    memory::CMemoryContext* const m_parent;
    memory::CMemoryAllocator m_allocator;
    memory::CMemoryContext m_context;
    const std::uint64_t m_limit;
    std::uint64_t m_live{}, m_peak{};
    bool m_limited{}, m_allocation_failed{};
};

class CReportMemoryScope
{
public:
    explicit CReportMemoryScope(memory::CMemoryContext& context) noexcept :
        m_previous{ memory::set_thread_memory_context(&context) } {}
    ~CReportMemoryScope() noexcept { (void)memory::set_thread_memory_context(m_previous); }
    CReportMemoryScope(const CReportMemoryScope&) = delete;
    CReportMemoryScope& operator=(const CReportMemoryScope&) = delete;
private:
    memory::CMemoryContext* m_previous;
};

}   //  namespace platform::reports
