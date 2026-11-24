
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_execution.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Audio service lifecycle and generic test protocol.

#pragma once
#include "audio_run.hpp"
#include "framework/module/module_execution.hpp"

namespace audio
{
class CAudioExecution
{
public:
    CAudioExecution(const modules::SExecutionContext& context, IAudioOutput& output, const SRunClock clock = native_run_clock()) noexcept;
    [[nodiscard]] modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept;
    [[nodiscard]] bool complete() const noexcept;

private:
    void service() noexcept;
    void receive(const threading::CErasedPodMsg& message, const connections::SSource& source) noexcept;
    [[nodiscard]] bool reply(const connections::SToken token, const threading::CErasedPodMsg& message) noexcept;
    const modules::CCommunication m_communication;
    CAudioRun m_run;
    connections::SToken m_conversation, m_rejected_connection;
    threading::CErasedPodMsg m_rejected_reply;
    std::int32_t m_correlation{ -1 };
    bool m_pending{ false }, m_rejected{ false }, m_reported{ false };
    bool m_enabled{ false }, m_stopping{ false }, m_failed{ false };
};

[[nodiscard]] modules::SExecutionV1 audio_execution() noexcept;

}   //  namespace audio
