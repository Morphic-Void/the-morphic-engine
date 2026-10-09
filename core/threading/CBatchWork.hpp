
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CBatchWork.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Host-owned batch submission and requester-owned completion routing.

#pragma once

#ifndef CBATCH_WORK_HPP_INCLUDED
#define CBATCH_WORK_HPP_INCLUDED

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "memory/memory_context.hpp"
#include "platform/platform_defines.hpp"
#include "system/system_ids.hpp"
#include "threading/CThreadControlState.hpp"
#include "threading/CWaitPredicate.hpp"
#include "threading/transports/TMpmcTransport.hpp"

namespace threading
{

using FBatchWork = void(MV_STD_ABI_CALL*)(void* const object) noexcept;

enum class EBatchSubmission : std::uint8_t
{
    queued = 0u,
    completed_inline,
    rejected
};

enum class EBatchRejection : std::uint8_t
{
    none = 0u,
    invalid_work,
    requester_exiting,
    duplicate_object,
    duplicate_correlation
};

struct SBatchSubmission
{
    EBatchSubmission status{ EBatchSubmission::rejected };
    EBatchRejection reason{ EBatchRejection::invalid_work };
};

enum class EBatchCompletion : std::uint8_t
{
    executed = 0u,
    discarded_requester_exiting,
    discarded_context_install_failed
};

struct SBatchResponse
{
    std::uint32_t correlation{ 0u };
    EBatchCompletion completion{ EBatchCompletion::executed };
};

class CBatchClient;

struct SBatchWork
{
    FBatchWork function{ nullptr };
    void* object{ nullptr };
    memory::CMemoryContext* memory_context{ nullptr };
    CBatchClient* requester{ nullptr };
    std::uint32_t correlation{ 0u };
};

static_assert(std::is_standard_layout_v<SBatchWork> && std::is_trivially_copyable_v<SBatchWork>);
static_assert(std::is_standard_layout_v<SBatchResponse> && std::is_trivially_copyable_v<SBatchResponse>);

inline constexpr std::uint32_t k_batch_channel_capacity = 128u;
using CBatchWorkChannel = transports::TMpmcArenaTransport<SBatchWork, k_batch_channel_capacity>;
using CBatchReturnChannel = transports::TMpmcArenaTransport<SBatchResponse, k_batch_channel_capacity>;

//  The Host owns this shared channel and its runner wake word. Requesters only
//  borrow it; all runners pull from the same channel.
struct SBatchShared
{
    CBatchWorkChannel work;
    CWaitPredicate wake;
    std::uint32_t runner_count{ 0u };
};

//  One stable route per requesting thread. Only that thread mutates m_pending
//  and consumes responses. Runners only read the immutable route information
//  and produce into m_returns.
class CBatchClient
{
public:
    CBatchClient(SBatchShared& shared, CThreadControlState& control, CWaitPredicate& wake,
        const module_ids::id_type module_id, void* const binding) noexcept :
        m_shared{ &shared }, m_control{ &control }, m_wake{ &wake },
        m_module_id{ module_id }, m_binding{ binding } {}

    CBatchClient(const CBatchClient&) = delete;
    CBatchClient& operator=(const CBatchClient&) = delete;

    [[nodiscard]] SBatchSubmission submit(FBatchWork function, void* object, const std::uint32_t correlation) noexcept
    {
        memory::CMemoryContext* const context = memory::get_ambient_memory_context();
        if ((function == nullptr) || (object == nullptr) || (context == nullptr) ||
            !context->is_usable() || !context->belongs_to_module(m_module_id) ||
            (m_binding == nullptr))
        {
            return { EBatchSubmission::rejected, EBatchRejection::invalid_work };
        }
        if (m_control->exit_requested())
        {
            return { EBatchSubmission::rejected, EBatchRejection::requester_exiting };
        }
        for (const SPending& entry : m_pending)
        {
            if (entry.object == object)
            {
                return { EBatchSubmission::rejected, EBatchRejection::duplicate_object };
            }
            if ((entry.object != nullptr) && (entry.correlation == correlation))
            {
                return { EBatchSubmission::rejected, EBatchRejection::duplicate_correlation };
            }
        }

        //  A queued response retains its credit until the requester consumes it.
        //  Thus every accepted asynchronous work item has guaranteed return space.
        if ((m_shared->runner_count != 0u) && (m_outstanding.load(std::memory_order_acquire) < k_batch_channel_capacity))
        {
            SPending* free_entry = nullptr;
            for (SPending& entry : m_pending)
            {
                if (entry.object == nullptr)
                {
                    free_entry = &entry;
                    break;
                }
            }
            if (free_entry != nullptr)
            {
                transports::TReservedArenaSlot<SBatchWork, k_batch_channel_capacity> reserved(m_shared->work);
                if (reserved)
                {
                    *reserved = { function, object, context, this, correlation };
                    free_entry->object = object;
                    free_entry->correlation = correlation;
                    m_outstanding.fetch_add(1u, std::memory_order_release);
                    if (reserved.publish())
                    {
                        (void)m_shared->wake.poke_epoch_and_wake_one();
                        return { EBatchSubmission::queued, EBatchRejection::none };
                    }
                    m_outstanding.fetch_sub(1u, std::memory_order_release);
                    *free_entry = {};
                    return { EBatchSubmission::rejected, EBatchRejection::invalid_work };
                }
            }
        }

        function(object);
        return { EBatchSubmission::completed_inline, EBatchRejection::none };
    }

    [[nodiscard]] bool receive(SBatchResponse& response) noexcept
    {
        transports::TAcquiredArenaSlot<SBatchResponse, k_batch_channel_capacity> acquired(m_returns);
        if (!acquired)
        {
            return false;
        }
        response = *acquired;
        for (SPending& entry : m_pending)
        {
            if ((entry.object != nullptr) && (entry.correlation == response.correlation))
            {
                //  Restore credit only after the return slot is reusable.
                if (!acquired.recycle())
                {
                    return false;
                }
                entry = {};
                m_outstanding.fetch_sub(1u, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool complete(const SBatchResponse& response) noexcept
    {
        m_return_publishers.fetch_add(1u, std::memory_order_acq_rel);
        bool delivered = false;
        {
            transports::TReservedArenaSlot<SBatchResponse, k_batch_channel_capacity> reserved(m_returns);
            if (reserved)
            {
                *reserved = response;
                if (reserved.publish())
                {
                    delivered = m_wake->poke_epoch_and_wake_one();
                }
            }
        }
        m_return_publishers.fetch_sub(1u, std::memory_order_release);
        return delivered;
    }

    [[nodiscard]] bool exit_requested() const noexcept { return m_control->exit_requested(); }
    [[nodiscard]] bool has_runners() const noexcept { return m_shared->runner_count != 0u; }
    [[nodiscard]] std::uint32_t outstanding() const noexcept
    {
        return m_outstanding.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint32_t return_publishers() const noexcept
    {
        return m_return_publishers.load(std::memory_order_acquire);
    }
    [[nodiscard]] module_ids::id_type module_id() const noexcept { return m_module_id; }
    [[nodiscard]] void* binding() const noexcept { return m_binding; }

private:
    friend struct SBatchClientTestAccess;
    struct SPending
    {
        void* object{ nullptr };
        std::uint32_t correlation{ 0u };
    };

    SBatchShared* m_shared;
    CThreadControlState* m_control;
    CWaitPredicate* m_wake;
    module_ids::id_type m_module_id;
    void* m_binding;
    CBatchReturnChannel m_returns;
    SPending m_pending[k_batch_channel_capacity];
    std::atomic<std::uint32_t> m_outstanding{ 0u };
    std::atomic<std::uint32_t> m_return_publishers{ 0u };
};

}   //  namespace threading

#endif  //  CBATCH_WORK_HPP_INCLUDED
