
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_client.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Endpoint-local connections and bounded, authenticated common POD receiving.

#pragma once

#include "direct_connection.hpp"
#include "framework/threading/CEndpointContext.hpp"

namespace connections
{

[[nodiscard]] inline bool is_control(const threading::CErasedPodMsg& message) noexcept
{
    return message.is_payload_a<Registered>() || message.is_payload_a<Offer>() || message.is_payload_a<Install>() ||
        message.is_payload_a<Activate>() || message.is_payload_a<Closing>() || message.is_payload_a<Retired>() ||
        message.is_payload_a<Result>();
}

//  One helper belongs to one endpoint execution context. Module work may retain
//  tokens and copied PODs, never descriptors. The endpoint serializes helper use.
class CClient
{
public:
    static constexpr std::uint32_t k_capacity = 16u;
    explicit CClient(threading::CEndpointContext& context,
        const SBulkPolicy* const policies = nullptr, const std::uint32_t policy_count = 0u) noexcept
        : m_context{ context }, m_policies{ policies }, m_policy_count{ policy_count } {}
    CClient(const CClient&) = delete;
    CClient& operator=(const CClient&) = delete;

    [[nodiscard]] bool enable(const std::int32_t slot = 0) noexcept
    {
        if (failed() || m_closing || m_context.sealing() || (slot < 0))
        {
            return false;
        }
        return m_context.post(threading::make_pod_message(slot, Register{}));
    }
    [[nodiscard]] bool enabled() const noexcept { return m_enabled && !m_closing && !m_context.sealing(); }
    [[nodiscard]] bool open(const system_ids::id_type first, const system_ids::id_type second,
        const std::int32_t slot) noexcept;
    [[nodiscard]] bool open(const system_ids::id_type peer, const std::int32_t slot) noexcept
    {
        return open(address(), peer, slot);
    }
    [[nodiscard]] bool open_bulk(const module_ids::id_type peer, const std::uint32_t version, const std::int32_t slot,
        const std::uint64_t capacity, const bool producer) noexcept;
    [[nodiscard]] bool decide(const SToken token, const EStatus status) noexcept;
    //  At most one traversal and one returned record. Host mechanics run only
    //  for the Host queue; no module callback or hidden control drain occurs.
    [[nodiscard]] bool receive(SReceived& received, const bool direct_data = true) noexcept;
    void progress() noexcept;
    void close(const SToken token, const EStatus reason = EStatus::cancelled) noexcept;
    void close_all() noexcept;
    [[nodiscard]] bool quiesce(const SToken token) noexcept;
    [[nodiscard]] SToken active(const system_ids::id_type peer) const noexcept;
    [[nodiscard]] SToken active_bulk(const module_ids::id_type peer) const noexcept;
    [[nodiscard]] bool active(const SToken token) const noexcept;
    [[nodiscard]] bool closing(const SToken token) const noexcept;
    [[nodiscard]] SToken next_closing() const noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return m_failed || m_data_failed; }
    [[nodiscard]] bool needs_progress() const noexcept;
    [[nodiscard]] bool has_data(const bool direct_data = true) noexcept;
    void clear_notifications() noexcept;
    [[nodiscard]] EPost post(const system_ids::id_type peer, const threading::CErasedPodMsg& message,
        const SToken expected = {}) noexcept;
    [[nodiscard]] EPost post(const SToken token, const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] memory::CMemoryView producer_data(const SToken token) noexcept;
    [[nodiscard]] EBulkPublish publish_bulk(const SToken token) noexcept;
    [[nodiscard]] bool discard_and_acquire(const SToken token) noexcept;
    [[nodiscard]] memory::CMemoryConstView consumer_data(const SToken token) noexcept;
    [[nodiscard]] system_ids::id_type address() const noexcept
    {
        return system_ids::ops::make_system_id(m_context.module_id(), m_context.thread_id());
    }

private:
    enum class EPhase : std::uint8_t { empty, offered, accepted, installed, active, closing };
    struct SRecord
    {
        SToken token;
        system_ids::id_type peer{};
        services::SClientToken peer_instance;
        std::uint32_t version{ 1u };
        SBulkConfiguration bulk;
        const SEndpointDescriptor* endpoint{ nullptr };
        EPhase phase{ EPhase::empty };
        EStatus reason{ EStatus::cancelled }, decision{ EStatus::rejected };
        bool decision_pending{ false }, ready_pending{ false }, close_pending{ false };
        bool closing_seen{ false }, quiescence_pending{ false }, quiescent{ false };
    };
    struct SRequest
    {
        std::int32_t slot{ -1 };
        bool pending{ false };
    };
    template<typename T> bool send(const T& value) noexcept
    {
        if (m_failed) return false;
        const threading::SEndpointPost posted = m_context.post(threading::make_pod_message(-1, value));
        if (posted) return true;
        if (posted.status != threading::EEndpointPost::busy) m_failed = true;
        return false;
    }
    [[nodiscard]] bool request(const Request& request, const std::int32_t slot) noexcept;
    void handle_host(const threading::CErasedPodMsg& message) noexcept;
    void data_failure(const SToken token, const EStatus reason) noexcept;
    [[nodiscard]] ERead read(SRecord& record, threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] SRecord* find(const SToken token) noexcept;
    [[nodiscard]] const SRecord* find(const SToken token) const noexcept;
    [[nodiscard]] SRecord* vacant() noexcept;
    [[nodiscard]] bool bulk_policy(const module_ids::id_type peer, const std::uint32_t version,
        const SBulkConfiguration bulk) const noexcept;
    threading::CEndpointContext& m_context;
    const SBulkPolicy* m_policies;
    std::uint32_t m_policy_count;
    SRecord m_records[k_capacity];
    SRequest m_requests[k_capacity];
    std::uint32_t m_cursor{ 0u };
    bool m_failed{ false }, m_data_failed{ false }, m_enabled{ false }, m_closing{ false };
};

inline CClient::SRecord* CClient::find(const SToken token) noexcept
{
    if (!token.value) return nullptr;
    for (SRecord& record : m_records)
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
    for (const SRecord& record : m_records)
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
    for (SRecord& record : m_records)
    {
        if (record.phase == EPhase::empty) return &record;
    }
    return nullptr;
}
inline bool CClient::bulk_policy(const module_ids::id_type peer, const std::uint32_t version,
    const SBulkConfiguration bulk) const noexcept
{
    const SContract expected = contract(m_context.module_id(), peer, version);
    for (std::uint32_t index = 0u; index < m_policy_count; ++index)
    {
        const SBulkPolicy& candidate = m_policies[index];
        if ((candidate.identity.first == expected.first) && (candidate.identity.second == expected.second) &&
            (candidate.identity.version == version) && candidate.bulk.matches(bulk))
        {
            return true;
        }
    }
    return false;
}
inline bool CClient::request(const Request& value, const std::int32_t slot) noexcept
{
    if (m_failed || m_closing || (slot < 0) || m_context.sealing() || m_context.exit_requested())
    {
        return false;
    }
    SRequest* available = nullptr;
    for (SRequest& candidate : m_requests)
    {
        if (!candidate.pending)
        {
            if (!available) available = &candidate;
        }
        else if (candidate.slot == slot) return false;
    }
    if (!available) return false;
    *available = { slot, true };
    const threading::SEndpointPost posted = m_context.post(threading::make_pod_message(slot, value));
    if (posted) return true;
    if (posted.status == threading::EEndpointPost::transport_failed) m_failed = true;
    else *available = {};
    return false;
}
inline bool CClient::open(const system_ids::id_type first, const system_ids::id_type second,
    const std::int32_t slot) noexcept
{
    return request({ first, second }, slot);
}
inline bool CClient::open_bulk(const module_ids::id_type peer, const std::uint32_t version, const std::int32_t slot,
    const std::uint64_t capacity, const bool producer) noexcept
{
    const SBulkConfiguration bulk{ capacity, (producer ? m_context.module_id() : peer) };
    if (!threading::transports::CBulkBufferTransport::valid_capacity(capacity) || !bulk_policy(peer, version, bulk))
    {
        return false;
    }
    return request({ {}, {}, peer, version, bulk }, slot);
}
inline bool CClient::decide(const SToken token, const EStatus status) noexcept
{
    SRecord* const record = find(token);
    if (!record || (record->phase != EPhase::offered) || record->decision_pending)
    {
        return false;
    }
    record->decision = (m_closing || m_context.sealing() || m_context.exit_requested()) ? EStatus::cancelled : status;
    record->phase = (record->decision == EStatus::success) ? EPhase::accepted : EPhase::closing;
    record->decision_pending = true;
    progress();
    return true;
}

inline void CClient::handle_host(const threading::CErasedPodMsg& message) noexcept
{
    Registered registered;
    Offer offer;
    Install install;
    Activate activate;
    Closing closing;
    Retired retired;
    Result result;
    if (message.copy_payload_to(registered))
    {
        m_enabled = (registered.status == EStatus::success) && !m_closing && !m_context.sealing();
    }
    else if (message.copy_payload_to(offer))
    {
        if (find(offer.token)) return;
        SRecord* const record = vacant();
        if (!record)
        {
            //  No descriptor exists. A safely refused decision can time out.
            (void)send(Decision{ offer.token, EStatus::busy });
            return;
        }
        record->token = offer.token;
        record->peer = offer.peer_address;
        record->peer_instance = offer.peer;
        record->version = offer.version;
        record->bulk = offer.bulk;
        record->phase = EPhase::offered;
        if (!enabled() || m_context.exit_requested()) (void)decide(offer.token, EStatus::cancelled);
        else if (offer.bulk.is_bulk())
        {
            (void)decide(offer.token, bulk_policy(system_ids::ops::get_module_id(offer.peer_address),
                offer.version, offer.bulk) ? EStatus::success : EStatus::unsupported);
        }
    }
    else if (message.copy_payload_to(install))
    {
        //  Find the exact live obligation before touching the borrowed descriptor.
        //  Host FIFO ordering protects it until this side acknowledges Closing.
        SRecord* const record = find(install.token);
        if (!record || (record->phase != EPhase::accepted))
        {
            return;
        }
        const SEndpointDescriptor* const endpoint = install.descriptor;
        if (!endpoint || (endpoint->size != sizeof(SEndpointDescriptor)) ||
            (endpoint->token.value != install.token.value) || (endpoint->local.value != m_context.identity().value) ||
            (endpoint->local_address != address()) || (endpoint->peer.value != record->peer_instance.value) ||
            (endpoint->peer_address != record->peer) || (endpoint->version != record->version) ||
            !endpoint->bulk.matches(record->bulk) || !endpoint->admission ||
            !endpoint->notification.is_valid() || !endpoint->peer_notification.is_valid())
        {
            close(install.token, EStatus::invalid_request);
            return;
        }
        const bool valid = endpoint->bulk.is_bulk() ?
            (!endpoint->outgoing && !endpoint->incoming &&
                ((endpoint->bulk.producer == m_context.module_id()) ?
                    (endpoint->bulk_producer && !endpoint->bulk_consumer) :
                    (!endpoint->bulk_producer && endpoint->bulk_consumer))) :
            ((endpoint->version == 1u) && endpoint->outgoing && endpoint->incoming &&
                !endpoint->bulk_producer && !endpoint->bulk_consumer);
        if (!valid || m_closing || m_context.sealing())
        {
            close(install.token, EStatus::invalid_request);
            return;
        }
        record->endpoint = endpoint;
        record->phase = EPhase::installed;
        record->ready_pending = true;
    }
    else if (message.copy_payload_to(activate))
    {
        SRecord* const record = find(activate.token);
        if (record && (record->phase == EPhase::installed) && record->endpoint && !m_closing && !m_context.sealing())
        {
            record->phase = EPhase::active;
        }
    }
    else if (message.copy_payload_to(closing))
    {
        SRecord* const record = find(closing.token);
        if (record)
        {
            record->phase = EPhase::closing;
            record->reason = closing.reason;
            record->closing_seen = true;
            record->decision_pending = record->ready_pending = record->close_pending = false;
        }
    }
    else if (message.copy_payload_to(retired))
    {
        SRecord* const record = find(retired.token);
        if (record) *record = {};
    }
    else if (message.copy_payload_to(result))
    {
        for (SRequest& request : m_requests)
        {
            if (request.pending && (request.slot == message.query_async_slot()))
            {
                request = {};
                break;
            }
        }
    }
}

inline bool CClient::receive(SReceived& received, const bool direct_data) noexcept
{
    progress();
    if (!direct_data)
    {
        if (!m_context.read(received.message)) return false;
        received.source = {};
        handle_host(received.message);
        progress();
        return true;
    }
    for (std::uint32_t visited = 0u; visited <= k_capacity; ++visited)
    {
        const std::uint32_t selected = m_cursor;
        m_cursor = (m_cursor == k_capacity) ? 0u : (m_cursor + 1u);
        if (!selected)
        {
            if (!m_context.read(received.message)) continue;
            received.source = {};
            handle_host(received.message);
            progress();
            return true;
        }
        SRecord& record = m_records[selected - 1u];
        const ERead result = read(record, received.message);
        if (was_read(result))
        {
            received.source = { SSource::EKind::direct, record.peer, record.peer_instance, record.token };
            return true;
        }
    }
    return false;
}
inline void CClient::progress() noexcept
{
    if (m_failed) return;
    if (m_context.sealing() && !m_closing) close_all();
    for (SRecord& record : m_records)
    {
        if (record.endpoint)
        {
            if (record.bulk.is_bulk())
            {
                if (record.endpoint->notification.failed()) close(record.token, EStatus::notification_failed);
            }
            else
            {
                const COperation operation{ record.endpoint->admission };
                if (operation && record.endpoint->notification.failed()) data_failure(record.token, EStatus::notification_failed);
            }
        }
        if (record.decision_pending && send(Decision{ record.token, record.decision })) record.decision_pending = false;
        if (record.ready_pending && send(Ready{ record.token })) record.ready_pending = false;
        if (record.close_pending && send(Close{ record.token, record.reason })) record.close_pending = false;
        if (record.quiescence_pending && send(Quiescent{ record.token }))
        {
            record.quiescence_pending = false;
            record.quiescent = true;
        }
    }
}
inline void CClient::close(const SToken token, const EStatus reason) noexcept
{
    SRecord* const record = find(token);
    if (!record || (record->phase == EPhase::closing))
    {
        return;
    }
    record->phase = EPhase::closing;
    record->reason = reason;
    record->decision_pending = record->ready_pending = false;
    record->close_pending = true;
}
inline void CClient::close_all() noexcept
{
    m_closing = true;
    for (SRecord& record : m_records)
    {
        if (record.phase != EPhase::empty) close(record.token);
    }
}
inline void CClient::data_failure(const SToken token, const EStatus reason) noexcept
{
    //  Uncertain publication must not be retried. Fail the endpoint while
    //  retaining Host control progress and every explicit quiescence obligation.
    close(token, reason);
    m_data_failed = true;
    close_all();
    m_context.fail((reason == EStatus::transport_failed) ? threading::EEndpointFailure::direct_transport_poisoned :
        threading::EEndpointFailure::wake_failed);
}
inline bool CClient::quiesce(const SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!record || !record->closing_seen)
    {
        return false;
    }
    if (!record->quiescent && !record->quiescence_pending)
    {
        //  Helper calls are serial; every local operation guard has unwound.
        //  Peer operations do not delay this acknowledgement or need a wait.
        record->endpoint = nullptr;
        record->quiescence_pending = true;
    }
    progress();
    return record->quiescent;
}
inline SToken CClient::active(const system_ids::id_type peer) const noexcept
{
    for (const SRecord& record : m_records)
    {
        if ((record.peer == peer) && !record.bulk.is_bulk() && active(record.token))
        {
            return record.token;
        }
    }
    return {};
}
inline SToken CClient::active_bulk(const module_ids::id_type peer) const noexcept
{
    for (const SRecord& record : m_records)
    {
        if ((system_ids::ops::get_module_id(record.peer) == peer) && record.bulk.is_bulk() && active(record.token))
        {
            return record.token;
        }
    }
    return {};
}
inline bool CClient::active(const SToken token) const noexcept
{
    const SRecord* const record = find(token);
    return !m_failed && !m_closing && !m_context.sealing() && record && (record->phase == EPhase::active) &&
        record->endpoint && (record->bulk.is_bulk() || record->endpoint->admission->available());
}
inline bool CClient::closing(const SToken token) const noexcept
{
    const SRecord* const record = find(token);
    return !record || (record->phase == EPhase::closing);
}
inline SToken CClient::next_closing() const noexcept
{
    for (const SRecord& record : m_records)
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
    for (const SRecord& record : m_records)
    {
        if (record.phase != EPhase::empty) return false;
    }
    for (const SRequest& request : m_requests)
    {
        if (request.pending)
        {
            return false;
        }
    }
    return true;
}
inline bool CClient::needs_progress() const noexcept
{
    if (m_failed) return false;
    for (const SRecord& record : m_records)
    {
        if (record.decision_pending || record.ready_pending || record.close_pending || record.quiescence_pending)
        {
            return true;
        }
    }
    return false;
}
inline bool CClient::has_data(const bool direct_data) noexcept
{
    if (m_context.has_data()) return true;
    if (!direct_data || m_closing || m_failed || m_context.sealing())
    {
        return false;
    }
    for (SRecord& record : m_records)
    {
        if ((record.phase != EPhase::active) || !record.endpoint)
        {
            continue;
        }
        if (record.bulk.is_bulk())
        {
            if (record.endpoint->bulk_consumer && record.endpoint->bulk_consumer->has_staged_data())
            {
                return true;
            }
            continue;
        }
        const COperation operation{ record.endpoint->admission };
        if (!operation) continue;
        if (record.endpoint->incoming && record.endpoint->incoming->refresh_readable_count())
        {
            return true;
        }
    }
    return false;
}
inline void CClient::clear_notifications() noexcept
{
    for (SRecord& record : m_records)
    {
        if (!record.endpoint) continue;
        if (record.bulk.is_bulk())
        {
            record.endpoint->notification.clear_pending();
            continue;
        }
        const COperation operation{ record.endpoint->admission };
        if (operation) record.endpoint->notification.clear_pending();
    }
}
inline EPost CClient::post(const system_ids::id_type peer, const threading::CErasedPodMsg& message,
    const SToken expected) noexcept
{
    const SToken token = active(peer);
    if (!token.value || (expected.value && (expected.value != token.value)))
    {
        return EPost::no_pipe;
    }
    return post(token, message);
}
inline EPost CClient::post(const SToken token, const threading::CErasedPodMsg& message) noexcept
{
    SRecord* const record = find(token);
    if (!active(token) || !record->endpoint->outgoing)
    {
        return EPost::no_pipe;
    }
    system_type_id type;
    if (!message.query_message_type_id().try_system_type_id(type))
    {
        return EPost::invalid_message;
    }
    const SEndpointDescriptor& endpoint = *record->endpoint;
    const COperation operation{ endpoint.admission };
    if (!operation) return EPost::no_pipe;
    if (!endpoint.outgoing->post(message))
    {
        if (!endpoint.outgoing->posting_poisoned())
        {
            return EPost::full;
        }
        (void)endpoint.peer_notification.signal();
        data_failure(token, EStatus::transport_failed);
        return EPost::transport_failed;
    }
    if (endpoint.peer_notification.signal())
    {
        return EPost::queued;
    }
    data_failure(token, EStatus::notification_failed);
    return EPost::queued_wake_failed;
}
inline ERead CClient::read(SRecord& record, threading::CErasedPodMsg& message) noexcept
{
    if (!active(record.token) || !record.endpoint->incoming)
    {
        return ERead::inactive;
    }
    const SEndpointDescriptor& endpoint = *record.endpoint;
    const COperation operation{ endpoint.admission };
    if (!operation) return ERead::inactive;
    if (!endpoint.incoming->read(message))
    {
        return ERead::empty;
    }
    const bool signalled = endpoint.peer_notification.signal();
    if (signalled) return ERead::record;
    data_failure(record.token, EStatus::notification_failed);
    return ERead::record_wake_failed;
}
inline memory::CMemoryView CClient::producer_data(const SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!active(token) || !record->endpoint->bulk_producer)
    {
        return {};
    }
    return record->endpoint->bulk_producer->producer_data();
}
inline EBulkPublish CClient::publish_bulk(const SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!active(token) || !record->endpoint->bulk_producer)
    {
        return EBulkPublish::inactive;
    }
    (void)record->endpoint->bulk_producer->publish();
    if (record->endpoint->peer_notification.signal())
    {
        return EBulkPublish::published;
    }
    close(token, EStatus::notification_failed);
    return EBulkPublish::published_wake_failed;
}
inline bool CClient::discard_and_acquire(const SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!active(token) || !record->endpoint->bulk_consumer)
    {
        return false;
    }
    return record->endpoint->bulk_consumer->discard_and_acquire();
}
inline memory::CMemoryConstView CClient::consumer_data(const SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!active(token) || !record->endpoint->bulk_consumer)
    {
        return {};
    }
    return record->endpoint->bulk_consumer->consumer_data();
}

}   //  namespace connections
