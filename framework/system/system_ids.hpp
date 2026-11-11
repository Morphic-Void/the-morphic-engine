
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    system_ids.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Configured Morphic identity catalogues and named combined identities.
//  Encoding and structural validity remain independent of these catalogues.

#pragma once

#include "foundation/system/system_ids.hpp"

//==============================================================================
//  Type ids
//==============================================================================

namespace system_type_ids
{

#include "framework/system/system_type_ids.def"

#define MV_SYSTEM_TYPE(name) name##_index_value,
enum : index_type
{
    MV_SYSTEM_TYPES(MV_SYSTEM_TYPE)
    k_count
};
#undef MV_SYSTEM_TYPE

static_assert((k_count <= ops::k_capacity), "The system type definition count exceeds the encoded system-type capacity.");

#define MV_SYSTEM_TYPE(name) \
constexpr index_type name##_index = ops::encode_index(name##_index_value); \
constexpr system_type_id name = ops::encode_id(name##_index);
MV_SYSTEM_TYPES(MV_SYSTEM_TYPE)
#undef MV_SYSTEM_TYPE
#undef MV_SYSTEM_TYPES

}   //  namespace system_type_ids

//==============================================================================
//  Mount point ids
//==============================================================================

namespace mount_point_ids
{

#define MV_SYSTEM_MOUNT_POINT(name) name##_index_value,
#define MV_SYSTEM_MODULE(name, mount_point_name)
enum : index_type::repr_type
{
#include "framework/system/runtime_ids.def"
    k_count
};
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

static_assert((k_count <= ops::k_capacity), "The mount-point definition count exceeds the encoded mount-point capacity.");
static_assert((k_count <= ops::k_dependency_mask_capacity), "The mount-point catalogue exceeds erased-owner dependency capacity.");

#define MV_SYSTEM_MOUNT_POINT(name) \
constexpr index_type name##_index = ops::make_index(name##_index_value); \
constexpr id_type name = ops::make_id(name##_index);
#define MV_SYSTEM_MODULE(name, mount_point_name)
#include "framework/system/runtime_ids.def"
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

}   //  namespace mount_point_ids

//==============================================================================
//  Module ids
//==============================================================================

namespace module_ids
{

#define MV_SYSTEM_MOUNT_POINT(name)
#define MV_SYSTEM_MODULE(name, mount_point_name) name##_index_value,
enum : index_type::repr_type
{
#include "framework/system/runtime_ids.def"
    k_count
};
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

static_assert((k_count <= ops::k_capacity), "The module definition count exceeds the encoded module capacity.");

#define MV_SYSTEM_MOUNT_POINT(name)
#define MV_SYSTEM_MODULE(name, mount_point_name) \
constexpr index_type name##_index = ops::make_index(name##_index_value); \
constexpr id_type name = ops::make_id(mount_point_ids::mount_point_name, name##_index);
#include "framework/system/runtime_ids.def"
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

}   //  namespace module_ids

//==============================================================================
//  Thread ids
//==============================================================================

namespace thread_ids
{

#define MV_SYSTEM_THREAD(name) name##_index_value,
enum : index_type::repr_type
{
#include "framework/system/thread_ids.def"
    k_count
};
#undef MV_SYSTEM_THREAD

static_assert((k_count <= ops::k_capacity), "The thread definition count exceeds the encoded thread capacity.");

#define MV_SYSTEM_THREAD(name) \
constexpr index_type name##_index = ops::make_index(name##_index_value); \
constexpr id_type name = ops::make_id(name##_index);
#include "framework/system/thread_ids.def"
#undef MV_SYSTEM_THREAD

}   //  namespace thread_ids

//==============================================================================
//  System ids
//==============================================================================

namespace system_ids
{

constexpr id_type host = ops::make_system_id(module_ids::executable, thread_ids::host);
constexpr id_type bg_file_io = ops::make_system_id(module_ids::executable, thread_ids::bg_file_io);
constexpr id_type bg_conditioning = ops::make_system_id(module_ids::executable, thread_ids::bg_conditioning);
constexpr id_type executive = ops::make_system_id(module_ids::executive, thread_ids::executive);

}   //  namespace system_ids
