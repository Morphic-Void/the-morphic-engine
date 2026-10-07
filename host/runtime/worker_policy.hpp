//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    worker_policy.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Oct 26
//
//  Host background-worker sizing and Host-owned conditioning load tracking.

#pragma once

#ifndef HOST_WORKER_POLICY_HPP_INCLUDED
#define HOST_WORKER_POLICY_HPP_INCLUDED

#include <algorithm>
#include <cstdint>
#include <limits>

namespace host
{

inline constexpr std::uint32_t k_default_worker_count = 2u;
inline constexpr std::uint32_t k_max_conditioning_threads = 8u;
inline constexpr std::uint32_t k_max_worker_count = 1u + k_max_conditioning_threads;
inline constexpr std::uint32_t k_main_thread_budget = 5u; //  Host, Executive, debug, rendering and planned audio.
inline constexpr std::uint32_t k_os_thread_headroom = 3u;
inline constexpr std::uint32_t k_hardware_thread_cap = 64u;

[[nodiscard]] constexpr std::uint32_t worker_count_limit(const std::uint32_t hardware_threads) noexcept
{
    const std::uint32_t budget = std::min(hardware_threads, k_hardware_thread_cap);
    const std::uint32_t reserved = k_main_thread_budget + k_os_thread_headroom;
    const std::uint32_t available = (budget > reserved) ? budget - reserved : 0u;
    //  Eight reported hardware threads permit separate I/O and conditioning,
    //  accepting reduced OS headroom because these workers are used in bursts.
    const std::uint32_t minimum = (budget >= 8u) ? 2u : 1u;
    return std::min(k_max_worker_count, std::max(minimum, available));
}

[[nodiscard]] inline bool parse_worker_count(const char* const text, std::uint32_t& count) noexcept
{
    if ((text == nullptr) || (*text == '\0'))
    {
        return false;
    }
    std::uint32_t value = 0u;
    for (const char* digit = text; *digit != '\0'; ++digit)
    {
        if ((*digit < '0') || (*digit > '9'))
        {
            return false;
        }
        const std::uint32_t next = static_cast<std::uint32_t>(*digit - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max() - next) / 10u)
        {
            return false;
        }
        value = value * 10u + next;
    }
    if (value == 0u)
    {
        return false;
    }
    count = value;
    return true;
}

//  Only the Host accesses this state. Outstanding includes running and queued
//  jobs, ending when the Host receives completion. Selection alone reserves no
//  capacity: record a post only after the worker transport accepts it.
class CConditioningSchedule
{
public:
    [[nodiscard]] bool initialise(const std::uint32_t count) noexcept
    {
        if ((count == 0u) || (count > k_max_conditioning_threads))
        {
            return false;
        }
        *this = CConditioningSchedule{};
        m_count = count;
        return true;
    }

    [[nodiscard]] std::int32_t select() const noexcept
    {
        std::int32_t selected = -1;
        std::uint32_t least = std::numeric_limits<std::uint32_t>::max();
        for (std::uint32_t offset = 0u; offset < m_count; ++offset)
        {
            const std::uint32_t index = (m_next + offset) % m_count;
            if (m_outstanding[index] < least)
            {
                selected = static_cast<std::int32_t>(index);
                least = m_outstanding[index];
            }
        }
        return selected;
    }

    [[nodiscard]] bool record_post(const std::uint32_t index) noexcept
    {
        if ((index >= m_count) || (m_outstanding[index] == std::numeric_limits<std::uint32_t>::max()))
        {
            return false;
        }
        ++m_outstanding[index];
        m_next = (index + 1u) % m_count;
        return true;
    }

    [[nodiscard]] bool record_completion(const std::uint32_t index) noexcept
    {
        if ((index >= m_count) || (m_outstanding[index] == 0u))
        {
            return false;
        }
        --m_outstanding[index];
        return true;
    }

private:
    std::uint32_t m_outstanding[k_max_conditioning_threads]{};
    std::uint32_t m_count{ 0u };
    std::uint32_t m_next{ 0u };
};

}   //  namespace host

#endif  //  HOST_WORKER_POLICY_HPP_INCLUDED
