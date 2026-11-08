
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    spirv_reflect_adapter.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Private hybrid allocation routing and cleanup auditing for SPIRV-Reflect.
//  Initial allocation can fail normally; allocation failure during vendor execution
//  terminates with a best-effort diagnostic. Teardown sweeps all remaining storage.

#include "memory/memory_context.hpp"
#include <stdlib.h>
#include <cstring>
#include <cstddef>
#include "runtime/shader_reflection_spirv.hpp"
#if defined(WIN32)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

namespace rendering::shader_reflection::detail
{

static thread_local SAllocationOwner* t_allocations{ nullptr };

class CActiveOwner final
{

public:
    explicit CActiveOwner(SAllocationOwner& owner) noexcept
        : m_previous{ std::exchange(t_allocations, &owner) }
    {
    }

    ~CActiveOwner() noexcept
    {
        t_allocations = m_previous;
    }

    CActiveOwner(const CActiveOwner&) = delete;
    CActiveOwner& operator=(const CActiveOwner&) = delete;

private:
    SAllocationOwner* const m_previous;
};

[[noreturn]] void SAllocationOwner::exhausted(const std::size_t count, const std::size_t bytes) const noexcept
{
    //  Bound identity text so numeric evidence fits the fixed panic buffer.
    MV_PANIC("SPIRV-Reflect allocation failed: identity=%.*s identity_bytes=%zu input=%zu requested=%zu x %zu bytes managers=%zu tracked=%zu live=%llu owned=%llu",
        static_cast<int>(std::min(identity.length(), std::size_t{ 512u })),
        identity.cstring() != nullptr ? identity.cstring() : "", identity.length(),
        input_bytes, count, bytes, allocator.manager_count(), allocator.tracked_allocation_count(),
        static_cast<unsigned long long>(allocator.allocated_size()),
        static_cast<unsigned long long>(allocator.total_owned_size()));
}

bool SAllocationOwner::prepare() noexcept
{
    constexpr SHybridConfig config{ { alignof(std::max_align_t), 64u * 1024u }, 16u * 1024u };
    if ((context == nullptr) || !allocator.initialise(config, *context))
    {
        return false;
    }
    //  Materialise the first manager before vendor entry, where failure can be
    //  reported normally. Subsequent vendor allocations must never return null.
    void* const probe = allocator.allocate(1u);
    if (probe == nullptr)
    {
        allocator.deallocate();
        return false;
    }
    (void)allocator.free(probe);
    peak_owned = allocator.total_owned_size();
    return true;
}

void* SAllocationOwner::allocate(const std::size_t size) noexcept
{
    const std::size_t bytes = std::max(size, std::size_t{ 1u });
    void* const address = allocator.allocate(bytes, alignof(std::max_align_t));
    if (address == nullptr)
    {
        exhausted(1u, size);
    }
    peak_live = std::max(peak_live, allocator.allocated_size());
    peak_owned = std::max(peak_owned, allocator.total_owned_size());
    return address;
}

void* SAllocationOwner::allocate_zeroed(const std::size_t count, const std::size_t size) noexcept
{
    if ((size != 0u) && (count > (memory::k_byte_size_ceiling / size)))
    {
        exhausted(count, size);
    }
    const auto bytes = count * size;
    void* const allocation = allocate(bytes);
    std::memset(allocation, 0, bytes);
    return allocation;
}

CHybridAllocator32::EFreeResult SAllocationOwner::free(void* const pointer) noexcept
{
    const auto result = allocator.free(pointer);
    if (result == CHybridAllocator32::EFreeResult::invalid_allocation)
    {
        ++invalid_frees;
    }
    return result;
}

void SAllocationOwner::sweep() noexcept
{
    outstanding_count = allocator.allocation_count();
    outstanding_bytes = allocator.allocated_size();
    if ((invalid_frees != 0u) || (outstanding_count != 0u))
    {
        (void)debug_system::report_immediate(debug_system::EEventLevel::error, MV_INTERNAL_USAGE_POINT,
            "SPIRV-Reflect allocation audit failed: invalid_frees=%llu outstanding=%llu bytes=%llu",
            static_cast<unsigned long long>(invalid_frees), static_cast<unsigned long long>(outstanding_count),
            static_cast<unsigned long long>(outstanding_bytes));
    }
    allocator.deallocate();
}

static void* reflect_malloc(const std::size_t size) noexcept
{
    if (t_allocations == nullptr)
    {
        MV_PANIC("SPIRV-Reflect allocation outside an active module");
    }
    return t_allocations->allocate(size);
}

static void* reflect_calloc(const std::size_t count, const std::size_t size) noexcept
{
    if (t_allocations == nullptr)
    {
        MV_PANIC("SPIRV-Reflect allocation outside an active module");
    }
    return t_allocations->allocate_zeroed(count, size);
}

static void reflect_free(void* const pointer) noexcept
{
    if (t_allocations == nullptr)
    {
        if (pointer == nullptr) return;
        MV_PANIC("SPIRV-Reflect free outside an active module");
    }
    (void)t_allocations->free(pointer);
}

static void breadcrumb(const SAllocationOwner& owner, const char* const phase, const SpvReflectResult result) noexcept
{
    //  Direct reporting deliberately bypasses informational macro filtering.
    //  Fixed formatting storage and immediate flush do not guarantee delivery.
    (void)debug_system::report_immediate(debug_system::EEventLevel::info, MV_INTERNAL_USAGE_POINT,
        "SPIRV-Reflect %s: identity=%.*s identity_bytes=%zu input=%zu result=%d managers=%zu tracked=%zu live=%llu owned=%llu",
        phase, static_cast<int>(std::min(owner.identity.length(), std::size_t{ 512u })),
        owner.identity.cstring() != nullptr ? owner.identity.cstring() : "", owner.identity.length(),
        owner.input_bytes, static_cast<int>(result), owner.allocator.manager_count(), owner.allocator.tracked_allocation_count(),
        static_cast<unsigned long long>(owner.allocator.allocated_size()),
        static_cast<unsigned long long>(owner.allocator.total_owned_size()));
}

}   //  namespace rendering::shader_reflection::detail

#undef malloc
#undef calloc
#undef free
#define malloc rendering::shader_reflection::detail::reflect_malloc
#define calloc rendering::shader_reflection::detail::reflect_calloc
#define free rendering::shader_reflection::detail::reflect_free
//  Keep vendor creation entry points private to this TU's integration. The
//  importer uses CModule below, never an uncontained C creation call.
#define spvReflectCreateShaderModule private_spvReflectCreateShaderModule
#define spvReflectCreateShaderModule2 private_spvReflectCreateShaderModule2
#define spvReflectGetShaderModule private_spvReflectGetShaderModule
#include "external/SPIRV-Reflect/spirv_reflect.c"
#undef spvReflectGetShaderModule
#undef spvReflectCreateShaderModule2
#undef spvReflectCreateShaderModule
#undef free
#undef calloc
#undef malloc

namespace rendering::shader_reflection::detail
{

CModule* CModule::active() noexcept
{
    return (t_allocations != nullptr) ? t_allocations->module : nullptr;
}

void CModule::close() noexcept
{
    if (!m_created)
    {
        return;
    }

    CActiveOwner scope{ m_allocations };
    breadcrumb(m_allocations, "destroy begin", SPV_REFLECT_RESULT_SUCCESS);
    m_allocations.vendor_active = true;
    spvReflectDestroyShaderModule(&m_module);
    m_allocations.vendor_active = false;
    breadcrumb(m_allocations, "destroy complete", SPV_REFLECT_RESULT_SUCCESS);
    m_allocations.sweep();
    std::memset(&m_module, 0, sizeof(m_module));
    m_created = false;
    m_allocations.identity = {};
}

SpvReflectResult CModule::create(const std::size_t bytes, const void* const words,
    bool& allocation_size_rejected, const CStringView identity) noexcept
{
    allocation_size_rejected = false;
    if (m_created)
    {
        return SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR;
    }
    m_allocations.input_bytes = bytes;
    m_allocations.peak_live = 0u;
    m_allocations.peak_owned = 0u;
    m_allocations.invalid_frees = 0u;
    m_allocations.outstanding_count = 0u;
    m_allocations.outstanding_bytes = 0u;
    if (bytes > memory::k_byte_size_ceiling)
    {
        allocation_size_rejected = true;
        return SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED;
    }
    CActiveOwner scope{ m_allocations };
    if (!m_allocations.prepare())
    {
        return SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED;
    }
    m_allocations.identity = identity;
    breadcrumb(m_allocations, "create begin", SPV_REFLECT_RESULT_SUCCESS);
    m_allocations.vendor_active = true;
    const auto result = CreateShaderModule(0u, bytes, words, &m_module);
    m_allocations.vendor_active = false;
    breadcrumb(m_allocations, "create complete", result);
    if (result == SPV_REFLECT_RESULT_SUCCESS)
    {
        m_created = true;
    }
    else
    {
        //  Reclaim even allocations abandoned by ordinary vendor error returns.
        //  This does not repair unsafe non-allocation parser failure paths.
        m_allocations.sweep();
        std::memset(&m_module, 0, sizeof(m_module));
        m_allocations.identity = {};
    }
    return result;
}

}   //  namespace rendering::shader_reflection::detail
