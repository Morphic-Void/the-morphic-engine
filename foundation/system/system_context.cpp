
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   system_context.cpp
//  Author: Ritchie Brannan
//  Date:   12 Jul 26
//
//  Module-local and thread-local ambient system context state.

#include "system/system_context.hpp"

namespace system_context
{

//==============================================================================
//  Ambient state
//==============================================================================

static module_ids::id_type s_module_id{};

thread_local thread_ids::id_type t_thread_id{};

//==============================================================================
//  Ambient system identity
//==============================================================================

module_ids::id_type get_ambient_module_id() noexcept
{
    return s_module_id;
}

thread_ids::id_type get_ambient_thread_id() noexcept
{
    return t_thread_id;
}

system_ids::id_type get_ambient_system_id() noexcept
{
    return system_ids::ops::make_system_id(s_module_id, t_thread_id);
}

module_ids::id_type set_ambient_module_id(const module_ids::id_type module_id) noexcept
{
    const module_ids::id_type previous_module_id = s_module_id;
    s_module_id = module_id;
    return previous_module_id;
}

thread_ids::id_type set_ambient_thread_id(const thread_ids::id_type thread_id) noexcept
{
    const thread_ids::id_type previous_thread_id = t_thread_id;
    t_thread_id = thread_id;
    return previous_thread_id;
}

}   //  namespace system_context
