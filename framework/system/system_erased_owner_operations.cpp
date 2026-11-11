
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    system_erased_owner_operations.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Concrete SYSTEM operation table for this binary; Host shares only identity
//  metadata, never these executable operation pointers.

#include "framework/system/system_ids.hpp"
#include <array>

#include "framework/system/system_erased_owner_operations.hpp"
#include "framework/system/transported_types.hpp"
#include "system/erased_owner.hpp"

namespace erased_owner_operations
{

namespace internal
{

static constexpr std::array<SRegistration, system_type_ids::k_count>
make_system_operations() noexcept
{
    std::array<SRegistration, system_type_ids::k_count> result{};

#define MV_ERASED_OWNER_PAYLOAD(type) \
    result[system_type_ids::ops::decode_index( \
        system_type_ids::ops::decode_id(k_system_type_id_v<type>))] = \
        SRegistration{ k_type_id_v<type>, \
            TDefaultOperationsFactory<type>::make() };
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) \
    result[system_type_ids::ops::decode_index( \
        system_type_ids::ops::decode_id(k_system_type_id_v<type>))] = \
        SRegistration{ k_type_id_v<type>, \
            TNestedOperationsFactory<type, &type::member>::make() };
#include "framework/system/system_erased_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

    return result;
}

static constexpr auto s_system_operations = make_system_operations();
static constexpr SCategoryView s_system_view{ s_system_operations.data(), static_cast<std::uint32_t>(s_system_operations.size()) };

}   //  namespace internal

const SCategoryView& system_operations_view() noexcept
{
    return internal::s_system_view;
}

}   //  namespace erased_owner_operations
