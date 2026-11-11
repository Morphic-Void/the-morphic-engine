
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SErasedMsgHeader.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Aug 26
//
//  Shared coordination header for erased thread messages.

#pragma once

#ifndef SERASED_MSG_HEADER_HPP_INCLUDED
#define SERASED_MSG_HEADER_HPP_INCLUDED

#include <cstddef>      //  offsetof
#include <cstdint>      //  std::int32_t, std::uint64_t
#include <type_traits>  //  std::is_standard_layout_v, std::is_trivially_copyable_v

#include "foundation/system/system_ids.hpp"

namespace threading
{

struct alignas(16) SErasedMsgHeader
{
    type_id message_type_id{ type_ids::undefined };
    std::int32_t async_slot{ 0 };
    std::uint64_t reserved{ 0u };
};

static_assert((offsetof(SErasedMsgHeader, message_type_id) == 0u), "Erased message type id must be the first header field.");
static_assert((offsetof(SErasedMsgHeader, async_slot) == 4u), "Erased message async slot must follow the type id.");
static_assert((offsetof(SErasedMsgHeader, reserved) == 8u), "Erased message reserved field must complete the 16-byte header.");
static_assert((sizeof(SErasedMsgHeader) == 16u), "Erased message header must occupy exactly 16 bytes.");
static_assert((alignof(SErasedMsgHeader) == 16u), "Erased message header must retain 16-byte alignment.");
static_assert(std::is_trivially_copyable_v<SErasedMsgHeader>, "Erased message header must remain trivially copyable.");
static_assert(std::is_standard_layout_v<SErasedMsgHeader>, "Erased message header must remain standard layout.");

}   //  namespace threading

#endif  //  #ifndef SERASED_MSG_HEADER_HPP_INCLUDED
