
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_fixture.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Real Platform acceptance variant: borrowed feeds, synthetic adapters and traces.

#include <cstdlib>
#include <cstring>
#include "tests/input_host/control.hpp"
#include "platform/runtime/platform_runtime.hpp"
#include "platform/runtime/input_acceptance.hpp"
#include "containers/TInstance.hpp"
#include "debug/macros.hpp"

namespace platform::runtime
{
struct SInputAcceptanceAccess
{
    static std::int32_t reserve(CPlatformRuntime& runtime, const services::ServiceRequest& request) noexcept
    { return runtime.reserve(request, true); }
    static void complete(CPlatformRuntime& runtime, const std::int32_t slot, const services::EServiceStatus status) noexcept
    { runtime.complete(slot, status); }
    static const CInputConnection* connection(const CPlatformRuntime& runtime, const std::uint32_t index) noexcept
    { return runtime.m_inputs[index] ? &*runtime.m_inputs[index] : nullptr; }
    static bool idle(const CPlatformRuntime& runtime) noexcept
    {
        const CDesktopInput& desktop = runtime.m_desktop;
        if (desktop.m_count || desktop.m_baseline_pending || desktop.m_baseline_inflight || desktop.m_cutover || desktop.m_losing)
        {
            return false;
        }
        for (const TInstance<CInputConnection>& connection : runtime.m_inputs)
        {
            if (connection && (connection->mapper().pending() || connection->source_baseline_pending ||
                connection->source_baseline_inflight || connection->snapshot().pending || connection->snapshot().ingress_pending))
            {
                return false;
            }
        }
        return true;
    }
    static std::uint32_t occupancy(const CInputConnection& connection, const std::uint32_t stream) noexcept
    {
        return (stream == input::k_actions) ? (input::k_action_capacity - connection.m_actions.writable_count()) :
            (connection.view.text ? (input::k_text_capacity - connection.m_text.writable_count()) : 0u);
    }
    static acceptance::SObservationTrace origin(const CInputConnection& connection) noexcept
    {
        const CInputMapper& mapper = connection.m_mapper;
        const bool admitted = (mapper.m_observation.kind == runtime::EObservationKind::text) ? mapper.m_text_admitted : mapper.m_admitted;
        return (admitted && !mapper.m_selective && !mapper.m_observation.baseline) ?
            mapper.m_observation.trace : acceptance::SObservationTrace{};
    }
};
}

namespace platform::acceptance
{
using namespace input_host;
using services::EServiceStatus;
using runtime::SInputAcceptanceAccess;

struct SDevice
{
    SFeed history[k_history_capacity];
    std::uint32_t head{}, count{};
    std::uint64_t instance{}, ordinal{}, last_observation{}, seen_loss{};
    bool lost{};
};
struct SState
{
    explicit SState(const modules::SHostPumpContext& source) noexcept : context{ source } {}
    const modules::SHostPumpContext& context;
    SShared shared;
    runtime::CPlatformRuntime* runtime{};
    SView view;
    services::SDispatchedCommand acquisition;
    services::SServiceBorrow borrow;
    services::EServiceQuiescence quiescence{ services::EServiceQuiescence::none };
    std::int32_t completion{ -1 };
    bool offered{}, completed{}, retiring{}, freeze_requested{}, native{};
    bool controllers_started{};
    std::uint64_t next_device{ 1u }, next_origin{}, pump_start{};
    SDevice devices[4];
    float keys[256]{};
};
static TInstance<SState>& state_owner() noexcept
{
    static TInstance<SState> state;
    return state;
}

static bool option(const char* const name, const char* const expected) noexcept
{
#if MV_PLATFORM_WINDOWS
    char buffer[64]{};
    std::size_t size = 0u;
    return (getenv_s(&size, buffer, sizeof(buffer), name) == 0) && (std::strcmp(buffer, expected) == 0);
#else
    const char* const value = std::getenv(name);
    return value && (std::strcmp(value, expected) == 0);
#endif
}
bool initialise(const modules::SHostPumpContext& context) noexcept
{
    if (state_owner() || !state_owner().emplace(context))
    {
        MV_LOG(error, "Input fixture: state allocation failed");
        return false;
    }
    SState& state = *state_owner();
    state.native = option("MORPHIC_INPUT_SOURCE", "native");
    state.shared.tracing = !option("MORPHIC_INPUT_TRACE", "off");
    if (!state.shared.feed.initialise(k_feed_capacity) ||
        (state.shared.tracing && (!state.shared.traces.resize(k_trace_capacity) || !state.shared.pumps.resize(k_trace_capacity))))
    {
        MV_LOG(error, "Input fixture: feed/trace allocation failed");
        state_owner().reset();
        return false;
    }
    state.view.provider = context.instance;
    state.view.shared = &state.shared;
    return true;
}
void destroy() noexcept { state_owner().reset(); }
bool retired() noexcept { return !state_owner() || !state_owner()->borrow.value; }

bool command(runtime::CPlatformRuntime& runtime, const services::SDispatchedCommand& dispatched) noexcept
{
    SState& state = *state_owner();
    const std::int32_t completion = SInputAcceptanceAccess::reserve(runtime, dispatched.command);
    if (completion < 0) return false;
    if (state.borrow.value && (dispatched.requester.value == state.view.consumer.value) &&
        (dispatched.command.opcode == 2u) && !dispatched.command.payload_size)
    {
        state.freeze_requested = true;
        SInputAcceptanceAccess::complete(runtime, completion, EServiceStatus::success);
        return true;
    }
    if (state.offered || (dispatched.command.opcode != 1u) || dispatched.command.payload_size)
    {
        SInputAcceptanceAccess::complete(runtime, completion, EServiceStatus::invalid_request);
        return true;
    }
    state.acquisition = dispatched;
    state.completion = completion;
    state.view.consumer = dispatched.requester;
    state.offered = true;
    return true;
}
void message(const ::threading::CErasedPodMsg& message) noexcept
{
    if (!state_owner()) return;
    SState& state = *state_owner();
    services::ServiceBorrowControl control;
    services::ServiceRequestCancelled cancellation;
    if (message.copy_payload_to(control) && (control.borrow.value == state.borrow.value) && state.borrow.value &&
        (control.route.value == state.acquisition.command.route.value) && (control.request.value == state.acquisition.command.request.value))
    {
        state.shared.closing.store(true);
        if (control.control == services::EServiceBorrowControl::consumer_quiescent) state.quiescence = control.quiescence;
    }
    else if (message.copy_payload_to(cancellation) && state.offered &&
        (cancellation.route.value == state.acquisition.command.route.value) && (cancellation.request.value == state.acquisition.command.request.value))
    {
        state.shared.closing.store(true);
    }
}
void progress(runtime::CPlatformRuntime& runtime, const bool stopping) noexcept
{
    if (!state_owner()) return;
    SState& state = *state_owner();
    const modules::SHostPumpContext& context = state.context;
    if (stopping) state.shared.closing.store(true);
    if (state.offered && !state.completed)
    {
        EServiceStatus status = EServiceStatus::closing;
        if (!state.shared.closing.load())
        {
            const services::SServiceBorrowOffer offer{ state.acquisition.command.route, state.acquisition.command.request,
                { &state.view, sizeof(state.view), 1u } };
            status = context.services->prepare_borrow(context.service_context, &offer, &state.borrow);
        }
        if (status != EServiceStatus::busy)
        {
            SInputAcceptanceAccess::complete(runtime, state.completion, status);
            state.completed = true;
        }
    }
    if (state.shared.closing.load() && state.borrow.value)
    {
        if (!state.retiring)
        {
            const EServiceStatus status = context.services->begin_borrow_retirement(context.service_context, state.borrow);
            if (status == EServiceStatus::success) state.retiring = true;
            else if (status != EServiceStatus::busy) state.shared.failed.store(true);
        }
        const bool safe = (state.quiescence == services::EServiceQuiescence::relinquished) ||
            (state.quiescence == services::EServiceQuiescence::execution_ended) ||
            (state.quiescence == services::EServiceQuiescence::never_published);
        if (state.retiring && safe && (context.services->finish_borrow_retirement(context.service_context, state.borrow) == EServiceStatus::success))
        {
            state.borrow = {};
        }
    }
    if (state.shared.frozen.load()) return;
    bool idle = !state.shared.consumer.readable_count() && SInputAcceptanceAccess::idle(runtime);
    for (const SDevice& device : state.devices) idle = idle && !device.count;
    if (idle) state.shared.drained.store(state.shared.consumed.load(), std::memory_order_release);
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {
        const runtime::CInputConnection* const connection = SInputAcceptanceAccess::connection(runtime, index);
        state.shared.connection[index].store(connection ? connection->view.connection.value : 0u);
        const bool ready = connection && (connection->snapshot().phase == input::EPhase::active) &&
            !connection->snapshot().pending && !connection->snapshot().ingress_pending && !connection->source_baseline_pending &&
            !connection->source_baseline_inflight && !connection->mapper().pending();
        state.shared.ready_connection[index].store(ready ? connection->view.connection.value : 0u);
        for (std::uint32_t stream = 0u; stream < 2u; ++stream)
        {
            const std::uint32_t count = connection ? SInputAcceptanceAccess::occupancy(*connection, stream ? input::k_text : input::k_actions) : 0u;
            state.shared.occupancy[index][stream].store(count);
            if (count > state.shared.high_water[index][stream].load()) state.shared.high_water[index][stream].store(count);
        }
    }
}
static bool ticks(std::uint64_t& value) noexcept
{
    if (sample(value)) return true;
    state_owner()->shared.clock_errors.fetch_add(1u);
    return false;
}
void pump_begin(runtime::CPlatformRuntime& runtime) noexcept
{
    if (state_owner()) state_owner()->runtime = &runtime;
    if (!state_owner() || state_owner()->shared.frozen.load() || !state_owner()->shared.tracing)
    {
        return;
    }
    (void)ticks(state_owner()->pump_start);
}
void pump_end() noexcept
{
    if (!state_owner() || state_owner()->shared.frozen.load())
    {
        return;
    }
    SState& state = *state_owner();
    if (state.shared.tracing)
    {
        std::uint64_t end = 0u;
        if (ticks(end))
        {
            if (state.shared.pump_count < state.shared.pumps.size())
                state.shared.pumps[state.shared.pump_count++] = { state.pump_start, end };
            else ++state.shared.pump_overflow;
        }
    }
    if (state.freeze_requested) state.shared.frozen.store(true, std::memory_order_release);
}
void desktop_ingress(runtime::SDesktopTransaction& transaction) noexcept
{
    if (!state_owner() || state_owner()->shared.frozen.load() || !state_owner()->shared.tracing)
    {
        return;
    }
    if (!transaction.trace.observation) transaction.trace.observation = ++state_owner()->next_origin;
    (void)ticks(transaction.trace.ingress);
}
void controller_ingress(native::SControllerReading& reading) noexcept
{
    if (!state_owner() || state_owner()->shared.frozen.load() || !state_owner()->shared.tracing)
    {
        return;
    }
    if (!reading.trace.observation) reading.trace.observation = ++state_owner()->next_origin;
    (void)ticks(reading.trace.ingress);
}
void publication(const runtime::CInputConnection& connection, const std::uint32_t stream,
    const std::uint64_t generation, const std::uint64_t sequence, const bool boundary) noexcept
{
    if (!state_owner() || state_owner()->shared.frozen.load())
    {
        return;
    }
    SState& state = *state_owner();
    const std::uint32_t occupied = SInputAcceptanceAccess::occupancy(connection, stream);
    const std::uint32_t channel = (stream == input::k_text) ? 1u : 0u;
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {
        if (state.runtime && (SInputAcceptanceAccess::connection(*state.runtime, index) == &connection))
        {
            state.shared.connection[index].store(connection.view.connection.value);
            state.shared.publications[index][channel].fetch_add(1u);
            if (occupied > state.shared.high_water[index][channel].load()) state.shared.high_water[index][channel].store(occupied);
            break;
        }
    }
    if (!state.shared.tracing) return;
    if (state.shared.trace_count == state.shared.traces.size())
    {
        ++state.shared.trace_overflow;
        return;
    }
    const SObservationTrace origin = boundary ? SObservationTrace{} : SInputAcceptanceAccess::origin(connection);
    STrace& trace = state.shared.traces[state.shared.trace_count++];
    trace = { state.context.instance.value, connection.view.connection.value, generation, sequence,
        origin.observation, origin.offered, origin.ingress, 0u, stream, occupied, boundary ? 1u : 0u, 0u };
    //  Last sample before the caller makes this watermark visible to readers.
    (void)ticks(trace.publication);
}

static void arrival(runtime::CPlatformRuntime& runtime, const std::uint32_t index, const bool connected) noexcept
{
    SState& state = *state_owner();
    SDevice& device = state.devices[index];
    if (device.instance) runtime.controller_departed(device.instance);
    device = {};
    if (connected)
    {
        device.instance = state.next_device++;
        native::SControllerInfo info;
        info.device = device.instance;
        std::memcpy(info.name, "Input acceptance controller", 28u);
        runtime.controller_arrived(info);
    }
    state.shared.device_instance[index].store(device.instance);
}
static void drain(runtime::CPlatformRuntime& runtime, const std::uint64_t budget_ns) noexcept
{
    SState& state = *state_owner();
    if (state.native || state.shared.closing.load() || state.shared.frozen.load())
    {
        return;
    }
    platform::threading::CWaitDeadline budget;
    if (budget.set_after_nanoseconds(budget_ns).code != platform::threading::EWaitError::none)
    {
        state.shared.failed.store(true);
        return;
    }
    for (std::uint32_t work = 0u; work < 64u; ++work)
    {
        std::uint64_t remaining = 0u;
        if ((budget.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            break;
        }
        SFeed frame;
        if (!state.shared.consumer.read(frame)) break;
        if ((frame.session != state.borrow.value) || !state.borrow.value)
        {
            state.shared.failed.store(true);
            continue;
        }
        switch (frame.kind)
        {
            case EFeed::controller:
            {
                if (frame.device >= 4u)
                {
                    state.shared.failed.store(true);
                    break;
                }
                SDevice& device = state.devices[frame.device];
                if (!device.instance || (frame.instance != device.instance))
                {
                    break;
                }
                if (frame.observation <= device.last_observation)
                {
                    break;
                }
                if (device.count == k_history_capacity)
                {
                    device.lost = true;
                    device.head = device.count = 0u;
                    state.shared.history_loss.fetch_add(1u);
                }
                device.history[(device.head + device.count) & k_history_mask] = frame;
                ++device.count;
                break;
            }
            case EFeed::key:
            case EFeed::text:
            {
                runtime::SDesktopTransaction transaction;
                transaction.trace = { frame.observation, frame.offered, 0u };
                transaction.repeats = frame.repeats;
                if (frame.kind == EFeed::text)
                {
                    transaction.text = transaction.utf32 = true;
                    transaction.code_unit = frame.value;
                }
                else if (frame.device < 256u)
                {
                    state.keys[frame.device] = frame.value ? 1.0f : 0.0f;
                    transaction.controls[0] = { frame.device, state.keys[frame.device] };
                    transaction.count = 1u;
                }
                else
                {
                    state.shared.failed.store(true);
                    break;
                }
                runtime.desktop_observation(transaction);
                break;
            }
            case EFeed::focus:
            {
                runtime.input_focus(frame.value != 0u);
                break;
            }
            case EFeed::modal:
            {
                runtime.enter_modal(false);
                runtime.leave_modal(false);
                break;
            }
            case EFeed::arrival:
            {
                if (frame.device < 4u) arrival(runtime, frame.device, frame.value != 0u);
                else state.shared.failed.store(true);
                break;
            }
            case EFeed::history_loss:
            {
                if (frame.device < 4u) state.devices[frame.device].lost = true;
                else state.shared.failed.store(true);
                break;
            }
            case EFeed::freeze:
            {
                state.freeze_requested = true;
                break;
            }
        }
        state.shared.consumed.store(frame.observation, std::memory_order_release);
    }
}
static EServiceStatus create_window(native::SWindow& window) noexcept
{
    window.handle = 1u;
    window.instance = 1u;
    window.runtime->native_extent(800u, 600u, 96u, false);
    window.runtime->input_focus(true);
    return EServiceStatus::success;
}
static bool dispatch_window(native::SWindow& window, const std::uint64_t budget) noexcept
{
    drain(*window.runtime, budget);
    window.work_pending = !state_owner()->shared.closing.load() && !state_owner()->shared.frozen.load() &&
        (state_owner()->shared.consumer.readable_count() != 0u);
    if (!window.work_pending)
    {
        runtime::SInputObservation baseline;
        for (std::uint32_t key = runtime::input_control::k_first_key; key <= runtime::input_control::k_last_key; ++key)
        {
            baseline.controls[baseline.count++] = { key, state_owner()->keys[key] };
        }
        for (std::uint32_t key = runtime::input_control::k_first_mouse_button; key <= runtime::input_control::k_mouse_wheel; ++key)
        {
            baseline.controls[baseline.count++] = { key, state_owner()->keys[key] };
        }
        window.runtime->desktop_quiet(baseline, window.runtime->desktop_epoch());
    }
    return true;
}
static bool destroy_window(native::SWindow& window) noexcept
{
    window.handle = window.instance = 0u;
    window.work_pending = false;
    return true;
}
static bool capture_window(native::SWindow&, const bool) noexcept { return true; }
const native::SWindowAdapter& window_adapter() noexcept
{
    static const native::SWindowAdapter adapter{ &create_window, &dispatch_window, &destroy_window, &capture_window };
    return state_owner()->native ? native::window_adapter() : adapter;
}
static bool initialise_controllers(native::SControllers& source) noexcept
{
    source.state = &*state_owner();
    source.runtime->controller_backend(input::EControllerBackend::ready);
    state_owner()->controllers_started = true;
    for (std::uint32_t index = 0u; index < 4u; ++index) arrival(*source.runtime, index, true);
    return true;
}
static void dispatch_controllers(native::SControllers& source, const std::uint64_t budget) noexcept
{
    drain(*source.runtime, budget);
    source.work_pending = !state_owner()->shared.closing.load() && !state_owner()->shared.frozen.load() &&
        (state_owner()->shared.consumer.readable_count() != 0u);
}
static native::EControllerRead read_controller(native::SControllers&, const std::uint64_t instance,
    const bool baseline, native::SControllerReading& reading) noexcept
{
    for (std::uint32_t index = 0u; index < 4u; ++index)
    {
        SDevice& device = state_owner()->devices[index];
        if (!instance || (device.instance != instance))
        {
            continue;
        }
        const std::uint64_t lost = state_owner()->shared.latest[index].loss.load();
        if (!baseline && (device.lost || (lost != device.seen_loss)))
        {
            device.lost = false;
            device.seen_loss = lost;
            return native::EControllerRead::history_lost;
        }
        SFeed frame;
        if (baseline)
        {
            if (!state_owner()->shared.latest[index].read(frame))
            {
                return native::EControllerRead::empty;
            }
            if (frame.instance != instance)
            {
                return native::EControllerRead::empty;
            }
            device.head = device.count = 0u;
            device.lost = false;
            device.seen_loss = lost;
        }
        else
        {
            if (!device.count)
            {
                return native::EControllerRead::empty;
            }
            frame = device.history[device.head];
            device.head = (device.head + 1u) & k_history_mask;
            --device.count;
        }
        device.last_observation = frame.observation;
        reading.ordinal = ++device.ordinal;
        reading.time_ns = frame.observation;
        reading.trace = { frame.observation, frame.offered, 0u };
        reading.buttons = frame.value;
        reading.left_x = frame.left_x;
        reading.left_y = frame.left_y;
        reading.right_x = frame.right_x;
        reading.right_y = frame.right_y;
        reading.left_trigger = frame.left_trigger;
        reading.right_trigger = frame.right_trigger;
        return native::EControllerRead::reading;
    }
    return native::EControllerRead::disconnected;
}
static bool destroy_controllers(native::SControllers& source) noexcept
{
    source.state = nullptr;
    source.work_pending = false;
    return true;
}
const native::SControllerAdapter& controller_adapter() noexcept
{
    static const native::SControllerAdapter adapter{ &initialise_controllers, &dispatch_controllers, &read_controller, &destroy_controllers };
    return state_owner()->native ? native::controller_adapter() : adapter;
}
}
