
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    pumped_client.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Host-thread lifecycle and authenticated, queued service callbacks.

#pragma once

#include "services/service_protocol.hpp"
#include "module/bound_module.hpp"
#include "runtime/service_router.hpp"
#include "threading/transports/TRingTransport.hpp"

namespace host
{

class CPumpedClient
{
public:
    CPumpedClient(SClientRecord& client, modules::CBoundModule& binding,
        const modules::SHostPumpV1& capability, CServiceRouter& router) noexcept;
    [[nodiscard]] bool initialise() noexcept;
    void activate() noexcept;
    void pump() noexcept;
    void request_stop(const modules::EStopReason reason) noexcept;
    [[nodiscard]] bool deactivate() noexcept;
    [[nodiscard]] const threading::CErasedPodMsg* peek_pod(const bool control) noexcept;
    void consume_pod(const bool control) noexcept;
    [[nodiscard]] bool read(threading::CErasedOwnerMsg& message) noexcept { return m_owners.read(message); }
    [[nodiscard]] bool ready() const noexcept { return m_client.phase == EClientPhase::ready; }
    [[nodiscard]] bool stopped() const noexcept { return m_client.phase == EClientPhase::stopped; }
    [[nodiscard]] bool deactivated() const noexcept { return m_deactivated; }
    [[nodiscard]] bool retained() const noexcept { return m_retained; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

private:
    [[nodiscard]] bool enter() noexcept;
    void leave() noexcept;
    void observe(const modules::ECallStatus status, const modules::SProgress& progress) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL post_pod(void*, const threading::CErasedPodMsg*) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL post_owner(void*, threading::CErasedOwnerMsg*) noexcept;
    static services::EServiceStatus MV_STD_ABI_CALL complete(void*, const services::ServiceReply*) noexcept;

    SClientRecord& m_client;
    modules::CBoundModule& m_binding;
    const modules::SHostPumpV1 m_capability;
    CServiceRouter& m_router;
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
    threading::CWaitDeadline m_activation_deadline;
};

}   //  namespace host
