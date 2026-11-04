
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window_record.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Implements window close-request sequencing, close decisions and conversion
//  of native pixel extents into the published logical window state.

#include "runtime/window_record.hpp"

namespace platform::runtime
{

bool SWindowRecord::request_close() noexcept
{
    if (snapshot.phase != windows::EWindowPhase::open) return false;
    if (snapshot.close_request == UINT64_MAX)
    {
        close(UINT32_MAX);
        return true;
    }
    ++snapshot.close_request;
    snapshot.phase = windows::EWindowPhase::close_pending;
    return true;
}

services::EServiceStatus SWindowRecord::answer_close(const windows::SAnswerClose& answer) noexcept
{
    if ((answer.accept > 1u) || answer.reserved) return services::EServiceStatus::invalid_request;
    if (snapshot.phase == windows::EWindowPhase::closing) return services::EServiceStatus::closing;
    if ((snapshot.phase != windows::EWindowPhase::close_pending) || !answer.close_request ||
        (answer.close_request != snapshot.close_request)) return services::EServiceStatus::stale;
    snapshot.phase = answer.accept ? windows::EWindowPhase::closing : windows::EWindowPhase::open;
    return services::EServiceStatus::success;
}

void SWindowRecord::close(const std::uint32_t failure) noexcept
{
    snapshot.phase = windows::EWindowPhase::closing;
    if (failure && !snapshot.failure) snapshot.failure = failure;
}

void SWindowRecord::extent(const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t dpi, const bool minimised) noexcept
{
    snapshot.pixel_width = minimised ? 0u : width;
    snapshot.pixel_height = minimised ? 0u : height;
    snapshot.dpi = dpi ? dpi : 96u;
    snapshot.logical_width = static_cast<std::uint32_t>(((static_cast<std::uint64_t>(snapshot.pixel_width) * 96u) + (snapshot.dpi / 2u)) / snapshot.dpi);
    snapshot.logical_height = static_cast<std::uint32_t>(((static_cast<std::uint64_t>(snapshot.pixel_height) * 96u) + (snapshot.dpi / 2u)) / snapshot.dpi);
    snapshot.minimised = minimised ? 1u : 0u;
}

}   //  namespace platform::runtime
