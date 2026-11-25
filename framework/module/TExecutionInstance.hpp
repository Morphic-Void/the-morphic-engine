
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TExecutionInstance.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Nov 26
//
//  Single-allocation execution objects carried through module-local opaque handles.
//  The caller establishes quiescence before destruction. A handle is neither a
//  T pointer nor a validated registry key; only its creating specialisation may
//  access or destroy it, in the originating module while that module is loaded.

#pragma once

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "foundation/memory/memory_token.hpp"

namespace modules
{

template<typename T>
class TExecutionInstance final
{
public:
    TExecutionInstance() = delete;

    template<typename... TArgs>
    [[nodiscard]] static void* create(TArgs&&... args) noexcept;

    template<typename... TArgs>
    [[nodiscard]] static void* create_in(memory::CMemoryContext& context, TArgs&&... args) noexcept;

    //  Null yields null. A non-null handle must still be live and of this type.
    [[nodiscard]] static T* get(void* const handle) noexcept;

    //  Null is harmless. A non-null handle is consumed exactly once.
    static void destroy(void* const handle) noexcept;

private:
    struct SBlock
    {
        template<typename... TArgs>
        explicit SBlock(memory::CMemoryToken&& storage, TArgs&&... args) noexcept
            : allocation{ std::move(storage) }, value(std::forward<TArgs>(args)...) {}

        memory::CMemoryToken allocation;
        T value;
    };
};

template<typename T>
template<typename... TArgs>
void* TExecutionInstance<T>::create(TArgs&&... args) noexcept
{
    memory::CMemoryContext* const context = memory::get_ambient_memory_context();
    return context ? create_in(*context, std::forward<TArgs>(args)...) : nullptr;
}

template<typename T>
template<typename... TArgs>
void* TExecutionInstance<T>::create_in(memory::CMemoryContext& context, TArgs&&... args) noexcept
{
    static_assert((!std::is_array_v<T> && !std::is_reference_v<T> &&
        !std::is_const_v<T> && !std::is_volatile_v<T>), "Execution instances require an unqualified object type.");
    static_assert(std::is_nothrow_constructible_v<T, TArgs&&...>, "Execution construction must not throw.");
    static_assert(std::is_nothrow_destructible_v<T>, "Execution destruction must not throw.");
    static_assert((sizeof(SBlock) <= memory::k_byte_size_ceiling), "Execution storage exceeds the byte ceiling.");

    memory::CMemoryToken allocation{ 1u, alignof(SBlock), &context };
    if (!allocation.allocate(sizeof(SBlock))) return nullptr;
    void* const storage = allocation.data();
    return ::new (storage) SBlock(std::move(allocation), std::forward<TArgs>(args)...);
}

template<typename T>
T* TExecutionInstance<T>::get(void* const handle) noexcept
{
    return handle ? std::addressof(static_cast<SBlock*>(handle)->value) : nullptr;
}

template<typename T>
void TExecutionInstance<T>::destroy(void* const handle) noexcept
{
    if (!handle) return;
    SBlock* const block = static_cast<SBlock*>(handle);
    //  Keep storage and its original attribution alive through T's destructor.
    memory::CMemoryToken allocation{ std::move(block->allocation) };
    block->~SBlock();
}

}   //  namespace modules
