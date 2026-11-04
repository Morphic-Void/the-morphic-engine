
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_maintenance.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Fixed capture slots retain owners and exact sources through outer-call unwind.

#pragma once

#include "runtime/client_record.hpp"
#include "runtime/host_restrictions.hpp"

namespace host
{

class CHost;
class CHostMaintenance
{
public:
    explicit CHostMaintenance(CHost& owner, const platform::threading::SAwakeClock clock = {}) noexcept
        : m_host{ owner }, m_restrictions{ clock } {}
    [[nodiscard]] CHostRestrictions& restrictions() noexcept { return m_restrictions; }
    [[nodiscard]] bool running() const noexcept { return m_running; }
    [[nodiscard]] bool stop_requested() const noexcept { return m_stop_requested; }
    [[nodiscard]] bool empty() const noexcept { return m_count == 0u; }
    [[nodiscard]] bool references(const threading::CThreadPackage& package) const noexcept;
    [[nodiscard]] bool captured(const SClientRecord* const client, const threading::CThreadPackage* const package) const noexcept;
    [[nodiscard]] modules::SHostMaintenanceResult run() noexcept;
    void poll_stop() noexcept;
    void replay() noexcept;
    void fanout() noexcept;
    [[nodiscard]] bool stage_delivery(SClientRecord& client, const threading::CErasedPodMsg& message,
        const bool control, const bool reserved) noexcept;

private:
    struct SSource
    {
        SClientRecord* client{ nullptr };
        threading::CThreadPackage* package{ nullptr };
        std::uint64_t sequence{ 0u };
        std::uint32_t credits{ 0u };
        bool control{ false };
    };
    struct SPod { SSource source; threading::CErasedPodMsg message; };
    struct SOwner { SSource source; threading::CErasedOwnerMsg message; };
    struct SDelivery { SSource source; threading::CErasedPodMsg message; };
    [[nodiscard]] bool capture_pod(const bool worker, const bool control) noexcept;
    [[nodiscard]] bool capture_owner(const bool worker) noexcept;
    [[nodiscard]] bool next_source(const bool worker, const std::uint32_t lane, SSource& source, std::uint32_t& index) noexcept;
    [[nodiscard]] bool publish_reply() noexcept;
    [[nodiscard]] bool signal_one() noexcept;
    void accepted(SSource& source) noexcept;
    void released(SSource& source) noexcept;

    CHost& m_host;
    CHostRestrictions m_restrictions;
    SPod m_pods[288]; //  128 client, 128 worker, 32 independent controls.
    SOwner m_owners[64]; //  32 client and 32 worker slots, always constructed.
    SDelivery m_deliveries[64];
    std::uint64_t m_next_sequence{ 1u };
    std::uint32_t m_count{ 0u }, m_lane{ 0u }, m_cursors[7]{};
    bool m_running{ false }, m_stop_requested{ false }, m_pressure{ false };
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SHostRuntimeTestAccess;
    friend struct SHostMaintenanceTestAccess;
#endif
};

}   //  namespace host
