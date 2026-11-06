
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_pump.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Optional same-process Host-thread execution capability. No native thread resources.

#pragma once

#include <cstdint>
#include <type_traits>

#include "module/module_binding.hpp"
#include "module/host_execution_context.hpp"
#include "services/service_protocol.hpp"
#include "services/service_borrow.hpp"
#include "services/service_notifications.hpp"

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
    const services::SDispatchedCommand* commands{ nullptr };
    const threading::CErasedPodMsg* messages{ nullptr };
    std::uint32_t command_count{ 0u };
    std::uint32_t message_count{ 0u };
    std::uint32_t notifications_pending{ 0u }; //  Hint only; drain and recheck registration state.
    std::uint64_t time_budget_ns{ 2000000u };
};

struct SPumpResult
{
    std::uint32_t consumed_commands{ 0u };
    std::uint32_t consumed_messages{ 0u };
    SProgress progress;
};

//  Callbacks are Host-thread-only and authenticate the stable supplied context.
//  Ownership moves only to Host, and only on success. busy retains caller data.
//  The complete current table requires matching size/version and coordinated
//  Host/module rebuilds. Worker preparation is not exposed.
struct SHostPumpServices
{
    std::uint32_t size{ sizeof(SHostPumpServices) };
    std::uint32_t version{ 1u };
    services::EServiceStatus(MV_STD_ABI_CALL* post_pod)(void* const, const threading::CErasedPodMsg* const) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* post_owner)(void* const, threading::CErasedOwnerMsg* const) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* complete)(void* const, const services::ServiceReply* const) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* prepare_borrow)(void* const, const services::SServiceBorrowOffer* const, services::SServiceBorrow* const) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* begin_borrow_retirement)(void* const, const services::SServiceBorrow) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* finish_borrow_retirement)(void* const, const services::SServiceBorrow) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* bind_borrow_notification)(void* const, const services::SServiceBorrow,
        const services::SNotificationRegistration, threading::SNotificationEndpoint* const) noexcept{ nullptr };
    SRestrictionEnterResult(MV_STD_ABI_CALL* enter_restriction)(void* const, const SRestrictionReason) noexcept{ nullptr };
    ERestrictionStatus(MV_STD_ABI_CALL* leave_restriction)(void* const, const SRestrictionScope) noexcept{ nullptr };
    SHostMaintenanceResult(MV_STD_ABI_CALL* maintain_host)(void* const) noexcept{ nullptr };
};

struct SHostPumpContext
{
    std::uint32_t size{ sizeof(SHostPumpContext) };
    std::uint32_t version{ 1u };
    SModuleInstance instance;
    services::SClientToken client;
    thread_ids::id_type host_thread{};
    const SHostPumpServices* services{ nullptr };
    void* service_context{ nullptr };
    const SHostExecutionContext* execution{ nullptr };
};

inline constexpr char k_host_pump_symbol_name[]{ "morphic_module_host_pump_v2" };
inline constexpr std::uint32_t k_host_pump_native_events = 1u;
struct SHostPumpV2
{
    [[nodiscard]] bool is_valid() const noexcept;

    std::uint32_t size{ sizeof(SHostPumpV2) };
    std::uint32_t version{ 2u };
    std::uint32_t flags{ 0u };
    ECallStatus(MV_STD_ABI_CALL* activate)(const SHostPumpContext* const, void** const, SProgress* const) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* pump)(void* const, const SPumpInput* const, SPumpResult* const) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* begin_stop)(void* const, const EStopReason, SProgress* const) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* deactivate)(void* const) noexcept{ nullptr };
};
using FQueryHostPump = EBindingResult(MV_STD_ABI_CALL*)(const std::uint32_t, SHostPumpV2* const) noexcept;

inline bool SHostPumpV2::is_valid() const noexcept
{
    if ((size != sizeof(SHostPumpV2)) || (version != 2u) || ((flags & ~k_host_pump_native_events) != 0u) ||
        (activate == nullptr) || (pump == nullptr) ||
        (begin_stop == nullptr) || (deactivate == nullptr))
    {
        return false;
    }
    return true;
}

static_assert(std::is_standard_layout_v<SHostPumpV2> && std::is_trivially_copyable_v<SHostPumpV2>);
static_assert(std::is_standard_layout_v<SHostPumpServices> && std::is_trivially_copyable_v<SHostPumpServices>);
static_assert(std::is_standard_layout_v<SHostPumpContext> && std::is_trivially_copyable_v<SHostPumpContext>);
static_assert(std::is_standard_layout_v<SPumpInput> && std::is_trivially_copyable_v<SPumpInput>);
static_assert(std::is_standard_layout_v<SPumpResult> && std::is_trivially_copyable_v<SPumpResult>);

}   //  namespace modules
