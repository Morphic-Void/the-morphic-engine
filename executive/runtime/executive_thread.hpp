
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_thread.hpp
//  Authors: Ritchie Brannan / OpenAI tools
//  Date:    7 Aug 26
//
//  Entry point for the executive thread.

#pragma once

#ifndef EXECUTIVE_THREAD_HPP_INCLUDED
#define EXECUTIVE_THREAD_HPP_INCLUDED

#include "module/binding/executive_binding.hpp"

namespace executive
{

FExecutiveThread executive_thread_entry_point() noexcept;

}   //  namespace executive

#endif  //  #ifndef EXECUTIVE_THREAD_HPP_INCLUDED
