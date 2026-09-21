//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#pragma once

#ifndef MORPHIC_TEST_SCOPES_HPP_INCLUDED
#define MORPHIC_TEST_SCOPES_HPP_INCLUDED

#include "memory/memory_context.hpp"
#include "system/system_context.hpp"
#include "containers/TInstance.hpp"
#include "debug/service.hpp"
#include "tests/support/test_context.hpp"

namespace tests
{

//  Exercise assertion continuations using the debug service's incident counter,
//  with breakpoints disabled as in the debug-service tests. No log worker needed.
class TAssertionTestScope
{
public:
    explicit TAssertionTestScope(TTestContext& ctx) noexcept
        : m_owner(TInstance<debug_system::CDebugServiceState>::create())
    {
        m_installed = m_owner.is_ready() && debug_system::install_service(m_owner.operator->());
        TEST_EXPECT(ctx, m_installed);
        if (m_installed) m_owner->publish_configuration(0u);
    }

    ~TAssertionTestScope() noexcept
    {
        if (m_installed) (void)debug_system::uninstall_service(m_owner.operator->());
    }

    template<typename TOperation>
    void expect_assertion(TTestContext& ctx, TOperation&& operation)
    {
        if (!m_installed) return;
        const std::uint32_t before = m_owner->allocate_incident_id();
        operation();
        const std::uint32_t after = m_owner->allocate_incident_id();
#if MV_DEVELOPMENT_BUILD
        TEST_EXPECT(ctx, after == before + 2u);
#else
        TEST_EXPECT(ctx, after == before + 1u);
#endif
    }

private:
    TInstance<debug_system::CDebugServiceState> m_owner;
    bool m_installed = false;
};

class TMemoryContextScope
{
public:
    explicit TMemoryContextScope(memory::CMemoryContext* const context) noexcept
        : m_previous(memory::set_thread_memory_context(context)) {}
    ~TMemoryContextScope() noexcept
    {
        (void)memory::set_thread_memory_context(m_previous);
    }
    TMemoryContextScope(const TMemoryContextScope&) = delete;
    TMemoryContextScope& operator=(const TMemoryContextScope&) = delete;

private:
    memory::CMemoryContext* m_previous;
};

class TModuleIdScope
{
public:
    explicit TModuleIdScope(const module_ids::id_type id) noexcept
        : m_previous(system_context::set_ambient_module_id(id)) {}
    ~TModuleIdScope() noexcept
    {
        (void)system_context::set_ambient_module_id(m_previous);
    }
    TModuleIdScope(const TModuleIdScope&) = delete;
    TModuleIdScope& operator=(const TModuleIdScope&) = delete;

private:
    module_ids::id_type m_previous;
};

}   //  namespace tests

#endif  //  MORPHIC_TEST_SCOPES_HPP_INCLUDED
