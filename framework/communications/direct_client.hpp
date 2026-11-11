
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_client.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Service-thread-local connection state. Deferred work retains only SToken.

#pragma once

#include "direct_connection.hpp"
#include "framework/threading/CThreadContext.hpp"
#include "framework/threading/messages/message_helpers.hpp"

namespace connections
{

class CClient
{
public:
    static constexpr std::uint32_t k_capacity = 16u;
    CClient(threading::CThreadContext& context, const module_ids::id_type role,
        const SContractPolicy* const policies, const std::uint32_t policy_count) noexcept
        : m_context{ context }, m_role{ role }, m_policies{ policies }, m_policy_count{ policy_count } {}
    CClient(const CClient&) = delete;
    CClient& operator=(const CClient&) = delete;

    //  Must be the first startup POD, before publishing Running.
    [[nodiscard]] bool register_service() noexcept { return send(Register{}); }
    [[nodiscard]] bool open(const module_ids::id_type peer, const std::uint32_t version, const std::int32_t slot) noexcept;
    [[nodiscard]] bool handle(const threading::CErasedPodMsg& message) noexcept;
    void progress() noexcept;
    void close(const SToken token, const EStatus reason = EStatus::cancelled) noexcept;
    void close_all() noexcept;
    //  Call only after connection-dependent local cleanup and all endpoint users unwind.
    [[nodiscard]] bool quiesce(const SToken token) noexcept;
    [[nodiscard]] bool take_result(Result& result, std::int32_t& slot) noexcept;
    [[nodiscard]] SToken active(const module_ids::id_type peer) const noexcept;
    [[nodiscard]] bool active(const SToken token) const noexcept;
    [[nodiscard]] bool closing(const SToken token) const noexcept;
    [[nodiscard]] SToken next_closing() const noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool needs_progress() const noexcept;
    [[nodiscard]] bool has_data() noexcept;
    void clear_notifications() noexcept;
    [[nodiscard]] EPost post(const SToken token, const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] ERead read(const SToken token, threading::CErasedPodMsg& message) noexcept;

private:
    enum class EPhase : std::uint8_t { empty, requesting, offered, installed, active, closing, retired };
    struct SRecord
    {
        SToken token;
        module_ids::id_type peer{};
        modules::SModuleInstance peer_instance;
        std::uint32_t version{ 0u };
        const SContractPolicy* policy{ nullptr };
        const SEndpointDescriptor* endpoint{ nullptr };
        EPhase phase{ EPhase::empty };
        EStatus reason{ EStatus::cancelled };
        bool decision_pending{ false }, ready_pending{ false }, close_pending{ false };
        bool closing_seen{ false }, quiescence_pending{ false }, quiescent{ false };
    };
    //  A reciprocal request may still be in Host's inbox after an independently
    //  offered connection retires. Its correlation has a separate local lifetime.
    struct SRequest
    {
        module_ids::id_type peer{};
        std::uint32_t version{ 0u };
        std::int32_t slot{ -1 };
        bool pending{ false }, completed{ false }, cancelled{ false };
        Result result;
    };
    template<typename T> bool send(const T& value) noexcept
    {
        if (m_failed) return false;
        if (m_context.post(threading::make_pod_message(-1, value)))
        {
            return true;
        }
        if (m_context.pod_submission_poisoned())
        {
            m_failed = true;
        }
        return false;
    }
    [[nodiscard]] SRecord* find(const SToken token) noexcept;
    [[nodiscard]] const SRecord* find(const SToken token) const noexcept;
    [[nodiscard]] SRecord* vacant() noexcept;
    [[nodiscard]] const SContractPolicy* policy(const module_ids::id_type peer, const std::uint32_t version) const noexcept;
    threading::CThreadContext& m_context;
    module_ids::id_type m_role;
    const SContractPolicy* m_policies;
    std::uint32_t m_policy_count;
    SRecord m_records[k_capacity];
    SRequest m_requests[k_capacity];
    bool m_failed{ false };
};

inline CClient::SRecord* CClient::find(const SToken token) noexcept
{
    if (!token.value) return nullptr;
    for (auto& record : m_records)
    {
        if ((record.phase != EPhase::empty) && (record.token.value == token.value))
        {
            return &record;
        }
    }
    return nullptr;
}
inline const CClient::SRecord* CClient::find(const SToken token) const noexcept
{
    if (!token.value) return nullptr;
    for (const auto& record : m_records)
    {
        if ((record.phase != EPhase::empty) && (record.token.value == token.value))
        {
            return &record;
        }
    }
    return nullptr;
}
inline CClient::SRecord* CClient::vacant() noexcept
{
    for (auto& record : m_records)
    {
        if (record.phase == EPhase::empty)
        {
            return &record;
        }
    }
    return nullptr;
}
inline const SContractPolicy* CClient::policy(const module_ids::id_type peer, const std::uint32_t version) const noexcept
{
    const auto expected = contract(m_role, peer, version);
    for (std::uint32_t index = 0u; index < m_policy_count; ++index)
    {
        const auto& candidate = m_policies[index].identity;
        if ((candidate.first == expected.first) && (candidate.second == expected.second) && (candidate.version == version))
        {
            return &m_policies[index];
        }
    }
    return nullptr;
}
inline bool CClient::open(const module_ids::id_type peer, const std::uint32_t version, const std::int32_t slot) noexcept
{
    if (m_failed || (slot < 0) || !version || !policy(peer, version) || m_context.exit_requested())
    {
        return false;
    }
    SRequest* vacant = nullptr;
    for (auto& request : m_requests)
    {
        if (!request.pending && !request.completed)
        {
            if (!vacant) vacant = &request;
            continue;
        }
        if ((request.peer == peer) || (request.slot == slot))
        {
            return false;
        }
    }
    if (!vacant) return false;
    *vacant = { peer, version, slot, true, false, false, {} };
    if (m_context.post(threading::make_pod_message(slot, Request{ peer, version })))
    {
        return true;
    }
    if (m_context.pod_submission_poisoned())
    {
        m_failed = true;
    }
    else
    {
        *vacant = {};
    } // Only an ordinary rejection proves no publication.
    return false;
}

inline bool CClient::handle(const threading::CErasedPodMsg& message) noexcept
{
    Offer offer;
    Install install;
    Activate activate;
    Closing closing;
    Retired retired;
    Result result;
    if (message.copy_payload_to(offer))
    {
        auto* record = find(offer.token);
        if (record) return true;
        const auto* const accepted_policy = policy(offer.peer_role, offer.version);
        if (!accepted_policy || m_context.exit_requested())
        {
            (void)send(Decision{ offer.token, EStatus::unsupported });
            return true;
        }
        record = vacant();
        if (!record)
        {
            (void)send(Decision{ offer.token, EStatus::busy });
            return true;
        }
        record->token = offer.token;
        record->peer = offer.peer_role;
        record->peer_instance = offer.peer;
        record->version = offer.version;
        record->policy = accepted_policy;
        record->phase = EPhase::offered;
        for (const auto& request : m_requests)
        {
            if (request.pending && request.cancelled && (request.peer == offer.peer_role) && (request.version == offer.version))
            {
                record->phase = EPhase::closing;
                record->close_pending = true;
            }
        }
        record->decision_pending = true;
    }
    else if (message.copy_payload_to(install))
    {
        //  Host's FIFO publication obligation protects the descriptor until our
        //  closing-boundary acknowledgement, including a locally cancelled open.
        const auto* const endpoint = install.descriptor;
        const auto* const execution = m_context.host_execution();
        if (!endpoint || !execution || (endpoint->token.value != install.token.value) ||
            (endpoint->local.value != execution->instance.value) || (endpoint->local_role != m_role))
        {
            (void)send(Close{ install.token, EStatus::invalid_request });
            return true;
        }
        auto* record = find(install.token);
        if (!record)
        {
            for (const auto& request : m_requests)
            {
                if (!request.pending || (request.peer != endpoint->peer_role) || (request.version != endpoint->version))
                {
                    continue;
                }
                record = vacant();
                if (!record) break;
                record->peer = request.peer;
                record->version = request.version;
                record->policy = policy(request.peer, request.version);
                record->phase = request.cancelled ? EPhase::closing : EPhase::requesting;
                break;
            }
        }
        if (!record)
        {
            (void)send(Close{ install.token, EStatus::invalid_request });
            return true;
        }
        record->token = install.token;
        if ((endpoint->peer_role != record->peer) || (endpoint->version != record->version) ||
            !endpoint->peer.value || (record->peer_instance.value && (record->peer_instance.value != endpoint->peer.value)) ||
            !endpoint->outgoing || !endpoint->incoming || !endpoint->notification.is_valid() || !endpoint->peer_notification.is_valid())
        {
            close(install.token, EStatus::invalid_request);
            return true;
        }
        record->peer_instance = endpoint->peer;
        if ((record->phase == EPhase::closing) || record->quiescent || !record->policy)
        {
            record->close_pending = true;
            return true;
        }
        if (record->endpoint) return true;
        record->endpoint = endpoint;
        record->phase = EPhase::installed;
        record->ready_pending = true;
    }
    else if (message.copy_payload_to(activate))
    {
        if (auto* const record = find(activate.token))
        {
            if ((record->phase == EPhase::installed) && record->endpoint)
            {
                record->phase = EPhase::active;
            }
        }
    }
    else if (message.copy_payload_to(closing))
    {
        if (auto* const record = find(closing.token))
        {
            record->phase = EPhase::closing;
            record->reason = closing.reason;
            record->closing_seen = true;
            record->decision_pending = record->ready_pending = record->close_pending = false;
        }
        else
        {
            (void)send(Quiescent{ closing.token });
        } // No local endpoint was installed.
    }
    else if (message.copy_payload_to(retired))
    {
        if (auto* const record = find(retired.token))
        {
            record->endpoint = nullptr;
            *record = {};
        }
    }
    else if (message.copy_payload_to(result))
    {
        for (auto& request : m_requests)
        {
            if (!request.pending || (request.slot != message.query_async_slot()))
            {
                continue;
            }
            request.pending = false;
            request.completed = true;
            request.result = result;
            if (request.cancelled && result.token.value)
            {
                close(result.token);
            }
            break;
        }
    }
    else return false;
    progress();
    return true;
}

inline void CClient::progress() noexcept
{
    if (m_failed) return;
    for (auto& record : m_records)
    {
        if (record.endpoint && record.endpoint->notification.failed())
        {
            close(record.token, EStatus::notification_failed);
        }
        if (record.decision_pending && send(Decision{ record.token,
            ((record.phase == EPhase::closing) ? EStatus::cancelled : EStatus::success) }))
        {
            record.decision_pending = false;
        }
        if (record.ready_pending && send(Ready{ record.token }))
        {
            record.ready_pending = false;
        }
        if (record.close_pending && record.token.value && send(Close{ record.token, record.reason }))
        {
            record.close_pending = false;
        }
        if (record.quiescence_pending && send(Quiescent{ record.token }))
        {
            record.quiescence_pending = false;
            record.quiescent = true;
        }
    }
}

inline void CClient::close(const SToken token, const EStatus reason) noexcept
{
    auto* const record = find(token);
    if (!record || (record->phase == EPhase::retired) || (record->phase == EPhase::closing))
    {
        return;
    }
    record->phase = EPhase::closing;
    record->reason = reason;
    record->ready_pending = false;
    record->close_pending = true;
}
inline void CClient::close_all() noexcept
{
    for (auto& request : m_requests)
    {
        if (request.pending)
        {
            request.cancelled = true;
        }
    }
    for (auto& record : m_records)
    {
        if ((record.phase == EPhase::empty) || (record.phase == EPhase::retired))
        {
            continue;
        }
        if (record.token.value) close(record.token);
        else
        {
            record.phase = EPhase::closing;
            record.close_pending = true;
        }
    }
}
inline bool CClient::quiesce(const SToken token) noexcept
{
    auto* const record = find(token);
    if (!record || !record->closing_seen)
    {
        return false;
    }
    if (!record->quiescent && !record->quiescence_pending)
    {
        record->endpoint = nullptr;
        record->quiescence_pending = true;
    }
    progress();
    return record->quiescent;
}
inline bool CClient::take_result(Result& result, std::int32_t& slot) noexcept
{
    for (auto& request : m_requests)
    {
        if (!request.completed) continue;
        result = request.result;
        slot = request.slot;
        request = {};
        return true;
    }
    return false;
}
inline SToken CClient::active(const module_ids::id_type peer) const noexcept
{
    for (const auto& record : m_records)
    {
        if ((record.phase == EPhase::active) && (record.peer == peer))
        {
            return record.token;
        }
    }
    return {};
}
inline bool CClient::active(const SToken token) const noexcept
{
    const auto* const record = find(token);
    return record && (record->phase == EPhase::active);
}
inline bool CClient::closing(const SToken token) const noexcept
{
    const auto* const record = find(token);
    return !record || (record->phase == EPhase::closing) || (record->phase == EPhase::retired);
}
inline SToken CClient::next_closing() const noexcept
{
    for (const auto& record : m_records)
    {
        if ((record.phase == EPhase::closing) && record.closing_seen && !record.quiescent)
        {
            return record.token;
        }
    }
    return {};
}
inline bool CClient::idle() const noexcept
{
    for (const auto& record : m_records)
    {
        if (record.phase != EPhase::empty)
        {
            return false;
        }
    }
    for (const auto& request : m_requests)
    {
        if (request.pending || request.completed)
        {
            return false;
        }
    }
    return true;
}
inline bool CClient::needs_progress() const noexcept
{
    if (m_failed) return false;
    for (const auto& record : m_records)
    {
        if (record.decision_pending || record.ready_pending || (record.close_pending && record.token.value) || record.quiescence_pending)
        {
            return true;
        }
    }
    return false;
}
inline bool CClient::has_data() noexcept
{
    for (auto& record : m_records)
    {
        if ((record.phase == EPhase::active) && record.endpoint && record.endpoint->incoming->refresh_readable_count())
        {
            return true;
        }
    }
    return false;
}
inline void CClient::clear_notifications() noexcept
{
    for (auto& record : m_records)
    {
        if (record.endpoint)
        {
            record.endpoint->notification.clear_pending();
        }
    }
}

inline EPost CClient::post(const SToken token, const threading::CErasedPodMsg& message) noexcept
{
    auto* const record = find(token);
    if (m_failed || !record || (record->phase != EPhase::active) || !record->endpoint)
    {
        return EPost::inactive;
    }
    if (!record->policy->admits(message, m_role))
    {
        return EPost::invalid_message;
    }
    auto& endpoint = *record->endpoint;
    if (!endpoint.outgoing->post(message))
    {
        if (!endpoint.outgoing->posting_poisoned())
        {
            return EPost::full;
        }
        //  Publication may have happened before allocation failed. Never retry.
        (void)endpoint.peer_notification.signal();
        close(token, EStatus::transport_failed);
        return EPost::transport_failed;
    }
    if (endpoint.peer_notification.signal())
    {
        return EPost::queued;
    }
    close(token, EStatus::notification_failed);
    return EPost::queued_wake_failed;
}
inline ERead CClient::read(const SToken token, threading::CErasedPodMsg& message) noexcept
{
    auto* const record = find(token);
    if (m_failed || !record || (record->phase != EPhase::active) || !record->endpoint)
    {
        return ERead::inactive;
    }
    auto& endpoint = *record->endpoint;
    if (!endpoint.incoming->read(message))
    {
        return ERead::empty;
    }
    const bool signalled = endpoint.peer_notification.signal();
    if (!record->policy->admits(message, record->peer))
    {
        close(token, EStatus::invalid_request);
        return ERead::invalid_message;
    }
    if (signalled) return ERead::record;
    close(token, EStatus::notification_failed);
    return ERead::record_wake_failed;
}

}   //  namespace connections
