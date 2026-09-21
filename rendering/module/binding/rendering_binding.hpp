
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_binding.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Rendering module function identities and ABI signatures.

#pragma once

#ifndef RENDERING_BINDING_HPP_INCLUDED
#define RENDERING_BINDING_HPP_INCLUDED

#include "platform/threading/thread_lifetime.hpp"
#include "system/system_type_registration.hpp"

namespace rendering
{

//==============================================================================
//  Function identity declarations
//==============================================================================

struct CRenderingThreadFunction;

using FRenderingThread = platform::threading::FThreadEntry;

}   //  namespace rendering

MV_REGISTER_SYSTEM_TYPE(rendering::CRenderingThreadFunction, system_type_ids::rendering_thread_function);

#endif  //  #ifndef RENDERING_BINDING_HPP_INCLUDED
