
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connections.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Host-owned symmetric connections coordinated over private lifecycle lanes.

#pragma once

#include "containers/TUnorderedCollection.hpp"
#include "framework/module/host_eligible_budget.hpp"
#include "runtime/execution/client_record.hpp"

namespace host
{

struct SDirectRequest
{
    system_ids::id_type first{}, second{};
    module_ids::id_type bulk_peer{};
    std::uint32_t version{ 1u };
    connections::SBulkConfiguration bulk;
};

class CDirectConnections
{
public:
    static constexpr std::uint32_t k_capacity = 32u, k_per_client = 8u, k_requesters = 8u;
    void register_client(SClientRecord& client, const std::int32_t slot) noexcept;
    void request(SClientRecord& client, SClientRecord* const first, SClientRecord* const second, const SDirectRequest& request,
        const std::int32_t slot, const modules::SHostRestrictionReader restriction) noexcept;
    void decision(const SClientRecord& client, const connections::SToken token, const connections::EStatus status) noexcept;
    void ready(const SClientRecord& client, const connections::SToken token) noexcept;
    void activated(const SClientRecord& client, const connections::SToken token) noexcept;
    void close(const SClientRecord& client, const connections::SToken token, const connections::EStatus reason) noexcept;
    void quiescent(const SClientRecord& client, const connections::SToken token) noexcept;
    void close_client(SClientRecord& client) noexcept;
    void close_all() noexcept;
    void close_admission() noexcept { m_closed = true; }
    void progress() noexcept;
    [[nodiscard]] bool client_idle(const SClientRecord& client) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return (m_count == 0u) && (m_request_count == 0u); }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    void limit_wait(std::uint64_t& remaining) noexcept;

private:
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SDirectConnectionTestAccess;
#endif
    enum class EPhase : std::uint8_t { offered, installing, activating, active, closing };
    struct SSide
    {
        SClientRecord* client{ nullptr };
        bool offered{ false }, accepted{ false }, exposed{ false }, ready{ false };
        bool activated{ false }, closing_sent{ false }, quiescent{ false }, retired_sent{ false };
        bool activation_sent{ false };
        bool pinned{ false };
        threading::CNotification notification;
        SDirectEndpointDescriptor descriptor;
    };
    struct SRecord
    {
        SRecord(SClientRecord& first, SClientRecord& second, const std::uint32_t version,
            const connections::SBulkConfiguration bulk) noexcept;
        connections::SToken token;
        CDirectAdmission admission;
        std::uint32_t version;
        EPhase phase{ EPhase::offered };
        connections::EStatus reason{ connections::EStatus::cancelled };
        SSide sides[2];
        CModulePodPipe forward, reverse;
        connections::SBulkConfiguration bulk_configuration;
        threading::transports::CBulkBufferTransport bulk;
        threading::transports::CBulkBufferProducerEndpoint bulk_producer{ bulk };
        threading::transports::CBulkBufferConsumerEndpoint bulk_consumer{ bulk };
        modules::CHostEligibleBudget budget;
    };
    //  These records hold only requester authority and copied identities. They
    //  survive transport retirement without retaining participant pointers.
    struct SRequest
    {
        SClientRecord* requester{ nullptr };
        struct SResult
        {
            connections::SToken token;
            services::SClientToken first, second;
            connections::EStatus status{ connections::EStatus::invalid_request };
        } result;
        std::int32_t slot{ -1 };
        bool ready{ false };
    };
    [[nodiscard]] SRecord* find(const connections::SToken token) noexcept;
    [[nodiscard]] std::uint32_t count(const SClientRecord& client) const noexcept;
    void results(SRecord& record) noexcept;
    void progress_results() noexcept;
    void refuse(SClientRecord& client, const SClientRecord* const first, const SClientRecord* const second,
        const std::int32_t slot, const connections::EStatus status, const connections::SToken token = {}) noexcept;
    static void begin_close(SRecord& record, const connections::EStatus reason) noexcept;
    [[nodiscard]] bool install(SRecord& record) noexcept;
    TUnorderedCollection<SRecord> m_records;
    SRequest m_requests[k_capacity * k_requesters];
    std::uint32_t m_count{ 0u };
    std::uint32_t m_request_count{ 0u };
    bool m_closed{ false }, m_failed{ false };
};

}   //  namespace host
