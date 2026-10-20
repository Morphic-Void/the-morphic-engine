
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    message_helpers.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Typed construction and borrowed inspection; posting remains caller-owned.

#pragma once

#ifndef MESSAGE_HELPERS_HPP_INCLUDED
#define MESSAGE_HELPERS_HPP_INCLUDED

#include "threading/messages/CErasedOwnerMsg.hpp"
#include "threading/messages/CErasedPodMsg.hpp"

namespace threading
{

inline constexpr std::int32_t k_uncorrelated_slot{ -1 };

template<typename TMessage>
struct TMessagePayload
{
    using type = TMessage;
};

template<typename T>
[[nodiscard]] CErasedPodMsg make_pod_message(const std::int32_t slot, const T& payload) noexcept
{
    CErasedPodMsg message;
    message.set_async_slot(slot);
    message.assign_payload(payload);
    return message;
}

template<typename TMessage>
[[nodiscard]] CErasedOwnerMsg make_owner_message(const std::int32_t slot) noexcept
{
    CErasedOwnerMsg message;
    message.set_message_type<TMessage>();
    message.set_async_slot(slot);
    message.set_owner(CErasedOwner::create<typename TMessagePayload<TMessage>::type>());
    return message;
}

enum class EOwnerMessageState : std::uint8_t { present, empty, invalid };

template<typename TPayload>
struct TOwnerMessageInspection
{
    EOwnerMessageState state{ EOwnerMessageState::invalid };
    TPayload* payload{ nullptr };
};

template<typename TMessage>
[[nodiscard]] TOwnerMessageInspection<typename TMessagePayload<TMessage>::type>
inspect_owner_message(CErasedOwnerMsg& message) noexcept
{
    using TPayload = typename TMessagePayload<TMessage>::type;
    if (!message.is_message_a<TMessage>()) return {};
    if (!message.has_owner()) return { EOwnerMessageState::empty, nullptr };
    TPayload* const payload = message.owner().payload<TPayload>();
    return { (payload != nullptr) ? EOwnerMessageState::present : EOwnerMessageState::invalid, payload };
}

template<typename TMessage>
[[nodiscard]] TOwnerMessageInspection<const typename TMessagePayload<TMessage>::type>
inspect_owner_message(const CErasedOwnerMsg& message) noexcept
{
    using TPayload = typename TMessagePayload<TMessage>::type;
    if (!message.is_message_a<TMessage>()) return {};
    if (!message.has_owner()) return { EOwnerMessageState::empty, nullptr };
    const TPayload* const payload = message.owner().payload<TPayload>();
    return { (payload != nullptr) ? EOwnerMessageState::present : EOwnerMessageState::invalid, payload };
}

//  An inspection cannot outlive a temporary carrier.
template<typename TMessage> void inspect_owner_message(CErasedOwnerMsg&&) = delete;
template<typename TMessage> void inspect_owner_message(const CErasedOwnerMsg&&) = delete;

}   //  namespace threading

#endif  //  MESSAGE_HELPERS_HPP_INCLUDED
