
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    batch_pool.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Host-owned runners for module-defined batch work.

#pragma once

#ifndef HOST_BATCH_POOL_HPP_INCLUDED
#define HOST_BATCH_POOL_HPP_INCLUDED

#include <cstdint>

#include "platform/threading/thread_lifetime.hpp"
#include "threading/CBatchWork.hpp"

namespace host
{

inline constexpr std::uint32_t k_default_batch_runner_count = 8u;
inline constexpr std::uint32_t k_max_batch_runner_count = 32u;

[[nodiscard]] constexpr std::uint32_t batch_runner_limit(const std::uint32_t hardware_threads,
    const std::uint32_t host_workers, const std::uint32_t provisioned_module_threads = 2u) noexcept
{
    const std::uint32_t hardware = (hardware_threads < 64u) ? hardware_threads : 64u;
    //  Four places cover the fixed Morphic/OS allowance; the default two
    //  module threads bring that allowance to six.
    const std::uint32_t reserved = 4u + provisioned_module_threads + host_workers;
    const std::uint32_t available = (hardware > reserved) ? hardware - reserved : 0u;
    return (available < k_max_batch_runner_count) ? available : k_max_batch_runner_count;
}

class CBatchPool final
{
public:
    CBatchPool() noexcept = default;
    CBatchPool(const CBatchPool&) = delete;
    CBatchPool& operator=(const CBatchPool&) = delete;
    ~CBatchPool() noexcept;

    [[nodiscard]] bool start(const std::uint32_t count) noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] threading::SBatchShared& shared() noexcept { return m_shared; }
    [[nodiscard]] std::uint32_t runner_count() const noexcept { return m_count; }

private:
    struct SRunner
    {
        CBatchPool* pool{ nullptr };
        std::uint32_t index{ 0u };
        platform::threading::CThread thread;
        threading::CThreadControlState control;
        threading::CParkingTicket ticket;
        bool created{ false };
    };

    static std::uint32_t MV_STD_ABI_CALL runner_entry(void* user_data) noexcept;
    [[nodiscard]] bool run(SRunner& runner) noexcept;

    threading::SBatchShared m_shared;
    SRunner m_runners[k_max_batch_runner_count];
    std::uint32_t m_count{ 0u };
};

}   //  namespace host

#endif  //  HOST_BATCH_POOL_HPP_INCLUDED
