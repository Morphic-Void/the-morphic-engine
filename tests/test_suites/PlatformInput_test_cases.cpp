
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
#include <thread>
#include <chrono>
#include "platform/runtime/platform_runtime.hpp"
#include "framework/platform/input_reader.hpp"
#include "host/runtime/service_router.hpp"
#include "host/runtime/host_restrictions.hpp"
#include "tests/support/test_context.hpp"
#include "threading/messages/message_helpers.hpp"
#include "services/service_borrow_client.hpp"

namespace platform::runtime
{
struct SPlatformInputTestAccess
{
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
    static platform::threading::WaitError read(void* const context, std::uint64_t& time) noexcept
    {
        time = static_cast<SClock*>(context)->time;
        return {};
    }
    platform::threading::SAwakeClock source() noexcept { return { this, &read }; }
};

struct SNative
{
    unsigned destroyed{ 0u };
    static inline SNative* current{ nullptr };
    static EServiceStatus create(platform::native::SWindow& window) noexcept
    {
        window.handle = 71u;
        window.instance = 72u;
        window.runtime->native_extent(800u, 600u, 96u, false);
        return EServiceStatus::success;
    }
    static bool dispatch(platform::native::SWindow&, const std::uint64_t) noexcept { return true; }
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
    explicit SFixture(TTestContext& tests) noexcept : tests{ tests }
    {
        SNative::current = &native;
        TEST_EXPECT(tests, (wake.initialise().code == platform::threading::EWaitError::none));
        client.phase = other.phase = provider.phase = host::EClientPhase::ready;
        client.execution = other.execution = provider.execution = host::EClientExecution::pumped;
        client.owner_wake = other.owner_wake = provider.owner_wake = wake.endpoint();
        services.complete = &complete;
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
        TEST_EXPECT(tests, (runtime.emplace(context, SNative::adapter(), clock.source()) && runtime->initialise()));
        TEST_EXPECT(tests, SPlatformInputTestAccess::install(*runtime, EConnection::desktop));
        TEST_EXPECT(tests, SPlatformInputTestAccess::install(*runtime, EConnection::controller));
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
        for (unsigned iteration = 0u; iteration < 40u; ++iteration)
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
    const SInputEndpointV1* view{ nullptr };
    std::uint64_t acquisition{ 0u };
    bool second{ false };
    unsigned boundaries{ 0u }, records{ 0u };
    std::uint32_t cancellations{ 0u };
    ~SUse() noexcept { reader.close(); }
    bool acquire(SFixture& fixture, const EConnection kind, const bool other = false) noexcept
    {
        second = other;
        if (kind == EConnection::desktop)
        {
            acquisition = fixture.submit(ECommand::acquire_desktop, SAcquireDesktop{ { fixture.window() }, { 1u }, 1u, 1u }, second);
        }
        else acquisition = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, 0u, 1u, 1u }, second);
        fixture.step();
        borrow = fixture.prepared(acquisition);
        TEST_EXPECT(fixture.tests, (borrow.view.descriptor != nullptr));
        if (!borrow.view.descriptor) return false;
        view = static_cast<const SInputEndpointV1*>(borrow.view.descriptor);
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
    connection.observe_subject(record.subject, record.payload[0] == 0u);
    return connection.post_action(record);
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
    connection.observe_neutral(1u);
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
    TEST_EXPECT(ctx, (connection.post_text(text()) == EInputPost::published));
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
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::suppressed));
    connection.observe_neutral(1u);
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    TEST_EXPECT(ctx, (connection.post_text(text()) == EInputPost::suppressed));
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
    connection.observe_neutral(1u);
    for (unsigned index = 0u; index < k_action_capacity; ++index)
    {
        TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::published));
    }
    for (unsigned index = 0u; index < k_text_capacity; ++index)
    {
        TEST_EXPECT(ctx, (connection.post_text(text()) == EInputPost::published));
    }
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::overflow));
    const std::uint64_t generation = connection.snapshot().action_generation;
    const std::uint64_t committed = connection.snapshot().committed;
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::suppressed));
    fixture.step();
    TEST_EXPECT(ctx, ((connection.snapshot().action_generation == generation) && (connection.snapshot().committed == committed) && !fixture.progress.work_pending));
    TEST_EXPECT(ctx, (connection.post_text(text()) == EInputPost::overflow));
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
    TEST_EXPECT(ctx, (post_action(connection, action()) == EInputPost::suppressed));
    connection.observe_neutral(1u);
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
    connection.observe_neutral(1u);
    independent.observe_neutral(1u);
    (void)fixture.submit(ECommand::enable_text, STextTarget{ desktop.view->connection, connection.snapshot().transition, 5u });
    fixture.step();
    desktop.drain(fixture);
    desktop.acknowledge(fixture);
    const std::uint64_t action_generation = connection.snapshot().action_generation;
    for (unsigned index = 0u; index < k_text_capacity; ++index) TEST_EXPECT(ctx, (connection.post_text(text()) == EInputPost::published));
    TEST_EXPECT(ctx, (connection.post_text(text()) == EInputPost::overflow));
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
    TEST_EXPECT(ctx, (connection.post_text(invalid) == EInputPost::invalid));
    invalid.payload[0] = 0xe0u; invalid.payload[1] = 0x80u;
    TEST_EXPECT(ctx, (connection.post_text(invalid) == EInputPost::invalid));
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
        SUse use;
        if (!use.acquire(fixture, EConnection::controller))
        {
            return;
        }
        const std::uint64_t duplicate = fixture.submit(ECommand::acquire_controller, SAcquireController{ { 2u }, 0u, 1u, 1u });
        fixture.step();
        TEST_EXPECT(ctx, (fixture.terminal(duplicate) == EServiceStatus::busy));
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
    TEST_EXPECT(ctx, publication.publish(state));
    SInputSnapshot observed;
    TEST_EXPECT(ctx, ((publication.read(observed) == EStateRead::success) &&
        (observed.action_generation == state.action_generation) && (observed.text_loss == state.text_loss)));
    SInputPublicationTestAccess::revision(publication, 3u);
    TEST_EXPECT(ctx, (publication.read(observed) == EStateRead::retry));
    SInputPublicationTestAccess::revision(publication, 0xfffffffcu);
    TEST_EXPECT(ctx, !publication.publish(state));
    TEST_EXPECT(ctx, ((publication.read(observed) == EStateRead::failed) && (observed.phase == EPhase::closing)));
    TEST_EXPECT(ctx, !publication.publish(state));
}

void transition_serialisation_and_neutrality(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::desktop)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::desktop);
    connection.observe_neutral(1u);
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
    //  A release observed during recovery establishes the baseline at activation.
    connection.observe_neutral(1u);
    connection.observe_neutral(2u);
    connection.observe_subject(2u, false);
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
    TEST_EXPECT(ctx, (post_action(connection, action(2u)) == EInputPost::suppressed));
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
    connection.observe_neutral(1u);
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

void concurrent_merge_and_recovery(TTestContext& ctx)
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
    connection.observe_neutral(1u);
    std::atomic<bool> done{ false }, finished{ false }, failed{ false }, ack_pending{ false };
    SBoundaryReady acknowledgement;
    std::uint32_t applied = 0u;
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::thread consumer{ [&]() noexcept
    {
        std::uint64_t last = 0u;
        while (std::chrono::steady_clock::now() < end)
        {
            SInputEvent event;
            const EInputRead result = use.reader.next(event, 4u);
            if (result == EInputRead::cancelled) (void)use.reader.take_cancellation();
            else if (result == EInputRead::record)
            {
                const std::uint64_t sequence = (event.stream == k_actions) ? event.action.sequence : event.text.sequence;
                if (sequence <= last) failed.store(true);
                last = sequence;
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
            if (done.load() && (result == EInputRead::empty) && !use.reader.snapshot().pending && !ack_pending.load())
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
    while (!finished.load() && (std::chrono::steady_clock::now() < end))
    {
        if (ack_pending.load(std::memory_order_acquire))
        {
            const EServiceStatus status = connection.acknowledge(acknowledgement);
            if ((status != EServiceStatus::success) && (status != EServiceStatus::stale)) failed.store(true);
            connection.observe_neutral(1u);
            ack_pending.store(false, std::memory_order_release);
        }
        if (produced < 20000u)
        {
            const EInputPost result = (produced & 1u) ? connection.post_text(text()) : post_action(connection, action());
            if ((result == EInputPost::invalid) || (result == EInputPost::closed) || (result == EInputPost::failed)) failed.store(true);
            ++produced;
        }
        else done.store(true);
        connection.progress(100u);
        std::this_thread::yield();
    }
    consumer.join();
    TEST_EXPECT(ctx, (!failed.load() && finished.load() && (produced == 20000u) && (applied > 0u)));
}

void local_cancellation_survives_overflow(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    SUse use;
    if (!use.acquire(fixture, EConnection::controller)) return;
    use.activate(fixture);
    CInputConnection& connection = *SPlatformInputTestAccess::connection(*fixture.runtime, EConnection::controller);
    connection.observe_neutral(1u);
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
    connection.observe_neutral(1u);
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
    //  Publishing a neutral release must not turn the physical baseline into held.
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

}   //  namespace

void run_platform_input_tests(tests::TTestContext& ctx)
{
    readiness_and_ordering(ctx);
    pressure_and_recovery(ctx);
    independent_streams_and_validation(ctx);
    deadlines_admission_and_retirement(ctx);
    copied_marker_and_bounded_snapshot(ctx);
    transition_serialisation_and_neutrality(ctx);
    eligible_pause_cancel_and_replacement(ctx);
    concurrent_merge_and_recovery(ctx);
    notification_failure_and_closure(ctx);
    local_cancellation_survives_overflow(ctx);
}

int run_platform_input_focused()
{
    tests::TTestContext ctx;
    run_platform_input_tests(ctx);
    return ctx.report("PlatformInput");
}
