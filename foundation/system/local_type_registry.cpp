
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    local_type_registry.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26
//
//  Component-local installed type-registry view and validated lookup.

#include "system/local_type_registry.hpp"

namespace local_type_registry
{

static SLocalTypeRegistryView s_installed_view{};
static bool s_view_installed{ false };

[[nodiscard]] static bool validate_name(const SLocalTypeName& name) noexcept
{
    if (name.bytes[0] == 0)
    {
        return false;
    }

    std::size_t terminator = 0u;
    while ((terminator < k_short_name_capacity) && (name.bytes[terminator] != 0))
    {
        ++terminator;
    }
    if (terminator >= k_short_name_capacity)
    {
        return false;
    }

    for (std::size_t index = terminator + 1u; index < k_short_name_capacity; ++index)
    {
        if (name.bytes[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool validate_view(const SLocalTypeRegistryView& view) noexcept
{
    if ((view.type_count > local_type_ids::ops::k_capacity) ||
        ((view.type_count == 0u) != (view.types == nullptr)))
    {
        return false;
    }

    for (std::uint32_t index = 0u; index < view.type_count; ++index)
    {
        const SLocalTypeRegistration& registration = view.types[index];
        if (!local_type_ids::ops::is_valid_id(registration.id) ||
            !local_type_ids::ops::is_valid_index(registration.index) ||
            (registration.index != index) ||
            (local_type_ids::ops::encode_id(registration.index) != registration.id) ||
            !validate_name(registration.short_name))
        {
            return false;
        }
    }
    return true;
}

bool install_view(const SLocalTypeRegistryView& view) noexcept
{
    if (s_view_installed || !validate_view(view))
    {
        return false;
    }
    s_installed_view = view;
    s_view_installed = true;
    return true;
}

bool view_is_installed() noexcept
{
    return s_view_installed;
}

const SLocalTypeRegistryView* installed_view() noexcept
{
    return s_view_installed ? &s_installed_view : nullptr;
}

const SLocalTypeRegistration* find_type(
    const SLocalTypeRegistryView* const view,
    const local_type_id id) noexcept
{
    if ((view == nullptr) || !local_type_ids::ops::is_valid_id(id) ||
        (view->type_count > local_type_ids::ops::k_capacity) ||
        !((view->type_count == 0u) ? (view->types == nullptr) : (view->types != nullptr)))
    {
        return nullptr;
    }

    const local_type_ids::index_type index = local_type_ids::ops::decode_id(id);
    if (index >= view->type_count)
    {
        return nullptr;
    }

    const SLocalTypeRegistration& registration = view->types[index];
    return (registration.id == id) &&
        (registration.index == index) &&
        validate_name(registration.short_name)
        ? &registration
        : nullptr;
}

const SLocalTypeRegistration* find_type(const local_type_id id) noexcept
{
    return find_type(installed_view(), id);
}

const SLocalTypeName* lookup_name(const local_type_id id) noexcept
{
    const SLocalTypeRegistration* const registration = find_type(id);
    return (registration != nullptr) ? &registration->short_name : nullptr;
}

}   //  namespace local_type_registry
