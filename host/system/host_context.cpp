
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_context.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    13 July 26
//
//  Owns and installs the Host allocator and memory context.
// 
//  This file should not be included in modules/DLLs.
//
//  Design constraints
//  ------------------
//  - Requires C++17 or later.
//  - No exceptions.
//  - Allocation uses nothrow new[].

#include <cstddef>      //  std::size_t
#include <new>          //  std::nothrow, aligned operator new[]/delete[]

#include "system/host_context.hpp"
#include <local_type_ids.hpp>
#include "system/system_id_definitions.hpp"
#include "platform/platform_defines.hpp"
#include "memory/memory_policies.hpp"
#include "memory/memory_context.hpp"
#include "platform/threading/thread_naming.hpp"
#include "system/erased_owner_operations.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"
#include "system/system_ids.hpp"

namespace host
{

//==============================================================================
//  The host memory allocation functions
//==============================================================================

static void* MV_STD_ABI_CALL host_allocate(void* const context, const std::size_t align, const std::size_t bytes) noexcept
{
    (void)context;
    if (bytes != 0u)
    {
        return ::operator new[](bytes, std::align_val_t{ align }, std::nothrow);
    }
    return nullptr;

}

static bool MV_STD_ABI_CALL host_deallocate(void* const context, const std::size_t align, void* const ptr) noexcept
{
    (void)context;
    (void)align;
    if (ptr != nullptr)
    {
        ::operator delete[](ptr, std::align_val_t{ align });
        return true;
    }
    return false;
}

//==============================================================================
//  Host context lifetime and installation
//==============================================================================

CHostContext::CHostContext() noexcept
    : m_allocator{ nullptr, &host_allocate, &host_deallocate, system_ids::host }
    , m_memory{ m_allocator, system_ids::host }
{
    //  Even failed registry installation leaves dependent empty containers with
    //  a valid memory context through construction and orderly destruction.
    m_previous_module_memory = memory::set_module_memory_context(&m_memory);
    m_previous_thread_memory = memory::set_thread_memory_context(nullptr);
    m_previous_module = system_context::set_ambient_module_id(module_ids::executable);
    m_previous_thread = system_context::set_ambient_thread_id(thread_ids::host);

    const erased_owner_operations::SRegistryView owner_operations{
        erased_owner_operations::system_operations_view(),
        erased_owner_operations::local_operations_view() };
    if (!system_id_registry::install_view(system_registry_view()) ||
        !local_type_registry::install_view(local_type_registry::component_view()) ||
        !erased_owner_operations::install_view(owner_operations))
    {
        return;
    }

    const char* const thread_name = system_id_registry::lookup_thread_name(thread_ids::host);
    if (thread_name != nullptr)
    {
        (void)platform::threading::set_current_thread_name(thread_name);
    }
    m_installed = memory::get_ambient_memory_context() == &m_memory;
}

CHostContext::~CHostContext() noexcept
{
    (void)memory::set_thread_memory_context(m_previous_thread_memory);
    (void)memory::set_module_memory_context(m_previous_module_memory);
    (void)system_context::set_ambient_thread_id(m_previous_thread);
    (void)system_context::set_ambient_module_id(m_previous_module);
}

bool CHostContext::is_installed() const noexcept
{
    return m_installed;
}

memory::CMemoryAllocator& CHostContext::allocator() noexcept
{
    return m_allocator;
}

}   //  namespace host
