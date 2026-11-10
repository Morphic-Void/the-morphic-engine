
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    consumer.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Independently scheduled input consumer using the real Host service protocol.

#include "tests/input_host/consumer.hpp"

namespace input_host
{
bool CConsumer::handle(const ::threading::CErasedPodMsg& message)
{
    ServiceReply reply;
    ServiceBorrowControl control;
    ServiceNotificationRegistered registered;
    modules::ModuleResult module_result;
    if (shutdown_requested && message.copy_payload_to(module_result))
    {
        return true;
    }
    if (message.copy_payload_to(reply))
    {
        for (std::uint32_t index = 0u; index < borrow_count; ++index)
        {
            SBorrow& borrow = borrows[index];
            if (borrow.live && (reply.route.value == borrow.route.value) && (reply.request.value == borrow.request.value))
            {
                if (!client.observe(reply)) return false;
                borrow.token = client.borrow(borrow.route, borrow.request);
                if (reply.notice == EServiceNotice::acknowledged) borrow.acknowledged = true;
                if (reply.notice == EServiceNotice::completed)
                {
                    borrow.terminal = true;
                    borrow.status = reply.status;
                    if ((reply.status != borrow.expected) && !cleaning && !borrow.closing)
                    {
                        return false;
                    }
                }
                return true;
            }
        }
        if (reply.notice != EServiceNotice::completed)
        {
            return true;
        }
        if (reply.request.value == pending_schema) pending_schema = 0u;
        for (std::uint64_t& retired : retired_requests)
        {
            if (retired != reply.request.value) continue;
            retired = 0u;
            return true;
        }
        for (SPipe& pipe : pipes)
        {
            if (pipe.pending && (reply.request.value == pipe.pending))
            {
                pipe.pending = 0u;
                if ((reply.status == EServiceStatus::busy) || (reply.status == EServiceStatus::stale))
                {
                    if (pipe.reader && pipe.view) pipe.reader->retry_boundary(pipe.boundary);
                }
                else if ((reply.status != EServiceStatus::success) && !cleaning)
                {
                    return false;
                }
                return true;
            }
        }
        if (reply.request.value != waiting) return cleaning;
        response = reply;
        response_ready = true;
        return true;
    }
    if (message.copy_payload_to(control))
    {
        if (!client.observe(control)) return false;
        if ((control.control == EServiceBorrowControl::closing) || (control.control == EServiceBorrowControl::retired))
        {
            for (SBorrow& borrow : borrows)
            {
                if (!borrow.live || (borrow.route.value != control.route.value) || (borrow.request.value != control.request.value))
                {
                    continue;
                }
                borrow.token = control.borrow;
                borrow.closing = true;
                for (SPipe& pipe : pipes)
                {
                    if (pipe.borrow != &borrow) continue;
                    if (pipe.reader) pipe.reader->close();
                    pipe.view = nullptr;
                }
                if (borrow.token.value == control_borrow.value) shared = nullptr;
            }
        }
        return true;
    }
    if (message.copy_payload_to(registered))
    {
        return client.observe(registered);
    }
    AssetResult transferred;
    AssetAcquireResult acquired;
    AssetReleaseResult released;
    AssetDisposeResult disposed;
    EAssetPending asset_reply = EAssetPending::none;
    if (message.copy_payload_to(transferred))
    {
        asset_reply = EAssetPending::transfer;
        if (asset_reply != asset_pending) return false;
        if (transferred.status == EAssetStatus::success) source_asset = transferred.asset;
    }
    else if (message.copy_payload_to(acquired))
    {
        asset_reply = EAssetPending::acquire;
        if (asset_reply != asset_pending) return false;
        if (acquired.status == EAssetStatus::success) source_acquisition = acquired.acquisition;
    }
    else if (message.copy_payload_to(released))
    {
        asset_reply = EAssetPending::release;
        if (asset_reply != asset_pending) return false;
        if (released.status == EAssetStatus::success) source_acquisition = {};
    }
    else if (message.copy_payload_to(disposed))
    {
        asset_reply = EAssetPending::dispose;
        if (asset_reply != asset_pending) return false;
        if (disposed.status == EAssetStatus::success) source_asset = {};
    }
    if (asset_reply != EAssetPending::none)
    {
        asset_pending = EAssetPending::none;
        if (!host_waiting) return true;
    }
    if (cleaning && !host_waiting)
    {
        return true;
    }
    if (host_ready) return false;
    host_response = message;
    host_ready = true;
    return true;
}

bool CConsumer::service()
{
    bool work = false;
    ::threading::CErasedPodMsg message;
    for (std::uint32_t count = 0u; (count < 64u) && !host_ready && thread.read(message); ++count)
    {
        if (!handle(message)) failed = true;
        work = true;
    }
    for (SBorrow& borrow : borrows)
    {
        if (!borrow.live || !borrow.closing || borrow.quiescent)
        {
            continue;
        }
        ServiceBorrowQuiescent quiescence;
        if (client.quiesce(borrow.token, quiescence))
        {
            borrow.quiescent = post(quiescence);
            work = true;
        }
    }
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {
        SPipe& pipe = pipes[index];
        if (!pipe.view || (pause_mask & (1u << index)))
        {
            continue;
        }
        for (std::uint32_t count = 0u; count < 32u; ++count)
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
            if ((result == EInputRead::failed) || (result == EInputRead::closing))
            {
                if ((result == EInputRead::closing) && shutdown_requested)
                {
                    pipe.reader->close();
                    pipe.view = nullptr;
                    break;
                }
                std::fprintf(stderr, "Input Host: reader %u failed, result %u\n", index, static_cast<unsigned>(result));
                std::fflush(stderr);
                failed = true;
                break;
            }
            work = true;
            if (result != EInputRead::record) continue;
            SReadTrace trace = pipe.retained_trace;
            if (!pipe.has_retained)
            {
                trace = { pipe.view->provider.value, event.connection.value, event.generation,
                    (event.stream == k_actions) ? event.action.sequence : event.text.sequence, 0u, 0u, event.stream, 0u };
                if (shared && shared->tracing && !sample(trace.read)) failed = true;
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
                break;
            }
            pipe.has_retained = false;
            if (validation == EInputValidation::current)
            {
                if (trace.sequence <= pipe.last_sequence) failed = true;
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
                if (shared && shared->tracing && !sample(trace.applied)) failed = true;
            }
            else
            {
                ++pipe.stale;
                trace.outcome = (validation == EInputValidation::retry) ? 2u : 1u;
            }
            if (shared && shared->tracing)
            {
                if (read_count < reads.size()) reads[read_count++] = trace;
                else ++trace_overflow;
            }
        }
        if (pipe.view && !pipe.pending && pipe.reader->boundary_ready(pipe.boundary))
        {
            ServiceRequest request;
            request.route = input_route;
            request.request.value = ++next_request;
            request.opcode = static_cast<std::uint16_t>(ECommand::boundary_ready);
            request.payload_size = sizeof(pipe.boundary);
            std::memcpy(request.payload, &pipe.boundary, sizeof(pipe.boundary));
            if (!post(request) || !pipe.reader->submitted(pipe.boundary)) failed = true;
            pipe.pending = request.request.value;
            work = true;
        }
    }
    ++rounds;
    return work;
}

bool CConsumer::cycle(const std::uint64_t wait_ns)
{
    const std::uint32_t epoch = thread.observe_epoch();
    bool work = service();
    for (SBorrow& borrow : borrows)
    {
        if (!borrow.live || borrow.closing)
        {
            continue;
        }
        const ::threading::SNotificationReader notification = client.notification(borrow.token);
        if (!notification.is_valid()) continue;
        if (notification.failed()) failed = true;
        notification.clear_pending();
    }
    work = service() || work;
    if (!failed && !work && wait_ns)
    {
        CWaitDeadline deadline;
        if (deadline.set_after_nanoseconds(wait_ns).code != EWaitError::none)
        {
            return false;
        }
        ++waits;
        const ::threading::WaitResult result = thread.wait_for_new_epoch(epoch, deadline);
        if ((result.status != platform::threading::EWaitStatus::satisfied) &&
            (result.status != platform::threading::EWaitStatus::timed_out))
        {
            return false;
        }
    }
    return !failed;
}

bool CConsumer::module(const modules::EModuleAction action, const module_ids::id_type module, const bool threaded)
{
    host_waiting = true;
    CErasedOwner owner = CErasedOwner::create<modules::ModuleRequest>();
    modules::ModuleRequest* const request = owner.payload<modules::ModuleRequest>();
    if (!request) return false;
    request->module = module;
    request->action = action;
    request->require_host_pump = !threaded;
    request->require_service_thread = threaded;
    const char* const basename = (module == module_ids::rendering) ? "package:/bin/MorphicInputSecondary" :
        (ordinary ? "package:/bin/MorphicPlatform" : "package:/bin/MorphicInputPlatform");
    const std::string file = test_environment::module_filename(basename);
    if (!request->file.set(file.c_str())) return false;
    ::threading::CErasedOwnerMsg message;
    message.set_message_type<modules::ModuleRequest>();
    message.set_async_slot(1);
    message.set_owner(std::move(owner));
    if (!thread.post(std::move(message))) return false;
    for (std::uint32_t count = 0u; count < 2u; ++count)
    {
        if (!until([&] { return host_ready; }))
        {
            return false;
        }
        modules::ModuleResult result;
        const bool success = host_response.copy_payload_to(result) && (result.status == modules::EModuleStatus::success);
        host_ready = false;
        if (!success)
        {
            host_waiting = false;
            MV_LOG(error, "Input Host: module notice {} status {}", static_cast<unsigned>(result.notice), static_cast<unsigned>(result.status));
            return false;
        }
    }
    host_waiting = false;
    return true;
}
bool CConsumer::open(const SServiceContract contract, SServiceRoute& route)
{
    host_waiting = true;
    if (!post(OpenServiceRequest{ module_ids::platform, contract.number, contract.version, { ++next_request } }) ||
        !until([&] { return host_ready; }))
    {
        return false;
    }
    OpenServiceResult result;
    const bool success = host_response.copy_payload_to(result) && (result.status == EServiceStatus::success);
    host_ready = false;
    host_waiting = false;
    route = result.route;
    return success;
}
bool CConsumer::transact(const SServiceRoute route, const std::uint16_t opcode,
    const void* const data, const std::uint16_t size, SBorrow* const acquisition, const EServiceStatus expected)
{
    ServiceRequest request;
    request.route = route;
    request.request.value = ++next_request;
    request.opcode = opcode;
    request.payload_size = size;
    if (size) std::memcpy(request.payload, data, size);
    if (acquisition)
    {
        *acquisition = { route, request.request, {}, false, false, true };
        acquisition->expected = expected;
        if (!client.begin(route, request.request, opcode))
        {
            acquisition->live = false;
            return false;
        }
    }
    waiting = request.request.value;
    if ((route.value == input_route.value) && (opcode == static_cast<std::uint16_t>(ECommand::install_configuration))) pending_schema = waiting;
    response_ready = false;
    if (!post(request))
    {
        if (pending_schema == waiting) pending_schema = 0u;
        if (acquisition)
        {
            (void)client.abandon_unsubmitted(route, request.request);
            acquisition->live = false;
        }
        return false;
    }
    if (acquisition) acquisition->submitted = true;
    if (!until([&] { return acquisition ? (acquisition->token.value || acquisition->terminal) : response_ready; }))
    {
        return false;
    }
    if (acquisition) return acquisition->token.value != 0u;
    if (response.status != expected)
    {
        MV_LOG(error, "Input Host: opcode {} failed with {}", static_cast<unsigned>(opcode), static_cast<unsigned>(response.status));
        return false;
    }
    return true;
}
bool CConsumer::acquire(const SServiceRoute route, const std::uint16_t opcode, const void* const data,
    const std::uint16_t size, SBorrow*& result, const EServiceStatus expected)
{
    result = nullptr;
    for (std::uint32_t index = 0u; index < borrow_count; ++index)
    {
        if (!borrows[index].live)
        {
            result = &borrows[index];
            break;
        }
    }
    if (!result)
    {
        if (borrow_count == 8u) return false;
        result = &borrows[borrow_count++];
    }
    return transact(route, opcode, data, size, result, expected) && result->token.value;
}
bool CConsumer::notification(const SBorrow& borrow)
{
    RegisterServiceNotification request;
    if (!client.request_notification(borrow.token, request))
    {
        return false;
    }
    if (!post(request))
    {
        (void)client.abandon_unsubmitted_notification(borrow.token);
        return false;
    }
    return until([&] { return client.registration(borrow.token).value != 0u; });
}
bool CConsumer::release(SBorrow& borrow)
{
    ReleaseServiceBorrow request;
    for (SPipe& pipe : pipes)
    {
        if (pipe.borrow != &borrow) continue;
        if (pipe.reader) pipe.reader->close();
        pipe.view = nullptr;
        if (pipe.pending)
        {
            bool stored = false;
            for (std::uint64_t& retired : retired_requests)
            {
                if (retired) continue;
                retired = pipe.pending;
                stored = true;
                break;
            }
            if (!stored) return false;
            pipe.pending = 0u;
        }
    }
    if (borrow.token.value == control_borrow.value) shared = nullptr;
    if (!borrow.token.value && !borrow.terminal)
    {
        if (!until([&] { return borrow.acknowledged || borrow.terminal || borrow.token.value; }))
        {
            return false;
        }
        if (!borrow.terminal && !borrow.token.value && !borrow.cancel_sent)
        {
            CancelServiceRequest cancellation;
            if (!client.cancel(borrow.route, borrow.request, cancellation) || !post(cancellation))
            {
                return false;
            }
            borrow.cancel_sent = true;
        }
        if (!until([&] { return borrow.token.value || borrow.terminal; }))
        {
            return false;
        }
    }
    if (borrow.token.value && !client.retired(borrow.token))
    {
        if (!borrow.closing)
        {
            if (!client.release(borrow.token, request) || !post(request))
            {
                return false;
            }
            borrow.closing = true;
        }
        if (!until([&] { return client.retired(borrow.token); }))
        {
            return false;
        }
    }
    if (!until([&] { return client.forget(borrow.route, borrow.request); }))
    {
        return false;
    }
    borrow.live = false;
    for (SPipe& pipe : pipes)
    {
        if (pipe.borrow != &borrow) continue;
        pipe.reader.reset();
        pipe.borrow = nullptr;
    }
    return true;
}
bool CConsumer::install(const std::string& schema, SInstallationToken& installation)
{
    host_waiting = true;
    CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
    RawAssetTransfer* const asset = owner.payload<RawAssetTransfer>();
    if (!asset || !asset->storage.value.resize(schema.size(), 32u))
    {
        return false;
    }
    std::memcpy(asset->storage.value.data(), schema.data(), schema.size());
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
    if (!until([&] { return host_ready; }))
    {
        return false;
    }
    AssetResult result;
    const bool transferred = host_response.copy_payload_to(result) && (result.status == EAssetStatus::success);
    host_ready = false;
    if (!transferred) return false;
    source_asset = result.asset;
    if (!post(AssetAcquireRequest{ result.asset }) || !until([&] { return host_ready; }))
    {
        return false;
    }
    AssetAcquireResult held;
    const bool acquired = host_response.copy_payload_to(held) && (held.status == EAssetStatus::success);
    host_ready = false;
    if (!acquired) return false;
    source_acquisition = held.acquisition;
    if (!command(ECommand::install_configuration, SInstallConfiguration{ result.asset }))
    {
        return false;
    }
    SInstallationResult installed;
    if (response.payload_size != sizeof(installed))
    {
        return false;
    }
    std::memcpy(&installed, response.payload, sizeof(installed));
    installation = installed.installation;
    return release_source() && (installation.value != 0u);
}
bool CConsumer::release_source()
{
    struct SReplyScope
    {
        bool& waiting;
        bool& ready;
        ~SReplyScope() { waiting = ready = false; }
    } scope{ host_waiting, host_ready };
    host_ready = host_waiting = false;
    if (!until([&] { return asset_pending == EAssetPending::none; }))
    {
        return false;
    }
    if (pending_schema && !until([&] { return !pending_schema; }))
    {
        return false;
    }
    host_waiting = true;
    if (source_acquisition)
    {
        if (!post(AssetReleaseRequest{ source_acquisition }) || !until([&] { return host_ready; }))
        {
            return false;
        }
        AssetReleaseResult released;
        const bool success = host_response.copy_payload_to(released) && (released.status == EAssetStatus::success);
        host_ready = false;
        if (!success) return false;
        source_acquisition = {};
    }
    if (source_asset.is_valid())
    {
        if (!post(AssetDisposeRequest{ source_asset }) || !until([&] { return host_ready; }))
        {
            return false;
        }
        AssetDisposeResult disposed;
        const bool success = host_response.copy_payload_to(disposed) && (disposed.status == EAssetStatus::success);
        host_ready = false;
        if (!success) return false;
        source_asset = {};
    }
    host_waiting = false;
    return true;
}

bool CConsumer::setup()
{
    native = option("MORPHIC_INPUT_SOURCE", "native");
    ordinary = option("MORPHIC_INPUT_SOURCE", "ordinary");
    if (!module(modules::EModuleAction::load) || !open(k_input_contract, input_route) ||
        !open(platform::windows::k_window_contract, window_route))
    {
        return false;
    }
    if (!ordinary)
    {
        SBorrow* borrow = nullptr;
        if (!open(k_contract, control_route) || !acquire(control_route, 1u, nullptr, 0u, borrow))
        {
            return false;
        }
        const SServiceView view = client.view(borrow->token);
        if (!view.descriptor || (view.size != sizeof(SView)) || (view.version != 1u))
        {
            return false;
        }
        const SView& control = *static_cast<const SView*>(view.descriptor);
        if (control.consumer.value != thread.host_execution()->instance.value)
        {
            return false;
        }
        shared = control.shared;
        control_borrow = borrow->token;
        if (!shared || (shared->tracing && !reads.resize(k_trace_capacity)))
        {
            return false;
        }

    }
    if (!install((native || ordinary) ? desktop_schema() : tests::input_schema(), installations[0]) ||
        !install(option("MORPHIC_INPUT_CASE", "work-pressure") ? expanded_schema() : tests::controller_schema(), installations[1]))
    {
        return false;
    }

    SBorrow* window_borrow = nullptr;
    if (!acquire(window_route, static_cast<std::uint16_t>(platform::windows::EWindowCommand::create_window), nullptr, 0u, window_borrow))
    {
        return false;
    }
    const SServiceView window_descriptor = client.view(window_borrow->token);
    if (!window_descriptor.descriptor || (window_descriptor.size != sizeof(platform::windows::SWindowViewV1)))
    {
        return false;
    }
    const platform::windows::SWindowViewV1& window_view = *static_cast<const platform::windows::SWindowViewV1*>(window_descriptor.descriptor);
    window = window_view.window;
    native_window = window_view.native_window;
    if (!notification(*window_borrow)) return false;
    const platform::windows::SWindowReady ready{ window, window_borrow->token, client.registration(window_borrow->token) };
    if (!transact(window_route, static_cast<std::uint16_t>(platform::windows::EWindowCommand::window_ready), &ready, sizeof(ready)))
    {
        return false;
    }

    SBorrow* catalogue = nullptr;
    if (!acquire(input_route, static_cast<std::uint16_t>(ECommand::acquire_catalogue), nullptr, 0u, catalogue) || !notification(*catalogue) ||
        !command(ECommand::catalogue_ready, SCatalogueReady{ catalogue->token, client.registration(catalogue->token), catalogue->request }))
    {
        return false;
    }
    if (shared && !native)
    {
        for (std::uint32_t index = 0u; index < 4u; ++index)
        {
            SFeed neutral;
            neutral.instance = shared->device_instance[index].load();
            shared->latest[index].publish(neutral);
        }
    }
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {

        if (!acquire_pipe(index)) return false;
    }
    return until([&] { return active(); });
}
bool CConsumer::acquire_pipe(const std::uint32_t index)
{
    SPipe& pipe = pipes[index];
    pipe.last_sequence = pipe.pending = 0u;
    pipe.has_retained = pipe.drained = false;
    pipe.retained = {};
    pipe.retained_trace = {};
    std::memset(pipe.values, 0, sizeof(pipe.values));
    const SAcquireDesktop desktop{ window, installations[0], 3u, 4u };
    const SAcquireController controller{ installations[1], 0u, 8u, 4u };
    if (!acquire(input_route, static_cast<std::uint16_t>(index ? ECommand::acquire_controller : ECommand::acquire_desktop),
        index ? static_cast<const void*>(&controller) : static_cast<const void*>(&desktop), sizeof(desktop), pipe.borrow))
    {
        return false;
    }
    const SServiceView descriptor = client.view(pipe.borrow->token);
    if (!descriptor.descriptor || (descriptor.size != sizeof(SInputEndpointV4)) || (descriptor.version != 4u))
    {
        return false;
    }
    const SInputEndpointV4* const view = static_cast<const SInputEndpointV4*>(descriptor.descriptor);
    if (!pipe.reader.emplace() || !pipe.reader->install(*view, thread.host_execution()->instance))
    {
        return false;
    }
    pipe.view = view;
    if (!notification(*pipe.borrow) ||
        !command(ECommand::input_ready, SInputReady{ pipe.view->connection, client.registration(pipe.borrow->token), pipe.borrow->request }))
    {
        return false;
    }
    return true;
}
bool CConsumer::active()
{
    for (SPipe& pipe : pipes)
    {
        if (!pipe.view) continue;
        SInputSnapshot state;
        if (!pipe.borrow->terminal || (pipe.view->state->read(state) != EStateRead::success) ||
            (state.phase != EPhase::active) || state.pending || state.ingress_pending || pipe.pending)
        {
            return false;
        }
    }
    return true;
}
bool CConsumer::feed(SFeed frame)
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
        if (intended && (frame.offered >= intended))
        {
            const std::uint64_t late = frame.offered - intended;
            if (late > measuring->late_max) measuring->late_max = late;
        }
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
bool CConsumer::settle(const std::uint64_t milliseconds)
{
    CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(milliseconds).code != EWaitError::none)
    {
        return false;
    }
    for (;;)
    {
        std::uint64_t remaining = 0u;
        if (deadline.remaining_nanoseconds(remaining).code != EWaitError::none)
        {
            return false;
        }
        if (!remaining) return !failed;
        if (!cycle((remaining < 10000000u) ? remaining : 10000000u))
        {
            return false;
        }
    }
}
bool CConsumer::teardown()
{
    cleaning = true;
    host_ready = host_waiting = false;
    bool safe = true;
    for (SPipe& pipe : pipes)
    {
        if (pipe.borrow && pipe.borrow->live && !release(*pipe.borrow)) safe = false;
        failed = false;
    }
    if (!release_source()) safe = false;
    failed = false;
    for (const SInstallationToken installation : installations)
    {
        if (!shutdown_requested && installation.value && !command(ECommand::retire_configuration, SRetireConfiguration{ installation })) safe = false;
        failed = false;
    }
    for (std::uint32_t index = borrow_count; index; --index)
    {
        if (borrows[index - 1u].live && !release(borrows[index - 1u])) safe = false;
        failed = false;
    }
    for (const SServiceRoute route : { input_route, window_route, control_route })
    {
        if (!route.value) continue;
        waiting = ++next_request;
        response_ready = false;
        if (!post(CloseServiceRequest{ route, { waiting } }) || !until([&] { return response_ready; }) ||
            (response.status != EServiceStatus::success)) safe = false;
        failed = false;
    }
    if (!safe) return false;
    if (secondary || shutdown_requested)
    {
        return true;
    }
    if (!module(modules::EModuleAction::unload))
    {
        return false;
    }
    CErasedOwner owner = CErasedOwner::create<modules::ModuleRequest>();
    modules::ModuleRequest* const request = owner.payload<modules::ModuleRequest>();
    if (!request) return false;
    request->module = module_ids::executive;
    request->action = modules::EModuleAction::unload;
    ::threading::CErasedOwnerMsg message;
    message.set_message_type<modules::ModuleRequest>();
    message.set_async_slot(1);
    message.set_owner(std::move(owner));
    if (!thread.post(std::move(message))) return false;
    return until([&] { return thread.exit_requested(); });
}
bool CConsumer::run()
{
    bool success = setup();
    if (success) success = exercise();
    if (shared && !failed && !shutdown_requested)
    {
        const bool measured = report();
        success = success && measured;
    }
    if (!success)
    {
        std::fprintf(stderr, "Input Host: functional/measurement failure; attempting cooperative retirement\n");
        std::fflush(stderr);
    }
    failed = false;
    const bool retired = teardown();
    return success && retired;
}
bool CConsumer::run_secondary()
{
    secondary = true;
    bool success = open(k_input_contract, input_route) && install(tests::controller_schema(), installations[1]) &&
        acquire_pipe(1u) && until([&] { return active(); });
    if (success) success = until([&] { return (pipes[1].applied >= 2u) && (pipes[1].values[k_south_action][0] == 0.0f); });
    if (success) MV_LOG(info, "Input Host: second consumer independently applied press and release");
    failed = false;
    const bool retired = teardown();
    return success && retired;
}
static std::uint32_t MV_STD_ABI_CALL executive_entry(void* const data) noexcept
{
    ::threading::CThreadContext context{ *static_cast<::threading::CThreadResources*>(data) };
    context.startup();
    context.mark_running();
    TInstance<CConsumer> consumer;
    const bool allocated = consumer.emplace(context);
#if defined(MV_INPUT_SECONDARY)
    const bool success = allocated && consumer->run_secondary();
#else
    const bool success = allocated && consumer->run();
#endif
    consumer.reset();
    if (success)
    {
        MV_LOG(info, "Input Host: acceptance passed");
        context.mark_exiting();
        context.mark_exited();
    }
    else
    {
        MV_LOG(error, "Input Host: acceptance failed");
        context.mark_failed(1u);
    }
    return success ? 0u : 1u;
}
#if defined(MV_INPUT_SECONDARY)
static constexpr module_ids::id_type k_advertised_module_id{ module_ids::rendering };
#else
static constexpr module_ids::id_type k_advertised_module_id{ module_ids::executive };
#endif
static constexpr std::uint32_t k_advertised_version_minor{ 0u };
static modules::EBindingResult MV_STD_ABI_CALL query_function(const system_type_id type,
    const std::uint32_t, modules::FModuleFunction* const function) noexcept
{
    if (!function)
    {
        return modules::EBindingResult::invalid_argument;
    }
    *function = nullptr;
    if (type != system_type_ids::executive_thread_function)
    {
        return modules::EBindingResult::unsupported_function;
    }
    *function = reinterpret_cast<modules::FModuleFunction>(&executive_entry);
    return modules::EBindingResult::success;
}
static constexpr modules::SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();
static modules::CModuleBindingContext s_binding{ k_binding_config };
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v3(modules::SBootstrapFunctions* const functions) noexcept
{
    return input_host::s_binding.bootstrap(functions);
}
#if defined(MV_INPUT_SECONDARY)
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_thread_v1(modules::SServiceThread* const value) noexcept
{
    if (!value)
    {
        return modules::EBindingResult::invalid_argument;
    }
    *value = { thread_ids::rendering, &input_host::executive_entry };
    return modules::EBindingResult::success;
}
#endif
