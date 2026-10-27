
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    spirv_reflect_adapter.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Private allocation routing for unmodified SPIRV-Reflect. One growing arena
//  keeps vendor addresses stable until extraction and destruction complete.
//  Initial allocation can fail normally; allocation failure during vendor execution
//  terminates with a best-effort diagnostic. Vendor frees are intentionally inert.

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

//  Empirical allowance, not a bound on arbitrary vendor expansion. Consumption
//  includes alignment and temporary allocations even after vendor free calls.
static constexpr std::size_t k_arena_allowance = 64u * 1024u;
static constexpr std::size_t k_arena_multiplier = 2u;

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

bool plan_allocation(const std::uint64_t estimated_bytes, SAllocationPlan& plan) noexcept
{
    plan = {};
    if (estimated_bytes > ((memory::k_byte_size_ceiling - k_arena_allowance) / k_arena_multiplier))
    {
        return false;
    }
    const auto requested = k_arena_allowance + (static_cast<std::size_t>(estimated_bytes) * k_arena_multiplier);
    const auto alignment = memory::condition_alignment(alignof(std::max_align_t));
    const auto conditioned = memory::condition_bytes(alignment, requested);
    if (conditioned == 0u)
    {
        return false;
    }
    const auto reserved = bit_ops::round_up_to_pow2(conditioned);
    plan = { static_cast<std::uint32_t>(estimated_bytes), requested, reserved, alignment };
    return true;
}

[[noreturn]] void SAllocationOwner::exhausted(const std::size_t count, const std::size_t bytes) const noexcept
{
    //  Bound identity text so numeric evidence fits the fixed panic buffer.
    MV_PANIC("SPIRV-Reflect arena allocation failed: identity=%.*s identity_bytes=%zu input=%zu capacity=%zu consumed=%zu requested=%zu x %zu bytes estimate=%llu demand=%zu reserved=%zu blocks=%zu block_size=%zu allocated=%llu",
        static_cast<int>(std::min(identity.length(), std::size_t{ 512u })),
        identity.cstring() != nullptr ? identity.cstring() : "", identity.length(),
        input_bytes, capacity, consumed, count, bytes,
        static_cast<unsigned long long>(plan.estimated_bytes), plan.requested_bytes, plan.reserved_bytes,
        arena.buffer_count(), arena.block_size(), static_cast<unsigned long long>(arena.memory_attribution().allocation_size));
}

void* SAllocationOwner::allocate(const std::size_t size) noexcept
{
    constexpr auto alignment = alignof(std::max_align_t);
    const auto bytes = std::max(size, std::size_t{ 1u });
    void* const address = arena.bump(bytes, alignment);
    if (address == nullptr)
    {
        exhausted(1u, size);
    }
    consumed = arena.allocated_size_with_alignment();
    capacity = arena.buffer_count() * arena.block_size();
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
        "SPIRV-Reflect %s: identity=%.*s identity_bytes=%zu input=%zu capacity=%zu consumed=%zu result=%d estimate=%llu demand=%zu reserved=%zu blocks=%zu block_size=%zu allocated=%llu",
        phase, static_cast<int>(std::min(owner.identity.length(), std::size_t{ 512u })),
        owner.identity.cstring() != nullptr ? owner.identity.cstring() : "", owner.identity.length(),
        owner.input_bytes, owner.capacity, owner.consumed, static_cast<int>(result),
        static_cast<unsigned long long>(owner.plan.estimated_bytes), owner.plan.requested_bytes, owner.plan.reserved_bytes,
        owner.arena.buffer_count(), owner.arena.block_size(),
        static_cast<unsigned long long>(owner.arena.memory_attribution().allocation_size));
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

//  Keep the frozen model's native coefficients beside the vendor definitions.
//  Updating SPIRV-Reflect requires auditing its allocation sites and these terms.
class CAllocationEstimate final
{
public:
    [[nodiscard]] bool add(const std::uint64_t count, const std::uint64_t bytes,
        const std::uint64_t copies = 1u) noexcept
    {
        auto term = count;
        return multiply_size(term, bytes) && multiply_size(term, copies) && add_size(m_total, term);
    }

    [[nodiscard]] bool finish(SAllocationPlan& plan) const noexcept
    {
        return plan_allocation(m_total, plan);
    }

private:
    std::uint64_t m_total{ 0u };
};

static bool baseline(CAllocationEstimate& estimate, const SAllocationCounts& counts) noexcept
{
    return estimate.add(counts.input_bytes, 1u) &&
        estimate.add(counts.instructions, sizeof(SpvReflectPrvNode)) &&
        estimate.add(counts.id_bound, sizeof(std::uint32_t));
}

bool estimate_baseline(const SAllocationCounts& counts, SAllocationPlan& plan) noexcept
{
    plan = {};
    CAllocationEstimate estimate;
    return baseline(estimate, counts) && estimate.finish(plan);
}

bool estimate_allocation(const SAllocationCounts& counts, SAllocationPlan& plan) noexcept
{
    plan = {};
    CAllocationEstimate estimate;
    constexpr auto padding = alignof(std::max_align_t);
    constexpr auto pointer = sizeof(void*);
    auto passes = static_cast<std::uint64_t>(counts.entry_points);
    if (!add_size(passes, 1u) || !baseline(estimate, counts))
    {
        return false;
    }

    //  Type copies and declared-member metadata, including repeated DAG paths.
    if (!estimate.add(counts.types, sizeof(SpvReflectTypeDescription) + padding) ||
        !estimate.add(counts.expanded_type_members, sizeof(SpvReflectTypeDescription) + padding) ||
        !estimate.add(counts.declared_members, sizeof(SpvReflectPrvDecorations) + pointer + (2u * padding)))
    {
        return false;
    }

    //  All resource/interface roots are charged, even if not statically used.
    if (!estimate.add(counts.resources, sizeof(SpvReflectDescriptorBinding)) ||
        !estimate.add(counts.resources, 64u, passes) ||
        !estimate.add(counts.push_constant_roots, sizeof(SpvReflectBlockVariable) + padding) ||
        !estimate.add(counts.expanded_resource_members, sizeof(SpvReflectBlockVariable) + pointer + (2u * padding)) ||
        !estimate.add(counts.interfaces, sizeof(SpvReflectInterfaceVariable) + pointer + 4u + padding, counts.entry_points) ||
        !estimate.add(counts.expanded_interface_members, sizeof(SpvReflectInterfaceVariable) + pointer + 4u + padding, counts.entry_points))
    {
        return false;
    }

    //  Access records include argument and copy-memory operands. Per-entry
    //  scratch is charged without reconstructing the vendor's reachability sets.
    if (!estimate.add(counts.functions, sizeof(SpvReflectPrvFunction) + (4u * padding)) ||
        !estimate.add(counts.parameters, 4u) ||
        !estimate.add(counts.calls, 4u + pointer) ||
        !estimate.add(counts.expanded_entry_calls, 4u) ||
        !estimate.add(counts.access_records, sizeof(SpvReflectPrvAccessedVariable) + 8u, passes) ||
        !estimate.add(counts.access_chains, sizeof(SpvReflectPrvAccessChain) + padding) ||
        !estimate.add(counts.access_indices, 4u))
    {
        return false;
    }

    //  Embedded source replacements/concatenations remain allocated until close.
    //  Fixed slack is empirical and deliberately unchanged from the frozen model.
    if (!estimate.add(counts.strings, sizeof(SpvReflectPrvString) + padding) ||
        !estimate.add(counts.cumulative_source_bytes, 1u) ||
        !estimate.add(counts.source_allocations, padding) ||
        !estimate.add(1u, padding) ||
        !estimate.add(counts.entry_points, sizeof(SpvReflectEntryPoint) + 64u) ||
        !estimate.add(1u, 4096u) ||
        !estimate.add(counts.instructions, 16u) ||
        !estimate.add(counts.specializations, 64u))
    {
        return false;
    }
    return estimate.finish(plan);
}

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
    const SAllocationPlan& plan, bool& allocation_size_rejected, const CStringView identity) noexcept
{
    allocation_size_rejected = false;
    if (m_created)
    {
        return SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR;
    }
    m_allocations.input_bytes = bytes;
    m_allocations.consumed = 0u;
    m_allocations.capacity = 0u;
    m_allocations.plan = {};
    SAllocationPlan checked;
    if ((bytes > memory::k_byte_size_ceiling) || !plan_allocation(plan.estimated_bytes, checked) ||
        (checked.requested_bytes != plan.requested_bytes) || (checked.reserved_bytes != plan.reserved_bytes) ||
        (checked.alignment != plan.alignment))
    {
        allocation_size_rejected = true;
        return SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED;
    }
    m_allocations.plan = plan;
    CActiveOwner scope{ m_allocations };
    if (!m_allocations.arena.allocate(plan.reserved_bytes, plan.alignment))
    {
        return SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED;
    }
    const auto usable = m_allocations.arena.block_size();
    if ((m_allocations.arena.buffer_count() != 1u) || (usable != plan.reserved_bytes) ||
        (m_allocations.arena.memory_attribution().allocation_size != plan.reserved_bytes))
    {
        m_allocations.arena.deallocate();
        return SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR;
    }
    m_allocations.capacity = usable;
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
