
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_context.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    13 July 26
//
//  Installs the host context and owns component memory contexts.
// 
//  This file should not be included in modules/DLLs.
//
//  Design constraints
//  ------------------
//  - Requires C++17 or later.
//  - No exceptions.

#pragma once

#ifndef HOST_CONTEXT_HPP_INCLUDED
#define HOST_CONTEXT_HPP_INCLUDED

//==============================================================================
//  External declarations
//==============================================================================

namespace memory
{
class CMemoryContext;
class CMemoryAllocator;
}

namespace host
{

//  Installs the host context.
[[nodiscard]] bool host_context_install() noexcept;
[[nodiscard]] memory::CMemoryContext* host_memory_context() noexcept;
[[nodiscard]] memory::CMemoryAllocator& host_memory_allocator() noexcept;

}   //  namespace host

#endif  //  #ifndef HOST_CONTEXT_HPP_INCLUDED
