
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CErasedPodMsg.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 May 2026
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Fixed-size POD typeless message for thread communication.
//
//  CErasedPodMsg is a non-owning, trivially-copyable message container
//  intended for queue-based communication between engine threads.
//
//  Concrete message payloads are identified by their component type registry
//  and copied into and out of private inline storage. No typed pointer into
//  the message payload is exposed.
//
//  The message payload is shaped as six 64-bit cells. This supports common
//  thread-message forms such as task slot references, generation counters,
//  flags, result codes, small handles, and non-owning pointer-sized values.
//
//  IMPORTANT SEMANTIC NOTE
//  -----------------------
//  CErasedPodMsg does not own any memory referenced by values stored in its
//  payload. Pointer values carried by a message are views only. Their validity
//  must be guaranteed by the surrounding thread, host, or provisioning
//  protocol.
//
//  Ownership transfer must use an explicit owning transport or host-owned
//  storage path, not CErasedPodMsg.
//
//  A LOCAL payload may only be admitted to a concrete transport whose source
//  and destination are the same binary component.

#pragma once

#ifndef CERASED_POD_MSG_HPP_INCLUDED
#define CERASED_POD_MSG_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint64_t, std::uintptr_t
#include <cstring>      //  std::memcpy, std::memset
#include <type_traits>  //  std::is_standard_layout_v, std::is_trivially_copyable_v

#include "foundation/system/type_registration.hpp"
#include "framework/threading/messages/SErasedMsgHeader.hpp"

namespace threading
{

//==============================================================================
//  CErasedPodMsg
//  Fixed-size non-owning POD typeless thread message
//==============================================================================

class alignas(16) CErasedPodMsg
{
private:
    static constexpr std::size_t k_payload_size = sizeof(std::uint64_t) * 6u;
    static constexpr std::size_t k_payload_align = 16u;

    struct alignas(16) SStorage
    {
        SErasedMsgHeader header{};
        alignas(k_payload_align) unsigned char payload[k_payload_size]{};
    };

    static_assert((offsetof(SStorage, header) == 0u), "CErasedPodMsg header must be first.");
    static_assert((offsetof(SStorage, payload) == 16u), "CErasedPodMsg payload must follow the header.");
    static_assert((sizeof(SStorage) == 64u), "CErasedPodMsg storage must occupy exactly 64 bytes.");

public:
    [[nodiscard]] std::int32_t query_async_slot() const noexcept;

    void set_async_slot(const std::int32_t async_slot) noexcept;

    [[nodiscard]] bool has_message_type() const noexcept;

    [[nodiscard]] type_id query_message_type_id() const noexcept;

    template<typename T>
    static constexpr bool is_payload_compatible_with() noexcept;

    template<typename T>
    [[nodiscard]] bool is_payload_a() const noexcept;

    template<typename T>
    void assign_payload(const T& value) noexcept;

    template<typename T>
    [[nodiscard]] bool copy_payload_to(T& out) const noexcept;

private:
    template<typename T>
    static constexpr void validate_payload_type() noexcept;

    SStorage m_storage;
};

//==============================================================================
//  CErasedPodMsg out of class function bodies
//==============================================================================

inline std::int32_t CErasedPodMsg::query_async_slot() const noexcept
{
    return m_storage.header.async_slot;
}

inline void CErasedPodMsg::set_async_slot(const std::int32_t async_slot) noexcept
{
    m_storage.header.async_slot = async_slot;
}

inline bool CErasedPodMsg::has_message_type() const noexcept
{
    return m_storage.header.message_type_id.is_valid();
}

inline type_id CErasedPodMsg::query_message_type_id() const noexcept
{
    return m_storage.header.message_type_id;
}

template<typename T>
constexpr bool CErasedPodMsg::is_payload_compatible_with() noexcept
{
    return std::is_trivially_copyable_v<T>
        && std::is_standard_layout_v<T>
        && (sizeof(T) <= k_payload_size)
        && (alignof(T) <= k_payload_align);
}

template<typename T>
inline bool CErasedPodMsg::is_payload_a() const noexcept
{
    validate_payload_type<T>();
    return m_storage.header.message_type_id == k_type_id_v<T>;
}

template<typename T>
inline void CErasedPodMsg::assign_payload(const T& value) noexcept
{
    validate_payload_type<T>();

    std::memset(m_storage.payload, 0, k_payload_size);
    std::memcpy(m_storage.payload, &value, sizeof(T));
    m_storage.header.message_type_id = k_type_id_v<T>;
}

template<typename T>
inline bool CErasedPodMsg::copy_payload_to(T& out) const noexcept
{
    validate_payload_type<T>();

    if (m_storage.header.message_type_id != k_type_id_v<T>)
    {
        return false;
    }
    std::memcpy(&out, m_storage.payload, sizeof(T));
    return true;
}

template<typename T>
constexpr void CErasedPodMsg::validate_payload_type() noexcept
{
    static_assert(k_type_id_v<T>.is_valid(),
        "CErasedPodMsg requires a valid, non-zero payload type id.");
    static_assert(is_payload_compatible_with<T>(),
        "CErasedPodMsg requires a trivially copyable, standard-layout payload that fits its fixed 48-byte, 16-byte-aligned storage.");
}

static_assert((sizeof(std::uintptr_t) <= sizeof(std::uint64_t)), "CErasedPodMsg requires pointer-sized values to fit in std::uint64_t.");
static_assert(std::is_trivially_copyable_v<CErasedPodMsg>, "CErasedPodMsg must remain trivially copyable for transport.");
static_assert(std::is_standard_layout_v<CErasedPodMsg>, "CErasedPodMsg must remain standard layout.");
static_assert((alignof(CErasedPodMsg) == 16u), "CErasedPodMsg must retain 16-byte alignment.");
static_assert((sizeof(CErasedPodMsg) == 64u), "CErasedPodMsg must retain its fixed 64-byte transport size.");

}   //  namespace threading

#endif  //  #ifndef CERASED_POD_MSG_HPP_INCLUDED
