
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Private report query adapter and one-attempt staging. Each step issues at most one native query.

#pragma once

#include "runtime/report_candidate.hpp"
#include "framework/platform/report_messages.hpp"
#include "platform/threading/awake_clock.hpp"
#include "containers/TInstance.hpp"

#if MV_PLATFORM_WINDOWS
#include "native/windows/report_state.hpp"
#endif

namespace platform::native
{

enum class EReportStep { more, finished, topology_changed, failed };

struct SReportSession
{
    runtime::SReportCandidate candidate;
#if MV_PLATFORM_WINDOWS
    TInstance<SWindowsReport> windows;
#endif
    reports::SFailure failure;
    platform::threading::SAwakeClock clock;
    modules::CHostEligibleBudget* budget{};
    std::uint32_t native_calls{}, raw_modes{};
    std::uint64_t maximum_call_ns{}, slice_end_ns{};
};

struct SReportAdapter
{
    EReportStep (*step)(SReportSession&) noexcept;
};

[[nodiscard]] const SReportAdapter& report_adapter() noexcept;

}   //  namespace platform::native
