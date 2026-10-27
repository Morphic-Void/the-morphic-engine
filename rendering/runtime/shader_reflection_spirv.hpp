
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_reflection_spirv.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Private rendering boundary; never include from shared Core contracts.

#pragma once

#ifndef RENDERING_RUNTIME_SHADER_REFLECTION_SPIRV_HPP_INCLUDED
#define RENDERING_RUNTIME_SHADER_REFLECTION_SPIRV_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>
#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "external/SPIRV-Reflect/spirv_reflect.h"

namespace rendering::shader_reflection::detail
{

class CModule;

//  Operation-local counters, independent of the vendor's native type layout.
struct SAllocationCounts
{
    std::uint32_t input_bytes{ 0u };
    std::uint32_t instructions{ 0u };
    std::uint32_t id_bound{ 0u };
    std::uint32_t types{ 0u };
    std::uint32_t declared_members{ 0u };
    std::uint32_t expanded_type_members{ 0u };
    std::uint32_t resources{ 0u };
    std::uint32_t push_constant_roots{ 0u };
    std::uint32_t expanded_resource_members{ 0u };
    std::uint32_t interfaces{ 0u };
    std::uint32_t expanded_interface_members{ 0u };
    std::uint32_t entry_points{ 0u };
    std::uint32_t functions{ 0u };
    std::uint32_t parameters{ 0u };
    std::uint32_t calls{ 0u };
    std::uint32_t expanded_entry_calls{ 0u };
    std::uint32_t access_records{ 0u };
    std::uint32_t access_chains{ 0u };
    std::uint32_t access_indices{ 0u };
    std::uint32_t strings{ 0u };
    std::uint32_t cumulative_source_bytes{ 0u };
    std::uint32_t source_allocations{ 0u };
    std::uint32_t specializations{ 0u };
};

struct SAllocationPlan
{
    std::uint32_t estimated_bytes{ 0u };
    std::size_t requested_bytes{ 0u };
    std::size_t reserved_bytes{ 0u };
    std::size_t alignment{ 0u };
};

//  Larger counts cannot contribute to an admitted single arena. Reject rather
//  than clipping; intermediates/aggregate working estimates remain wider.
[[nodiscard]] static inline bool add_count(std::uint32_t& total, const std::uint32_t value) noexcept
{
    if ((total > memory::k_byte_size_ceiling) || (value > (memory::k_byte_size_ceiling - total)))
    {
        return false;
    }
    total += value;
    return true;
}

[[nodiscard]] static inline bool add_size(std::uint64_t& total, const std::uint64_t value) noexcept
{
    if (value > (std::numeric_limits<std::uint64_t>::max() - total))
    {
        return false;
    }
    total += value;
    return true;
}

[[nodiscard]] static inline bool multiply_size(std::uint64_t& value, const std::uint64_t factor) noexcept
{
    if ((factor != 0u) && (value > (std::numeric_limits<std::uint64_t>::max() / factor)))
    {
        return false;
    }
    value *= factor;
    return true;
}

[[nodiscard]] bool plan_allocation(const std::uint64_t estimated_bytes, SAllocationPlan& plan) noexcept;
[[nodiscard]] bool estimate_baseline(const SAllocationCounts& counts, SAllocationPlan& plan) noexcept;
[[nodiscard]] bool estimate_allocation(const SAllocationCounts& counts, SAllocationPlan& plan) noexcept;

struct SAllocationOwner
{
    CModule* module;
    CByteBuffer arena;
    CStringView identity;
    SAllocationPlan plan;
    std::size_t input_bytes{ 0u };
    std::size_t capacity{ 0u };
    std::size_t consumed{ 0u };
    [[nodiscard]] void* allocate(const std::size_t bytes) noexcept;
    [[nodiscard]] void* allocate_zeroed(const std::size_t count, const std::size_t bytes) noexcept;
    [[noreturn]] void exhausted(const std::size_t count, const std::size_t bytes) const noexcept;
};

//  One fixed arena owns all vendor storage through creation and extraction.
//  Vendor frees do nothing; close or destruction reclaims the entire arena.
//  The final engine schema copies its data and does not borrow this owner.
class CModule final
{

public:
    CModule() noexcept = default;
    //  Fallback only: release the arena, never call vendor code here.
    ~CModule() noexcept = default;
    CModule(const CModule&) = delete;
    CModule& operator=(const CModule&) = delete;
    CModule(CModule&&) = delete;
    CModule& operator=(CModule&&) = delete;

    //  Initial arena allocation is recoverable, before vendor entry. Exhaustion
    //  during reflection terminates; vendor allocations must never return null.
    //  Identity is borrowed until close, including synchronous log calls.
    [[nodiscard]] SpvReflectResult create(const std::size_t bytes, const void* const words,
        const SAllocationPlan& plan, bool& allocation_size_rejected, const CStringView identity = {}) noexcept;
    void close() noexcept;
    [[nodiscard]] std::size_t arena_capacity() const noexcept
    {
        return m_allocations.capacity;
    }
    [[nodiscard]] std::size_t arena_consumed() const noexcept
    {
        return m_allocations.consumed;
    }

    [[nodiscard]] const SpvReflectShaderModule& value() const noexcept
    {
        return m_module;
    }

private:
    friend struct SModuleTestAccess;
    [[nodiscard]] static CModule* active() noexcept;
    SAllocationOwner m_allocations{ this };
    SpvReflectShaderModule m_module{};
    bool m_created{ false };
};

}   //  namespace rendering::shader_reflection::detail

#endif   //  RENDERING_RUNTIME_SHADER_REFLECTION_SPIRV_HPP_INCLUDED
