
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CErasedOwnerMsg.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Aug 26
//
//  Move-only erased message carrying independently typed owned content.

#pragma once

#ifndef CERASED_OWNER_MSG_HPP_INCLUDED
#define CERASED_OWNER_MSG_HPP_INCLUDED

#include <cstdint>      //  std::int32_t
#include <type_traits>  //  move and destruction traits
#include <utility>      //  std::move

#include "foundation/system/erased_owner.hpp"
#include "foundation/system/type_registration.hpp"
#include "framework/threading/messages/SErasedMsgHeader.hpp"

namespace threading
{

class alignas(16) CErasedOwnerMsg
{
public:
    CErasedOwnerMsg() noexcept = default;
    CErasedOwnerMsg(const CErasedOwnerMsg&) = delete;
    CErasedOwnerMsg& operator=(const CErasedOwnerMsg&) = delete;
    CErasedOwnerMsg(CErasedOwnerMsg&& other) noexcept;
    CErasedOwnerMsg& operator=(CErasedOwnerMsg&& other) noexcept;
    ~CErasedOwnerMsg() noexcept = default;

    [[nodiscard]] std::int32_t query_async_slot() const noexcept;
    void set_async_slot(const std::int32_t async_slot) noexcept;

    [[nodiscard]] bool has_message_type() const noexcept;
    [[nodiscard]] type_id query_message_type_id() const noexcept;

    template<typename T>
    [[nodiscard]] bool is_message_a() const noexcept;

    template<typename T>
    void set_message_type() noexcept;

    [[nodiscard]] bool has_owner() const noexcept;
    [[nodiscard]] type_id query_owner_type_id() const noexcept;
    [[nodiscard]] CErasedOwner& owner() noexcept;
    [[nodiscard]] const CErasedOwner& owner() const noexcept;
    void set_owner(CErasedOwner&& owner) noexcept;
    [[nodiscard]] CErasedOwner take_owner() noexcept;

private:
    template<typename T>
    static constexpr void validate_message_type() noexcept;

    void make_canonical_empty() noexcept;

    SErasedMsgHeader m_header{};
    CErasedOwner m_owner;
};

static_assert(std::is_nothrow_default_constructible_v<CErasedOwnerMsg>);
static_assert(std::is_nothrow_move_constructible_v<CErasedOwnerMsg>);
static_assert(std::is_nothrow_move_assignable_v<CErasedOwnerMsg>);
static_assert(std::is_nothrow_destructible_v<CErasedOwnerMsg>);
static_assert((sizeof(CErasedOwnerMsg) == 48u), "CErasedOwnerMsg must occupy 48 bytes on supported targets.");
static_assert((alignof(CErasedOwnerMsg) == 16u), "CErasedOwnerMsg must retain 16-byte alignment.");

//==============================================================================
//  CErasedOwnerMsg out of class function bodies
//==============================================================================

inline CErasedOwnerMsg::CErasedOwnerMsg(CErasedOwnerMsg&& other) noexcept
    : m_header(other.m_header)
    , m_owner(std::move(other.m_owner))
{
    other.make_canonical_empty();
}

inline CErasedOwnerMsg& CErasedOwnerMsg::operator=(CErasedOwnerMsg&& other) noexcept
{
    if (this != &other)
    {
        m_header = other.m_header;
        m_owner = std::move(other.m_owner);
        other.make_canonical_empty();
    }
    return *this;
}

inline std::int32_t CErasedOwnerMsg::query_async_slot() const noexcept
{
    return m_header.async_slot;
}

inline void CErasedOwnerMsg::set_async_slot(const std::int32_t async_slot) noexcept
{
    m_header.async_slot = async_slot;
}

inline bool CErasedOwnerMsg::has_message_type() const noexcept
{
    return m_header.message_type_id.is_valid();
}

inline type_id CErasedOwnerMsg::query_message_type_id() const noexcept
{
    return m_header.message_type_id;
}

template<typename T>
inline bool CErasedOwnerMsg::is_message_a() const noexcept
{
    validate_message_type<T>();
    return m_header.message_type_id == k_type_id_v<T>;
}

template<typename T>
inline void CErasedOwnerMsg::set_message_type() noexcept
{
    validate_message_type<T>();
    m_header.message_type_id = k_type_id_v<T>;
}

inline bool CErasedOwnerMsg::has_owner() const noexcept
{
    return m_owner.is_ready();
}

inline type_id CErasedOwnerMsg::query_owner_type_id() const noexcept
{
    return m_owner.query_type_id();
}

inline CErasedOwner& CErasedOwnerMsg::owner() noexcept
{
    return m_owner;
}

inline const CErasedOwner& CErasedOwnerMsg::owner() const noexcept
{
    return m_owner;
}

inline void CErasedOwnerMsg::set_owner(CErasedOwner&& owner) noexcept
{
    m_owner = std::move(owner);
}

inline CErasedOwner CErasedOwnerMsg::take_owner() noexcept
{
    return std::move(m_owner);
}

template<typename T>
constexpr void CErasedOwnerMsg::validate_message_type() noexcept
{
    static_assert(k_type_id_v<T>.is_valid(),
        "CErasedOwnerMsg requires a valid, non-zero message type id.");
}

inline void CErasedOwnerMsg::make_canonical_empty() noexcept
{
    m_header = SErasedMsgHeader{};
}

}   //  namespace threading

#endif  //  #ifndef CERASED_OWNER_MSG_HPP_INCLUDED
