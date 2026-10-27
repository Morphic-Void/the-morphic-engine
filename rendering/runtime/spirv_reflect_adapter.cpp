
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    spirv_reflect_adapter.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Private allocation routing for unmodified SPIRV-Reflect. One fixed arena
//  keeps vendor addresses stable until extraction and destruction complete.
//  Initial allocation can fail normally; exhaustion during vendor execution
//  terminates with a best-effort diagnostic. Vendor frees are intentionally inert.

#include "memory/memory_context.hpp"
#include <stdlib.h>
#include <cstring>
#include <cstddef>
#include "rendering/runtime/shader_reflection_spirv.hpp"
#if defined(WIN32)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

namespace rendering::shader_reflection::detail
{

static thread_local SAllocationOwner* t_allocations{ nullptr };

//  Empirical allowance, not a bound on arbitrary vendor expansion. Consumption
//  includes alignment and temporary allocations even after vendor free calls.
static constexpr std::size_t k_arena_allowance = 64u * 1024u;
static constexpr std::size_t k_arena_multiplier = 64u;

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

bool SAllocationOwner::capacity_for(const std::size_t input_bytes, std::size_t& capacity) noexcept
{
    capacity = 0u;
    if (input_bytes > ((memory::k_byte_size_ceiling - k_arena_allowance) / k_arena_multiplier))
    {
        return false;
    }
    capacity = k_arena_allowance + (input_bytes * k_arena_multiplier);
    return true;
}

[[noreturn]] void SAllocationOwner::exhausted(const std::size_t count, const std::size_t bytes) const noexcept
{
    //  Bound identity text so numeric evidence fits the fixed panic buffer.
    MV_PANIC("SPIRV-Reflect arena exhausted: identity=%.*s identity_bytes=%zu input=%zu capacity=%zu consumed=%zu requested=%zu x %zu bytes",
        static_cast<int>(std::min(identity.length(), std::size_t{ 512u })),
        identity.cstring() != nullptr ? identity.cstring() : "", identity.length(),
        input_bytes, capacity, consumed, count, bytes);
}

void* SAllocationOwner::allocate(const std::size_t size) noexcept
{
    constexpr auto alignment = alignof(std::max_align_t);
    const auto padding = (alignment - (consumed % alignment)) % alignment;
    const auto bytes = std::max(size, std::size_t{ 1u });
    if (!arena.is_ready() || (consumed > capacity) || (padding > (capacity - consumed)))
    {
        exhausted(1u, size);
    }
    const auto offset = consumed + padding;
    if (bytes > (capacity - offset))
    {
        exhausted(1u, size);
    }
    auto* const address = arena.data() + offset;
    consumed = offset + bytes;
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

static void reflect_free(void*) noexcept
{
    //  No per-allocation tracking or reclamation. The operation owns the arena.
}

static void breadcrumb(const SAllocationOwner& owner, const char* const phase, const SpvReflectResult result) noexcept
{
    //  Direct reporting deliberately bypasses informational macro filtering.
    //  Fixed formatting storage and immediate flush do not guarantee delivery.
    (void)debug_system::report_immediate(debug_system::EEventLevel::info, MV_INTERNAL_USAGE_POINT,
        "SPIRV-Reflect %s: identity=%.*s identity_bytes=%zu input=%zu capacity=%zu consumed=%zu result=%d",
        phase, static_cast<int>(std::min(owner.identity.length(), std::size_t{ 512u })),
        owner.identity.cstring() != nullptr ? owner.identity.cstring() : "", owner.identity.length(),
        owner.input_bytes, owner.capacity, owner.consumed, static_cast<int>(result));
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
    spvReflectDestroyShaderModule(&m_module);
    breadcrumb(m_allocations, "destroy complete", SPV_REFLECT_RESULT_SUCCESS);
    m_allocations.arena.deallocate();
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
    m_allocations.consumed = 0u;
    if (!SAllocationOwner::capacity_for(bytes, m_allocations.capacity))
    {
        allocation_size_rejected = true;
        return SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED;
    }
    CActiveOwner scope{ m_allocations };
    if (!m_allocations.arena.allocate(m_allocations.capacity, alignof(std::max_align_t)))
    {
        return SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED;
    }
    m_allocations.identity = identity;
    breadcrumb(m_allocations, "create begin", SPV_REFLECT_RESULT_SUCCESS);
    const auto result = CreateShaderModule(0u, bytes, words, &m_module);
    breadcrumb(m_allocations, "create complete", result);
    if (result == SPV_REFLECT_RESULT_SUCCESS)
    {
        m_created = true;
    }
    else
    {
        //  Reclaim even allocations abandoned by ordinary vendor error returns.
        //  This does not repair unsafe non-allocation parser failure paths.
        m_allocations.arena.deallocate();
        std::memset(&m_module, 0, sizeof(m_module));
        m_allocations.identity = {};
    }
    return result;
}

}   //  namespace rendering::shader_reflection::detail
