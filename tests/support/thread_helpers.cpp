
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    thread_helpers.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#include "tests/support/thread_helpers.hpp"

#include "threading/CThreadPackage.hpp"

namespace tests
{

std::uint32_t MV_STD_ABI_CALL echo_pod_messages(void* const data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    threading::CThreadContext context(resources);
    context.startup();
    context.mark_running();
    while (!context.exit_requested())
    {
        const std::uint32_t epoch = resources.wait_predicate.get_word();
        threading::CErasedPodMsg message;
        while (context.read(message))
        {
            if (!context.post(message))
            {
                context.mark_failed(1u);
                return 1u;
            }
        }
        if (!context.exit_requested()) (void)context.wait_for_new_epoch(epoch);
    }
    context.mark_exited();
    return 0u;
}

}   //  namespace tests
