
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner_registration.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Aug 26
//
//  Explicit C++ payload eligibility for CErasedOwner.

#pragma once

#ifndef ERASED_OWNER_REGISTRATION_HPP_INCLUDED
#define ERASED_OWNER_REGISTRATION_HPP_INCLUDED

#include <type_traits>  //  std::false_type, std::true_type

template<typename T>
struct TIsErasedOwnerPayload : std::false_type {};

template<typename T>
inline constexpr bool k_is_erased_owner_payload_v = TIsErasedOwnerPayload<T>::value;

#define MV_REGISTER_ERASED_OWNER_PAYLOAD(T) \
template<> \
struct TIsErasedOwnerPayload<T> : std::true_type \
{ \
}

#endif  //  ERASED_OWNER_REGISTRATION_HPP_INCLUDED
