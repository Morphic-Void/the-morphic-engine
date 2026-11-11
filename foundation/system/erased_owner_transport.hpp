
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner_transport.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  Attribution-aware SPSC transport for CErasedOwner.

#pragma once

#ifndef ERASED_OWNER_TRANSPORT_HPP_INCLUDED
#define ERASED_OWNER_TRANSPORT_HPP_INCLUDED

#include <cstdint>      //  std::uint32_t
#include <utility>      //  std::move

#include "system/erased_owner.hpp"
#include "threading/transports/TOwningTransport.hpp"

namespace threading::transports
{

class CErasedOwnerTransport
{
public:
    CErasedOwnerTransport() noexcept = default;
    explicit CErasedOwnerTransport(
        const module_ids::id_type destination_module_id,
        memory::CMemoryContext* const transport_context,
        memory::CMemoryContext* const recipient_context = nullptr) noexcept;
    CErasedOwnerTransport(const CErasedOwnerTransport&) = delete;
    CErasedOwnerTransport& operator=(const CErasedOwnerTransport&) = delete;
    CErasedOwnerTransport(CErasedOwnerTransport&&) = delete;
    CErasedOwnerTransport& operator=(CErasedOwnerTransport&&) = delete;
    ~CErasedOwnerTransport() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return m_transport.is_ready(); }

    [[nodiscard]] bool posting_is_valid() const noexcept;
    [[nodiscard]] bool post(CErasedOwner&& owner) noexcept;
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_transport.writable_count(); }

    [[nodiscard]] bool reading_is_valid() const noexcept;
    [[nodiscard]] bool read(CErasedOwner& owner) noexcept;
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_transport.readable_count(); }

    [[nodiscard]] bool initialise(const std::uint32_t capacity) noexcept;
    void deallocate() noexcept { m_transport.deallocate(); }

    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept;
    [[nodiscard]] memory::CMemoryContext* recipient_memory_context() const noexcept;
    [[nodiscard]] module_ids::id_type destination_module_id() const noexcept;

private:
    [[nodiscard]] bool attribution_is_valid() const noexcept;

    TOwning<CErasedOwner> m_transport;
    memory::CMemoryContext* const m_recipient_context{ nullptr };
    module_ids::id_type m_destination_module_id{};
};

class CErasedOwnerProducerEndpoint
{
public:
    explicit CErasedOwnerProducerEndpoint(CErasedOwnerTransport& transport) noexcept : m_transport(transport) {}
    ~CErasedOwnerProducerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_transport.posting_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_transport.is_ready(); }
    [[nodiscard]] bool post(CErasedOwner&& owner) noexcept { return m_transport.post(std::move(owner)); }
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_transport.writable_count(); }

private:
    CErasedOwnerTransport& m_transport;
};

class CErasedOwnerConsumerEndpoint
{
public:
    explicit CErasedOwnerConsumerEndpoint(CErasedOwnerTransport& transport) noexcept : m_transport(transport) {}
    ~CErasedOwnerConsumerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_transport.reading_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_transport.is_ready(); }
    [[nodiscard]] bool read(CErasedOwner& owner) noexcept { return m_transport.read(owner); }
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_transport.readable_count(); }

private:
    CErasedOwnerTransport& m_transport;
};

//==============================================================================
//  CErasedOwnerTransport out of class function bodies
//==============================================================================

inline memory::CMemoryContext* CErasedOwnerTransport::memory_context() const noexcept
{
    return m_transport.memory_context();
}

inline memory::CMemoryContext* CErasedOwnerTransport::recipient_memory_context() const noexcept
{
    return m_recipient_context;
}

inline module_ids::id_type CErasedOwnerTransport::destination_module_id() const noexcept
{
    return m_destination_module_id;
}

}   //  namespace threading::transports

#endif  //  ERASED_OWNER_TRANSPORT_HPP_INCLUDED
