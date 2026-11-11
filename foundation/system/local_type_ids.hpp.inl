
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    local_type_ids.hpp.inl
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Aug 26
//
//  Shared component-local type identity declarations.
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

#include "platform/platform_defines.hpp"
#include "system/erased_owner_operations.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/local_type_registration.hpp"
#include "system/local_type_registry.hpp"

namespace local_type_registry
{
[[nodiscard]] const SLocalTypeRegistryView& MV_STD_ABI_CALL component_view() noexcept;
}

namespace erased_owner_operations
{
[[nodiscard]] const SCategoryView& MV_STD_ABI_CALL local_operations_view() noexcept;
}

namespace local_type_ids
{
#define MV_LOCAL_TYPE(name, cpp_type) name##_index_value,
enum : index_type
{
    MV_LOCAL_TYPES(MV_LOCAL_TYPE)
    k_count
};
#undef MV_LOCAL_TYPE

static_assert(k_count <= ops::k_capacity);

#define MV_LOCAL_TYPE(name, cpp_type) \
    inline constexpr index_type name##_index = ops::encode_index(name##_index_value); \
    inline constexpr local_type_id name = ops::encode_id(name##_index);
MV_LOCAL_TYPES(MV_LOCAL_TYPE)
#undef MV_LOCAL_TYPE
}

#define MV_LOCAL_TYPE(name, cpp_type) \
    MV_REGISTER_LOCAL_TYPE(cpp_type, local_type_ids::name); \
    static_assert(k_local_type_id_v<cpp_type> == local_type_ids::name);
MV_LOCAL_TYPES(MV_LOCAL_TYPE)
#undef MV_LOCAL_TYPE

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
MV_LOCAL_ERASED_OWNER_PAYLOADS(
    MV_ERASED_OWNER_PAYLOAD,
    MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE)
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#undef MV_LOCAL_ERASED_OWNER_PAYLOADS
#undef MV_LOCAL_TYPES
