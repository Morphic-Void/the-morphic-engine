
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
#include "framework/threading/messages/message_helpers.hpp"

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
    std::int32_t completion{ -1 }, freeze_completion{ -1 };
    bool offered{}, completed{}, retiring{}, freeze_requested{}, native{};
    bool controllers_started{}, report_case{}, report_hold{}, report_departure{};
    std::uint64_t delay_end{};
    std::uint32_t report_ordinal{}, report_steps{};
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
static native::EReportStep report_step(native::SReportSession& session) noexcept
{
    using namespace platform_report_v1;
    SState& state = *state_owner();
    runtime::SReportCandidate& candidate = session.candidate;
    PlatformReportHeader& header = candidate.header;
    if (state.report_hold) return native::EReportStep::more;
    if (state.report_departure && (state.report_ordinal == 2u))
    {
        std::uint64_t now{};
        if (session.clock.read(now).code != platform::threading::EWaitError::none)
        {
            session.failure = { reports::EStage::capture, reports::EReason::clock_failed };
            return native::EReportStep::failed;
        }
        if (!state.delay_end) state.delay_end = now + UINT64_C(250000000);
        if (now < state.delay_end)
        {
            return native::EReportStep::more;
        }
    }
    if (!state.report_departure && (state.report_ordinal == 2u))
    {
        session.failure = { reports::EStage::capture, reports::EReason::native_failure, reports::EErrorDomain::win32, 31u };
        return native::EReportStep::failed;
    }
    //  A check-only step lets the Executive exercise overlapping public requests.
    const bool denied = state.report_ordinal == 4u;
    if (++state.report_steps <= (denied ? 1u : 4u)) ++session.native_calls;
    if (state.report_steps < 8u)
    {
        return native::EReportStep::more;
    }
    header.discovery = denied ? PlatformDiscovery::unavailable : PlatformDiscovery::captured;
    header.coverage = denied ? PlatformCoverage::limited : PlatformCoverage::complete;
    header.inventory_query_id = 1u;
    header.consistency_query_id = 2u;
    if (state.report_ordinal == 3u) --header.limits.query_passes;
    const schema::SSpanReference absent{ -1, 0, 0u };
    if (denied)
    {
        header.adapters = header.sources = header.displays = header.paths = header.mode_catalogues = absent;
    }
    const std::uint32_t count = denied ? 2u : 4u;
    header.queries = { 1, 0, count };
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        PlatformQueryResult row{};
        row.id = i + 1u;
        constexpr PlatformQueryKind kinds[]{ PlatformQueryKind::inventory, PlatformQueryKind::consistency_check,
            PlatformQueryKind::desktop_annotation, PlatformQueryKind::adapter_output_inventory };
        constexpr PlatformQueryApi apis[]{ PlatformQueryApi::windows_ccd, PlatformQueryApi::windows_ccd,
            PlatformQueryApi::windows_gdi_devices, PlatformQueryApi::windows_dxgi };
        row.kind = kinds[i];
        row.api = apis[i];
        row.native_flags = (i < 2u) ? 0x51u : 0u;
        row.subject_kind = PlatformSubjectKind::report;
        row.status = PlatformQueryStatus::success;
        row.call_count = row.attempts = 1u;
        row.diagnostic.string_value_id = -1;
        if (denied && !i)
        {
            row.status = PlatformQueryStatus::unavailable;
            row.reason = PlatformQueryReason::access_denied;
            row.error_domain = PlatformErrorDomain::win32;
            row.native_error = 5u;
        }
        else if (denied)
        {
            row.status = PlatformQueryStatus::blocked;
            row.reason = PlatformQueryReason::dependency_missing;
            row.blocked_by_query_id = 1u;
            row.call_count = row.attempts = 0u;
        }
        if (!candidate.queries.push_back(row))
        {
            session.failure = { reports::EStage::capture, reports::EReason::allocation_failed };
            return native::EReportStep::failed;
        }
    }
    if (session.clock.read(header.capture_end_ns).code != platform::threading::EWaitError::none)
    {
        session.failure = { reports::EStage::capture, reports::EReason::clock_failed };
        return native::EReportStep::failed;
    }
    return native::EReportStep::finished;
}

const native::SReportAdapter& report_adapter() noexcept
{
    static constexpr native::SReportAdapter injected{ &report_step };
    return state_owner()->report_case ? injected : native::report_adapter();
}

bool initialise(const modules::SHostPumpContext& context) noexcept
{
    if (state_owner() || !state_owner().emplace(context))
    {
        MV_LOG(error, "Input fixture: state allocation failed");
        return false;
    }
    SState& state = *state_owner();
    state.report_hold = option("MORPHIC_PUMP_CASE", "platform-report-cancel") || option("MORPHIC_PUMP_CASE", "platform-report-shutdown");
    state.report_departure = option("MORPHIC_PUMP_CASE", "platform-report-departure");
    state.report_case = state.report_hold || state.report_departure || option("MORPHIC_PUMP_CASE", "platform-report-injected");
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
        if (state.shared.frozen.load())
        {
            SInputAcceptanceAccess::complete(runtime, completion, EServiceStatus::success);
            return true;
        }
        if (state.freeze_requested)
        {
            SInputAcceptanceAccess::complete(runtime, completion, EServiceStatus::busy);
            return true;
        }
        state.freeze_requested = true;
        state.freeze_completion = completion;
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
    if (state.report_case)
    {
        reports::PlatformReportCaptureRequest request;
        reports::PlatformReportCaptureReceipt receipt;
        reports::PlatformReportResult current;
        AssetReleaseResult released;
        if (message.copy_payload_to(request))
        {
            ++state.report_ordinal;
            state.report_steps = 0u;
            if (state.report_ordinal > 1u)
            {
                const reports::PlatformReportCaptureFailed stale{
                    { request.capture.platform_instance, (request.capture.capture_id - 1u) }, { reports::EStage::capture, reports::EReason::clock_failed } };
                const auto previous = ::threading::make_pod_message(-1, stale);
                (void)state.context.services->post_pod(state.context.service_context, &previous);
            }
        }
        else if (message.copy_payload_to(receipt) && state.report_hold && (receipt.disposition == reports::EDisposition::interrupted))
        {
            MV_LOG(info, "Platform consumed the pending-capture interruption receipt");
        }
        else if (message.copy_payload_to(receipt) && state.report_departure && (state.report_ordinal == 2u) &&
            (receipt.disposition == reports::EDisposition::published))
        {
            MV_LOG(info, "Host published maintenance capture across requester departure");
        }
        else if (message.copy_payload_to(receipt) && (state.report_ordinal == 1u) &&
            (receipt.disposition == reports::EDisposition::published))
        {
            const auto read = ::threading::make_pod_message(710, reports::PlatformReportRequest{});
            if (state.context.services->post_pod(state.context.service_context, &read) != EServiceStatus::success)
            {
                MV_LOG(error, "Platform report fixture could not submit current read");
            }
        }
        else if (message.copy_payload_to(current) && (message.query_async_slot() == 710))
        {
            const auto release = ::threading::make_pod_message(711, AssetReleaseRequest{ current.acquisition });
            if ((current.status != reports::EStatus::success) || !current.acquisition || !current.bytes.size() ||
                (state.context.services->post_pod(state.context.service_context, &release) != EServiceStatus::success))
            {
                MV_LOG(error, "Platform report fixture failed to acquire current");
            }
        }
        else if (message.copy_payload_to(released) && (message.query_async_slot() == 711) && (released.status == EAssetStatus::success))
        {
            MV_LOG(info, "Platform independently acquired and released the Host report");
        }
    }
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
    if (state.shared.closing.load() && (state.freeze_completion >= 0))
    {
        SInputAcceptanceAccess::complete(runtime, state.freeze_completion, EServiceStatus::closing);
        state.freeze_completion = -1;
    }
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
    if (state.freeze_requested && state.runtime && !state.shared.consumer.readable_count() &&
        SInputAcceptanceAccess::idle(*state.runtime))
    {
        for (const SDevice& device : state.devices)
        {
            if (device.count) return;
        }
        for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
        {
            const runtime::CInputConnection* const connection = SInputAcceptanceAccess::connection(*state.runtime, index);
            if (connection && (SInputAcceptanceAccess::occupancy(*connection, 0u) || SInputAcceptanceAccess::occupancy(*connection, 1u)))
            {
                return;
            }
        }
        state.shared.frozen.store(true, std::memory_order_release);
        if (state.freeze_completion >= 0)
        {
            SInputAcceptanceAccess::complete(*state.runtime, state.freeze_completion, EServiceStatus::success);
            state.freeze_completion = -1;
        }
    }
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
