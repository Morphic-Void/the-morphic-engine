
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    modal_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Incremental consumer of a borrowed simulated native-modal invocation.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/host_eligible_budget.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/filesystem/filesystem_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "tests/host_pump/fixture_types.hpp"
#include "debug/macros.hpp"
#include <cstring>

namespace pump_tests
{

class CModalExecution
{
public:
    enum class EMode : unsigned { normal, full, shutdown };
    CModalExecution(const modules::CCommunication& communication, const modules::SHostExecutionContext* const execution) noexcept
        : m_communication{ communication }, m_execution{ execution } {}
    bool start(const services::SServiceRoute route, const EMode mode) noexcept
    {
        if (m_active || !m_execution || !m_execution->restriction.is_valid() || !m_consumer.begin(route, { 1u }, 8u))
        {
            return false;
        }
        m_route = route;
        m_mode = mode;
        m_active = true;
        services::ServiceRequest request;
        request.route = route;
        request.request = { 1u };
        request.opcode = 8u;
        prepare(EPost::request, 80, request);
        if (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) unsafe();
        return !m_failed;
    }
    void stop() noexcept { m_stopping = true; leave(); }
    bool failed() const noexcept { return m_failed; }
    bool done() const noexcept { return m_done; }
    bool domain_complete() const noexcept { return !m_active || (m_done && !m_unsafe); }
    bool owns(const threading::CErasedPodMsg& message) const noexcept
    {
        const std::int32_t slot = message.query_async_slot();
        return m_active && ((slot == 80) || (slot == 81) || ((slot >= 9000) && (slot < 10064)) ||
            message.is_payload_a<services::ServiceBorrowControl>());
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_unsafe) return;
        const std::int32_t slot = message.query_async_slot();
        if (slot >= 9000)
        {
            if (m_entered && !m_left) fail();
            if (slot < 10000)
            {
                const unsigned index = static_cast<unsigned>(slot - 9000);
                AssetReleaseResult result;
                if ((index >= m_pods) || m_pod_seen[index] || !message.copy_payload_to(result) || result.acquisition ||
                    (result.status != EAssetStatus::invalid_request))
                {
                    unsafe();
                    return;
                }
                m_pod_seen[index] = true;
            }
            else
            {
                const unsigned index = static_cast<unsigned>(slot - 10000);
                filesystem::FilesystemRefreshResult result;
                if ((index >= m_owners) || m_owner_seen[index] || !message.copy_payload_to(result) ||
                    ((result.status != filesystem::EFilesystemStatus::invalid_root) &&
                        (result.status != filesystem::EFilesystemStatus::queue_full)))
                {
                    unsafe();
                    return;
                }
                m_owner_seen[index] = true;
            }
            ++m_replies;
            return;
        }
        services::ServiceReply reply;
        services::ServiceBorrowControl control;
        if (message.copy_payload_to(reply))
        {
            if (slot == 81)
            {
                if (!m_modal_sent || m_modal_done || (reply.route.value != m_route.value) ||
                    (reply.request.value != 2u) || (reply.opcode != 9u) || reply.payload_size)
                {
                    unsafe();
                    return;
                }
                if (reply.notice == services::EServiceNotice::acknowledged)
                {
                    if (m_modal_acknowledged || (reply.status != services::EServiceStatus::success)) unsafe();
                    else m_modal_acknowledged = true;
                    return;
                }
                if ((reply.notice != services::EServiceNotice::completed) ||
                    (reply.status > services::EServiceStatus::delivery_failed))
                {
                    unsafe();
                    return;
                }
                m_modal_done = true;
                if ((!m_left && m_entered) || ((reply.status == services::EServiceStatus::success) && !m_modal_acknowledged) ||
                    ((reply.status != services::EServiceStatus::success) && !(m_stopping && (reply.status == services::EServiceStatus::cancelled)))) fail();
                return;
            }
            if ((slot != 80) || !m_submitted || !m_consumer.observe(reply))
            {
                unsafe();
                return;
            }
            if (reply.notice == services::EServiceNotice::prepared)
            {
                m_borrow = m_consumer.borrow(m_route, { 1u });
                const services::SServiceView view = m_consumer.view(m_borrow);
                if (!m_stopping && !m_releasing && view.descriptor)
                {
                    if ((view.size != sizeof(SModalDescriptor)) || (view.version != 1u)) fail();
                    else
                    {
                        m_data = static_cast<const SModalDescriptor*>(view.descriptor)->state;
                        if (!m_data) fail();
                    }
                }
            }
            if (reply.notice == services::EServiceNotice::completed)
            {
                m_terminal = true;
                if (reply.status != services::EServiceStatus::success)
                {
                    if (!(m_stopping && (reply.status == services::EServiceStatus::cancelled))) fail();
                    m_releasing = true;
                    leave();
                }
            }
        }
        else if (message.copy_payload_to(control))
        {
            if ((control.route.value != m_route.value) || (control.request.value != 1u) ||
                ((control.control == services::EServiceBorrowControl::retired) && !m_quiescent_sent &&
                    (control.quiescence != services::EServiceQuiescence::never_published)) || !m_consumer.observe(control))
            {
                unsafe();
                return;
            }
            m_borrow = control.borrow;
            m_releasing = true;
            leave();
        }
        else unsafe();
    }
    void advance(bool& runnable, std::uint64_t& next_wake) noexcept
    {
        if (!m_active || m_done || m_unsafe) return;
        if (m_stopping && !m_submitted)
        {
            m_done = m_consumer.abandon_unsubmitted(m_route, { 1u });
            if (!m_done) unsafe();
            m_post = EPost::none;
            return;
        }
        std::uint64_t remaining = 0u;
        if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            unsafe();
            return;
        }
        if (remaining < next_wake) next_wake = remaining;
        // Even an output-busy turn may be waiting on a native-maintenance
        // atomic (pressure/entry/progress) which does not signal this context.
        if (m_data && !m_left && (next_wake > 1000000u)) next_wake = 1000000u;
        if (m_stopping || m_releasing)
        {
            m_owner = {};
            if ((m_post == EPost::modal) || (m_post == EPost::flood)) m_post = EPost::none;
        }
        for (unsigned turn = 0u; turn < 4u; ++turn)
        {
            if (m_post != EPost::none)
            {
                const modules::ESubmission result = m_owner.has_owner() ?
                    m_communication.submit({}, std::move(m_owner)) : m_communication.submit({}, m_pending);
                if (result == modules::ESubmission::busy)
                {
                    if ((m_post == EPost::flood) && m_owner.has_owner() && (m_owners >= 32u) &&
                        m_data && m_data->pressure.load(std::memory_order_acquire))
                    {
                        m_owner = {}; // Refused ownership never entered transport.
                        m_post = EPost::none;
                        m_flood_complete = true;
                    }
                    else return;
                }
                else if (!modules::accepted(result))
                {
                    if (m_post == EPost::request)
                    {
                        m_done = m_consumer.abandon_unsubmitted(m_route, { 1u });
                        if (!m_done) unsafe();
                        fail();
                    }
                    else unsafe();
                    return;
                }
                else
                {
                    if (m_post == EPost::request) m_submitted = true;
                    if (m_post == EPost::modal) m_modal_sent = true;
                    if (m_post == EPost::quiesce) m_quiescent_sent = true;
                    if (m_post == EPost::flood)
                    {
                        if (m_pods < 512u) ++m_pods;
                        else ++m_owners;
                    }
                    m_post = EPost::none;
                }
            }
            if (m_terminal && (!m_modal_sent || m_modal_done) && (m_replies == (m_pods + m_owners)) &&
                (!m_borrow.value || m_consumer.retired(m_borrow)))
            {
                if (!m_consumer.forget(m_route, { 1u })) unsafe();
                else m_done = true;
                if (m_done && !m_failed) MV_LOG(info, "Pump fixture: modal budgets, deferred replies and borrowed retirement passed");
                return;
            }
            if (!m_stopping && !m_releasing && !m_left && m_data)
            {
                // Native fixture atomics have no per-field notification. Poll
                // once per timed invocation; never keep a runnable spin alive.
                if (next_wake > 1000000u) next_wake = 1000000u;
                if (!m_modal_sent)
                {
                    if (!m_terminal || !m_data->ready.load(std::memory_order_acquire)) return;
                    services::ServiceRequest request;
                    request.route = m_route;
                    request.request = { 2u };
                    request.opcode = 9u;
                    prepare(EPost::modal, 81, request);
                    continue;
                }
                if (!m_modal_acknowledged || !m_data->entered.load(std::memory_order_acquire)) return;
                m_entered = true;
                if (!m_budget_started)
                {
                    modules::SHostRestrictionSnapshot snapshot;
                    const modules::ERestrictionRead read = m_execution->restriction.read(snapshot);
                    if (read == modules::ERestrictionRead::retry) return;
                    if ((read != modules::ERestrictionRead::success) || (snapshot.depth != 3u) ||
                        (snapshot.counts[0] != 2u) || (snapshot.counts[1] != 1u))
                    {
                        fail();
                        continue;
                    }
                    const modules::EBudgetStatus status = m_budget.start(m_execution->restriction, 50000000u);
                    if (status == modules::EBudgetStatus::retry) return;
                    if ((status != modules::EBudgetStatus::restricted) ||
                        (m_hold.set_after_milliseconds(100u).code != platform::threading::EWaitError::none))
                    {
                        fail();
                        continue;
                    }
                    m_budget_started = true;
                }
                if ((m_mode == EMode::full) && !m_flood_complete)
                {
                    if (m_pods < 512u) prepare(EPost::flood, (9000 + static_cast<std::int32_t>(m_pods)), AssetReleaseRequest{});
                    else if (m_owners < 64u)
                    {
                        m_owner = threading::make_owner_message<filesystem::FilesystemRefreshRequest>(10000 + static_cast<std::int32_t>(m_owners));
                        filesystem::FilesystemRefreshRequest* const request = m_owner.owner().payload<filesystem::FilesystemRefreshRequest>();
                        if (!request || !request->root.set("unknown:"))
                        {
                            fail();
                            continue;
                        }
                        m_post = EPost::flood;
                    }
                    else m_flood_complete = true;
                    if (m_post != EPost::none) continue;
                }
                std::uint64_t held = 0u;
                if (m_hold.remaining_nanoseconds(held).code != platform::threading::EWaitError::none)
                {
                    fail();
                    continue;
                }
                if (held || (m_data->passes.load(std::memory_order_acquire) < 8u)) return;
                const modules::SEligibleBudgetObservation observed = m_budget.observe();
                if (observed.status == modules::EBudgetStatus::retry) return;
                if ((observed.status != modules::EBudgetStatus::restricted) || (observed.remaining_ns != 50000000u) ||
                    ((m_mode == EMode::full) && !m_data->pressure.load(std::memory_order_acquire)))
                {
                    fail();
                    continue;
                }
                if ((m_mode == EMode::shutdown) && !m_execution->stop.request()) fail();
                leave();
            }
            if (!m_releasing && m_borrow.value && (m_left || m_stopping))
            {
                services::ReleaseServiceBorrow release;
                if (!m_consumer.release(m_borrow, release))
                {
                    unsafe();
                    return;
                }
                m_releasing = true;
                prepare(EPost::release, -1, release);
            }
            else if (m_releasing && !m_quiescent_prepared)
            {
                services::ServiceBorrowQuiescent quiescent;
                if (!m_consumer.quiesce(m_borrow, quiescent)) return;
                m_quiescent_prepared = true;
                prepare(EPost::quiesce, -1, quiescent);
            }
            else return;
        }
        runnable = (m_post != EPost::none) || (m_stopping && !m_releasing && m_borrow.value);
    }

private:
    friend struct SModalExecutionTestAccess;
    enum class EPost : unsigned { none, request, modal, flood, release, quiesce };
    const modules::CCommunication& m_communication;
    const modules::SHostExecutionContext* const m_execution;
    services::CServiceBorrowClient m_consumer;
    services::SServiceRoute m_route;
    services::SServiceBorrow m_borrow;
    SModalData* m_data{ nullptr };
    threading::CErasedPodMsg m_pending;
    threading::CErasedOwnerMsg m_owner;
    platform::threading::CWaitDeadline m_deadline, m_hold;
    modules::CHostEligibleBudget m_budget;
    EMode m_mode{ EMode::normal };
    EPost m_post{ EPost::none };
    unsigned m_pods{}, m_owners{}, m_replies{};
    bool m_pod_seen[512]{}, m_owner_seen[64]{};
    bool m_active{}, m_submitted{}, m_terminal{}, m_stopping{}, m_releasing{}, m_quiescent_prepared{}, m_quiescent_sent{};
    bool m_modal_sent{}, m_modal_acknowledged{}, m_modal_done{}, m_budget_started{}, m_entered{}, m_left{}, m_flood_complete{};
    bool m_failed{}, m_unsafe{}, m_done{};
    template<typename T> void prepare(const EPost post, const std::int32_t slot, const T& value) noexcept
    {
        m_pending = threading::make_pod_message(slot, value);
        m_post = post;
    }
    void leave() noexcept
    {
        if (m_data) m_data->leave.store(1u, std::memory_order_release);
        m_data = nullptr;
        m_left = true;
    }
    void fail() noexcept { m_failed = m_stopping = true; leave(); m_owner = {}; }
    void unsafe() noexcept { fail(); m_unsafe = true; }
};

}   // namespace pump_tests
