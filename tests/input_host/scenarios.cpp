
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    scenarios.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Synthetic pressure/recovery cases and the interactive real Host observation pass.

#include "framework/system/system_ids.hpp"
#include "tests/input_host/consumer.hpp"

namespace input_host
{
bool CConsumer::exercise()
{
    if (native || ordinary)
    {
        return native_pass();
    }
    for (std::uint32_t index = 0u; index < 4u; ++index)
    {
        SFeed frame;
        frame.kind = EFeed::controller;
        frame.device = index;
        frame.value = 1u;
        if (!feed(frame) || !until([&] { return pipes[index + 1u].values[k_south_action][0] == 1.0f; }))
        {
            return false;
        }
        frame.value = 0u;
        if (!feed(frame) || !until([&] { return pipes[index + 1u].values[k_south_action][0] == 0.0f; }))
        {
            return false;
        }
    }
    SFeed key;
    key.kind = EFeed::key;
    key.device = 1u;
    key.value = 1u;
    if (!feed(key) || !until([&] { return pipes[0].values[k_key_a_action][0] == 1.0f; }))
    {
        return false;
    }
    key.value = 0u;
    if (!feed(key) || !until([&] { return pipes[0].values[k_key_a_action][0] == 0.0f; }))
    {
        return false;
    }
    MV_LOG(info, "Input Host: five independent pipes acquired, acknowledged and applied");
    if (!settle(100u)) return false;
    if (option("MORPHIC_INPUT_CASE", "pressure"))
    {
        return pressure();
    }
    if (option("MORPHIC_INPUT_CASE", "load") || option("MORPHIC_INPUT_CASE", "load-smoke"))
    {
        return load();
    }
    if (option("MORPHIC_INPUT_CASE", "second-consumer"))
    {
        return second_consumer();
    }
    if (option("MORPHIC_INPUT_CASE", "transitions"))
    {
        return transitions();
    }
    if (option("MORPHIC_INPUT_CASE", "work-pressure"))
    {
        return work_pressure();
    }
    if (option("MORPHIC_INPUT_CASE", "shutdown-blocked"))
    {
        return shutdown_blocked();
    }
    if (option("MORPHIC_INPUT_CASE", "ingress-loss"))
    {
        return ingress_loss();
    }
    if (option("MORPHIC_INPUT_CASE", "acquisition-cleanup"))
    {
        return acquisition_cleanup();
    }
    if (option("MORPHIC_INPUT_CASE", "source-cleanup"))
    {
        return source_cleanup();
    }
    return true;
}
bool CConsumer::source_cleanup()
{
    //  Begin cleanup before receiving a successful transfer or acquisition reply.
    //  The result still conveys ownership which must be harvested and retired.
    for (const bool acquire_source : { false, true })
    {
        CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
        RawAssetTransfer* const asset = owner.payload<RawAssetTransfer>();
        if (!asset || !asset->storage.value.resize(32u, 32u))
        {
            return false;
        }
        std::memset(asset->storage.value.data(), 0, 32u);
        ::threading::CErasedOwnerMsg message;
        message.set_message_type<RawAssetTransfer>();
        message.set_async_slot(1);
        message.set_owner(std::move(owner));
        asset_pending = EAssetPending::transfer;
        if (!thread.post(std::move(message)))
        {
            asset_pending = EAssetPending::none;
            return false;
        }
        if (acquire_source)
        {
            if (!until([&] { return asset_pending == EAssetPending::none; }) || !source_asset.is_valid())
            {
                return false;
            }
            if (!post(AssetAcquireRequest{ source_asset }))
            {
                return false;
            }
        }
        cleaning = true;
        const bool released = release_source();
        cleaning = false;
        if (!released || source_asset.is_valid() || source_acquisition ||
            (asset_pending != EAssetPending::none) || host_waiting || host_ready)
        {
            return false;
        }
    }
    MV_LOG(info, "Input Host: late source transfer/acquisition ownership retired during cleanup");
    return true;
}
bool CConsumer::transitions()
{
    SFeed frame;
    frame.kind = EFeed::controller;
    for (const EFeed boundary : { EFeed::focus, EFeed::modal, EFeed::history_loss })
    {
        for (std::uint32_t device = 0u; device < 4u; ++device)
        {
            frame.device = device;
            frame.value = 1u;
            frame.left_x = 0.75f;
            if (!feed(frame) || !until([&] { return pipes[device + 1u].values[k_south_action][0] == 1.0f; }))
            {
                return false;
            }
        }
        SFeed change;
        change.kind = boundary;
        const std::uint64_t cancellations = pipes[1].cancellations;
        if (!feed(change)) return false;
        if (boundary == EFeed::focus)
        {
            if (!until([&] { return pipes[1].values[k_south_action][0] == 0.0f; }))
            {
                return false;
            }
            change.value = 1u;
            if (!feed(change)) return false;
        }
        const std::uint64_t submitted = next_observation;
        //  The adapter can consume the feed before the Platform publishes its boundary.
        if (!until([&]
            {
                return (shared->consumed.load() >= submitted) && (pipes[1].cancellations > cancellations) && active();
            }))
        {
            MV_LOG(error, "Input Host: controller did not recover from boundary {}", static_cast<unsigned>(boundary));
            return false;
        }
        if (pipes[1].values[k_south_action][0] != 0.0f)
        {
            MV_LOG(error, "Input Host: controller still applied after boundary {}", static_cast<unsigned>(boundary));
            return false;
        }
        frame.device = 0u;
        frame.value = 1u;
        frame.left_x = 0.8f;
        if (!feed(frame) || !settle(30u) || (pipes[1].values[k_south_action][0] != 0.0f))
        {
            return false;
        }
        //  The button rearms independently while the held stick remains suppressed.
        frame.value = 0u;
        if (!feed(frame) || !settle(20u))
        {
            return false;
        }
        frame.value = 1u;
        if (!feed(frame) || !until([&] { return pipes[1].values[k_south_action][0] == 1.0f; }) || (pipes[1].values[k_left_stick_action][0] != 0.0f))
        {
            return false;
        }
        for (std::uint32_t device = 0u; device < 4u; ++device)
        {
            frame.device = device;
            frame.value = 0u;
            frame.left_x = 0.0f;
            if (!feed(frame)) return false;
        }
        if (!settle(30u)) return false;
    }
    SInputSnapshot before;
    if (pipes[1].view->state->read(before) != EStateRead::success)
    {
        return false;
    }
    SFeed arrival;
    arrival.kind = EFeed::arrival;
    arrival.device = 0u;
    if (!feed(arrival) || !until([&]
        {
            SInputSnapshot state;
            return (pipes[1].view->state->read(state) == EStateRead::success) && !state.device && active();
        }))
    {
        return false;
    }
    arrival.value = 1u;
    if (!feed(arrival)) return false;
    const std::uint64_t reconnected = next_observation;
    if (!until([&] { return shared->consumed.load() >= reconnected; }))
    {
        return false;
    }
    frame.device = 0u;
    frame.value = 0u;
    frame.left_x = 0.0f;
    if (!feed(frame)) return false;
    SInputSnapshot state;
    if (pipes[1].view->state->read(state) != EStateRead::success)
    {
        return false;
    }
    if (!command(ECommand::assign_device, SAssignDevice{ pipes[1].view->connection, before.device, state.action_generation }, EServiceStatus::stale) ||
        !command(ECommand::assign_device, SAssignDevice{ pipes[1].view->connection, k_default_device, state.action_generation }) ||
        !until([&] { return active(); }))
    {
        return false;
    }
    if ((pipes[1].view->state->read(state) != EStateRead::success) || !state.device || (state.device == before.device))
    {
        return false;
    }
    //  Change context with the action ring full and its marker unable to publish.
    if (!until([&] { return !shared->occupancy[1][0].load(); }))
    {
        return false;
    }
    pause_mask = 2u;
    for (std::uint32_t count = 0u; count < k_action_capacity; ++count)
    {
        frame.value = (count & 1u) ? 0u : 1u;
        if (!feed(frame) || !until([&] { return shared->occupancy[1][0].load() == (count + 1u); }))
        {
            return false;
        }
    }
    if (!pipes[1].reader->cancel_application(k_actions))
    {
        MV_LOG(error, "Input Host: could not cancel application on the full action ring");
        return false;
    }
    (void)pipes[1].reader->take_cancellation();
    std::memset(pipes[1].values, 0, sizeof(pipes[1].values));
    if (!command(ECommand::select_context, SSelectContext{ pipes[1].view->connection, 5u }))
    {
        return false;
    }
    if (!until([&] { return pipes[1].view->state->read(state) == EStateRead::success; }) || !(state.pending & k_actions))
    {
        MV_LOG(error, "Input Host: expected a pending action boundary with the action ring full");
        return false;
    }
    pause_mask = 0u;
    if (!until([&] { return active(); }) || !pipes[1].reader->resume_application(pipes[1].boundary, k_actions))
    {
        MV_LOG(error, "Input Host: could not resume application after the blocked action boundary");
        return false;
    }
    frame.value = 0u;
    if (!feed(frame) || !settle(20u))
    {
        return false;
    }
    frame.value = 1u;
    if (!feed(frame) || !until([&] { return pipes[1].values[k_south_action][0] == 1.0f; }))
    {
        return false;
    }
    frame.value = 0u;
    if (!feed(frame) || !until([&] { return pipes[1].values[k_south_action][0] == 0.0f; }))
    {
        return false;
    }
    //  Retirement must not need a full ring to be drained.
    pause_mask = 2u;
    for (std::uint32_t count = 0u; count < k_action_capacity; ++count)
    {
        frame.value = (count & 1u) ? 0u : 1u;
        if (!feed(frame) || !until([&] { return shared->occupancy[1][0].load() == (count + 1u); }))
        {
            return false;
        }
    }
    if (!command(ECommand::select_context, SSelectContext{ pipes[1].view->connection, 4u }) || !release(*pipes[1].borrow))
    {
        return false;
    }
    pause_mask = 0u;
    if (!acquire_pipe(1u) || !until([&] { return active(); }))
    {
        return false;
    }
    MV_LOG(info, "Input Host: focus, modal, history loss, fresh assignment, blocked context and unread retirement passed");
    return true;
}
bool CConsumer::work_pressure()
{
    const std::uint64_t before = pipes[1].applied;
    SFeed frame;
    frame.kind = EFeed::controller;
    frame.device = 0u;
    frame.value = 1u;
    if (!feed(frame) || !until([&] { return pipes[1].applied == (before + k_expansion_actions + 1u); }))
    {
        return false;
    }
    for (std::uint32_t action = 150u; action < (150u + k_expansion_actions); ++action)
    {
        if (pipes[1].values[action][0] != 1.0f)
        {
            return false;
        }
    }
    frame.value = 0u;
    if (!feed(frame) || !until([&] { return pipes[1].applied == (before + ((k_expansion_actions + 1u) << 1u)); }))
    {
        return false;
    }
    pause_mask = 2u;
    for (std::uint32_t index = 0u; index < 4u; ++index)
    {
        frame.value = (index & 1u) ? 0u : 1u;
        if (!feed(frame)) return false;
    }
    if (!until([&]
        {
            SInputSnapshot state;
            return (pipes[1].view->state->read(state) == EStateRead::success) && state.action_loss;
        }))
    {
        return false;
    }
    frame.device = 1u;
    frame.value = 2u;
    if (!feed(frame) || !until([&] { return pipes[2].values[k_east_action][0] == 1.0f; }))
    {
        return false;
    }
    //  Ordinary service traffic must still complete while the noisy pipe is full.
    for (std::uint32_t index = 0u; index < 16u; ++index)
    {
        if (!command(ECommand::select_context, SSelectContext{ pipes[3].view->connection, (index & 1u) ? 4u : 5u }))
        {
            return false;
        }
    }
    pause_mask = 0u;
    if (!until([&] { return active(); })) return false;
    MV_LOG(info, "Input Host: 65-output continuation, full-pipe fairness and ordinary service progress passed");
    return true;
}
bool CConsumer::shutdown_blocked()
{
    pause_mask = 2u;
    SFeed frame;
    frame.kind = EFeed::controller;
    for (std::uint32_t count = 0u; count < k_action_capacity; ++count)
    {
        frame.value = (count & 1u) ? 0u : 1u;
        if (!feed(frame) || !until([&] { return shared->occupancy[1][0].load() == (count + 1u); }))
        {
            return false;
        }
    }
    if (!command(ECommand::select_context, SSelectContext{ pipes[1].view->connection, 5u }))
    {
        return false;
    }
    SInputSnapshot state;
    if (!until([&] { return pipes[1].view->state->read(state) == EStateRead::success; }) || !(state.pending & k_actions))
    {
        MV_LOG(error, "Input Host: expected a pending action boundary with the action ring full");
        return false;
    }
    pause_mask = (1u << k_pipe_count) - 1u;
    CErasedOwner owner = CErasedOwner::create<modules::ModuleRequest>();
    modules::ModuleRequest* const request = owner.payload<modules::ModuleRequest>();
    if (!request) return false;
    request->module = module_ids::executive;
    request->action = modules::EModuleAction::unload;
    ::threading::CErasedOwnerMsg message;
    message.set_message_type<modules::ModuleRequest>();
    message.set_async_slot(1);
    message.set_owner(std::move(owner));
    shutdown_requested = cleaning = true;
    if (!thread.post(std::move(message)) || !until([&] { return thread.exit_requested(); }))
    {
        return false;
    }
    MV_LOG(info, "Input Host: shutdown requested with full output and blocked boundary");
    return true;
}
bool CConsumer::ingress_loss()
{
    SFeed frame;
    frame.kind = EFeed::key;
    frame.device = 1u;
    for (std::uint32_t count = 0u; count < 2000u; ++count)
    {
        frame.value = (count & 1u) ? 0u : 1u;
        if (!feed(frame)) return false;
    }
    if (!until([&]
        {
            SInputSnapshot state;
            return (pipes[0].view->state->read(state) == EStateRead::success) && state.ingress_loss && active();
        }))
    {
        return false;
    }
    const std::uint64_t final_observation = next_observation;
    if (!until([&] { return shared->drained.load(std::memory_order_acquire) >= final_observation; }))
    {
        return false;
    }
    frame.value = 1u;
    if (!feed(frame) || !until([&] { return pipes[0].values[k_key_a_action][0] == 1.0f; }))
    {
        return false;
    }
    frame.value = 0u;
    if (!feed(frame) || !until([&] { return pipes[0].values[k_key_a_action][0] == 0.0f; }))
    {
        return false;
    }
    MV_LOG(info, "Input Host: desktop ingress overflow and fresh-input recovery passed");
    return !shared->feed_loss.load();
}
bool CConsumer::native_pass()
{
#if MV_PLATFORM_WINDOWS
    CWaitDeadline deadline;
    const std::uint64_t duration = option("MORPHIC_INPUT_DURATION", "120") ? 120000u : 60000u;
    if (deadline.set_after_milliseconds(duration).code != EWaitError::none)
    {
        return false;
    }
    std::uint64_t display = 0u;
    bool held[3]{};
    for (;;)
    {
        std::uint64_t remaining = 0u;
        if (deadline.remaining_nanoseconds(remaining).code != EWaitError::none)
        {
            return false;
        }
        if (!remaining) break;
        for (std::uint32_t index = 0u; index < 3u; ++index)
        {
            const bool down = pipes[0].values[20u + index][0] != 0.0f;
            if (down && !held[index])
            {
                SInputSnapshot state;
                SPipe& pipe = pipes[1];
                if ((pipe.view->state->read(state) != EStateRead::success) || state.pending)
                {
                    continue;
                }
                if (!pipe.reader->cancel_application(k_actions))
                {
                    continue;
                }
                (void)pipe.reader->take_cancellation();
                std::memset(pipe.values, 0, sizeof(pipe.values));
                const bool changed = index ? command(ECommand::assign_device,
                    SAssignDevice{ pipe.view->connection, (index == 1u) ? k_default_device : k_unassigned_device, state.action_generation }) :
                    command(ECommand::select_context, SSelectContext{ pipe.view->connection, (state.context == 4u) ? 5u : 4u });
                if (!changed || !until([&] { return active(); }) || !pipe.reader->resume_application(pipe.boundary, k_actions))
                {
                    return false;
                }
            }
            held[index] = down;
        }
        std::uint64_t now = 0u;
        if (!sample(now)) return false;
        if (now >= display)
        {
            const SPipe& pipe = pipes[1];
            wchar_t title[512]{};
            swprintf_s(title, L"Real Host input | Key A %.0f | Pad A %.0f LS %.2f,%.2f RS %.2f,%.2f LT %.2f RT %.2f | C=context R=assign U=unassign | %llu applied",
                pipes[0].values[k_key_a_action][0], pipe.values[k_south_action][0], pipe.values[k_left_stick_action][0], pipe.values[k_left_stick_action][1],
                pipe.values[k_right_stick_action][0], pipe.values[k_right_stick_action][1], pipe.values[k_left_trigger_action][0], pipe.values[k_right_trigger_action][0],
                static_cast<unsigned long long>(pipe.applied));
            if (!SetWindowTextW(reinterpret_cast<HWND>(native_window), title))
            {
                return false;
            }
            display = now + (thread.perf_count_conversion().query_ticks_per_second() / 60u);
        }
        if (!cycle(10000000u)) return false;
    }
    MV_LOG(info, "Input Host: native observation window completed; physical result requires user confirmation");
    return true;
#else
    return false;
#endif
}
bool CConsumer::acquisition_cleanup()
{
    if (!release(*pipes[1].borrow)) return false;
    SAcquireController request{ installations[1], k_default_device, 99999u, 4u };
    SBorrow* rejected = nullptr;
    if (acquire(input_route, static_cast<std::uint16_t>(ECommand::acquire_controller), &request, sizeof(request), rejected,
        EServiceStatus::invalid_request) || !rejected || !rejected->terminal || rejected->token.value ||
        (rejected->status != EServiceStatus::invalid_request) || !release(*rejected))
    {
        return false;
    }
    request.profile = 8u;
    if (!acquire(input_route, static_cast<std::uint16_t>(ECommand::acquire_controller), &request, sizeof(request), pipes[1].borrow))
    {
        return false;
    }
    //  Deliberately retire the prepared borrow before installing a reader or latch.
    if (!release(*pipes[1].borrow) || !acquire_pipe(1u) || !until([&] { return active(); }))
    {
        return false;
    }
    MV_LOG(info, "Input Host: rejected and prepared-only acquisitions retired before reuse");
    return true;
}
bool CConsumer::load()
{
    const bool smoke = option("MORPHIC_INPUT_CASE", "load-smoke");
    const std::uint32_t rate = option("MORPHIC_INPUT_RATE", "120") ? 120u : (option("MORPHIC_INPUT_RATE", "500") ? 500u : 1000u);
    const std::uint32_t controllers = option("MORPHIC_INPUT_PIPES", "1") ? 1u : 4u;
    const std::uint64_t frequency = thread.perf_count_conversion().query_ticks_per_second();
    if (!frequency) return false;
    std::uint64_t initial_applied[k_pipe_count]{}, expected[k_pipe_count]{};
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index) initial_applied[index] = pipes[index].applied;
    SFeed frame;
    frame.kind = EFeed::controller;
    std::uint64_t pattern = 0u;
    for (std::uint32_t pass = 0u; pass < 4u; ++pass)
    {
        const std::uint64_t duration_ms = smoke ? 500u : (pass ? 10000u : 2000u);
        std::uint64_t begin = 0u;
        if (!sample(begin)) return false;
        const std::uint64_t end = begin + ((frequency * duration_ms) / 1000u);
        std::uint64_t tick = 0u;
        if (pass)
        {
            measuring = &windows[window_count++];
            measuring->begin = begin;
            if (platform::threading::SAwakeClock{}.read(measuring->awake_begin).code != EWaitError::none)
            {
                return false;
            }
            measuring->rate = rate;
            measuring->controllers = controllers;
            measuring->wait_begin = waits;
            measuring->round_begin = rounds;
        }
        for (;;)
        {
            std::uint64_t now = 0u;
            if (!sample(now) || (now < begin))
            {
                return false;
            }
            if (now >= end) break;
            const std::uint64_t due = begin + ((tick * frequency) / rate);
            if (now >= due)
            {
                intended = due;
                for (std::uint32_t device = 0u; device < controllers; ++device)
                {
                    frame.device = device;
                    frame.value = (pattern & 1u) ? 0u : 1u;
                    frame.left_x = frame.value ? 0.75f : 0.0f;
                    if (!feed(frame)) return false;
                    expected[device + 1u] += 2u;
                }
                if (!(pattern & 15u))
                {
                    SFeed key;
                    key.kind = EFeed::key;
                    key.device = 1u;
                    key.value = (pattern & 16u) ? 0u : 1u;
                    if (!feed(key)) return false;
                    ++expected[0];
                }
                ++tick;
                ++pattern;
                if (!cycle(0u)) return false;
            }
            else
            {
                std::uint64_t wait_ns = 0u;
                if (!thread.perf_count_conversion().to_nanoseconds_u64((due - now), wait_ns) || !cycle(wait_ns))
                {
                    return false;
                }
            }
        }
        if (measuring)
        {
            if (!sample(measuring->end)) return false;
            if (platform::threading::SAwakeClock{}.read(measuring->awake_end).code != EWaitError::none)
            {
                return false;
            }
            measuring->wait_end = waits;
            measuring->round_end = rounds;
        }
        measuring = nullptr;
        intended = 0u;
    }
    for (std::uint32_t device = 0u; device < controllers; ++device)
    {
        frame.device = device;
        frame.value = 0u;
        frame.left_x = 0.0f;
        if (!feed(frame)) return false;
        if (pattern & 1u) expected[device + 1u] += 2u;
    }
    const std::uint64_t final_observation = next_observation;
    if (!until([&] { return shared->drained.load(std::memory_order_acquire) >= final_observation; }))
    {
        return false;
    }
    for (SPipe& pipe : pipes) pipe.drained = false;
    if (!until([&]
        {
            if ((shared->drained.load(std::memory_order_acquire) < final_observation) || !active())
            {
                return false;
            }
            for (const SPipe& pipe : pipes)
            {
                if (!pipe.drained || pipe.has_retained)
                {
                    return false;
                }
            }
            return true;
        }))
    {
        return false;
    }
    if (shared->feed_loss.load() || shared->history_loss.load())
    {
        MV_LOG(error, "Input Host: source loss feed {} history {}", shared->feed_loss.load(), shared->history_loss.load());
        return false;
    }
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {
        if ((pipes[index].applied - initial_applied[index]) != expected[index])
        {
            return false;
        }
    }
    for (SPipe& pipe : pipes)
    {
        SInputSnapshot state;
        if ((pipe.view->state->read(state) != EStateRead::success) || state.action_loss || state.text_loss || state.ingress_loss)
        {
            MV_LOG(error, "Input Host: load loss actions {} text {} ingress {}", state.action_loss, state.text_loss, state.ingress_loss);
            return false;
        }
    }
    return true;
}
bool CConsumer::pressure()
{
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {
        SPipe& pipe = pipes[index];
        if (!until([&] { return active() && !shared->occupancy[index][0].load(); }))
        {
            return false;
        }
        SInputSnapshot before;
        if (pipe.view->state->read(before) != EStateRead::success)
        {
            return false;
        }
        const std::uint64_t cancellations = pipe.cancellations;
        pause_mask = 1u << index;
        SFeed frame;
        frame.kind = index ? EFeed::controller : EFeed::key;
        frame.device = index ? (index - 1u) : 1u;
        for (std::uint32_t count = 0u; count < k_action_capacity; ++count)
        {
            frame.value = (count & 1u) ? 0u : 1u;
            if (!feed(frame) || !until([&] { return shared->occupancy[index][0].load() == (count + 1u); }))
            {
                return false;
            }
            if ((count + 1u) == (k_action_capacity - 1u))
            {
                MV_LOG(info, "Input Host: pipe {} action capacity minus one observed", index);
            }
        }
        frame.value = 1u;
        if (!feed(frame) || !until([&]
            {
                SInputSnapshot state;
                return (pipe.view->state->read(state) == EStateRead::success) && (state.action_loss > before.action_loss);
            }))
        {
            return false;
        }
        if (shared->occupancy[index][0].load() != k_action_capacity)
        {
            return false;
        }
        //  A control request and another pipe must progress while this ring is full.
        const std::uint32_t other = (index == 1u) ? 2u : 1u;
        SFeed quiet;
        quiet.kind = EFeed::controller;
        quiet.device = other - 1u;
        quiet.value = 2u;
        if (!feed(quiet) || !until([&] { return pipes[other].values[k_east_action][0] == 1.0f; }))
        {
            return false;
        }
        quiet.value = 0u;
        if (!feed(quiet) || !until([&] { return pipes[other].values[k_east_action][0] == 0.0f; }))
        {
            return false;
        }
        pause_mask = 0u;
        if (!until([&] { return active() && (pipe.cancellations > cancellations); }))
        {
            return false;
        }
        frame.value = 0u;
        if (!feed(frame) || !settle(30u))
        {
            return false;
        }
        frame.value = 1u;
        const std::uint32_t action = index ? 100u : 7u;
        if (!feed(frame) || !until([&] { return pipe.values[action][0] == 1.0f; }))
        {
            return false;
        }
        frame.value = 0u;
        if (!feed(frame) || !until([&] { return pipe.values[action][0] == 0.0f; }))
        {
            return false;
        }
        MV_LOG(info, "Input Host: pipe {} full, overflow, isolation and neutral recovery passed", index);
    }
    SInputSnapshot desktop;
    if (pipes[0].view->state->read(desktop) != EStateRead::success)
    {
        return false;
    }
    if (!command(ECommand::enable_text, STextTarget{ pipes[0].view->connection, desktop.transition, 1u }) ||
        !until([&] { return active() && !shared->occupancy[0][1].load(); }))
    {
        return false;
    }
    if (pipes[0].view->state->read(desktop) != EStateRead::success)
    {
        return false;
    }
    pause_mask = 1u;
    SFeed text;
    text.kind = EFeed::text;
    text.value = 'x';
    for (std::uint32_t count = 0u; count < k_text_capacity; ++count)
    {
        if (!feed(text) || !until([&] { return shared->occupancy[0][1].load() == (count + 1u); }))
        {
            return false;
        }
    }
    if (!feed(text) || !until([&]
        {
            SInputSnapshot state;
            return (pipes[0].view->state->read(state) == EStateRead::success) && (state.text_loss > desktop.text_loss);
        }))
    {
        return false;
    }
    pause_mask = 0u;
    if (!until([&] { return active(); })) return false;
    const std::uint64_t applied = pipes[0].applied;
    if (!feed(text) || !until([&] { return pipes[0].applied > applied; }))
    {
        return false;
    }
    MV_LOG(info, "Input Host: text capacity boundaries, overflow and recovery passed");
    for (const std::uint64_t duration : { 25u, 100u, 500u })
    {
        pause_mask = 2u;
        std::uint64_t begin = 0u, end = 0u, elapsed = 0u;
        if (!sample(begin)) return false;
        SFeed frame;
        frame.kind = EFeed::controller;
        frame.device = 0u;
        for (std::uint32_t count = 0u; count < 300u; ++count)
        {
            frame.value = (count & 1u) ? 0u : 1u;
            if (!feed(frame)) return false;
        }
        if (!settle(duration) || !sample(end) || (end < begin) ||
            !thread.perf_count_conversion().to_nanoseconds_u64((end - begin), elapsed))
        {
            return false;
        }
        pause_mask = 0u;
        if (!until([&] { return active(); })) return false;
        MV_LOG(info, "Input Host: selected-pipe pause requested {} ms, measured {} ns", duration, elapsed);
    }
    pause_mask = (1u << k_pipe_count) - 1u;
    if (!settle(100u)) return false;
    pause_mask = 0u;
    return until([&] { return active(); });
}
bool CConsumer::second_consumer()
{
    const std::uint64_t old_connection = pipes[1].view->connection.value;
    if (!release(*pipes[1].borrow) || !module(modules::EModuleAction::load, module_ids::rendering, true))
    {
        return false;
    }
    if (!until([&]
        {
            const std::uint64_t current = shared->connection[1].load();
            return current && (current != old_connection) && (shared->ready_connection[1].load() == current);
        }))
    {
        return false;
    }
    SFeed frame;
    frame.kind = EFeed::controller;
    frame.device = 0u;
    frame.value = 1u;
    if (!feed(frame) || !settle(100u))
    {
        return false;
    }
    frame.value = 0u;
    if (!feed(frame) || !until([&] { return shared->connection[1].load() == 0u; }))
    {
        return false;
    }
    if (!module(modules::EModuleAction::unload, module_ids::rendering, true))
    {
        return false;
    }
    if (!acquire_pipe(1u) || !until([&] { return active(); }))
    {
        return false;
    }
    if (pipes[1].view->connection.value == old_connection)
    {
        return false;
    }
    frame.value = 1u;
    if (!feed(frame) || !until([&] { return pipes[1].values[k_south_action][0] == 1.0f; }))
    {
        return false;
    }
    frame.value = 0u;
    return feed(frame) && until([&] { return pipes[1].values[k_south_action][0] == 0.0f; });
}

}
