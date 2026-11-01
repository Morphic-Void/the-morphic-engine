
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    miniaudio_adapter.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Private output owner. Vendor declarations never cross this boundary.

#pragma once
#include "audio_run.hpp"
#include "memory/memory_context.hpp"

namespace audio
{
//  Private adapter failure-injection seam. Normal output supplies no probe.
//  Counts describe real acquisition/unwind operations, not simulated teardown.
struct SContextProbe
{
    unsigned fail_mutex{ 0u };
    unsigned mutex_attempts{ 0u };
    unsigned mutex_acquisitions{ 0u };
    unsigned mutex_releases{ 0u };
    unsigned api_attempts{ 0u };
    unsigned api_releases{ 0u };
    unsigned backend_acquisitions{ 0u };
    unsigned backend_releases{ 0u };
};

class CMiniaudioOutput final : public IAudioOutput
{
public:
    explicit CMiniaudioOutput(memory::CMemoryContext& memory) noexcept : m_memory{ memory } {}
    ~CMiniaudioOutput() noexcept override;
    ETestRunStatus open() noexcept override;
    bool start() noexcept override;
    bool close() noexcept override;
    SProgress observe() const noexcept override;
    CMiniaudioOutput(const CMiniaudioOutput&) = delete;
    CMiniaudioOutput& operator=(const CMiniaudioOutput&) = delete;
private:
    friend struct SMiniaudioTestAccess;
    ETestRunStatus open(bool cleanup_guard_only, SContextProbe* probe = nullptr) noexcept;
    struct SState;
    memory::CMemoryContext& m_memory;
    SState* m_state{ nullptr };
};
}   //  namespace audio
