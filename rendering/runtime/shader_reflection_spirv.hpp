
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
#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "external/SPIRV-Reflect/spirv_reflect.h"

namespace rendering::shader_reflection::detail
{

class CModule;

struct SAllocationOwner
{
    CModule* module;
    CByteBuffer arena;
    CStringView identity;
    std::size_t input_bytes{ 0u };
    std::size_t capacity{ 0u };
    std::size_t consumed{ 0u };
    [[nodiscard]] static bool capacity_for(const std::size_t input_bytes, std::size_t& capacity) noexcept;
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
        bool& allocation_size_rejected, const CStringView identity = {}) noexcept;
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
