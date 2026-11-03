
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  The service owns all device operations; idle loading never opens a device.

#include "module/module_messages.hpp"
#include "threading/messages/message_helpers.hpp"
#include "audio_thread.hpp"
#include "miniaudio_adapter.hpp"
#include "module/module_binding_context.hpp"

namespace audio
{

static bool report(threading::CThreadContext& context, const std::int32_t correlation, const modules::ETestRunStatus status) noexcept
{
    modules::RunTestsResult result{ module_ids::audio, status };
    if (status == modules::ETestRunStatus::success) result.passed = 1u;
    else if ((status == modules::ETestRunStatus::unavailable) || (status == modules::ETestRunStatus::cancelled)) result.skipped = 1u;
    else if (status == modules::ETestRunStatus::failed) result.failed = 1u;
    return context.post(threading::make_pod_message(correlation, result));
}

std::uint32_t serve_audio(threading::CThreadResources& resources, IAudioOutput& output) noexcept
{
    threading::CThreadContext context{ resources };
    context.startup();
    CAudioRun run{ output };
    std::int32_t correlation = threading::k_uncorrelated_slot;
    bool pending = false;
    bool failed = false;
    context.mark_running();
    MV_LOG(info, "Audio: Running (device closed)");
    for (;;)
    {
        const auto epoch = resources.wait_predicate.get_word();
        context.advance_heartbeat();
        const bool exiting = context.exit_requested();
        if (exiting) run.cancel();
        else run.check();

        if (pending && !run.active())
        {
            const auto status = run.result();
            MV_LOG(info, "Audio: Test complete after cleanup (status {})", static_cast<unsigned>(status));
            failed = !report(context, correlation, status) || failed;
            pending = false;
        }

        //  Bound each drain so message storms cannot starve progress/deadlines.
        threading::CErasedPodMsg message;
        bool read_any = false;
        for (unsigned count = 0u; count < 32u && context.read(message); ++count)
        {
            read_any = true;
            modules::RunTestsRequest request;
            const auto slot = message.query_async_slot();
            if (!message.copy_payload_to(request)) { failed = true; break; }
            modules::ETestRunStatus rejection = modules::ETestRunStatus::invalid_request;
            if ((request.module == module_ids::audio) && (slot >= 0))
            {
                if (context.exit_requested() || failed) rejection = modules::ETestRunStatus::cancelled;
                else if (pending) rejection = modules::ETestRunStatus::busy;
                else
                {
                    correlation = slot;
                    pending = true;
                    run.begin();
                    continue;
                }
            }
            failed = !report(context, slot, rejection) || failed;
        }
        if (failed || exiting) break;
        if (read_any) continue;
        if (run.active())
        {
            const auto result = context.wait_for_new_epoch(epoch, run.next_wait());
            if ((result.status != threading::EWaitStatus::satisfied) && (result.status != threading::EWaitStatus::timed_out))
            {
                run.wait_failed(result);
                failed = !report(context, correlation, run.result());
                pending = false;

                //  Control release/unsupported timing cannot safely serve more work.
                failed = true;
                break;
            }
        }
        else if (!pending)
        {
            if (context.wait_for_new_epoch(epoch) == ~std::uint32_t{ 0u })
            {
                failed = true;
                break;
            }
        }
    }
    context.mark_exiting();
    run.cancel();
    if (pending) failed = !report(context, correlation, run.result()) || failed;
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
