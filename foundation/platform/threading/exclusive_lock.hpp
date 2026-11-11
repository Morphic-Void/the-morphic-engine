
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   exclusive_lock.hpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  In-process, exclusive, non-recursive native lock.
//
//  CExclusiveLock owns a native lock object stored in opaque inline storage.
//  Construction attempts to initialise the native lock; is_valid() reports
//  whether the lock is usable.
//
//  The lock is non-recursive. Recursive acquisition is caller misuse and may
//  block in the underlying native primitive.
//
//  Use of an invalid lock, release by a non-owner, destruction while owned,
//  and destruction while waiters have been observed fail without calling
//  the underlying native primitive where practical.
//
//  CExclusiveLock deliberately performs no debug reporting because it is a
//  synchronization primitive used by the debug infrastructure. Contract and
//  native-operation failures therefore have no diagnostic side effect here.
//
//  Concurrent destruction and use is caller misuse.
// 
//  The destructor can detect some active-use cases, but this does not make
//  object lifetime synchronization safe under misuse.

#pragma once

#ifndef EXCLUSIVE_LOCK_HPP_INCLUDED
#define EXCLUSIVE_LOCK_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t, std::uint32_t

#include "platform/threading/native_thread_id.hpp"

namespace platform::threading
{

//==============================================================================
//  Native exclusive lock
//==============================================================================

class CExclusiveLock
{
public:

    //  Deleted lifetime
    CExclusiveLock(const CExclusiveLock&) = delete;
    CExclusiveLock& operator=(const CExclusiveLock&) = delete;
    CExclusiveLock(CExclusiveLock&&) = delete;
    CExclusiveLock& operator=(CExclusiveLock&&) = delete;

    //  Constructor and destructor
    CExclusiveLock() noexcept;
    ~CExclusiveLock() noexcept;

    //  Status
    bool is_valid() const noexcept;

    //  Operations
    void acquire() noexcept;
    [[nodiscard]] bool try_acquire() noexcept;
    void release() noexcept;

private:

    bool initialise_native_lock() noexcept;
    bool destroy_native_lock() noexcept;
    bool acquire_native_lock() noexcept;
    bool try_acquire_native_lock() noexcept;
    bool release_native_lock() noexcept;

    bool try_acquire_owner_thread_id_gate() noexcept;
    void release_owner_thread_id_gate() noexcept;

    void clear() noexcept;

    static constexpr std::size_t k_opaque_size = 64u;
    static constexpr std::size_t k_opaque_alignment = 16u;

    alignas(k_opaque_alignment) std::uint8_t m_opaque[k_opaque_size] = { 0u };

    std::atomic<std::uint32_t> m_owner_thread_id_gate{ 0u };
    CPlatformThreadId m_owner_thread_id;

    std::atomic<std::uint32_t> m_waiter_count{ 0u };

    bool m_valid = false;
};

}   //  namespace platform::threading

#endif  //  #ifndef EXCLUSIVE_LOCK_HPP_INCLUDED
