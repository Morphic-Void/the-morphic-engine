
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    local_type_registry.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26
//
//  Immutable component-local type definition tables and validated lookup.

#pragma once

#ifndef LOCAL_TYPE_REGISTRY_HPP_INCLUDED
#define LOCAL_TYPE_REGISTRY_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "foundation/system/system_ids.hpp"

namespace local_type_registry
{

inline constexpr std::size_t k_short_name_capacity = 16u;

struct SLocalTypeName
{
    char bytes[k_short_name_capacity]{};
};

template<std::size_t N>
constexpr bool is_valid_name_literal(const char (&name)[N]) noexcept
{
    return (N >= 2u) && (N <= k_short_name_capacity) && (name[N - 1u] == 0);
}

template<std::size_t N>
constexpr SLocalTypeName make_name(const char (&name)[N]) noexcept
{
    static_assert((N >= 2u), "A local type name must not be empty.");
    static_assert((N <= k_short_name_capacity), "A local type name is limited to 15 bytes plus its terminator.");

    SLocalTypeName result{};
    for (std::size_t index = 0u; index < N; ++index)
    {
        result.bytes[index] = name[index];
    }
    return result;
}

struct SLocalTypeRegistration
{
    local_type_id id{};
    local_type_ids::index_type index{ local_type_ids::ops::k_invalid_index };
    SLocalTypeName short_name{};
};

struct SLocalTypeRegistryView
{
    const SLocalTypeRegistration* types{ nullptr };
    std::uint32_t type_count{ 0u };
};

static_assert(std::is_standard_layout_v<SLocalTypeName> && std::is_trivially_copyable_v<SLocalTypeName>);
static_assert(sizeof(SLocalTypeName) == k_short_name_capacity);
static_assert(std::is_standard_layout_v<SLocalTypeRegistration> && std::is_trivially_copyable_v<SLocalTypeRegistration>);
static_assert(std::is_standard_layout_v<SLocalTypeRegistryView> && std::is_trivially_copyable_v<SLocalTypeRegistryView>);
static_assert(sizeof(SLocalTypeRegistryView) == (sizeof(void*) * 2u));

[[nodiscard]] bool validate_view(const SLocalTypeRegistryView& view) noexcept;
[[nodiscard]] bool install_view(const SLocalTypeRegistryView& view) noexcept;
[[nodiscard]] bool view_is_installed() noexcept;
[[nodiscard]] const SLocalTypeRegistryView* installed_view() noexcept;

[[nodiscard]] const SLocalTypeRegistration* find_type(const SLocalTypeRegistryView* const view, const local_type_id id) noexcept;
[[nodiscard]] const SLocalTypeRegistration* find_type(const local_type_id id) noexcept;
[[nodiscard]] const SLocalTypeName* lookup_name(const local_type_id id) noexcept;

}   //  namespace local_type_registry

#endif  //  LOCAL_TYPE_REGISTRY_HPP_INCLUDED
