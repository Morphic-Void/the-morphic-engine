
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_preamble.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Common Host-owned dispatch, local routing and lifecycle for module execution.

#pragma once

#include "host/runtime/execution/module_channel.hpp"
#include "host/runtime/execution/direct_endpoint.hpp"
#include "host/runtime/execution/pressure_selection.hpp"
#include "host/runtime/execution/pressure_diagnostic.hpp"
#include "host/module/bound_module.hpp"
#include "foundation/platform/threading/native_thread_id.hpp"
#include "foundation/platform/threading/awake_clock.hpp"

namespace threading { class CThreadResources; }

namespace host
{

class CModulePreamble
{
public:
    CModulePreamble(threading::CEndpointResources& resources, CModuleChannel& channel,
        modules::CBoundModule& binding, const modules::SExecutionV1& execution,
        memory::CMemoryContext& host_memory, void* const creation_context = nullptr,
        const platform::threading::SAwakeClock clock = {}) noexcept;
    CModulePreamble(const CModulePreamble&) = delete;
    CModulePreamble& operator=(const CModulePreamble&) = delete;

    [[nodiscard]] bool activate() noexcept;
    [[nodiscard]] std::uint32_t run_serviced(threading::CThreadResources& resources) noexcept;
    void process(const std::uint64_t time_budget_ns = 2000000u) noexcept;
    void request_stop(const modules::EExecutionStop reason) noexcept;
    [[nodiscard]] bool work_pending() noexcept;
    void limit_wait(std::uint64_t& remaining) noexcept;
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] bool destroyed() const noexcept { return m_destroyed; }
    [[nodiscard]] bool retained() const noexcept { return m_retained; }
    //  Only a returned destroy refusal after the Host seal grant. No further
    //  module callbacks are permitted; this is not successful destruction.
    [[nodiscard]] bool destroy_refused() const noexcept { return m_destroy_refused; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool stopping() const noexcept { return m_stopping; }
    [[nodiscard]] bool destroying() const noexcept { return m_destroying; }
    [[nodiscard]] bool native_events_active() const noexcept { return m_progress.native_events_active != 0u; }
    [[nodiscard]] bool application_stop_requested() const noexcept { return m_application_stop; }
    [[nodiscard]] const modules::SExecutionContext& context() const noexcept { return m_context; }

private:
    static constexpr std::uint32_t k_connections = 16u;
    static constexpr std::uint32_t k_requests = 64u;
    static constexpr std::uint32_t k_host_lane = k_connections;
    static constexpr std::uint32_t k_self_lane = k_connections + 1u;
    static constexpr std::uint32_t k_lanes = k_connections + 2u;
    static constexpr std::uint32_t k_async_cadence = 32u;
    static constexpr std::uint32_t k_ordinary_budget = 64u;
    static constexpr std::uint32_t k_control_budget = 64u;

    enum class EConnectionPhase : std::uint8_t { empty, offered, accepted, installed, active, closing };
    struct SConnection
    {
        modules::SExecutionNotification notification;
        const SDirectEndpointDescriptor* descriptor{ nullptr };
        EConnectionPhase phase{ EConnectionPhase::empty };
        connections::EStatus decision{ connections::EStatus::rejected };
        CPressureDiagnostic pressure;
        bool offer_pending{ false }, availability_pending{ false }, closing_pending{ false }, retirement_pending{ false };
        bool decision_pending{ false }, installed_pending{ false }, activated_pending{ false }, close_pending{ false };
        bool closing_seen{ false }, module_quiescent{ false }, quiescence_pending{ false }, quiescence_sent{ false }, output_interest{ false };
    };
    struct SRequest
    {
        modules::SExecutionIntent::EKind kind{ modules::SExecutionIntent::EKind::connect };
        std::int32_t correlation{ -1 };
        modules::SExecutionNotification notification;
        services::SClientToken child;
        services::EServiceStatus ready_status{}, ended_status{}, retired_status{};
        bool live{ false }, result{ false }, ready{ false }, ended{ false }, retired{ false };
    };

    [[nodiscard]] bool enter_module_invocation() noexcept;
    void leave_module_invocation() noexcept;
    [[nodiscard]] bool invoke(const modules::SInvocation& invocation) noexcept;
    void observe(const modules::EExecutionStatus status, const modules::SExecutionProgress& progress) noexcept;
    void fail(const threading::EEndpointFailure reason) noexcept;
    [[nodiscard]] std::uint64_t remaining_budget() noexcept;
    [[nodiscard]] bool service_async() noexcept;
    [[nodiscard]] bool drain_controls() noexcept;
    void consume_control(const SLifecycleMessage& message) noexcept;
    void progress_controls() noexcept;
    [[nodiscard]] bool dispatch_notification() noexcept;
    void progress_coordinator() noexcept;
    void progress_completion() noexcept;
    void refresh_lanes() noexcept;
    void diagnose_pressure() noexcept;
    [[nodiscard]] bool dispatch_ordinary(const std::uint32_t lane) noexcept;
    [[nodiscard]] bool capacity_ready() noexcept;
    [[nodiscard]] bool connections_idle() const noexcept;
    void close_connection(SConnection& connection, const connections::EStatus reason) noexcept;
    [[nodiscard]] SConnection* find_connection(const connections::SToken token) noexcept;
    [[nodiscard]] SConnection* route(const modules::SDestination& destination) noexcept;
    [[nodiscard]] SRequest* find_request(const std::int32_t correlation) noexcept;
    [[nodiscard]] SRequest* vacant_request() noexcept;
    [[nodiscard]] modules::ESubmission submit_pod(const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] modules::ESubmission submit_owner(const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept;
    [[nodiscard]] modules::ESubmission submit_intent(const modules::SExecutionIntent& intent) noexcept;
    [[nodiscard]] modules::SConnectionQuery query_connection(const modules::SDestination& destination) noexcept;

    static CModulePreamble* authenticated(void* const cookie) noexcept;
    static modules::ESubmission MV_STD_ABI_CALL post_pod(void* const, const modules::SDestination&, const threading::CErasedPodMsg&) noexcept;
    static modules::ESubmission MV_STD_ABI_CALL post_owner(void* const, const modules::SDestination&, threading::CErasedOwnerMsg&&) noexcept;
    static modules::ESubmission MV_STD_ABI_CALL post_intent(void* const, const modules::SExecutionIntent&) noexcept;
    static modules::SConnectionQuery MV_STD_ABI_CALL query(void* const, const modules::SDestination&) noexcept;
    static memory::CMemoryView MV_STD_ABI_CALL producer_data(void* const, const connections::SToken) noexcept;
    static connections::EBulkPublish MV_STD_ABI_CALL publish_bulk(void* const, const connections::SToken) noexcept;
    static bool MV_STD_ABI_CALL acquire_bulk(void* const, const connections::SToken) noexcept;
    static memory::CMemoryConstView MV_STD_ABI_CALL consumer_data(void* const, const connections::SToken) noexcept;
    static const modules::SCommunicationV1 s_communication;
    static thread_local CModulePreamble* t_invocation;

    threading::CEndpointResources& m_resources;
    CModuleChannel& m_channel;
    modules::CBoundModule& m_binding;
    const modules::SExecutionV1 m_execution;
    const modules::CCommunication m_communication;
    modules::SExecutionContext m_context;
    modules::SExecutionProgress m_progress;
    TModuleFifo<threading::CErasedPodMsg> m_self;
    platform::threading::SAwakeClock m_clock;
    platform::threading::CPlatformThreadId m_native_owner;
    void* m_runtime{ nullptr };
    SConnection m_connections[k_connections];
    SRequest m_requests[k_requests];
    SPressureLane m_lanes[k_lanes];
    CPressureDiagnostic m_pressure, m_owner_pressure, m_control_pressure, m_self_pressure, m_starvation[k_lanes];
    std::uint64_t m_starvation_sources[k_lanes]{};
    CPressureSelection m_selection{ m_lanes, k_lanes };
    std::uint32_t m_notification_cursor{ 0u }, m_control_count{ 0u }, m_since_async{ 0u };
    std::uint64_t m_turn_start{ 0u }, m_turn_budget{ 0u }, m_next_wake{ UINT64_MAX };
    std::uint64_t m_host_dispatched{ 0u }, m_admission_frontier{ 0u };
    SPublicationFrontier m_close_frontier, m_seal_frontier;
    modules::EExecutionStop m_stop_reason{ modules::EExecutionStop::requested };
    modules::SExecutionNotification m_participation;
    services::SClientToken m_standard_child;
    bool m_created{ false }, m_started{ false }, m_ready{ false }, m_ready_pending{ false };
    bool m_stopping{ false }, m_stop_notified{ false }, m_processing{ false }, m_destroyed{ false };
    bool m_failed{ false }, m_retained{ false }, m_application_stop{ false }, m_destroying{ false };
    bool m_destroy_refused{ false };
    bool m_participating{ false }, m_participation_requested{ false }, m_participation_pending{ false }, m_event_pending{ false };
    bool m_host_output_interest{ false }, m_owner_output_interest{ false }, m_control_output_interest{ false };
    bool m_close_requested{ false }, m_close_sent{ false }, m_admission_seen{ false }, m_admission_notified{ false };
    bool m_seal_sent{ false }, m_seal_seen{ false }, m_module_stop_pending{ false };
    bool m_standard_requested{ false }, m_standard_ready{ false }, m_standard_ended{ false }, m_standard_retired{ false };
    bool m_standard_stop_sent{ false }, m_async_since_work{ false };
};

}   //  namespace host
