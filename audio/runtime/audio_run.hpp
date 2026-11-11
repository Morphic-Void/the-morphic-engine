
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_run.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Service-owned finite run, with device and clock seams for hardware-free tests.

#pragma once
#include "framework/communications/module_tests.hpp"
#include "framework/module/module_messages.hpp"
#include "tone_test.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "threading/CWaitPredicate.hpp"

namespace audio
{
class IAudioOutput
{
public:
    virtual ~IAudioOutput() noexcept = default;
    virtual modules::ETestRunStatus open() noexcept = 0;
    virtual bool start() noexcept = 0;
    //  Always quiesces callbacks, including after a partial open/start failure.
    virtual bool close() noexcept = 0;
    virtual SProgress observe() const noexcept = 0;
};

struct SRunClock
{
    void* context{ nullptr };
    platform::threading::WaitError (*set)(void* const, threading::CWaitDeadline&, std::uint64_t) noexcept;
    platform::threading::WaitError (*remaining)(void* const, const threading::CWaitDeadline&, std::uint64_t&) noexcept;
};

SRunClock native_run_clock() noexcept;

class CAudioRun
{
public:
    explicit CAudioRun(IAudioOutput& output, const SRunClock clock = native_run_clock()) noexcept : m_output{ output }, m_clock{ clock } {}
    ~CAudioRun() noexcept;
    CAudioRun(const CAudioRun&) = delete;
    CAudioRun& operator=(const CAudioRun&) = delete;
    void begin() noexcept;
    void check() noexcept;
    void cancel() noexcept;
    void wait_failed(const threading::WaitResult& result) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] modules::ETestRunStatus result() const noexcept { return m_result; }
    [[nodiscard]] const threading::CWaitDeadline& next_wait() const noexcept { return m_wait; }

private:
    bool set(threading::CWaitDeadline& deadline, const std::uint64_t milliseconds) noexcept;
    bool remaining(const threading::CWaitDeadline& deadline, std::uint64_t& value) noexcept;
    void finish(modules::ETestRunStatus status) noexcept;
    IAudioOutput& m_output;
    SRunClock m_clock;
    threading::CWaitDeadline m_overall, m_progress, m_check, m_wait;
    std::uint32_t m_frames{ 0u };
    bool m_active{ false };
    modules::ETestRunStatus m_result{ modules::ETestRunStatus::invalid_request };
};
}   //  namespace audio
