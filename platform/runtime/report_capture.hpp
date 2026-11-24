
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_capture.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Single owner-pump capture with a retained outcome and exact receipt retirement.

#pragma once

#include "native/report.hpp"
#include "framework/platform/report_memory.hpp"
#include "framework/module/module_execution.hpp"
#include "framework/threading/messages/CErasedOwnerMsg.hpp"

namespace platform::runtime
{

class CReportCapture
{
public:
    CReportCapture(const modules::SExecutionContext& context, const platform::threading::SAwakeClock clock,
        const native::SReportAdapter& adapter) noexcept : m_instance{ context.instance }, m_execution{ context.execution },
        m_communication{ *context.communication }, m_clock{ clock }, m_adapter{ adapter }, m_budget{ clock } {}
    void message(const ::threading::CErasedPodMsg& message) noexcept;
    void observe_pump(const std::uint64_t start) noexcept;
    void progress(const bool stopping, const std::uint64_t share_ns) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_phase != EPhase::idle; }
    [[nodiscard]] bool runnable() const noexcept
    {
        return active() && (m_phase != EPhase::receipt) && !m_output_wait;
    }

private:
    enum class EPhase { idle, start, native, schema, post, receipt };
    void fail(const reports::SFailure failure) noexcept;
    bool eligible(const bool stopping) noexcept;
    void build() noexcept;
    [[nodiscard]] modules::SModuleInstance instance() const noexcept
    {
        return m_instance;
    }
    [[nodiscard]] const modules::SHostExecutionContext* execution() const noexcept
    {
        return m_execution;
    }
    const modules::SModuleInstance m_instance;
    const modules::SHostExecutionContext* const m_execution{ nullptr };
    const modules::CCommunication m_communication;
    const platform::threading::SAwakeClock m_clock;
    const native::SReportAdapter& m_adapter;
    modules::CHostEligibleBudget m_budget;
    reports::SCaptureIdentity m_identity;
    TInstance<reports::CReportMemory> m_memory;
    TInstance<native::SReportSession> m_session;
    ::threading::CErasedOwnerMsg m_envelope;
    reports::SFailure m_failure;
    std::uint64_t m_last_id{}, m_begin{}, m_max_call{}, m_schema_ns{}, m_max_pump{};
    std::uint32_t m_attempts{}, m_calls{}, m_raw_modes{};
    EPhase m_phase{ EPhase::idle };
    bool m_output_wait{ false };
};

}   //  namespace platform::runtime
