
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  The service owns all device operations; idle loading never opens a device.

#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/communications/direct_client.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "audio_thread.hpp"
#include "miniaudio_adapter.hpp"
#include "framework/module/module_binding_context.hpp"

namespace audio
{

static modules::RunTestsResult report(const modules::ETestRunStatus status) noexcept
{
    modules::RunTestsResult result{ module_ids::audio, status };
    if (status == modules::ETestRunStatus::success)
    {
        result.passed = 1u;
    }
    else if ((status == modules::ETestRunStatus::unavailable) || (status == modules::ETestRunStatus::cancelled))
    {
        result.skipped = 1u;
    }
    else if (status == modules::ETestRunStatus::failed)
    {
        result.failed = 1u;
    }
    return result;
}

std::uint32_t serve_audio(threading::CThreadResources& resources, IAudioOutput& output) noexcept
{
    threading::CThreadContext context{ resources };
    context.startup();
    connections::CClient connection{ context, module_ids::audio,
        modules::test_connections::policies, modules::test_connections::policy_count };
    CAudioRun run{ output };
    std::int32_t correlation = threading::k_uncorrelated_slot;
    connections::SToken conversation, rejected_connection;
    threading::CErasedPodMsg rejected_reply;
    bool pending = false;
    bool rejected = false;
    bool failed = false;
    bool reported = false;
    if (!connection.register_service())
    {
        context.mark_failed(1u);
        return 1u;
    }
    context.mark_running();
    MV_LOG(info, "Audio: Running (device closed)");
    for (;;)
    {
        const std::uint32_t epoch = context.observe_epoch();
        connection.clear_notifications();
        context.advance_heartbeat();
        bool progressed = false;
        threading::CErasedPodMsg message;
        for (unsigned count = 0u; (count < 32u) && context.read(message); ++count)
        {
            progressed = true;
            if (!connection.handle(message)) failed = true;
        }
        connection.progress();
        const bool exiting = context.exit_requested();
        if (exiting || failed || connection.failed() || (pending && !connection.active(conversation)))
        {
            if (exiting || failed || connection.failed())
            {
                connection.close_all();
            }
            run.cancel();
            pending = false;
        }
        else run.check();

        if (pending && !run.active())
        {
            const auto status = run.result();
            if (!reported)
            {
                MV_LOG(info, "Audio: Test complete after cleanup (status {})", static_cast<unsigned>(status));
            }
            reported = true;
            const auto posted = connection.post(conversation, threading::make_pod_message(correlation, report(status)));
            if (posted != connections::EPost::full)
            {
                pending = false;
                progressed = true;
            }
            if (posted == connections::EPost::invalid_message)
            {
                failed = true;
            }
        }
        if (rejected)
        {
            const auto posted = connection.post(rejected_connection, rejected_reply);
            if (posted != connections::EPost::full)
            {
                rejected = false;
                progressed = true;
            }
            if (posted == connections::EPost::invalid_message)
            {
                failed = true;
            }
        }
        const auto token = connection.active(module_ids::executive);
        //  One retained rejection bounds local work when the result queue is full.
        for (unsigned count = 0u; (count < 32u) && !rejected && token.value && !exiting && !failed; ++count)
        {
            const auto read = connection.read(token, message);
            if (!connections::was_read(read)) break;
            progressed = true;
            if (!connection.active(token)) break;
            modules::RunTestsRequest request;
            const auto slot = message.query_async_slot();
            if (!message.copy_payload_to(request))
            {
                failed = true;
                break;
            }
            modules::ETestRunStatus rejection = modules::ETestRunStatus::invalid_request;
            if ((request.module == module_ids::audio) && (slot >= 0))
            {
                if (context.exit_requested() || failed)
                {
                    rejection = modules::ETestRunStatus::cancelled;
                }
                else if (pending)
                {
                    rejection = modules::ETestRunStatus::busy;
                }
                else
                {
                    correlation = slot;
                    conversation = token;
                    pending = true;
                    reported = false;
                    run.begin();
                    continue;
                }
            }
            rejected_connection = token;
            rejected_reply = threading::make_pod_message(slot, report(rejection));
            rejected = true;
        }
        for (auto closing = connection.next_closing(); closing.value; closing = connection.next_closing())
        {
            if ((pending && (conversation.value == closing.value)) || (rejected && (rejected_connection.value == closing.value)))
            {
                break;
            }
            if (!connection.quiesce(closing)) break;
        }
        failed = failed || connection.failed();
        if (failed || exiting)
        {
            break;
        }
        if (progressed || connection.needs_progress())
        {
            continue;
        }
        if (!rejected && connection.has_data())
        {
            continue;
        }
        if (run.active())
        {
            const auto result = context.wait_for_new_epoch(epoch, run.next_wait());
            if ((result.status != threading::EWaitStatus::satisfied) && (result.status != threading::EWaitStatus::timed_out))
            {
                run.wait_failed(result);
                pending = false;

                //  Control release/unsupported timing cannot safely serve more work.
                failed = true;
                break;
            }
        }
        else
        {
            if (context.wait_for_new_epoch(epoch) == ~std::uint32_t{ 0u })
            {
                failed = true;
                break;
            }
        }
    }
    context.mark_exiting();
    connection.close_all();
    run.cancel();
    connection.progress();
    if (failed)
    {
        context.mark_failed(1u);
        return 1u;
    }
    MV_LOG(info, "Audio: Exited");
    context.mark_exited();
    return 0u;
}

static std::uint32_t MV_STD_ABI_CALL entry(void* const user_data) noexcept
{
    if (user_data == nullptr) return ~0u;
    auto& resources = *static_cast<threading::CThreadResources*>(user_data);
    auto* memory = memory::get_ambient_memory_context();
    if (!modules::is_thread_context_ready(user_data) || (memory == nullptr))
    {
        resources.control_state.mark_failed(~0u);
        return ~0u;
    }
    CMiniaudioOutput output{ *memory };
    return serve_audio(resources, output);
}

platform::threading::FThreadEntry audio_thread_entry_point() noexcept { return &entry; }

}   //  namespace audio
