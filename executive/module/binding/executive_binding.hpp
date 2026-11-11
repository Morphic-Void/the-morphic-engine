
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_binding.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Executive module function identities and ABI signatures.

#pragma once

#ifndef EXECUTIVE_BINDING_HPP_INCLUDED
#define EXECUTIVE_BINDING_HPP_INCLUDED

#include "framework/system/system_ids.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "system/system_type_registration.hpp"

namespace executive
{

//==============================================================================
//  Function identity declarations
//==============================================================================

struct CExecutiveThreadFunction;

using FExecutiveThread = platform::threading::FThreadEntry;

}   //  namespace executive

MV_REGISTER_SYSTEM_TYPE(executive::CExecutiveThreadFunction, system_type_ids::executive_thread_function);

#endif  //  #ifndef EXECUTIVE_BINDING_HPP_INCLUDED
