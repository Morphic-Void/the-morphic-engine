
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Rendering module thread and asynchronous texture conditioning.

#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/communications/direct_client.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"

#include "runtime/rendering_thread.hpp"
#include "runtime/basis_codec.hpp"
#include "runtime/resource_requests.hpp"
#include "runtime/shader_requests.hpp"
#include "runtime/texture_flow_tests.hpp"

#include "containers/TUnorderedCollection.hpp"
#include "debug/macros.hpp"
#include "framework/module/module_binding_context.hpp"
#include "texture/texture_requests.hpp"
#include "framework/threading/CThreadContext.hpp"

#include <utility>

namespace rendering
{

static std::uint32_t MV_STD_ABI_CALL thread_entry(threading::CThreadContext& context, void* const module_context) noexcept
{
    (void)module_context;
    if (!modules::is_thread_context_ready(&context))
    {
        context.mark_failed(~0u);
        return ~0u;
    }
    context.startup();
    connections::CClient connection{ context };
    if (!connection.enable() || (context.batch_client() == nullptr) || !basis_codec::initialise())
    {
        context.mark_failed(1u);
        return 1u;
    }
    CResourceRequests requests{ *context.batch_client(), { &context, [](void* const value, const threading::CErasedPodMsg& message) noexcept -> bool {
        return static_cast<threading::CThreadContext*>(value)->post(message); } } };
    CTextureFlowTests tests{ context, context.module_id(), requests, connection };
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
    bool failed = !context.publish_ready();
    for (;;)
    {
        const std::uint32_t epoch = context.observe_epoch();
        connection.clear_notifications();
        context.advance_heartbeat();
        if (context.exit_requested())
        {
            context.mark_exiting();
            connection.close_all();
        }
        bool progressed = false;
        threading::SBatchResponse response;
        for (unsigned count = 0u; (count < 32u) && context.batch_client()->receive(response); ++count)
        {
            progressed = true;
            const bool completed = requests.complete(response);
            failed = !completed || failed;
            if (failed) connection.close_all();
        }

        //  Host asset and release completions still drive cleanup after closing.
        connections::SReceived received;
        for (unsigned count = 0u; (count < 32u) && connection.receive(received,
            (tests.can_read() && !context.exit_requested() && !failed)); ++count)
        {
            progressed = true;
            const threading::CErasedPodMsg& message = received.message;
            if (!received.source.is_host())
            {
                if ((received.source.address != system_ids::executive) || !tests.start(received.source.connection, message))
                {
                    failed = true;
                    connection.close_all();
                }
                continue;
            }
            connections::Offer offer;
            if (message.copy_payload_to(offer))
            {
                (void)connection.decide(offer.token, modules::test_connections::accepts(module_ids::rendering, offer) ?
                    connections::EStatus::success : connections::EStatus::rejected);
            }
            if (connections::is_control(message)) continue;
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
            if (connection.has_data(tests.can_read() && !context.exit_requested() && !failed))
            {
                continue;
            }
            if (context.exit_requested())
            {
                //  Remain visibly Exiting while queued/running jobs drain.
                (void)context.wait_for_new_epoch_preserving_state(epoch);
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
