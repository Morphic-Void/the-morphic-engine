
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   native_thread_id.hpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Native thread identifier query.
//
//  CPlatformThreadId stores a native OS thread id value for the calling
//  thread. It is not a thread handle, engine thread id, persistent identity,
//  or array index.
//
//  Native ids may be reused after the corresponding thread exits. Failure or
//  unavailable platform support returns an invalid id.

#pragma once

#ifndef NATIVE_THREAD_ID_HPP_INCLUDED
#define NATIVE_THREAD_ID_HPP_INCLUDED

#include <cstdint>      //  std::uint64_t

namespace platform::threading
{

//==============================================================================
//  Native thread identifier
//==============================================================================

class CPlatformThreadId
{
public:

    constexpr CPlatformThreadId() noexcept = default;

    explicit constexpr CPlatformThreadId(const std::uint64_t value) noexcept : m_value(value) {}

    constexpr bool is_valid() const noexcept { return m_value != 0u; }

    constexpr std::uint64_t value() const noexcept { return m_value; }

    constexpr bool operator==(const CPlatformThreadId rhs) const noexcept { return m_value == rhs.m_value; }

    constexpr bool operator!=(const CPlatformThreadId rhs) const noexcept { return m_value != rhs.m_value; }

private:

    std::uint64_t m_value{ 0u };
};

//  Returns a best-effort native OS thread id for the calling thread.
CPlatformThreadId query_current_thread_id() noexcept;

}   //  namespace platform::threading

#endif  //  #ifndef NATIVE_THREAD_ID_HPP_INCLUDED
