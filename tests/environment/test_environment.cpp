//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#include "tests/environment/test_environment.hpp"

#include <cstddef>
#include <new>

#include "debug/service.hpp"
#include "memory/memory_context.hpp"
#include "platform/platform_defines.hpp"
#include "platform/threading/thread_naming.hpp"
#include "system/erased_owner_operations.hpp"
#include "system/local_type_registry.hpp"
#include "system/system_context.hpp"
#include "tests/environment/local_type_ids.hpp"

namespace test_environment
{
namespace
{
void* MV_STD_ABI_CALL allocate_memory(
    void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    return (bytes == 0u)
        ? nullptr
        : ::operator new[](bytes, std::align_val_t{ alignment }, std::nothrow);
}

bool MV_STD_ABI_CALL deallocate_memory(
    void*, const std::size_t alignment, void* const pointer) noexcept
{
    if (pointer == nullptr)
    {
        return false;
    }
    ::operator delete[](pointer, std::align_val_t{ alignment });
    return true;
}

memory::CMemoryAllocator s_allocator(
    nullptr, &allocate_memory, &deallocate_memory, system_ids::host);
memory::CMemoryContext s_executable_context(s_allocator, system_ids::host);
memory::CMemoryContext s_executive_context(s_allocator, system_ids::executive);
}

static memory::CMemoryContext s_rendering_context(s_allocator,
    system_ids::ops::make_system_id(module_ids::render_vulkan, thread_ids::rendering));

bool install() noexcept
{
    const erased_owner_operations::SRegistryView owner_operations{
        erased_owner_operations::system_operations_view(),
        erased_owner_operations::local_operations_view()
    };
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
    (void)memory::set_module_memory_context(&s_executable_context);
    return
        (memory::get_ambient_memory_context() == &s_executable_context) &&
        is_clean();
}

bool is_clean() noexcept
{
    return
        (debug_system::get_service() == nullptr) &&
        (system_context::get_ambient_module_id() == module_ids::executable) &&
        (system_context::get_ambient_thread_id() == thread_ids::host) &&
        (memory::get_ambient_memory_context() == &s_executable_context) &&
        s_executable_context.is_attribution_empty() &&
        s_executive_context.is_attribution_empty() &&
        s_rendering_context.is_attribution_empty();
}

memory::CMemoryContext* executable_memory_context() noexcept
{
    return &s_executable_context;
}

memory::CMemoryContext* executive_memory_context() noexcept
{
    return &s_executive_context;
}

memory::CMemoryContext* rendering_memory_context() noexcept
{
    return &s_rendering_context;
}

}   //  namespace test_environment
