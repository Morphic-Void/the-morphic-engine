
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   system_context.hpp
//  Author: Ritchie Brannan
//  Date:   12 Jul 26
//
//  Ambient system context.

#pragma once

#ifndef SYSTEM_CONTEXT_HPP_INCLUDED
#define SYSTEM_CONTEXT_HPP_INCLUDED

#include "foundation/system/system_ids.hpp"

namespace system_context
{

//==============================================================================
//  Ambient system identity
//==============================================================================

[[nodiscard]] module_ids::id_type get_ambient_module_id() noexcept;
[[nodiscard]] thread_ids::id_type get_ambient_thread_id() noexcept;
[[nodiscard]] system_ids::id_type get_ambient_system_id() noexcept;

module_ids::id_type set_ambient_module_id(const module_ids::id_type module_id = {}) noexcept;
thread_ids::id_type set_ambient_thread_id(const thread_ids::id_type thread_id = {}) noexcept;

}   //  namespace system_context

#endif  //  #ifndef SYSTEM_CONTEXT_HPP_INCLUDED
