
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   debugger.hpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:   31 Jul 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Current-process debugger interaction and fail-fast termination.

#pragma once

#ifndef DEBUGGER_HPP_INCLUDED
#define DEBUGGER_HPP_INCLUDED

namespace platform::system
{

[[nodiscard]] bool debugger_attached() noexcept;
void write_debugger_output(const char* const text) noexcept;
void break_into_debugger() noexcept;
[[noreturn]] void fail_fast() noexcept;

}   //  namespace platform::system

#endif  //  #ifndef DEBUGGER_HPP_INCLUDED

