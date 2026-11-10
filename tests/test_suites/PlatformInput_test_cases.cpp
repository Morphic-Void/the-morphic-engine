
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PlatformInput_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    06 Nov 26
//
//  Synthetic input records through real routing, notifications and borrow cleanup.

#include <cstring>
#include <cstdio>
#include <cwchar>
#include <thread>
#include <chrono>
#include "platform/runtime/platform_runtime.hpp"
#include "framework/platform/input_reader.hpp"
#include "host/runtime/service_router.hpp"
#include "host/runtime/host_restrictions.hpp"
#include "tests/support/test_context.hpp"
#include "threading/messages/message_helpers.hpp"
#include "services/service_borrow_client.hpp"
#include "tests/support/input_schema.hpp"
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#include "platform/native/windows/input_windows.hpp"
#endif

namespace platform::runtime
{
struct SPlatformInputTestAccess
{
    static EInputPost post_action(CInputConnection& connection, const input::SActionInputV1& record) noexcept
    {
        return connection.post_action(record);
    }
    static EInputPost post_text(CInputConnection& connection, const input::STextInputV1& record) noexcept
    {
        return connection.post_text(record);
    }
    static bool install(CPlatformRuntime& runtime, const input::EConnection kind) noexcept
    {
        const std::int32_t slot = runtime.m_input_installations.emplace();
        if (slot < 0) return false;
        SInputInstallation& installation = *runtime.m_input_installations.get_object(slot);
        installation.token.value = static_cast<std::uint64_t>(kind);
        installation.kind = kind;
        installation.profile = 1u;
        if (!installation.contexts.resize(3u) || !installation.subjects.resize(3u))
        {
            return false;
        }
        for (std::uint32_t index = 0u; index < 3u; ++index)
        {
            installation.contexts[index] = index + 1u;
            installation.subjects[index] = index + 1u;
        }
        return true;
    }
    static CInputConnection* connection(CPlatformRuntime& runtime, const input::EConnection kind) noexcept
    {
        TInstance<CInputConnection>& owned = runtime.m_inputs[(kind == input::EConnection::desktop) ? 0u : 1u];
        return owned ? &*owned : nullptr;
    }
    static void saturate_acquisitions(CPlatformRuntime& runtime, const bool busy) noexcept
    {
        for (std::uint32_t index = 0u; index < 32u; ++index)
        {
            runtime.m_completions[index].live = busy;
        }
    }
    static void exhaust_sequence(CInputConnection& connection) noexcept
    {
        connection.m_state.committed = UINT64_MAX;
    }
    static input::CInputPublication& publication(CInputConnection& connection) noexcept
    {
        return connection.m_publication;
    }
    static void late_dispatch(CPlatformRuntime& runtime) noexcept { runtime.m_native_first = true; }
    static std::uintptr_t handle(const CPlatformRuntime& runtime) noexcept { return runtime.m_window.native.handle; }
    static const native::SDesktop& desktop(const CPlatformRuntime& runtime) noexcept { return runtime.m_window.native.desktop; }
    static CInputConnection* connection(CPlatformRuntime& runtime, const input::SConnectionToken token) noexcept { return runtime.find_input(token); }
    static input::SCatalogueSnapshot catalogue(const CPlatformRuntime& runtime) noexcept { return runtime.m_catalogue; }
    static std::uint64_t device(const CPlatformRuntime& runtime, const std::uint64_t native) noexcept
    {
        for (const CPlatformRuntime::SDevice& device : runtime.m_devices)
        {
            if (device.token && (device.info.device == native)) return device.token;
        }
        return 0u;
    }
};
}

namespace platform::input
{
struct SInputPublicationTestAccess
{
    static void revision(CInputPublication& publication, const std::uint32_t revision) noexcept
    {
        publication.m_revision.store(revision);
    }
};
struct SInputReaderTestAccess
{
    static void copy_action(CInputReader& reader) noexcept
    {
        reader.m_has_action = reader.m_view->actions->read(reader.m_action);
    }
};
}

namespace
{

using tests::TTestContext;
using namespace services;
using namespace platform::input;
using platform::runtime::CInputConnection;
using platform::runtime::EInputPost;
using platform::runtime::SPlatformInputTestAccess;

struct SClock
{
    std::uint64_t time{ 100u };
    bool native_time{};
    static platform::threading::WaitError read(void* const context, std::uint64_t& time) noexcept
    {
        const SClock& clock = *static_cast<SClock*>(context);
        if (clock.native_time)
        {
            return platform::threading::query_awake_nanoseconds(time);
        }
        time = clock.time;
        return {};
    }
    platform::threading::SAwakeClock source() noexcept { return { this, &read }; }
};

struct SNative
{
    unsigned destroyed{ 0u };
    std::uint64_t committed_at_failure{};
    bool fail_dispatch{ false };
    bool captured{}, revoke_during_capture{}, fail_release{};
    static inline SNative* current{ nullptr };
    static EServiceStatus create(platform::native::SWindow& window) noexcept
    {
        window.handle = 71u;
        window.instance = 72u;
        window.runtime->native_extent(800u, 600u, 96u, false);
        return EServiceStatus::success;
    }
    static bool dispatch(platform::native::SWindow& window, const std::uint64_t) noexcept
    {
        if (current->fail_dispatch)
        {
            const CInputConnection* const connection = SPlatformInputTestAccess::connection(*window.runtime, EConnection::desktop);
            current->committed_at_failure = connection ? connection->snapshot().committed : 0u;
            window.runtime->native_failure(7u);
        }
        return true;
    }
    static bool destroy(platform::native::SWindow& window) noexcept
    {
        ++current->destroyed;
        window.handle = window.instance = 0u;
        return true;
    }
    static const platform::native::SWindowAdapter& adapter() noexcept
    {
        static const platform::native::SWindowAdapter adapter{ &create, &dispatch, &destroy };
        return adapter;
    }
    static bool capture(platform::native::SWindow& window, const bool acquire) noexcept
    {
        if (!acquire && current->fail_release)
        {
            return false;
        }
        current->captured = acquire;
        if (acquire && current->revoke_during_capture) window.runtime->input_focus(false);
        return true;
    }
    static bool desktop_dispatch(platform::native::SWindow& window, const std::uint64_t budget) noexcept
    {
        platform::runtime::SInputObservation baseline;
        window.runtime->desktop_quiet(baseline, window.runtime->desktop_epoch());
        return dispatch(window, budget);
    }
    static const platform::native::SWindowAdapter& desktop_adapter() noexcept
    {
        static const platform::native::SWindowAdapter adapter{ &create, &desktop_dispatch, &destroy, &capture };
        return adapter;
    }
};

struct SControllers
{
    struct SDevice
    {
        bool connected{}, history_lost{}, baseline_wait{};
        platform::native::SControllerInfo info;
        platform::native::SControllerReading latest, queued[16]{};
        std::uint32_t head{}, count{}, reads{};
        std::uint64_t ordinal{};
    } devices[k_device_capacity];
    bool available{ true };
    bool eligible{ true }, delay_resume{};
    std::uint32_t eligibility_calls{};
    static inline SControllers* current{};
    static bool initialise(platform::native::SControllers& source) noexcept
    {
        if (!current->available) return false;
        source.state = current;
        source.runtime->controller_backend(EControllerBackend::ready);
        return true;
    }
    static void dispatch(platform::native::SControllers& source, const std::uint64_t) noexcept { source.work_pending = false; }
    static void eligibility(platform::native::SControllers& source, const bool eligible) noexcept
    {
        SControllers& self = *static_cast<SControllers*>(source.state);
        self.eligible = eligible;
        ++self.eligibility_calls;
        if (eligible && self.delay_resume)
        {
            for (SDevice& device : self.devices) device.baseline_wait = device.connected;
        }
    }
    static platform::native::EControllerRead read(platform::native::SControllers& source,
        const std::uint64_t instance, const bool baseline, platform::native::SControllerReading& reading) noexcept
    {
        SControllers& self = *static_cast<SControllers*>(source.state);
        for (SDevice& device : self.devices)
        {
            if (!device.connected || (device.info.device != instance))
            {
                continue;
            }
            ++device.reads;
            if (device.history_lost)
            {
                device.history_lost = false;
                return platform::native::EControllerRead::history_lost;
            }
            if (baseline)
            {
                if (device.baseline_wait)
                {
                    return platform::native::EControllerRead::empty;
                }
                reading = device.latest;
                device.head = device.count = 0u;
            }
            else if (device.count)
            {
                reading = device.queued[device.head];
                device.head = (device.head + 1u) & 15u;
                --device.count;
            }
            else return platform::native::EControllerRead::empty;
            reading.ordinal = ++device.ordinal;
            return platform::native::EControllerRead::reading;
        }
        return platform::native::EControllerRead::disconnected;
    }
    static bool destroy(platform::native::SControllers& source) noexcept { source.state = nullptr; return true; }
    static const platform::native::SControllerAdapter& adapter() noexcept
    {
        static const platform::native::SControllerAdapter result{ &initialise, &dispatch, &read, &destroy, &eligibility };
        return result;
    }
    void attach(platform::runtime::CPlatformRuntime& runtime, const std::uint32_t slot, const std::uint64_t instance) noexcept
    {
        SDevice& device = devices[slot];
        device = {};
        device.connected = true;
        device.info.device = instance;
        device.info.vendor = 0x045eu;
        device.info.product = 0x028eu;
        std::memcpy(device.info.name, "Synthetic pad", 14u);
        runtime.controller_arrived(device.info);
    }
    void sample(const std::uint32_t slot, const platform::native::SControllerReading& reading) noexcept
    {
        SDevice& device = devices[slot];
        device.latest = reading;
        if (device.count == 16u) device.history_lost = true;
        else device.queued[(device.head + device.count++) & 15u] = reading;
    }
};

struct SReceived
{
    ServiceReply reply;
    bool second{ false };
};

struct SFixture
{
    TTestContext& tests;
    SClock clock;
    SNative native;
    SControllers controllers;
    platform::threading::COwnerWait wake;
    host::CHostRestrictions restrictions{ clock.source() };
    host::SClientRecord client{ module_ids::executive, 101u }, other{ module_ids::rendering, 102u };
    host::SClientRecord provider{ module_ids::platform, 103u };
    host::CServiceRouter router;
    modules::SHostExecutionContext execution;
    modules::SHostPumpServices services;
    modules::SHostPumpContext context;
    TInstance<platform::runtime::CPlatformRuntime> runtime;
    SServiceRoute route, other_route, window_route;
    std::uint64_t sequence{ 1u };
    SReceived history[512]{};
    std::uint32_t history_count{ 0u };
    bool completion_busy{ false };
    modules::SProgress progress;
    CAssetRepository assets;
    schema::CPreparedSchema conditioned;
    std::string source;
    threading::CErasedPodMsg host_requests[4];
    unsigned host_request_count{}, schema_requests{}, schema_releases{};
    bool hold_schema{ false }, hold_release{ false }, post_busy{ false };
    EAssetStatus schema_status{ EAssetStatus::success };

    static EServiceStatus MV_STD_ABI_CALL post(void* const context, const threading::CErasedPodMsg* const message) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        if (self.post_busy || (self.host_request_count == 4u)) return EServiceStatus::busy;
        self.host_requests[self.host_request_count++] = *message;
        return EServiceStatus::success;
    }
    static EServiceStatus MV_STD_ABI_CALL commit(void* const context, const ServiceReply* const reply) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        return self.completion_busy ? EServiceStatus::busy : self.router.commit_resource(self.provider, *reply);
    }
    CAssetId schema_source(const unsigned extra = 0u)
    {
        source = tests::input_schema(extra);
        TEST_EXPECT(tests, assets.initialise());
        return assets.insert(CErasedOwner::create<RawAssetTransfer>());
    }
    void host_step() noexcept
    {
        if (!host_request_count) return;
        const threading::CErasedPodMsg message = host_requests[0];
        SchemaPrepareRequest prepare;
        AssetReleaseRequest release;
        if (message.copy_payload_to(prepare))
        {
            if (hold_schema) return;
            SchemaPrepareResult reply;
            if (schema_status != EAssetStatus::success)
            {
                reply.status = schema_status;
                TEST_EXPECT(tests, provider.post_reply(threading::make_pod_message(message.query_async_slot(), reply)));
                --host_request_count;
                return;
            }
            const bool prepared = conditioned.prepare({ reinterpret_cast<const std::uint8_t*>(source.data()), source.size() },
                prepare.policy, prepare.limits, reply.diagnostic);
            if (!prepared)
            {
                std::printf("Synthetic schema: status %u, stage %u, reason %u, line %u, column %u\n",
                    static_cast<unsigned>(reply.diagnostic.status), reply.diagnostic.stage, reply.diagnostic.reason,
                    reply.diagnostic.line, reply.diagnostic.column);
            }
            TEST_EXPECT(tests, prepared);
            reply.status = EAssetStatus::success;
            reply.acquisition = CAssetAcquisitionId{ ++schema_requests };
            reply.descriptor = conditioned.descriptor();
            TEST_EXPECT(tests, provider.post_reply(threading::make_pod_message(message.query_async_slot(), reply)));
        }
        else if (message.copy_payload_to(release))
        {
            if (hold_release) return;
            ++schema_releases;
            conditioned.clear();
            TEST_EXPECT(tests, provider.post_reply(threading::make_pod_message(message.query_async_slot(),
                AssetReleaseResult{ release.acquisition, EAssetStatus::success })));
        }
        else TEST_EXPECT(tests, false);
        --host_request_count;
        for (unsigned index = 0u; index < host_request_count; ++index) host_requests[index] = host_requests[index + 1u];
    }

    static EServiceStatus MV_STD_ABI_CALL complete(void* const context, const ServiceReply* const reply) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        return self.completion_busy ? EServiceStatus::busy : self.router.complete(self.provider, *reply);
    }
    static EServiceStatus MV_STD_ABI_CALL prepare(void* const context, const SServiceBorrowOffer* const offer,
        SServiceBorrow* const borrow) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        return self.router.prepare_borrow(self.provider, *offer, *borrow);
    }
    static EServiceStatus MV_STD_ABI_CALL begin(void* const context, const SServiceBorrow borrow) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        return self.router.begin_borrow_retirement(self.provider, borrow);
    }
    static EServiceStatus MV_STD_ABI_CALL finish(void* const context, const SServiceBorrow borrow) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        return self.router.finish_borrow_retirement(self.provider, borrow);
    }
    static EServiceStatus MV_STD_ABI_CALL bind(void* const context, const SServiceBorrow borrow,
        const SNotificationRegistration registration, threading::SNotificationEndpoint* const endpoint) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(context);
        return self.router.bind_borrow_notification(self.provider, borrow, registration, *endpoint);
    }
    static modules::SRestrictionEnterResult MV_STD_ABI_CALL enter(void* const context, const modules::SRestrictionReason reason) noexcept
    {
        return static_cast<SFixture*>(context)->restrictions.enter(reason);
    }
    static modules::ERestrictionStatus MV_STD_ABI_CALL leave(void* const context, const modules::SRestrictionScope scope) noexcept
    {
        return static_cast<SFixture*>(context)->restrictions.leave(scope);
    }
    static modules::SHostMaintenanceResult MV_STD_ABI_CALL maintain(void* const) noexcept
    {
        modules::SHostMaintenanceResult result;
        result.status = modules::EMaintenanceStatus::success;
        return result;
    }
    explicit SFixture(TTestContext& tests, const bool seed = true,
        const platform::native::SWindowAdapter& adapter = SNative::adapter(),
        const platform::native::SControllerAdapter& controller_adapter = SControllers::adapter()) noexcept : tests{ tests }
    {
        SNative::current = &native;
        SControllers::current = &controllers;
        TEST_EXPECT(tests, (wake.initialise().code == platform::threading::EWaitError::none));
        client.phase = other.phase = provider.phase = host::EClientPhase::ready;
        client.execution = other.execution = provider.execution = host::EClientExecution::pumped;
        client.owner_wake = other.owner_wake = provider.owner_wake = wake.endpoint();
        services.complete = &complete;
        services.post_pod = &post;
        services.commit_resource = &commit;
        services.prepare_borrow = &prepare;
        services.begin_borrow_retirement = &begin;
        services.finish_borrow_retirement = &finish;
        services.bind_borrow_notification = &bind;
        services.enter_restriction = &enter;
        services.leave_restriction = &leave;
        services.maintain_host = &maintain;
        execution.restriction = restrictions.reader();
        context.instance = provider.instance;
        context.services = &services;
        context.service_context = this;
        context.execution = &execution;
        context.owner_wake = wake.endpoint();
        TEST_EXPECT(tests, (runtime.emplace(context, adapter, clock.source(), controller_adapter) && runtime->initialise()));
        if (seed)
        {
            TEST_EXPECT(tests, SPlatformInputTestAccess::install(*runtime, EConnection::desktop));
            TEST_EXPECT(tests, SPlatformInputTestAccess::install(*runtime, EConnection::controller));
        }
        route = open(client, k_input_contract);
        other_route = open(other, k_input_contract);
        window_route = open(client, platform::windows::k_window_contract);
    }
    SServiceRoute open(host::SClientRecord& recipient, const SServiceContract contract) noexcept
    {
        const SServiceContract contracts[]{ platform::windows::k_window_contract, k_input_contract };
        host::SServiceProvider advertisement{ &provider, contracts, 2u };
        TEST_EXPECT(tests, recipient.reserve_replies(1u));
        router.open({ module_ids::platform, contract.number, contract.version, { sequence++ } }, 1, recipient, &advertisement);
        OpenServiceResult result;
        TEST_EXPECT(tests, (recipient.messages[0].copy_payload_to(result) && (result.status == EServiceStatus::success)));
        recipient.consume(1u);
        return result.route;
    }
    void collect() noexcept
    {
        for (host::SClientRecord* const recipient : { &client, &other })
        {
            threading::CErasedPodMsg messages[64];
            std::uint32_t controls = 0u;
            const std::uint32_t count = recipient->copy_messages(messages, 64u, controls);
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                ServiceReply reply;
                if (messages[index].copy_payload_to(reply))
                {
                    TEST_EXPECT(tests, (history_count < 512u));
                    if (history_count < 512u) history[history_count++] = { reply, recipient == &other };
                }
            }
            recipient->consume_messages(count, controls);
        }
    }
    std::uint64_t submit(const std::uint16_t opcode, const void* const payload, const std::uint16_t size,
        const bool second = false, const bool window = false) noexcept
    {
        host::SClientRecord& recipient = second ? other : client;
        ServiceRequest request;
        request.route = window ? window_route : (second ? other_route : route);
        request.request.value = sequence++;
        request.opcode = opcode;
        request.payload_size = size;
        if (size) std::memcpy(request.payload, payload, size);
        TEST_EXPECT(tests, recipient.reserve_replies(2u));
        router.request(request, static_cast<std::int32_t>(request.request.value), recipient);
        return request.request.value;
    }
    template<typename T>
    std::uint64_t submit(const ECommand opcode, const T& payload, const bool second = false) noexcept
    {
        return submit(static_cast<std::uint16_t>(opcode), &payload, sizeof(payload), second);
    }
    void step() noexcept
    {
        host_step();
        router.progress();
        SDispatchedCommand commands[64];
        threading::CErasedPodMsg messages[64];
        std::uint32_t controls = 0u;
        modules::SPumpInput input;
        input.commands = commands;
        input.command_count = router.prepare_batch(provider, commands, 64u);
        input.messages = messages;
        input.message_count = provider.copy_messages(messages, 64u, controls);
        modules::SPumpResult result;
        TEST_EXPECT(tests, restrictions.begin_invocation(provider.instance, true));
        runtime->pump(input, result);
        progress = result.progress;
        TEST_EXPECT(tests, restrictions.end_invocation());
        TEST_EXPECT(tests, router.finish_batch(provider, commands, input.command_count, result.consumed_commands));
        provider.consume_messages(result.consumed_messages, controls);
        router.progress();
        collect();
    }
    SPreparedServiceBorrow prepared(const std::uint64_t request) const noexcept
    {
        for (std::uint32_t index = 0u; index < history_count; ++index)
        {
            const ServiceReply& reply = history[index].reply;
            if ((reply.request.value == request) && (reply.notice == EServiceNotice::prepared))
            {
                SPreparedServiceBorrow result;
                std::memcpy(&result, reply.payload, sizeof(result));
                return result;
            }
        }
        return {};
    }
    EServiceStatus terminal(const std::uint64_t request) const noexcept
    {
        for (std::uint32_t index = 0u; index < history_count; ++index)
        {
            const ServiceReply& reply = history[index].reply;
            if ((reply.request.value == request) && (reply.notice == EServiceNotice::completed))
            {
                return reply.status;
            }
        }
        return EServiceStatus::unavailable;
    }
    ServiceNotificationRegistered registration(const SPreparedServiceBorrow& borrow, const bool second = false,
        const bool window = false) noexcept
    {
        host::SClientRecord& recipient = second ? other : client;
        TEST_EXPECT(tests, recipient.reserve_replies(1u));
        router.register_notification(recipient, { (window ? window_route : (second ? other_route : route)), borrow.borrow }, 400);
        ServiceNotificationRegistered result;
        TEST_EXPECT(tests, recipient.messages[recipient.message_count - 1u].copy_payload_to(result));
        collect();
        return result;
    }
    std::uint64_t window() noexcept
    {
        const std::uint64_t request = submit(static_cast<std::uint16_t>(platform::windows::EWindowCommand::create_window), nullptr, 0u, false, true);
        step();
        const SPreparedServiceBorrow borrow = prepared(request);
        TEST_EXPECT(tests, (borrow.view.descriptor != nullptr));
        if (!borrow.view.descriptor) return 0u;
        const platform::windows::SWindowViewV1& view = *static_cast<const platform::windows::SWindowViewV1*>(borrow.view.descriptor);
        const ServiceNotificationRegistered registered = registration(borrow, false, true);
        const platform::windows::SWindowReady ready{ view.window, borrow.borrow, registered.registration };
        (void)submit(static_cast<std::uint16_t>(platform::windows::EWindowCommand::window_ready), &ready, sizeof(ready), false, true);
        step();
        return view.window.value;
    }
    void release(const SPreparedServiceBorrow& borrow, const bool second = false, const bool window = false) noexcept
    {
        host::SClientRecord& recipient = second ? other : client;
        const SServiceRoute source = window ? window_route : (second ? other_route : route);
        router.release_borrow(recipient, { source, borrow.borrow });
        router.quiesce_borrow(recipient, { source, borrow.borrow });
    }
    ~SFixture() noexcept
    {
        completion_busy = false;
        hold_schema = hold_release = post_busy = false;
        for (std::uint32_t index = 0u; index < history_count; ++index)
        {
            const SReceived& received = history[index];
            if (received.reply.notice == EServiceNotice::prepared)
            {
                SPreparedServiceBorrow borrow;
                std::memcpy(&borrow, received.reply.payload, sizeof(borrow));
                release(borrow, received.second, (received.reply.route.value == window_route.value));
            }
        }
        TEST_EXPECT(tests, restrictions.begin_invocation(provider.instance, true));
        runtime->begin_stop(progress);
        TEST_EXPECT(tests, restrictions.end_invocation());
        for (unsigned iteration = 0u; iteration < 10000u; ++iteration)
        {
            step();
            if (runtime->stopped() && router.provider_borrows_retired(provider))
            {
                break;
            }
        }
        TEST_EXPECT(tests, runtime->stopped());
        TEST_EXPECT(tests, router.provider_borrows_retired(provider));
        TEST_EXPECT(tests, !router.failed());
    }
};

struct SUse
{
    CInputReader reader;
    SPreparedServiceBorrow borrow;
    ServiceNotificationRegistered registered;
    const SInputEndpointV4* view{ nullptr };
    std::uint64_t acquisition{ 0u };
    bool second{ false };
    unsigned boundaries{ 0u }, records{ 0u };
    std::uint32_t cancellations{ 0u };
    ~SUse() noexcept { reader.close(); }
    bool acquire(SFixture& fixture, const EConnection kind, const bool other = false,
        const SInstallationToken installation = {}, const std::uint32_t profile = 1u, const std::uint32_t context = 1u,
        const std::uint64_t device = k_default_device) noexcept
    {
        second = other;
        if (kind == EConnection::desktop)
        {
            acquisition = fixture.submit(ECommand::acquire_desktop, SAcquireDesktop{ { fixture.window() },
                { installation.value ? installation.value : 1u }, profile, context }, second);
        }
        else acquisition = fixture.submit(ECommand::acquire_controller,
            SAcquireController{ { installation.value ? installation.value : 2u }, device, profile, context }, second);
        fixture.step();
        for (std::uint32_t step = 0u; !fixture.prepared(acquisition).borrow.value && (step < 10000u); ++step) fixture.step();
        borrow = fixture.prepared(acquisition);
        TEST_EXPECT(fixture.tests, (borrow.view.descriptor != nullptr));
        if (!borrow.view.descriptor) return false;
        view = static_cast<const SInputEndpointV4*>(borrow.view.descriptor);
        TEST_EXPECT(fixture.tests, reader.install(*view, (second ? fixture.other.instance : fixture.client.instance)));
        return true;
    }
    void ready(SFixture& fixture) noexcept
    {
        registered = fixture.registration(borrow, second);
        TEST_EXPECT(fixture.tests, (registered.status == EServiceStatus::success));
        const std::uint64_t request = fixture.submit(ECommand::input_ready,
            SInputReady{ view->connection, registered.registration, { acquisition } }, second);
        fixture.step();
        TEST_EXPECT(fixture.tests, (fixture.terminal(request) == EServiceStatus::success));
    }
    void drain(SFixture& fixture) noexcept
    {
        for (unsigned iteration = 0u; iteration < 1024u; ++iteration)
        {
            SInputEvent event;
            const EInputRead result = reader.next(event, 4u);
            if (result == EInputRead::empty) return;
            if (result == EInputRead::cancelled) cancellations |= reader.take_cancellation();
            else if (result == EInputRead::boundary) ++boundaries;
            else if (result == EInputRead::record)
            {
                TEST_EXPECT(fixture.tests, (reader.validate(event) == EInputValidation::current));
                ++records;
            }
            else if ((result != EInputRead::progress) && (result != EInputRead::retry))
            {
                TEST_EXPECT(fixture.tests, false);
                return;
            }
        }
        TEST_EXPECT(fixture.tests, false);
    }
    void acknowledge(SFixture& fixture) noexcept
    {
        SBoundaryReady ready;
        TEST_EXPECT(fixture.tests, reader.boundary_ready(ready));
        (void)fixture.submit(ECommand::boundary_ready, ready, second);
        TEST_EXPECT(fixture.tests, reader.submitted(ready));
        fixture.step();
    }
    void activate(SFixture& fixture) noexcept { ready(fixture); drain(fixture); acknowledge(fixture); }
};

static SActionInputV1 action(const std::uint32_t subject = 1u) noexcept
{
    SActionInputV1 record;
    record.subject = subject;
    record.time_ns = 100u;
    record.kind = EActionKind::digital;
    record.payload[0] = 1u;
    return record;
}

static EInputPost post_action(CInputConnection& connection, const SActionInputV1& record) noexcept
{
    return SPlatformInputTestAccess::post_action(connection, record);
}

static STextInputV1 text() noexcept
{
    STextInputV1 record;
    record.time_ns = 100u;
    record.byte_count = 1u;
    record.payload[0] = 'a';
    return record;
}

void readiness_and_ordering(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::desktop)) return;
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    TEST_EXPECT(ctx, (fixture.terminal(use.acquisition) == EServiceStatus::unavailable));
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::suppressed));
    use.ready(fixture);
    use.drain(fixture);
    TEST_EXPECT(ctx, ((use.boundaries == 2u) && !use.records && !connection.snapshot().text_enabled));
    fixture.completion_busy = true;
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, ((connection.snapshot().phase == EPhase::active) && (fixture.terminal(use.acquisition) == EServiceStatus::unavailable)));
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    SInputEvent event;
    TEST_EXPECT(ctx, (use.reader.next(event) == EInputRead::record));
    TEST_EXPECT(ctx, (use.reader.validate(event) == EInputValidation::current));
    fixture.completion_busy = false;
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(use.acquisition) == EServiceStatus::success));
    const STextTarget target{ use.view->connection, connection.snapshot().transition, 123u };
    (void)fixture.submit(ECommand::enable_text, target);
    fixture.step();
    use.drain(fixture);
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, text()) == EInputPost::published));
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    std::uint64_t last = 0u;
    for (const std::uint32_t stream : { k_actions, k_text, k_actions })
    {
        TEST_EXPECT(ctx, (use.reader.next(event) == EInputRead::record));
        const std::uint64_t sequence = (stream == k_actions) ? event.action.sequence : event.text.sequence;
        TEST_EXPECT(ctx, ((event.stream == stream) && (sequence > last)));
        last = sequence;
    }
    //  Deferred application is invalidated by a new context, before its marker.
    (void)fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 2u, 0u });
    fixture.step();
    TEST_EXPECT(ctx, (use.reader.validate(event) == EInputValidation::stale));
    TEST_EXPECT(ctx, (use.reader.take_cancellation() == k_desktop));
    use.drain(fixture);
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, text()) == EInputPost::suppressed));
    const std::uint64_t stale = fixture.submit(ECommand::enable_text, target);
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(stale) == EServiceStatus::stale));
}

void pressure_and_recovery(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::desktop)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    (void)fixture.submit(ECommand::enable_text, STextTarget{ use.view->connection, connection.snapshot().transition, 10u });
    fixture.step();
    use.drain(fixture);
    use.acknowledge(fixture);
    for (unsigned index = 0u; index < k_action_capacity; ++index)
    {
        TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    }
    for (unsigned index = 0u; index < k_text_capacity; ++index)
    {
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, text()) == EInputPost::published));
    }
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::overflow));
    const std::uint64_t generation = connection.snapshot().action_generation;
    const std::uint64_t committed = connection.snapshot().committed;
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::suppressed));
    fixture.step();
    TEST_EXPECT(ctx, ((connection.snapshot().action_generation == generation) && (connection.snapshot().committed == committed) && !fixture.progress.work_pending));
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, text()) == EInputPost::overflow));
    TEST_EXPECT(ctx, (connection.snapshot().pending == k_desktop));
    TEST_EXPECT(ctx, ((connection.snapshot().action_loss == 2u) && (connection.snapshot().text_loss == 1u)));
    TEST_EXPECT(ctx, (fixture.wake.begin_cycle().code == platform::threading::EWaitError::none));
    use.drain(fixture);
    platform::threading::CWaitDeadline deadline;
    TEST_EXPECT(ctx, (deadline.set_after_nanoseconds(0u).code == platform::threading::EWaitError::none));
    TEST_EXPECT(ctx, (fixture.wake.wait(deadline).status == platform::threading::EOwnerWaitStatus::activity));
    TEST_EXPECT(ctx, (!use.records && (use.cancellations == k_desktop)));
    fixture.step();
    use.drain(fixture);
    SBoundaryReady old;
    TEST_EXPECT(ctx, use.reader.boundary_ready(old));
    //  A new context supersedes both just-observed markers; old ack has no effect.
    (void)fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 2u, 0u });
    const std::uint64_t stale = fixture.submit(ECommand::boundary_ready, old);
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(stale) == EServiceStatus::stale) && (connection.snapshot().pending == k_desktop)));
    use.drain(fixture);
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    use.drain(fixture);
    TEST_EXPECT(ctx, (use.records == 1u));
}

void independent_streams_and_validation(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse desktop, controller;
    if (!desktop.acquire(fixture, EConnection::desktop) || !controller.acquire(fixture, EConnection::controller, true))
    {
        return;
    }
    desktop.activate(fixture);
    controller.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    CInputConnection& independent = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller);
    (void)fixture.submit(ECommand::enable_text, STextTarget{ desktop.view->connection, connection.snapshot().transition, 5u });
    fixture.step();
    desktop.drain(fixture);
    desktop.acknowledge(fixture);
    const std::uint64_t action_generation = connection.snapshot().action_generation;
    for (unsigned index = 0u; index < k_text_capacity; ++index) TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, text()) == EInputPost::published));
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, text()) == EInputPost::overflow));
    TEST_EXPECT(ctx, ((connection.snapshot().pending == k_text) && (connection.snapshot().action_generation == action_generation)));
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    TEST_EXPECT(ctx, (post_action(independent, action()) == EInputPost::published));
    desktop.drain(fixture);
    controller.drain(fixture);
    TEST_EXPECT(ctx, ((desktop.records == 1u) && (controller.records == 1u)));
    fixture.step();
    desktop.drain(fixture);
    desktop.acknowledge(fixture);
    const std::uint64_t text_generation = connection.snapshot().text_generation;
    TEST_EXPECT(ctx, desktop.reader.cancel_application(k_text));
    (void)fixture.submit(ECommand::enable_text, STextTarget{ desktop.view->connection, connection.snapshot().transition, 5u });
    fixture.step();
    TEST_EXPECT(ctx, (connection.snapshot().text_generation == (text_generation + 1u)));
    desktop.drain(fixture);
    SBoundaryReady target_ready;
    TEST_EXPECT(ctx, desktop.reader.boundary_ready(target_ready));
    desktop.acknowledge(fixture);
    TEST_EXPECT(ctx, desktop.reader.resume_application(target_ready, k_text));
    STextInputV1 invalid = text();
    invalid.byte_count = 3u;
    invalid.payload[0] = 0xedu; invalid.payload[1] = 0xa0u; invalid.payload[2] = 0x80u;
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, invalid) == EInputPost::invalid));
    invalid.payload[0] = 0xe0u; invalid.payload[1] = 0x80u;
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::post_text(connection, invalid) == EInputPost::invalid));
    TEST_EXPECT(ctx, (post_action(connection, action(999u)) == EInputPost::invalid));
    const std::uint64_t foreign = fixture.submit(ECommand::select_context, SSelectContext{ desktop.view->connection, 2u, 0u }, true);
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(foreign) == EServiceStatus::invalid_request));
    SPlatformInputTestAccess::exhaust_sequence(independent);
    TEST_EXPECT(ctx, (post_action(independent, action()) == EInputPost::failed));
    SInputEvent event;
    TEST_EXPECT(ctx, (controller.reader.next(event) == EInputRead::failed));
}

void deadlines_admission_and_retirement(TTestContext& ctx)
{
    {
        SFixture fixture{ ctx };
        const std::uint64_t invalid = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 0u }, 0u, 1u, 1u });
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(invalid) == EServiceStatus::invalid_request));
        SPlatformInputTestAccess::saturate_acquisitions(*fixture.runtime, true);
        const std::uint64_t busy = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, 0u, 1u, 1u });
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(busy) == EServiceStatus::busy));
        SPlatformInputTestAccess::saturate_acquisitions(*fixture.runtime, false);
        fixture.controllers.attach(*fixture.runtime, 0u, 17u);
        const std::uint64_t device = SPlatformInputTestAccess::device(*fixture.runtime, 17u);
        SUse use;
        if (!use.acquire(fixture, EConnection::controller))
        {
            return;
        }
        const std::uint64_t duplicate = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, device, 1u, 1u });
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(duplicate) == EServiceStatus::occupied));
        fixture.clock.time += 4000000000u;
        use.ready(fixture);
        use.drain(fixture);
        //  Initial ack shares the original budget; InputReady does not restart it.
        fixture.clock.time += 1000000001u;
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(use.acquisition) == EServiceStatus::timed_out));
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller) != nullptr));
        use.reader.close();
        fixture.release(use.borrow);
        fixture.step(); fixture.step();
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller) == nullptr));
    }
    {
        SFixture fixture{ ctx };
        SUse use;
        if (!use.acquire(fixture, EConnection::controller))
        {
            return;
        }
        use.activate(fixture);
        use.reader.close();
        fixture.client.phase = host::EClientPhase::stopped;
        fixture.client.terminal_before_drain = fixture.client.incoming_drained = true;
        fixture.router.retire_client(fixture.client);
        fixture.step(); fixture.step(); fixture.step();
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller) == nullptr));
    }
    {
        SFixture fixture{ ctx };
        SUse desktop, controller;
        if (!desktop.acquire(fixture, EConnection::desktop) || !controller.acquire(fixture, EConnection::controller, true))
        {
            return;
        }
        desktop.activate(fixture);
        controller.activate(fixture);
        //  Relinquish every window view; desktop's own dependency still protects it.
        for (std::uint32_t index = 0u; index < fixture.history_count; ++index)
        {
            const ServiceReply& reply = fixture.history[index].reply;
            if ((reply.route.value == fixture.window_route.value) && (reply.notice == EServiceNotice::prepared))
            {
                SPreparedServiceBorrow borrow;
                std::memcpy(&borrow, reply.payload, sizeof(borrow));
                fixture.release(borrow, false, true);
            }
        }
        fixture.step(); fixture.step();
        TEST_EXPECT(ctx, !fixture.native.destroyed);
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop)->snapshot().phase == EPhase::closing));
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller)->snapshot().phase == EPhase::active));
        desktop.reader.close();
        fixture.release(desktop.borrow);
        fixture.runtime->enter_callback();
        fixture.step();
        TEST_EXPECT(ctx, !fixture.native.destroyed);
        fixture.runtime->leave_callback();
        fixture.step(); fixture.step();
        TEST_EXPECT(ctx, (fixture.native.destroyed == 1u));
    }
}

void copied_marker_and_bounded_snapshot(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::controller))
    {
        return;
    }
    use.ready(fixture);
    //  Deterministically model a marker copied before observing its generation.
    SInputReaderTestAccess::copy_action(use.reader);
    use.drain(fixture);
    TEST_EXPECT(ctx, (use.boundaries == 1u));
    use.acknowledge(fixture);
    CInputPublication publication;
    SInputSnapshot state;
    state.action_generation = UINT64_MAX - 7u;
    state.text_loss = 0x123456789abcdef0u;
    state.capture_grant = 0x1234567812345678u;
    state.capture_revision = 0x8765432187654321u;
    state.capture_revocations = 0xabcdef1234567890u;
    state.capture_reason = ECaptureReason::lost;
    state.ingress_loss = 0x1020304050607080u;
    state.ingress_pending = k_text;
    TEST_EXPECT(ctx, publication.publish(state));
    SInputSnapshot observed;
    TEST_EXPECT(ctx, ((publication.read(observed) == EStateRead::success) &&
        (observed.action_generation == state.action_generation) && (observed.text_loss == state.text_loss) &&
        (observed.capture_grant == state.capture_grant) && (observed.capture_revision == state.capture_revision) &&
        (observed.capture_revocations == state.capture_revocations) && (observed.capture_reason == ECaptureReason::lost) &&
        (observed.ingress_loss == state.ingress_loss) && (observed.ingress_pending == k_text)));
    SInputPublicationTestAccess::revision(publication, 3u);
    TEST_EXPECT(ctx, (publication.read(observed) == EStateRead::retry));
    SInputPublicationTestAccess::revision(publication, 0xfffffffcu);
    TEST_EXPECT(ctx, !publication.publish(state));
    TEST_EXPECT(ctx, ((publication.read(observed) == EStateRead::failed) && (observed.phase == EPhase::closing)));
    TEST_EXPECT(ctx, !publication.publish(state));
}

void transition_serialisation(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::desktop)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    const std::uint64_t generation = connection.snapshot().action_generation;
    TEST_EXPECT(ctx, use.reader.cancel_application(k_desktop));
    SInputEvent cancelled_event;
    TEST_EXPECT(ctx, (use.reader.next(cancelled_event) == EInputRead::cancelled));
    TEST_EXPECT(ctx, (use.reader.take_cancellation() == k_desktop));
    const std::uint64_t rejected = fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 999u, 0u });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(rejected) == EServiceStatus::invalid_request));
    TEST_EXPECT(ctx, (use.reader.next(cancelled_event) == EInputRead::empty));
    const std::uint64_t transition = fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 1u, 0u });
    fixture.step();
    TEST_EXPECT(ctx, ((connection.snapshot().transition == transition) && (connection.snapshot().action_generation == (generation + 1u))));
    const std::uint64_t busy = fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 2u, 0u });
    const std::uint64_t text_busy = fixture.submit(ECommand::enable_text, STextTarget{ use.view->connection, transition, 5u });
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(busy) == EServiceStatus::busy) && (fixture.terminal(text_busy) == EServiceStatus::busy)));
    use.drain(fixture);
    SBoundaryReady ready, repeat;
    TEST_EXPECT(ctx, use.reader.boundary_ready(ready));
    TEST_EXPECT(ctx, use.reader.submitted(ready));
    TEST_EXPECT(ctx, !use.reader.boundary_ready(repeat));
    use.reader.retry_boundary(ready);
    TEST_EXPECT(ctx, use.reader.boundary_ready(repeat));
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, use.reader.resume_application(ready, k_desktop));
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    TEST_EXPECT(ctx, (post_action(connection, action(2u)) == EInputPost::published));
    //  Terminal revision publication still notifies the registered consumer.
    use.registered.reader.clear_pending();
    SInputPublicationTestAccess::revision(SPlatformInputTestAccess::publication(connection), 0xfffffffcu);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::failed));
    TEST_EXPECT(ctx, use.registered.reader.pending());
}

void eligible_pause_cancel_and_replacement(TTestContext& ctx)
{
    {
        SFixture fixture{ ctx };
        SUse use;
        if (!use.acquire(fixture, EConnection::controller))
        {
            return;
        }
        TEST_EXPECT(ctx, fixture.restrictions.begin_invocation(fixture.provider.instance, true));
        const modules::SRestrictionEnterResult scope = fixture.restrictions.enter({ 3u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        fixture.clock.time += 10000000000u;
        TEST_EXPECT(ctx, (fixture.restrictions.leave(scope.scope) == modules::ERestrictionStatus::success));
        TEST_EXPECT(ctx, fixture.restrictions.end_invocation());
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(use.acquisition) == EServiceStatus::unavailable));
        use.activate(fixture);
        const SConnectionToken old = use.view->connection;
        use.reader.close();
        fixture.release(use.borrow);
        fixture.step(); fixture.step();
        SUse replacement;
        if (!replacement.acquire(fixture, EConnection::controller, true))
        {
            return;
        }
        TEST_EXPECT(ctx, (replacement.view->connection.value != old.value));
        const std::uint64_t stale = fixture.submit(ECommand::boundary_ready, SBoundaryReady{ old, 1u, 0u }, true);
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(stale) == EServiceStatus::stale));
        replacement.activate(fixture);
    }
    {
        SFixture fixture{ ctx };
        CServiceBorrowClient consumer;
        const SServiceRequestId request{ fixture.sequence };
        TEST_EXPECT(ctx, consumer.begin(fixture.route, request, static_cast<std::uint16_t>(ECommand::acquire_controller)));
        (void)fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, 0u, 1u, 1u });
        //  Consume the acknowledgement, then cancel before exposing prepared data.
        ServiceReply acknowledged;
        TEST_EXPECT(ctx, fixture.client.messages[0].copy_payload_to(acknowledged));
        TEST_EXPECT(ctx, consumer.observe(acknowledged));
        CancelServiceRequest cancel;
        TEST_EXPECT(ctx, consumer.cancel(fixture.route, request, cancel));
        fixture.step();
        for (std::uint32_t index = 0u; index < fixture.history_count; ++index)
        {
            const ServiceReply& reply = fixture.history[index].reply;
            if ((reply.request.value == request.value) && (reply.notice == EServiceNotice::prepared))
            {
                TEST_EXPECT(ctx, consumer.observe(reply));
            }
        }
        const SServiceBorrow borrow = consumer.borrow(fixture.route, request);
        TEST_EXPECT(ctx, (borrow.value && !consumer.view(borrow).descriptor));
        fixture.router.cancel_request(fixture.client, cancel);
        ReleaseServiceBorrow release;
        ServiceBorrowQuiescent quiet;
        TEST_EXPECT(ctx, consumer.release(borrow, release));
        TEST_EXPECT(ctx, consumer.quiesce(borrow, quiet));
        fixture.router.release_borrow(fixture.client, release);
        fixture.router.quiesce_borrow(fixture.client, quiet);
        fixture.step(); fixture.step();
        TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller) == nullptr));
    }
}

void notification_failure_and_closure(TTestContext& ctx)
{
    threading::CWaitPredicate predicate;
    threading::CNotification notification;
    TEST_EXPECT(ctx, predicate.acquire_control());
    TEST_EXPECT(ctx, notification.initialise(predicate));
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::controller)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller);
    connection.notification = notification.endpoint();
    predicate.release_control();
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::failed));
    TEST_EXPECT(ctx, (connection.view.actions->readable_count() == 1u));
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::closed));
    TEST_EXPECT(ctx, (connection.view.actions->readable_count() == 1u));
    SInputEvent event;
    TEST_EXPECT(ctx, (use.reader.next(event) == EInputRead::failed));
    use.reader.close();
    fixture.release(use.borrow);
    fixture.step(); fixture.step();
    TEST_EXPECT(ctx, (SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller) == nullptr));
}

void concurrent_merge_and_recovery(TTestContext& ctx, const bool defer_consumer)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::desktop)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    (void)fixture.submit(ECommand::enable_text, STextTarget{ use.view->connection, connection.snapshot().transition, 10u });
    fixture.step();
    use.drain(fixture);
    use.acknowledge(fixture);
    std::atomic<bool> done{ false }, finished{ false }, failed{ false }, ack_pending{ false };
    std::atomic<bool> consumer_released{ !defer_consumer };
    SBoundaryReady acknowledgement;
    constexpr std::uint32_t k_stress_records = 20000u;
    std::uint32_t applied = 0u;
    std::uint64_t last_applied = 0u;
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread consumer{ [&]() noexcept
    {
        //  The delayed case forces both streams to overflow before any read.
        //  Recovery may then discard the entire burst, regardless of scheduler.
        while (!consumer_released.load(std::memory_order_acquire) && (std::chrono::steady_clock::now() < end))
        {
            std::this_thread::yield();
        }
        while (std::chrono::steady_clock::now() < end)
        {
            const bool producer_done = done.load(std::memory_order_acquire);
            SInputEvent event;
            const EInputRead result = use.reader.next(event, 4u);
            if (result == EInputRead::cancelled) (void)use.reader.take_cancellation();
            else if (result == EInputRead::record)
            {
                const std::uint64_t sequence = (event.stream == k_actions) ? event.action.sequence : event.text.sequence;
                if (sequence <= last_applied) failed.store(true);
                last_applied = sequence;
                ++applied;
            }
            else if ((result == EInputRead::failed) || (result == EInputRead::closing))
            {
                failed.store(true);
                break;
            }
            if (!ack_pending.load(std::memory_order_acquire))
            {
                SBoundaryReady ready;
                if (use.reader.boundary_ready(ready))
                {
                    acknowledgement = ready;
                    if (!use.reader.submitted(ready)) failed.store(true);
                    ack_pending.store(true, std::memory_order_release);
                }
            }
            if (producer_done && (result == EInputRead::empty) && !use.reader.snapshot().pending && !ack_pending.load())
            {
                finished.store(true);
                return;
            }
            std::this_thread::yield();
        }
        failed.store(true);
        finished.store(true);
    } };
    std::uint32_t produced = 0u;
    bool sentinel_committed = false;
    std::uint64_t sentinel_sequence = 0u;
    while (!finished.load() && (std::chrono::steady_clock::now() < end))
    {
        if (ack_pending.load(std::memory_order_acquire))
        {
            const EServiceStatus status = connection.acknowledge(acknowledgement);
            if ((status != EServiceStatus::success) && (status != EServiceStatus::stale)) failed.store(true);
            ack_pending.store(false, std::memory_order_release);
        }
        if (produced < k_stress_records)
        {
            const EInputPost result = (produced & 1u) ? SPlatformInputTestAccess::post_text(connection, text()) : post_action(connection, action());
            if ((result == EInputPost::invalid) || (result == EInputPost::closed) || (result == EInputPost::failed)) failed.store(true);
            ++produced;
            if (produced == k_stress_records) consumer_released.store(true, std::memory_order_release);
        }
        else if (!sentinel_committed && !connection.snapshot().pending)
        {
            //  Yield does not guarantee consumer progress: recovery may legally
            //  discard all earlier records. Require one fresh, readable record
            //  after the last overflow before asking the consumer to finish.
            const EInputPost result = post_action(connection, action());
            sentinel_committed = result == EInputPost::published;
            if (sentinel_committed)
            {
                sentinel_sequence = connection.snapshot().committed;
                done.store(true);
            }
            else if ((result == EInputPost::invalid) || (result == EInputPost::closed) || (result == EInputPost::failed))
            {
                failed.store(true);
                done.store(true);
                break;
            }
        }
        connection.progress(100u);
        std::this_thread::yield();
    }
    consumer.join();
    if (failed.load() || !finished.load() || !sentinel_committed || !applied || (last_applied != sentinel_sequence))
    {
        std::printf("Concurrent input: deferred=%u produced=%u applied=%u sentinel=%u failed=%u finished=%u pending=%u ack=%u last=%llu expected=%llu\n",
            static_cast<unsigned>(defer_consumer), produced, applied, static_cast<unsigned>(sentinel_committed),
            static_cast<unsigned>(failed.load()), static_cast<unsigned>(finished.load()), connection.snapshot().pending,
            static_cast<unsigned>(ack_pending.load()), static_cast<unsigned long long>(last_applied),
            static_cast<unsigned long long>(sentinel_sequence));
    }
    TEST_EXPECT(ctx, (!failed.load() && finished.load() && sentinel_committed &&
        (produced == k_stress_records) && (applied > 0u) && (last_applied == sentinel_sequence)));
    if (defer_consumer)
    {
        //  Without the sentinel this valid execution has applied == 0, which
        //  violated the original test's scheduler-dependent assertion.
        TEST_EXPECT(ctx, ((applied == 1u) && connection.snapshot().action_loss && connection.snapshot().text_loss));
    }
}

void local_cancellation_survives_overflow(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::controller)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller);
    TEST_EXPECT(ctx, use.reader.cancel_application(k_actions));
    for (unsigned index = 0u; index < k_action_capacity; ++index)
    {
        TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    }
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::overflow));
    use.drain(fixture);
    fixture.step();
    use.drain(fixture);
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    use.drain(fixture);
    TEST_EXPECT(ctx, !use.records);
    (void)fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 1u, 0u });
    fixture.step();
    use.drain(fixture);
    SBoundaryReady ready;
    TEST_EXPECT(ctx, use.reader.boundary_ready(ready));
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, use.reader.resume_application(ready, k_actions));
    //  Transport publication does not carry physical rearming policy.
    SActionInputV1 release = action();
    release.payload[0] = 0u;
    TEST_EXPECT(ctx, (post_action(connection, release) == EInputPost::published));
    (void)fixture.submit(ECommand::select_context, SSelectContext{ use.view->connection, 2u, 0u });
    fixture.step();
    use.drain(fixture);
    use.acknowledge(fixture);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    use.drain(fixture);
    TEST_EXPECT(ctx, (use.records == 1u));
}

SInstallationToken installation_token(const SFixture& fixture, const std::uint64_t request)
{
    for (std::uint32_t index = 0u; index < fixture.history_count; ++index)
    {
        const ServiceReply& reply = fixture.history[index].reply;
        if ((reply.request.value == request) && (reply.notice == EServiceNotice::completed) &&
            (reply.status == EServiceStatus::success) && (reply.payload_size == sizeof(SInstallationResult)))
        {
            SInstallationResult result;
            std::memcpy(&result, reply.payload, sizeof(result));
            return result.installation;
        }
    }
    return {};
}

void mapper_runtime_integration(TTestContext& ctx)
{
    using platform::runtime::EInputAdmission;
    using platform::runtime::EObservationKind;
    using platform::runtime::SInputObservation;
    SFixture fixture{ ctx, false };
    const CAssetId source = fixture.schema_source(1500u);
    const std::string held = R"("held":{"declaration":{"id":7,"kind":"digital_state"}})";
    const std::size_t action = fixture.source.find(held);
    TEST_EXPECT(ctx, (action != std::string::npos));
    fixture.source.replace(action, held.size(), R"("held":{"declaration":{"id":7,"kind":"pulse"}})");
    const std::string regular = R"("activation":"regular")";
    for (std::size_t position = fixture.source.find(regular); position != std::string::npos; position = fixture.source.find(regular))
    {
        fixture.source.replace(position, regular.size(), R"("activation":"start_press")");
    }
    const std::uint64_t install = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    for (unsigned step = 0u; !installation_token(fixture, install).value && (step < 20000u); ++step) fixture.step();
    const SInstallationToken token = installation_token(fixture, install);
    TEST_EXPECT(ctx, token.value);
    const std::uint64_t window = fixture.window();
    const std::uint64_t acquisition = fixture.submit(ECommand::acquire_desktop, SAcquireDesktop{ { window }, token, 3u, 4u });
    fixture.step();
    CInputConnection* connection = SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    TEST_EXPECT(ctx, (connection && !connection->mapper().prepared() && !fixture.prepared(acquisition).borrow.value));
    const std::uint64_t busy = fixture.submit(ECommand::retire_configuration, SRetireConfiguration{ token });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(busy) == EServiceStatus::busy));
    SUse desktop, controller;
    desktop.acquisition = acquisition;
    for (unsigned step = 0u; !fixture.prepared(acquisition).borrow.value && (step < 16u); ++step) fixture.step();
    desktop.borrow = fixture.prepared(acquisition);
    TEST_EXPECT(ctx, desktop.borrow.view.descriptor);
    if (!desktop.borrow.view.descriptor) return;
    desktop.view = static_cast<const SInputEndpointV4*>(desktop.borrow.view.descriptor);
    TEST_EXPECT(ctx, desktop.reader.install(*desktop.view, fixture.client.instance));
    desktop.activate(fixture);
    if (!controller.acquire(fixture, EConnection::controller, false, token, 8u, 4u)) return;
    controller.activate(fixture);
    CInputConnection& pad = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller);
    const auto settle = [&]() noexcept
    {
        for (unsigned step = 0u; (connection->mapper().pending() || pad.mapper().pending()) && (step < 100u); ++step)
        {
            fixture.step();
        }
        TEST_EXPECT(ctx, (!connection->mapper().pending() && !pad.mapper().pending()));
    };
    SInputObservation key, stick;
    key.device = 11u;
    stick.device = 22u;
    fixture.runtime->input_device(controller.view->connection, stick.device);
    fixture.step();
    controller.drain(fixture);
    controller.acknowledge(fixture);
    key.ordinal = stick.ordinal = 1u;
    key.time_ns = stick.time_ns = 100u;
    key.baseline = stick.baseline = true;
    key.kind = stick.kind = EObservationKind::snapshot;
    key.count = stick.count = 1u;
    key.controls[0].control = 1u;
    stick.controls[0].control = 270u;
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::accepted));
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(controller.view->connection, stick) == EInputAdmission::accepted));
    fixture.step();
    TEST_EXPECT(ctx, (connection->mapper().pending() && !pad.mapper().pending() && fixture.progress.work_pending));
    settle();
    key.baseline = stick.baseline = false;
    ++key.ordinal;
    ++stick.ordinal;
    key.controls[0].x = stick.controls[0].x = 1.0f;
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::accepted));
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(controller.view->connection, stick) == EInputAdmission::accepted));
    fixture.step();
    desktop.drain(fixture);
    controller.drain(fixture);
    TEST_EXPECT(ctx, ((controller.records == 1u) && (desktop.records > 0u) && ((desktop.records + controller.records) <= 64u)));
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::busy));
    TEST_EXPECT(ctx, fixture.restrictions.begin_invocation(fixture.provider.instance, true));
    fixture.runtime->enter_modal(false);
    const std::uint64_t desktop_generation = connection->snapshot().action_generation;
    const std::uint64_t controller_generation = pad.snapshot().action_generation;
    fixture.runtime->enter_modal(true);
    fixture.runtime->leave_modal(false);
    fixture.runtime->input_focus(false);
    fixture.runtime->leave_modal(true);
    TEST_EXPECT(ctx, ((connection->snapshot().action_generation == desktop_generation) &&
        (pad.snapshot().action_generation == controller_generation)));
    TEST_EXPECT(ctx, fixture.restrictions.end_invocation());
    fixture.step();
    desktop.drain(fixture);
    controller.drain(fixture);
    desktop.acknowledge(fixture);
    controller.acknowledge(fixture);
    TEST_EXPECT(ctx, (!connection->mapper().pending() && !pad.mapper().pending()));
    const unsigned desktop_before = desktop.records, pad_before = controller.records;
    for (const float value : { 0.0f, 1.0f })
    {
        key.controls[0].x = stick.controls[0].x = value;
        ++key.ordinal;
        ++stick.ordinal;
        TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::accepted));
        TEST_EXPECT(ctx, (fixture.runtime->input_observation(controller.view->connection, stick) == EInputAdmission::accepted));
        settle();
    }
    fixture.runtime->input_focus(true);
    ++key.ordinal;
    ++stick.ordinal;
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::accepted));
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(controller.view->connection, stick) == EInputAdmission::accepted));
    settle();
    desktop.drain(fixture);
    controller.drain(fixture);
    TEST_EXPECT(ctx, ((desktop.records == desktop_before) && (controller.records == pad_before)));
    key.controls[0].x = 0.0f;
    ++key.ordinal;
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::accepted));
    settle();
    key.controls[0].x = 1.0f;
    ++key.ordinal;
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::accepted));
    const std::uint64_t committed = connection->snapshot().committed;
    fixture.native.fail_dispatch = true;
    SPlatformInputTestAccess::late_dispatch(*fixture.runtime);
    fixture.step();
    fixture.native.fail_dispatch = false;
    TEST_EXPECT(ctx, ((connection->snapshot().phase == EPhase::closing) &&
        (fixture.native.committed_at_failure >= committed) &&
        (connection->snapshot().committed == fixture.native.committed_at_failure)));
    TEST_EXPECT(ctx, (fixture.runtime->input_observation(desktop.view->connection, key) == EInputAdmission::closed));
}

void installation_lifecycle(TTestContext& ctx)
{
    SFixture fixture{ ctx, false };
    const CAssetId source = fixture.schema_source(300u);
    const std::uint64_t request = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    fixture.step();
    const std::uint64_t competing = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(competing) == EServiceStatus::busy));
    TEST_EXPECT(ctx, (!fixture.host_request_count && !fixture.schema_releases));
    TEST_EXPECT(ctx, !installation_token(fixture, request).value);
    fixture.hold_release = true;
    for (unsigned iteration = 0u; !fixture.host_request_count && (iteration < 10000u); ++iteration) fixture.step();
    TEST_EXPECT(ctx, (fixture.host_request_count == 1u));
    TEST_EXPECT(ctx, !installation_token(fixture, request).value);
    fixture.hold_release = false;
    fixture.completion_busy = true;
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.schema_releases == 1u) && !installation_token(fixture, request).value));
    fixture.completion_busy = false;
    fixture.step();
    const SInstallationToken token = installation_token(fixture, request);
    TEST_EXPECT(ctx, token.value);
    TEST_EXPECT(ctx, (fixture.terminal(request) == EServiceStatus::success));
    //  Cancellation after the durable commit cannot revoke the published resource.
    fixture.router.cancel_request(fixture.client, { fixture.route, { request } });
    fixture.step();
    const std::uint64_t acquire = fixture.submit(ECommand::acquire_controller, SAcquireController{ token, 0u, 8u, 4u }, true);
    fixture.step();
    const SPreparedServiceBorrow borrowed = fixture.prepared(acquire);
    TEST_EXPECT(ctx, borrowed.view.descriptor);
    const std::uint64_t busy = fixture.submit(ECommand::retire_configuration, SRetireConfiguration{ token });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(busy) == EServiceStatus::busy));
    fixture.release(borrowed, true);
    fixture.step();
    fixture.step();
    const std::uint64_t retire = fixture.submit(ECommand::retire_configuration, SRetireConfiguration{ token }, true);
    fixture.step();
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(retire) == EServiceStatus::success));
    const std::uint64_t stale = fixture.submit(ECommand::acquire_controller, SAcquireController{ token, 0u, 8u, 4u });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(stale) == EServiceStatus::invalid_request));
}

void installation_admission_and_owner(TTestContext& ctx)
{
    SFixture fixture{ ctx, false };
    const CAssetId source = fixture.schema_source();
    SInstallationToken first;
    for (unsigned index = 0u; index < 8u; ++index)
    {
        const std::uint64_t request = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
        for (unsigned iteration = 0u; (iteration < 10000u) && !installation_token(fixture, request).value; ++iteration) fixture.step();
        const SInstallationToken token = installation_token(fixture, request);
        TEST_EXPECT(ctx, token.value);
        if (!index) first = token;
    }
    const std::uint64_t busy = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(busy) == EServiceStatus::busy));
    fixture.client.phase = host::EClientPhase::stopped;
    fixture.router.retire_client(fixture.client);
    fixture.step();
    const std::uint64_t retire = fixture.submit(ECommand::retire_configuration, SRetireConfiguration{ first }, true);
    fixture.step();
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(retire) == EServiceStatus::success));
}

void installation_conditioning_failure(TTestContext& ctx)
{
    for (const EAssetStatus status : { EAssetStatus::allocation_failed, EAssetStatus::conditioning_failed })
    {
        SFixture fixture{ ctx, false };
        const CAssetId source = fixture.schema_source();
        fixture.schema_status = status;
        const std::uint64_t request = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
        for (unsigned step = 0u; step < 4u; ++step) fixture.step();
        const EServiceStatus expected = (status == EAssetStatus::allocation_failed) ? EServiceStatus::allocation_failed : EServiceStatus::invalid_request;
        TEST_EXPECT(ctx, (fixture.terminal(request) == expected));
        TEST_EXPECT(ctx, (!fixture.schema_releases && !installation_token(fixture, request).value));
    }
}

void installation_cancellation(TTestContext& ctx)
{
    for (unsigned phase = 0u; phase < 4u; ++phase)
    {
        SFixture fixture{ ctx, false };
        const CAssetId source = fixture.schema_source(100u);
        fixture.hold_schema = (phase == 0u);
        fixture.hold_release = (phase == 2u);
        fixture.completion_busy = (phase == 3u);
        const std::uint64_t request = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
        fixture.step();
        if (phase >= 1u) fixture.step();
        if (phase >= 2u)
        {
            for (unsigned iteration = 0u; (iteration < 10000u) &&
                (fixture.hold_release ? !fixture.host_request_count : !fixture.schema_releases); ++iteration)
            {
                fixture.step();
            }
        }
        fixture.router.cancel_request(fixture.client, { fixture.route, { request } });
        fixture.completion_busy = false;
        fixture.step();
        fixture.hold_schema = fixture.hold_release = false;
        for (unsigned iteration = 0u; iteration < 1000u; ++iteration) fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(request) == EServiceStatus::cancelled));
        TEST_EXPECT(ctx, (!installation_token(fixture, request).value && (fixture.schema_requests == 1u) && (fixture.schema_releases == 1u)));
        const std::uint64_t retry = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
        for (unsigned iteration = 0u; (iteration < 10000u) && !installation_token(fixture, retry).value; ++iteration) fixture.step();
        TEST_EXPECT(ctx, installation_token(fixture, retry).value);
    }
    SFixture fixture{ ctx, false };
    const CAssetId source = fixture.schema_source();
    fixture.post_busy = true;
    const std::uint64_t request = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    fixture.step();
    fixture.clock.time += 5000000000u;
    fixture.step();
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(request) == EServiceStatus::timed_out) && !fixture.schema_requests));
}

void capture_authority(TTestContext& ctx)
{
    SFixture fixture{ ctx, true, SNative::desktop_adapter() };
    SUse desktop;
    TEST_EXPECT(ctx, desktop.acquire(fixture, EConnection::desktop));
    desktop.activate(fixture);
    for (std::uint32_t step = 0u; step < 12u; ++step) fixture.step();
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    TEST_EXPECT(ctx, !connection.snapshot().ingress_pending);
    const SSetCapture acquire{ desktop.view->connection, connection.snapshot().transition, connection.snapshot().action_generation };
    const std::uint64_t grant = fixture.submit(ECommand::set_capture, acquire);
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(grant) == EServiceStatus::success) && fixture.native.captured &&
        (connection.snapshot().capture_grant == grant)));
    const std::uint64_t stale = fixture.submit(ECommand::release_capture,
        SReleaseCapture{ desktop.view->connection, acquire.transition, grant + 1u });
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(stale) == EServiceStatus::stale) && fixture.native.captured));
    const std::uint64_t text_generation = connection.snapshot().text_generation;
    fixture.native.captured = false;
    fixture.runtime->desktop_capture_lost();
    TEST_EXPECT(ctx, (!connection.snapshot().capture_grant && (connection.snapshot().capture_revocations == 1u) &&
        (connection.snapshot().capture_reason == ECaptureReason::lost) &&
        (connection.snapshot().text_generation == text_generation) &&
        (connection.snapshot().pending == k_actions)));
    desktop.drain(fixture);
    fixture.step();
    desktop.drain(fixture);
    desktop.acknowledge(fixture);
    for (std::uint32_t step = 0u; step < 12u; ++step) fixture.step();
    const std::uint64_t old = fixture.submit(ECommand::set_capture, acquire);
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(old) == EServiceStatus::stale));
    fixture.native.revoke_during_capture = true;
    const std::uint64_t reentrant = fixture.submit(ECommand::set_capture,
        SSetCapture{ desktop.view->connection, connection.snapshot().transition, connection.snapshot().action_generation });
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(reentrant) == EServiceStatus::stale) &&
        !connection.snapshot().capture_grant && !fixture.native.captured));
}

void capture_failure_cleanup(TTestContext& ctx)
{
    for (const bool release_failure : { false, true })
    {
        SFixture fixture{ ctx, true, SNative::desktop_adapter() };
        SUse desktop;
        TEST_EXPECT(ctx, desktop.acquire(fixture, EConnection::desktop));
        desktop.activate(fixture);
        for (std::uint32_t step = 0u; step < 12u; ++step) fixture.step();
        CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
        if (!release_failure) SInputPublicationTestAccess::revision(SPlatformInputTestAccess::publication(connection), 0xfffffffeu);
        const std::uint64_t grant = fixture.submit(ECommand::set_capture,
            SSetCapture{ desktop.view->connection, connection.snapshot().transition, connection.snapshot().action_generation });
        fixture.step();
        if (!release_failure)
        {
            TEST_EXPECT(ctx, ((fixture.terminal(grant) == EServiceStatus::delivery_failed) &&
                !fixture.native.captured && !connection.snapshot().capture_grant));
        }
        else
        {
            TEST_EXPECT(ctx, (fixture.terminal(grant) == EServiceStatus::success));
            fixture.native.fail_release = true;
            desktop.reader.close();
            fixture.release(desktop.borrow);
            fixture.step();
            TEST_EXPECT(ctx, (fixture.native.captured && SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop)));
            fixture.native.fail_release = false;
            for (std::uint32_t step = 0u; step < 12u; ++step) fixture.step();
            TEST_EXPECT(ctx, (!fixture.native.captured && !SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop)));
        }
    }
}

struct SCatalogue
{
    SPreparedServiceBorrow borrow;
    const SControllerCatalogueV1* view{};
    bool acquire(SFixture& fixture)
    {
        const std::uint64_t request = fixture.submit(static_cast<std::uint16_t>(ECommand::acquire_catalogue), nullptr, 0u);
        for (unsigned step = 0u; !fixture.prepared(request).borrow.value && (step < 16u); ++step) fixture.step();
        borrow = fixture.prepared(request);
        if (!borrow.view.descriptor) return false;
        view = static_cast<const SControllerCatalogueV1*>(borrow.view.descriptor);
        const ServiceNotificationRegistered registered = fixture.registration(borrow);
        (void)fixture.submit(ECommand::catalogue_ready, SCatalogueReady{ borrow.borrow, registered.registration, { request } });
        fixture.step();
        TEST_EXPECT(fixture.tests, (fixture.terminal(request) == EServiceStatus::success));
        return true;
    }
};

static void controller_settle(SFixture& fixture, SUse& use)
{
    for (unsigned step = 0u; step < 12u; ++step) fixture.step();
    use.drain(fixture);
    SBoundaryReady ready;
    if (use.reader.boundary_ready(ready)) use.acknowledge(fixture);
    for (unsigned step = 0u; step < 12u; ++step) fixture.step();
}

void controller_catalogue_and_claims(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SCatalogue catalogue;
    TEST_EXPECT(ctx, catalogue.acquire(fixture));
    if (!catalogue.view) return;
    SCatalogueSnapshot empty;
    TEST_EXPECT(ctx, (catalogue.view->state->read(empty) == EStateRead::success));
    TEST_EXPECT(ctx, ((empty.backend == EControllerBackend::ready) && !empty.count &&
        (catalogue.view->pipe_capacity == 4u) && (catalogue.view->device_capacity == 16u)));
    SUse first, second, third, fourth;
    if (!first.acquire(fixture, EConnection::controller))
    {
        return;
    }
    first.activate(fixture);
    SInputEndpointV4 wrong_clock = *first.view;
    wrong_clock.clock = EClock::awake_nanoseconds;
    TEST_EXPECT(ctx, !wrong_clock.is_valid());
    TEST_EXPECT(ctx, !first.reader.snapshot().device);
    for (std::uint32_t slot = 0u; slot < 3u; ++slot) fixture.controllers.attach(*fixture.runtime, slot, 100u + slot);
    const std::uint64_t one = SPlatformInputTestAccess::device(*fixture.runtime, 100u);
    const std::uint64_t two = SPlatformInputTestAccess::device(*fixture.runtime, 101u);
    const std::uint64_t three = SPlatformInputTestAccess::device(*fixture.runtime, 102u);
    TEST_EXPECT(ctx, (one && two && three && (one != two) && (two != three)));
    first.drain(fixture);
    TEST_EXPECT(ctx, !first.reader.snapshot().device); // Arrival does not auto-fill an existing slot.
    const std::uint64_t stale = fixture.submit(ECommand::enumerate_device, SEnumerateDevice{ catalogue.borrow.borrow, empty.revision });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(stale) == EServiceStatus::stale));
    SCatalogueSnapshot current;
    TEST_EXPECT(ctx, (catalogue.view->state->read(current) == EStateRead::success));
    const std::uint64_t query = fixture.submit(ECommand::enumerate_device, SEnumerateDevice{ catalogue.borrow.borrow, current.revision });
    const std::uint64_t foreign = fixture.submit(ECommand::enumerate_device, SEnumerateDevice{ catalogue.borrow.borrow, current.revision }, true);
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(query) == EServiceStatus::success) && (fixture.terminal(foreign) == EServiceStatus::invalid_request)));
    SDeviceResult result;
    for (std::uint32_t index = 0u; index < fixture.history_count; ++index)
    {
        const ServiceReply& reply = fixture.history[index].reply;
        if ((reply.request.value == query) && (reply.payload_size == sizeof(result))) std::memcpy(&result, reply.payload, sizeof(result));
    }
    TEST_EXPECT(ctx, ((result.device == one) && (result.flags == k_device_eligible)));
    //  Hold an enumeration completion across the claim change. Its copied result
    //  must retain the revision and availability that were actually enumerated.
    fixture.completion_busy = true;
    const std::uint64_t retained = fixture.submit(ECommand::enumerate_device,
        SEnumerateDevice{ catalogue.borrow.borrow, current.revision });
    fixture.step();
    const std::uint64_t assign = fixture.submit(ECommand::assign_device,
        SAssignDevice{ first.view->connection, one, first.reader.snapshot().action_generation });
    fixture.step();
    fixture.completion_busy = false;
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(assign) == EServiceStatus::success));
    bool retained_found = false;
    for (std::uint32_t index = 0u; index < fixture.history_count; ++index)
    {
        const ServiceReply& reply = fixture.history[index].reply;
        if ((reply.request.value == retained) && (reply.payload_size == sizeof(result)))
        {
            std::memcpy(&result, reply.payload, sizeof(result));
            retained_found = true;
        }
    }
    TEST_EXPECT(ctx, (retained_found && (result.revision == current.revision) &&
        (result.device == one) && (result.flags == k_device_eligible)));
    controller_settle(fixture, first);
    TEST_EXPECT(ctx, (first.reader.snapshot().device == one));
    if (!second.acquire(fixture, EConnection::controller, true))
    {
        return;
    }
    second.activate(fixture);
    if (!third.acquire(fixture, EConnection::controller, false, {}, 1u, 1u, three))
    {
        return;
    }
    third.activate(fixture);
    controller_settle(fixture, second);
    controller_settle(fixture, third);
    TEST_EXPECT(ctx, ((second.reader.snapshot().device == two) && (third.reader.snapshot().device == three)));
    const std::uint64_t occupied = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, one, 1u, 1u });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(occupied) == EServiceStatus::occupied));
    if (!fourth.acquire(fixture, EConnection::controller, false, {}, 1u, 1u, k_unassigned_device))
    {
        return;
    }
    fourth.activate(fixture);
    const std::uint64_t full = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, 0u, 1u, 1u });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(full) == EServiceStatus::busy));
    const std::uint64_t independent = second.reader.snapshot().action_generation;
    fixture.runtime->controller_departed(100u);
    fixture.controllers.attach(*fixture.runtime, 0u, 100u); // Reused native identity still gets a new public instance.
    const std::uint64_t replacement = SPlatformInputTestAccess::device(*fixture.runtime, 100u);
    TEST_EXPECT(ctx, (replacement && (replacement != one)));
    controller_settle(fixture, first);
    second.drain(fixture);
    TEST_EXPECT(ctx, (!first.reader.snapshot().device && (first.reader.snapshot().controller_state == EControllerState::disconnected) &&
        (second.reader.snapshot().action_generation == independent) && (second.reader.snapshot().device == two)));
    const std::uint64_t old = fixture.submit(ECommand::assign_device,
        SAssignDevice{ first.view->connection, one, first.reader.snapshot().action_generation });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(old) == EServiceStatus::stale));
    const std::uint64_t reassign = fixture.submit(ECommand::assign_device,
        SAssignDevice{ first.view->connection, replacement, first.reader.snapshot().action_generation });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(reassign) == EServiceStatus::success));
    controller_settle(fixture, first);
    TEST_EXPECT(ctx, (first.reader.snapshot().device == replacement));
    for (std::uint32_t slot = 3u; slot < k_device_capacity; ++slot) fixture.controllers.attach(*fixture.runtime, slot, 100u + slot);
    platform::native::SControllerInfo extra;
    extra.device = 999u;
    fixture.runtime->controller_arrived(extra);
    TEST_EXPECT(ctx, (catalogue.view->state->read(current) == EStateRead::success));
    TEST_EXPECT(ctx, ((current.count == k_device_capacity) && current.overflow && !SPlatformInputTestAccess::device(*fixture.runtime, 999u)));
    first.drain(fixture);
    TEST_EXPECT(ctx, (first.reader.snapshot().device == replacement));
    // A closing pipe retains its claim and capacity until consumer quiescence.
    const SConnectionToken first_token = first.view->connection;
    first.reader.close();
    fixture.router.release_borrow(fixture.client, { fixture.route, first.borrow.borrow });
    fixture.step();
    CInputConnection* const closing = SPlatformInputTestAccess::connection(*fixture.runtime, first_token);
    TEST_EXPECT(ctx, (closing && (closing->snapshot().phase == EPhase::closing) && (closing->snapshot().device == replacement)));
    fixture.router.quiesce_borrow(fixture.client, { fixture.route, first.borrow.borrow });
    for (unsigned step = 0u; step < 8u; ++step) fixture.step();
    TEST_EXPECT(ctx, !SPlatformInputTestAccess::connection(*fixture.runtime, first_token));
}

void controller_source_history(TTestContext& ctx)
{
    SFixture fixture{ ctx, false };
    const CAssetId source = fixture.schema_source();
    const std::uint64_t install = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    for (unsigned step = 0u; !installation_token(fixture, install).value && (step < 20000u); ++step) fixture.step();
    const SInstallationToken installation = installation_token(fixture, install);
    TEST_EXPECT(ctx, installation.value);
    fixture.controllers.attach(*fixture.runtime, 0u, 201u);
    fixture.controllers.attach(*fixture.runtime, 1u, 202u);
    SUse first, second;
    if (!first.acquire(fixture, EConnection::controller, false, installation, 8u, 4u))
    {
        return;
    }
    first.activate(fixture);
    if (!second.acquire(fixture, EConnection::controller, true, installation, 8u, 4u))
    {
        return;
    }
    second.activate(fixture);
    controller_settle(fixture, first);
    controller_settle(fixture, second);
    const std::uint64_t other_generation = second.reader.snapshot().action_generation;
    platform::native::SControllerReading sample;
    sample.time_ns = 10u;
    sample.left_x = 0.8f;
    const auto observe = [&](const bool available, const std::uint64_t time, const float x)
    {
        sample.time_valid = available;
        sample.time_ns = time;
        sample.left_x = x;
        fixture.controllers.sample(0u, sample);
        for (unsigned step = 0u; step < 12u; ++step) fixture.step();
        SInputEvent captured;
        bool found = false;
        for (unsigned step = 0u; step < 32u; ++step)
        {
            SInputEvent event;
            const EInputRead read = first.reader.next(event);
            if (read == EInputRead::empty) break;
            if (read == EInputRead::record)
            {
                captured = event;
                found = true;
            }
        }
        TEST_EXPECT(ctx, found);
        TEST_EXPECT(ctx, ((captured.action.time_ns == (available ? time : 0u)) &&
            (((captured.action.flags & k_no_source_time) == 0u) == available) && captured.device && captured.clock_domain && captured.clock_epoch));
        return captured;
    };
    const SInputEvent a = observe(true, 10u, 0.8f);
    const SInputEvent b = observe(true, 10u, 0.5f); // Tied timestamps still preserve source order.
    const SInputEvent c = observe(false, 0u, 0.3f);
    const SInputEvent d = observe(true, 11u, 0.7f);
    TEST_EXPECT(ctx, ((a.action.sequence < b.action.sequence) && (b.action.sequence < c.action.sequence) &&
        (c.action.sequence < d.action.sequence)));
    fixture.clock.time = 777u;
    sample.time_ns = 12u;
    fixture.controllers.sample(0u, sample); // Unchanged controls still update receipt diagnostics.
    for (unsigned step = 0u; step < 12u; ++step) fixture.step();
    first.drain(fixture);
    TEST_EXPECT(ctx, (first.reader.snapshot().receipt_awake_ns == 777u));
    fixture.controllers.devices[0].history_lost = true;
    fixture.step();
    controller_settle(fixture, first);
    TEST_EXPECT(ctx, ((first.reader.snapshot().device == a.device) && first.reader.snapshot().ingress_loss));
    second.drain(fixture);
    TEST_EXPECT(ctx, (second.reader.snapshot().action_generation == other_generation));
    // A clock reset crosses a boundary and permits a smaller source timestamp.
    fixture.controllers.devices[0].info.clock_epoch = 2u;
    fixture.controllers.devices[0].latest = {};
    fixture.runtime->controller_arrived(fixture.controllers.devices[0].info);
    controller_settle(fixture, first);
    TEST_EXPECT(ctx, ((first.reader.snapshot().clock_epoch == 2u) && (first.reader.validate(d) == EInputValidation::stale)));
    (void)observe(true, 1u, 0.5f);
    // A backwards timestamp without an epoch change is an explicit source fault.
    sample.time_ns = 0u;
    sample.left_x = 0.9f;
    fixture.controllers.sample(0u, sample);
    controller_settle(fixture, first);
    TEST_EXPECT(ctx, (!first.reader.snapshot().device && (first.reader.snapshot().controller_state == EControllerState::invalid_sample)));
    second.drain(fixture);
    TEST_EXPECT(ctx, ((second.reader.snapshot().device != 0u) && (second.reader.snapshot().action_generation == other_generation)));
    CInputConnection& survivor = *SPlatformInputTestAccess::connection(*fixture.runtime, second.view->connection);
    const std::uint64_t retained_device = survivor.snapshot().device;
    SInputPublicationTestAccess::revision(SPlatformInputTestAccess::publication(survivor), 0xfffffffcu);
    sample.time_ns = 20u;
    fixture.controllers.sample(1u, sample);
    fixture.step();
    TEST_EXPECT(ctx, ((survivor.snapshot().phase == EPhase::closing) && (survivor.snapshot().device == retained_device)));
    TEST_EXPECT(ctx, !survivor.assign(0u, 0u, 0u, EControllerState::unassigned, EReason::assignment));
    TEST_EXPECT(ctx, (survivor.snapshot().device == retained_device));
}

void controller_parallel_recovery(TTestContext& ctx)
{
    SFixture fixture{ ctx, false };
    const CAssetId source = fixture.schema_source();
    fixture.source = tests::controller_schema();
    const std::uint64_t install = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    for (unsigned step = 0u; !installation_token(fixture, install).value && (step < 20000u); ++step) fixture.step();
    const SInstallationToken installation = installation_token(fixture, install);
    TEST_EXPECT(ctx, installation.value);
    fixture.controllers.attach(*fixture.runtime, 0u, 301u);
    fixture.controllers.attach(*fixture.runtime, 1u, 302u);
    SUse first, second;
    if (!first.acquire(fixture, EConnection::controller, false, installation, 8u, 4u) ||
        !second.acquire(fixture, EConnection::controller, false, installation, 8u, 5u))
    {
        return;
    }
    first.activate(fixture);
    second.activate(fixture);
    controller_settle(fixture, first);
    controller_settle(fixture, second);
    platform::native::SControllerReading sample;
    for (unsigned report = 0u; report < 16u; ++report)
    {
        sample.time_ns = report + 1u;
        sample.buttons = (report & 1u) ? 0u : 0x3fffu;
        sample.left_x = (report & 1u) ? 0.0f : 0.8f;
        fixture.controllers.sample(0u, sample);
        fixture.controllers.sample(1u, sample);
    }
    for (unsigned step = 0u; step < 128u; ++step) fixture.step();
    first.drain(fixture);
    second.drain(fixture);
    TEST_EXPECT(ctx, ((first.records >= 200u) && (second.records >= 200u) &&
        (first.reader.snapshot().context == 4u) && (second.reader.snapshot().context == 5u)));
    const std::uint64_t original_device = first.reader.snapshot().device;
    const std::uint64_t generation = first.reader.snapshot().action_generation;
    const std::uint64_t context = fixture.submit(ECommand::select_context, SSelectContext{ first.view->connection, 5u });
    const std::uint64_t stale = fixture.submit(ECommand::assign_device, SAssignDevice{ first.view->connection, k_unassigned_device, generation });
    fixture.step();
    TEST_EXPECT(ctx, ((fixture.terminal(context) == EServiceStatus::success) && (fixture.terminal(stale) == EServiceStatus::stale)));
    controller_settle(fixture, first);
    TEST_EXPECT(ctx, (first.reader.snapshot().device == original_device));
    //  Both sources remain held throughout focus loss. The returning baseline
    //  must suppress them until a subsequent neutral report and fresh input.
    fixture.controllers.delay_resume = true;
    fixture.runtime->input_focus(false);
    sample.time_ns = 100u;
    sample.buttons = 1u;
    sample.left_x = 0.8f;
    fixture.controllers.sample(0u, sample);
    fixture.controllers.sample(1u, sample);
    //  No pump separates loss and return: the adapter must still see both.
    fixture.runtime->input_focus(true);
    TEST_EXPECT(ctx, (fixture.controllers.devices[0].baseline_wait && fixture.controllers.devices[1].baseline_wait));
    for (unsigned step = 0u; step < 16u; ++step) fixture.step();
    first.drain(fixture);
    second.drain(fixture);
    TEST_EXPECT(ctx, ((first.reader.snapshot().ingress_pending & k_actions) && (second.reader.snapshot().ingress_pending & k_actions)));
    fixture.controllers.devices[0].baseline_wait = false;
    controller_settle(fixture, first);
    second.drain(fixture);
    TEST_EXPECT(ctx, (!(first.reader.snapshot().ingress_pending & k_actions) && (second.reader.snapshot().ingress_pending & k_actions)));
    fixture.controllers.devices[1].baseline_wait = false;
    controller_settle(fixture, second);
    const unsigned before_first = first.records, before_second = second.records;
    sample.time_ns = 101u;
    fixture.controllers.sample(0u, sample);
    fixture.controllers.sample(1u, sample);
    for (unsigned step = 0u; step < 16u; ++step) fixture.step();
    first.drain(fixture);
    second.drain(fixture);
    TEST_EXPECT(ctx, ((first.records == before_first) && (second.records == before_second)));
    sample.time_ns = 102u;
    sample.buttons = 0u;
    sample.left_x = 0.0f;
    fixture.controllers.sample(0u, sample);
    fixture.controllers.sample(1u, sample);
    for (unsigned step = 0u; step < 16u; ++step) fixture.step();
    sample.time_ns = 103u;
    sample.buttons = 1u;
    sample.left_x = 0.8f;
    fixture.controllers.sample(0u, sample);
    fixture.controllers.sample(1u, sample);
    for (unsigned step = 0u; step < 16u; ++step) fixture.step();
    first.drain(fixture);
    second.drain(fixture);
    TEST_EXPECT(ctx, ((first.records > before_first) && (second.records > before_second)));
    fixture.runtime->controller_backend(EControllerBackend::failed, 99u);
    controller_settle(fixture, first);
    controller_settle(fixture, second);
    TEST_EXPECT(ctx, (!first.reader.snapshot().device && !second.reader.snapshot().device &&
        (first.reader.snapshot().controller_state == EControllerState::unavailable) &&
        (second.reader.snapshot().controller_state == EControllerState::unavailable)));
}

void controller_eligibility_edges(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.runtime->input_focus(false);
    SCatalogue catalogue;
    TEST_EXPECT(ctx, catalogue.acquire(fixture));
    TEST_EXPECT(ctx, (!fixture.controllers.eligible && (fixture.controllers.eligibility_calls == 1u)));
    fixture.runtime->input_focus(true);
    fixture.runtime->input_focus(false);
    fixture.runtime->input_focus(true);
    TEST_EXPECT(ctx, (fixture.controllers.eligible && (fixture.controllers.eligibility_calls == 4u)));
    fixture.runtime->enter_modal(false);
    fixture.runtime->enter_modal(true);
    fixture.runtime->input_focus(false);
    fixture.runtime->leave_modal(true);
    fixture.runtime->leave_modal(false);
    TEST_EXPECT(ctx, (!fixture.controllers.eligible && (fixture.controllers.eligibility_calls == 5u)));
    fixture.runtime->input_focus(true);
    fixture.runtime->input_focus(true);
    TEST_EXPECT(ctx, (fixture.controllers.eligible && (fixture.controllers.eligibility_calls == 6u)));
}

void controller_unavailable_and_catalogue_retirement(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.controllers.available = false;
    SCatalogue catalogue;
    TEST_EXPECT(ctx, catalogue.acquire(fixture));
    if (!catalogue.view) return;
    SCatalogueSnapshot snapshot;
    TEST_EXPECT(ctx, (catalogue.view->state->read(snapshot) == EStateRead::success));
    TEST_EXPECT(ctx, ((snapshot.backend == EControllerBackend::unavailable) && !snapshot.count));
    fixture.runtime->input_focus(false);
    fixture.runtime->input_focus(true);
    TEST_EXPECT(ctx, !fixture.controllers.eligibility_calls);
    const std::uint64_t unavailable = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, 0u, 1u, 1u });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.terminal(unavailable) == EServiceStatus::unavailable));
    SUse unassigned;
    if (!unassigned.acquire(fixture, EConnection::controller, false, {}, 1u, 1u, k_unassigned_device))
    {
        return;
    }
    unassigned.activate(fixture);
    TEST_EXPECT(ctx, !unassigned.reader.snapshot().device);
    const std::uint64_t request = fixture.submit(static_cast<std::uint16_t>(ECommand::acquire_catalogue), nullptr, 0u);
    fixture.step();
    const SPreparedServiceBorrow pending = fixture.prepared(request);
    TEST_EXPECT(ctx, pending.view.descriptor);
    if (!pending.view.descriptor) return;
    fixture.clock.time += UINT64_C(5000000001);
    //  Catalogue progress runs after this pump's completion flush; allow the
    //  retained terminal reply to be delivered by the following pump.
    for (unsigned step = 0u; (fixture.terminal(request) == EServiceStatus::unavailable) && (step < 4u); ++step)
    {
        fixture.step();
    }
    TEST_EXPECT(ctx, (fixture.terminal(request) == EServiceStatus::timed_out));
    const SControllerCatalogueV1& retained = *static_cast<const SControllerCatalogueV1*>(pending.view.descriptor);
    TEST_EXPECT(ctx, (retained.state->read(snapshot) == EStateRead::success));
    TEST_EXPECT(ctx, !fixture.router.provider_borrows_retired(fixture.provider));
    fixture.release(pending);
    fixture.router.release_borrow(fixture.client, { fixture.route, catalogue.borrow.borrow });
    fixture.step();
    TEST_EXPECT(ctx, (catalogue.view->state->read(snapshot) == EStateRead::success));
    fixture.router.quiesce_borrow(fixture.client, { fixture.route, catalogue.borrow.borrow });
    for (unsigned step = 0u; step < 8u; ++step) fixture.step();
}

#if MV_PLATFORM_WINDOWS
static void controller_native_acceptance(TTestContext& ctx, const bool interactive)
{
    SFixture fixture{ ctx, false, (interactive ? platform::native::window_adapter() : SNative::adapter()),
        platform::native::controller_adapter() };
    fixture.clock.native_time = true;
    SCatalogue catalogue;
    TEST_EXPECT(ctx, catalogue.acquire(fixture));
    if (!catalogue.view) return;
    SCatalogueSnapshot available;
    const ULONGLONG startup_deadline = GetTickCount64() + 5000u;
    do
    {
        fixture.step();
        TEST_EXPECT(ctx, (catalogue.view->state->read(available) == EStateRead::success));
        if (available.backend != EControllerBackend::initialising)
        {
            break;
        }
        Sleep(1u);
    } while (GetTickCount64() < startup_deadline);
    std::printf("GameInput backend=%u devices=%u error=0x%08x (zero devices is valid)\n",
        static_cast<unsigned>(available.backend), available.count, available.error);
    TEST_EXPECT(ctx, (available.backend == EControllerBackend::ready));
    if (available.backend != EControllerBackend::ready)
    {
        return;
    }
    //  Enumerate copied public results, including the revision check at the end.
    std::uint32_t cursor = 0u;
    bool enumeration_complete = false;
    for (std::uint32_t item = 0u; item <= k_device_capacity; ++item)
    {
        const std::uint64_t request = fixture.submit(ECommand::enumerate_device,
            SEnumerateDevice{ catalogue.borrow.borrow, available.revision, cursor });
        fixture.step();
        if (fixture.terminal(request) == EServiceStatus::stale)
        {
            break;
        }
        TEST_EXPECT(ctx, (fixture.terminal(request) == EServiceStatus::success));
        SDeviceResult device;
        bool found = false;
        for (std::uint32_t index = 0u; index < fixture.history_count; ++index)
        {
            const ServiceReply& reply = fixture.history[index].reply;
            if ((reply.request.value == request) && (reply.payload_size == sizeof(device)))
            {
                std::memcpy(&device, reply.payload, sizeof(device));
                found = true;
            }
        }
        TEST_EXPECT(ctx, (found && (device.revision == available.revision)));
        if (!found) return;
        if (device.flags & k_device_end)
        {
            enumeration_complete = true;
            break;
        }
        TEST_EXPECT(ctx, (device.device && (device.next_cursor > cursor) && (device.next_cursor <= k_device_capacity)));
        std::printf("  device=%llu eligible=%u claimed=%u\n", static_cast<unsigned long long>(device.device),
            ((device.flags & k_device_eligible) ? 1u : 0u), ((device.flags & k_device_claimed) ? 1u : 0u));
        cursor = device.next_cursor;
    }
    SCatalogueSnapshot after;
    TEST_EXPECT(ctx, (catalogue.view->state->read(after) == EStateRead::success));
    TEST_EXPECT(ctx, (enumeration_complete || (after.revision != available.revision)));
    if (after.revision != available.revision) std::printf("Catalogue changed during scan; repeat for a coherent list.\n");
    const CAssetId source = fixture.schema_source();
    fixture.source = tests::controller_schema();
    const std::uint64_t install = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    for (unsigned step = 0u; !installation_token(fixture, install).value && (step < 20000u); ++step) fixture.step();
    const SInstallationToken installation = installation_token(fixture, install);
    TEST_EXPECT(ctx, installation.value);
    if (!installation.value) return;
    if (interactive) (void)fixture.window();
    SUse pipes[k_controller_capacity];
    for (std::uint32_t index = 0u; index < k_controller_capacity; ++index)
    {
        if (!pipes[index].acquire(fixture, EConnection::controller, false, installation, 8u, 4u + (index & 1u)))
        {
            return;
        }
        pipes[index].activate(fixture);
        controller_settle(fixture, pipes[index]);
        std::printf("  pipe=%u device=%llu context=%u\n", (index + 1u),
            static_cast<unsigned long long>(pipes[index].reader.snapshot().device), pipes[index].reader.snapshot().context);
    }
    if (!interactive)
    {
        std::printf("Runtime/pipe lifecycle smoke only; physical controls remain unverified.\n");
        return;
    }
    const HWND handle = reinterpret_cast<HWND>(SPlatformInputTestAccess::handle(*fixture.runtime));
    (void)SetForegroundWindow(handle);
    (void)SetFocus(handle);
    std::printf("120-second observation: 1-4 select displayed pipe; C changes its context; U unassigns it;\n"
        "R explicitly assigns available devices to unassigned pipes; Escape finishes. Move/release every control.\n"
        "After disconnect/reconnect, press R to reassign. Alt+Tab tests held-state recovery.\n"
        "Display refresh targets 60 Hz; periodic console logging remains once per second.\n");
    struct SDisplay
    {
        std::uint32_t buttons{}, records{};
        SVectorAction left, right;
        float left_trigger{}, right_trigger{};
        std::uint64_t time{}, domain{}, epoch{};
    } display[k_controller_capacity];
    constexpr int keys[]{ '1', '2', '3', '4', 'C', 'U', 'R', VK_ESCAPE };
    bool held[sizeof(keys) / sizeof(keys[0])]{};
    std::uint32_t selected = 0u;
    const ULONGLONG end = GetTickCount64() + 120000u;
    constexpr std::uint32_t k_display_hz = 60u;
    const std::chrono::steady_clock::duration display_interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds{ 1 }) / k_display_hz;
    std::chrono::steady_clock::time_point display_at = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point log_at = display_at;
    while ((GetTickCount64() < end) && IsWindow(handle) && !fixture.runtime->cancelling())
    {
        fixture.step();
        for (std::uint32_t index = 0u; index < k_controller_capacity; ++index)
        {
            SUse& use = pipes[index];
            SDisplay& values = display[index];
            for (unsigned work = 0u; work < k_action_capacity; ++work)
            {
                SInputEvent event;
                const EInputRead read = use.reader.next(event);
                if (read == EInputRead::empty) break;
                if ((read == EInputRead::failed) || (read == EInputRead::closing))
                {
                    std::printf("Pipe %u reader stopped unexpectedly: %u\n", (index + 1u), static_cast<unsigned>(read));
                    TEST_EXPECT(ctx, false);
                    return;
                }
                if (read == EInputRead::cancelled)
                {
                    (void)use.reader.take_cancellation();
                    const std::uint32_t records = values.records;
                    values = {};
                    values.records = records;
                }
                if (read != EInputRead::record) continue;
                TEST_EXPECT(ctx, (use.reader.validate(event) == EInputValidation::current));
                ++values.records;
                values.time = event.action.time_ns;
                values.domain = event.clock_domain;
                values.epoch = event.clock_epoch;
                const std::uint32_t control = event.action.subject - 100u;
                if (control < 14u)
                {
                    SDigitalAction value;
                    std::memcpy(&value, event.action.payload, sizeof(value));
                    if (value.active) values.buttons |= 1u << control;
                    else values.buttons &= ~(1u << control);
                }
                else if (control == 14u) std::memcpy(&values.left, event.action.payload, sizeof(values.left));
                else if (control == 15u) std::memcpy(&values.right, event.action.payload, sizeof(values.right));
                else if (control == 16u) std::memcpy(&values.left_trigger, event.action.payload, sizeof(values.left_trigger));
                else if (control == 17u) std::memcpy(&values.right_trigger, event.action.payload, sizeof(values.right_trigger));
            }
            SBoundaryReady ready;
            if (use.reader.boundary_ready(ready)) use.acknowledge(fixture);
        }
        for (unsigned key = 0u; key < (sizeof(keys) / sizeof(keys[0])); ++key)
        {
            const bool down = (GetForegroundWindow() == handle) && ((GetAsyncKeyState(keys[key]) & 0x8000) != 0);
            const bool pressed = down && !held[key];
            held[key] = down;
            if (!pressed) continue;
            if (key < k_controller_capacity) selected = key;
            else if (keys[key] == VK_ESCAPE) return;
            else if (keys[key] == 'C')
            {
                const SInputSnapshot& snapshot = pipes[selected].reader.snapshot();
                const std::uint64_t request = fixture.submit(ECommand::select_context,
                    SSelectContext{ pipes[selected].view->connection, ((snapshot.context == 4u) ? 5u : 4u) });
                fixture.step();
                std::printf("Context request: status=%u\n", static_cast<unsigned>(fixture.terminal(request)));
            }
            else
            {
                for (std::uint32_t index = 0u; index < k_controller_capacity; ++index)
                {
                    const SInputSnapshot& snapshot = pipes[index].reader.snapshot();
                    if (((keys[key] == 'U') && (index != selected)) || ((keys[key] == 'R') && snapshot.device))
                    {
                        continue;
                    }
                    const std::uint64_t target = (keys[key] == 'U') ? k_unassigned_device : k_default_device;
                    const std::uint64_t request = fixture.submit(ECommand::assign_device,
                        SAssignDevice{ pipes[index].view->connection, target, snapshot.action_generation });
                    fixture.step();
                    std::printf("Assignment pipe=%u status=%u\n", (index + 1u), static_cast<unsigned>(fixture.terminal(request)));
                }
            }
        }
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const SInputSnapshot& snapshot = pipes[selected].reader.snapshot();
        const SDisplay& values = display[selected];
        if (now >= display_at)
        {
            wchar_t title[512]{};
            (void)std::swprintf(title, 512u,
                L"Pad %u device %llu state %u context %u | buttons %04x L %.2f %.2f R %.2f %.2f T %.2f %.2f | records %u/%u/%u/%u",
                (selected + 1u), static_cast<unsigned long long>(snapshot.device), static_cast<unsigned>(snapshot.controller_state), snapshot.context,
                values.buttons, values.left.x, values.left.y, values.right.x, values.right.y, values.left_trigger, values.right_trigger,
                display[0].records, display[1].records, display[2].records, display[3].records);
            (void)SetWindowTextW(handle, title);
            //  Retain the cadence through small scheduling delays; skip missed refreshes.
            display_at += display_interval;
            if (display_at <= now) display_at = now + display_interval;
        }
        if (now >= log_at)
        {
            std::printf("Pipe %u device=%llu time=%llu domain/epoch=%llu/%llu receipt=%llu records=%u\n", (selected + 1u),
                static_cast<unsigned long long>(snapshot.device), static_cast<unsigned long long>(values.time),
                static_cast<unsigned long long>(values.domain), static_cast<unsigned long long>(values.epoch),
                static_cast<unsigned long long>(snapshot.receipt_awake_ns), values.records);
            log_at = now + std::chrono::seconds{ 1 };
        }
        Sleep(1u);
    }
    std::printf("Physical behaviour requires operator review; record each model and transport separately.\n");
}

void desktop_native_bookkeeping(TTestContext& ctx, SFixture& fixture, SUse& desktop)
{
    using namespace platform::native;
    using namespace platform::runtime::input_control;
    platform::runtime::CPlatformRuntime& runtime = *fixture.runtime;
    SWindow probe;
    probe.runtime = &runtime;
    probe.handle = SPlatformInputTestAccess::handle(runtime);
    const HWND handle = reinterpret_cast<HWND>(probe.handle);
    struct SKeyboardRestore
    {
        BYTE saved[256]{};
        bool valid{};
        ~SKeyboardRestore() { if (valid) (void)SetKeyboardState(saved); }
    } restore;
    restore.valid = GetKeyboardState(restore.saved) != FALSE;
    TEST_EXPECT(ctx, restore.valid);
    if (!restore.valid) return;
    BYTE keyboard[256]{};
    keyboard[VK_RETURN] = 0x80u;
    TEST_EXPECT(ctx, SetKeyboardState(keyboard));
    desktop_baseline(probe);
    TEST_EXPECT(ctx, (!probe.desktop.known[k_enter] && !probe.desktop.known[k_keypad_enter]));
    keyboard[VK_RETURN] = 0u;
    TEST_EXPECT(ctx, SetKeyboardState(keyboard));
    desktop_baseline(probe);
    TEST_EXPECT(ctx, (probe.desktop.known[k_enter] && probe.desktop.known[k_keypad_enter] &&
        !probe.desktop.keys[k_enter] && !probe.desktop.keys[k_keypad_enter]));
    keyboard[VK_LCONTROL] = keyboard[VK_CONTROL] = keyboard[VK_RMENU] = keyboard[VK_MENU] = 0x80u;
    TEST_EXPECT(ctx, SetKeyboardState(keyboard));
    desktop_baseline(probe);
    TEST_EXPECT(ctx, (probe.desktop.altgr && !probe.desktop.known[k_left_ctrl] && !probe.desktop.known[k_either_ctrl]));
    probe.desktop.genuine_ctrl = true;
    desktop_baseline(probe);
    TEST_EXPECT(ctx, (probe.desktop.keys[k_left_ctrl] && probe.desktop.known[k_left_ctrl] &&
        probe.desktop.keys[k_either_ctrl] && probe.desktop.known[k_either_ctrl]));
    std::memset(keyboard, 0, sizeof(keyboard));
    TEST_EXPECT(ctx, SetKeyboardState(keyboard));
    const auto deliver = [&](const UINT kind, const WPARAM key, const LPARAM flags, const bool paired)
    {
        //  Keep timer/paint traffic outside the deliberately adjacent synthetic
        //  key sequence. Lifecycle callbacks still run and can invalidate it.
        MSG unrelated{};
        for (std::uint32_t step = 0u; (step < 64u) && PeekMessageW(&unrelated, nullptr, 0u, 0u, PM_REMOVE); ++step)
        {
            (void)TranslateMessage(&unrelated);
            (void)DispatchMessageW(&unrelated);
        }
        TEST_EXPECT(ctx, PostMessageW(handle, kind, key, flags));
        MSG preview{};
        TEST_EXPECT(ctx, PeekMessageW(&preview, handle, kind, kind, PM_NOREMOVE));
        //  PostMessage timestamps depend on scheduler granularity. Explicitly
        //  give this synthetic pair the same stamp as its retained prefix.
        if (paired && probe.desktop.ctrl_pending) probe.desktop.ctrl_time = preview.time;
        if (paired && probe.desktop.ctrl_release_pending) probe.desktop.ctrl_release_time = preview.time;
        MSG message{};
        const bool retrieved = desktop_next(probe, message);
        TEST_EXPECT(ctx, (retrieved && (message.hwnd == handle) && (message.message == kind) && (message.wParam == key)));
        if (retrieved) (void)desktop_message(probe, message.message, message.wParam, message.lParam);
    };
    constexpr LPARAM k_ctrl_down = (0x1du << 16u) | 1u;
    constexpr LPARAM k_alt_down = (0x38u << 16u) | (1u << 24u) | 1u;
    constexpr LPARAM k_released = static_cast<LPARAM>((1u << 30u) | (1u << 31u));
    probe.desktop.genuine_ctrl = false;
    probe.desktop.known[k_left_ctrl] = false;
    probe.desktop.keys[k_left_ctrl] = false;
    deliver(WM_KEYDOWN, VK_CONTROL, k_ctrl_down, false);
    deliver(WM_SYSKEYDOWN, VK_MENU, k_alt_down, true);
    TEST_EXPECT(ctx, (probe.desktop.altgr && !probe.desktop.known[k_left_ctrl] && !probe.desktop.known[k_either_ctrl]));
    for (const UINT family : { WM_KEYDOWN, WM_SYSKEYDOWN })
    {
        for (const bool ctrl_first : { false, true })
        {
            probe.desktop = {};
            desktop_baseline(probe);
            deliver(WM_KEYDOWN, VK_CONTROL, k_ctrl_down, false);
            deliver(WM_KEYUP, VK_F12, ((0x58u << 16u) | k_released | 1u), false);
            TEST_EXPECT(ctx, probe.desktop.genuine_ctrl);
            deliver(WM_KEYDOWN, VK_CONTROL, k_ctrl_down, false);
            deliver(family, VK_MENU, k_alt_down, true);
            TEST_EXPECT(ctx, (probe.desktop.altgr && probe.desktop.keys[k_left_ctrl] && probe.desktop.keys[k_either_ctrl]));
            if (ctrl_first)
            {
                deliver(WM_KEYUP, VK_CONTROL, (k_ctrl_down | k_released), false);
                TEST_EXPECT(ctx, (probe.desktop.altgr && !probe.desktop.keys[k_left_ctrl] && !probe.desktop.keys[k_either_ctrl]));
            }
            deliver((family == WM_KEYDOWN) ? WM_KEYUP : WM_SYSKEYUP, VK_MENU, (k_alt_down | k_released), false);
            deliver(WM_KEYUP, VK_CONTROL, (k_ctrl_down | k_released), true);
            TEST_EXPECT(ctx, (!probe.desktop.altgr && (probe.desktop.keys[k_left_ctrl] == !ctrl_first) &&
                (probe.desktop.keys[k_either_ctrl] == !ctrl_first)));
            if (!ctrl_first) deliver(WM_KEYUP, VK_CONTROL, (k_ctrl_down | k_released), false);
            TEST_EXPECT(ctx, (!probe.desktop.genuine_ctrl && !probe.desktop.keys[k_left_ctrl] && !probe.desktop.keys[k_either_ctrl]));
        }
    }
    //  Physical UK trace: Ctrl, Ctrl+RAlt, Ctrl-up, then native AltGr repeat
    //  pairs whose Ctrl prefix changes from first-down to bit-30 repeat.
    const auto settle = [&]()
    {
        for (std::uint32_t step = 0u; step < 32u; ++step) fixture.step();
    };
    probe.desktop = {};
    desktop_baseline(probe);
    settle();
    desktop.drain(fixture);
    deliver(WM_KEYDOWN, VK_CONTROL, k_ctrl_down, false);
    deliver(WM_KEYUP, VK_F12, ((0x58u << 16u) | k_released | 1u), false);
    deliver(WM_KEYDOWN, VK_MENU, (k_alt_down | (1u << 29u)), false);
    deliver(WM_SYSKEYUP, VK_CONTROL, (k_ctrl_down | k_released | (1u << 29u)), false);
    TEST_EXPECT(ctx, (!probe.desktop.genuine_ctrl && !probe.desktop.keys[k_left_ctrl] && probe.desktop.keys[k_right_alt]));
    settle();
    desktop.drain(fixture);
    constexpr std::uint16_t k_ctrl_mask = 1u << 2u;
    constexpr std::uint16_t k_right_alt_mask = 1u << 5u;
    constexpr std::uint16_t k_altgr_mask = 1u << 8u;
    std::uint32_t modifier_records = 0u;
    for (const bool repeat : { false, true, true })
    {
        deliver(WM_KEYDOWN, VK_CONTROL, (k_ctrl_down | (1u << 29u) | (repeat ? (1u << 30u) : 0u)), false);
        deliver(WM_KEYDOWN, VK_MENU, (k_alt_down | (1u << 29u) | (1u << 30u)), true);
        TEST_EXPECT(ctx, (probe.desktop.altgr && !probe.desktop.genuine_ctrl &&
            !probe.desktop.keys[k_left_ctrl] && !probe.desktop.keys[k_either_ctrl] && probe.desktop.keys[k_right_alt]));
        settle();
        for (std::uint32_t step = 0u; step < 128u; ++step)
        {
            SInputEvent event;
            const EInputRead result = desktop.reader.next(event);
            if (result == EInputRead::empty) break;
            if ((result != EInputRead::record) || (event.stream != k_actions) || (event.action.kind != EActionKind::modifiers))
            {
                continue;
            }
            SModifierInput value;
            std::memcpy(&value, event.action.payload, sizeof(value));
            ++modifier_records;
            TEST_EXPECT(ctx, ((value.state.modifiers == (k_right_alt_mask | k_altgr_mask)) &&
                (value.state.modifiers_known & k_ctrl_mask)));
        }
        if (!repeat) TEST_EXPECT(ctx, (modifier_records == 1u));
    }
    TEST_EXPECT(ctx, (modifier_records == 1u));
    //  An ordinary quiet point must not replace tracked modifier knowledge with
    //  the OS's synthetic Ctrl state. Only a required recovery baseline may do so.
    const SDesktop tracked = SPlatformInputTestAccess::desktop(runtime);
    keyboard[VK_CONTROL] = keyboard[VK_LCONTROL] = keyboard[VK_MENU] = keyboard[VK_RMENU] = 0x80u;
    TEST_EXPECT(ctx, SetKeyboardState(keyboard));
    settle();
    const SDesktop& current = SPlatformInputTestAccess::desktop(runtime);
    TEST_EXPECT(ctx, ((current.keys[k_left_ctrl] == tracked.keys[k_left_ctrl]) &&
        (current.known[k_left_ctrl] == tracked.known[k_left_ctrl]) && (current.keys[k_right_alt] == tracked.keys[k_right_alt]) &&
        (current.altgr == tracked.altgr)));
    TEST_EXPECT(ctx, desktop_message(probe, WM_MOUSEMOVE, 0u, MAKELPARAM(10, 10)));
    settle();
    bool pointer_seen = false;
    for (std::uint32_t step = 0u; step < 128u; ++step)
    {
        SInputEvent event;
        const EInputRead result = desktop.reader.next(event);
        if (result == EInputRead::empty) break;
        if ((result == EInputRead::record) && (event.stream == k_actions) && (event.action.kind == EActionKind::pointer))
        {
            SPointerInput value;
            std::memcpy(&value, event.action.payload, sizeof(value));
            pointer_seen = true;
            TEST_EXPECT(ctx, ((value.state.modifiers == (k_right_alt_mask | k_altgr_mask)) &&
                (value.state.modifiers_known == k_modifier_mask)));
        }
    }
    TEST_EXPECT(ctx, pointer_seen);
    std::memset(keyboard, 0, sizeof(keyboard));
    TEST_EXPECT(ctx, SetKeyboardState(keyboard));
    //  A genuine unpaired Ctrl re-press while AltGr remains held still counts.
    deliver(WM_KEYDOWN, VK_CONTROL, k_ctrl_down, false);
    deliver(WM_KEYUP, VK_F12, ((0x58u << 16u) | k_released | 1u), false);
    TEST_EXPECT(ctx, (probe.desktop.genuine_ctrl && probe.desktop.keys[k_left_ctrl] && probe.desktop.keys[k_either_ctrl]));
    deliver(WM_KEYUP, VK_CONTROL, (k_ctrl_down | k_released), false);
    deliver(WM_KEYUP, VK_MENU, (k_alt_down | k_released), false);
    deliver(WM_KEYUP, VK_CONTROL, (k_ctrl_down | k_released), true);
    TEST_EXPECT(ctx, (!probe.desktop.keys[k_left_ctrl] && !probe.desktop.keys[k_either_ctrl] && !probe.desktop.keys[k_right_alt]));
    settle();
    desktop.drain(fixture);
    probe.desktop.dead = probe.desktop.translation = true;
    probe.desktop.turn_char = false;
    TEST_EXPECT(ctx, desktop_message(probe, WM_UNICHAR, 0x1f600u, 1));
    TEST_EXPECT(ctx, (probe.desktop.dead && probe.desktop.translation && !probe.desktop.turn_char));
}

void desktop_interactive(SFixture& fixture, SUse& desktop, const CInputConnection& connection, const HWND handle)
{
    std::printf("Desktop input: 30 seconds. The window title shows text, pointer/wheel values, modifier/button masks and capture.\n"
        "Type with your layouts/dead keys/AltGr; Backspace edits the title. Hold the left button to capture, drag outside, release.\n"
        "Change focus or monitor to inspect cancellation/DPI. Text is transient and is not logged; review physical expectations manually.\n");
    std::fflush(stdout);
    wchar_t text[128]{};
    std::size_t length = 0u;
    SPointerInput pointer;
    SWheelInput wheel;
    std::uint32_t records = 0u;
    bool want_capture = false;
    std::uint64_t capture_request = 0u;
    ULONGLONG display_at = 0u;
    const ULONGLONG until = GetTickCount64() + 30000u;
    while ((GetTickCount64() < until) && !fixture.runtime->failed() && (connection.snapshot().phase != EPhase::closing))
    {
        fixture.step();
        for (std::uint32_t step = 0u; step < 128u; ++step)
        {
            SInputEvent event;
            const EInputRead result = desktop.reader.next(event);
            if (result == EInputRead::empty) break;
            if (result == EInputRead::cancelled)
            {
                const std::uint32_t cancelled = desktop.reader.take_cancellation();
                if (cancelled & k_text) { length = 0u; text[0] = 0; }
                if (cancelled & k_actions) want_capture = false;
            }
            if ((result != EInputRead::record) || (desktop.reader.validate(event) != EInputValidation::current))
            {
                continue;
            }
            ++records;
            if (event.stream == k_text)
            {
                wchar_t scalar[2]{};
                const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    reinterpret_cast<const char*>(event.text.payload), event.text.byte_count, scalar, 2);
                if ((count > 0) && ((length + static_cast<std::size_t>(count)) < 128u))
                {
                    for (int index = 0; index < count; ++index) text[length++] = scalar[index];
                    text[length] = 0;
                }
            }
            else if ((event.action.kind == EActionKind::pulse) && (event.action.subject == 10u) && length)
            {
                const wchar_t last = text[--length];
                if ((last >= 0xdc00u) && (last <= 0xdfffu) && length) --length;
                text[length] = 0;
            }
            else if ((event.action.kind == EActionKind::pointer) || (event.action.kind == EActionKind::button))
            {
                std::memcpy(&pointer, event.action.payload, sizeof(pointer));
                if ((event.action.kind == EActionKind::button) &&
                    (event.action.subject == platform::runtime::input_control::k_first_mouse_button))
                {
                    want_capture = (pointer.state.buttons & 1u) != 0u;
                }
            }
            else if (event.action.kind == EActionKind::wheel) std::memcpy(&wheel, event.action.payload, sizeof(wheel));
            else if (event.action.kind == EActionKind::modifiers) std::memcpy(&pointer.state, event.action.payload, sizeof(pointer.state));
        }
        SBoundaryReady ready;
        if (desktop.reader.boundary_ready(ready)) desktop.acknowledge(fixture);
        if (capture_request && (fixture.terminal(capture_request) != EServiceStatus::unavailable))
        {
            if (fixture.terminal(capture_request) != EServiceStatus::success) want_capture = false;
            capture_request = 0u;
        }
        const SInputSnapshot& snapshot = connection.snapshot();
        if (!capture_request && want_capture && !snapshot.capture_grant && !snapshot.pending && !snapshot.ingress_pending)
        {
            capture_request = fixture.submit(ECommand::set_capture,
                SSetCapture{ desktop.view->connection, snapshot.transition, snapshot.action_generation });
        }
        else if (!capture_request && !want_capture && snapshot.capture_grant)
        {
            capture_request = fixture.submit(ECommand::release_capture,
                SReleaseCapture{ desktop.view->connection, snapshot.transition, snapshot.capture_grant });
        }
        if (GetTickCount64() >= display_at)
        {
            wchar_t title[512]{};
            (void)std::swprintf(title, 512u, L"Input [%ls] | xy %.1f,%.1f wheel %.2f,%.2f mods %03x/%03x buttons %02x/%02x capture %llu records %u",
                text, pointer.x, pointer.y, wheel.horizontal, wheel.vertical,
                static_cast<unsigned>(pointer.state.modifiers), static_cast<unsigned>(pointer.state.modifiers_known),
                static_cast<unsigned>(pointer.state.buttons), static_cast<unsigned>(pointer.state.buttons_known),
                static_cast<unsigned long long>(snapshot.capture_grant), records);
            (void)SetWindowTextW(handle, title);
            display_at = GetTickCount64() + 100u;
        }
        Sleep(1u);
    }
    (void)SetWindowTextW(handle, L"Desktop input acceptance complete");
    std::printf("Observed %u records. Physical layout, editing and capture results require operator review.\n", records);
}

void desktop_native_acceptance(TTestContext& ctx, const bool interactive)
{
    SFixture fixture{ ctx, false, platform::native::window_adapter() };
    const CAssetId source = fixture.schema_source();
    const auto replace = [&](const std::string& from, const std::string& to)
    {
        const std::size_t at = fixture.source.find(from);
        TEST_EXPECT(ctx, (at != std::string::npos));
        if (at != std::string::npos) fixture.source.replace(at, from.size(), to);
    };
    replace(R"("ActionDefinition":{"held")", R"("ActionDefinition":{"editing":{"declaration":{"id":10,"kind":"pulse"}},"held")");
    replace(R"("InputSourceDefinition":{"key")", R"("InputSourceDefinition":{"editing_key":{"declaration":{"id":12,"kind":"physical_key","control":"backspace","direction":"whole"}},"key")");
    replace(R"("BindingDefinition":{"key_binding")", R"("BindingDefinition":{"editing_binding":{"declaration":{"id":13,"profile":3,"context":4,"action":10,"source":12,"parameters":0,"activation":"start_press","allow_during_text":true,"native_repeat":true,"reserved":0}},"key_binding")");
    replace(R"("data":[{"context":4,"action":7})", R"("data":[{"context":4,"action":10},{"context":4,"action":7})");
    const std::uint64_t install = fixture.submit(ECommand::install_configuration, SInstallConfiguration{ source });
    for (std::uint32_t step = 0u; !installation_token(fixture, install).value && (step < 20000u); ++step) fixture.step();
    const SInstallationToken token = installation_token(fixture, install);
    TEST_EXPECT(ctx, token.value);
    SUse desktop;
    if (!desktop.acquire(fixture, EConnection::desktop, false, token, 3u, 4u))
    {
        return;
    }
    desktop.activate(fixture);
    const HWND handle = reinterpret_cast<HWND>(SPlatformInputTestAccess::handle(*fixture.runtime));
    (void)SetForegroundWindow(handle);
    (void)SetFocus(handle);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    const auto pump = [&]()
    {
        for (std::uint32_t step = 0u; step < 32u; ++step) fixture.step();
    };
    pump();
    desktop.drain(fixture);
    if (connection.snapshot().pending) desktop.acknowledge(fixture);
    pump();
    TEST_EXPECT(ctx, !connection.snapshot().ingress_pending);
    const std::uint64_t enabled = fixture.submit(ECommand::enable_text,
        STextTarget{ desktop.view->connection, connection.snapshot().transition, 7u });
    pump();
    TEST_EXPECT(ctx, (fixture.terminal(enabled) == EServiceStatus::success));
    desktop.drain(fixture);
    desktop.acknowledge(fixture);
    pump();
    TEST_EXPECT(ctx, !connection.snapshot().ingress_pending);
    if (interactive)
    {
        desktop_interactive(fixture, desktop, connection, handle);
        return;
    }
    //  Inject into this owned native window, then consume through the public
    //  reader. This is native-message evidence, not physical-device acceptance.
    TEST_EXPECT(ctx, PostMessageW(handle, WM_CHAR, L'a', 1));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_KEYDOWN, VK_BACK, (0x0eu << 16u) | 1u));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_KEYUP, VK_BACK, (0x0eu << 16u) | (1u << 30u) | (1u << 31u) | 1u));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_CHAR, L'b', 1));
    pump();
    char ordering[16]{};
    std::uint32_t used = 0u;
    for (std::uint32_t iteration = 0u; iteration < 128u; ++iteration)
    {
        SInputEvent event;
        const EInputRead result = desktop.reader.next(event);
        if (result == EInputRead::empty) break;
        if (result != EInputRead::record) continue;
        TEST_EXPECT(ctx, (desktop.reader.validate(event) == EInputValidation::current));
        if ((event.stream == k_text) && (used < 15u)) ordering[used++] = static_cast<char>(event.text.payload[0]);
        else if ((event.action.kind == EActionKind::pulse) && (used < 15u)) ordering[used++] = '#';
    }
    TEST_EXPECT(ctx, (std::strcmp(ordering, "a#b") == 0));
    TEST_EXPECT(ctx, (SendMessageW(handle, WM_UNICHAR, UNICODE_NOCHAR, 0) == TRUE));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_UNICHAR, 0x1f600u, 1));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_UNICHAR, 0xd800u, 1));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_UNICHAR, 0x110000u, 1));
    pump();
    std::uint32_t unicode_records = 0u;
    for (std::uint32_t iteration = 0u; iteration < 128u; ++iteration)
    {
        SInputEvent event;
        const EInputRead result = desktop.reader.next(event);
        if (result == EInputRead::empty) break;
        if ((result == EInputRead::record) && (event.stream == k_text))
        {
            ++unicode_records;
            constexpr unsigned char expected[]{ 0xf0u, 0x9fu, 0x98u, 0x80u };
            TEST_EXPECT(ctx, ((event.text.byte_count == sizeof(expected)) && !std::memcmp(event.text.payload, expected, sizeof(expected))));
        }
    }
    TEST_EXPECT(ctx, (unicode_records == 1u));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_MOUSEMOVE, 0u, MAKELPARAM(-12, 24)));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 24)));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_LBUTTONUP, 0u, MAKELPARAM(12, 24)));
    TEST_EXPECT(ctx, PostMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0u, (WHEEL_DELTA >> 1u)), MAKELPARAM(12, 24)));
    pump();
    bool pointer = false, wheel = false;
    unsigned buttons = 0u;
    for (std::uint32_t iteration = 0u; iteration < 128u; ++iteration)
    {
        SInputEvent event;
        const EInputRead result = desktop.reader.next(event);
        if (result == EInputRead::empty) break;
        if ((result != EInputRead::record) || (event.stream != k_actions))
        {
            continue;
        }
        if (event.action.kind == EActionKind::pointer)
        {
            SPointerInput value;
            std::memcpy(&value, event.action.payload, sizeof(value));
            pointer = value.x < 0.0f;
        }
        if (event.action.kind == EActionKind::button) ++buttons;
        if (event.action.kind == EActionKind::wheel)
        {
            SWheelInput value;
            std::memcpy(&value, event.action.payload, sizeof(value));
            wheel = value.vertical == 0.5f;
        }
    }
    TEST_EXPECT(ctx, (pointer && wheel && (buttons == 2u)));
    desktop_native_bookkeeping(ctx, fixture, desktop);
    const std::uint64_t grant = fixture.submit(ECommand::set_capture,
        SSetCapture{ desktop.view->connection, connection.snapshot().transition, connection.snapshot().action_generation });
    pump();
    TEST_EXPECT(ctx, ((fixture.terminal(grant) == EServiceStatus::success) && (GetCapture() == handle)));
    const std::uint64_t text_generation = connection.snapshot().text_generation;
    TEST_EXPECT(ctx, ReleaseCapture());
    TEST_EXPECT(ctx, (!connection.snapshot().capture_grant && (connection.snapshot().capture_reason == ECaptureReason::lost) &&
        (connection.snapshot().text_generation == text_generation)));
}
#endif

}   //  namespace

void run_input_mapper_tests(tests::TTestContext& ctx);
void run_gameinput_ownership_tests(tests::TTestContext& ctx);

void run_platform_input_tests(tests::TTestContext& ctx)
{
    run_gameinput_ownership_tests(ctx);
    run_input_mapper_tests(ctx);
    mapper_runtime_integration(ctx);
    installation_lifecycle(ctx);
    installation_cancellation(ctx);
    installation_admission_and_owner(ctx);
    installation_conditioning_failure(ctx);
    readiness_and_ordering(ctx);
    pressure_and_recovery(ctx);
    independent_streams_and_validation(ctx);
    deadlines_admission_and_retirement(ctx);
    copied_marker_and_bounded_snapshot(ctx);
    transition_serialisation(ctx);
    eligible_pause_cancel_and_replacement(ctx);
    concurrent_merge_and_recovery(ctx, false);
    concurrent_merge_and_recovery(ctx, true);
    notification_failure_and_closure(ctx);
    local_cancellation_survives_overflow(ctx);
    capture_authority(ctx);
    capture_failure_cleanup(ctx);
    controller_catalogue_and_claims(ctx);
    controller_source_history(ctx);
    controller_parallel_recovery(ctx);
    controller_eligibility_edges(ctx);
    controller_unavailable_and_catalogue_retirement(ctx);
}

int run_platform_input_focused()
{
    tests::TTestContext ctx;
    run_platform_input_tests(ctx);
    return ctx.report("PlatformInput");
}

int run_platform_input_native(const bool interactive)
{
#if MV_PLATFORM_WINDOWS
    tests::TTestContext ctx;
    desktop_native_acceptance(ctx, interactive);
    return ctx.report(interactive ? "DesktopInputInteractive" : "DesktopInputNativeMessages");
#else
    (void)interactive;
    return 2;
#endif
}

int run_platform_controller_native(const bool interactive)
{
#if MV_PLATFORM_WINDOWS
    tests::TTestContext ctx;
    controller_native_acceptance(ctx, interactive);
    return ctx.report(interactive ? "ControllerInputInteractive" : "ControllerInputNative");
#else
    (void)interactive;
    return 2;
#endif
}
