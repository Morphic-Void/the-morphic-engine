
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    consumer.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Private consumer state shared by lifecycle, scenarios and frozen reporting.

#pragma once

#include "system/storage_type_bindings.hpp"
#include "module/module_messages.hpp"
#include "module/module_binding_context.hpp"
#include "tests/module_lifecycle/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/input_schema.hpp"
#include "tests/input_host/control.hpp"
#include "tests/input_host/schema.hpp"
#include "framework/platform/input_reader.hpp"
#include "framework/platform/controllers.hpp"
#include "services/service_borrow_client.hpp"
#include "assets/asset_messages.hpp"
#include "threading/CThreadPackage.hpp"
#include "threading/messages/message_helpers.hpp"
#include "containers/TInstance.hpp"
#include "debug/macros.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace input_host
{
using namespace services;
using namespace platform::input;
using platform::threading::CWaitDeadline;
using platform::threading::EWaitError;

//  Fixture action IDs, authored independently of Platform mapper tables.
inline constexpr std::uint32_t k_key_a_action = 7u, k_south_action = 100u, k_east_action = 101u;
inline constexpr std::uint32_t k_left_stick_action = 114u, k_right_stick_action = 115u;
inline constexpr std::uint32_t k_left_trigger_action = 116u, k_right_trigger_action = 117u;

struct SReadTrace
{
    std::uint64_t provider{}, connection{}, generation{}, sequence{}, read{}, applied{};
    std::uint32_t stream{}, outcome{};
};
struct SBorrow
{
    SServiceRoute route;
    SServiceRequestId request;
    SServiceBorrow token;
    bool closing{}, quiescent{}, live{}, terminal{}, acknowledged{}, submitted{}, cancel_sent{};
    EServiceStatus expected{ EServiceStatus::success }, status{ EServiceStatus::success };
};
struct SPipe
{
    TInstance<CInputReader> reader;
    const SInputEndpointV4* view{};
    SBorrow* borrow{};
    SBoundaryReady boundary;
    SInputEvent retained;
    SReadTrace retained_trace;
    std::uint64_t pending{}, applied{}, cancellations{}, stale{}, last_sequence{};
    float values[256][2]{};
    bool has_retained{}, drained{};
};
struct SWindow
{
    std::uint64_t begin{}, end{}, offered{}, admitted{}, late_max{}, wait_begin{}, wait_end{}, round_begin{}, round_end{};
    std::uint32_t rate{}, controllers{};
    std::uint64_t expected[k_pipe_count]{};
    std::uint64_t awake_begin{}, awake_end{};
};
enum class EAssetPending { none, transfer, acquire, release, dispose };

inline bool option(const char* const name, const char* const expected) noexcept
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

class CConsumer
{
public:
    explicit CConsumer(::threading::CThreadContext& context) noexcept : thread{ context } {}
    bool run();
    bool run_secondary();
private:
    ::threading::CThreadContext& thread;
    CServiceBorrowClient client;
    SBorrow borrows[8];
    SPipe pipes[k_pipe_count];
    SServiceRoute input_route, window_route, control_route;
    SShared* shared{};
    SServiceBorrow control_borrow;
    platform::windows::SWindowToken window;
    std::uintptr_t native_window{};
    SInstallationToken installations[2];
    CAssetId source_asset;
    CAssetAcquisitionId source_acquisition;
    std::uint64_t pending_schema{};
    EAssetPending asset_pending{ EAssetPending::none };
    TPodVector<SReadTrace> reads;
    std::uint32_t read_count{}, trace_overflow{}, borrow_count{};
    std::uint64_t next_request{}, waiting{}, next_observation{}, waits{}, rounds{}, discarded{};
    ServiceReply response;
    ::threading::CErasedPodMsg host_response;
    bool failed{}, response_ready{}, host_ready{}, host_waiting{}, native{}, ordinary{}, cleaning{}, secondary{}, shutdown_requested{};
    std::uint32_t pause_mask{};
    std::uint64_t retired_requests[32]{};
    SWindow windows[3];
    SWindow* measuring{};
    std::uint64_t intended{};
    std::uint32_t window_count{};

    bool handle(const ::threading::CErasedPodMsg& message);
    bool service();
    bool cycle(const std::uint64_t wait_ns);
    template<class F> bool until(const F& predicate, const std::uint64_t milliseconds = 10000u)
    {
        CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(milliseconds).code != EWaitError::none)
        {
            return false;
        }
        while (!predicate())
        {
            std::uint64_t remaining = 0u;
            if (failed || (deadline.remaining_nanoseconds(remaining).code != EWaitError::none) || !remaining)
            {
                MV_LOG(error, "Input Host: timeout/failure waiting for request {}", waiting);
                return false;
            }
            if (!cycle((remaining < 10000000u) ? remaining : 10000000u))
            {
                return false;
            }
        }
        return !failed;
    }
    template<class T> bool post(const T& message)
    {
        constexpr bool asset_request = std::is_same_v<T, AssetAcquireRequest> || std::is_same_v<T, AssetReleaseRequest> || std::is_same_v<T, AssetDisposeRequest>;
        if constexpr (std::is_same_v<T, AssetAcquireRequest>) asset_pending = EAssetPending::acquire;
        if constexpr (std::is_same_v<T, AssetReleaseRequest>) asset_pending = EAssetPending::release;
        if constexpr (std::is_same_v<T, AssetDisposeRequest>) asset_pending = EAssetPending::dispose;
        if (thread.post(::threading::make_pod_message(1, message)))
        {
            return true;
        }
        if constexpr (asset_request) asset_pending = EAssetPending::none;
        failed = true;
        return false;
    }
    bool module(const modules::EModuleAction action, const module_ids::id_type module = module_ids::platform, const bool threaded = false);
    bool open(const SServiceContract contract, SServiceRoute& route);
    bool transact(const SServiceRoute route, const std::uint16_t opcode,
        const void* const data, const std::uint16_t size, SBorrow* const acquisition = nullptr,
        const EServiceStatus expected = EServiceStatus::success);
    template<class T> bool command(const ECommand opcode, const T& payload, const EServiceStatus expected = EServiceStatus::success)
    { return transact(input_route, static_cast<std::uint16_t>(opcode), &payload, sizeof(payload), nullptr, expected); }
    bool acquire(const SServiceRoute route, const std::uint16_t opcode, const void* const data,
        const std::uint16_t size, SBorrow*& result, const EServiceStatus expected = EServiceStatus::success);
    bool notification(const SBorrow& borrow);
    bool release(SBorrow& borrow);
    bool install(const std::string& schema, SInstallationToken& installation);
    bool release_source();
    bool setup();
    bool acquire_pipe(const std::uint32_t index);
    bool teardown();
    bool feed(SFeed frame);
    bool settle(const std::uint64_t milliseconds);
    bool active();
    bool exercise();
    bool pressure();
    bool load();
    bool second_consumer();
    bool transitions();
    bool work_pressure();
    bool shutdown_blocked();
    bool ingress_loss();
    bool native_pass();
    bool acquisition_cleanup();
    bool source_cleanup();
    bool report();
};

}
