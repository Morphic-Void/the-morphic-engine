//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    worker_policy.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Oct 26
//
//  Host I/O and optional dedicated conditioning-worker sizing.

#pragma once

#ifndef HOST_WORKER_POLICY_HPP_INCLUDED
#define HOST_WORKER_POLICY_HPP_INCLUDED

#include <cstdint>
#include <limits>

namespace host
{

inline constexpr std::uint32_t k_default_worker_count = 2u;
inline constexpr std::uint32_t k_max_worker_count = 2u;

[[nodiscard]] constexpr std::uint32_t worker_count_limit(const std::uint32_t hardware_threads) noexcept
{
    //  Eight reported hardware threads permit separate I/O and conditioning,
    //  accepting reduced OS headroom because these workers are used in bursts.
    return (hardware_threads >= 8u) ? k_max_worker_count : 1u;
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

}   //  namespace host

#endif  //  HOST_WORKER_POLICY_HPP_INCLUDED
