
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_context.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    13 July 26
//
//  Owns and installs the Host allocator and memory context.
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

#include "memory/memory_context.hpp"

namespace host
{

//  Must outlive all Host objects and module memory contexts using its allocator.
//  Installs memory/identity before dependent container members are constructed.
class CHostContext final
{
public:
    CHostContext() noexcept;
    CHostContext(const CHostContext&) = delete;
    CHostContext& operator=(const CHostContext&) = delete;
    CHostContext(CHostContext&&) = delete;
    CHostContext& operator=(CHostContext&&) = delete;
    ~CHostContext() noexcept;

    [[nodiscard]] bool is_installed() const noexcept;
    [[nodiscard]] memory::CMemoryAllocator& allocator() noexcept;

private:
    memory::CMemoryAllocator m_allocator;
    memory::CMemoryContext m_memory;
    memory::CMemoryContext* m_previous_module_memory{ nullptr };
    memory::CMemoryContext* m_previous_thread_memory{ nullptr };
    module_ids::id_type m_previous_module{};
    thread_ids::id_type m_previous_thread{};
    bool m_installed{ false };
};

}   //  namespace host

#endif  //  #ifndef HOST_CONTEXT_HPP_INCLUDED
