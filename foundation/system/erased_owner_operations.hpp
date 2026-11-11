
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner_operations.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Aug 26
//
//  Component-local CErasedOwner runtime-operation authority.

#pragma once

#ifndef ERASED_OWNER_OPERATIONS_HPP_INCLUDED
#define ERASED_OWNER_OPERATIONS_HPP_INCLUDED

#include <cstdint>
#include <type_traits>

#include "foundation/system/system_ids.hpp"

namespace memory
{
class CMemoryContext;
}

namespace erased_owner_operations
{

using FDestroy = void(*)(void* const payload) noexcept;
using FValidateMemorySource = bool(*)(const void* const payload, memory::CMemoryContext* const source) noexcept;
using FMemoryAllocationCount = std::uint32_t(*)(const void* const payload) noexcept;
using FMemoryAllocationSize = std::uint64_t(*)(const void* const payload) noexcept;
using FCanReattributeTo = bool(*)(const void* const payload, memory::CMemoryContext* const target) noexcept;
using FReplaceMemoryContext = void(*)(void* const payload, memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

struct SOperations
{
    FDestroy destroy{ nullptr };
    FValidateMemorySource validate_memory_source{ nullptr };
    FMemoryAllocationCount memory_allocation_count{ nullptr };
    FMemoryAllocationSize memory_allocation_size{ nullptr };
    FCanReattributeTo can_reattribute_to{ nullptr };
    FReplaceMemoryContext replace_memory_context{ nullptr };

    [[nodiscard]] constexpr bool is_complete() const noexcept;

    [[nodiscard]] constexpr bool is_empty() const noexcept;
};

struct SRegistration
{
    type_id identity{ type_ids::undefined };
    SOperations operations{};
};

struct SCategoryView
{
    const SRegistration* registrations{ nullptr };
    std::uint32_t count{ 0u };
};

struct SRegistryView
{
    SCategoryView system{};
    SCategoryView local{};
};

//==============================================================================
//  SOperations out of class function bodies
//==============================================================================

constexpr bool SOperations::is_complete() const noexcept
{
    return
        (destroy != nullptr) &&
        (validate_memory_source != nullptr) &&
        (memory_allocation_count != nullptr) &&
        (memory_allocation_size != nullptr) &&
        (can_reattribute_to != nullptr) &&
        (replace_memory_context != nullptr);
}

constexpr bool SOperations::is_empty() const noexcept
{
    return
        (destroy == nullptr) &&
        (validate_memory_source == nullptr) &&
        (memory_allocation_count == nullptr) &&
        (memory_allocation_size == nullptr) &&
        (can_reattribute_to == nullptr) &&
        (replace_memory_context == nullptr);
}

static_assert(std::is_standard_layout_v<SOperations> && std::is_trivially_copyable_v<SOperations>);
static_assert(sizeof(SOperations) == (sizeof(void*) * 6u));
static_assert(std::is_standard_layout_v<SRegistration> && std::is_trivially_copyable_v<SRegistration>);
static_assert(std::is_standard_layout_v<SCategoryView> && std::is_trivially_copyable_v<SCategoryView>);
static_assert(sizeof(SCategoryView) == (sizeof(void*) * 2u));
static_assert(std::is_standard_layout_v<SRegistryView> && std::is_trivially_copyable_v<SRegistryView>);
static_assert(sizeof(SRegistryView) == (sizeof(void*) * 4u));

[[nodiscard]] bool validate_view(const SRegistryView& view) noexcept;
//  Installation is one-shot. There is deliberately no uninstall operation:
//  component-local authority remains available for the component lifetime.
[[nodiscard]] bool install_view(const SRegistryView& view) noexcept;
[[nodiscard]] bool view_is_installed() noexcept;
[[nodiscard]] const SRegistryView* installed_view() noexcept;

[[nodiscard]] const SRegistration* find(const SRegistryView* const view, const type_id identity) noexcept;
[[nodiscard]] const SRegistration* find(const type_id identity) noexcept;

template<typename T>
struct TDefaultOperationsFactory;

template<typename T, auto Member>
struct TNestedOperationsFactory;

}   //  namespace erased_owner_operations

#endif  //  ERASED_OWNER_OPERATIONS_HPP_INCLUDED
