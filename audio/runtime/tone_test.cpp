
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    tone_test.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Sample-count envelope; every requested output frame is written.

#include "tone_test.hpp"
#include <cmath>

namespace audio
{

bool CToneTest::reset(const std::uint32_t sample_rate) noexcept
{
    if ((sample_rate < 4u) || (sample_rate > 768000u)) return false;
    m_rate = sample_rate;
    m_cursor = 0u;
    m_phase = 0.0;
    m_frames.store(0u, std::memory_order_relaxed);
    m_complete.store(0u, std::memory_order_relaxed);
    m_interrupted.store(0u, std::memory_order_relaxed);
    m_cancelled.store(0u, std::memory_order_relaxed);
    return true;
}

void CToneTest::fill(float* output, const std::uint32_t frames) noexcept
{
    static constexpr double tau = 6.283185307179586476925286766559;
    const double step = tau * 261.6255653005986 / m_rate;
    const std::uint32_t end = m_rate * 5u;
    const std::uint32_t padded_end = end + (m_rate + 3u) / 4u;
    const bool cancelled = m_cancelled.load(std::memory_order_acquire) != 0u;
    for (std::uint32_t i = 0u; i < frames; ++i)
    {
        float sample = 0.0f;
        if (!cancelled && (m_cursor < end))
        {
            double gain = 0.1;
            if (m_cursor < m_rate * 2u) gain *= static_cast<double>(m_cursor) / (m_rate * 2u - 1u);
            else if (m_cursor >= m_rate * 3u) gain *= static_cast<double>(end - 1u - m_cursor) / (m_rate * 2u - 1u);
            sample = static_cast<float>(std::sin(m_phase) * gain);
            m_phase += step;
            if (m_phase >= tau) m_phase -= tau;
        }
        *output++ = sample;
        *output++ = sample;
        if (!cancelled && (m_cursor < padded_end)) ++m_cursor;
    }
    m_frames.store(m_cursor, std::memory_order_release);
    if (m_cursor == padded_end) m_complete.store(1u, std::memory_order_release);
}

SProgress CToneTest::observe() const noexcept
{
    //  Completion publishes all preceding frame stores.
    const bool complete = m_complete.load(std::memory_order_acquire) != 0u;
    return { m_frames.load(std::memory_order_acquire), complete,
        m_interrupted.load(std::memory_order_acquire) != 0u };
}

}   //  namespace audio
