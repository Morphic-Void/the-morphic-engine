
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_thread.hpp
//  Authors: Ritchie Brannan / OpenAI tools
//  Date:    21 Sep 26
//
//  Entry point for the rendering thread.

#pragma once

#ifndef RENDERING_THREAD_HPP_INCLUDED
#define RENDERING_THREAD_HPP_INCLUDED

#include "rendering/module/binding/rendering_binding.hpp"

namespace rendering
{

FRenderingThread rendering_thread_entry_point() noexcept;

}   //  namespace rendering

#endif  //  #ifndef RENDERING_THREAD_HPP_INCLUDED
