
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_maintenance.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Fixed capture slots retain owners and exact sources through outer-call unwind.

#pragma once

#include "runtime/execution/client_record.hpp"
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
    [[nodiscard]] modules::SHostMaintenanceResult run() noexcept;
    void poll_stop() noexcept;
    void replay() noexcept;
    void fanout() noexcept;

private:
    struct SSource
    {
        SClientRecord* client{ nullptr };
        threading::CThreadPackage* package{ nullptr };
        std::uint64_t sequence{ 0u };
    };
    struct SPod { SSource source; threading::CErasedPodMsg message; };
    struct SOwner { SSource source; threading::CErasedOwnerMsg message; };
    [[nodiscard]] bool capture_pod(const bool worker) noexcept;
    [[nodiscard]] bool capture_owner(const bool worker) noexcept;
    [[nodiscard]] bool next_source(const bool worker, const std::uint32_t lane, SSource& source, std::uint32_t& index) noexcept;
    [[nodiscard]] bool signal_one() noexcept;
    void accepted(SSource& source) noexcept;
    void released(SSource& source) noexcept;

    CHost& m_host;
    CHostRestrictions m_restrictions;
    SPod m_pods[128]; //  Host worker captures; modules retain one head in each lane.
    SOwner m_owners[32]; //  Constructed worker owner slots, retained until replay.
    std::uint64_t m_next_sequence{ 1u };
    std::uint32_t m_count{ 0u }, m_lane{ 0u }, m_cursors[5]{};
    bool m_running{ false }, m_stop_requested{ false }, m_pressure{ false };
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SHostRuntimeTestAccess;
    friend struct SHostMaintenanceTestAccess;
#endif
};

}   //  namespace host
