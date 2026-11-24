
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    execution.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Bounded Input Host consumer operations and affine common-context progress.

#include "tests/input_host/execution.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace input_host
{
using namespace services;
using namespace platform::input;
using platform::threading::EWaitError;

CInputExecution::CInputExecution(const modules::SExecutionContext& context, const bool is_secondary) noexcept
    : communication{ *context.communication }, instance{ context.instance }, endpoint{ context.endpoint },
      source{ communication }, borrows{ communication }, secondary{ is_secondary }
{
    native = option("MORPHIC_INPUT_SOURCE", "native");
    ordinary = option("MORPHIC_INPUT_SOURCE", "ordinary");
    if (secondary) setup = ESetup::control_route;
    if (!conversion.init()) fail();
    arm_stage();
}

bool CInputExecution::option(const char* const name, const char* const expected) noexcept
{
#if MV_PLATFORM_WINDOWS
    char value[64]{};
    std::size_t size = 0u;
    return (getenv_s(&size, value, sizeof(value), name) == 0) && (std::strcmp(value, expected) == 0);
#else
    const char* const value = std::getenv(name);
    return value && (std::strcmp(value, expected) == 0);
#endif
}

bool CInputExecution::complete() const noexcept
{
    return stopping && !unsafe && (stage == EStage::done) && source.complete() && borrows.complete() &&
        (framework_shutdown || (!installations[0].value && !installations[1].value));
}

void CInputExecution::fail(const bool retain) noexcept
{
    if (!failed)
    {
        MV_LOG(error, "Input Host: failed stage {}, setup {}, step {}, cleanup {}, retain {}",
            static_cast<unsigned>(stage), static_cast<unsigned>(setup), scenario_step, static_cast<unsigned>(cleanup), retain);
        MV_LOG(error, "Input Host: failed operation {}, request {}, opcode {}, sent {}, done {}",
            static_cast<unsigned>(operation.kind), operation.request.request.value,
            static_cast<unsigned>(operation.request.opcode), operation.sent, operation.done);
    }
    if (!failed && (stage == EStage::freeze) && shared)
    {
        const SFreezeProgress& progress = shared->freeze;
        MV_LOG(error, "Input Host: freeze request {} failed: requested {}, frozen {}, closing {}, source failed {}, pumps {}",
            operation.request.request.value, progress.requested.load(), shared->frozen.load(), shared->closing.load(),
            shared->failed.load(), progress.pumps.load());
        MV_LOG(error, "Input Host: last freeze backlog: feed {}, history [{}, {}, {}, {}], desktop busy {}",
            progress.feed.load(), progress.history[0].load(), progress.history[1].load(), progress.history[2].load(),
            progress.history[3].load(), progress.desktop.load());
        MV_LOG(error, "Input Host: freeze desktop eligible {}, cutover {}, queued {}, baseline {}, loss {}, runnable {}",
            progress.eligible.load(), progress.cutover.load(), progress.desktop_queued.load(), progress.desktop_baseline.load(),
            progress.desktop_loss.load(), progress.desktop_runnable.load());
        MV_LOG(error, "Input Host: last freeze pipe masks: mappers {}, baselines {}, boundaries {}, ingress {}, actions {}, text {}",
            progress.mappers.load(), progress.baselines.load(), progress.boundaries.load(), progress.ingress.load(),
            progress.actions.load(), progress.text.load());
        MV_LOG(error, "Input Host: freeze frontier: consumed {}, drained {}, feed loss {}, history loss {}, pause mask {}",
            shared->consumed.load(), shared->drained.load(), shared->feed_loss.load(), shared->history_loss.load(), pause_mask);
    }
    failed = true;
    unsafe |= retain;
}

void CInputExecution::deadline_expired() noexcept
{
    if (!failed)
    {
        MV_LOG(error, "Input Host: deadline expired; applied per pipe [{}, {}, {}, {}, {}], pause mask {}",
            pipes[0].applied, pipes[1].applied, pipes[2].applied, pipes[3].applied, pipes[4].applied, pause_mask);
    }
    // A failed measurement is not unknown resource custody. Only this named
    // native freeze may continue known cleanup after coherent clock expiry.
    const bool safe_freeze = native && !secondary && (stage == EStage::freeze) && !failed && !unsafe &&
        !source.failed() && source.complete() && !borrows.failed() && shared &&
        (operation.kind == EOperation::command) && (operation.request.route.value == control_route.value) &&
        control_route.value && (operation.request.opcode == 2u) && !operation.request.payload_size;
    fail(!safe_freeze);
    if (!safe_freeze) return;
    freeze_timeout_cleanup = true;
    begin_cleanup();
    close_all(); // Every local reader/view closes before borrow cleanup can publish.
}

void CInputExecution::arm_stage(const std::uint64_t milliseconds) noexcept
{
    if (stage_deadline.set_after_milliseconds(milliseconds).code != EWaitError::none) fail(true);
}

void CInputExecution::next_setup(const ESetup next) noexcept
{
    setup = next;
    stage_started = false;
    runnable = true;
    arm_stage();
}

void CInputExecution::next_stage(const EStage next) noexcept
{
    stage = next;
    stage_started = false;
    runnable = true;
    arm_stage();
}

void CInputExecution::poll() noexcept
{
    next_wake = std::min(next_wake, UINT64_C(1000000));
}

void CInputExecution::clear_operation() noexcept
{
    // Retain the exact accepted value-only freeze identity through ALL cleanup
    // paths. A late ACK/completion still validates normally. After actual stop,
    // Host quiescence may cancel an unread outcome; no local resource depends on it.
    if (freeze_timeout_cleanup && operation.sent)
    {
        return;
    }
    operation = {};
}

bool CInputExecution::begin_module(const modules::EModuleAction action, const module_ids::id_type module) noexcept
{
    if (operation.kind != EOperation::none) return false;
    CErasedOwner owner = CErasedOwner::create<modules::ModuleRequest>();
    modules::ModuleRequest* const request = owner.payload<modules::ModuleRequest>();
    if (!request) return false;
    request->module = module;
    request->action = action;
    const char* const basename = (module == module_ids::rendering) ? "package:/bin/MorphicInputSecondary" :
        (ordinary ? "package:/bin/MorphicPlatform" : "package:/bin/MorphicInputPlatform");
    const std::string file = test_environment::module_filename(basename);
    if (!request->file.set(file.c_str())) return false;
    operation.owner.set_message_type<modules::ModuleRequest>();
    operation.owner.set_async_slot(1);
    operation.owner.set_owner(std::move(owner));
    operation.kind = (module == module_ids::executive) ? EOperation::self_stop : EOperation::module;
    operation.module = module;
    return operation.deadline.set_after_milliseconds(10000u).code == EWaitError::none;
}

bool CInputExecution::begin_open(const SServiceContract contract) noexcept
{
    if (operation.kind != EOperation::none) return false;
    const OpenServiceRequest request{ module_ids::platform, contract.number, contract.version, { ++next_request } };
    operation.request.request = request.request;
    operation.pod = threading::make_pod_message(1, request);
    operation.kind = EOperation::open;
    return operation.deadline.set_after_milliseconds(10000u).code == EWaitError::none;
}

bool CInputExecution::begin_command(const SRoute route, const std::uint16_t opcode, const void* const payload,
    const std::uint16_t size, const EStatus expected) noexcept
{
    if ((operation.kind != EOperation::none) || (size > sizeof(ServiceRequest::payload)) || (size && !payload))
    {
        return false;
    }
    operation.request.route = route;
    operation.request.request.value = ++next_request;
    operation.request.opcode = opcode;
    operation.request.payload_size = size;
    if (size) std::memcpy(operation.request.payload, payload, size);
    operation.pod = threading::make_pod_message(1, operation.request);
    operation.kind = EOperation::command;
    operation.expected = expected;
    return operation.deadline.set_after_milliseconds(10000u).code == EWaitError::none;
}

bool CInputExecution::begin_acquire(const unsigned borrow, const SRoute route, const std::uint16_t opcode,
    const void* const payload, const std::uint16_t size, const EStatus expected) noexcept
{
    if ((size > sizeof(ServiceRequest::payload)) || (size && !payload))
    {
        return false;
    }
    ServiceRequest request;
    request.route = route;
    request.request.value = ++next_request;
    request.opcode = opcode;
    request.payload_size = size;
    if (size) std::memcpy(request.payload, payload, size);
    return borrows.begin(borrow, request, expected);
}

void CInputExecution::publish_operation() noexcept
{
    if (freeze_timeout_cleanup || (operation.kind == EOperation::none) || operation.done)
    {
        return;
    }
    std::uint64_t remaining = 0u;
    if (operation.deadline.remaining_nanoseconds(remaining).code != EWaitError::none)
    {
        fail(true);
        return;
    }
    if (!remaining)
    {
        deadline_expired();
        return;
    }
    next_wake = std::min(next_wake, remaining);
    if (operation.sent) return;
    const modules::ESubmission result = ((operation.kind == EOperation::module) || (operation.kind == EOperation::self_stop)) ?
        communication.submit({}, std::move(operation.owner)) : communication.submit({}, operation.pod);
    if (modules::accepted(result))
    {
        operation.sent = true;
        if (operation.kind == EOperation::self_stop) shutdown_requested = true;
        if (secondary && (stage == EStage::secondary_notify) && (operation.kind == EOperation::command) &&
            (operation.request.opcode == k_secondary_cleanup_complete)) secondary_notified = true;
    }
    else if (result != modules::ESubmission::busy) fail();
}

bool CInputExecution::receive(const threading::CErasedPodMsg& message) noexcept
{
    if (source.owns(message.query_async_slot()))
    {
        source.receive(message);
        capture_installation();
        return true;
    }
    if (borrows.owns(message))
    {
        borrows.receive(message);
        return true;
    }
    const std::int32_t correlation = message.query_async_slot();
    if ((correlation >= 32) && (correlation < (32 + static_cast<std::int32_t>(k_pipe_count))))
    {
        SPipe& pipe = pipes[correlation - 32];
        ServiceReply reply;
        if (!pipe.pending || !message.copy_payload_to(reply) || (reply.route.value != input_route.value) ||
            (reply.request.value != pipe.pending) || (reply.opcode != static_cast<std::uint16_t>(ECommand::boundary_ready)) ||
            (reply.payload_size != 0u) || (reply.status > EStatus::delivery_failed))
        {
            return false;
        }
        if (reply.notice == EServiceNotice::acknowledged)
        {
            if (pipe.boundary_acknowledged || (reply.status != EStatus::success))
            {
                return false;
            }
            pipe.boundary_acknowledged = true;
            return true;
        }
        if ((reply.notice != EServiceNotice::completed) || !pipe.boundary_acknowledged)
        {
            return false;
        }
        pipe.pending = 0u;
        pipe.boundary_acknowledged = false;
        if ((reply.status == EStatus::busy) || (reply.status == EStatus::stale))
        {
            if (pipe.view) pipe.reader->retry_boundary(pipe.boundary);
        }
        else if ((reply.status != EStatus::success) && !cleaning)
        {
            return false;
        }
        return true;
    }
    if (correlation != 1) return false;
    // Stop cancels unread value-only command/module outcomes through Host
    // quiescence. Source and borrow custody were routed above and stay live.
    if (stopping && (operation.kind == EOperation::none))
    {
        return true;
    }
    if (!operation.sent || operation.done)
    {
        return false;
    }
    if ((operation.kind == EOperation::module) || (operation.kind == EOperation::self_stop))
    {
        modules::ModuleResult result;
        if (!message.copy_payload_to(result) || (result.module != operation.module))
        {
            return false;
        }
        if (result.notice == modules::EModuleNotice::acknowledged)
        {
            if (operation.acknowledged || (result.status != modules::EModuleStatus::success))
            {
                return false;
            }
            operation.acknowledged = true;
        }
        else if ((result.notice == modules::EModuleNotice::completed) && operation.acknowledged)
        {
            operation.done = true;
            if (result.status != modules::EModuleStatus::success) fail();
        }
        else return false;
        return true;
    }
    if (operation.kind == EOperation::open)
    {
        if (!message.copy_payload_to(operation.opened) ||
            (operation.opened.request.value != operation.request.request.value)) return false;
        operation.done = true;
        if ((operation.opened.status != EStatus::success) || !operation.opened.route.value || !operation.opened.provider.value) fail();
        return true;
    }
    ServiceReply reply;
    if (!message.copy_payload_to(reply) || (reply.route.value != operation.request.route.value) ||
        (reply.request.value != operation.request.request.value) || (reply.opcode != operation.request.opcode) ||
        (reply.payload_size > sizeof(reply.payload)) || (reply.status > EStatus::delivery_failed)) return false;
    if (reply.notice == EServiceNotice::acknowledged)
    {
        if ((operation.kind != EOperation::command) || operation.acknowledged ||
            (reply.status != EStatus::success) || reply.payload_size) return false;
        operation.acknowledged = true;
        return true;
    }
    if ((reply.notice != EServiceNotice::completed) ||
        ((operation.kind == EOperation::command) && !operation.acknowledged)) return false;
    operation.result = reply;
    operation.done = true;
    if ((reply.status != operation.expected) && (operation.kind != EOperation::close)) fail();
    return true;
}

void CInputExecution::capture_installation() noexcept
{
    if (!source_active || source_captured || !source.installation().value)
    {
        return;
    }
    // Copy the value token before helper reuse, including a late success after
    // stop. Source.complete() does not mean this configuration is retired.
    if (installations[source_installation].value)
    {
        fail(true);
        return;
    }
    installations[source_installation] = source.installation();
    source_captured = true;
}

void CInputExecution::advance_setup() noexcept
{
    const bool idle = operation.kind == EOperation::none;
    switch (setup)
    {
        case ESetup::load:
        {
            if (idle)
            {
                if (!begin_module(modules::EModuleAction::load, module_ids::platform)) fail();
            }
            else if (operation.done)
            {
                loaded = true;
                clear_operation();
                next_setup(ESetup::input_route);
            }
            break;
        }
        case ESetup::input_route:
        case ESetup::window_route:
        case ESetup::control_route:
        {
            if (idle)
            {
                const SServiceContract contract = (setup == ESetup::input_route) ? k_input_contract :
                    ((setup == ESetup::window_route) ? platform::windows::k_window_contract : k_contract);
                if (!begin_open(contract)) fail();
            }
            else if (operation.done)
            {
                if (setup == ESetup::input_route) input_route = operation.opened.route;
                if (setup == ESetup::window_route) window_route = operation.opened.route;
                if (setup == ESetup::control_route) control_route = operation.opened.route;
                clear_operation();
                if (setup == ESetup::control_route) next_setup(secondary ? ESetup::input_route : ESetup::control_acquire);
                else if (setup == ESetup::input_route) next_setup(secondary ? ESetup::controller_install : ESetup::window_route);
                else next_setup(ordinary ? ESetup::desktop_install : ESetup::control_route);
            }
            break;
        }
        case ESetup::control_acquire:
        {
            if (!stage_started)
            {
                if (!begin_acquire(0u, control_route, 1u, nullptr, 0u)) fail();
                stage_started = true;
            }
            else if (borrows.record(0u).prepared)
            {
                const SServiceView descriptor = borrows.view(0u);
                if (!descriptor.descriptor || (descriptor.size != sizeof(SView)) || (descriptor.version != 1u))
                {
                    fail();
                    break;
                }
                const SView& view = *static_cast<const SView*>(descriptor.descriptor);
                if ((view.consumer.value != instance.value) || !view.shared)
                {
                    fail();
                    break;
                }
                shared = view.shared;
                control_borrow = borrows.record(0u).token;
                if (shared->tracing && !reads.resize(k_trace_capacity)) fail();
                next_setup(ESetup::desktop_install);
            }
            break;
        }
        case ESetup::desktop_install:
        case ESetup::controller_install:
        {
            if (!stage_started)
            {
                source_installation = (setup == ESetup::desktop_install) ? 0u : 1u;
                const std::string schema = source_installation ?
                    (option("MORPHIC_INPUT_CASE", "work-pressure") ? expanded_schema() : tests::controller_schema()) :
                    ((native || ordinary) ? desktop_schema() : tests::input_schema());
                source_captured = false;
                source_active = source.start(schema.data(), schema.size(), input_route, { ++next_request });
                if (!source_active) fail();
                stage_started = true;
                runnable = true;
            }
            else if (source.complete())
            {
                capture_installation();
                if (!installations[source_installation].value) fail();
                source_active = false;
                if (setup == ESetup::desktop_install) next_setup(ESetup::controller_install);
                else
                {
                    index = secondary ? 1u : 0u;
                    next_setup(secondary ? ESetup::pipe_acquire : ESetup::window_acquire);
                }
            }
            break;
        }
        case ESetup::window_acquire:
        case ESetup::catalogue_acquire:
        case ESetup::pipe_acquire:
        {
            const unsigned borrow = (setup == ESetup::window_acquire) ? 1u : ((setup == ESetup::catalogue_acquire) ? 2u : (3u + index));
            if (!stage_started)
            {
                bool started = false;
                if (setup == ESetup::window_acquire)
                {
                    started = begin_acquire(borrow, window_route, static_cast<std::uint16_t>(platform::windows::EWindowCommand::create_window), nullptr, 0u);
                }
                else if (setup == ESetup::catalogue_acquire)
                {
                    started = begin_acquire(borrow, input_route, static_cast<std::uint16_t>(ECommand::acquire_catalogue), nullptr, 0u);
                }
                else
                {
                    const SAcquireDesktop desktop{ window, installations[0], 3u, 4u };
                    const SAcquireController controller{ installations[1], 0u, 8u, 4u };
                    started = begin_acquire(borrow, input_route, static_cast<std::uint16_t>(index ? ECommand::acquire_controller : ECommand::acquire_desktop),
                        index ? static_cast<const void*>(&controller) : static_cast<const void*>(&desktop), sizeof(desktop));
                }
                if (!started) fail();
                stage_started = true;
            }
            else if (borrows.record(borrow).prepared)
            {
                const SServiceView descriptor = borrows.view(borrow);
                if (setup == ESetup::window_acquire)
                {
                    if (!descriptor.descriptor || (descriptor.size != sizeof(platform::windows::SWindowViewV1)) || (descriptor.version != 1u))
                    {
                        fail();
                        break;
                    }
                    const auto& view = *static_cast<const platform::windows::SWindowViewV1*>(descriptor.descriptor);
                    window = view.window;
                    native_window = view.native_window;
                    next_setup(ESetup::window_notification);
                }
                else if (setup == ESetup::catalogue_acquire) next_setup(ESetup::catalogue_notification);
                else
                {
                    if (!descriptor.descriptor || (descriptor.size != sizeof(SInputEndpointV4)) || (descriptor.version != 4u))
                    {
                        fail();
                        break;
                    }
                    SPipe& pipe = pipes[index];
                    pipe.view = static_cast<const SInputEndpointV4*>(descriptor.descriptor);
                    if (!pipe.reader.emplace() || !pipe.reader->install(*pipe.view, instance)) fail();
                    next_setup(ESetup::pipe_notification);
                }
            }
            break;
        }
        case ESetup::window_notification:
        case ESetup::catalogue_notification:
        case ESetup::pipe_notification:
        {
            const unsigned borrow = (setup == ESetup::window_notification) ? 1u : ((setup == ESetup::catalogue_notification) ? 2u : (3u + index));
            if (!stage_started)
            {
                if (!borrows.register_notification(borrow)) fail();
                stage_started = true;
            }
            else if (borrows.registration(borrow).value)
            {
                next_setup((setup == ESetup::window_notification) ? ESetup::window_ready :
                    ((setup == ESetup::catalogue_notification) ? ESetup::catalogue_ready : ESetup::pipe_ready));
            }
            break;
        }
        case ESetup::window_ready:
        case ESetup::catalogue_ready:
        case ESetup::pipe_ready:
        {
            if (idle)
            {
                if (setup == ESetup::window_ready)
                {
                    const platform::windows::SWindowReady ready{ window, borrows.record(1u).token, borrows.registration(1u) };
                    if (!begin_command(window_route, static_cast<std::uint16_t>(platform::windows::EWindowCommand::window_ready), &ready, sizeof(ready))) fail();
                }
                else if (setup == ESetup::catalogue_ready)
                {
                    if (!command(ECommand::catalogue_ready, SCatalogueReady{ borrows.record(2u).token,
                        borrows.registration(2u), borrows.record(2u).request.request })) fail();
                }
                else if (!command(ECommand::input_ready, SInputReady{ pipes[index].view->connection,
                    borrows.registration(3u + index), borrows.record(3u + index).request.request })) fail();
            }
            else if (operation.done)
            {
                clear_operation();
                if (setup == ESetup::window_ready) next_setup(ESetup::catalogue_acquire);
                else if (setup == ESetup::catalogue_ready)
                {
                    if (shared && !native)
                    {
                        for (unsigned device = 0u; device < 4u; ++device)
                        {
                            SFeed neutral;
                            neutral.instance = shared->device_instance[device].load();
                            shared->latest[device].publish(neutral);
                        }
                    }
                    index = 0u;
                    next_setup(ESetup::pipe_acquire);
                }
                else
                {
                    ++index;
                    next_setup((reacquiring || secondary || (index == k_pipe_count)) ? ESetup::active : ESetup::pipe_acquire);
                }
            }
            break;
        }
        case ESetup::active:
        {
            if (active())
            {
                if (reacquiring)
                {
                    reacquiring = false;
                    next_stage(EStage::scenario);
                }
                else
                {
                    index = exercise_step = 0u;
                    next_stage(EStage::exercise);
                }
            }
            else poll();
            break;
        }
    }
}

bool CInputExecution::publication_pending() const noexcept
{
    if ((source_active && !source.complete()) || borrows.publication_pending() ||
        ((operation.kind != EOperation::none) && !operation.sent)) return true;
    for (const SPipe& pipe : pipes)
    {
        if (pipe.boundary_prepared) return true;
    }
    return false;
}

void CInputExecution::service() noexcept
{
    ++service_passes;
    for (unsigned borrow = 0u; borrow < CBorrowExecution::k_count; ++borrow)
    {
        const threading::SNotificationReader reader = borrows.notification(borrow);
        if (!reader.is_valid()) continue;
        if (reader.failed()) fail();
        reader.clear_pending();
    }
    for (unsigned pipe_index = 0u; pipe_index < k_pipe_count; ++pipe_index)
    {
        SPipe& pipe = pipes[pipe_index];
        if (!pipe.view || (pause_mask & (1u << pipe_index)))
        {
            continue;
        }
        for (unsigned count = 0u; count < 32u; ++count)
        {
            SInputEvent event = pipe.retained;
            const EInputRead result = pipe.has_retained ? EInputRead::record : pipe.reader->next(event);
            const std::uint32_t cancel = pipe.reader->take_cancellation();
            if (cancel)
            {
                ++pipe.cancellations;
                if (cancel & k_actions) std::memset(pipe.values, 0, sizeof(pipe.values));
            }
            pipe.drained = result == EInputRead::empty;
            if (pipe.drained) break;
            if (result == EInputRead::retry)
            {
                poll();
                break;
            }
            if ((result == EInputRead::failed) || (result == EInputRead::closing))
            {
                if (cleaning && (result == EInputRead::closing)) close_reader(pipe_index);
                else fail();
                break;
            }
            if (result != EInputRead::record)
            {
                if (count == 31u) runnable = true;
                continue;
            }
            SReadTrace trace = pipe.retained_trace;
            if (!pipe.has_retained)
            {
                trace = { pipe.view->provider.value, event.connection.value, event.generation,
                    (event.stream == k_actions) ? event.action.sequence : event.text.sequence, 0u, 0u, event.stream, 0u };
                if (shared && shared->tracing && !sample(trace.read)) fail();
            }
            const EInputValidation validation = pipe.reader->validate(event);
            const std::uint32_t later_cancel = pipe.reader->take_cancellation();
            if (later_cancel)
            {
                ++pipe.cancellations;
                if (later_cancel & k_actions) std::memset(pipe.values, 0, sizeof(pipe.values));
            }
            if (validation == EInputValidation::retry)
            {
                pipe.retained = event;
                pipe.retained_trace = trace;
                pipe.has_retained = true;
                poll();
                break;
            }
            pipe.has_retained = false;
            if (validation == EInputValidation::current)
            {
                if (trace.sequence <= pipe.last_sequence) fail();
                pipe.last_sequence = trace.sequence;
                if ((event.stream == k_actions) && (event.action.subject < 256u))
                {
                    float* const value = pipe.values[event.action.subject];
                    if (event.action.kind == EActionKind::digital)
                    {
                        SDigitalAction digital;
                        std::memcpy(&digital, event.action.payload, sizeof(digital));
                        value[0] = digital.active ? 1.0f : 0.0f;
                    }
                    else std::memcpy(value, event.action.payload, (sizeof(float) << 1u));
                }
                ++pipe.applied;
                if (shared && shared->tracing && !sample(trace.applied)) fail();
            }
            else
            {
                ++pipe.stale;
                trace.outcome = 1u;
            }
            if (shared && shared->tracing)
            {
                if (read_count < reads.size()) reads[read_count++] = trace;
                else ++trace_overflow;
            }
            if (count == 31u) runnable = true;
        }
        if (pipe.view && !pipe.pending && !pipe.boundary_prepared && !publication_pending() &&
            pipe.reader->boundary_ready(pipe.boundary))
        {
            pipe.boundary_request = {};
            pipe.boundary_request.route = input_route;
            pipe.boundary_request.request.value = ++next_request;
            pipe.boundary_request.opcode = static_cast<std::uint16_t>(ECommand::boundary_ready);
            pipe.boundary_request.payload_size = sizeof(pipe.boundary);
            std::memcpy(pipe.boundary_request.payload, &pipe.boundary, sizeof(pipe.boundary));
            pipe.boundary_prepared = true;
        }
        if (pipe.view && pipe.boundary_prepared)
        {
            const modules::ESubmission result = communication.submit({},
                threading::make_pod_message((32 + static_cast<std::int32_t>(pipe_index)), pipe.boundary_request));
            if (modules::accepted(result))
            {
                if (!pipe.reader->submitted(pipe.boundary)) fail();
                pipe.pending = pipe.boundary_request.request.value;
                pipe.boundary_prepared = false;
            }
            else if (result != modules::ESubmission::busy) fail();
        }
    }
}

void CInputExecution::close_reader(const unsigned pipe_index) noexcept
{
    SPipe& pipe = pipes[pipe_index];
    if (pipe.reader) pipe.reader->close();
    pipe.view = nullptr;
    pipe.has_retained = pipe.boundary_prepared = false;
    borrows.close_local(3u + pipe_index);
}

void CInputExecution::close_all() noexcept
{
    for (unsigned pipe_index = 0u; pipe_index < k_pipe_count; ++pipe_index) close_reader(pipe_index);
    shared = nullptr;
    for (unsigned borrow = 0u; borrow < CBorrowExecution::k_count; ++borrow) borrows.close_local(borrow);
}

bool CInputExecution::active() const noexcept
{
    for (unsigned pipe_index = 0u; pipe_index < k_pipe_count; ++pipe_index)
    {
        const SPipe& pipe = pipes[pipe_index];
        if (!pipe.view) continue;
        SInputSnapshot state;
        if (!borrows.record(3u + pipe_index).terminal || (pipe.view->state->read(state) != EStateRead::success) ||
            (state.phase != EPhase::active) || state.pending || state.ingress_pending || pipe.pending || pipe.boundary_prepared)
        {
            return false;
        }
    }
    return true;
}

bool CInputExecution::feed(SFeed frame) noexcept
{
    if (!shared || shared->closing.load())
    {
        return false;
    }
    frame.session = control_borrow.value;
    frame.observation = ++next_observation;
    if (!sample(frame.offered)) return false;
    if (measuring)
    {
        ++measuring->offered;
        if (intended && (frame.offered >= intended)) measuring->late_max = std::max(measuring->late_max, (frame.offered - intended));
    }
    if (frame.kind == EFeed::controller)
    {
        frame.instance = shared->device_instance[frame.device].load();
        shared->latest[frame.device].publish(frame);
    }
    if (shared->producer.post(frame))
    {
        if (measuring)
        {
            ++measuring->admitted;
            if (frame.kind == EFeed::controller) measuring->expected[frame.device + 1u] += 2u;
            else if (frame.kind == EFeed::key) ++measuring->expected[0];
        }
        return true;
    }
    shared->feed_loss.fetch_add(1u);
    if (frame.kind == EFeed::controller) shared->latest[frame.device].loss.fetch_add(1u);
    return false;
}

void CInputExecution::advance_exercise() noexcept
{
    if (secondary)
    {
        if ((pipes[1].applied >= 2u) && (pipes[1].values[100u][0] == 0.0f))
        {
            MV_LOG(info, "Input Host: second consumer independently applied press and release");
            begin_cleanup();
        }
        else poll();
        return;
    }
    if (native || ordinary)
    {
        native_pass();
        return;
    }
    if (exercise_step < 20u)
    {
        const unsigned target = exercise_step >> 2u;
        const bool press = (exercise_step & 2u) == 0u;
        const unsigned pipe_index = (target == 4u) ? 0u : (target + 1u);
        const unsigned action = (target == 4u) ? 7u : 100u;
        if (!(exercise_step & 1u))
        {
            SFeed frame;
            frame.kind = (target == 4u) ? EFeed::key : EFeed::controller;
            frame.device = (target == 4u) ? 1u : target;
            frame.value = press ? 1u : 0u;
            if (!feed(frame)) fail();
            ++exercise_step;
            arm_stage();
        }
        else if (pipes[pipe_index].values[action][0] == (press ? 1.0f : 0.0f))
        {
            ++exercise_step;
            runnable = true;
        }
        else poll();
        return;
    }
    if (!settling)
    {
        MV_LOG(info, "Input Host: five independent pipes acquired, acknowledged and applied");
        if (settle_deadline.set_after_milliseconds(100u).code != EWaitError::none) fail();
        settling = true;
    }
    std::uint64_t remaining = 0u;
    if (settle_deadline.remaining_nanoseconds(remaining).code != EWaitError::none) fail();
    else if (remaining) next_wake = std::min(next_wake, remaining);
    else
    {
        next_stage(option("MORPHIC_INPUT_CASE", "lifecycle") ? EStage::freeze : EStage::scenario);
    }
}

void CInputExecution::advance_report() noexcept
{
    if (!shared)
    {
        fail();
        return;
    }
    if (stage == EStage::freeze)
    {
        if (operation.kind == EOperation::none)
        {
            if (!begin_command(control_route, 2u, nullptr, 0u)) fail();
        }
        else if (operation.done)
        {
            if (!shared->frozen.load(std::memory_order_acquire))
            {
                poll();
                return;
            }
            // Preserve the pressure case's post-freeze service interval: any
            // untraced history escaping the handshake must still be observed.
            if (option("MORPHIC_INPUT_CASE", "pressure"))
            {
                if (!stage_started)
                {
                    stage_started = true;
                    if (settle_deadline.set_after_milliseconds(30u).code != EWaitError::none) fail();
                }
                std::uint64_t remaining = 0u;
                if (settle_deadline.remaining_nanoseconds(remaining).code != EWaitError::none) fail();
                else if (remaining)
                {
                    next_wake = std::min(next_wake, remaining);
                    return;
                }
            }
            clear_operation();
            report_frozen = true;
            report_index = 0u;
            next_stage(EStage::report);
        }
        return;
    }
    // Frozen consumer counters never include retry invocations. Each snapshot
    // gets one coherent attempt before a timed wake, preserving the deadline.
    if (report_index < k_pipe_count)
    {
        if (pipes[report_index].view)
        {
            const EStateRead result = pipes[report_index].view->state->read(report_states[report_index]);
            if (result == EStateRead::retry)
            {
                poll();
                return;
            }
            if (result != EStateRead::success)
            {
                fail();
                return;
            }
        }
        ++report_index;
        runnable = true;
    }
    else
    {
        if (!write_report()) fail();
        begin_cleanup();
    }
}

void CInputExecution::begin_cleanup() noexcept
{
    if (cleaning) return;
    cleaning = true;
    source.stop();
    measuring = nullptr;
    capture_installation();
    clear_operation();
    cleanup = ECleanup::pipes;
    index = 0u;
    next_stage(EStage::cleanup);
}

void CInputExecution::advance_cleanup() noexcept
{
    switch (cleanup)
    {
        case ECleanup::pipes:
        {
            for (unsigned pipe_index = 0u; pipe_index < k_pipe_count; ++pipe_index) close_reader(pipe_index);
            for (unsigned borrow = 3u; borrow < CBorrowExecution::k_count; ++borrow)
            {
                if (borrows.record(borrow).live) return;
            }
            cleanup = ECleanup::source;
            arm_stage();
            runnable = true;
            break;
        }
        case ECleanup::source:
        {
            capture_installation();
            if (!source.complete()) return;
            source_active = false;
            index = 0u;
            cleanup = (secondary && !stopping && !failed) ? ECleanup::secondary_barrier : ECleanup::configurations;
            arm_stage();
            runnable = true;
            break;
        }
        case ECleanup::secondary_barrier:
        {
            if (stopping || failed)
            {
                if (!secondary_barrier_released)
                {
                    MV_LOG(error, "Input Host: secondary cleanup barrier interrupted before release");
                    fail();
                }
                clear_operation();
                cleanup = ECleanup::configurations;
                runnable = true;
            }
            else if (operation.kind == EOperation::none)
            {
                if (!begin_command(control_route, k_secondary_cleanup_barrier, nullptr, 0u)) fail();
            }
            else if (operation.done)
            {
                secondary_barrier_released = true;
                clear_operation();
                cleanup = ECleanup::configurations;
                arm_stage();
                runnable = true;
            }
            break;
        }
        case ECleanup::configurations:
        {
            if (shutdown_requested && !stopping && !framework_shutdown)
            {
                return; // Await the stop correlated with the accepted self-unload.
            }
            if (framework_shutdown)
            {
                // These values remain recorded. No explicit retirement ACK is
                // claimed: global Host shutdown owns Platform's stopped gate.
                clear_operation();
                cleanup = ECleanup::borrows;
                arm_stage();
                runnable = true;
                break;
            }
            if (index == 2u)
            {
                cleanup = ECleanup::borrows;
                arm_stage();
                runnable = true;
            }
            else if (!installations[index].value)
            {
                ++index;
                runnable = true;
            }
            else if (operation.kind == EOperation::none)
            {
                if (!command(ECommand::retire_configuration, SRetireConfiguration{ installations[index] })) fail();
            }
            else if (operation.done)
            {
                if (operation.result.status != EStatus::success)
                {
                    fail();
                    break;
                }
                installations[index] = {};
                clear_operation();
                ++index;
                arm_stage();
                runnable = true;
            }
            break;
        }
        case ECleanup::borrows:
        {
            close_all();
            if (!borrows.complete()) return;
            index = 0u;
            cleanup = ECleanup::routes;
            settling = false;
            arm_stage();
            runnable = true;
            break;
        }
        case ECleanup::routes:
        {
            if (framework_shutdown || (index == 3u))
            {
                cleanup = ECleanup::unload;
                arm_stage();
                runnable = true;
                break;
            }
            SRoute& route = (index == 0u) ? input_route : ((index == 1u) ? window_route : control_route);
            if (!route.value)
            {
                ++index;
                runnable = true;
                break;
            }
            if (settling)
            {
                std::uint64_t remaining = 0u;
                if (settle_deadline.remaining_nanoseconds(remaining).code != EWaitError::none) fail();
                else if (remaining)
                {
                    next_wake = std::min(next_wake, remaining);
                    return;
                }
                settling = false;
            }
            if (operation.kind == EOperation::none)
            {
                operation.kind = EOperation::close;
                operation.request.route = route;
                operation.request.request.value = ++next_request;
                operation.pod = threading::make_pod_message(1, CloseServiceRequest{ route, operation.request.request });
                operation.deadline = stage_deadline;
            }
            else if (operation.done)
            {
                if (operation.result.status == EStatus::busy)
                {
                    clear_operation();
                    settling = true;
                    if (settle_deadline.set_after_milliseconds(1u).code != EWaitError::none) fail();
                    poll();
                }
                else if (operation.result.status == EStatus::success)
                {
                    clear_operation();
                    route = {};
                    ++index;
                    arm_stage();
                    runnable = true;
                }
                else fail();
            }
            break;
        }
        case ECleanup::unload:
        {
            if (framework_shutdown || secondary || !loaded)
            {
                cleanup = ECleanup::finished;
                runnable = true;
            }
            else if (operation.kind == EOperation::none)
            {
                if (!begin_module(modules::EModuleAction::unload, module_ids::platform)) fail();
            }
            else if (operation.done)
            {
                clear_operation();
                loaded = false;
                cleanup = ECleanup::finished;
                runnable = true;
            }
            break;
        }
        case ECleanup::finished:
        {
            if (secondary && !stopping)
            {
                next_stage(failed ? EStage::secondary_wait : EStage::secondary_notify);
                break;
            }
            if (!failed && !passed)
            {
                passed = true;
                MV_LOG(info, "Input Host: acceptance passed");
            }
            if (framework_shutdown && (installations[0].value || installations[1].value))
            {
                MV_LOG(info, "Input Host: consumer access ended; configuration retirement remains with Platform shutdown");
            }
            if (freeze_timeout_cleanup && !stopping)
            {
                return;
            }
            next_stage(stopping ? EStage::done : EStage::self_stop);
            break;
        }
    }
}

void CInputExecution::advance_secondary() noexcept
{
    if (stage == EStage::secondary_wait) return;
    if (!control_route.value)
    {
        if (operation.kind == EOperation::none)
        {
            if (!begin_open(k_contract)) fail();
        }
        else if (operation.done)
        {
            control_route = operation.opened.route;
            clear_operation();
            runnable = true;
        }
    }
    else if (operation.kind == EOperation::none)
    {
        if (!begin_command(control_route, k_secondary_cleanup_complete, nullptr, 0u)) fail();
    }
    else if (secondary_notified)
    {
        // Only value identities remain. Keep the accepted notification state
        // while the primary requests unload; stop may cancel its unread reply.
        next_stage(EStage::secondary_wait);
    }
}

modules::EExecutionStatus CInputExecution::handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
{
    runnable = false;
    next_wake = UINT64_MAX;
    switch (invocation.event)
    {
        case modules::EExecutionEvent::start:
        case modules::EExecutionEvent::service_async:
        case modules::EExecutionEvent::provider_admission_closed:
        {
            break;
        }
        case modules::EExecutionEvent::message:
        {
            if (!invocation.source.is_host() || !invocation.message || !receive(*invocation.message)) fail(true);
            break;
        }
        case modules::EExecutionEvent::stop:
        {
            stopping = true;
            if (blocked_shutdown && shutdown_requested)
            {
                MV_LOG(info, "Input Host: shutdown requested with full output and blocked boundary");
                blocked_shutdown = false;
            }
            framework_shutdown |= shutdown_requested || (invocation.notification.stop_reason == modules::EExecutionStop::shutdown);
            // An unexpected interruption of this acceptance fixture is fatal;
            // the existing framework escalation supplies global shutdown.
            if (!cleaning && !framework_shutdown) fail();
            begin_cleanup();
            close_all();
            if ((stage == EStage::secondary_notify) || (stage == EStage::secondary_wait))
            {
                if (!secondary_notified) fail();
                clear_operation();
                if (!failed)
                {
                    passed = true;
                    MV_LOG(info, "Input Host: acceptance passed");
                }
                next_stage(EStage::done);
            }
            else if (stage == EStage::self_stop)
            {
                clear_operation();
                next_stage(EStage::done);
            }
            break;
        }
        default:
        {
            fail();
            break;
        }
    }
    for (unsigned borrow = 0u; borrow < CBorrowExecution::k_count; ++borrow)
    {
        if (!borrows.record(borrow).live || !borrows.record(borrow).closing)
        {
            continue;
        }
        const CBorrowExecution::SRecord& record = borrows.record(borrow);
        const bool expected_refusal = record.terminal && (record.expected != EStatus::success) && (record.status == record.expected);
        if (!record.local_closed && !cleaning && !expected_refusal)
        {
            // Closing can overtake the module stop callback. Domain steps must
            // not access a descriptor after this invocation relinquishes it.
            if (!shutdown_requested) fail();
            begin_cleanup();
        }
        if (borrow >= 3u) close_reader(borrow - 3u);
        else
        {
            if (borrow == 0u) shared = nullptr;
            borrows.close_local(borrow);
        }
    }
    if (source.failed() || borrows.failed()) fail();
    if (failed)
    {
        begin_cleanup();
        if (reported) framework_shutdown = true; // Fatal callback already committed framework escalation.
    }
    if (!unsafe && ((stage == EStage::cleanup) || !publication_pending()))
    {
        switch (stage)
        {
            case EStage::setup:
            {
                advance_setup();
                break;
            }
            case EStage::exercise:
            {
                advance_exercise();
                break;
            }
            case EStage::scenario:
            {
                advance_scenario();
                break;
            }
            case EStage::freeze:
            case EStage::report:
            {
                advance_report();
                break;
            }
            case EStage::cleanup:
            {
                advance_cleanup();
                break;
            }
            case EStage::self_stop:
            {
                if ((operation.kind == EOperation::none) && !begin_module(modules::EModuleAction::unload, module_ids::executive)) fail();
                break;
            }
            case EStage::secondary_notify:
            case EStage::secondary_wait:
            {
                advance_secondary();
                break;
            }
            case EStage::done:
            {
                break;
            }
        }
    }
    if (!unsafe)
    {
        bool source_runnable = false, borrow_runnable = false;
        source.advance(source_runnable, next_wake);
        borrows.advance(borrow_runnable, next_wake);
        publish_operation();
        runnable |= source_runnable || borrow_runnable;
        if (!report_frozen && !cleaning) service();
        if (source.failed() || borrows.failed()) fail();
        if ((stage != EStage::done) && (stage != EStage::self_stop))
        {
            std::uint64_t remaining = 0u;
            if (stage_deadline.remaining_nanoseconds(remaining).code != EWaitError::none) fail(true);
            else if (!remaining) deadline_expired();
            else next_wake = std::min(next_wake, remaining);
        }
    }
    progress = {};
    progress.ready = 1u;
    progress.work_pending = !unsafe && runnable;
    progress.work_complete = complete();
    progress.next_wake_ns = (unsafe || complete()) ? UINT64_MAX : next_wake;
    // One count per returned request to park, regardless of whether Host's
    // final recheck actually permits a native wait. Frozen reporting excludes it.
    if (!report_frozen && !progress.work_pending && !progress.work_complete && (!failed || reported)) ++wait_requests;
    if (failed && !reported)
    {
        reported = true;
        MV_LOG(error, "Input Host: acceptance failed; retaining accepted cleanup obligations");
        return modules::EExecutionStatus::failed;
    }
    return modules::EExecutionStatus::success;
}

}   //  namespace input_host
