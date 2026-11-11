
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   thread_lifetime.hpp
//  Author: Ritchie Brannan
//  Date:   8 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Native joinable thread lifetime wrapper.
//
//  CThread owns the native join/release token for one started thread.
//  It is not a native thread identity object, engine thread id, TLS
//  context, name, priority, affinity, or job-system policy object.
//
//  The CThread object is used as the internal startup payload. It must
//  remain alive until the native join/release token has been consumed by
//  join_and_close() or close_handle().
//
//  close_handle() abandons join ownership only. It does not stop the
//  running thread.
//
//  Destroying a valid CThread releases/abandons join ownership. It does
//  not implicitly join.
//
//  CThread deliberately performs no debug reporting because it provides the
//  writer-thread lifetime used by the debug infrastructure. Failures are
//  observable through return values where supplied and are otherwise silent.

#pragma once

#ifndef THREAD_LIFETIME_HPP_INCLUDED
#define THREAD_LIFETIME_HPP_INCLUDED

#include <cstdint>      //  std::uint32_t, std::uint64_t

#include "platform/platform_defines.hpp"

namespace platform::threading
{

//==============================================================================
//  Native thread lifetime
//==============================================================================

using FThreadEntry = std::uint32_t(MV_STD_ABI_CALL*)(void* const user_data) noexcept;

class CThread
{
public:

    //  Deleted lifetime
    CThread(const CThread&) = delete;
    CThread& operator=(const CThread&) = delete;
    CThread(CThread&&) = delete;
    CThread& operator=(CThread&&) = delete;

    //  Lifetime
    CThread() noexcept;
    ~CThread() noexcept;

    //  Status
    bool is_valid() const noexcept;

    //  Thread startup
    //
    //  entry_point and user_data must both be non-null.
    //  stack_size_bytes == 0u selects the platform default.
    bool create(
        FThreadEntry const entry_point, void* const user_data,
        const std::uint32_t stack_size_bytes = 0u) noexcept;

    //  Thread shutdown
    bool join_and_close() noexcept;
    void close_handle() noexcept;

private:
    friend struct PlatformShimAccess;

    std::uint32_t entry_point_shim() noexcept;

    bool create_native_thread(const std::uint32_t stack_size_bytes) noexcept;

    void clear() noexcept;

    std::uint64_t m_native_token = 0u;

    FThreadEntry m_entry_point = nullptr;
    void* m_user_data = nullptr;

    //  Windows thread id returned by _beginthreadex.
    //  Used only for same-thread join avoidance.
    //  Unused on pthread platforms.
    std::uint32_t m_windows_thread_id = 0u;

    bool m_valid = false;
};

}   //  namespace platform::threading

#endif  //  #ifndef THREAD_LIFETIME_HPP_INCLUDED
