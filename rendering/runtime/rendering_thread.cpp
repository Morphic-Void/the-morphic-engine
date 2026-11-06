
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Rendering module thread and asynchronous texture conditioning.

#include "framework/communications/module_tests.hpp"
#include "framework/communications/direct_client.hpp"
#include "module/module_messages.hpp"
#include "threading/messages/message_helpers.hpp"

#include "runtime/rendering_thread.hpp"
#include "runtime/basis_codec.hpp"
#include "runtime/resource_requests.hpp"
#include "runtime/shader_requests.hpp"
#include "runtime/texture_flow_tests.hpp"

#include "containers/TUnorderedCollection.hpp"
#include "debug/macros.hpp"
#include "module/module_binding_context.hpp"
#include "texture/texture_requests.hpp"
#include "threading/CThreadPackage.hpp"

#include <utility>

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
    connections::CClient connection{ context, module_ids::rendering,
        modules::test_connections::policies, modules::test_connections::policy_count };
    if (!connection.register_service() || (resources.batch_client == nullptr) || !basis_codec::initialise())
    {
        context.mark_failed(1u);
        return 1u;
    }
    CResourceRequests requests{ *resources.batch_client, { &context, [](void* const value, const threading::CErasedPodMsg& message) noexcept {
        return static_cast<threading::CThreadContext*>(value)->post(message); } } };
    CTextureFlowTests tests{ context, resources.config.worker_module_id, requests, connection };
    if (!requests.initialise())
    {
        basis_codec::shutdown();
        context.mark_failed(1u);
        return 1u;
    }
    MV_LOG(info, "Rendering: Running");
    if (!shader_compiler::initialise_compiler())
    {
        MV_LOG(info, "Rendering: DXC is unavailable; shader requests will retain failure reports");
    }
    context.mark_running();
    bool failed = false;
    for (;;)
    {
        const std::uint32_t epoch = resources.wait_predicate.get_word();
        connection.clear_notifications();
        context.advance_heartbeat();
        if (context.exit_requested())
        {
            context.mark_exiting();
            connection.close_all();
        }
        bool progressed = false;
        threading::SBatchResponse response;
        for (unsigned count = 0u; (count < 32u) && resources.batch_client->receive(response); ++count)
        {
            progressed = true;
            const bool completed = requests.complete(response);
            failed = !completed || failed;
            if (failed) connection.close_all();
        }

        //  Host asset and release completions still drive cleanup after closing.
        threading::CErasedPodMsg message;
        for (unsigned count = 0u; (count < 32u) && context.read(message); ++count)
        {
            progressed = true;
            if (connection.handle(message))
            {
                if (connection.failed())
                {
                    failed = true;
                    connection.close_all();
                }
                continue;
            }
            if (message.is_payload_a<AssetAcquireResult>() || message.is_payload_a<AssetReleaseResult>())
            {
                failed = !requests.complete(message) || failed;
            }
            else if (message.is_payload_a<AssetResult>() || message.is_payload_a<AssetDisposeResult>())
            {
                failed = !tests.complete(message) || failed;
            }
            else
            {
                failed = true;
            }
            if (failed) connection.close_all();
        }
        connection.progress();
        failed = !tests.progress() || connection.failed() || failed;
        if (failed) connection.close_all();
        const auto token = connection.active(module_ids::executive);
        for (unsigned count = 0u; (count < 32u) && token.value && tests.can_read() && !context.exit_requested() && !failed; ++count)
        {
            const auto read = connection.read(token, message);
            if (!connections::was_read(read)) break;
            progressed = true;
            if (!connection.active(token)) break;
            failed = !tests.start(token, message) || failed;
        }
        SResourceResult result;
        std::int32_t correlation;
        while (requests.receive(result, correlation))
        {
            progressed = true;
            if (!failed)
            {
                failed = !tests.complete(std::move(result), correlation);
            }
            result = {};
        }
        failed = requests.failed() || failed;
        if (failed) connection.close_all();
        if (!tests.pending() && !requests.pending())
        {
            for (auto closing = connection.next_closing(); closing.value; closing = connection.next_closing())
            {
                if (!connection.quiesce(closing)) break;
            }
        }
        if ((context.exit_requested() || failed) && !requests.jobs_pending() && (failed || (!requests.pending() && !tests.pending())))
        {
            break;
        }
        if (!progressed && !connection.needs_progress())
        {
            if (tests.can_read() && connection.has_data())
            {
                continue;
            }
            if (context.exit_requested())
            {
                //  Remain visibly Exiting while queued/running jobs drain.
                (void)resources.wait_predicate.wait_until_not_equal(resources.parking_ticket, epoch);
            }
            else
            {
                (void)context.wait_for_new_epoch(epoch);
            }
        }
    }

    context.mark_exiting();
    connection.close_all();
    connection.progress();
    basis_codec::shutdown();
    if (failed)
    {
        MV_CRITICAL_EVENT("Rendering: Work or test response delivery failed");
        context.mark_failed(2u);
        return 2u;
    }
    MV_LOG(info, "Rendering: Exited");
    context.mark_exited();
    return 0u;
}

FRenderingThread rendering_thread_entry_point() noexcept
{
    return &thread_entry;
}

}   //  namespace rendering
