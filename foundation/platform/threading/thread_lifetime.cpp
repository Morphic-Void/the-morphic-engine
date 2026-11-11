
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   thread_lifetime.cpp
//  Author: Ritchie Brannan
//  Date:   8 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Native joinable thread lifetime implementation.

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t, std::uint64_t, std::uintptr_t
#include <cstring>      //  std::memcpy

#include "platform/threading/thread_lifetime.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#include <process.h>
#endif

#if MV_PLATFORM_HAS_PTHREADS
#include <pthread.h>
#endif

namespace platform::threading
{

//==============================================================================
//  Native token packing
//==============================================================================

template <typename T>
static std::uint64_t pack_native_token(const T& token) noexcept
{
    static_assert((sizeof(T) <= sizeof(std::uint64_t)), "Native thread token is too large.");

    std::uint64_t packed = 0u;
    std::memcpy(&packed, &token, sizeof(T));
    return packed;
}

template <typename T>
static T unpack_native_token(const std::uint64_t packed) noexcept
{
    static_assert((sizeof(T) <= sizeof(std::uint64_t)), "Native thread token is too large.");

    T token;
    std::memcpy(&token, &packed, sizeof(T));
    return token;
}

//==============================================================================
//  Platform entry point shims
//==============================================================================

struct PlatformShimAccess
{
#if MV_PLATFORM_WINDOWS

    static unsigned __stdcall platform_entry_point_shim(void* const arg) noexcept
    {
        CThread* const thread = static_cast<CThread*>(arg);
        if (thread != nullptr)
        {
            return static_cast<unsigned>(thread->entry_point_shim());
        }
        return 0u;
    }

#elif MV_PLATFORM_HAS_PTHREADS

    static void* platform_entry_point_shim(void* const arg) noexcept
    {
        CThread* const thread = static_cast<CThread*>(arg);
        if (thread != nullptr)
        {
            const std::uint32_t result = thread->entry_point_shim();
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(result));
        }
        return nullptr;
    }

#endif
};

//==============================================================================
//  Construction and destruction
//==============================================================================

CThread::CThread() noexcept
{
    clear();
}

CThread::~CThread() noexcept
{
    close_handle();
}

//==============================================================================
//  Status
//==============================================================================

bool CThread::is_valid() const noexcept
{
    return m_valid;
}

//==============================================================================
//  Operations
//==============================================================================

bool CThread::create(
    FThreadEntry const entry_point, void* const user_data,
    const std::uint32_t stack_size_bytes) noexcept
{
    if (!is_valid())
    {
        if ((entry_point != nullptr) && (user_data != nullptr))
        {
            clear();
            m_entry_point = entry_point;
            m_user_data = user_data;
            const bool created = create_native_thread(stack_size_bytes);
            if (created)
            {
                return true;
            }
            clear();
        }
    }
    return false;
}

bool CThread::join_and_close() noexcept
{
    if (is_valid())
    {
#if MV_PLATFORM_WINDOWS
        const std::uint32_t current_thread_id = static_cast<std::uint32_t>(::GetCurrentThreadId());
        if (m_windows_thread_id != current_thread_id)
        {
            const HANDLE handle = unpack_native_token<HANDLE>(m_native_token);
            const DWORD wait_result = ::WaitForSingleObject(handle, INFINITE);
            if (wait_result == WAIT_OBJECT_0)
            {
                (void)::CloseHandle(handle);
                clear();
                return true;
            }
        }
#elif MV_PLATFORM_HAS_PTHREADS
        const pthread_t native_thread = unpack_native_token<pthread_t>(m_native_token);
        if (::pthread_equal(::pthread_self(), native_thread) == 0)
        {
            const int join_result = ::pthread_join(native_thread, nullptr);
            if (join_result == 0)
            {
                clear();
                return true;
            }
        }
#endif
    }
    return false;
}

void CThread::close_handle() noexcept
{
    if (is_valid())
    {
#if MV_PLATFORM_WINDOWS
        const HANDLE handle = unpack_native_token<HANDLE>(m_native_token);
        (void)::CloseHandle(handle);
#elif MV_PLATFORM_HAS_PTHREADS
        const pthread_t native_thread = unpack_native_token<pthread_t>(m_native_token);
        (void)::pthread_detach(native_thread);
#endif
    }
    clear();
}

//==============================================================================
//  Entry
//==============================================================================

std::uint32_t CThread::entry_point_shim() noexcept
{
    if ((m_entry_point != nullptr) && (m_user_data != nullptr))
    {
        return m_entry_point(m_user_data);
    }
    return 0u;
}

//==============================================================================
//  Native thread implementation
//==============================================================================

bool CThread::create_native_thread(const std::uint32_t stack_size_bytes) noexcept
{
#if MV_PLATFORM_WINDOWS

    static_assert((sizeof(HANDLE) <= sizeof(std::uint64_t)), "CThread native token is too small for HANDLE.");

    unsigned int windows_thread_id = 0u;

    const std::uintptr_t raw_handle = ::_beginthreadex(
        nullptr, static_cast<unsigned int>(stack_size_bytes),
        &PlatformShimAccess::platform_entry_point_shim, this, 0u, &windows_thread_id);

    if (raw_handle != 0u)
    {
        const HANDLE handle = reinterpret_cast<HANDLE>(raw_handle);
        m_native_token = pack_native_token(handle);
        m_windows_thread_id = static_cast<std::uint32_t>(windows_thread_id);
        m_valid = true;
        return true;
    }

#elif MV_PLATFORM_HAS_PTHREADS

    static_assert((sizeof(pthread_t) <= sizeof(std::uint64_t)), "CThread native token is too small for pthread_t.");

    pthread_attr_t attr;
    pthread_attr_t* attr_ptr = nullptr;

    if (stack_size_bytes != 0u)
    {
        const int attr_result = ::pthread_attr_init(&attr);

        if (attr_result != 0)
        {
            return false;
        }

        const int stack_result = ::pthread_attr_setstacksize(
            &attr, static_cast<std::size_t>(stack_size_bytes));

        if (stack_result != 0)
        {
            (void)::pthread_attr_destroy(&attr);
            return false;
        }

        attr_ptr = &attr;
    }

    pthread_t native_thread;

    const int create_result = ::pthread_create(
        &native_thread, attr_ptr, &PlatformShimAccess::platform_entry_point_shim, this);

    if (attr_ptr != nullptr)
    {
        (void)::pthread_attr_destroy(&attr);
    }

    if (create_result == 0)
    {
        m_native_token = pack_native_token(native_thread);
        m_windows_thread_id = 0u;
        m_valid = true;
        return true;
    }

#else

    (void)stack_size_bytes;

#endif

    return false;
}

//==============================================================================
//  Storage
//==============================================================================

void CThread::clear() noexcept
{
    m_native_token = 0u;
    m_entry_point = nullptr;
    m_user_data = nullptr;
    m_windows_thread_id = 0u;
    m_valid = false;
}

#if !MV_PLATFORM_WINDOWS && !MV_PLATFORM_HAS_PTHREADS

#error "platform::threading::CThread is not implemented for this platform."

#endif

}   //  namespace platform::threading
