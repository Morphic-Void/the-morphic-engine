
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    execution_scenarios.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Explicit acceptance scenario steps; all waits return to the Host preamble.

#include "tests/input_host/execution.hpp"
#include "framework/platform/controllers.hpp"
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace input_host
{
using namespace services;
using namespace platform::input;
static constexpr std::uint32_t k_key_a_action = 7u, k_south_action = 100u, k_east_action = 101u;

void CInputExecution::advance_scenario() noexcept
{
    if (option("MORPHIC_INPUT_CASE", "source-cleanup")) source_cleanup();
    else if (option("MORPHIC_INPUT_CASE", "acquisition-cleanup")) acquisition_cleanup();
    else if (option("MORPHIC_INPUT_CASE", "history-freshness")) history_freshness();
    else if (option("MORPHIC_INPUT_CASE", "ingress-loss")) ingress_loss();
    else if (option("MORPHIC_INPUT_CASE", "work-pressure")) work_pressure();
    else if (option("MORPHIC_INPUT_CASE", "shutdown-blocked")) shutdown_blocked();
    else if (option("MORPHIC_INPUT_CASE", "load") || option("MORPHIC_INPUT_CASE", "load-smoke")) load();
    else if (option("MORPHIC_INPUT_CASE", "pressure")) pressure();
    else if (option("MORPHIC_INPUT_CASE", "transitions")) transitions();
    else if (option("MORPHIC_INPUT_CASE", "second-consumer")) second_consumer();
    else fail(); // Remaining scenarios are migrated explicitly, never silently passed.
}

void CInputExecution::source_cleanup() noexcept
{
    if (source_active)
    {
        if (!source.complete()) return;
        source_active = false;
        ++scenario_count;
        arm_stage();
        runnable = true;
        return;
    }
    if (scenario_count == 2u)
    {
        MV_LOG(info, "Input Host: late source transfer/acquisition ownership retired during cleanup");
        next_stage(EStage::freeze);
        return;
    }
    // Cleanup intent exists before publication. Late successful results convey
    // custody which the helper releases/disposes, without accessing their bytes.
    const std::uint8_t bytes[32]{};
    const CSourceExecution::EMode mode = scenario_count ? CSourceExecution::EMode::acquire_cleanup : CSourceExecution::EMode::transfer_cleanup;
    source_captured = false;
    source_active = source.start(bytes, sizeof(bytes), {}, {}, mode);
    if (!source_active) fail();
}

void CInputExecution::acquisition_cleanup() noexcept
{
    switch (scenario_step)
    {
        case 0u:
        {
            close_reader(1u);
            scenario_step = 1u;
            runnable = true;
            break;
        }
        case 1u:
        {
            if (borrows.record(4u).live || pipes[1].pending) return;
            const SAcquireController request{ installations[1], k_default_device, 99999u, 4u };
            if (!begin_acquire(4u, input_route, static_cast<std::uint16_t>(ECommand::acquire_controller),
                &request, sizeof(request), EStatus::invalid_request)) fail();
            scenario_step = 2u;
            arm_stage();
            break;
        }
        case 2u:
        {
            const CBorrowExecution::SRecord& record = borrows.record(4u);
            if (!record.terminal) return;
            if (record.token.value || (record.status != EStatus::invalid_request))
            {
                fail();
                return;
            }
            borrows.close_local(4u);
            scenario_step = 3u;
            runnable = true;
            break;
        }
        case 3u:
        {
            if (borrows.record(4u).live) return;
            const SAcquireController request{ installations[1], k_default_device, 8u, 4u };
            if (!begin_acquire(4u, input_route, static_cast<std::uint16_t>(ECommand::acquire_controller), &request, sizeof(request))) fail();
            scenario_step = 4u;
            arm_stage();
            break;
        }
        case 4u:
        {
            if (!borrows.record(4u).prepared) return;
            // No input reader or notification was installed for this descriptor.
            borrows.close_local(4u);
            scenario_step = 5u;
            runnable = true;
            break;
        }
        case 5u:
        {
            if (borrows.record(4u).live) return;
            SPipe& pipe = pipes[1];
            pipe.reader.reset();
            pipe.last_sequence = pipe.pending = 0u;
            pipe.has_retained = pipe.drained = pipe.boundary_prepared = pipe.boundary_acknowledged = false;
            pipe.retained = {};
            pipe.retained_trace = {};
            std::memset(pipe.values, 0, sizeof(pipe.values));
            index = 1u;
            reacquiring = true;
            scenario_step = 6u;
            stage = EStage::setup;
            next_setup(ESetup::pipe_acquire);
            break;
        }
        case 6u:
        {
            MV_LOG(info, "Input Host: rejected and prepared-only acquisitions retired before reuse");
            next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::next_scenario(const unsigned step) noexcept
{
    scenario_step = step;
    arm_stage();
    runnable = true;
}

void CInputExecution::history_freshness() noexcept
{
    if (!shared)
    {
        fail();
        return;
    }
    SFeed frame;
    frame.kind = EFeed::controller;
    switch (scenario_step)
    {
        case 0u:
        {
            frame.kind = EFeed::focus;
            if (!feed(frame)) fail();
            scenario_observation = next_observation;
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            if (shared->consumed.load() < scenario_observation)
            {
                poll();
                return;
            }
            scenario_loss = shared->history_loss.load();
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(2u);
            break;
        }
        case 2u:
        {
            if (shared->history_loss.load() <= scenario_loss)
            {
                poll();
                return;
            }
            frame.kind = EFeed::focus;
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(3u);
            break;
        }
        case 3u:
        {
            if (!active())
            {
                poll();
                return;
            }
            if (pipes[1].values[100u][0] != 0.0f) fail();
            if (!feed(frame)) fail(); // Neutral precedes a fresh press.
            scenario_observation = next_observation;
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            if ((shared->drained.load() < scenario_observation) || !active())
            {
                poll();
                return;
            }
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(5u);
            break;
        }
        case 5u:
        {
            if (pipes[1].values[100u][0] != 1.0f)
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(6u);
            break;
        }
        case 6u:
        {
            if (pipes[1].values[100u][0] != 0.0f)
            {
                poll();
                return;
            }
            MV_LOG(info, "Input Host: stale controller history discarded, held baseline suppressed, neutral and fresh press recovered");
            next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::ingress_loss() noexcept
{
    if (!shared)
    {
        fail();
        return;
    }
    SFeed frame;
    frame.kind = EFeed::key;
    frame.device = 1u;
    switch (scenario_step)
    {
        case 0u:
        {
            // Preserve the original finite 2,000-record burst before servicing
            // consumer input. Splitting it into quiet turns would weaken loss.
            for (unsigned count = 0u; count < 2000u; ++count)
            {
                frame.value = (count & 1u) ? 0u : 1u;
                if (!feed(frame))
                {
                    fail();
                    return;
                }
            }
            scenario_observation = next_observation;
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            SInputSnapshot snapshot;
            if ((pipes[0].view->state->read(snapshot) != EStateRead::success) || !snapshot.ingress_loss ||
                !active() || (shared->drained.load(std::memory_order_acquire) < scenario_observation))
            {
                poll();
                return;
            }
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(2u);
            break;
        }
        case 2u:
        {
            if (pipes[0].values[7u][0] != 1.0f)
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(3u);
            break;
        }
        case 3u:
        {
            if (pipes[0].values[7u][0] != 0.0f)
            {
                poll();
                return;
            }
            if (shared->feed_loss.load()) fail();
            MV_LOG(info, "Input Host: desktop ingress overflow and fresh-input recovery passed");
            next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::work_pressure() noexcept
{
    SFeed frame;
    frame.kind = EFeed::controller;
    switch (scenario_step)
    {
        case 0u:
        {
            scenario_applied = pipes[1].applied;
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            if (pipes[1].applied != (scenario_applied + k_expansion_actions + 1u))
            {
                poll();
                return;
            }
            for (unsigned action = 150u; action < (150u + k_expansion_actions); ++action)
            {
                if (pipes[1].values[action][0] != 1.0f) fail();
            }
            if (!feed(frame)) fail();
            next_scenario(2u);
            break;
        }
        case 2u:
        {
            if (pipes[1].applied != (scenario_applied + ((k_expansion_actions + 1u) << 1u)))
            {
                poll();
                return;
            }
            pause_mask = 2u;
            for (unsigned count = 0u; count < 4u; ++count)
            {
                frame.value = (count & 1u) ? 0u : 1u;
                if (!feed(frame)) fail();
            }
            next_scenario(3u);
            break;
        }
        case 3u:
        {
            SInputSnapshot snapshot;
            if ((pipes[1].view->state->read(snapshot) != EStateRead::success) || !snapshot.action_loss)
            {
                poll();
                return;
            }
            frame.device = 1u;
            frame.value = 2u;
            if (!feed(frame)) fail();
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            if (pipes[2].values[101u][0] != 1.0f)
            {
                poll();
                return;
            }
            scenario_count = 0u;
            next_scenario(5u);
            break;
        }
        case 5u:
        {
            if (operation.kind == EOperation::none)
            {
                if (!command(ECommand::select_context, SSelectContext{ pipes[3].view->connection, (scenario_count & 1u) ? 4u : 5u })) fail();
            }
            else if (operation.done)
            {
                clear_operation();
                ++scenario_count;
                if (scenario_count == 16u)
                {
                    pause_mask = 0u;
                    next_scenario(6u);
                }
                else
                {
                    arm_stage();
                    runnable = true;
                }
            }
            break;
        }
        case 6u:
        {
            if (!active())
            {
                poll();
                return;
            }
            MV_LOG(info, "Input Host: 65-output continuation, full-pipe fairness and ordinary service progress passed");
            next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::shutdown_blocked() noexcept
{
    if (!shared)
    {
        fail();
        return;
    }
    switch (scenario_step)
    {
        case 0u:
        {
            pause_mask = 2u;
            SFeed frame;
            frame.kind = EFeed::controller;
            frame.value = (scenario_count & 1u) ? 0u : 1u;
            if (!feed(frame)) fail();
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            if (shared->occupancy[1][0].load() != (scenario_count + 1u))
            {
                poll();
                return;
            }
            ++scenario_count;
            next_scenario((scenario_count == k_action_capacity) ? 2u : 0u);
            break;
        }
        case 2u:
        {
            if (operation.kind == EOperation::none)
            {
                if (!command(ECommand::select_context, SSelectContext{ pipes[1].view->connection, 5u })) fail();
            }
            else if (operation.done)
            {
                clear_operation();
                next_scenario(3u);
            }
            break;
        }
        case 3u:
        {
            SInputSnapshot snapshot;
            if (pipes[1].view->state->read(snapshot) != EStateRead::success)
            {
                poll();
                return;
            }
            if (!(snapshot.pending & k_actions))
            {
                fail();
                return;
            }
            pause_mask = (1u << k_pipe_count) - 1u;
            blocked_shutdown = true;
            if (!begin_module(modules::EModuleAction::unload, module_ids::executive)) fail();
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            // The accepted self-unload is correlated with the following stop.
            // Its ordinary outcomes carry no resource; Host cancels unread ones.
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::native_pass() noexcept
{
#if MV_PLATFORM_WINDOWS
    if (!scenario_step)
    {
        const std::uint64_t duration = option("MORPHIC_INPUT_DURATION", "120") ? 120000u : 60000u;
        if (native_deadline.set_after_milliseconds(duration).code != platform::threading::EWaitError::none)
        {
            fail();
            return;
        }
        scenario_step = 1u;
        arm_stage(duration + 10000u);
    }
    if (scenario_step == 2u)
    {
        if (!operation.done) return;
        clear_operation();
        scenario_step = 3u;
    }
    if (scenario_step == 3u)
    {
        if (!active())
        {
            poll();
            return;
        }
        if (!pipes[1].reader->resume_application(pipes[1].boundary, k_actions))
        {
            fail();
            return;
        }
        native_held[index] = true;
        scenario_step = 1u;
    }
    std::uint64_t remaining = 0u;
    if (native_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
    {
        fail();
        return;
    }
    if (!remaining)
    {
        MV_LOG(info, "Input Host: native observation window completed; physical result requires user confirmation");
        if (shared) next_stage(EStage::freeze);
        else begin_cleanup();
        return;
    }
    for (unsigned control = 0u; control < 3u; ++control)
    {
        const bool down = pipes[0].values[20u + control][0] != 0.0f;
        if (down && !native_held[control])
        {
            SInputSnapshot state;
            SPipe& pipe = pipes[1];
            const EStateRead read = pipe.view->state->read(state);
            if (read == EStateRead::retry)
            {
                poll();
                return;
            }
            if (read != EStateRead::success)
            {
                fail();
                return;
            }
            if (state.pending || !pipe.reader->cancel_application(k_actions))
            {
                continue;
            }
            (void)pipe.reader->take_cancellation();
            std::memset(pipe.values, 0, sizeof(pipe.values));
            const bool started = control ? command(ECommand::assign_device,
                SAssignDevice{ pipe.view->connection, (control == 1u) ? k_default_device : k_unassigned_device, state.action_generation }) :
                command(ECommand::select_context, SSelectContext{ pipe.view->connection, (state.context == 4u) ? 5u : 4u });
            if (!started) fail();
            index = control;
            scenario_step = 2u;
            arm_stage();
            return;
        }
        native_held[control] = down;
    }
    std::uint64_t now = 0u;
    if (!sample(now))
    {
        fail();
        return;
    }
    if (now >= native_display)
    {
        const SPipe& pipe = pipes[1];
        wchar_t title[512]{};
        swprintf_s(title, L"Real Host input | Key A %.0f | Pad A %.0f LS %.2f,%.2f RS %.2f,%.2f LT %.2f RT %.2f | C=context R=assign U=unassign | %llu applied",
            pipes[0].values[k_key_a_action][0], pipe.values[k_south_action][0], pipe.values[114u][0], pipe.values[114u][1],
            pipe.values[115u][0], pipe.values[115u][1], pipe.values[116u][0], pipe.values[117u][0],
            static_cast<unsigned long long>(pipe.applied));
        if (!SetWindowTextW(reinterpret_cast<HWND>(native_window), title))
        {
            fail();
            return;
        }
        native_display = now + (conversion.query_ticks_per_second() / 60u);
    }
    // The observation's original end time survives commands and their ACKs.
    // Stage timeouts cover bounded command/boundary progress, then return to
    // the remaining observation duration without extending that end time.
    arm_stage((remaining / 1000000u) + 10000u);
    next_wake = std::min(next_wake, std::min(remaining, UINT64_C(10000000)));
#else
    fail();
#endif
}

void CInputExecution::second_consumer() noexcept
{
    SFeed frame;
    frame.kind = EFeed::controller;
    switch (scenario_step)
    {
        case 0u:
        {
            scenario_observation = pipes[1].view->connection.value;
            close_reader(1u);
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            if (borrows.record(4u).live || pipes[1].pending)
            {
                return;
            }
            if (!begin_module(modules::EModuleAction::load, module_ids::rendering)) fail();
            next_scenario(2u);
            break;
        }
        case 2u:
        {
            if (!operation.done) return;
            const std::uint64_t current = shared->connection[1].load();
            if (!current || (current == scenario_observation) || (shared->ready_connection[1].load() != current))
            {
                poll();
                return;
            }
            clear_operation();
            frame.value = 1u;
            if (!feed(frame)) fail();
            if (settle_deadline.set_after_milliseconds(100u).code != platform::threading::EWaitError::none) fail();
            next_scenario(3u);
            break;
        }
        case 3u:
        {
            std::uint64_t remaining = 0u;
            if (settle_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                fail();
                return;
            }
            if (remaining)
            {
                next_wake = std::min(next_wake, remaining);
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            if (shared->connection[1].load() || !shared->secondary_cleanup_waiting.load())
            {
                poll();
                return;
            }
            if (shared->secondary_cleanup_complete.load())
            {
                fail();
                return;
            }
            MV_LOG(info, "Input Host: retired pipe observed while secondary cleanup is blocked");
            if (!begin_command(control_route, k_release_secondary_cleanup, nullptr, 0u)) fail();
            next_scenario(5u);
            break;
        }
        case 5u:
        {
            if (!operation.done || shared->connection[1].load() || !shared->secondary_cleanup_complete.load())
            {
                poll();
                return;
            }
            clear_operation();
            if (!begin_module(modules::EModuleAction::unload, module_ids::rendering)) fail();
            next_scenario(6u);
            break;
        }
        case 6u:
        {
            if (!operation.done) return;
            clear_operation();
            SPipe& pipe = pipes[1];
            pipe.reader.reset();
            pipe.last_sequence = pipe.pending = 0u;
            pipe.has_retained = pipe.drained = pipe.boundary_prepared = pipe.boundary_acknowledged = false;
            pipe.retained = {};
            pipe.retained_trace = {};
            std::memset(pipe.values, 0, sizeof(pipe.values));
            index = 1u;
            reacquiring = true;
            scenario_step = 7u;
            next_setup(ESetup::pipe_acquire);
            next_stage(EStage::setup);
            break;
        }
        case 7u:
        {
            if (pipes[1].view->connection.value == scenario_observation)
            {
                fail();
                return;
            }
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(8u);
            break;
        }
        case 8u:
        {
            if (pipes[1].values[k_south_action][0] != 1.0f)
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(9u);
            break;
        }
        case 9u:
        {
            if (pipes[1].values[k_south_action][0] != 0.0f) poll();
            else next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::transitions() noexcept
{
    const auto delay = [&](const std::uint64_t milliseconds, const unsigned step)
    {
        if (settle_deadline.set_after_milliseconds(milliseconds).code != platform::threading::EWaitError::none) fail();
        next_scenario(step);
    };
    const auto elapsed = [&]() -> bool
    {
        std::uint64_t remaining = 0u;
        if (settle_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
        {
            fail();
            return false;
        }
        if (!remaining) return true;
        next_wake = std::min(next_wake, remaining);
        return false;
    };
    const auto snapshot = [&](SInputSnapshot& state) -> bool
    {
        const EStateRead read = pipes[1].view->state->read(state);
        if (read == EStateRead::retry) poll();
        else if (read != EStateRead::success) fail();
        return read == EStateRead::success;
    };
    SFeed frame;
    frame.kind = EFeed::controller;
    SFeed change;
    change.kind = (scenario_count == 0u) ? EFeed::focus : ((scenario_count == 1u) ? EFeed::modal : EFeed::history_loss);
    switch (scenario_step)
    {
        case 0u:
        {
            frame.device = index;
            frame.value = 1u;
            frame.left_x = 0.75f;
            if (!feed(frame)) fail();
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            if (pipes[index + 1u].values[k_south_action][0] != 1.0f)
            {
                poll();
                return;
            }
            ++index;
            next_scenario((index == 4u) ? 2u : 0u);
            break;
        }
        case 2u:
        {
            scenario_applied = pipes[1].cancellations;
            if (!feed(change)) fail();
            scenario_observation = next_observation;
            next_scenario(scenario_count ? 4u : 3u);
            break;
        }
        case 3u:
        {
            if (pipes[1].values[k_south_action][0] != 0.0f)
            {
                poll();
                return;
            }
            change.value = 1u;
            if (!feed(change)) fail();
            scenario_observation = next_observation;
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            if ((shared->consumed.load() < scenario_observation) ||
                (pipes[1].cancellations <= scenario_applied) || !active())
            {
                poll();
                return;
            }
            if (pipes[1].values[k_south_action][0] != 0.0f)
            {
                fail();
                return;
            }
            frame.value = 1u;
            frame.left_x = 0.8f;
            if (!feed(frame)) fail();
            delay(30u, 5u);
            break;
        }
        case 5u:
        {
            if (!elapsed()) return;
            if (pipes[1].values[k_south_action][0] != 0.0f)
            {
                fail();
                return;
            }
            frame.left_x = 0.8f;
            if (!feed(frame)) fail();
            delay(20u, 6u);
            break;
        }
        case 6u:
        {
            if (!elapsed()) return;
            frame.value = 1u;
            frame.left_x = 0.8f;
            if (!feed(frame)) fail();
            next_scenario(7u);
            break;
        }
        case 7u:
        {
            if (pipes[1].values[k_south_action][0] != 1.0f)
            {
                poll();
                return;
            }
            // The button rearms independently of the still-held stick.
            if (pipes[1].values[114u][0] != 0.0f)
            {
                fail();
                return;
            }
            for (unsigned device = 0u; device < 4u; ++device)
            {
                frame.device = device;
                if (!feed(frame)) fail();
            }
            delay(30u, 8u);
            break;
        }
        case 8u:
        {
            if (!elapsed()) return;
            ++scenario_count;
            index = 0u;
            next_scenario((scenario_count == 3u) ? 9u : 0u);
            break;
        }
        case 9u:
        {
            SInputSnapshot state;
            if (!snapshot(state)) return;
            scenario_loss = state.device;
            change = {};
            change.kind = EFeed::arrival;
            if (!feed(change)) fail();
            next_scenario(10u);
            break;
        }
        case 10u:
        {
            SInputSnapshot state;
            if (!snapshot(state)) return;
            if (state.device || !active())
            {
                poll();
                return;
            }
            change = {};
            change.kind = EFeed::arrival;
            change.value = 1u;
            if (!feed(change)) fail();
            scenario_observation = next_observation;
            next_scenario(11u);
            break;
        }
        case 11u:
        {
            if (shared->consumed.load() < scenario_observation)
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(12u);
            break;
        }
        case 12u:
        {
            SInputSnapshot state;
            if (!snapshot(state)) return;
            scenario_generation = state.action_generation;
            if (!command(ECommand::assign_device, SAssignDevice{ pipes[1].view->connection, scenario_loss, scenario_generation }, EStatus::stale)) fail();
            next_scenario(13u);
            break;
        }
        case 13u:
        {
            if (!operation.done) return;
            clear_operation();
            if (!command(ECommand::assign_device, SAssignDevice{ pipes[1].view->connection, k_default_device, scenario_generation })) fail();
            next_scenario(14u);
            break;
        }
        case 14u:
        {
            if (!operation.done || !active())
            {
                poll();
                return;
            }
            SInputSnapshot state;
            if (!snapshot(state)) return;
            if (!state.device || (state.device == scenario_loss))
            {
                fail();
                return;
            }
            clear_operation();
            next_scenario(15u);
            break;
        }
        case 15u:
        {
            if (shared->occupancy[1][0].load())
            {
                poll();
                return;
            }
            pause_mask = 2u;
            scenario_count = 0u;
            next_scenario(16u);
            break;
        }
        case 16u:
        case 24u:
        {
            frame.value = (scenario_count & 1u) ? 0u : 1u;
            if (!feed(frame)) fail();
            next_scenario(scenario_step + 1u);
            break;
        }
        case 17u:
        case 25u:
        {
            if (shared->occupancy[1][0].load() != (scenario_count + 1u))
            {
                poll();
                return;
            }
            ++scenario_count;
            next_scenario((scenario_count == k_action_capacity) ? (scenario_step + 1u) : (scenario_step - 1u));
            break;
        }
        case 18u:
        {
            if (!pipes[1].reader->cancel_application(k_actions))
            {
                fail();
                return;
            }
            (void)pipes[1].reader->take_cancellation();
            std::memset(pipes[1].values, 0, sizeof(pipes[1].values));
            if (!command(ECommand::select_context, SSelectContext{ pipes[1].view->connection, 5u })) fail();
            next_scenario(19u);
            break;
        }
        case 19u:
        {
            if (!operation.done) return;
            SInputSnapshot state;
            if (!snapshot(state)) return;
            if (!(state.pending & k_actions))
            {
                fail();
                return;
            }
            clear_operation();
            pause_mask = 0u;
            next_scenario(20u);
            break;
        }
        case 20u:
        {
            if (!active())
            {
                poll();
                return;
            }
            if (!pipes[1].reader->resume_application(pipes[1].boundary, k_actions))
            {
                fail();
                return;
            }
            if (!feed(frame)) fail();
            delay(20u, 21u);
            break;
        }
        case 21u:
        {
            if (!elapsed()) return;
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(22u);
            break;
        }
        case 22u:
        {
            if (pipes[1].values[k_south_action][0] != 1.0f)
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(23u);
            break;
        }
        case 23u:
        {
            if (pipes[1].values[k_south_action][0] != 0.0f)
            {
                poll();
                return;
            }
            pause_mask = 2u;
            scenario_count = 0u;
            next_scenario(24u);
            break;
        }
        case 26u:
        {
            if (operation.kind == EOperation::none)
            {
                if (!command(ECommand::select_context, SSelectContext{ pipes[1].view->connection, 4u })) fail();
            }
            else if (operation.done)
            {
                clear_operation();
                close_reader(1u);
                next_scenario(27u);
            }
            break;
        }
        case 27u:
        {
            if (borrows.record(4u).live || pipes[1].pending)
            {
                return;
            }
            SPipe& pipe = pipes[1];
            pipe.reader.reset();
            pipe.last_sequence = pipe.pending = 0u;
            pipe.has_retained = pipe.drained = pipe.boundary_prepared = pipe.boundary_acknowledged = false;
            pipe.retained = {};
            pipe.retained_trace = {};
            std::memset(pipe.values, 0, sizeof(pipe.values));
            pause_mask = 0u;
            index = 1u;
            reacquiring = true;
            scenario_step = 28u;
            next_setup(ESetup::pipe_acquire);
            next_stage(EStage::setup);
            break;
        }
        case 28u:
        {
            MV_LOG(info, "Input Host: focus, modal, history loss, fresh assignment, blocked context and unread retirement passed");
            next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::pressure() noexcept
{
    const auto delay = [&](const std::uint64_t milliseconds, const unsigned step)
    {
        if (settle_deadline.set_after_milliseconds(milliseconds).code != platform::threading::EWaitError::none) fail();
        next_scenario(step);
    };
    const auto elapsed = [&]() -> bool
    {
        std::uint64_t remaining = 0u;
        if (settle_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
        {
            fail();
            return false;
        }
        if (!remaining) return true;
        next_wake = std::min(next_wake, remaining);
        return false;
    };
    const auto snapshot = [&](const unsigned pipe, SInputSnapshot& state) -> bool
    {
        const EStateRead read = pipes[pipe].view->state->read(state);
        if (read == EStateRead::retry) poll();
        else if (read != EStateRead::success) fail();
        return read == EStateRead::success;
    };
    SFeed frame;
    frame.kind = index ? EFeed::controller : EFeed::key;
    frame.device = index ? (index - 1u) : 1u;
    const unsigned other = (index == 1u) ? 2u : 1u;
    SFeed quiet;
    quiet.kind = EFeed::controller;
    quiet.device = other - 1u;
    SFeed text;
    text.kind = EFeed::text;
    text.value = 'x';
    switch (scenario_step)
    {
        case 0u:
        {
            // Setup used index as its pipe cursor; the first pressure visit
            // starts independently, subsequent visits keep the next pipe.
            if (index == k_pipe_count) index = 0u;
            if (!active() || shared->occupancy[index][0].load())
            {
                poll();
                return;
            }
            SInputSnapshot state;
            if (!snapshot(index, state)) return;
            scenario_loss = state.action_loss;
            scenario_applied = pipes[index].cancellations;
            scenario_count = 0u;
            pause_mask = 1u << index;
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            frame.value = (scenario_count & 1u) ? 0u : 1u;
            if (!feed(frame)) fail();
            next_scenario(2u);
            break;
        }
        case 2u:
        {
            if (shared->occupancy[index][0].load() != (scenario_count + 1u))
            {
                poll();
                return;
            }
            ++scenario_count;
            if (scenario_count == (k_action_capacity - 1u))
            {
                MV_LOG(info, "Input Host: pipe {} action capacity minus one observed", index);
            }
            if (scenario_count == k_action_capacity)
            {
                frame.value = 1u;
                if (!feed(frame)) fail();
                next_scenario(3u);
            }
            else next_scenario(1u);
            break;
        }
        case 3u:
        {
            SInputSnapshot state;
            if (!snapshot(index, state)) return;
            if (state.action_loss <= scenario_loss)
            {
                poll();
                return;
            }
            if (shared->occupancy[index][0].load() != k_action_capacity)
            {
                fail();
                return;
            }
            quiet.value = 2u;
            if (!feed(quiet)) fail();
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            if (pipes[other].values[k_east_action][0] != 1.0f)
            {
                poll();
                return;
            }
            if (!feed(quiet)) fail();
            next_scenario(5u);
            break;
        }
        case 5u:
        {
            if (pipes[other].values[k_east_action][0] != 0.0f)
            {
                poll();
                return;
            }
            pause_mask = 0u;
            next_scenario(6u);
            break;
        }
        case 6u:
        {
            if (!active() || (pipes[index].cancellations <= scenario_applied))
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            delay(30u, 7u);
            break;
        }
        case 7u:
        {
            if (!elapsed()) return;
            frame.value = 1u;
            if (!feed(frame)) fail();
            next_scenario(8u);
            break;
        }
        case 8u:
        {
            if (pipes[index].values[index ? k_south_action : k_key_a_action][0] != 1.0f)
            {
                poll();
                return;
            }
            if (!feed(frame)) fail();
            next_scenario(9u);
            break;
        }
        case 9u:
        {
            if (pipes[index].values[index ? k_south_action : k_key_a_action][0] != 0.0f)
            {
                poll();
                return;
            }
            MV_LOG(info, "Input Host: pipe {} full, overflow, isolation and neutral recovery passed", index);
            ++index;
            next_scenario((index == k_pipe_count) ? 10u : 0u);
            break;
        }
        case 10u:
        {
            SInputSnapshot state;
            if (!snapshot(0u, state)) return;
            if (!command(ECommand::enable_text, STextTarget{ pipes[0].view->connection, state.transition, 1u })) fail();
            next_scenario(11u);
            break;
        }
        case 11u:
        {
            if (!operation.done || !active() || shared->occupancy[0][1].load())
            {
                poll();
                return;
            }
            SInputSnapshot state;
            if (!snapshot(0u, state)) return;
            clear_operation();
            scenario_loss = state.text_loss;
            pause_mask = 1u;
            scenario_count = 0u;
            next_scenario(12u);
            break;
        }
        case 12u:
        {
            if (!feed(text)) fail();
            next_scenario(13u);
            break;
        }
        case 13u:
        {
            if (shared->occupancy[0][1].load() != (scenario_count + 1u))
            {
                poll();
                return;
            }
            ++scenario_count;
            if (scenario_count == k_text_capacity)
            {
                if (!feed(text)) fail();
                next_scenario(14u);
            }
            else next_scenario(12u);
            break;
        }
        case 14u:
        {
            SInputSnapshot state;
            if (!snapshot(0u, state)) return;
            if (state.text_loss <= scenario_loss)
            {
                poll();
                return;
            }
            pause_mask = 0u;
            next_scenario(15u);
            break;
        }
        case 15u:
        {
            if (!active())
            {
                poll();
                return;
            }
            scenario_applied = pipes[0].applied;
            if (!feed(text)) fail();
            next_scenario(16u);
            break;
        }
        case 16u:
        {
            if (pipes[0].applied <= scenario_applied)
            {
                poll();
                return;
            }
            MV_LOG(info, "Input Host: text capacity boundaries, overflow and recovery passed");
            index = 0u;
            next_scenario(17u);
            break;
        }
        case 17u:
        {
            pause_mask = 2u;
            if (!sample(scenario_observation))
            {
                fail();
                return;
            }
            frame = {};
            frame.kind = EFeed::controller;
            for (unsigned count = 0u; count < 300u; ++count)
            {
                frame.value = (count & 1u) ? 0u : 1u;
                if (!feed(frame)) fail();
            }
            const std::uint64_t duration = (index == 0u) ? 25u : ((index == 1u) ? 100u : 500u);
            delay(duration, 18u);
            break;
        }
        case 18u:
        {
            if (!elapsed()) return;
            std::uint64_t end = 0u;
            if (!sample(end) || (end < scenario_observation) ||
                !conversion.to_nanoseconds_u64((end - scenario_observation), scenario_applied))
            {
                fail();
                return;
            }
            pause_mask = 0u;
            next_scenario(19u);
            break;
        }
        case 19u:
        {
            if (!active())
            {
                poll();
                return;
            }
            const std::uint64_t duration = (index == 0u) ? 25u : ((index == 1u) ? 100u : 500u);
            MV_LOG(info, "Input Host: selected-pipe pause requested {} ms, measured {} ns", duration, scenario_applied);
            ++index;
            next_scenario((index == 3u) ? 20u : 17u);
            break;
        }
        case 20u:
        {
            pause_mask = (1u << k_pipe_count) - 1u;
            delay(100u, 21u);
            break;
        }
        case 21u:
        {
            if (!elapsed()) return;
            pause_mask = 0u;
            next_scenario(22u);
            break;
        }
        case 22u:
        {
            if (!active()) poll();
            else next_stage(EStage::freeze);
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

void CInputExecution::load() noexcept
{
    const bool smoke = option("MORPHIC_INPUT_CASE", "load-smoke");
    const std::uint32_t rate = option("MORPHIC_INPUT_RATE", "120") ? 120u : (option("MORPHIC_INPUT_RATE", "500") ? 500u : 1000u);
    const std::uint32_t controllers = option("MORPHIC_INPUT_PIPES", "1") ? 1u : 4u;
    const std::uint64_t frequency = conversion.query_ticks_per_second();
    if (!frequency)
    {
        fail();
        return;
    }
    switch (scenario_step)
    {
        case 0u:
        {
            for (unsigned pipe = 0u; pipe < k_pipe_count; ++pipe) load_state.initial[pipe] = pipes[pipe].applied;
            next_scenario(1u);
            break;
        }
        case 1u:
        {
            const std::uint64_t duration_ms = smoke ? 500u : (load_state.pass ? 10000u : 2000u);
            if (!sample(load_state.begin))
            {
                fail();
                return;
            }
            load_state.end = load_state.begin + ((frequency * duration_ms) / 1000u);
            load_state.tick = 0u;
            if (load_state.pass)
            {
                measuring = &windows[window_count++];
                measuring->begin = load_state.begin;
                if (platform::threading::SAwakeClock{}.read(measuring->awake_begin).code != platform::threading::EWaitError::none)
                {
                    fail();
                    return;
                }
                measuring->rate = rate;
                measuring->controllers = controllers;
                measuring->wait_begin = wait_requests;
                measuring->round_begin = service_passes;
            }
            next_scenario(2u);
            arm_stage(duration_ms + 10000u);
            break;
        }
        case 2u:
        {
            std::uint64_t now = 0u;
            if (!sample(now) || (now < load_state.begin))
            {
                fail();
                return;
            }
            if (now >= load_state.end)
            {
                if (measuring)
                {
                    measuring->end = now;
                    if (platform::threading::SAwakeClock{}.read(measuring->awake_end).code != platform::threading::EWaitError::none)
                    {
                        fail();
                        return;
                    }
                    measuring->wait_end = wait_requests;
                    measuring->round_end = service_passes;
                }
                measuring = nullptr;
                intended = 0u;
                ++load_state.pass;
                next_scenario((load_state.pass == 4u) ? 3u : 1u);
                break;
            }
            const std::uint64_t due = load_state.begin + ((load_state.tick * frequency) / rate);
            if (now < due)
            {
                std::uint64_t wait_ns = 0u;
                if (!conversion.to_nanoseconds_u64((due - now), wait_ns)) fail();
                else next_wake = std::min(next_wake, wait_ns);
                return;
            }
            // One scheduled observation group per invocation. Catch-up remains
            // bounded and all input/lifecycle service runs between groups.
            intended = due;
            SFeed frame;
            frame.kind = EFeed::controller;
            frame.value = (load_state.pattern & 1u) ? 0u : 1u;
            frame.left_x = frame.value ? 0.75f : 0.0f;
            for (std::uint32_t device = 0u; device < controllers; ++device)
            {
                frame.device = device;
                if (!feed(frame)) fail();
                load_state.expected[device + 1u] += 2u;
            }
            if (!(load_state.pattern & 15u))
            {
                SFeed key;
                key.kind = EFeed::key;
                key.device = 1u;
                key.value = (load_state.pattern & 16u) ? 0u : 1u;
                if (!feed(key)) fail();
                ++load_state.expected[0];
            }
            ++load_state.tick;
            ++load_state.pattern;
            const std::uint64_t next = load_state.begin + ((load_state.tick * frequency) / rate);
            if (next <= now) runnable = true;
            else
            {
                std::uint64_t wait_ns = 0u;
                if (!conversion.to_nanoseconds_u64((next - now), wait_ns)) fail();
                else next_wake = std::min(next_wake, wait_ns);
            }
            break;
        }
        case 3u:
        {
            SFeed frame;
            frame.kind = EFeed::controller;
            for (std::uint32_t device = 0u; device < controllers; ++device)
            {
                frame.device = device;
                if (!feed(frame)) fail();
                if (load_state.pattern & 1u) load_state.expected[device + 1u] += 2u;
            }
            scenario_observation = next_observation;
            next_scenario(4u);
            break;
        }
        case 4u:
        {
            if (shared->drained.load(std::memory_order_acquire) < scenario_observation)
            {
                poll();
                return;
            }
            for (SPipe& pipe : pipes) pipe.drained = false;
            next_scenario(5u);
            break;
        }
        case 5u:
        {
            if (!active())
            {
                poll();
                return;
            }
            for (const SPipe& pipe : pipes)
            {
                if (!pipe.drained || pipe.has_retained)
                {
                    poll();
                    return;
                }
            }
            if (shared->feed_loss.load() || shared->history_loss.load())
            {
                MV_LOG(error, "Input Host: source loss feed {} history {}", shared->feed_loss.load(), shared->history_loss.load());
                fail();
                return;
            }
            for (unsigned pipe = 0u; pipe < k_pipe_count; ++pipe)
            {
                if ((pipes[pipe].applied - load_state.initial[pipe]) != load_state.expected[pipe])
                {
                    MV_LOG(error, "Input Host: pipe {} expected {} load records, applied {}", pipe,
                        load_state.expected[pipe], (pipes[pipe].applied - load_state.initial[pipe]));
                    fail();
                    return;
                }
            }
            index = 0u;
            next_scenario(6u);
            break;
        }
        case 6u:
        {
            SInputSnapshot snapshot;
            const EStateRead read = pipes[index].view->state->read(snapshot);
            if (read == EStateRead::retry)
            {
                poll();
                return;
            }
            if ((read != EStateRead::success) || snapshot.action_loss || snapshot.text_loss || snapshot.ingress_loss)
            {
                MV_LOG(error, "Input Host: load loss actions {} text {} ingress {}", snapshot.action_loss, snapshot.text_loss, snapshot.ingress_loss);
                fail();
                return;
            }
            ++index;
            if (index == k_pipe_count) next_stage(EStage::freeze);
            else runnable = true;
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
}

}   //  namespace input_host
