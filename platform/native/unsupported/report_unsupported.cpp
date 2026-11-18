
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_unsupported.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Reports absence of a native discovery backend on this build.

#include "platform/platform_defines.hpp"
#if !MV_PLATFORM_WINDOWS
#include "native/report.hpp"

namespace platform::native
{

static EReportStep unsupported(SReportSession& session) noexcept
{
    session.failure = { reports::EStage::capture, reports::EReason::unsupported_backend };
    return EReportStep::failed;
}

const SReportAdapter& report_adapter() noexcept
{
    static constexpr SReportAdapter adapter{ &unsupported };
    return adapter;
}

}   //  namespace platform::native
#endif
