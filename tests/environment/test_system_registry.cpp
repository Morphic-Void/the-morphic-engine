
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    test_system_registry.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#include "tests/environment/test_environment.hpp"

#include <limits>

namespace test_environment
{
namespace
{
constexpr system_id_registry::STypeRegistration s_type_registrations[] =
{
#include "system/system_type_ids.def"
#define MV_SYSTEM_TYPE(name) { system_type_ids::name, system_type_ids::name##_index, #name, sizeof(#name) - 1u },
    MV_SYSTEM_TYPES(MV_SYSTEM_TYPE)
#undef MV_SYSTEM_TYPE
#undef MV_SYSTEM_TYPES
};

constexpr system_id_registry::SMountPointRegistration s_mount_point_registrations[] =
{
#define MV_SYSTEM_MOUNT_POINT(name) { mount_point_ids::name, mount_point_ids::name##_index, #name, sizeof(#name) - 1u },
#define MV_SYSTEM_MODULE(name, mount_point_name)
#include "system/runtime_ids.def"
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT
};

constexpr system_id_registry::SThreadRegistration s_thread_registrations[] =
{
#define MV_SYSTEM_THREAD(name) { thread_ids::name, thread_ids::name##_index, #name, sizeof(#name) - 1u },
#include "system/thread_ids.def"
#undef MV_SYSTEM_THREAD
};

constexpr system_id_registry::SModuleRegistration s_module_registrations[] =
{
#define MV_SYSTEM_MOUNT_POINT(name)
#define MV_SYSTEM_MODULE(name, mount_point_name) \
    { module_ids::name, module_ids::name##_index, mount_point_ids::mount_point_name, #name, sizeof(#name) - 1u },
#include "system/runtime_ids.def"
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT
};

template<typename T, std::size_t N>
constexpr std::uint32_t count_of(const T (&)[N]) noexcept
{
    static_assert(N <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
    return static_cast<std::uint32_t>(N);
}

constexpr system_id_registry::SSystemRegistryView s_registry_view{
    s_type_registrations, count_of(s_type_registrations),
    s_mount_point_registrations, count_of(s_mount_point_registrations),
    s_thread_registrations, count_of(s_thread_registrations),
    s_module_registrations, count_of(s_module_registrations)
};

static_assert(count_of(s_type_registrations) == system_type_ids::k_count);
static_assert(count_of(s_mount_point_registrations) == mount_point_ids::k_count);
static_assert(count_of(s_thread_registrations) == thread_ids::k_count);
static_assert(count_of(s_module_registrations) == module_ids::k_count);
}

const system_id_registry::SSystemRegistryView& system_registry_view() noexcept
{
    return s_registry_view;
}

}   //  namespace test_environment
