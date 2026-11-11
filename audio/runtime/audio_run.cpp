
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_run.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Awake-time budgets persist through message wakeups and periodic checks.

#include "framework/communications/module_tests.hpp"
#include "framework/module/module_messages.hpp"
#include "audio_run.hpp"
#include "debug/macros.hpp"

namespace audio
{
SRunClock native_run_clock() noexcept
{
    return { nullptr,
        [](void* const, threading::CWaitDeadline& deadline, const std::uint64_t ms) noexcept { return deadline.set_after_milliseconds(ms); },
        [](void* const, const threading::CWaitDeadline& deadline, std::uint64_t& ns) noexcept { return deadline.remaining_nanoseconds(ns); } };
}

CAudioRun::~CAudioRun() noexcept
{
    if (m_active) cancel();
}

bool CAudioRun::set(threading::CWaitDeadline& deadline, const std::uint64_t milliseconds) noexcept
{
    const auto error = m_clock.set(m_clock.context, deadline, milliseconds);
    if (error.code == platform::threading::EWaitError::none)
    {
        return true;
    }
    MV_LOG(error, "Audio: Deadline construction failed ({}, native {})", static_cast<unsigned>(error.code), error.native_error);
    finish(modules::ETestRunStatus::failed);
    return false;
}

bool CAudioRun::remaining(const threading::CWaitDeadline& deadline, std::uint64_t& value) noexcept
{
    const auto error = m_clock.remaining(m_clock.context, deadline, value);
    if (error.code == platform::threading::EWaitError::none)
    {
        return true;
    }
    MV_LOG(error, "Audio: Deadline query failed ({}, native {})", static_cast<unsigned>(error.code), error.native_error);
    finish(modules::ETestRunStatus::failed);
    return false;
}

void CAudioRun::finish(modules::ETestRunStatus status) noexcept
{
    if (!m_output.close())
    {
        status = modules::ETestRunStatus::failed;
    }
    m_result = status;
    m_active = false;
}

void CAudioRun::begin() noexcept
{
    if (m_active) return;
    m_active = true;
    m_frames = 0u;
    if (!set(m_overall, 15000u)) return;
    const auto opened = m_output.open();
    std::uint64_t overall;
    if (!remaining(m_overall, overall)) return;
    if (overall == 0u)
    {
        MV_LOG(error, "Audio: Overall deadline expired during device preparation");
        finish(modules::ETestRunStatus::failed);
        return;
    }
    if (opened != modules::ETestRunStatus::success)
    {
        finish(opened);
        return;
    }
    if (!m_output.start())
    {
        finish(modules::ETestRunStatus::failed);
        return;
    }
    if (!set(m_progress, 2000u) || !set(m_check, 20u))
    {
        return;
    }
    check();
}

void CAudioRun::check() noexcept
{
    if (!m_active) return;
    std::uint64_t overall, progress, check_time;
    if (!remaining(m_overall, overall) || !remaining(m_progress, progress) || !remaining(m_check, check_time))
    {
        return;
    }

    //  Observe again after querying the clocks: a completed callback wins over
    //  expiry, but interruption still fails even if all frames were submitted.
    const auto observed = m_output.observe();
    if (observed.interrupted)
    {
        MV_LOG(error, "Audio: Output interrupted");
        finish(modules::ETestRunStatus::failed);
        return;
    }
    if (observed.complete)
    {
        finish(modules::ETestRunStatus::success);
        return;
    }
    if (overall == 0u)
    {
        MV_LOG(error, "Audio: Overall deadline expired");
        finish(modules::ETestRunStatus::failed);
        return;
    }
    if (observed.frames > m_frames)
    {
        m_frames = observed.frames;
        if (!set(m_progress, 2000u) || !remaining(m_progress, progress))
        {
            return;
        }
    }
    if (progress == 0u)
    {
        MV_LOG(error, "Audio: Callback progress deadline expired");
        finish(modules::ETestRunStatus::failed);
        return;
    }
    if (check_time == 0u)
    {
        if (!set(m_check, 20u) || !remaining(m_check, check_time))
        {
            return;
        }
    }
    //  Copy the original nearest deadline; never renew a budget to wait.
    m_wait = ((overall <= progress) && (overall <= check_time)) ? m_overall :
        ((progress <= check_time) ? m_progress : m_check);
}

void CAudioRun::cancel() noexcept
{
    if (m_active)
    {
        finish(modules::ETestRunStatus::cancelled);
    }
}

void CAudioRun::wait_failed(const threading::WaitResult& result) noexcept
{
    MV_LOG(error, "Audio: Epoch wait failed ({}, native {})", static_cast<unsigned>(result.status), result.native_error);
    if (m_active) finish(modules::ETestRunStatus::failed);
}
}   //  namespace audio
