
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    system_type_registration.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  C++ type binding for generated system type ids.

#pragma once

#ifndef SYSTEM_TYPE_REGISTRATION_HPP_INCLUDED
#define SYSTEM_TYPE_REGISTRATION_HPP_INCLUDED

#include "foundation/system/system_ids.hpp"
#include "system/type_id_binding_category.hpp"

//==============================================================================
//  Type-to-id binding
//==============================================================================

template<typename T>
struct TSystemTypeId;

template<typename T>
inline constexpr system_type_id k_system_type_id_v = TSystemTypeId<T>::value;

//==============================================================================
//  Registration macros
//==============================================================================

#define MV_REGISTER_SYSTEM_TYPE(T, type_id_value) \
template<> \
struct TTypeIdBindingCategory<T> \
{ \
    static constexpr ETypeIdBindingCategory value = ETypeIdBindingCategory::system; \
}; \
template<> \
struct TSystemTypeId<T> \
{ \
    static_assert(system_type_ids::ops::is_valid_id(type_id_value), \
        "MV_REGISTER_SYSTEM_TYPE requires a valid, non-zero type id."); \
    static constexpr system_type_id value = type_id_value; \
}

#endif  //  #ifndef SYSTEM_TYPE_REGISTRATION_HPP_INCLUDED
