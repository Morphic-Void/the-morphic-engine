
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    02 Nov 26
//
//  Real DLL/SO fixtures for Host-thread execution and cooperative retirement.

#include "framework/system/system_ids.hpp"
#include "framework/system/storage_type_bindings.hpp"
#include "framework/filesystem/filesystem_messages.hpp"
#include "framework/module/module_messages.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>
#include "tests/module_lifecycle/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/module/module_binding_context.hpp"
#include "framework/module/host_pump.hpp"
#include "framework/module/service_thread_pump.hpp"
#include "framework/services/provider_client.hpp"
#include "framework/module/host_eligible_budget.hpp"
#include "framework/module/service_provider.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "system/system_context.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/platform/report_messages.hpp"
#if MV_PUMP_EXECUTIVE
#include "framework/assets/schema_messages.hpp"
#include "data_model/document_parser.hpp"
#include "schema/data_block_view.hpp"
#include "platform/filesystem/file.hpp"
#endif
#include "containers/TInstance.hpp"
#include "framework/threading/CThreadContext.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/processor_relax.hpp"
#include "debug/macros.hpp"
#include "debug/service.hpp"
#if MV_PLATFORM_WINDOWS && MV_PUMP_EXECUTIVE
#include "platform/windows_include.hpp"
#include "framework/platform/window.hpp"
#endif

namespace pump_tests
{

using namespace modules;
using namespace services;
static const char* selected() noexcept
{
#if MV_PLATFORM_WINDOWS
    static thread_local char value[64]{};
    std::size_t required = 0u;
    return ((getenv_s(&required, value, sizeof(value), "MORPHIC_PUMP_CASE") == 0) && (required != 0u)) ? value : "normal";
#else
    const char* value = std::getenv("MORPHIC_PUMP_CASE");
    return (value != nullptr) ? value : "normal";
#endif
}
static bool scenario(const char* const value) noexcept { return std::strcmp(selected(), value) == 0; }
static bool borrow_scenario() noexcept { return std::strncmp(selected(), "borrow-", 7u) == 0; }
static bool notification_scenario() noexcept { return std::strncmp(selected(), "notify-", 7u) == 0; }
static bool modal_scenario() noexcept { return std::strncmp(selected(), "modal-", 6u) == 0; }

struct SModalData
{
    std::atomic<unsigned> ready{ 0u }, entered{ 0u }, leave{ 0u }, passes{ 0u }, pressure{ 0u };
};
struct SModalDescriptor { SModalData* state{ nullptr }; };

struct SNotificationData { std::atomic<unsigned> value{ 0u }; };
struct SNotificationReady { SServiceBorrow borrow; SNotificationRegistration registration; };

struct SSequenceProbe
{
    std::atomic<std::uint64_t>* counter{ nullptr };
    std::uint64_t sequence{ 0u };
};

static threading::CErasedOwnerMsg module_request(const module_ids::id_type module,
    const modules::EModuleAction action, const char* const file = nullptr)
{
    auto owner = CErasedOwner::create<modules::ModuleRequest>();
    auto* request = owner.payload<modules::ModuleRequest>();
    if (request != nullptr)
    {
        request->module = module;
        request->action = action;
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
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    for (;;)
    {
        if (context.read(message)) return true;
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
        platform::threading::processor_relax();
    }
}
static bool close_route(threading::CThreadContext& context, const SServiceRoute route,
    const SServiceRequestId request, const std::int32_t slot)
{
    platform::threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    for (;;)
    {
        threading::CErasedPodMsg message;
        ServiceReply reply;
        if (!context.post(threading::make_pod_message(slot, CloseServiceRequest{ route, request })) ||
            !receive(context, message) || !message.copy_payload_to(reply) || (message.query_async_slot() != slot) ||
            (reply.route.value != route.value) || (reply.request.value != request.value)) return false;
        if (reply.status == EServiceStatus::success)
        {
            return true;
        }
        if (reply.status != EServiceStatus::busy)
        {
            return false;
        }
        // Provider completion may still be crossing its own FIFO after retirement.
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
    }
}

static bool operation(threading::CThreadContext& context, const modules::EModuleAction action,
    const modules::EModuleStatus expected,
    const module_ids::id_type module = module_ids::rendering)
{
    const auto file = test_environment::module_filename((module == module_ids::platform) ?
        ((scenario("platform-report-injected") || scenario("platform-report-cancel") || scenario("platform-report-shutdown") ||
            scenario("platform-report-departure")) ? "package:/bin/MorphicInputPlatform" : "package:/bin/MorphicPlatform") : "package:/bin/MorphicPumpProvider");
    if (!context.post(module_request(module, action, file.c_str())))
    {
        return false;
    }
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
    if (!context.post(module_request(module_ids::executive, (replace ? modules::EModuleAction::replace : modules::EModuleAction::unload),
        (replace ? file.c_str() : nullptr))))
    {
        return false;
    }
    platform::threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    while (!context.exit_requested())
    {
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
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
        if (!request || !request->storage.value.resize(32u, 32u))
        {
            return false;
        }
        threading::CErasedOwnerMsg transfer;
        transfer.set_message_type<RawAssetTransfer>();
        transfer.set_async_slot(210);
        transfer.set_owner(std::move(value));
        AssetResult asset;
        AssetAcquireResult acquired;
        if (!context.post(std::move(transfer)) || !receive(context, message) || !message.copy_payload_to(asset) ||
            (asset.status != EAssetStatus::success) ||
            !context.post(threading::make_pod_message(211, AssetAcquireRequest{asset.asset})) || !receive(context, message) ||
            !message.copy_payload_to(acquired) || (acquired.status != EAssetStatus::success))
        {
            return false;
        }
        assets[index] = asset.asset;
        acquisitions[index] = acquired.acquisition;
    }
    for (const auto asset : assets)
    {
        if (!context.post(threading::make_pod_message(212, AssetDisposeRequest{asset})))
        {
            return false;
        }
    }
    //  This same-queue marker replies only after all 600 retained disposals were admitted.
    if (!context.post(threading::make_pod_message(214, AssetAcquireRequest{})) || !receive(context, message) ||
        (message.query_async_slot() != 214))
    {
        return false;
    }
    for (const auto acquisition : acquisitions)
    {
        if (!context.post(threading::make_pod_message(213, AssetReleaseRequest{acquisition})))
        {
            return false;
        }
    }
    if (!self_stop(context, false)) return false;
    unsigned released = 0u, disposed = 0u;
    for (unsigned index = 0u; index < 1200u; ++index)
    {
        AssetReleaseResult release;
        AssetDisposeResult disposal;
        if (!receive(context, message)) return false;
        if (message.copy_payload_to(release) && (release.status == EAssetStatus::success))
        {
            ++released;
        }
        else if (message.copy_payload_to(disposal) && (disposal.status == EAssetStatus::success))
        {
            ++disposed;
        }
        else return false;
    }
    if ((released != 600u) || (disposed != 600u))
    {
        return false;
    }
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
    if (!context.post(threading::make_pod_message(70, request)))
    {
        return false;
    }
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
                if ((view.descriptor != nullptr) && ((view.size != sizeof(unsigned)) || (view.version != 1u) ||
                    (*static_cast<const unsigned*>(view.descriptor) != 4242u)))
                {
                    return false;
                }
                MV_LOG(info, "Pump fixture: borrowed descriptor observed before terminal result");
                if (scenario("borrow-cancel"))
                {
                    CancelServiceRequest cancel;
                    if (!consumer.cancel(route, { 1u }, cancel) || !context.post(threading::make_pod_message(-1, cancel)))
                    {
                        return false;
                    }
                    releasing = true;
                }
            }
            if (reply.notice == EServiceNotice::completed)
            {
                if (reply.status != (scenario("borrow-cancel") ? EServiceStatus::cancelled : EServiceStatus::success))
                {
                    return false;
                }
                terminal = true;
                if (scenario("borrow-abandoned"))
                {
                    MV_LOG(info, "Pump fixture: consumer departed with a live borrow");
                    return self_stop(context, false);
                }
                if (!releasing && !scenario("borrow-provider-failed"))
                {
                    if (scenario("borrow-shutdown") && !self_stop(context, false))
                    {
                        return false;
                    }
                    ReleaseServiceBorrow release;
                    if (!consumer.release(borrow, release) || !context.post(threading::make_pod_message(-1, release)))
                    {
                        return false;
                    }
                    releasing = true;
                }
            }
        }
        else if (message.copy_payload_to(control))
        {
            if (!consumer.observe(control)) return false;
            borrow = control.borrow;
            if (control.control == EServiceBorrowControl::closing)
            {
                releasing = true;
            }
        }
        else return false;
        if (releasing && !quiescent)
        {
            ServiceBorrowQuiescent release;
            if (consumer.quiesce(borrow, release))
            {
                if (!context.post(threading::make_pod_message(-1, release)))
                {
                    return false;
                }
                quiescent = true;
            }
        }
        if (terminal && consumer.retired(borrow))
        {
            break;
        }
    }
    if (!consumer.forget(route, { 1u })) return false;
    MV_LOG(info, "Pump fixture: borrowed resource retired after consumer quiescence");
    if (scenario("borrow-shutdown")) return true;
    if (scenario("borrow-provider-failed"))
    {
        return context.exit_requested();
    }
    if (!scenario("borrow-provider-failed"))
    {
        if (!close_route(context, route, { 2u }, 71))
        {
            return false;
        }
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
    if (!context.post(threading::make_pod_message(70, acquisition)))
    {
        return false;
    }
    SServiceBorrow borrow;
    const SNotificationData* data = nullptr;
    threading::SNotificationReader reader;
    bool terminal = false, ready = false, observed = false, releasing = false, quiescent = false;
    threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
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
                    if (reply.status != EServiceStatus::success)
                    {
                        return false;
                    }
                    if (reply.notice == EServiceNotice::completed)
                    {
                        ready = true;
                    }
                }
                else
                {
                    if (!consumer.observe(reply))
                    {
                        return false;
                    }
                    if (reply.notice == EServiceNotice::prepared)
                    {
                        borrow = consumer.borrow(route, { 1u });
                        const auto view = consumer.view(borrow);
                        if ((view.size != sizeof(SNotificationData)) || (view.version != 1u) || !view.descriptor)
                        {
                            return false;
                        }
                        data = static_cast<const SNotificationData*>(view.descriptor);
                        RegisterServiceNotification request;
                        if (!consumer.request_notification(borrow, request) ||
                            !context.post(threading::make_pod_message(72, request)))
                        {
                            return false;
                        }
                    }
                    if (reply.notice == EServiceNotice::completed)
                    {
                        if (reply.status != (scenario("notify-cancel") ? EServiceStatus::cancelled : EServiceStatus::success))
                        {
                            return false;
                        }
                        terminal = true;
                    }
                }
            }
            else if (message.copy_payload_to(registration))
            {
                if (!consumer.observe(registration) || (registration.status != EServiceStatus::success))
                {
                    return false;
                }
                reader = consumer.notification(borrow);
                if (!reader.is_valid()) return false;
                ServiceRequest request;
                request.route = route;
                request.request.value = 2u;
                request.opcode = 7u;
                const SNotificationReady payload{ borrow, consumer.registration(borrow) };
                request.payload_size = sizeof(payload);
                std::memcpy(request.payload, &payload, sizeof(payload));
                if (!context.post(threading::make_pod_message(73, request)))
                {
                    return false;
                }
            }
            else if (message.copy_payload_to(control))
            {
                if (!consumer.observe(control))
                {
                    return false;
                }
                if (control.control == EServiceBorrowControl::closing)
                {
                    releasing = true;
                }
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
            if (!scenario("notify-cancel"))
            {
                reader.clear_pending();
            }
            if (scenario("notify-abandoned"))
            {
                return self_stop(context, false);
            }
#if !MV_PUMP_REPLACEMENT
            if (scenario("notify-replacement"))
            {
                return self_stop(context, true);
            }
#endif
            if (scenario("notify-shutdown") && !self_stop(context, false))
            {
                return false;
            }
            if (scenario("notify-cancel"))
            {
                CancelServiceRequest cancel;
                if (!consumer.cancel(route, { 1u }, cancel) || !context.post(threading::make_pod_message(-1, cancel)))
                {
                    return false;
                }
            }
            else
            {
                ReleaseServiceBorrow release;
                if (!consumer.release(borrow, release) || !context.post(threading::make_pod_message(-1, release)))
                {
                    return false;
                }
            }
            releasing = true;
        }
        if (releasing && !quiescent)
        {
            reader = {};
            data = nullptr;
            ServiceBorrowQuiescent proof;
            if (!consumer.quiesce(borrow, proof) || !context.post(threading::make_pod_message(-1, proof)))
            {
                return false;
            }
            quiescent = true;
        }
        if (terminal && consumer.retired(borrow))
        {
            break;
        }
        if (reader.is_valid())
        {
            reader.clear_pending();
            if (reader.failed()) return false;
            if (!observed && ready && (data->value.load(std::memory_order_acquire) != 0u))
            {
                continue;
            }
        }
        //  Messages and exit share this sampled epoch with external notifications.
        //  An exit request does not end cleanup; its messages continue to wake us.
        if (context.wait_for_new_epoch(epoch, deadline).status != threading::EWaitStatus::satisfied)
        {
            return false;
        }
    }
    if (!consumer.forget(route, { 1u })) return false;
    MV_LOG(info, "Pump fixture: notification retired after final publisher signal");
    if (scenario("notify-shutdown")) return true;
    if (!close_route(context, route, { 3u }, 74))
    {
        return false;
    }
    return operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success) && self_stop(context, false);
}

static bool modal_resource(threading::CThreadContext& context, const SServiceRoute route)
{
    const auto* const execution = context.host_execution();
    if (!execution || !execution->notification.is_valid())
    {
        return false;
    }
    CServiceBorrowClient consumer;
    ServiceRequest request;
    request.route = route;
    request.request = { 1u };
    request.opcode = 8u;
    if (!consumer.begin(route, request.request, request.opcode) || !context.post(threading::make_pod_message(80, request)))
    {
        return false;
    }
    SServiceBorrow borrow;
    SModalData* data = nullptr;
    bool terminal = false;
    while (!terminal || !data)
    {
        threading::CErasedPodMsg message;
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || !consumer.observe(reply))
        {
            return false;
        }
        if (reply.notice == EServiceNotice::prepared)
        {
            borrow = consumer.borrow(route, { 1u });
            const auto view = consumer.view(borrow);
            if (!view.descriptor || (view.size != sizeof(SModalDescriptor)))
            {
                return false;
            }
            data = static_cast<const SModalDescriptor*>(view.descriptor)->state;
        }
        if (reply.notice == EServiceNotice::completed)
        {
            terminal = reply.status == EServiceStatus::success;
        }
    }
    platform::threading::CWaitDeadline limit;
    if (limit.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    while (!data->ready.load(std::memory_order_acquire))
    {
        std::uint64_t remaining = 0u;
        if ((limit.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
        platform::threading::processor_relax();
    }
    request.request = { 2u };
    request.opcode = 9u;
    if (!context.post(threading::make_pod_message(81, request)))
    {
        return false;
    }
    threading::CErasedPodMsg message;
    ServiceReply acknowledgement;
    if (!receive(context, message) || !message.copy_payload_to(acknowledgement) ||
        (acknowledgement.request.value != 2u) || (acknowledgement.notice != EServiceNotice::acknowledged))
    {
        return false;
    }
    while (!data->entered.load(std::memory_order_acquire))
    {
        std::uint64_t remaining = 0u;
        if ((limit.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
        platform::threading::processor_relax();
    }
    SHostRestrictionSnapshot snapshot;
    if ((execution->restriction.read(snapshot) != ERestrictionRead::success) || (snapshot.depth != 3u) ||
        (snapshot.counts[0] != 2u) || (snapshot.counts[1] != 1u))
    {
        return false;
    }
    CHostEligibleBudget budget;
    if (budget.start(execution->restriction, 50000000u) != EBudgetStatus::restricted)
    {
        return false;
    }
    unsigned pods = 0u, owners = 0u;
    if (scenario("modal-full"))
    {
        for (; pods < 512u; ++pods)
        {
            if (!context.post(threading::make_pod_message((9000 + pods), AssetReleaseRequest{})))
            {
                return false;
            }
        }
        for (unsigned attempt = 0u; (attempt < 4096u) && (owners < 64u); ++attempt)
        {
            auto value = CErasedOwner::create<filesystem::FilesystemRefreshRequest>();
            auto* refresh = value.payload<filesystem::FilesystemRefreshRequest>();
            if (!refresh || !refresh->root.set("unknown:"))
            {
                return false;
            }
            threading::CErasedOwnerMsg owned;
            owned.set_message_type<filesystem::FilesystemRefreshRequest>();
            owned.set_async_slot(10000 + owners);
            owned.set_owner(std::move(value));
            if (context.post(std::move(owned))) ++owners;
            else if ((owners >= 32u) && data->pressure.load(std::memory_order_acquire))
            {
                break;
            }
            else platform::threading::processor_relax();
        }
    }
    std::uint64_t start = 0u, now = 0u;
    if (platform::threading::query_awake_nanoseconds(start).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    do
    {
        if (platform::threading::query_awake_nanoseconds(now).code != platform::threading::EWaitError::none)
        {
            return false;
        }
        if (context.read(message))
        {
            return false;
        } //  No terminal/release/owning reply may pass the held prefix.
        platform::threading::processor_relax();
    } while (((now - start) < 100000000u) || (data->passes.load(std::memory_order_acquire) < 8u));
    if ((budget.observe().status != EBudgetStatus::restricted) || (budget.observe().remaining_ns != 50000000u))
    {
        return false;
    }
    if (scenario("modal-full") && !data->pressure.load(std::memory_order_acquire))
    {
        return false;
    }
    if (scenario("modal-shutdown") && !execution->stop.request())
    {
        return false;
    }
    data->leave.store(1u, std::memory_order_release);
    data = nullptr; //  No reads after the following consumer-quiescent publication.
    ReleaseServiceBorrow release;
    ServiceBorrowQuiescent quiescent;
    if (!consumer.release(borrow, release) || !context.post(threading::make_pod_message(-1, release)) ||
        !consumer.quiesce(borrow, quiescent) || !context.post(threading::make_pod_message(-1, quiescent)))
    {
        return false;
    }
    bool completed = false;
    unsigned replies = 0u;
    while (!completed || !consumer.retired(borrow) || (replies != (pods + owners)))
    {
        if (!receive(context, message)) return false;
        ServiceReply reply;
        ServiceBorrowControl control;
        if (message.copy_payload_to(reply))
        {
            if ((reply.request.value != 2u) || (reply.notice != EServiceNotice::completed) || (reply.status != EServiceStatus::success))
            {
                return false;
            }
            completed = true;
        }
        else if (message.copy_payload_to(control))
        {
            if (!consumer.observe(control)) return false;
        }
        else if (message.query_async_slot() >= 9000)
        {
            ++replies;
        }
        else return false;
    }
    if (!consumer.forget(route, { 1u })) return false;
    MV_LOG(info, "Pump fixture: modal budgets, deferred replies and borrowed retirement passed");
    if (scenario("modal-shutdown")) return true;
    if (!close_route(context, route, { 3u }, 82))
    {
        return false;
    }
    return operation(context, EModuleAction::unload, EModuleStatus::success) && self_stop(context, false);
}

static bool report_exchange(threading::CThreadContext& context, const bool update,
    platform::reports::PlatformReportResult& result) noexcept
{
    using namespace platform::reports;
    const auto request = update ? threading::make_pod_message(264, PlatformReportUpdateRequest{}) :
        threading::make_pod_message(264, PlatformReportRequest{});
    threading::CErasedPodMsg reply;
    return context.post(request) && receive(context, reply) && (reply.query_async_slot() == 264) && reply.copy_payload_to(result);
}

static bool report_settled(threading::CThreadContext& context, platform::reports::PlatformReportResult& result)
{
    //  Polling is confined to the acceptance reader observing asynchronous initial activation.
    platform::threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(8000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    do
    {
        if (!report_exchange(context, false, result))
        {
            return false;
        }
        if (!result.update_in_progress) return true;
        if (result.acquisition)
        {
            threading::CErasedPodMsg reply;
            AssetReleaseResult released;
            if (!context.post(threading::make_pod_message(265, AssetReleaseRequest{ result.acquisition })) ||
                !receive(context, reply) || !reply.copy_payload_to(released) || (released.status != EAssetStatus::success))
            {
                return false;
            }
        }
        std::uint64_t remaining{};
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
    } while (true);
}

#if MV_PLATFORM_WINDOWS
static bool report_bytes(const char* const path, const CByteConstView data)
{
    return platform::filesystem::saveFile(path, data);
}

#endif

static bool release_report(threading::CThreadContext& context, const CAssetAcquisitionId token) noexcept
{
    threading::CErasedPodMsg reply;
    AssetReleaseResult released;
    return context.post(threading::make_pod_message(265, AssetReleaseRequest{ token })) && receive(context, reply) &&
        reply.copy_payload_to(released) && (released.status == EAssetStatus::success) && (released.acquisition == token);
}

static bool report_departure(threading::CThreadContext& context)
{
    using namespace platform::reports;
    PlatformReportResult current;
#if MV_PUMP_REPLACEMENT
    if (!report_settled(context, current) || (current.status != EStatus::success) ||
        (current.last_update != ELastUpdate::succeeded) || !release_report(context, current.acquisition))
    {
        return false;
    }
    MV_LOG(info, "Replacement Executive acquired current after requester departure");
    return operation(context, EModuleAction::unload, EModuleStatus::success, module_ids::platform) && self_stop(context, false);
#else
    return operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform) &&
        report_settled(context, current) && (current.status == EStatus::success) && release_report(context, current.acquisition) &&
        context.post(threading::make_pod_message(267, PlatformReportUpdateRequest{})) && self_stop(context, true);
#endif
}

static bool cancel_report(threading::CThreadContext& context)
{
    using namespace platform::reports;
    PlatformReportResult pending, unavailable;
    if (!operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform) ||
        !report_exchange(context, false, pending) || (pending.status != EStatus::not_ready) || !pending.update_in_progress ||
        pending.acquisition || (pending.last_update != ELastUpdate::none))
    {
        return false;
    }
    if (scenario("platform-report-shutdown"))
    {
        return self_stop(context, false);
    }
    return operation(context, EModuleAction::unload, EModuleStatus::success, module_ids::platform) &&
        report_exchange(context, false, unavailable) && (unavailable.status == EStatus::unavailable) &&
        !unavailable.update_in_progress && self_stop(context, false);
}

static bool schema_process(threading::CThreadContext& context)
{
    auto envelope = threading::make_owner_message<SchemaProcessRequest>(290);
    auto* const request = envelope.owner().payload<SchemaProcessRequest>();
    if (!request) return false;
    request->request_id = { 1u };
    CAssetId declarations;
#if MV_PUMP_REPLACEMENT
    const CByteBuffer handoff = platform::filesystem::loadFile("declaration-id.bin");
    if (handoff.size() != sizeof(declarations))
    {
        return false;
    }
    std::memcpy(&declarations, handoff.data(), sizeof(declarations));
    request->operation = schema::ESchemaAssembly::compose_roles;
    request->output_kind = schema::ESchemaOutput::runtime_data_block;
    request->input_count = 2u;
    request->types_source = 0;
    request->instances_source = request->data_source = 1;
    request->inputs.slots[0].kind = ESchemaInput::retained_document;
    request->inputs.slots[0].asset = declarations;
    auto& authored = request->inputs.slots[1];
    constexpr char text[] = R"({"instances":{"Report":{"main":{"declaration":{"rows":{"collection":"all","first":1,"count":1}}}}},"data":{"Row":{"all":{"data":[{"label":"Alpha"},{"label":"Zulu"}]}}}})";
#else
    request->output_kind = schema::ESchemaOutput::schema_document;
    request->input_count = 1u;
    auto& authored = request->inputs.slots[0];
    constexpr char text[] = R"({"types":{"structures":{"Row":{"members":[{"label":{"type":{"kind":"text"}}}]},"Report":{"members":[{"rows":{"type":{"kind":"span","element":"Row"}}}]}}}})";
#endif
    authored.kind = ESchemaInput::owned_document;
    if (!document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(text), sizeof(text) - 1u },
        authored.document).accepted() || !context.post(std::move(envelope)) || envelope.has_owner())
    {
        return false;
    }
    threading::CErasedPodMsg message;
    SchemaProcessAccepted accepted;
    SchemaProcessResult result;
    if (!receive(context, message) || (message.query_async_slot() != 290) || !message.copy_payload_to(accepted) ||
        (accepted.request_id.value != 1u) || (accepted.contract_version != 1u) || !receive(context, message) ||
        (message.query_async_slot() != 290) || !message.copy_payload_to(result) || (result.request_id.value != 1u) ||
        (result.contract_version != 1u) || result.reserved || (result.status != ESchemaProcessStatus::success) ||
        !result.success.asset || !result.success.acquisition || !result.success.descriptor)
    {
        return false;
    }
    const SSchemaOutputV1& output = *result.success.descriptor;
    if ((output.size != sizeof(output)) || (output.version != 1u) || (output.kind != result.output_kind) ||
        !output.bytes.is_ready())
    {
        return false;
    }
#if MV_PUMP_REPLACEMENT
    {
        schema::CBakedDataBlock block;
        schema::SDataBlockDiagnostic error;
        schema::CDataBlockView view;
        schema::SDataOperationDiagnostic diagnostic;
        schema::CDataValueView value, field;
        schema::SDataRangeView span;
        schema::SDataTextView label;
        if ((output.kind != schema::ESchemaOutput::runtime_data_block) ||
            !block.attach_asset(result.success.asset, output.bytes, error) || !view.bind(block, {}, diagnostic) ||
            !view.value(view.find_instance(CStringView{ "Report" }, CStringView{ "main" }), 0u, value) ||
            !view.member(value, CStringView{ "rows" }, field) || !view.span(field, span) ||
            (span.first != 1u) || (span.count != 1u) || !view.element(span, 0u, value) ||
            !view.member(value, CStringView{ "label" }, field) || !view.text(field, label) ||
            !(label.stored_text == CStringView{ "Zulu" }) || !view.value(span.collection, 0u, value) ||
            !view.member(value, CStringView{ "label" }, field) || !view.text(field, label) ||
            !(label.stored_text == CStringView{ "Alpha" }) || (output.bytes.align() < 128u) ||
            (block.file_bytes().size() != output.bytes.size()) || !block.bulk_payload_view().is_ready())
        {
            return false;
        }
    }
    if (!release_report(context, result.success.acquisition))
    {
        return false;
    }
    for (const CAssetId asset : { declarations, result.success.asset })
    {
        AssetDisposeResult disposed;
        if (!context.post(threading::make_pod_message(291, AssetDisposeRequest{ asset })) ||
            !receive(context, message) || !message.copy_payload_to(disposed) || (disposed.status != EAssetStatus::success))
        {
            return false;
        }
    }
    MV_LOG(info, "Replacement composed retained declarations and read remapped text and span");
    return self_stop(context, false);
#else
    declarations = result.success.asset;
    if ((output.kind != schema::ESchemaOutput::schema_document) ||
        !platform::filesystem::saveFile("declaration-id.bin",
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(&declarations), sizeof(declarations) }) ||
        !release_report(context, result.success.acquisition))
    {
        return false;
    }
    return self_stop(context, true);
#endif
}

static bool injected_report(threading::CThreadContext& context)
{
    using namespace platform::reports;
    PlatformReportResult initial, retained, update;
    if (!operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform) ||
        !report_settled(context, initial) || (initial.status != EStatus::success) || !initial.acquisition || !initial.bytes.size())
    {
        return false;
    }
    const auto* const bytes = initial.bytes.data();
    const auto signature = bytes[0];
    for (const EStage expected : { EStage::capture, EStage::validation })
    {
        //  A foreign producer outcome can never complete an Executive request.
        const PlatformReportCaptureFailed foreign{ { { 1u }, 2u }, { EStage::capture, EReason::clock_failed } };
        if (!context.post(threading::make_pod_message(-1, foreign)) || !report_exchange(context, true, update) ||
            (update.status != EStatus::failed) || (update.availability != EAvailability::available) ||
            (update.last_update != ELastUpdate::failed) || (update.failure.stage != expected) || update.acquisition ||
            update.bytes.size() || !report_settled(context, retained) || (retained.status != EStatus::success) ||
            (retained.bytes.data() != bytes) || (retained.acquisition == initial.acquisition) || !release_report(context, retained.acquisition))
        {
            return false;
        }
    }
    if (!context.post(threading::make_pod_message(267, PlatformReportUpdateRequest{})) ||
        !context.post(threading::make_pod_message(268, PlatformReportUpdateRequest{})) ||
        !context.post(threading::make_pod_message(269, PlatformReportRequest{})))
    {
        return false;
    }
    unsigned seen{};
    for (unsigned i = 0u; i < 3u; ++i)
    {
        threading::CErasedPodMsg reply;
        PlatformReportResult result;
        if (!receive(context, reply) || !reply.copy_payload_to(result))
        {
            return false;
        }
        switch (reply.query_async_slot())
        {
            case 267:
            {
                if ((result.status != EStatus::success) || !result.acquisition || (result.bytes.data() == bytes))
                {
                    return false;
                }
                seen |= 1u;
                update = result;
                break;
            }
            case 268:
            {
                if ((result.status != EStatus::update_in_progress) || !result.update_in_progress || result.acquisition)
                {
                    return false;
                }
                seen |= 2u;
                break;
            }
            case 269:
            {
                if ((result.status != EStatus::success) || (result.bytes.data() != bytes) || !result.update_in_progress)
                {
                    return false;
                }
                seen |= 4u;
                retained = result;
                break;
            }
            default:
            {
                return false;
            }
        }
    }
    if ((seen != 7u) || !release_report(context, retained.acquisition) || !operation(context, EModuleAction::unload, EModuleStatus::success, module_ids::platform) ||
        !report_exchange(context, false, retained) || (retained.status != EStatus::unavailable) ||
        !operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform) ||
        !report_settled(context, retained) || (retained.status != EStatus::success) || (bytes[0] != signature) ||
        (retained.bytes.data() == bytes) || !release_report(context, initial.acquisition) ||
        !release_report(context, update.acquisition) || !release_report(context, retained.acquisition))
    {
        return false;
    }
    return operation(context, EModuleAction::unload, EModuleStatus::success, module_ids::platform) && self_stop(context, false);
}

static bool platform_report(threading::CThreadContext& context)
{
    using namespace platform::reports;
    PlatformReportResult result;
    if (!report_exchange(context, false, result) || (result.status != EStatus::unavailable) || result.acquisition ||
        !operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform) ||
        !report_settled(context, result))
    {
        return false;
    }
    MV_LOG(info, "Report fixture initial status {}, stage {}, reason {}, native {}",
        static_cast<unsigned>(result.status), static_cast<unsigned>(result.failure.stage),
        static_cast<unsigned>(result.failure.reason), result.failure.native_error);
#if MV_PLATFORM_WINDOWS
    if ((result.status != EStatus::success) || !result.acquisition || !report_bytes("cold.bsd", result.bytes))
    {
        return false;
    }
    const PlatformReportResult cold = result;
    if (!report_exchange(context, true, result) || (result.status != EStatus::success) ||
        !result.acquisition || (result.acquisition == cold.acquisition) || !report_bytes("refresh.bsd", result.bytes))
    {
        return false;
    }
    const PlatformReportResult refresh = result;
#else
    if ((result.status != EStatus::not_ready) || (result.last_update != ELastUpdate::failed) ||
        (result.failure.reason != EReason::unsupported_backend) || result.acquisition ||
        !report_exchange(context, true, result) || (result.status != EStatus::failed) || result.acquisition ||
        (result.failure.reason != EReason::unsupported_backend))
    {
        return false;
    }
#endif
    if (!operation(context, EModuleAction::unload, EModuleStatus::success, module_ids::platform) ||
        !report_exchange(context, false, result) || (result.status != EStatus::unavailable) ||
        !operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform) ||
        !report_settled(context, result))
    {
        return false;
    }
#if MV_PLATFORM_WINDOWS
    if ((result.status != EStatus::success) || !result.acquisition || !report_bytes("reload.bsd", result.bytes) ||
        !report_bytes("historical.bsd", cold.bytes) || !report_bytes("historical-refresh.bsd", refresh.bytes))
    {
        return false;
    }
    for (const CAssetAcquisitionId token : { cold.acquisition, refresh.acquisition, result.acquisition })
    {
        threading::CErasedPodMsg reply;
        AssetReleaseResult released;
        if (!context.post(threading::make_pod_message(265, AssetReleaseRequest{ token })) || !receive(context, reply) ||
            !reply.copy_payload_to(released) || (released.status != EAssetStatus::success) || (released.acquisition != token))
        {
            return false;
        }
    }
#else
    if ((result.status != EStatus::not_ready) || (result.failure.reason != EReason::unsupported_backend))
    {
        return false;
    }
#endif
    return operation(context, EModuleAction::unload, EModuleStatus::success, module_ids::platform) && self_stop(context, false);
}

static bool platform_shell(threading::CThreadContext& context)
{
    const auto operation = [&](const modules::EModuleAction action, const modules::EModuleStatus expected,
        const module_ids::id_type module = module_ids::platform)
    { return pump_tests::operation(context, action, expected, module); };
    if (!operation(EModuleAction::load, EModuleStatus::success) ||
        !operation(EModuleAction::load, EModuleStatus::already_loaded) ||
        !operation(EModuleAction::load, EModuleStatus::already_loaded))
    {
        return false;
    }
    threading::CErasedPodMsg message;
    OpenServiceResult opened;
    if (!context.post(threading::make_pod_message(20, OpenServiceRequest{ module_ids::platform, 1u, 1u, { 1u } })) ||
        !receive(context, message) || !message.copy_payload_to(opened) || (opened.status != EServiceStatus::success))
    {
        return false;
    }
    for (const std::int32_t slot : { 18, 19 })
    {
        ServiceRequest unsupported;
        unsupported.route = opened.route;
        unsupported.request.value = static_cast<std::uint64_t>(slot);
        unsupported.opcode = 65535u;
        if (!context.post(threading::make_pod_message(slot, unsupported)))
        {
            return false;
        }
        for (const auto notice : { EServiceNotice::acknowledged, EServiceNotice::completed })
        {
            ServiceReply reply;
            if (!receive(context, message) || !message.copy_payload_to(reply) || (message.query_async_slot() != slot) ||
                (reply.route.value != opened.route.value) || (reply.request.value != unsupported.request.value) || (reply.opcode != 65535u) ||
                (reply.notice != notice) || (reply.status != ((notice == EServiceNotice::acknowledged) ? EServiceStatus::success : EServiceStatus::unsupported)))
            {
                return false;
            }
        }
    }
    MV_LOG(info, "Pump fixture: Platform unsupported service commands completed and route remained usable");
    if (!operation(EModuleAction::unload, EModuleStatus::busy))
    {
        return false;
    }
    ServiceRequest request;
    request.route = opened.route;
    request.request.value = 20u;
#if MV_PLATFORM_WINDOWS
    request.opcode = 65535u; // Ordinary/headless acceptance never requests a visible window.
#else
    request.opcode = 16u; // The portable shell explicitly rejects unsupported native creation.
#endif
    if (!context.post(threading::make_pod_message(30, request)))
    {
        return false;
    }
    for (const auto notice : { EServiceNotice::acknowledged, EServiceNotice::completed })
    {
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.notice != notice) ||
            (reply.status != ((notice == EServiceNotice::acknowledged) ? EServiceStatus::success : EServiceStatus::unsupported)))
        {
            return false;
        }
    }
    ServiceReply closed;
    if (!context.post(threading::make_pod_message(31, CloseServiceRequest{ opened.route, { 21u } })) ||
        !receive(context, message) || !message.copy_payload_to(closed) || (closed.status != EServiceStatus::success))
    {
        return false;
    }
    if (!operation(EModuleAction::unload, EModuleStatus::success))
    {
        return false;
    }
    MV_LOG(info, "Pump fixture: Platform shell loaded on demand, routed and retired without a service thread");
    return self_stop(context, false);
}

#if MV_PLATFORM_WINDOWS
static bool live_report(threading::CThreadContext& context, const HWND window)
{
    using namespace platform::reports;
    PlatformReportResult previous;
    if (!report_settled(context, previous) || (previous.status != EStatus::success) ||
        !context.post(threading::make_pod_message(266, PlatformReportUpdateRequest{})))
    {
        return false;
    }
    std::uint64_t worst{};
    unsigned samples{};
    const ULONGLONG begin = GetTickCount64();
    PlatformReportResult updated;
    for (;;)
    {
        threading::CErasedPodMsg reply;
        if (context.read(reply))
        {
            if (!reply.copy_payload_to(updated) || (reply.query_async_slot() != 266))
            {
                return false;
            }
            break;
        }
        //  Exercise the owned test window while the same Host owner pumps the update.
        if (!PostMessageW(window, WM_MOUSEMOVE, 0u, MAKELPARAM((samples & 63u), 8u)))
        {
            return false;
        }
        DWORD_PTR ignored{};
        std::uint64_t before{}, after{};
        if ((platform::threading::query_awake_nanoseconds(before).code != platform::threading::EWaitError::none) ||
            !SendMessageTimeoutW(window, WM_NULL, 0u, 0u, SMTO_BLOCK, 7000u, &ignored) ||
            (platform::threading::query_awake_nanoseconds(after).code != platform::threading::EWaitError::none) ||
            (after < before))
        {
            return false;
        }
        worst = std::max(worst, (after - before));
        ++samples;
        if ((GetTickCount64() - begin) > 8000u)
        {
            return false;
        }
        Sleep(1u);
    }
    if (!samples || (updated.status != EStatus::success) || !report_bytes("window-refresh.bsd", updated.bytes))
    {
        return false;
    }
    for (const CAssetAcquisitionId token : { previous.acquisition, updated.acquisition })
    {
        threading::CErasedPodMsg reply;
        AssetReleaseResult released;
        if (!context.post(threading::make_pod_message(265, AssetReleaseRequest{ token })) ||
            !receive(context, reply) || !reply.copy_payload_to(released) || (released.status != EAssetStatus::success))
        {
            return false;
        }
    }
    MV_LOG(info, "Live Platform report refresh, window replies {}, maximum window response ns {}", samples, worst);
    return true;
}

static bool platform_native(threading::CThreadContext& context)
{
    using namespace platform::windows;
    if (!operation(context, EModuleAction::load, EModuleStatus::success, module_ids::platform))
    {
        return false;
    }
    threading::CErasedPodMsg message;
    OpenServiceResult opened;
    if (!context.post(threading::make_pod_message(20, OpenServiceRequest{ module_ids::platform, 1u, 1u, { 1u } })) ||
        !receive(context, message) || !message.copy_payload_to(opened) || (opened.status != EServiceStatus::success))
    {
        return false;
    }
    CServiceBorrowClient consumer;
    ServiceRequest acquisition;
    acquisition.route = opened.route;
    acquisition.request.value = 1u;
    acquisition.opcode = static_cast<std::uint16_t>(EWindowCommand::create_window);
    if (!consumer.begin(opened.route, acquisition.request, acquisition.opcode) ||
        !context.post(threading::make_pod_message(30, acquisition)))
    {
        return false;
    }
    SServiceBorrow borrow;
    const SWindowViewV1* view = nullptr;
    bool acquired = false, ready = false;
    while (!acquired || !ready)
    {
        if (!receive(context, message)) return false;
        ServiceReply reply;
        ServiceNotificationRegistered registration;
        if (message.copy_payload_to(reply))
        {
            if (reply.status != EServiceStatus::success)
            {
                return false;
            }
            if (reply.request.value == 2u)
            {
                if (reply.notice == EServiceNotice::completed)
                {
                    ready = true;
                }
            }
            else
            {
                if (!consumer.observe(reply)) return false;
                if (reply.notice == EServiceNotice::prepared)
                {
                    borrow = consumer.borrow(opened.route, acquisition.request);
                    const auto prepared = consumer.view(borrow);
                    if ((prepared.size != sizeof(SWindowViewV1)) || (prepared.version != 1u) || !prepared.descriptor)
                    {
                        return false;
                    }
                    view = static_cast<const SWindowViewV1*>(prepared.descriptor);
                    if (!view->is_valid() || (view->provider.value != opened.provider.value))
                    {
                        return false;
                    }
                    RegisterServiceNotification request;
                    if (!consumer.request_notification(borrow, request) ||
                        !context.post(threading::make_pod_message(31, request)))
                    {
                        return false;
                    }
                }
                if (reply.notice == EServiceNotice::completed)
                {
                    acquired = true;
                }
            }
        }
        else if (message.copy_payload_to(registration))
        {
            if (!consumer.observe(registration) || (registration.status != EServiceStatus::success) || !view)
            {
                return false;
            }
            ServiceRequest request;
            request.route = opened.route;
            request.request.value = 2u;
            request.opcode = static_cast<std::uint16_t>(EWindowCommand::window_ready);
            const SWindowReady payload{ view->window, borrow, consumer.registration(borrow) };
            request.payload_size = sizeof(payload);
            std::memcpy(request.payload, &payload, sizeof(payload));
            if (!context.post(threading::make_pod_message(32, request)))
            {
                return false;
            }
        }
        else return false;
    }
    if (!view || !consumer.notification(borrow).pending())
    {
        return false;
    }
    const auto handle = reinterpret_cast<HWND>(view->native_window);
    const auto* const execution = context.host_execution();
    if (!execution || !IsWindow(handle))
    {
        return false;
    }
    if (scenario("platform-native-report") && !live_report(context, handle)) return false;
    const bool menu = scenario("platform-native-menu"), sizing = scenario("platform-native-size");
    const WPARAM command = menu ? SC_KEYMENU : (sizing ? (SC_SIZE + WMSZ_BOTTOMRIGHT) : SC_MOVE);
    if (!PostMessageW(handle, WM_SYSCOMMAND, command, (menu ? VK_SPACE : 0)))
    {
        return false;
    }
    const auto start = GetTickCount64();
    modules::SHostRestrictionSnapshot restriction;
    for (;;)
    {
        const auto read = execution->restriction.read(restriction);
        if (read == modules::ERestrictionRead::failed)
        {
            return false;
        }
        //  Provider-local reasons are asserted only by this Platform acceptance test.
        if ((read == modules::ERestrictionRead::success) && (restriction.authority.value == opened.provider.value) &&
            (restriction.reasons & (1u << (menu ? 4u : 3u))))
        {
            break;
        }
        if ((GetTickCount64() - start) >= 3000u)
        {
            return false;
        }
        Sleep(1u);
    }
    MV_LOG(info, "Pump fixture: Platform native modal scope authenticated by Host");
    ServiceRequest deferred;
    deferred.route = opened.route;
    deferred.request.value = 3u;
    deferred.opcode = 65535u;
    if (!context.post(threading::make_pod_message(33, deferred)))
    {
        return false;
    }
    Sleep(150u); // Stationary native loop must still offer Host maintenance.
    if (sizing)
    {
        for (unsigned index = 0u; index < 20u; ++index)
        {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dx = input.mi.dy = (index & 1u) ? -2 : 2;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            if (SendInput(1u, &input, sizeof(input)) != 1u)
            {
                return false;
            }
            Sleep(10u);
        }
    }
    //  Ordinary commands stay deferred while the original native frame is live.
    if (context.read(message) || (execution->restriction.read(restriction) != modules::ERestrictionRead::success) ||
        (restriction.authority.value != opened.provider.value) || !(restriction.reasons & (1u << (menu ? 4u : 3u))))
    {
        return false;
    }
    if (!execution->stop.request()) return false;
    bool quiescent = false, deferred_terminal = false;
    const auto stopping = GetTickCount64();
    for (;;)
    {
        while (context.read(message))
        {
            ServiceReply reply;
            ServiceBorrowControl control;
            if (message.copy_payload_to(reply))
            {
                if ((reply.request.value != 3u) || (reply.status !=
                    ((reply.notice == EServiceNotice::acknowledged) ? EServiceStatus::success : EServiceStatus::closing)))
                {
                    return false;
                }
                if (reply.notice == EServiceNotice::completed)
                {
                    deferred_terminal = true;
                }
            }
            else if (message.copy_payload_to(control))
            {
                if (!consumer.observe(control))
                {
                    return false;
                }
                if ((control.control == EServiceBorrowControl::closing) && !quiescent)
                {
                    if (!IsWindow(handle)) return false;
                    view = nullptr;
                    //  This consumer creates no external graphics resources and
                    //  has stopped all descriptor/notification reads before proof.
                    ServiceBorrowQuiescent proof;
                    if (!consumer.quiesce(borrow, proof) || !context.post(threading::make_pod_message(-1, proof)))
                    {
                        return false;
                    }
                    quiescent = true;
                }
            }
            else return false;
        }
        if (quiescent && deferred_terminal && consumer.retired(borrow) && context.exit_requested() && !IsWindow(handle))
        {
            break;
        }
        if ((GetTickCount64() - stopping) >= 5000u)
        {
            return false;
        }
        Sleep(1u);
    }
    if (!consumer.forget(opened.route, acquisition.request) ||
        (execution->restriction.read(restriction) != modules::ERestrictionRead::success) || restriction.depth)
    {
        return false;
    }
    MV_LOG(info, "Pump fixture: Platform native shutdown retired borrow and destroyed window");
    return true;
}
#endif

static bool probe_sequence(threading::CThreadContext& context, const SServiceRoute route,
    const std::uint64_t request_id, const std::uint64_t expected)
{
    std::atomic<std::uint64_t>* const counter = system_context::sequence_counter;
    if ((counter == nullptr) || (counter->fetch_add(1u, std::memory_order_relaxed) != expected))
    {
        return false;
    }
    ServiceRequest request;
    request.route = route;
    request.request.value = request_id;
    request.opcode = 12u;
    if (!context.post(threading::make_pod_message(90, request)))
    {
        return false;
    }
    for (const EServiceNotice notice : { EServiceNotice::acknowledged, EServiceNotice::completed })
    {
        threading::CErasedPodMsg message;
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) ||
            (message.query_async_slot() != 90) || (reply.route.value != route.value) ||
            (reply.request.value != request_id) || (reply.opcode != 12u) ||
            (reply.notice != notice) || (reply.status != EServiceStatus::success))
        {
            return false;
        }
        if (notice == EServiceNotice::completed)
        {
            if (reply.payload_size != sizeof(SSequenceProbe))
            {
                return false;
            }
            SSequenceProbe probe;
            std::memcpy(&probe, reply.payload, sizeof(probe));
            if ((probe.counter != counter) || (probe.sequence != (expected + 1u)))
            {
                return false;
            }
        }
    }
    return counter->fetch_add(1u, std::memory_order_relaxed) == (expected + 2u);
}

static bool run(threading::CThreadContext& context)
{
    if (scenario("schema-process")) return schema_process(context);
    if (scenario("platform-report-departure")) return report_departure(context);
    if (scenario("platform-report-cancel") || scenario("platform-report-shutdown"))
    {
        return cancel_report(context);
    }
    if (scenario("platform-report-injected")) return injected_report(context);
    if (scenario("platform-report")) return platform_report(context);
    if (scenario("platform-shell"))
    {
        return platform_shell(context);
    }
#if MV_PLATFORM_WINDOWS
    if (scenario("platform-native-move") || scenario("platform-native-size") || scenario("platform-native-menu") ||
        scenario("platform-native-report"))
    {
        return platform_native(context);
    }
#endif
    const bool rejected = scenario("bad-size") || scenario("bad-version") || scenario("bad-flags") ||
        scenario("bad-callback") || scenario("bad-provider-size") ||
        scenario("bad-provider-version") || scenario("bad-provider-flags") || scenario("bad-provider-count") ||
        scenario("bad-provider-null") || scenario("bad-provider-contract") || scenario("bad-provider-duplicate") ||
        scenario("bad-provider-query");
    const bool activation_failure = scenario("activation-failed") || scenario("activation-null") || scenario("activation-timeout") ||
        scenario("unauthorised-native-stop") || scenario("unauthorised-native-events");
    if (rejected || activation_failure)
    {
        return operation(context, modules::EModuleAction::load, (rejected ? modules::EModuleStatus::function_unavailable : modules::EModuleStatus::installation_failed)) && self_stop(context, false);
    }
#if MV_PUMP_REPLACEMENT
    if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::already_loaded))
    {
        return false;
    }
    MV_LOG(info, "Pump fixture: independent provider survived Executive replacement");
#else
    if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::success))
    {
        return false;
    }
    if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::already_loaded))
    {
        return false;
    }
#endif
    threading::CErasedPodMsg message;
    if (!context.post(threading::make_pod_message(20, OpenServiceRequest{module_ids::rendering, 1u, 1u, {1u}})) ||
        !receive(context, message))
    {
        return false;
    }
    OpenServiceResult opened;
    if (scenario("empty-provider"))
    {
        return message.copy_payload_to(opened) && (opened.status == EServiceStatus::unsupported) &&
            operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success) && self_stop(context, false);
    }
    if (!message.copy_payload_to(opened) || (opened.status != EServiceStatus::success) || !opened.provider.value)
    {
        return false;
    }
    if (borrow_scenario())
    {
        return borrowed_resource(context, opened.route);
    }
    if (notification_scenario())
    {
        return notified_resource(context, opened.route);
    }
    if (modal_scenario())
    {
        return modal_resource(context, opened.route);
    }
    if (!operation(context, modules::EModuleAction::unload, modules::EModuleStatus::busy))
    {
        return false;
    }
    constexpr unsigned count = 3u;
    for (unsigned index = 0u; index < count; ++index)
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = index + 1u;
        request.opcode = 1u;
        request.payload_size = 1u;
        request.payload[0] = static_cast<unsigned char>(index);
        if (!context.post(threading::make_pod_message((30 + index), request)))
        {
            return false;
        }
    }
    bool acknowledged[count]{};
    bool completed[count]{};
    for (unsigned index = 0u; index < (count * 2u); ++index)
    {
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.route.value != opened.route.value) ||
            !reply.request.value || (reply.request.value > count))
        {
            return false;
        }
        const auto ordinal = static_cast<unsigned>(reply.request.value - 1u);
        if (message.query_async_slot() != (30 + static_cast<int>(ordinal)))
        {
            return false;
        }
        if (reply.notice == EServiceNotice::acknowledged)
        {
            if (acknowledged[ordinal] || (reply.status != EServiceStatus::success))
            {
                return false;
            }
            acknowledged[ordinal] = true;
        }
        else
        {
            if (!acknowledged[ordinal] || completed[ordinal])
            {
                return false;
            }
            if (scenario("invalid-seal") || scenario("pump-failed"))
            {
                if (reply.status != EServiceStatus::cancelled)
                {
                    return false;
                }
            }
            else if ((reply.status != EServiceStatus::success) || (reply.payload_size != 1u) || (reply.payload[0] != ordinal))
            {
                return false;
            }
            completed[ordinal] = true;
        }
    }
    if (scenario("invalid-seal") || scenario("pump-failed"))
    {
        // Host owns fatal shutdown; accepted replies still settle through the FIFO.
        return context.exit_requested();
    }

    //  One provider, simultaneous contracts, identical opcode and request id.
    if (!context.post(threading::make_pod_message(21, OpenServiceRequest{module_ids::rendering, 2u, 3u, {2u}})) ||
        !receive(context, message))
    {
        return false;
    }
    OpenServiceResult second;
    if (!message.copy_payload_to(second) || (second.status != EServiceStatus::success) || (second.route.value == opened.route.value))
    {
        return false;
    }
    ServiceRequest other;
    other.route = second.route;
    other.request.value = 1u;
    other.opcode = 1u;
    other.payload_size = 1u;
    if (!context.post(threading::make_pod_message(31, other)))
    {
        return false;
    }
    for (const auto notice : {EServiceNotice::acknowledged, EServiceNotice::completed})
    {
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.notice != notice) || (reply.status != EServiceStatus::success) ||
            ((notice == EServiceNotice::completed) && ((reply.payload_size != 1u) || (reply.payload[0] != 33u))))
        {
            return false;
        }
    }
    ServiceReply second_closed;
    if (!context.post(threading::make_pod_message(22, CloseServiceRequest{second.route, {2u}})) || !receive(context, message) ||
        !message.copy_payload_to(second_closed) || (second_closed.status != EServiceStatus::success))
    {
        return false;
    }
    MV_LOG(info, "Pump fixture: simultaneous contracts preserved number and version");

    auto probe = [&](const std::uint16_t opcode, const std::int32_t slot) {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = (opcode == 10u) ? 4u : 6u;
        request.opcode = opcode;
        if (!context.post(threading::make_pod_message(slot, request)))
        {
            return false;
        }
        for (const auto notice : { EServiceNotice::acknowledged, EServiceNotice::completed })
        {
            ServiceReply reply;
            if (!receive(context, message) || !message.copy_payload_to(reply) || (message.query_async_slot() != slot) ||
                (reply.route.value != opened.route.value) || (reply.request.value != request.request.value) || (reply.opcode != opcode) ||
                (reply.notice != notice) || (reply.status != EServiceStatus::success))
            {
                return false;
            }
            if ((opcode == 10u) && (notice == EServiceNotice::completed) && ((reply.payload_size != 1u) || (reply.payload[0] != 5u)))
            {
                return false;
            }
        }
        return true;
    };
    if (!probe(10u, 40)) return false;
    if (scenario("sequence"))
    {
        if (!probe_sequence(context, opened.route, 5u, 0u))
        {
            return false;
        }
        //  This isolated fixture has no other sequence consumers. Seed only to
        //  exercise wrap, then preserve the resulting value across DLL reload.
        system_context::sequence_counter->store(
            (std::numeric_limits<std::uint64_t>::max() - 1u), std::memory_order_relaxed);
        if (!probe_sequence(context, opened.route, 6u, (std::numeric_limits<std::uint64_t>::max() - 1u)))
        {
            return false;
        }
    }
    if (scenario("cleanup-credits"))
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 5u;
        request.opcode = 4u;
        if (!context.post(threading::make_pod_message(44, request)))
        {
            return false;
        }
        for (const auto notice : {EServiceNotice::acknowledged, EServiceNotice::completed})
        {
            ServiceReply reply;
            if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.notice != notice) || (reply.status != EServiceStatus::success))
            {
                return false;
            }
        }
        if (!probe(11u, 41)) return false;
        MV_LOG(info, "Pump fixture: zero-reply cancellation admitted with 512 reserved replies");
        return self_stop(context, false);
    }
    if (scenario("threaded-credits"))
    {
        return threaded_credits(context);
    }
    if (scenario("inbox-full") || scenario("emergency"))
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 5u;
        request.opcode = 3u;
        if (!context.post(threading::make_pod_message(44, request)))
        {
            return false;
        }
        for (const auto notice : {EServiceNotice::acknowledged, EServiceNotice::completed})
        {
            ServiceReply reply;
            if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.notice != notice) || (reply.status != EServiceStatus::success))
            {
                return false;
            }
        }
        if (scenario("emergency"))
        {
            debug_system::get_service()->request_shutdown(debug_system::EShutdownReason::critical_incident);
        }
        return self_stop(context, false);
    }
    if (scenario("shutdown-pending") || (scenario("replacement") && !MV_PUMP_REPLACEMENT))
    {
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 5u;
        request.opcode = 2u;
        if (!context.post(threading::make_pod_message(44, request)) || !receive(context, message))
        {
            return false;
        }
        ServiceReply ack;
        if (!message.copy_payload_to(ack) || (ack.notice != EServiceNotice::acknowledged))
        {
            return false;
        }
        if (!self_stop(context, scenario("replacement")))
        {
            return false;
        }
        if (scenario("replacement"))
        {
            return true;
        } //  The old requester leaves a live, delivered operation.
        ServiceReply reply;
        if (!receive(context, message) || !message.copy_payload_to(reply) || (reply.status != EServiceStatus::success))
        {
            return false;
        }
        MV_LOG(info, "Pump fixture: cleanup reply received after Executive exit request");
    }
    if (!context.post(threading::make_pod_message(50, CloseServiceRequest{opened.route, {7u}})) || !receive(context, message))
    {
        return false;
    }
    ServiceReply closed;
    if (!message.copy_payload_to(closed) || (closed.status != EServiceStatus::success))
    {
        return false;
    }
    if (scenario("shutdown-pending")) return true;
    if (scenario("stop-timeout"))
    {
        return self_stop(context, false);
    }
    if (!operation(context, modules::EModuleAction::unload, (scenario("deactivate-failed") ? modules::EModuleStatus::unload_failed : modules::EModuleStatus::success)))
    {
        return false;
    }
    if (scenario("normal") || scenario("sequence"))
    {
        if (!operation(context, modules::EModuleAction::load, modules::EModuleStatus::success) ||
            !context.post(threading::make_pod_message(60, OpenServiceRequest{module_ids::rendering, 1u, 1u, {1u}})) || !receive(context, message))
        {
            return false;
        }
        OpenServiceResult replacement;
        if (!message.copy_payload_to(replacement) || (replacement.status != EServiceStatus::success) ||
            (replacement.provider.value == opened.provider.value) || (replacement.route.value == opened.route.value))
        {
            return false;
        }
        if (scenario("sequence"))
        {
            if (!probe_sequence(context, replacement.route, 1u, 1u))
            {
                return false;
            }
            MV_LOG(info, "Pump fixture: shared sequence pointer, wrap and reload passed");
        }
        if (!context.post(threading::make_pod_message(61, CloseServiceRequest{opened.route, {6u}})) || !receive(context, message) ||
            !message.copy_payload_to(closed) || (closed.status != EServiceStatus::stale))
        {
            return false;
        }
        if (!context.post(threading::make_pod_message(62, CloseServiceRequest{replacement.route, {2u}})) || !receive(context, message) ||
            !message.copy_payload_to(closed) || (closed.status != EServiceStatus::success) ||
            !operation(context, modules::EModuleAction::unload, modules::EModuleStatus::success))
        {
            return false;
        }
        MV_LOG(info, "Pump fixture: stale route rejected after provider reload");
    }
    return self_stop(context, false);
}
static std::uint32_t MV_STD_ABI_CALL executive_entry(threading::CThreadContext& context, void* const) noexcept
{
    context.startup();
    context.mark_running();
    if (!context.publish_ready()) return 1u;
    const bool success = run(context);
    if (success)
    {
        MV_LOG_RICH(info, "Pump fixture: %s passed", selected());
        context.mark_exiting();
        context.mark_exited();
    }
    else
    {
        MV_LOG(error, "Pump fixture failed");
        context.mark_failed(1u);
    }
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

static std::uint32_t MV_STD_ABI_CALL publish_notifications(void* const data) noexcept
{
    auto& notifier = *static_cast<SNotifier*>(data);
    notifier.data.value.store(1u, std::memory_order_release);
    if (!notifier.endpoint.signal())
    {
        notifier.failed.store(true, std::memory_order_release);
    }
    while (!notifier.finish.load(std::memory_order_acquire))
    {
        platform::threading::processor_relax();
    }
    notifier.data.value.store(2u, std::memory_order_release);
    if (!notifier.endpoint.signal())
    {
        notifier.failed.store(true, std::memory_order_release);
    }
    notifier.done.store(true, std::memory_order_release);
    return 0u;
}

struct SRuntime
{
    explicit SRuntime(const SHostPumpContext& supplied) noexcept : context{ &supplied }, services{ *supplied.endpoint } {}
    const SHostPumpContext* context;
    CProviderClient services;
    SDispatchedCommand commands[256];
    std::uint32_t command_head{ 0u }, command_count{ 0u }, completions{ 0u };
    ServiceReply current_reply;
    bool command_started{ false }, publisher_started{ false }, borrow_retiring{ false };
    bool admission_requested{ false }, admission_closed{ false }, stopped_logged{ false };
    unsigned extra_created{ 0u }, extra_retired{ 0u };
    struct SExtraThread
    {
        SClientToken endpoint;
        bool ended{ false }, stop_sent{ false };
    };
    SExtraThread extra_threads[2];

    TInstance<SModalData> modal;
    SModalDescriptor modal_view;
    TInstance<SNotifier> notifier;
    SClientToken notification_client;
    TInstance<unsigned> borrowed;
    SServiceBorrow borrow;
    ServiceReply borrow_result;
    bool borrow_done{ false }, borrower_quiescent{ false }, borrow_fail{ false };
    unsigned retirement_progress{ 0u };
    CByteBuffer storage;
    std::uint32_t starting{0u}, stopping{0u}, flooded{0u};
    bool delayed{false};
    platform::threading::CWaitDeadline delayed_deadline;
    ServiceReply test_reply;
    ServiceReply pending;
    CAssetId asset;
    bool stop{false};
    bool flood{false}, flood_complete{false};
    CAssetId assets[512];
    CAssetAcquisitionId acquisitions[512];
    unsigned created{0u}, disposals_posted{0u}, releases_posted{0u}, released{0u}, disposed{0u}, credits_wait{0u};
    bool credits{false};
    bool control_drain_pending{ false };
    unsigned control_reposts{ 0u };
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
    ERestrictionStatus restriction_result{ ERestrictionStatus::success };
    EMaintenanceStatus maintenance_result{ EMaintenanceStatus::success };
};
static std::uint32_t MV_STD_ABI_CALL foreign_call(void* const data) noexcept
{
    auto& call = *static_cast<SForeignCall*>(data);
    const auto& execution = *call.context->execution;
    call.restriction_result = execution.enter_restriction(execution.native_context, { 0u }).status;
    call.maintenance_result = execution.maintain_host(execution.native_context).status;
    return 0u;
}
static std::uint32_t MV_STD_ABI_CALL extra_thread(threading::CThreadContext& context, void* const) noexcept
{
    while (!context.exit_requested())
    {
        const std::uint32_t epoch = context.observe_epoch();
        if (!context.exit_requested()) (void)context.wait_for_new_epoch(epoch);
    }
    return 0u;
}
static bool context_valid(const SHostPumpContext* const context) noexcept
{
    return context && (context->size == sizeof(SHostPumpContext)) && (context->version == 1u) &&
        modules::is_thread_context_ready(context) && context->endpoint &&
        context->execution && context->execution->restriction.is_valid() && context->execution->notification.is_valid() &&
        (context->execution->instance.value == context->instance.value) &&
        (system_context::get_ambient_thread_id() == thread_ids::host) &&
        (system_context::get_ambient_module_id() == module_ids::rendering) &&
        (memory::get_ambient_memory_context() != nullptr);
}
template<typename T> static bool post(const SRuntime& state, const int slot, const T& payload)
{
    const auto message = threading::make_pod_message(slot, payload);
    return submission_status(state.context->endpoint->post(message)) == EServiceStatus::success;
}
template<typename T> static bool owner(const SRuntime& state, CErasedOwner&& value, const int slot)
{
    threading::CErasedOwnerMsg message;
    message.set_message_type<T>();
    message.set_async_slot(slot);
    message.set_owner(std::move(value));
    return (submission_status(state.context->endpoint->post(std::move(message))) == EServiceStatus::success) && !message.has_owner();
}
static ECallStatus MV_STD_ABI_CALL activate(const SHostPumpContext* const context, void** const runtime, SProgress* const progress) noexcept
{
    if (!context_valid(context) || !runtime_state().emplace(*context))
    {
        return ECallStatus::failed;
    }
    auto& state = *runtime_state();
    state.context = context;
    if (!state.storage.resize(32u, 32u))
    {
        return ECallStatus::failed;
    }
    SForeignCall foreign{context};
    platform::threading::CThread other;
    if (!other.create(&foreign_call, &foreign) || !other.join_and_close() || (foreign.restriction_result != ERestrictionStatus::invalid_context) ||
        (foreign.maintenance_result != EMaintenanceStatus::invalid_context))
    {
        return ECallStatus::failed;
    }
    if (scenario("activation-null"))
    {
        runtime_state().reset();
        return ECallStatus::failed;
    }
    *runtime = &state;
    if (scenario("modal-activation"))
    {
        const auto scope = context->execution->enter_restriction(context->execution->native_context, { 3u });
        platform::threading::CWaitDeadline hold;
        if ((scope.status != ERestrictionStatus::success) || (hold.set_after_milliseconds(5500u).code != platform::threading::EWaitError::none))
        {
            return ECallStatus::failed;
        }
        for (;;)
        {
            std::uint64_t remaining = 0u;
            if (hold.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                return ECallStatus::failed;
            }
            if (!remaining) break;
            const auto pass = context->execution->maintain_host(context->execution->native_context);
            if ((pass.status != EMaintenanceStatus::success) || (pass.operations > 32u) || pass.cancel_native)
            {
                return ECallStatus::failed;
            }
            platform::threading::processor_relax();
        }
        if (context->execution->leave_restriction(context->execution->native_context, scope.scope) != ERestrictionStatus::success)
        {
            return ECallStatus::failed;
        }
        MV_LOG(info, "Pump provider: activation excluded simulated restriction beyond five seconds");
    }
    if (scenario("mixed"))
    {
        for (const thread_ids::id_type thread : { thread_ids::rendering, thread_ids::audio })
        {
            const threading::CreateThreadRequest request{ &extra_thread, nullptr, thread };
            if (!context->endpoint->post(threading::make_pod_message(40, request)))
            {
                return ECallStatus::failed;
            }
        }
    }
    progress->phase = EPumpPhase::starting;
    progress->stop_requested = scenario("unauthorised-native-stop") ? 1u : 0u;
    progress->native_events_active = scenario("unauthorised-native-events") ? 1u : 0u;
    MV_LOG(info, "Pump provider: activation context and foreign-thread rejection passed");
    return scenario("activation-failed") ? ECallStatus::failed : ECallStatus::success;
}
static bool submit_completion(SRuntime& state, const ServiceReply& reply) noexcept
{
    if (!state.context->endpoint->post(threading::make_pod_message(-1, CompleteServiceRequest{ reply })))
    {
        return false;
    }
    ++state.completions;
    return true;
}
static bool test_message(SRuntime& state, const threading::CErasedPodMsg& message)
{
    if (message.is_payload_a<ServiceRequestCancelled>())
    {
        return true;
    }
    const auto slot = message.query_async_slot();
    if (slot == 101)
    {
        filesystem::FilesystemRefreshResult result;
        if (!message.copy_payload_to(result) || (result.status != filesystem::EFilesystemStatus::success))
        {
            return false;
        }
        auto value = CErasedOwner::create<RawAssetTransfer>();
        auto* request = value.payload<RawAssetTransfer>();
        return request && request->storage.value.resize(32u, 32u) && owner<RawAssetTransfer>(state, std::move(value), 102);
    }
    if (slot == 102)
    {
        AssetResult result;
        if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success))
        {
            return false;
        }
        state.asset = result.asset;
        return post(state, 103, AssetAcquireRequest{state.asset});
    }
    if (slot == 103)
    {
        AssetAcquireResult result;
        return message.copy_payload_to(result) && (result.status == EAssetStatus::success) && post(state, 104, AssetReleaseRequest{result.acquisition});
    }
    if (slot == 104)
    {
        AssetReleaseResult result;
        return message.copy_payload_to(result) && (result.status == EAssetStatus::success) && post(state, 105, AssetDisposeRequest{state.asset});
    }
    if (slot == 105)
    {
        AssetDisposeResult result;
        if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success))
        {
            return false;
        }
        auto request = module_request(module_ids::rendering, modules::EModuleAction::unload);
        request.set_async_slot(106);
        return submission_status(state.context->endpoint->post(std::move(request))) == EServiceStatus::success;
    }
    modules::ModuleResult result;
    if (!message.copy_payload_to(result)) return false;
    if (result.notice == modules::EModuleNotice::acknowledged)
    {
        return result.status == modules::EModuleStatus::success;
    }
    if (result.status != modules::EModuleStatus::invalid_request)
    {
        return false;
    }
    if (slot == 106)
    {
        auto request = module_request(module_ids::executive, modules::EModuleAction::unload);
        request.set_async_slot(107);
        return submission_status(state.context->endpoint->post(std::move(request))) == EServiceStatus::success;
    }
    if (slot != 107) return false;
    MV_LOG(info, "Pump provider: refresh, assets, acquisitions and requester restrictions passed");
    state.test_reply.payload_size = 1u;
    state.test_reply.payload[0] = 5u;
    return submit_completion(state, state.test_reply);
}
static bool create_credit_asset(const SRuntime& state)
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
        if (!message.copy_payload_to(asset) || (asset.status != EAssetStatus::success) || (state.created >= 512u))
        {
            return false;
        }
        state.assets[state.created] = asset.asset;
        return post(state, 211, AssetAcquireRequest{asset.asset});
    }
    if (slot == 211)
    {
        AssetAcquireResult acquired;
        if (!message.copy_payload_to(acquired) || (acquired.status != EAssetStatus::success) || (state.created >= 512u))
        {
            return false;
        }
        state.acquisitions[state.created++] = acquired.acquisition;
        return (state.created == 512u) || create_credit_asset(state);
    }
    AssetReleaseResult release;
    AssetDisposeResult disposal;
    if ((slot == 213) && message.copy_payload_to(release) && (release.status == EAssetStatus::success))
    {
        ++state.released;
        return true;
    }
    if ((slot == 212) && message.copy_payload_to(disposal) && (disposal.status == EAssetStatus::success))
    {
        ++state.disposed;
        return true;
    }
    return false;
}
static bool native_modal(SRuntime& state) noexcept
{
    if (!state.modal || !state.modal->ready.load())
    {
        return false;
    }
    const auto& execution = *state.context->execution;
    void* const native = execution.native_context;
    const auto a = execution.enter_restriction(native, { 0u });
    const auto b = execution.enter_restriction(native, { 0u });
    const auto c = execution.enter_restriction(native, { 1u });
    if ((a.status != ERestrictionStatus::success) || (b.status != ERestrictionStatus::success) || (c.status != ERestrictionStatus::success))
    {
        return false;
    }
    state.modal->entered.store(1u, std::memory_order_release);
    platform::threading::CWaitDeadline hold;
    if (hold.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    const ServiceReply retained = state.current_reply;
    for (;;)
    {
        const auto pass = execution.maintain_host(native);
        if ((pass.status != EMaintenanceStatus::success) || (pass.operations > 32u) || !context_valid(state.context))
        {
            return false;
        }
        state.modal->passes.fetch_add(1u, std::memory_order_release);
        if (pass.capacity_pressure) state.modal->pressure.store(1u, std::memory_order_release);
        if ((retained.route.value != state.current_reply.route.value) || (retained.request.value != state.current_reply.request.value))
        {
            return false;
        }
        if (state.modal->leave.load(std::memory_order_acquire) || pass.cancel_native)
        {
            break;
        }
        std::uint64_t remaining = 0u;
        if ((hold.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
        platform::threading::processor_relax();
    }
    for (unsigned count = 0u; count < 64u; ++count) (void)execution.maintain_host(native);
    if ((execution.leave_restriction(native, a.scope) != ERestrictionStatus::success) ||
        (execution.leave_restriction(native, a.scope) != ERestrictionStatus::invalid_scope) ||
        (execution.leave_restriction(native, c.scope) != ERestrictionStatus::success) ||
        (execution.leave_restriction(native, b.scope) != ERestrictionStatus::success)) return false;
    MV_LOG(info, "Pump provider: nested modal maintenance preserved accepted command state");
    return true;
}

static EServiceStatus progress_command(SRuntime& state) noexcept
{
    if (!state.command_count)
    {
        return EServiceStatus::success;
    }
    const SDispatchedCommand& dispatched = state.commands[state.command_head];
    const ServiceRequest& command = dispatched.command;
    if (!state.command_started)
    {
        state.current_reply = {};
        state.current_reply.route = command.route;
        state.current_reply.request = command.request;
        state.current_reply.opcode = command.opcode;
        state.current_reply.status = (state.stop && (scenario("invalid-seal") || scenario("pump-failed"))) ?
            EServiceStatus::cancelled : EServiceStatus::success;
        state.current_reply.payload_size = command.payload_size;
        std::memcpy(state.current_reply.payload, command.payload, sizeof(command.payload));
        if (dispatched.contract.number == 2u) state.current_reply.payload[0] = 33u;
        if (command.opcode == 12u)
        {
            const SSequenceProbe probe{ system_context::sequence_counter, system_context::sequence_counter->fetch_add(1u) };
            state.current_reply.payload_size = sizeof(probe);
            std::memcpy(state.current_reply.payload, &probe, sizeof(probe));
        }
        state.command_started = true;
    }
    if ((command.opcode == 5u) || (command.opcode == 6u) || (command.opcode == 8u))
    {
        SServiceView view;
        if (command.opcode == 5u)
        {
            if (!state.borrowed && !state.borrowed.emplace(4242u))
            {
                return EServiceStatus::allocation_failed;
            }
            view = { state.borrowed.operator->(), sizeof(unsigned), 1u };
        }
        else if (command.opcode == 6u)
        {
            if (!state.notifier && !state.notifier.emplace())
            {
                return EServiceStatus::allocation_failed;
            }
            view = { &state.notifier->data, sizeof(SNotificationData), 1u };
        }
        else
        {
            if (!state.modal && !state.modal.emplace())
            {
                return EServiceStatus::allocation_failed;
            }
            state.modal_view.state = &*state.modal;
            view = { &state.modal_view, sizeof(SModalDescriptor), 1u };
        }
        const SServiceBorrowOffer offer{ command.route, command.request, view };
        const EServiceStatus prepared = state.services.prepare_borrow(&offer, &state.borrow);
        if (prepared != EServiceStatus::success)
        {
            return prepared;
        }
        state.notification_client = dispatched.client;
        state.borrow_result = state.current_reply;
        state.borrower_quiescent = state.borrow_done = state.borrow_retiring = false;
        if ((command.opcode == 8u) && !post(state, 8801, AssetDisposeRequest{}))
        {
            return EServiceStatus::delivery_failed;
        }
        if ((command.opcode != 6u) && !scenario("borrow-cancel"))
        {
            if (!submit_completion(state, state.borrow_result))
            {
                return EServiceStatus::delivery_failed;
            }
            state.borrow_done = true;
        }
        state.borrow_fail = scenario("borrow-provider-failed");
    }
    else if (command.opcode == 7u)
    {
        SNotificationReady ready;
        if (!state.notifier || (command.payload_size != sizeof(ready)) || (dispatched.client.value != state.notification_client.value))
        {
            return EServiceStatus::invalid_request;
        }
        std::memcpy(&ready, command.payload, sizeof(ready));
        if (!state.publisher_started)
        {
            const EServiceStatus bound = state.services.bind_borrow_notification(ready.borrow, ready.registration, &state.notifier->endpoint);
            if (bound != EServiceStatus::success)
            {
                return bound;
            }
            if (!state.notifier->publisher.create(&publish_notifications, &*state.notifier))
            {
                return EServiceStatus::native_failure;
            }
            state.publisher_started = true;
        }
        if (!submit_completion(state, state.current_reply))
        {
            return EServiceStatus::delivery_failed;
        }
        if (!scenario("notify-cancel"))
        {
            if (!submit_completion(state, state.borrow_result))
            {
                return EServiceStatus::delivery_failed;
            }
            state.borrow_done = true;
        }
    }
    else if (command.opcode == 9u)
    {
        if (!state.modal || !state.modal->ready.load())
        {
            return EServiceStatus::busy;
        }
        if (!native_modal(state) || !submit_completion(state, state.current_reply))
        {
            return EServiceStatus::native_failure;
        }
    }
    else if (command.opcode == 10u)
    {
        state.test_reply = state.current_reply;
        auto value = CErasedOwner::create<filesystem::FilesystemRefreshRequest>();
        auto* const refresh = value.payload<filesystem::FilesystemRefreshRequest>();
        if (!refresh || !refresh->root.set("dev-source:") || !owner<filesystem::FilesystemRefreshRequest>(state, std::move(value), 101))
        {
            return EServiceStatus::delivery_failed;
        }
    }
    else if (command.opcode == 11u)
    {
        if ((state.credits_wait != 16u) || (state.disposals_posted != 512u))
        {
            return EServiceStatus::invalid_request;
        }
        for (unsigned count = 0u; (count < 64u) && (state.control_reposts < 1024u); ++count)
        {
            if (!post(state, -1, CancelServiceRequest{}))
            {
                return EServiceStatus::busy;
            }
            ++state.control_reposts;
        }
        if (state.control_reposts != 1024u)
        {
            return EServiceStatus::busy;
        }
        if (!submit_completion(state, state.current_reply))
        {
            return EServiceStatus::delivery_failed;
        }
    }
    else if (command.opcode == 4u)
    {
        state.pending = state.current_reply;
        state.credits = true;
        if (!create_credit_asset(state))
        {
            return EServiceStatus::delivery_failed;
        }
    }
    else if (command.opcode == 3u)
    {
        state.pending = state.current_reply;
        state.flood = true;
    }
    else if (command.opcode == 2u)
    {
        state.pending = state.current_reply;
        if (state.delayed_deadline.set_after_milliseconds(100u).code != platform::threading::EWaitError::none)
        {
            return EServiceStatus::native_failure;
        }
        state.delayed = true;
    }
    else if (!submit_completion(state, state.current_reply)) return EServiceStatus::delivery_failed;
    state.commands[state.command_head] = {};
    state.command_head = (state.command_head + 1u) & 255u;
    --state.command_count;
    state.command_started = false;
    return EServiceStatus::success;
}

static bool retire_borrow(SRuntime& state) noexcept
{
    if (!state.borrow.value) return true;
    if (state.stop && !state.borrow_retiring)
    {
        const EServiceStatus status = state.services.begin_borrow_retirement(state.borrow);
        if (status == EServiceStatus::success) state.borrow_retiring = true;
        else if (status != EServiceStatus::busy) return false;
    }
    if (!state.borrower_quiescent) return true;
    if (state.notifier && state.publisher_started)
    {
        state.notifier->finish.store(true, std::memory_order_release);
        if (!state.notifier->done.load(std::memory_order_acquire))
        {
            return true;
        }
        if (!state.notifier->publisher.join_and_close() || state.notifier->failed.load())
        {
            return false;
        }
        state.publisher_started = false;
        MV_LOG(info, "Pump provider: final notification signal drained after consumer quiescence");
    }
    if (++state.retirement_progress < 3u) return true;
    const EServiceStatus status = state.services.finish_borrow_retirement(state.borrow);
    if (status == EServiceStatus::busy) return true;
    if (status != EServiceStatus::success) return false;
    if (!state.borrow_done && !submit_completion(state, state.borrow_result))
    {
        return false;
    }
    state.borrow_done = true;
    state.borrow = {};
    state.borrowed.reset();
    state.notifier.reset();
    if (state.modal) MV_LOG(info, "Pump provider: modal borrow retired after invocation and quiescence");
    state.modal_view = {};
    state.modal.reset();
    MV_LOG(info, "Pump provider: provider-owned storage destroyed after publisher drain");
    return true;
}

static ECallStatus MV_STD_ABI_CALL pump(void* const runtime, const SPumpInput* const input, SPumpResult* const result) noexcept
{
    auto& state = *static_cast<SRuntime*>(runtime);
    if (!context_valid(state.context) || !input || !result)
    {
        return ECallStatus::failed;
    }
    *result = {};
    result->progress.phase = state.stop ? EPumpPhase::stopping : EPumpPhase::ready;
    for (unsigned count = 0u; count < 64u; ++count)
    {
        threading::CErasedPodMsg message;
        if (!state.context->endpoint->read(message)) break;
        ServiceCommand notice;
        ProviderOperationResult operation;
        ServiceBorrowControl control;
        threading::EndpointLifecycle lifecycle;
        if (message.copy_payload_to(operation))
        {
            if (message.query_async_slot() == -1)
            {
                if (!state.completions || (operation.status != EServiceStatus::success))
                {
                    return ECallStatus::failed;
                }
                --state.completions;
            }
            else if (!state.services.receive(message)) return ECallStatus::failed;
        }
        else if (message.copy_payload_to(notice))
        {
            if (!notice.operation || !notice.descriptor || (state.command_count == 256u))
            {
                return ECallStatus::failed;
            }
            state.commands[(state.command_head + state.command_count) & 255u] = *notice.descriptor;
            ++state.command_count;
        }
        else if (message.copy_payload_to(control))
        {
            if (control.borrow.value != state.borrow.value)
            {
                return ECallStatus::failed;
            }
            if (control.control == EServiceBorrowControl::consumer_quiescent)
            {
                state.borrower_quiescent = true;
                MV_LOG(info, "Pump provider: consumer quiescence received; draining publisher activity");
            }
        }
        else if (message.copy_payload_to(lifecycle))
        {
            if (lifecycle.status != EServiceStatus::success)
            {
                return ECallStatus::failed;
            }
            if (lifecycle.event == threading::EEndpointLifecycle::created)
            {
                if (state.extra_created == 2u)
                {
                    return ECallStatus::failed;
                }
                state.extra_threads[state.extra_created++].endpoint = lifecycle.endpoint;
            }
            if (lifecycle.event == threading::EEndpointLifecycle::execution_ended)
            {
                for (SRuntime::SExtraThread& thread : state.extra_threads)
                {
                    if (thread.endpoint.value == lifecycle.endpoint.value) thread.ended = true;
                }
            }
            if (lifecycle.event == threading::EEndpointLifecycle::retired) ++state.extra_retired;
        }
        else if (message.is_payload_a<threading::CreateThreadResult>()) {}
        else if (message.is_payload_a<threading::StopThreadResult>())
        {
            threading::StopThreadResult stopped;
            if (!message.copy_payload_to(stopped) || (stopped.status != EServiceStatus::success))
            {
                return ECallStatus::failed;
            }
        }
        else if (message.is_payload_a<threading::ProviderAdmissionClosed>()) state.admission_closed = true;
        else if (message.is_payload_a<threading::EndpointSealed>() || message.is_payload_a<ServiceRequestCancelled>()) {}
        else if ((message.query_async_slot() == 8801) && state.modal) state.modal->ready.store(1u, std::memory_order_release);
        else if (message.query_async_slot() == 200) {}
        else if (!(state.credits ? credit_message(state, message) : test_message(state, message))) return ECallStatus::failed;
        if (message.is_payload_a<ProviderOperationResult>() && state.command_count)
        {
            const EServiceStatus status = progress_command(state);
            if ((status != EServiceStatus::success) && (status != EServiceStatus::busy))
            {
                return ECallStatus::failed;
            }
        }
    }
    if (!state.stop && (scenario("activation-timeout") || (++state.starting < 3u)))
    {
        result->progress.phase = EPumpPhase::starting;
        return ECallStatus::success;
    }
    if (state.command_count)
    {
        if (!state.stop && (scenario("invalid-seal") || scenario("pump-failed")))
        {
            if (scenario("invalid-seal") &&
                (state.context->endpoint->post(threading::make_pod_message(-1, threading::SealEndpoint{})).status !=
                    threading::EEndpointPost::invalid)) return ECallStatus::invalid_argument;
            state.stop = true;
            return ECallStatus::failed;
        }
        const EServiceStatus status = progress_command(state);
        if ((status != EServiceStatus::success) && (status != EServiceStatus::busy))
        {
            return ECallStatus::failed;
        }
    }
    if (state.borrow_fail)
    {
        state.borrow_fail = false;
        return ECallStatus::failed;
    }
    if (!retire_borrow(state)) return ECallStatus::failed;
    if (state.delayed)
    {
        std::uint64_t remaining = 0u;
        if (state.delayed_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
        {
            return ECallStatus::failed;
        }
        if (!remaining)
        {
            if (!submit_completion(state, state.pending))
            {
                return ECallStatus::failed;
            }
            state.delayed = false;
            MV_LOG(info, "Pump provider: delayed operation completed");
        }
    }
    if (state.flood && !state.flood_complete)
    {
        for (unsigned count = 0u; (count < 64u) && (state.flooded < 1024u); ++count)
        {
            if (!post(state, 200, AssetDisposeRequest{}))
            {
                break;
            }
            ++state.flooded;
        }
        if (state.flooded == 1024u)
        {
            if (!submit_completion(state, state.pending))
            {
                return ECallStatus::failed;
            }
            state.flood_complete = true;
            MV_LOG(info, "Pump provider: growable inbox and outbound traffic exercised");
        }
    }
    if (state.credits && (state.created == 512u) && (state.credits_wait < 16u))
    {
        for (unsigned count = 0u; (count < 64u) && (state.disposals_posted < 512u); ++count)
        {
            if (!post(state, 212, AssetDisposeRequest{ state.assets[state.disposals_posted] }))
            {
                break;
            }
            ++state.disposals_posted;
        }
        if ((state.disposals_posted == 512u) && (++state.credits_wait == 16u))
        {
            if (!submit_completion(state, state.pending))
            {
                return ECallStatus::failed;
            }
            MV_LOG(info, "Pump provider: 512 disposal credits retained by live acquisitions");
        }
    }
    if (state.stop)
    {
        for (SRuntime::SExtraThread& thread : state.extra_threads)
        {
            if (!thread.endpoint.value || thread.ended || thread.stop_sent)
            {
                continue;
            }
            const threading::SEndpointPost posted = state.context->endpoint->post(
                threading::make_pod_message(41, threading::StopThreadRequest{ thread.endpoint }));
            if (!posted && (posted.status != threading::EEndpointPost::busy))
            {
                return ECallStatus::failed;
            }
            thread.stop_sent = posted;
        }
        if (!state.admission_requested) state.admission_requested = post(state, -2, threading::CloseProviderAdmission{});
        if (state.credits)
        {
            for (unsigned count = 0u; (count < 64u) && (state.releases_posted < state.created); ++count)
            {
                if (!post(state, 213, AssetReleaseRequest{ state.acquisitions[state.releases_posted] }))
                {
                    break;
                }
                ++state.releases_posted;
            }
            if ((state.released != state.created) || (state.disposed != state.created))
            {
                return ECallStatus::success;
            }
            if (!state.stopping) MV_LOG(info, "Pump provider: 512 retained disposals completed through common FIFO settlement");
        }
        if (scenario("stop-timeout"))
        {
            std::uint64_t remaining = 0u;
            if (state.stop_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                return ECallStatus::failed;
            }
            if (remaining) return ECallStatus::success;
            if (!state.stopping) MV_LOG(info, "Pump provider: live state survived shutdown grace expiry");
        }
        if (state.admission_closed && !state.command_count && !state.completions && state.services.idle() &&
            !state.borrow.value && !state.delayed && (state.extra_created == state.extra_retired))
        {
            if (++state.stopping >= 3u) (void)state.context->endpoint->seal();
        }
        if (state.context->endpoint->sealed())
        {
            result->progress.phase = EPumpPhase::stopped;
            if (!state.stopped_logged) MV_LOG(info, "Pump provider: stopped after continued progress");
            state.stopped_logged = true;
        }
    }
    result->progress.work_pending = state.command_count ? 1u : 0u;
    return ECallStatus::success;
}

static ECallStatus MV_STD_ABI_CALL begin_stop(void* const runtime, const EStopReason reason, SProgress* const progress) noexcept
{
    auto& state = *static_cast<SRuntime*>(runtime);
    state.stop = true;
    if (scenario("activation-timeout"))
    {
        if (reason != EStopReason::activation_failure)
        {
            return ECallStatus::failed;
        }
        MV_LOG(info, "Pump provider: activation deadline initiated cooperative stop");
    }
    if (scenario("stop-timeout") && (state.stop_deadline.set_after_milliseconds(11000u).code != platform::threading::EWaitError::none))
    {
        return ECallStatus::failed;
    }
    progress->phase = EPumpPhase::stopping;
    MV_LOG(info, "Pump provider: begin stop");
    return ECallStatus::success;
}
static ECallStatus MV_STD_ABI_CALL deactivate(void* const runtime) noexcept
{
    auto& state = *static_cast<SRuntime*>(runtime);
    if (!context_valid(state.context) || !state.context->endpoint->sealed())
    {
        return ECallStatus::failed;
    }
    const auto forbidden = threading::make_pod_message(90, AssetAcquireRequest{});
    if (state.context->endpoint->post(forbidden).status != threading::EEndpointPost::closed)
    {
        return ECallStatus::failed;
    }
    if (scenario("deactivate-failed"))
    {
        MV_LOG(info, "Pump provider: quiescent state retained");
        return ECallStatus::failed;
    }
    runtime_state().reset();
    MV_LOG(info, "Pump provider: deactivated");
    return ECallStatus::success;
}
#endif

static constexpr std::uint32_t k_advertised_version_minor{0u};
static EBindingResult MV_STD_ABI_CALL query_function(const system_type_id type, const std::uint32_t, FModuleFunction* const function) noexcept
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

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v1(modules::SBootstrapFunctions* const functions) noexcept
{
    return pump_tests::s_binding.bootstrap(functions);
}
#if MV_PUMP_EXECUTIVE
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_host_pump_v1(
    const std::uint32_t capacity, modules::SHostPumpV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value)))
    {
        return modules::EBindingResult::invalid_argument;
    }
    *value = modules::TServiceThreadPump<&pump_tests::executive_entry, &thread_ids::executive, false>::capability();
    return modules::EBindingResult::success;
}
#else
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_host_pump_v1(const std::uint32_t capacity, modules::SHostPumpV1* const value) noexcept
{
    using namespace pump_tests;
    if ((capacity < sizeof(*value)) || !value)
    {
        return EBindingResult::invalid_argument;
    }
    *value = {sizeof(*value), 1u, 0u, &activate, &pump, &begin_stop, &deactivate};
    if (scenario("bad-size")) --value->size;
    if (scenario("bad-version")) ++value->version;
    if (scenario("bad-flags")) value->flags = 2u;
    if (scenario("bad-callback")) value->pump = nullptr;
    return EBindingResult::success;
}
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_provider_v1(
    const std::uint32_t capacity, modules::SServiceProviderV1* const value) noexcept
{
    using namespace pump_tests;
    if ((value == nullptr) || (capacity != sizeof(*value)))
    {
        return EBindingResult::invalid_argument;
    }
    static const SServiceContract contracts[]{ {1u, 1u}, {2u, 3u} };
    static const SServiceContract invalid[]{ {1u, 0u} };
    static const SServiceContract duplicate[]{ {1u, 1u}, {1u, 2u} };
    *value = {sizeof(*value), 1u, 0u, 2u, contracts};
    if (scenario("bad-provider-size")) --value->size;
    if (scenario("bad-provider-version")) ++value->version;
    if (scenario("bad-provider-flags")) value->flags = 1u;
    if (scenario("bad-provider-count"))
    {
        value->contract_count = 17u;
    }
    if (scenario("bad-provider-null"))
    {
        value->contracts = nullptr;
    }
    if (scenario("bad-provider-contract"))
    {
        value->contracts = invalid;
        value->contract_count = 1u;
    }
    if (scenario("bad-provider-duplicate"))
    {
        value->contracts = duplicate;
    }
    if (scenario("bad-provider-query"))
    {
        return EBindingResult::unsupported_function;
    }
    if (scenario("empty-provider"))
    {
        value->contracts = nullptr;
        value->contract_count = 0u;
    }
    return EBindingResult::success;
}
#endif
