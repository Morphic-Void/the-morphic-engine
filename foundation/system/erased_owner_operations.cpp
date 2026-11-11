
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner_operations.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Aug 26
//
//  Generic installed component-local CErasedOwner operation authority.

#include "system/erased_owner_operations.hpp"

namespace erased_owner_operations
{

namespace internal
{

static SRegistryView s_installed_view{};
static bool s_view_installed{ false };

[[nodiscard]] static bool validate_category_view(const SCategoryView& view, const ETypeIdCategory category) noexcept
{
    if ((view.count > system_type_ids::ops::k_capacity) ||
        ((view.count == 0u) != (view.registrations == nullptr)))
    {
        return false;
    }

    for (std::uint32_t index = 0u; index < view.count; ++index)
    {
        const SRegistration& registration = view.registrations[index];
        if (!registration.identity.is_valid())
        {
            if (!registration.operations.is_empty())
            {
                return false;
            }
            continue;
        }

        if ((registration.identity.category() != category) ||
            !registration.operations.is_complete())
        {
            return false;
        }

        if (category == ETypeIdCategory::system)
        {
            system_type_id identity;
            if (!registration.identity.try_system_type_id(identity) ||
                (system_type_ids::ops::decode_index(system_type_ids::ops::decode_id(identity)) != index))
            {
                return false;
            }
        }
        else
        {
            local_type_id identity;
            if (!registration.identity.try_local_type_id(identity) ||
                (local_type_ids::ops::decode_index(local_type_ids::ops::decode_id(identity)) != index))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] static const SRegistration* find_in_category(const SCategoryView& view, const type_id identity, const std::uint32_t index) noexcept
{
    if ((index >= view.count) || (view.registrations == nullptr))
    {
        return nullptr;
    }

    const SRegistration& registration = view.registrations[index];
    return ((registration.identity == identity) && registration.operations.is_complete())
        ? &registration
        : nullptr;
}

}   //  namespace internal

bool validate_view(const SRegistryView& view) noexcept
{
    return
        internal::validate_category_view(view.system, ETypeIdCategory::system) &&
        internal::validate_category_view(view.local, ETypeIdCategory::local);
}

bool install_view(const SRegistryView& view) noexcept
{
    if (internal::s_view_installed || !validate_view(view))
    {
        return false;
    }
    internal::s_installed_view = view;
    internal::s_view_installed = true;
    return true;
}

bool view_is_installed() noexcept
{
    return internal::s_view_installed;
}

const SRegistryView* installed_view() noexcept
{
    return internal::s_view_installed ? &internal::s_installed_view : nullptr;
}

const SRegistration* find(const SRegistryView* const view, const type_id identity) noexcept
{
    if ((view == nullptr) || !identity.is_valid())
    {
        return nullptr;
    }

    system_type_id system_identity;
    if (identity.try_system_type_id(system_identity))
    {
        const std::uint32_t index = system_type_ids::ops::decode_index(system_type_ids::ops::decode_id(system_identity));
        return internal::find_in_category(view->system, identity, index);
    }

    local_type_id local_identity;
    if (!identity.try_local_type_id(local_identity))
    {
        return nullptr;
    }
    const std::uint32_t index = local_type_ids::ops::decode_index(local_type_ids::ops::decode_id(local_identity));
    return internal::find_in_category(view->local, identity, index);
}

const SRegistration* find(const type_id identity) noexcept
{
    return find(installed_view(), identity);
}

}   //  namespace erased_owner_operations
