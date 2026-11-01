
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    tone_test.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Finite callback-owned signal and lock-free publication to the service.

#pragma once

#include <atomic>
#include <cstdint>

namespace audio
{

struct SProgress
{
    std::uint32_t frames{ 0u };
    bool complete{ false };
    bool interrupted{ false };
};

class CToneTest
{
public:
    //  Only reset while all vendor callbacks are quiescent.
    [[nodiscard]] bool reset(std::uint32_t sample_rate) noexcept;
    void fill(float* output, std::uint32_t frames) noexcept;
    void cancel() noexcept { m_cancelled.store(1u, std::memory_order_release); }
    void interrupt() noexcept { m_interrupted.store(1u, std::memory_order_release); }
    [[nodiscard]] SProgress observe() const noexcept;

private:
    std::uint32_t m_rate{ 48000u };
    std::uint32_t m_cursor{ 0u };
    double m_phase{ 0.0 };
    std::atomic<std::uint32_t> m_frames{ 0u };
    std::atomic<std::uint32_t> m_complete{ 0u };
    std::atomic<std::uint32_t> m_interrupted{ 0u };
    std::atomic<std::uint32_t> m_cancelled{ 0u };
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Audio callback publication must be lock-free");

}   //  namespace audio
