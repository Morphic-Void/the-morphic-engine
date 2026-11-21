
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_pump.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Required module lifecycle pump using a common endpoint without native waits.

#pragma once

#include <cstdint>
#include <type_traits>

#include "framework/module/module_binding.hpp"
#include "framework/module/host_execution_context.hpp"
#include "framework/threading/CEndpointContext.hpp"
#include "framework/services/service_protocol.hpp"
#include "framework/services/service_borrow.hpp"
#include "framework/services/service_notifications.hpp"
#include "foundation/platform/threading/owner_wait.hpp"

namespace threading { class CErasedPodMsg; class CErasedOwnerMsg; }

namespace modules
{

enum class ECallStatus : std::uint32_t { success = 0u, invalid_argument, unsupported_version, failed };
enum class EPumpPhase : std::uint32_t { starting = 0u, ready, stopping, stopped, failed };
enum class EStopReason : std::uint32_t { requested = 0u, replacement, shutdown, activation_failure, pump_failure };

struct SProgress
{
    EPumpPhase phase{ EPumpPhase::starting };
    std::uint32_t error_domain{ 0u };
    std::uint32_t error_code{ 0u };
    std::uint32_t work_pending{ 0u }; //  Another invocation can make progress now.
    std::uint32_t native_events_active{ 0u }; //  Exact owner can service this thread's native queue.
    std::uint32_t stop_requested{ 0u }; //  Durable native application-stop request.
};

struct SPumpInput
{
    std::uint32_t size{ sizeof(SPumpInput) };
    std::uint32_t version{ 1u };
    std::uint32_t notifications_pending{ 0u }; //  Hint only; drain and recheck registration state.
    std::uint64_t time_budget_ns{ 2000000u };
};

struct SPumpResult
{
    SProgress progress;
};

struct SHostPumpContext
{
    std::uint32_t size{ sizeof(SHostPumpContext) };
    std::uint32_t version{ 1u };
    SModuleInstance instance;
    services::SClientToken client;
    thread_ids::id_type host_thread{};
    threading::CEndpointContext* endpoint{ nullptr };
    const SHostExecutionContext* execution{ nullptr };
    platform::threading::SWakeEndpoint owner_wake; //  Retained through all module borrows.
};

inline constexpr char k_host_pump_symbol_name[]{ "morphic_module_host_pump_v1" };
inline constexpr std::uint32_t k_host_pump_native_events = 1u;
struct SHostPumpV1
{
    [[nodiscard]] bool is_valid() const noexcept;

    std::uint32_t size{ sizeof(SHostPumpV1) };
    std::uint32_t version{ 1u };
    std::uint32_t flags{ 0u };
    ECallStatus(MV_STD_ABI_CALL* activate)(const SHostPumpContext* const, void** const, SProgress* const) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* pump)(void* const, const SPumpInput* const, SPumpResult* const) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* begin_stop)(void* const, const EStopReason, SProgress* const) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* deactivate)(void* const) noexcept{ nullptr };
};
using FQueryHostPump = EBindingResult(MV_STD_ABI_CALL*)(const std::uint32_t, SHostPumpV1* const) noexcept;

inline bool SHostPumpV1::is_valid() const noexcept
{
    if ((size != sizeof(SHostPumpV1)) || (version != 1u) || ((flags & ~k_host_pump_native_events) != 0u) ||
        (activate == nullptr) || (pump == nullptr) ||
        (begin_stop == nullptr) || (deactivate == nullptr))
    {
        return false;
    }
    return true;
}

static_assert(std::is_standard_layout_v<SHostPumpV1> && std::is_trivially_copyable_v<SHostPumpV1>);
static_assert(std::is_standard_layout_v<SHostPumpContext> && std::is_trivially_copyable_v<SHostPumpContext>);
static_assert(std::is_standard_layout_v<SPumpInput> && std::is_trivially_copyable_v<SPumpInput>);
static_assert(std::is_standard_layout_v<SPumpResult> && std::is_trivially_copyable_v<SPumpResult>);

}   //  namespace modules
