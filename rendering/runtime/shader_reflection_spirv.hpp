
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
#include "allocators/THybridAllocator.hpp"
#include "containers/StringBuffers.hpp"
#include "external/SPIRV-Reflect/spirv_reflect.h"

namespace rendering::shader_reflection::detail
{

class CModule;

struct SAllocationOwner
{
    CModule* module;
    //  Preserve construction-time ownership across worker/ambient context changes.
    memory::CMemoryContext* context{ memory::get_ambient_memory_context() };
    CHybridAllocator32 allocator;
    CStringView identity;
    std::size_t input_bytes{ 0u };
    std::uint64_t peak_owned{ 0u }, peak_live{ 0u };
    std::uint64_t invalid_frees{ 0u }, outstanding_count{ 0u }, outstanding_bytes{ 0u };
    bool vendor_active{ false };
    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] void* allocate(const std::size_t bytes) noexcept;
    [[nodiscard]] void* allocate_zeroed(const std::size_t count, const std::size_t bytes) noexcept;
    [[nodiscard]] CHybridAllocator32::EFreeResult free(void* const pointer) noexcept;
    void sweep() noexcept;
    [[noreturn]] void exhausted(const std::size_t count, const std::size_t bytes) const noexcept;
};

//  External allocation metadata validates individual frees; close audits and
//  sweeps remaining ownership independently of vendor cleanup.
//  The final engine schema copies its data and does not borrow this owner.
class CModule final
{

public:
    CModule() noexcept = default;
    //  Fallback only: release ownership, never call vendor code here.
    ~CModule() noexcept = default;
    CModule(const CModule&) = delete;
    CModule& operator=(const CModule&) = delete;
    CModule(CModule&&) = delete;
    CModule& operator=(CModule&&) = delete;

    //  Initial buddy setup is recoverable, before vendor entry. Failed allocations
    //  during reflection terminate; vendor allocations must never return null.
    //  Identity is borrowed until close, including synchronous log calls.
    [[nodiscard]] SpvReflectResult create(const std::size_t bytes, const void* const words,
        bool& allocation_size_rejected, const CStringView identity = {}) noexcept;
    void close() noexcept;
    [[nodiscard]] std::uint64_t peak_owned() const noexcept
    {
        return m_allocations.peak_owned;
    }
    [[nodiscard]] std::uint64_t peak_live() const noexcept
    {
        return m_allocations.peak_live;
    }
    [[nodiscard]] bool audit_clean() const noexcept
    {
        return (m_allocations.invalid_frees == 0u) && (m_allocations.outstanding_count == 0u);
    }
    [[nodiscard]] const SAllocationOwner& allocations() const noexcept { return m_allocations; }

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
