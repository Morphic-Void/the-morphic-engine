
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   memory_context.cpp
//  Author: Ritchie Brannan
//  Date:   12 Jul 26
//
//  Module-local and thread-local ambient memory context state.

#include "memory/memory_context.hpp"

namespace memory
{

//==============================================================================
//  Ambient state
//==============================================================================

static CMemoryContext* s_module_memory_context{ nullptr };

thread_local CMemoryContext* t_thread_memory_context{ nullptr };

//==============================================================================
//  Ambient memory context
//==============================================================================

CMemoryContext* get_ambient_memory_context() noexcept
{
    return (t_thread_memory_context != nullptr) ? t_thread_memory_context : s_module_memory_context;
}

CMemoryContext* set_module_memory_context(CMemoryContext* const context) noexcept
{
    CMemoryContext* const previous_context = s_module_memory_context;
    s_module_memory_context = context;
    return previous_context;
}

CMemoryContext* set_thread_memory_context(CMemoryContext* const context) noexcept
{
    CMemoryContext* const previous_context = t_thread_memory_context;
    t_thread_memory_context = context;
    return previous_context;
}

}   //  namespace memory
