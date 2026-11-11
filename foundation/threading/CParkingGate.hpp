
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   CParkingGate.hpp
//  Author: Ritchie Brannan
//  Date:   18 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Alternating parking gate built from two exclusive locks.
//
//  CParkingGate provides a small fallback parking primitive for higher-level
//  threading objects that need an owned place to block waiters. The controller
//  closes one phase while the other remains open. Parkers pass through the
//  phase recorded in their caller-owned CParkingTicket, then flip the ticket
//  for the next park.
//
//  The ticket records the last gate state observed by that waiter: control
//  lifetime generation, controlled/uncontrolled state, and phase. The phase is
//  the ordinary wake/cycle mechanism. The generation is a control lifetime
//  epoch used to prevent a stale controlled ticket from crossing a release and
//  later reacquire boundary without first returning to the caller.
//
//  Construction leaves both gates open. acquire_control() starts a new control
//  lifetime generation and closes one phase. release_control() publishes the
//  uncontrolled state and opens the closed phase.
//
//  Control operations are single-controller per instance. Parking tickets are
//  caller-owned, backend state; each concurrently parking thread must use a
//  distinct ticket.

#pragma once

#ifndef CPARKING_GATE_HPP_INCLUDED
#define CPARKING_GATE_HPP_INCLUDED

#include <atomic>   //  std::atomic
#include <cstdint>  //  std::uint32_t

#include "platform/threading/exclusive_lock.hpp"

namespace threading
{

//==============================================================================
//  CParkingTicket
//==============================================================================

class CParkingTicket
{
public:

    //  Default and deleted lifetime
    CParkingTicket() noexcept = default;
    CParkingTicket(const CParkingTicket&) = delete;
    CParkingTicket& operator=(const CParkingTicket&) = delete;
    CParkingTicket(CParkingTicket&&) = delete;
    CParkingTicket& operator=(CParkingTicket&&) = delete;
    ~CParkingTicket() noexcept = default;

private:

    friend class CParkingGate;

    //  Gate-only state accessors keep packed ticket-state usage explicit.
    std::uint32_t get_state() const noexcept { return m_state; }
    void set_state(const std::uint32_t state) noexcept { m_state = state; }

    std::uint32_t m_state = 0u;
};

//==============================================================================
//  CParkingGate
//==============================================================================

class CParkingGate
{
public:

    //  Constructor and destructor
    CParkingGate() noexcept;
    ~CParkingGate() noexcept;

    //  Deleted lifetime
    CParkingGate(const CParkingGate&) = delete;
    CParkingGate& operator=(const CParkingGate&) = delete;
    CParkingGate(CParkingGate&&) = delete;
    CParkingGate& operator=(CParkingGate&&) = delete;

    //  Status
    bool is_valid() const noexcept;
    bool has_control() const noexcept;

    //  Control
    bool acquire_control() noexcept;
    void release_control() noexcept;

    //  Parking
    void park(CParkingTicket& ticket) noexcept;

    //  Phase control
    void cycle_phase() noexcept;

private:

    static constexpr std::uint32_t k_phase_mask{ 1u };
    static constexpr std::uint32_t k_control_mask{ 2u };
    static constexpr std::uint32_t k_generation_step{ 4u };
    static constexpr std::uint32_t k_generation_mask{ ~(k_control_mask | k_phase_mask) };
    static constexpr std::uint32_t k_validation_mask{ k_generation_mask | k_control_mask };

    platform::threading::CExclusiveLock m_gates[2];

    std::atomic<std::uint32_t> m_state{ 0u };

    bool m_valid = false;
};

}   //  namespace threading

#endif  //  CPARKING_GATE_HPP_INCLUDED
