
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CErasedMessageTransports.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Aug 26
//
//  Concrete transports for erased POD and owning thread messages.

#pragma once

#ifndef CERASED_MESSAGE_TRANSPORTS_HPP_INCLUDED
#define CERASED_MESSAGE_TRANSPORTS_HPP_INCLUDED

#include <cstdint>      //  std::uint32_t
#include <utility>      //  std::move

#include "framework/threading/messages/CErasedOwnerMsg.hpp"
#include "framework/threading/messages/CErasedPodMsg.hpp"
#include "foundation/threading/transports/TOwningTransport.hpp"
#include "foundation/threading/transports/TQueueTransport.hpp"

namespace threading::transports
{

class CErasedPodMsgTransport
{
public:
    CErasedPodMsgTransport() noexcept = default;
    explicit CErasedPodMsgTransport(const module_ids::id_type destination_module_id) noexcept;
    CErasedPodMsgTransport(const CErasedPodMsgTransport&) = delete;
    CErasedPodMsgTransport& operator=(const CErasedPodMsgTransport&) = delete;
    CErasedPodMsgTransport(CErasedPodMsgTransport&&) = delete;
    CErasedPodMsgTransport& operator=(CErasedPodMsgTransport&&) = delete;
    ~CErasedPodMsgTransport() noexcept = default;

    [[nodiscard]] bool posting_is_valid() const noexcept;
    [[nodiscard]] bool posting_is_ready() const noexcept;
    [[nodiscard]] bool posting_poisoned() const noexcept;
    [[nodiscard]] bool post(const threading::CErasedPodMsg& msg) noexcept;
    [[nodiscard]] bool post_would_reallocate(const std::uint32_t count) const noexcept;

    [[nodiscard]] bool reading_is_valid() const noexcept;
    [[nodiscard]] bool reading_is_ready() const noexcept;
    [[nodiscard]] bool read(threading::CErasedPodMsg& msg) noexcept;
    [[nodiscard]] std::uint32_t current_readable_count() const noexcept;
    [[nodiscard]] std::uint32_t refresh_readable_count() noexcept;

    [[nodiscard]] bool initialise_fixed(const std::uint32_t capacity, const bool allow_discard = false) noexcept;
    [[nodiscard]] bool initialise_growable(const std::uint32_t capacity, const std::uint32_t max_capacity = 0u) noexcept;
    void deallocate() noexcept { m_transport.deallocate(); }

    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept;
    [[nodiscard]] module_ids::id_type destination_module_id() const noexcept;

private:
    TQueue<threading::CErasedPodMsg> m_transport;
    module_ids::id_type m_destination_module_id{};
};

class CErasedOwnerMsgTransport
{
public:
    CErasedOwnerMsgTransport() noexcept = default;
    explicit CErasedOwnerMsgTransport(
        const module_ids::id_type destination_module_id,
        memory::CMemoryContext* const transport_context,
        memory::CMemoryContext* const recipient_context = nullptr) noexcept;
    CErasedOwnerMsgTransport(const CErasedOwnerMsgTransport&) = delete;
    CErasedOwnerMsgTransport& operator=(const CErasedOwnerMsgTransport&) = delete;
    CErasedOwnerMsgTransport(CErasedOwnerMsgTransport&&) = delete;
    CErasedOwnerMsgTransport& operator=(CErasedOwnerMsgTransport&&) = delete;
    ~CErasedOwnerMsgTransport() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return m_transport.is_ready(); }

    [[nodiscard]] bool posting_is_valid() const noexcept;
    [[nodiscard]] bool post(threading::CErasedOwnerMsg&& msg) noexcept;
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_transport.writable_count(); }

    [[nodiscard]] bool reading_is_valid() const noexcept;
    [[nodiscard]] bool read(threading::CErasedOwnerMsg& msg) noexcept;
    //  Restricted Host capture: empty destination, no ownership reattribution.
    [[nodiscard]] bool capture(threading::CErasedOwnerMsg& msg) noexcept;
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_transport.readable_count(); }

    [[nodiscard]] bool initialise(const std::uint32_t capacity) noexcept;
    void deallocate() noexcept { m_transport.deallocate(); }

    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept;
    [[nodiscard]] memory::CMemoryContext* recipient_memory_context() const noexcept;
    [[nodiscard]] module_ids::id_type destination_module_id() const noexcept;

private:
    [[nodiscard]] bool attribution_is_valid() const noexcept;

    TOwning<threading::CErasedOwnerMsg> m_transport;
    memory::CMemoryContext* const m_recipient_context{ nullptr };
    module_ids::id_type m_destination_module_id{};
};

class CErasedOwnerMsgProducerEndpoint
{
public:
    explicit CErasedOwnerMsgProducerEndpoint(CErasedOwnerMsgTransport& transport) noexcept : m_transport(transport) {}
    ~CErasedOwnerMsgProducerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_transport.posting_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_transport.is_ready(); }
    [[nodiscard]] bool post(threading::CErasedOwnerMsg&& msg) noexcept;
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_transport.writable_count(); }

private:
    CErasedOwnerMsgTransport& m_transport;
};

class CErasedOwnerMsgConsumerEndpoint
{
public:
    explicit CErasedOwnerMsgConsumerEndpoint(CErasedOwnerMsgTransport& transport) noexcept : m_transport(transport) {}
    ~CErasedOwnerMsgConsumerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_transport.reading_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_transport.is_ready(); }
    [[nodiscard]] bool read(threading::CErasedOwnerMsg& msg) noexcept { return m_transport.read(msg); }
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_transport.readable_count(); }

private:
    CErasedOwnerMsgTransport& m_transport;
};

//==============================================================================
//  CErasedPodMsgTransport out of class function bodies
//==============================================================================

inline CErasedPodMsgTransport::CErasedPodMsgTransport(const module_ids::id_type destination_module_id) noexcept
    : m_destination_module_id(destination_module_id)
{
}

inline bool CErasedPodMsgTransport::posting_is_valid() const noexcept
{
    return module_ids::ops::is_valid_id(m_destination_module_id) && m_transport.posting_is_valid();
}

inline bool CErasedPodMsgTransport::posting_is_ready() const noexcept
{
    return module_ids::ops::is_valid_id(m_destination_module_id) && m_transport.posting_is_ready();
}

inline bool CErasedPodMsgTransport::posting_poisoned() const noexcept
{
    return m_transport.posting_poisoned();
}

inline bool CErasedPodMsgTransport::post_would_reallocate(const std::uint32_t count) const noexcept
{
    return m_transport.post_would_reallocate(count);
}

inline bool CErasedPodMsgTransport::reading_is_valid() const noexcept
{
    return module_ids::ops::is_valid_id(m_destination_module_id) && m_transport.reading_is_valid();
}

inline bool CErasedPodMsgTransport::reading_is_ready() const noexcept
{
    return module_ids::ops::is_valid_id(m_destination_module_id) && m_transport.reading_is_ready();
}

inline bool CErasedPodMsgTransport::read(threading::CErasedPodMsg& msg) noexcept
{
    return m_transport.read(msg);
}

inline std::uint32_t CErasedPodMsgTransport::current_readable_count() const noexcept
{
    return m_transport.current_readable_count();
}

inline std::uint32_t CErasedPodMsgTransport::refresh_readable_count() noexcept
{
    return m_transport.refresh_readable_count();
}

inline bool CErasedPodMsgTransport::initialise_fixed(const std::uint32_t capacity, const bool allow_discard) noexcept
{
    return module_ids::ops::is_valid_id(m_destination_module_id) && m_transport.initialise_fixed(capacity, allow_discard);
}

inline bool CErasedPodMsgTransport::initialise_growable(const std::uint32_t capacity, const std::uint32_t max_capacity) noexcept
{
    return module_ids::ops::is_valid_id(m_destination_module_id) && m_transport.initialise_growable(capacity, max_capacity);
}

inline memory::CMemoryContext* CErasedPodMsgTransport::memory_context() const noexcept
{
    return m_transport.memory_context();
}

inline module_ids::id_type CErasedPodMsgTransport::destination_module_id() const noexcept
{
    return m_destination_module_id;
}

//==============================================================================
//  CErasedOwnerMsgTransport out of class function bodies
//==============================================================================

inline memory::CMemoryContext* CErasedOwnerMsgTransport::memory_context() const noexcept
{
    return m_transport.memory_context();
}

inline memory::CMemoryContext* CErasedOwnerMsgTransport::recipient_memory_context() const noexcept
{
    return m_recipient_context;
}

inline module_ids::id_type CErasedOwnerMsgTransport::destination_module_id() const noexcept
{
    return m_destination_module_id;
}

//==============================================================================
//  CErasedOwnerMsgProducerEndpoint out of class function bodies
//==============================================================================

inline bool CErasedOwnerMsgProducerEndpoint::post(threading::CErasedOwnerMsg&& msg) noexcept
{
    return m_transport.post(std::move(msg));
}

}   //  namespace threading::transports

#endif  //  #ifndef CERASED_MESSAGE_TRANSPORTS_HPP_INCLUDED
