
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_context.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    13 July 26
//
//  Installs the host context and owns component memory contexts.
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

#include "host/system/host_context.hpp"
#include "host/module/types/local_type_ids.hpp"
#include "host/system/system_id_definitions.hpp"
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
//  The host memory allocator and component memory contexts
//==============================================================================

static memory::CMemoryAllocator s_host_memory_allocator(nullptr, &host_allocate, &host_deallocate, system_ids::host);

static memory::CMemoryContext s_host_memory_context(s_host_memory_allocator, system_ids::host);

//==============================================================================
//  Host context installation
//==============================================================================

bool host_context_install() noexcept
{
    const erased_owner_operations::SRegistryView owner_operations{
        erased_owner_operations::system_operations_view(),
        erased_owner_operations::local_operations_view() };
    if (!system_id_registry::install_view(system_registry_view()) ||
        !local_type_registry::install_view(local_type_registry::component_view()) ||
        !erased_owner_operations::install_view(owner_operations))
    {
        return false;
    }

    (void)system_context::set_ambient_module_id(module_ids::executable);
    (void)system_context::set_ambient_thread_id(thread_ids::host);
    const char* const thread_name = system_id_registry::lookup_thread_name(thread_ids::host);
    if (thread_name != nullptr)
    {
        (void)platform::threading::set_current_thread_name(thread_name);
    }
    (void)memory::set_module_memory_context(&s_host_memory_context);
    return memory::get_ambient_memory_context() == &s_host_memory_context;
}

memory::CMemoryContext* host_memory_context() noexcept
{
    return &s_host_memory_context;
}

memory::CMemoryAllocator& host_memory_allocator() noexcept
{
    return s_host_memory_allocator;
}

}   //  namespace host
