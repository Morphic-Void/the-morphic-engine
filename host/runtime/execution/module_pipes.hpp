
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_pipes.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Host-owned ordinary rings and bounded local storage for module execution.

#pragma once

#include "framework/threading/messages/CErasedPodMsg.hpp"
#include "foundation/threading/transports/TRingTransport.hpp"
#include "foundation/containers/TPodFifo.hpp"
#include "foundation/system/erased_transport_admission.hpp"

namespace host
{

inline constexpr std::uint32_t k_module_pod_capacity = 1024u;
inline constexpr std::uint32_t k_module_lifecycle_capacity = 256u;
inline constexpr std::uint32_t k_module_fifo_limit = 1024u;

enum class EModulePipePost : std::uint8_t { published, full, invalid, not_ready };

class CModulePodPipe
{
public:
    explicit CModulePodPipe(const module_ids::id_type destination) noexcept : m_destination{ destination } {}

    [[nodiscard]] bool initialise(memory::CMemoryContext& context,
        const std::uint32_t capacity = k_module_pod_capacity) noexcept
    {
        if ((capacity < threading::transports::TRing<threading::CErasedPodMsg>::k_min_capacity) ||
            ((capacity & (capacity - 1u)) != 0u) || !module_ids::ops::is_valid_id(m_destination))
        {
            return false;
        }
        if (!m_ring.initialise(capacity, &context))
        {
            return false;
        }
        m_capacity = capacity;
        return true;
    }
    [[nodiscard]] bool admits(const threading::CErasedPodMsg& message) const noexcept
    {
        //  Host routing cannot interpret module-local payload identities.
        system_type_id type;
        return message.query_message_type_id().try_system_type_id(type) &&
            erased_transport_admission::classify(message.query_message_type_id(), m_destination).is_admitted();
    }
    [[nodiscard]] bool ready() const noexcept { return m_ring.is_ready(); }
    [[nodiscard]] EModulePipePost post(const threading::CErasedPodMsg& message) noexcept
    {
        if (!admits(message))
        {
            return EModulePipePost::invalid;
        }
        if (!m_ring.is_ready())
        {
            return EModulePipePost::not_ready;
        }
        return m_ring.post(message) ? EModulePipePost::published : EModulePipePost::full;
    }
    [[nodiscard]] bool read(threading::CErasedPodMsg& message) noexcept { return m_ring.read(message); }
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_ring.readable_count(); }
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_ring.writable_count(); }
    [[nodiscard]] std::uint32_t capacity() const noexcept { return m_capacity; }

private:
    const module_ids::id_type m_destination;
    threading::transports::TRing<threading::CErasedPodMsg> m_ring;
    std::uint32_t m_capacity{ 0u };
};

template<typename T>
class TModuleFifo
{
public:
    explicit TModuleFifo(memory::CMemoryContext& context,
        const std::uint32_t limit = k_module_fifo_limit) noexcept : m_fifo{ context }, m_limit{ limit } {}

    [[nodiscard]] bool ensure_free(const std::uint32_t count) noexcept
    {
        if (count > (m_limit - m_fifo.size())) return false;
        const std::size_t needed = m_fifo.size() + count;
        if (needed > m_fifo.capacity())
        {
            const std::size_t capacity = memory::vector_growth_policy(needed, m_limit);
            if (!m_fifo.reallocate(capacity)) return false;
        }
        return m_fifo.ensure_free(count);
    }
    [[nodiscard]] bool push(const T& value) noexcept { return (m_fifo.size() < m_limit) && m_fifo.push_back(value); }
    [[nodiscard]] bool pop(T& value) noexcept { return m_fifo.pop_front(value); }
    [[nodiscard]] std::uint32_t size() const noexcept { return static_cast<std::uint32_t>(m_fifo.size()); }
    [[nodiscard]] std::uint32_t capacity() const noexcept { return static_cast<std::uint32_t>(m_fifo.capacity()); }
    [[nodiscard]] std::uint32_t limit() const noexcept { return m_limit; }

private:
    TPodFifo<T> m_fifo;
    const std::uint32_t m_limit;
};

}   //  namespace host
