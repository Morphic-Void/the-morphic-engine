
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    type_registration.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26
//
//  Category-bearing C++ type identity bindings.

#pragma once

#ifndef TYPE_REGISTRATION_HPP_INCLUDED
#define TYPE_REGISTRATION_HPP_INCLUDED

#include "system/local_type_registration.hpp"
#include "system/system_type_registration.hpp"

namespace type_registration_detail
{

template<typename T, ETypeIdBindingCategory Category>
struct TTypeIdSelector;

template<typename T>
struct TTypeIdSelector<T, ETypeIdBindingCategory::system>
{
    static constexpr type_id value{ k_system_type_id_v<T> };
};

template<typename T>
struct TTypeIdSelector<T, ETypeIdBindingCategory::local>
{
    static constexpr type_id value{ k_local_type_id_v<T> };
};

}   //  namespace type_registration_detail

template<typename T>
struct TTypeId : type_registration_detail::TTypeIdSelector<
    T, k_type_id_binding_category_v<T>>
{
};

template<typename T>
inline constexpr type_id k_type_id_v = TTypeId<T>::value;

#endif  //  TYPE_REGISTRATION_HPP_INCLUDED
