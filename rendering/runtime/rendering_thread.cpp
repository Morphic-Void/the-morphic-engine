
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Minimal rendering module thread, parked until the Host requests exit.

#include "rendering/runtime/rendering_thread.hpp"

#include "debug/macros.hpp"
#include "module/module_binding_context.hpp"
#include "threading/CThreadPackage.hpp"

namespace rendering
{

static std::uint32_t MV_STD_ABI_CALL thread_entry(void* const user_data) noexcept
{
    if (user_data == nullptr)
    {
        return ~0u;
    }

    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(user_data);
    if (!modules::is_thread_context_ready(user_data))
    {
        resources.control_state.mark_failed(~0u);
        return ~0u;
    }

    threading::CThreadContext context{ resources };
    context.startup();
    MV_REPORT("Rendering: Running");
    context.mark_running();

    std::uint32_t epoch{ 0u };
    while (!context.exit_requested())
    {
        epoch = context.wait_for_new_epoch(epoch);
    }

    context.mark_exiting();
    MV_REPORT("Rendering: Exited");
    context.mark_exited();
    return 0u;
}

FRenderingThread rendering_thread_entry_point() noexcept
{
    return &thread_entry;
}

}   //  namespace rendering
