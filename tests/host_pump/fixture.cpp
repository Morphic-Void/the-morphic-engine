
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Real DLL/SO fixtures for Host-thread execution and cooperative retirement.

#include "system/storage_type_bindings.hpp"
#include "filesystem/filesystem_messages.hpp"
#include "module/module_messages.hpp"
#include <cstdlib>
#include <cstring>
#include "tests/module_lifecycle/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "module/module_binding_context.hpp"
#include "module/host_pump.hpp"
#include "module/service_provider.hpp"
#include "services/service_borrow_client.hpp"
#include "system/system_context.hpp"
#include "assets/asset_messages.hpp"
#include "containers/TInstance.hpp"
#include "threading/CThreadPackage.hpp"
#include "threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/processor_relax.hpp"
#include "debug/macros.hpp"
#include "debug/service.hpp"

namespace pump_tests
{

using namespace modules;
using namespace services;
static const char* selected() noexcept
{
#if MV_PLATFORM_WINDOWS
    static thread_local char value[64]{};
    std::size_t required = 0u;
    return (getenv_s(&required, value, sizeof(value), "MORPHIC_PUMP_CASE") == 0 && required != 0u) ? value : "normal";
#else
    const char* value = std::getenv("MORPHIC_PUMP_CASE");
    return value != nullptr ? value : "normal";
#endif
}
static bool scenario(const char* value) noexcept { return std::strcmp(selected(), value) == 0; }
static bool borrow_scenario() noexcept { return std::strncmp(selected(), "borrow-", 7u) == 0; }
static bool notification_scenario() noexcept { return std::strncmp(selected(), "notify-", 7u) == 0; }

struct SNotificationData { std::atomic<unsigned> value{ 0u }; };
struct SNotificationReady { SServiceBorrow borrow; SNotificationRegistration registration; };

static threading::CErasedOwnerMsg module_request(const module_ids::id_type module,
    const modules::EModuleAction action, const char* file = nullptr, const bool pump = false, const bool thread = false)
{
    auto owner = CErasedOwner::create<modules::ModuleRequest>();
    auto* request = owner.payload<modules::ModuleRequest>();
    if (request != nullptr)
    {
        request->module = module;
        request->action = action;
        request->require_host_pump = pump;
        request->require_service_thread = thread;
        if (file != nullptr) (void)request->file.set(file);
    }
    threading::CErasedOwnerMsg message;
    message.set_message_type<modules::ModuleRequest>();
    message.set_async_slot(10);
    message.set_owner(std::move(owner));
    return message;
}

#if MV_PUMP_EXECUTIVE
static constexpr auto k_advertised_module_id = module_ids::executive;

//  Cleanup replies remain legal after an exit request. Only a real deadline ends
//  this test wait; the Host is responsible for continuing pump/worker progress.
static bool receive(threading::CThreadContext& context, threading::CErasedPodMsg& message) noexcept
{
    platform::threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) return false;
    for (;;)
    {
        if (context.read(message)) return true;
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining) return false;
        platform::threading::processor_relax();
    }
}
static bool operation(threading::CThreadContext& context, const modules::EModuleAction action,
    const modules::EModuleStatus expected, const bool pump = true, const bool thread = false)
{
    const auto file = test_environment::module_filename("package:/bin/MorphicPumpProvider");
    if (!context.post(module_request(module_ids::rendering, action, file.c_str(), pump, thread))) return false;
    for (const auto notice : { modules::EModuleNotice::acknowledged, modules::EModuleNotice::completed })
    {
        threading::CErasedPodMsg message;
        modules::ModuleResult result;
        if (!receive(context, message) || !message.copy_payload_to(result) || (message.query_async_slot() != 10) ||
            (result.notice != notice) || (result.status != ((notice == modules::EModuleNotice::acknowledged) ? modules::EModuleStatus::success : expected)))
        {
            MV_LOG(error, "Pump fixture: module result expected {} actual {}", static_cast<unsigned>(expected), static_cast<unsigned>(result.status));
            return false;
        }
    }
    return true;
}
static bool self_stop(threading::CThreadContext& context, const bool replace)
{
    const auto file = test_environment::module_filename("package:/bin/MorphicPumpReplacement");
    if (!context.post(module_request(module_ids::executive, replace ? modules::EModuleAction::replace : modules::EModuleAction::unload,
        replace ? file.c_str() : nullptr))) return false;
    platform::threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) return false;
    while (!context.exit_requested())
    {
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining) return false;
        platform::threading::processor_relax();
    }
    return true;
}
static bool threaded_credits(threading::CThreadContext& context)
{
    CAssetId assets[600];
    CAssetAcquisitionId acquisitions[600];
    threading::CErasedPodMsg message;
    for (unsigned index = 0u; index < 600u; ++index)
    {
        auto value = CErasedOwner::create<RawAssetTransfer>();
        auto* request = value.payload<RawAssetTransfer>();
        if (!request || !request->storage.value.resize(32u, 32u)) return false;
        threading::CErasedOwnerMsg transfer;
        transfer.set_message_type<RawAssetTransfer>();
        transfer.set_async_slot(210);
        transfer.set_owner(std::move(value));
        AssetResult asset;
        AssetAcquireResult acquired;
        if (!context.post(std::move(transfer)) || !receive(context, message) || !message.copy_payload_to(asset) ||
            asset.status != EAssetStatus::success ||
            !context.post(threading::make_pod_message(211, AssetAcquireRequest{asset.asset})) || !receive(context, message) ||
            !message.copy_payload_to(acquired) || acquired.status != EAssetStatus::success) return false;
        assets[index] = asset.asset;
        acquisitions[index] = acquired.acquisition;
    }
    for (const auto asset : assets)
        if (!context.post(threading::make_pod_message(212, AssetDisposeRequest{asset}))) return false;
    //  This same-queue marker replies only after all 600 retained disposals were admitted.
    if (!context.post(threading::make_pod_message(214, AssetAcquireRequest{})) || !receive(context, message) ||
        message.query_async_slot() != 214) return false;
    for (const auto acquisition : acquisitions)
        if (!context.post(threading::make_pod_message(213, AssetReleaseRequest{acquisition}))) return false;
    if (!self_stop(context, false)) return false;
    unsigned released = 0u, disposed = 0u;
    for (unsigned index = 0u; index < 1200u; ++index)
    {
        AssetReleaseResult release;
        AssetDisposeResult disposal;
        if (!receive(context, message)) return false;
        if (message.copy_payload_to(release) && release.status == EAssetStatus::success) ++released;
        else if (message.copy_payload_to(disposal) && disposal.status == EAssetStatus::success) ++disposed;
        else return false;
    }
    if (released != 600u || disposed != 600u) return false;
    MV_LOG(info, "Pump fixture: threaded 600 retained disposals, releases and self-exit passed");
    return true;
}
static bool borrowed_resource(threading::CThreadContext& context, const SServiceRoute route)
{
    CServiceBorrowClient consumer;
    if (!consumer.begin(route, { 1u }, 5u)) return false;
    ServiceRequest request;
    request.route = route;
    request.request.value = 1u;
    request.opcode = 5u;
    if (!context.post(threading::make_pod_message(70, request))) return false;
    SServiceBorrow borrow;
    bool terminal = false, releasing = false, quiescent = false;
    for (;;)
    {
        threading::CErasedPodMsg message;
        if (!receive(context, message)) return false;
        ServiceReply reply;
        ServiceBorrowControl control;
        if (message.copy_payload_to(reply))
        {
            if (!consumer.observe(reply)) return false;
            if (reply.notice == EServiceNotice::prepared)
            {
                borrow = consumer.borrow(route, { 1u });
                const auto view = consumer.view(borrow);
                if (view.descriptor != nullptr && (view.size != sizeof(unsigned) || view.version != 1u ||
                    *static_cast<const unsigned*>(view.descriptor) != 4242u)) return false;
                MV_LOG(info, "Pump fixture: borrowed descriptor observed before terminal result");
                if (scenario("borrow-cancel"))
                {
                    CancelServiceRequest cancel;
                    if (!consumer.cancel(route, { 1u }, cancel) || !context.post(threading::make_pod_message(-1, cancel))) return false;
                    releasing = true;
                }
            }
            if (reply.notice == EServiceNotice::completed)
            {
                if (reply.status != (scenario("borrow-cancel") ? EServiceStatus::cancelled : EServiceStatus::success)) return false;
                terminal = true;
                if (scenario("borrow-abandoned"))
                {
                    MV_LOG(info, "Pump fixture: consumer departed with a live borrow");
                    return self_stop(context, false);
                }
                if (!releasing && !scenario("borrow-provider-failed"))
                {
                    if (scenario("borrow-shutdown") && !self_stop(context, false)) return false;
                    ReleaseServiceBorrow release;
                    if (!consumer.release(borrow, release) || !context.post(threading::make_pod_message(-1, release))) return false;
                    releasing = true;
                }
            }
        }
        else if (message.copy_payload_to(control))
        {
            if (!consumer.observe(control)) return false;
            borrow = control.borrow;
            if (control.control == EServiceBorrowControl::closing) releasing = true;
        }
        else return false;
        if (releasing && !quiescent)
        {
            ServiceBorrowQuiescent release;
            if (consumer.quiesce(borrow, release))
            {
                if (!context.post(threading::make_pod_message(-1, release))) return false;
                quiescent = true;
            }
        }
        if (terminal && consumer.retired(borrow)) break;
    }
    if (!consumer.forget(route, { 1u })) return false;
    MV_LOG(info, "Pump fixture: borrowed resource retired after consumer quiescence");
    if (scenario("borrow-shutdown")) return true;
    if (!scenario("borrow-provider-failed"))
    {
        threading::CErasedPodMsg message;
        ServiceReply reply;
        if (!context.post(threading::make_pod_message(71, CloseServiceRequest{route, {2u}})) || !receive(context, message) ||
            !message.copy_payload_to(reply) || reply.status != EServiceStatus::success) return false;
    }
    return operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success) && self_stop(context, false);
}

static bool notified_resource(threading::CThreadContext& context, const SServiceRoute route)
{
    CServiceBorrowClient consumer;
    if (!consumer.begin(route, { 1u }, 6u)) return false;
    ServiceRequest acquisition;
    acquisition.route = route;
    acquisition.request.value = 1u;
    acquisition.opcode = 6u;
    if (!context.post(threading::make_pod_message(70, acquisition))) return false;
    SServiceBorrow borrow;
    const SNotificationData* data = nullptr;
    threading::SNotificationReader reader;
    bool terminal = false, ready = false, observed = false, releasing = false, quiescent = false;
    threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) return false;
    for (;;)
    {
        const auto epoch = context.observe_epoch();
        threading::CErasedPodMsg message;
        while (context.read(message))
        {
            ServiceReply reply;
            ServiceBorrowControl control;
            ServiceNotificationRegistered registration;
            if (message.copy_payload_to(reply))
            {
                if (reply.opcode == 7u)
                {
                    if (reply.status != EServiceStatus::success) return false;
                    if (reply.notice == EServiceNotice::completed) ready = true;
                }
                else
                {
                    if (!consumer.observe(reply)) return false;
                    if (reply.notice == EServiceNotice::prepared)
                    {
                        borrow = consumer.borrow(route, { 1u });
                        const auto view = consumer.view(borrow);
                        if (view.size != sizeof(SNotificationData) || view.version != 1u || !view.descriptor) return false;
                        data = static_cast<const SNotificationData*>(view.descriptor);
                        RegisterServiceNotification request;
                        if (!consumer.request_notification(borrow, request) ||
                            !context.post(threading::make_pod_message(72, request))) return false;
                    }
                    if (reply.notice == EServiceNotice::completed)
                    {
                        if (reply.status != (scenario("notify-cancel") ? EServiceStatus::cancelled : EServiceStatus::success)) return false;
                        terminal = true;
                    }
                }
            }
            else if (message.copy_payload_to(registration))
            {
                if (!consumer.observe(registration) || registration.status != EServiceStatus::success) return false;
                reader = consumer.notification(borrow);
                if (!reader.is_valid()) return false;
                ServiceRequest request;
                request.route = route;
                request.request.value = 2u;
                request.opcode = 7u;
                const SNotificationReady payload{ borrow, consumer.registration(borrow) };
                request.payload_size = sizeof(payload);
                std::memcpy(request.payload, &payload, sizeof(payload));
                if (!context.post(threading::make_pod_message(73, request))) return false;
            }
            else if (message.copy_payload_to(control))
            {
                if (!consumer.observe(control)) return false;
                if (control.control == EServiceBorrowControl::closing) releasing = true;
            }
            else return false;
        }
        if (!releasing && reader.is_valid() && ready && !observed)
        {
            if (reader.failed()) return false;
            if (data->value.load(std::memory_order_acquire) != 0u)
            {
                observed = true;
                MV_LOG(info, "Pump fixture: registered notification published durable data before wake");
            }
        }
        if (observed && !releasing && (terminal || scenario("notify-cancel")))
        {
            //  Leave cancellation pending deliberately. Other cases require a
            //  fresh final signal, including after the old consumer has exited.
            if (!scenario("notify-cancel")) reader.clear_pending();
            if (scenario("notify-abandoned")) return self_stop(context, false);
#if !MV_PUMP_REPLACEMENT
            if (scenario("notify-replacement")) return self_stop(context, true);
#endif
            if (scenario("notify-shutdown") && !self_stop(context, false)) return false;
            if (scenario("notify-cancel"))
            {
                CancelServiceRequest cancel;
                if (!consumer.cancel(route, { 1u }, cancel) || !context.post(threading::make_pod_message(-1, cancel))) return false;
            }
            else
            {
                ReleaseServiceBorrow release;
                if (!consumer.release(borrow, release) || !context.post(threading::make_pod_message(-1, release))) return false;
            }
            releasing = true;
        }
        if (releasing && !quiescent)
        {
            reader = {};
            data = nullptr;
            ServiceBorrowQuiescent proof;
            if (!consumer.quiesce(borrow, proof) || !context.post(threading::make_pod_message(-1, proof))) return false;
            quiescent = true;
        }
        if (terminal && consumer.retired(borrow)) break;
        if (reader.is_valid())
        {
            reader.clear_pending();
            if (reader.failed()) return false;
            if (!observed && ready && data->value.load(std::memory_order_acquire) != 0u) continue;
        }
        //  Messages and exit share this sampled epoch with external notifications.
        //  An exit request does not end cleanup; its messages continue to wake us.
        if (context.wait_for_new_epoch(epoch, deadline).status != threading::EWaitStatus::satisfied) return false;
    }
    if (!consumer.forget(route, { 1u })) return false;
    MV_LOG(info, "Pump fixture: notification retired after final publisher signal");
    if (scenario("notify-shutdown")) return true;
    threading::CErasedPodMsg message;
    ServiceReply closed;
    if (!context.post(threading::make_pod_message(74, CloseServiceRequest{ route, { 3u } })) ||
        !receive(context, message) || !message.copy_payload_to(closed) || closed.status != EServiceStatus::success) return false;
    return operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success) && self_stop(context, false);
}

static bool run(threading::CThreadContext& context)
{
    const bool rejected = scenario("bad-size") || scenario("bad-version") || scenario("bad-flags") ||
        scenario("bad-callback") || scenario("mixed") || scenario("bad-provider-size") ||
        scenario("bad-provider-version") || scenario("bad-provider-flags") || scenario("bad-provider-count") ||
        scenario("bad-provider-null") || scenario("bad-provider-contract") || scenario("bad-provider-duplicate") ||
        scenario("bad-provider-query");
    const bool activation_failure = scenario("activation-failed") || scenario("activation-null") || scenario("activation-timeout");
    if (rejected || activation_failure)
        return operation(context, modules::EModuleAction::load, rejected ? modules::EModuleStatus::function_unavailable : modules::EModuleStatus::installation_failed) && self_stop(context, false);
#if MV_PUMP_REPLACEMENT
    if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::already_loaded)) return false;
    MV_LOG(info, "Pump fixture: independent provider survived Executive replacement");
#else
    if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::success)) return false;
    if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::already_loaded) ||
        !operation(context, modules::EModuleAction::load, modules::EModuleStatus::function_unavailable, false, true) ||
        !operation(context, modules::EModuleAction::load, modules::EModuleStatus::invalid_request, true, true)) return false;
#endif
    threading::CErasedPodMsg message;
    if (!context.post(threading::make_pod_message(20, OpenServiceRequest{module_ids::rendering, 1u, 1u, {1u}})) ||
        !receive(context, message)) return false;
    OpenServiceResult opened;
    if (scenario("empty-provider"))
    {
        return message.copy_payload_to(opened) && (opened.status == EServiceStatus::unsupported) &&
            operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success) && self_stop(context, false);
    }
    if (!message.copy_payload_to(opened) || (opened.status != EServiceStatus::success) || !opened.provider.value) return false;
    if (borrow_scenario()) return borrowed_resource(context, opened.route);
    if (notification_scenario()) return notified_resource(context, opened.route);
    if (!operation(context, modules::EModuleAction::unload, modules::EModuleStatus::busy)) return false;
    constexpr unsigned count = 3u;
    for (unsigned index = 0u; index < count; ++index)
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = index + 1u;
        request.opcode = 1u;
        request.payload_size = 1u;
        request.payload[0] = static_cast<unsigned char>(index);
        if (!context.post(threading::make_pod_message(30 + index, request))) return false;
    }
    bool acknowledged[count]{};
    bool completed[count]{};
    for (unsigned index = 0u; index < count * 2u; ++index)
    {
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.route.value != opened.route.value) ||
            !reply.request.value || (reply.request.value > count)) return false;
        const auto ordinal = static_cast<unsigned>(reply.request.value - 1u);
        if (message.query_async_slot() != 30 + static_cast<int>(ordinal)) return false;
        if (reply.notice == EServiceNotice::acknowledged)
        {
            if (acknowledged[ordinal] || (reply.status != EServiceStatus::success)) return false;
            acknowledged[ordinal] = true;
        }
        else
        {
            if (!acknowledged[ordinal] || completed[ordinal]) return false;
            if (scenario("invalid-prefix") || scenario("pump-failed"))
            {
                if (reply.status != EServiceStatus::cancelled) return false;
            }
            else if ((reply.status != EServiceStatus::success) || (reply.payload_size != 1u) || (reply.payload[0] != ordinal)) return false;
            completed[ordinal] = true;
        }
    }
    if (scenario("invalid-prefix") || scenario("pump-failed"))
        return operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success) && self_stop(context, false);

    //  One provider, simultaneous contracts, identical opcode and request id.
    if (!context.post(threading::make_pod_message(21, OpenServiceRequest{module_ids::rendering, 2u, 3u, {2u}})) ||
        !receive(context, message)) return false;
    OpenServiceResult second;
    if (!message.copy_payload_to(second) || second.status != EServiceStatus::success || second.route.value == opened.route.value) return false;
    ServiceRequest other;
    other.route = second.route;
    other.request.value = 1u;
    other.opcode = 1u;
    other.payload_size = 1u;
    if (!context.post(threading::make_pod_message(31, other))) return false;
    for (const auto notice : {EServiceNotice::acknowledged, EServiceNotice::completed})
    {
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || reply.notice != notice || reply.status != EServiceStatus::success ||
            (notice == EServiceNotice::completed && (reply.payload_size != 1u || reply.payload[0] != 33u))) return false;
    }
    ServiceReply second_closed;
    if (!context.post(threading::make_pod_message(22, CloseServiceRequest{second.route, {2u}})) || !receive(context, message) ||
        !message.copy_payload_to(second_closed) || second_closed.status != EServiceStatus::success) return false;
    MV_LOG(info, "Pump fixture: simultaneous contracts preserved number and version");

    if (!context.post(threading::make_pod_message(40, modules::RunTestsRequest{module_ids::rendering})) || !receive(context, message)) return false;
    modules::RunTestsResult tests;
    if (!message.copy_payload_to(tests) || (tests.status != modules::ETestRunStatus::success) || tests.passed != 5u) return false;
    if (scenario("cleanup-credits"))
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 4u;
        request.opcode = 4u;
        if (!context.post(threading::make_pod_message(44, request))) return false;
        for (const auto notice : {EServiceNotice::acknowledged, EServiceNotice::completed})
        {
            ServiceReply reply;
            if (!receive(context, message) || !message.copy_payload_to(reply) || reply.notice != notice || reply.status != EServiceStatus::success) return false;
        }
        if (!context.post(threading::make_pod_message(41, modules::RunTestsRequest{module_ids::rendering})) || !receive(context, message) ||
            !message.copy_payload_to(tests) || tests.status != modules::ETestRunStatus::success || tests.passed != 0u) return false;
        MV_LOG(info, "Pump fixture: zero-reply completion admitted with 512 reserved replies");
        return self_stop(context, false);
    }
    if (scenario("threaded-credits")) return threaded_credits(context);
    if (scenario("inbox-full") || scenario("emergency"))
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 4u;
        request.opcode = 3u;
        if (!context.post(threading::make_pod_message(44, request))) return false;
        for (const auto notice : {EServiceNotice::acknowledged, EServiceNotice::completed})
        {
            ServiceReply reply;
            if (!receive(context, message) || !message.copy_payload_to(reply) || reply.notice != notice || reply.status != EServiceStatus::success) return false;
        }
        if (scenario("emergency")) debug_system::get_service()->request_shutdown(debug_system::EShutdownReason::critical_incident);
        return self_stop(context, false);
    }
    if (scenario("shutdown-pending") || (scenario("replacement") && !MV_PUMP_REPLACEMENT))
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 4u;
        request.opcode = 2u;
        if (!context.post(threading::make_pod_message(44, request)) || !receive(context, message)) return false;
        ServiceReply ack;
        if (!message.copy_payload_to(ack) || (ack.notice != EServiceNotice::acknowledged)) return false;
        if (!self_stop(context, scenario("replacement"))) return false;
        if (scenario("replacement")) return true; //  The old requester leaves a live, delivered operation.
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.status != EServiceStatus::success)) return false;
        MV_LOG(info, "Pump fixture: cleanup reply received after Executive exit request");
    }
    if (!context.post(threading::make_pod_message(50, CloseServiceRequest{opened.route, {5u}})) || !receive(context, message)) return false;
    ServiceReply closed;
    if (!message.copy_payload_to(closed) || (closed.status != EServiceStatus::success)) return false;
    if (scenario("shutdown-pending")) return true;
    if (scenario("stop-timeout")) return self_stop(context, false);
    if (!operation(context, modules::EModuleAction::unload, scenario("deactivate-failed") ? modules::EModuleStatus::unload_failed : modules::EModuleStatus::success)) return false;
    if (scenario("normal"))
    {
        if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::success) ||
            !context.post(threading::make_pod_message(60, OpenServiceRequest{module_ids::rendering, 1u, 1u, {1u}})) || !receive(context, message)) return false;
        OpenServiceResult replacement;
        if (!message.copy_payload_to(replacement) || replacement.status != EServiceStatus::success ||
            replacement.provider.value == opened.provider.value || replacement.route.value == opened.route.value) return false;
        if (!context.post(threading::make_pod_message(61, CloseServiceRequest{opened.route, {6u}})) || !receive(context, message) ||
            !message.copy_payload_to(closed) || closed.status != EServiceStatus::stale) return false;
        if (!context.post(threading::make_pod_message(62, CloseServiceRequest{replacement.route, {2u}})) || !receive(context, message) ||
            !message.copy_payload_to(closed) || closed.status != EServiceStatus::success ||
            !operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success)) return false;
        MV_LOG(info, "Pump fixture: stale route rejected after provider reload");
    }
    return self_stop(context, false);
}
static std::uint32_t MV_STD_ABI_CALL executive_entry(void* data) noexcept
{
    threading::CThreadContext context{*static_cast<threading::CThreadResources*>(data)};
    context.startup();
    context.mark_running();
    const bool success = run(context);
    if (success)
    {
        MV_LOG_RICH(info, "Pump fixture: %s passed", selected());
        context.mark_exiting();
        context.mark_exited();
    }
    else { MV_LOG(error, "Pump fixture failed"); context.mark_failed(1u); }
    return success ? 0u : 1u;
}
#else
static constexpr auto k_advertised_module_id = module_ids::rendering;
struct SNotifier
{
    SNotificationData data;
    threading::SNotificationEndpoint endpoint;
    platform::threading::CThread publisher;
    std::atomic<bool> finish{ false }, done{ false }, failed{ false };
};

static std::uint32_t MV_STD_ABI_CALL publish_notifications(void* data) noexcept
{
    auto& notifier = *static_cast<SNotifier*>(data);
    notifier.data.value.store(1u, std::memory_order_release);
    if (!notifier.endpoint.signal()) notifier.failed.store(true, std::memory_order_release);
    while (!notifier.finish.load(std::memory_order_acquire)) platform::threading::processor_relax();
    notifier.data.value.store(2u, std::memory_order_release);
    if (!notifier.endpoint.signal()) notifier.failed.store(true, std::memory_order_release);
    notifier.done.store(true, std::memory_order_release);
    return 0u;
}

struct SRuntime
{
    TInstance<SNotifier> notifier;
    SClientToken notification_client;
    TInstance<unsigned> borrowed;
    SServiceBorrow borrow;
    ServiceReply borrow_result;
    bool borrow_done{ false }, borrower_quiescent{ false }, borrow_fail{ false };
    unsigned retirement_progress{ 0u };
    const SHostPumpContext* context{nullptr};
    CByteBuffer storage;
    std::uint32_t starting{0u}, stopping{0u}, delayed{0u}, flooded{0u};
    std::int32_t test_slot{-1};
    ServiceReply pending;
    CAssetId asset;
    bool stop{false};
    bool flood{false}, flood_complete{false};
    CAssetId assets[512];
    CAssetAcquisitionId acquisitions[512];
    unsigned created{0u}, disposals_posted{0u}, releases_posted{0u}, released{0u}, disposed{0u}, credits_wait{0u};
    bool credits{false};
    platform::threading::CWaitDeadline stop_deadline;
};
static TInstance<SRuntime>& runtime_state() noexcept
{
    //  Construct only after the module memory context has been installed.
    static TInstance<SRuntime> state;
    return state;
}
struct SForeignCall
{
    const SHostPumpContext* context;
    EServiceStatus result{EServiceStatus::success};
    EServiceStatus borrow_result{EServiceStatus::success};
};
static std::uint32_t MV_STD_ABI_CALL foreign_call(void* data) noexcept
{
    auto& call = *static_cast<SForeignCall*>(data);
    const auto message = threading::make_pod_message(90, AssetDisposeRequest{});
    call.result = call.context->services->post_pod(call.context->service_context, &message);
    const auto& services = *call.context->services;
    const SServiceBorrowOffer offer{};
    SServiceBorrow borrow;
    call.borrow_result = services.prepare_borrow(call.context->service_context, &offer, &borrow);
    return 0u;
}
static bool context_valid(const SHostPumpContext* context) noexcept
{
    return context && modules::is_thread_context_ready(context) &&
        context->services && context->services->size == sizeof(SHostPumpServices) &&
        context->services->version == 1u && context->services->post_pod &&
        context->services->post_owner && context->services->complete &&
        context->services->prepare_borrow && context->services->begin_borrow_retirement &&
        context->services->finish_borrow_retirement && context->services->bind_borrow_notification &&
        system_context::get_ambient_thread_id() == thread_ids::host &&
        system_context::get_ambient_module_id() == module_ids::rendering &&
        memory::get_ambient_memory_context() != nullptr;
}
template<typename T> static bool post(SRuntime& state, const int slot, const T& payload)
{
    const auto message = threading::make_pod_message(slot, payload);
    return state.context->services->post_pod(state.context->service_context, &message) == EServiceStatus::success;
}
template<typename T> static bool owner(SRuntime& state, CErasedOwner&& value, const int slot)
{
    threading::CErasedOwnerMsg message;
    message.set_message_type<T>();
    message.set_async_slot(slot);
    message.set_owner(std::move(value));
    return state.context->services->post_owner(state.context->service_context, &message) == EServiceStatus::success && !message.has_owner();
}
static ECallStatus MV_STD_ABI_CALL activate(const SHostPumpContext* context, void** runtime, SProgress* progress) noexcept
{
    if (!context_valid(context) || !runtime_state().emplace()) return ECallStatus::failed;
    auto& state = *runtime_state();
    state.context = context;
    if (!state.storage.resize(32u, 32u)) return ECallStatus::failed;
    SForeignCall foreign{context};
    platform::threading::CThread other;
    if (!other.create(&foreign_call, &foreign) || !other.join_and_close() || foreign.result != EServiceStatus::invalid_request ||
        foreign.borrow_result != EServiceStatus::invalid_request) return ECallStatus::failed;
    if (scenario("activation-null")) { runtime_state().reset(); return ECallStatus::failed; }
    *runtime = &state;
    progress->phase = EPumpPhase::starting;
    MV_LOG(info, "Pump provider: activation context and foreign-thread rejection passed");
    return scenario("activation-failed") ? ECallStatus::failed : ECallStatus::success;
}
static bool test_message(SRuntime& state, const threading::CErasedPodMsg& message)
{
    if (message.is_payload_a<ServiceRequestCancelled>()) return true;
    const auto slot = message.query_async_slot();
    if (message.is_payload_a<modules::RunTestsRequest>())
    {
        state.test_slot = slot;
        auto value = CErasedOwner::create<filesystem::FilesystemRefreshRequest>();
        auto* request = value.payload<filesystem::FilesystemRefreshRequest>();
        return request && request->root.set("dev-source:") && owner<filesystem::FilesystemRefreshRequest>(state, std::move(value), 101);
    }
    if (slot == 101)
    {
        filesystem::FilesystemRefreshResult result;
        if (!message.copy_payload_to(result) || result.status != filesystem::EFilesystemStatus::success) return false;
        auto value = CErasedOwner::create<RawAssetTransfer>();
        auto* request = value.payload<RawAssetTransfer>();
        return request && request->storage.value.resize(32u, 32u) && owner<RawAssetTransfer>(state, std::move(value), 102);
    }
    if (slot == 102)
    {
        AssetResult result;
        if (!message.copy_payload_to(result) || result.status != EAssetStatus::success) return false;
        state.asset = result.asset;
        return post(state, 103, AssetAcquireRequest{state.asset});
    }
    if (slot == 103)
    {
        AssetAcquireResult result;
        return message.copy_payload_to(result) && result.status == EAssetStatus::success && post(state, 104, AssetReleaseRequest{result.acquisition});
    }
    if (slot == 104)
    {
        AssetReleaseResult result;
        return message.copy_payload_to(result) && result.status == EAssetStatus::success && post(state, 105, AssetDisposeRequest{state.asset});
    }
    if (slot == 105)
    {
        AssetDisposeResult result;
        if (!message.copy_payload_to(result) || result.status != EAssetStatus::success) return false;
        auto request = module_request(module_ids::rendering, modules::EModuleAction::unload);
        request.set_async_slot(106);
        return state.context->services->post_owner(state.context->service_context, &request) == EServiceStatus::success;
    }
    modules::ModuleResult result;
    if (!message.copy_payload_to(result)) return false;
    if (result.notice == modules::EModuleNotice::acknowledged) return result.status == modules::EModuleStatus::success;
    if (result.status != modules::EModuleStatus::invalid_request) return false;
    if (slot == 106)
    {
        auto request = module_request(module_ids::executive, modules::EModuleAction::unload);
        request.set_async_slot(107);
        return state.context->services->post_owner(state.context->service_context, &request) == EServiceStatus::success;
    }
    if (slot != 107) return false;
    MV_LOG(info, "Pump provider: refresh, assets, acquisitions and requester restrictions passed");
    return post(state, state.test_slot, modules::RunTestsResult{module_ids::rendering, modules::ETestRunStatus::success, 5u});
}
static bool create_credit_asset(SRuntime& state)
{
    auto value = CErasedOwner::create<RawAssetTransfer>();
    auto* request = value.payload<RawAssetTransfer>();
    return request && request->storage.value.resize(32u, 32u) && owner<RawAssetTransfer>(state, std::move(value), 210);
}
static bool credit_message(SRuntime& state, const threading::CErasedPodMsg& message)
{
    const auto slot = message.query_async_slot();
    if (slot == 210)
    {
        AssetResult asset;
        if (!message.copy_payload_to(asset) || asset.status != EAssetStatus::success || state.created >= 512u) return false;
        state.assets[state.created] = asset.asset;
        return post(state, 211, AssetAcquireRequest{asset.asset});
    }
    if (slot == 211)
    {
        AssetAcquireResult acquired;
        if (!message.copy_payload_to(acquired) || acquired.status != EAssetStatus::success || state.created >= 512u) return false;
        state.acquisitions[state.created++] = acquired.acquisition;
        return state.created == 512u || create_credit_asset(state);
    }
    AssetReleaseResult release;
    AssetDisposeResult disposal;
    if (slot == 213 && message.copy_payload_to(release) && release.status == EAssetStatus::success) { ++state.released; return true; }
    if (slot == 212 && message.copy_payload_to(disposal) && disposal.status == EAssetStatus::success) { ++state.disposed; return true; }
    if (message.is_payload_a<modules::RunTestsRequest>()) return post(state, slot, modules::RunTestsResult{module_ids::rendering, modules::ETestRunStatus::success, 0u});
    return false;
}
static ECallStatus MV_STD_ABI_CALL pump(void* runtime, const SPumpInput* input, SPumpResult* result) noexcept
{
    auto& state = *static_cast<SRuntime*>(runtime);
    if (!context_valid(state.context)) return ECallStatus::failed;
    result->progress.phase = EPumpPhase::ready;
    if (notification_scenario())
    {
        const auto& services = *state.context->services;
        for (std::uint32_t index = 0u; index < input->message_count; ++index)
        {
            ServiceBorrowControl control;
            ServiceRequestCancelled cancel;
            if (input->messages[index].copy_payload_to(control))
            {
                if (control.borrow.value != state.borrow.value) return ECallStatus::failed;
                if (control.control == EServiceBorrowControl::consumer_quiescent) state.borrower_quiescent = true;
            }
            else if (!input->messages[index].copy_payload_to(cancel)) return ECallStatus::failed;
            ++result->consumed_messages;
        }
        if (state.notifier && state.borrower_quiescent)
        {
            state.notifier->finish.store(true, std::memory_order_release);
            if (state.notifier->done.load(std::memory_order_acquire))
            {
                if (!state.notifier->publisher.join_and_close() || state.notifier->failed.load(std::memory_order_acquire)) return ECallStatus::failed;
                MV_LOG(info, "Pump provider: final notification signal drained after consumer quiescence");
                state.notifier.reset();
                if (services.finish_borrow_retirement(state.context->service_context, state.borrow) != EServiceStatus::success) return ECallStatus::failed;
                if (!state.borrow_done && services.complete(state.context->service_context, &state.borrow_result) != EServiceStatus::success) return ECallStatus::failed;
                state.borrow_done = true;
            }
        }
        if (input->command_count)
        {
            const auto& dispatched = input->commands[0];
            const auto& command = dispatched.command;
            if (command.opcode == 6u)
            {
                if (state.notifier || !state.notifier.emplace()) return ECallStatus::failed;
                state.borrower_quiescent = state.borrow_done = false;
                state.notification_client = dispatched.client;
                const SServiceBorrowOffer offer{ command.route, command.request, { &state.notifier->data, sizeof(SNotificationData), 1u } };
                if (services.prepare_borrow(state.context->service_context, &offer, &state.borrow) != EServiceStatus::success) return ECallStatus::failed;
                state.borrow_result = {};
                state.borrow_result.route = command.route;
                state.borrow_result.request = command.request;
                state.borrow_result.opcode = command.opcode;
                state.borrow_result.status = EServiceStatus::success;
            }
            else if (command.opcode == 7u)
            {
                SNotificationReady ready;
                if (!state.notifier || command.payload_size != sizeof(ready) || dispatched.client.value != state.notification_client.value ||
                    command.route.value != state.borrow_result.route.value) return ECallStatus::failed;
                std::memcpy(&ready, command.payload, sizeof(ready));
                if (ready.borrow.value != state.borrow.value ||
                    services.bind_borrow_notification(state.context->service_context, ready.borrow, ready.registration, &state.notifier->endpoint) != EServiceStatus::success ||
                    !state.notifier->publisher.create(&publish_notifications, &*state.notifier)) return ECallStatus::failed;
                ServiceReply reply;
                reply.route = command.route;
                reply.request = command.request;
                reply.opcode = command.opcode;
                reply.status = EServiceStatus::success;
                if (services.complete(state.context->service_context, &reply) != EServiceStatus::success) return ECallStatus::failed;
                if (!scenario("notify-cancel"))
                {
                    if (services.complete(state.context->service_context, &state.borrow_result) != EServiceStatus::success) return ECallStatus::failed;
                    state.borrow_done = true;
                }
            }
            else return ECallStatus::failed;
            result->consumed_commands = 1u;
        }
        if (!state.stop) return ECallStatus::success;
        if (state.notifier) { result->progress.phase = EPumpPhase::stopping; return ECallStatus::success; }
    }
    if (borrow_scenario())
    {
        const auto& services = *state.context->services;
        for (std::uint32_t index = 0u; index < input->message_count; ++index)
        {
            ServiceBorrowControl control;
            ServiceRequestCancelled cancel;
            if (input->messages[index].copy_payload_to(control))
            {
                if (control.borrow.value != state.borrow.value) return ECallStatus::failed;
                if (control.control == EServiceBorrowControl::consumer_quiescent)
                {
                    if (control.quiescence != (scenario("borrow-abandoned") ? EServiceQuiescence::execution_ended : EServiceQuiescence::relinquished))
                        return ECallStatus::failed;
                    state.borrower_quiescent = true;
                    MV_LOG(info, "Pump provider: consumer quiescence received; draining publisher activity");
                }
            }
            else if (!input->messages[index].copy_payload_to(cancel)) return ECallStatus::failed;
            ++result->consumed_messages;
        }
        if (state.borrowed && state.borrower_quiescent && ++state.retirement_progress >= 3u)
        {
            state.borrowed.reset();
            if (services.finish_borrow_retirement(state.context->service_context, state.borrow) != EServiceStatus::success)
                return ECallStatus::failed;
            if (!state.borrow_done)
            {
                if (services.complete(state.context->service_context, &state.borrow_result) != EServiceStatus::success) return ECallStatus::failed;
                state.borrow_done = true;
            }
            MV_LOG(info, "Pump provider: provider-owned storage destroyed after publisher drain");
        }
        if (state.borrow_fail)
        {
            state.borrow_fail = false;
            return ECallStatus::failed;
        }
        if (state.stop && state.borrowed)
        {
            result->progress.phase = EPumpPhase::stopping;
            return ECallStatus::success;
        }
        if (input->command_count)
        {
            const auto& command = input->commands[0].command;
            if (command.opcode != 5u || !state.borrowed.emplace(4242u)) return ECallStatus::failed;
            const SServiceBorrowOffer offer{ command.route, command.request, { state.borrowed.operator->(), sizeof(unsigned), 1u } };
            if (services.prepare_borrow(state.context->service_context, &offer, &state.borrow) != EServiceStatus::success) return ECallStatus::failed;
            state.borrow_result.route = command.route;
            state.borrow_result.request = command.request;
            state.borrow_result.opcode = command.opcode;
            state.borrow_result.status = EServiceStatus::success;
            if (!scenario("borrow-cancel"))
            {
                if (services.complete(state.context->service_context, &state.borrow_result) != EServiceStatus::success) return ECallStatus::failed;
                state.borrow_done = true;
            }
            state.borrow_fail = scenario("borrow-provider-failed");
            result->consumed_commands = 1u;
        }
        if (!state.stop) return ECallStatus::success;
    }
    if (state.stop)
    {
        if (state.credits)
        {
            for (unsigned count = 0u; count < 16u && state.releases_posted < 512u; ++count)
            {
                if (!post(state, 213, AssetReleaseRequest{state.acquisitions[state.releases_posted]})) break;
                ++state.releases_posted;
            }
            for (unsigned index = 0u; index < input->message_count; ++index)
            {
                if (!credit_message(state, input->messages[index])) return ECallStatus::failed;
                ++result->consumed_messages;
            }
            if (state.released != 512u || state.disposed != 512u) { result->progress.phase = EPumpPhase::stopping; return ECallStatus::success; }
            if (state.stopping == 0u) MV_LOG(info, "Pump provider: 512 retained disposals completed through independent cleanup credits");
        }
        if (scenario("stop-timeout"))
        {
            std::uint64_t remaining = 0u;
            if (state.stop_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) return ECallStatus::failed;
            if (remaining) { result->progress.phase = EPumpPhase::stopping; return ECallStatus::success; }
            if (state.stopping == 0u) MV_LOG(info, "Pump provider: live state survived shutdown grace expiry");
        }
        result->progress.phase = (++state.stopping >= 3u) ? EPumpPhase::stopped : EPumpPhase::stopping;
        if (result->progress.phase == EPumpPhase::stopped) MV_LOG(info, "Pump provider: stopped after continued progress");
        return ECallStatus::success;
    }
    if (scenario("activation-timeout") || ++state.starting < 3u) { result->progress.phase = EPumpPhase::starting; return ECallStatus::success; }
    if (state.credits && state.created == 512u && state.credits_wait < 16u)
    {
        for (unsigned count = 0u; count < 64u && state.disposals_posted < 512u; ++count)
        {
            if (!post(state, 212, AssetDisposeRequest{state.assets[state.disposals_posted]})) break;
            ++state.disposals_posted;
        }
        if (state.disposals_posted == 512u && ++state.credits_wait == 16u)
        {
            if (state.context->services->complete(state.context->service_context, &state.pending) != EServiceStatus::success) return ECallStatus::failed;
            MV_LOG(info, "Pump provider: 512 disposal credits retained by live acquisitions");
        }
    }
    if (state.delayed && (--state.delayed == 0u))
    {
        if (state.context->services->complete(state.context->service_context, &state.pending) != EServiceStatus::success) return ECallStatus::failed;
        MV_LOG(info, "Pump provider: delayed operation completed");
    }
    if (state.flood && !state.flood_complete)
    {
        bool busy = false;
        for (unsigned index = 0u; index < 256u; ++index)
        {
            if (!post(state, 200, AssetDisposeRequest{})) { busy = true; break; }
            ++state.flooded;
        }
        if (busy && state.flooded >= 700u)
        {
            if (state.context->services->complete(state.context->service_context, &state.pending) != EServiceStatus::success) return ECallStatus::failed;
            state.flood_complete = true;
            MV_LOG(info, "Pump provider: full inbox and outbound backpressure established");
        }
    }
    if (input->command_count)
    {
        if (scenario("invalid-prefix")) { result->consumed_commands = input->command_count + 1u; return ECallStatus::success; }
        if (scenario("pump-failed")) return ECallStatus::failed;
        const auto& request = input->commands[0];
        if (!request.requester.value || !request.client.value) return ECallStatus::failed;
        if ((request.contract.number != 1u || request.contract.version != 1u) &&
            (request.contract.number != 2u || request.contract.version != 3u)) return ECallStatus::failed;
        ServiceReply reply;
        reply.route = request.command.route;
        reply.request = request.command.request;
        reply.opcode = request.command.opcode;
        reply.status = EServiceStatus::success;
        reply.payload_size = request.command.payload_size;
        std::memcpy(reply.payload, request.command.payload, sizeof(reply.payload));
        if (request.contract.number == 2u) reply.payload[0] = 33u;
        if (reply.opcode == 4u) { state.pending = reply; state.credits = true; if (!create_credit_asset(state)) return ECallStatus::failed; }
        else if (reply.opcode == 3u) { state.pending = reply; state.flood = true; }
        else if (reply.opcode == 2u) { state.pending = reply; state.delayed = 10000u; }
        else if (state.context->services->complete(state.context->service_context, &reply) != EServiceStatus::success) return ECallStatus::failed;
        result->consumed_commands = 1u; //  Retain the rest of the borrowed batch.
    }
    if (input->message_count && !state.flood)
    {
        if (!(state.credits ? credit_message(state, input->messages[0]) : test_message(state, input->messages[0]))) return ECallStatus::failed;
        result->consumed_messages = 1u;
    }
    return ECallStatus::success;
}
static ECallStatus MV_STD_ABI_CALL begin_stop(void* runtime, EStopReason reason, SProgress* progress) noexcept
{
    auto& state = *static_cast<SRuntime*>(runtime);
    if (!context_valid(state.context)) return ECallStatus::failed;
    state.stop = true;
    if (scenario("activation-timeout"))
    {
        if (reason != EStopReason::activation_failure) return ECallStatus::failed;
        MV_LOG(info, "Pump provider: activation deadline initiated cooperative stop");
    }
    if (scenario("stop-timeout") && state.stop_deadline.set_after_milliseconds(11000u).code != platform::threading::EWaitError::none)
        return ECallStatus::failed;
    progress->phase = EPumpPhase::stopping;
    MV_LOG(info, "Pump provider: begin stop");
    return ECallStatus::success;
}
static ECallStatus MV_STD_ABI_CALL deactivate(void* runtime) noexcept
{
    auto& state = *static_cast<SRuntime*>(runtime);
    if (!context_valid(state.context) || state.stopping < 3u) return ECallStatus::failed;
    const auto forbidden = threading::make_pod_message(90, AssetDisposeRequest{});
    if (state.context->services->post_pod(state.context->service_context, &forbidden) != EServiceStatus::closing) return ECallStatus::failed;
    if (scenario("deactivate-failed")) { MV_LOG(info, "Pump provider: quiescent state retained"); return ECallStatus::failed; }
    runtime_state().reset();
    MV_LOG(info, "Pump provider: deactivated");
    return ECallStatus::success;
}
static std::uint32_t MV_STD_ABI_CALL forbidden_thread(void*) noexcept { return 1u; }
#endif

static constexpr std::uint32_t k_advertised_version_minor{0u};
static EBindingResult MV_STD_ABI_CALL query_function(system_type_id type, std::uint32_t, FModuleFunction* function) noexcept
{
    if (!function) return EBindingResult::invalid_argument;
    *function = nullptr;
#if MV_PUMP_EXECUTIVE
    if (type == system_type_ids::executive_thread_function)
    {
        *function = reinterpret_cast<FModuleFunction>(&executive_entry);
        return EBindingResult::success;
    }
#endif
    return EBindingResult::unsupported_function;
}
static constexpr SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();
static CModuleBindingContext s_binding{k_binding_config};

}   //  namespace pump_tests

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v3(modules::SBootstrapFunctions* functions) noexcept
{
    return pump_tests::s_binding.bootstrap(functions);
}
#if !MV_PUMP_EXECUTIVE
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_host_pump_v1(std::uint32_t capacity, modules::SHostPumpV1* value) noexcept
{
    using namespace pump_tests;
    if (capacity < sizeof(*value) || !value) return EBindingResult::invalid_argument;
    *value = {sizeof(*value), 1u, 0u, &activate, &pump, &begin_stop, &deactivate};
    if (scenario("bad-size")) --value->size;
    if (scenario("bad-version")) ++value->version;
    if (scenario("bad-flags")) value->flags = 1u;
    if (scenario("bad-callback")) value->pump = nullptr;
    return EBindingResult::success;
}
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_thread_v1(modules::SServiceThread* value) noexcept
{
    *value = {};
    if (!pump_tests::scenario("mixed")) return modules::EBindingResult::unsupported_function;
    *value = {thread_ids::rendering, &pump_tests::forbidden_thread};
    return modules::EBindingResult::success;
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_provider_v1(
    std::uint32_t capacity, modules::SServiceProviderV1* value) noexcept
{
    using namespace pump_tests;
    if (value == nullptr || capacity != sizeof(*value)) return EBindingResult::invalid_argument;
    static const SServiceContract contracts[]{ {1u, 1u}, {2u, 3u} };
    static const SServiceContract invalid[]{ {1u, 0u} };
    static const SServiceContract duplicate[]{ {1u, 1u}, {1u, 2u} };
    *value = {sizeof(*value), 1u, 0u, 2u, contracts};
    if (scenario("bad-provider-size")) --value->size;
    if (scenario("bad-provider-version")) ++value->version;
    if (scenario("bad-provider-flags")) value->flags = 1u;
    if (scenario("bad-provider-count")) value->contract_count = 17u;
    if (scenario("bad-provider-null")) value->contracts = nullptr;
    if (scenario("bad-provider-contract")) { value->contracts = invalid; value->contract_count = 1u; }
    if (scenario("bad-provider-duplicate")) value->contracts = duplicate;
    if (scenario("bad-provider-query")) return EBindingResult::unsupported_function;
    if (scenario("empty-provider")) { value->contracts = nullptr; value->contract_count = 0u; }
    return EBindingResult::success;
}
#endif
