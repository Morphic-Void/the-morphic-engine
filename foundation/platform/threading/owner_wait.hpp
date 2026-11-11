
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    owner_wait.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  One owner waits for coalesced producer signals and optional native activity.

#pragma once

#include <atomic>
#include <cstdint>
#include "platform/threading/wait_deadline.hpp"

namespace platform::threading
{

class COwnerWait;

//  Install before starting producers. The owner and native target must outlive
//  every signal call, including the final calls made after terminal publication.
struct SWakeEndpoint
{
    COwnerWait* target{ nullptr };
    [[nodiscard]] bool is_valid() const noexcept { return target != nullptr; }
    [[nodiscard]] bool signal() const noexcept;
};

enum class EOwnerWaitStatus : std::uint32_t
{
    activity, native_activity, timed_out, released, invalid_argument, unsupported, native_failure
};

struct SOwnerWaitResult
{
    EOwnerWaitStatus status{ EOwnerWaitStatus::invalid_argument };
    std::uint32_t native_error{ 0u };
};

class COwnerWait final
{
public:
    COwnerWait() noexcept = default;
    COwnerWait(const COwnerWait&) = delete;
    COwnerWait& operator=(const COwnerWait&) = delete;
    ~COwnerWait() noexcept;

    [[nodiscard]] WaitError initialise() noexcept;
    [[nodiscard]] SWakeEndpoint endpoint() noexcept { return { m_initialised ? this : nullptr }; }
    //  Owner only, before ALL durable checks and output retries. This is the
    //  sole acknowledgement: never call it between the final check and wait.
    [[nodiscard]] WaitError begin_cycle() noexcept;
    [[nodiscard]] SOwnerWaitResult wait(const CWaitDeadline& deadline, const bool native_events = false) noexcept;
    [[nodiscard]] WaitError error() const noexcept;
    //  Release interrupts the owner, but does not join producers or permit
    //  destruction. The endpoint remains alive until external quiescence.
    void release() noexcept;

private:
    friend struct SWakeEndpoint;
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SOwnerWaitTestAccess;
#endif
    [[nodiscard]] bool signal() noexcept;
    [[nodiscard]] bool wake_native() noexcept;
    static constexpr std::uint32_t k_pending = 1u, k_released = 2u;
    std::atomic<std::uint32_t> m_state{ 0u };
    std::atomic<std::uint32_t> m_failure{ 0u };
    void* m_native{ nullptr };
    bool m_initialised{ false };
#if defined(MV_HOST_RUNTIME_TESTS)
    void* m_test_context{ nullptr };
    void (*m_test_before_ack)(void*){ nullptr };
    void (*m_test_before_signal)(void*){ nullptr };
    void (*m_test_before_park)(void*){ nullptr };
#endif
};

inline bool SWakeEndpoint::signal() const noexcept
{
    return target && target->signal();
}

}   //  namespace platform::threading
