
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    pumped_client.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Host-thread lifecycle and authenticated, queued service callbacks.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/services/service_protocol.hpp"
#include "host/module/bound_module.hpp"
#include "runtime/service_router.hpp"
#include "runtime/host_maintenance.hpp"
#include "framework/module/host_eligible_budget.hpp"
#include "threading/transports/TRingTransport.hpp"

namespace host
{

class CPumpedClient
{
public:
    CPumpedClient(SClientRecord& client, modules::CBoundModule& binding,
        const modules::SHostPumpV2& capability, CServiceRouter& router, CHostMaintenance& maintenance) noexcept;
    [[nodiscard]] bool initialise() noexcept;
    void activate() noexcept;
    void pump() noexcept;
    void request_stop(const modules::EStopReason reason) noexcept;
    [[nodiscard]] bool deactivate() noexcept;
    [[nodiscard]] const threading::CErasedPodMsg* peek_pod(const bool control) noexcept;
    void consume_pod(const bool control) noexcept;
    [[nodiscard]] bool read(threading::CErasedOwnerMsg& message) noexcept { return m_owners.read(message); }
    [[nodiscard]] bool capture(threading::CErasedOwnerMsg& message) noexcept { return m_owners.capture(message); }
    [[nodiscard]] bool ready() const noexcept { return m_client.phase == EClientPhase::ready; }
    [[nodiscard]] bool stopped() const noexcept { return m_client.phase == EClientPhase::stopped; }
    [[nodiscard]] bool deactivated() const noexcept { return m_deactivated; }
    [[nodiscard]] bool retained() const noexcept { return m_retained; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool work_pending() const noexcept { return m_work_pending && !stopped() && !m_retained && !m_deactivated; }
    [[nodiscard]] bool native_events_active() const noexcept { return m_native_events && !stopped() && !m_retained && !m_deactivated; }
    [[nodiscard]] bool native_stop_requested() const noexcept { return m_native_stop; }
    [[nodiscard]] bool owns_native_queue() const noexcept { return (m_capability.flags & modules::k_host_pump_native_events) && !m_deactivated; }
    void limit_wait(std::uint64_t& remaining) noexcept;

private:
    [[nodiscard]] bool enter(const bool allow_restriction = true) noexcept;
    void leave() noexcept;
    void finish_frame() noexcept;
    void observe(const modules::ECallStatus status, const modules::SProgress& progress) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL post_pod(void* const, const threading::CErasedPodMsg* const) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL post_owner(void* const, threading::CErasedOwnerMsg* const) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL complete(void* const, const services::ServiceReply* const) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL commit_resource(void* const, const services::ServiceReply* const) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL prepare_borrow(void* const, const services::SServiceBorrowOffer* const, services::SServiceBorrow* const) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL begin_borrow_retirement(void* const, const services::SServiceBorrow) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL finish_borrow_retirement(void* const, const services::SServiceBorrow) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL bind_borrow_notification(void* const, const services::SServiceBorrow,
        const services::SNotificationRegistration, threading::SNotificationEndpoint* const) noexcept;
    static modules::SRestrictionEnterResult MV_STD_ABI_CALL enter_restriction(void* const, const modules::SRestrictionReason) noexcept;
    static modules::ERestrictionStatus MV_STD_ABI_CALL leave_restriction(void* const, const modules::SRestrictionScope) noexcept;
    static modules::SHostMaintenanceResult MV_STD_ABI_CALL maintain_host(void* const) noexcept;

    SClientRecord& m_client;
    modules::CBoundModule& m_binding;
    const modules::SHostPumpV2 m_capability;
    CServiceRouter& m_router;
    CHostMaintenance& m_maintenance;
    modules::SHostPumpContext m_context;
    void* m_runtime{ nullptr };
    threading::transports::TRing<threading::CErasedPodMsg> m_pods;
    threading::transports::TRing<threading::CErasedPodMsg> m_controls;
    threading::CErasedPodMsg m_pending_pod, m_pending_control;
    bool m_has_pending_pod{ false }, m_has_pending_control{ false };
    threading::transports::CErasedOwnerMsgTransport m_owners{ module_ids::executable, memory::get_ambient_memory_context() };
    modules::EStopReason m_stop_reason{ modules::EStopReason::requested };
    bool m_activated{ false };
    bool m_stop_begun{ false };
    bool m_deactivated{ false };
    bool m_retained{ false };
    bool m_failed{ false };
    bool m_protocol_failed{ false };
    bool m_call_failed{ false };
    bool m_work_pending{ false }, m_native_events{ false }, m_native_stop{ false };
    modules::CHostEligibleBudget m_activation_budget;
};

}   //  namespace host
