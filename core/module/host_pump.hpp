
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
#include "services/service_protocol.hpp"
#include "services/service_borrow.hpp"

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
    std::uint32_t work_pending{ 0u }; //  Zero or one in v1.
};

struct SPumpInput
{
    std::uint32_t size{ sizeof(SPumpInput) };
    std::uint32_t version{ 1u };
    const services::SDispatchedCommand* commands{ nullptr };
    const threading::CErasedPodMsg* messages{ nullptr };
    std::uint32_t command_count{ 0u };
    std::uint32_t message_count{ 0u };
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
//  Host/module rebuilds. Worker preparation and modal maintenance are not exposed.
struct SHostPumpServices
{
    std::uint32_t size{ sizeof(SHostPumpServices) };
    std::uint32_t version{ 1u };
    services::EServiceStatus(MV_STD_ABI_CALL* post_pod)(void*, const threading::CErasedPodMsg*) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* post_owner)(void*, threading::CErasedOwnerMsg*) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* complete)(void*, const services::ServiceReply*) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* prepare_borrow)(void*, const services::SServiceBorrowOffer*, services::SServiceBorrow*) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* begin_borrow_retirement)(void*, services::SServiceBorrow) noexcept{ nullptr };
    services::EServiceStatus(MV_STD_ABI_CALL* finish_borrow_retirement)(void*, services::SServiceBorrow) noexcept{ nullptr };
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
};

inline constexpr char k_host_pump_symbol_name[]{ "morphic_module_host_pump_v1" };
struct SHostPumpV1
{
    [[nodiscard]] bool is_valid() const noexcept;

    std::uint32_t size{ sizeof(SHostPumpV1) };
    std::uint32_t version{ 1u };
    std::uint32_t flags{ 0u };
    ECallStatus(MV_STD_ABI_CALL* activate)(const SHostPumpContext*, void**, SProgress*) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* pump)(void*, const SPumpInput*, SPumpResult*) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* begin_stop)(void*, EStopReason, SProgress*) noexcept{ nullptr };
    ECallStatus(MV_STD_ABI_CALL* deactivate)(void*) noexcept{ nullptr };
};
using FQueryHostPump = EBindingResult(MV_STD_ABI_CALL*)(std::uint32_t, SHostPumpV1*) noexcept;

inline bool SHostPumpV1::is_valid() const noexcept
{
    if ((size != sizeof(SHostPumpV1)) || (version != 1u) || (flags != 0u) ||
        (activate == nullptr) || (pump == nullptr) ||
        (begin_stop == nullptr) || (deactivate == nullptr))
    {
        return false;
    }
    return true;
}

static_assert(std::is_standard_layout_v<SHostPumpV1> && std::is_trivially_copyable_v<SHostPumpV1>);
static_assert(std::is_standard_layout_v<SHostPumpServices> && std::is_trivially_copyable_v<SHostPumpServices>);
static_assert(std::is_standard_layout_v<SHostPumpContext> && std::is_trivially_copyable_v<SHostPumpContext>);
static_assert(std::is_standard_layout_v<SPumpInput> && std::is_trivially_copyable_v<SPumpInput>);
static_assert(std::is_standard_layout_v<SPumpResult> && std::is_trivially_copyable_v<SPumpResult>);

}   //  namespace modules
