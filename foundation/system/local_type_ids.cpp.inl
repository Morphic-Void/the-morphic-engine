
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    local_type_ids.cpp.inl
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Aug 26
//
//  Shared component-local type identity definitions.
//
//  Required component catalog macros:
//  - MV_LOCAL_TYPES
//  - MV_LOCAL_ERASED_OWNER_PAYLOADS

#if !defined(MV_LOCAL_TYPES)
#error MV_LOCAL_TYPES must enumerate the component local types.
#endif

#if !defined(MV_LOCAL_ERASED_OWNER_PAYLOADS)
#error MV_LOCAL_ERASED_OWNER_PAYLOADS must enumerate the component erased-owner payloads.
#endif

#include <array>

#include "system/erased_owner.hpp"

static constexpr local_type_registry::SLocalTypeRegistration s_local_type_registrations[] =
{
#define MV_LOCAL_TYPE(name, cpp_type) \
    { local_type_ids::name, local_type_ids::name##_index, local_type_registry::make_name(#name) },
    MV_LOCAL_TYPES(MV_LOCAL_TYPE)
#undef MV_LOCAL_TYPE
};

static constexpr local_type_registry::SLocalTypeRegistryView s_local_type_registry_view{
    s_local_type_registrations,
    static_cast<std::uint32_t>(local_type_ids::k_count) };

static constexpr std::array<erased_owner_operations::SRegistration, local_type_ids::k_count> make_local_erased_owner_operations() noexcept
{
    std::array<erased_owner_operations::SRegistration, local_type_ids::k_count> result{};

#define MV_ERASED_OWNER_PAYLOAD(type) \
    result[local_type_ids::ops::decode_index( \
        local_type_ids::ops::decode_id(k_local_type_id_v<type>))] = \
        erased_owner_operations::SRegistration{ k_type_id_v<type>, \
            erased_owner_operations::TDefaultOperationsFactory<type>::make() };
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) \
    result[local_type_ids::ops::decode_index( \
        local_type_ids::ops::decode_id(k_local_type_id_v<type>))] = \
        erased_owner_operations::SRegistration{ k_type_id_v<type>, \
            erased_owner_operations::TNestedOperationsFactory<type, \
                &type::member>::make() };
    MV_LOCAL_ERASED_OWNER_PAYLOADS(
        MV_ERASED_OWNER_PAYLOAD,
        MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE)
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

    return result;
}

static constexpr auto s_local_erased_owner_operations = make_local_erased_owner_operations();
static constexpr erased_owner_operations::SCategoryView s_local_erased_owner_operations_view{
    s_local_erased_owner_operations.data(),
    static_cast<std::uint32_t>(s_local_erased_owner_operations.size()) };

namespace local_type_registry
{

const SLocalTypeRegistryView& MV_STD_ABI_CALL component_view() noexcept
{
    return s_local_type_registry_view;
}

}   //  namespace local_type_registry

namespace erased_owner_operations
{

const SCategoryView& MV_STD_ABI_CALL local_operations_view() noexcept
{
    return s_local_erased_owner_operations_view;
}

}   //  namespace erased_owner_operations

#undef MV_LOCAL_ERASED_OWNER_PAYLOADS
#undef MV_LOCAL_TYPES
