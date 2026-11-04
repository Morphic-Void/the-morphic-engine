
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window_record.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Defines the owned window record and each borrowed use's descriptor,
//  notification registration, readiness budget and retirement state.

#pragma once

#include "framework/platform/window.hpp"
#include "native/window.hpp"
#include "module/host_eligible_budget.hpp"

namespace platform::runtime
{

struct SWindowRecord
{
    [[nodiscard]] bool request_close() noexcept;
    [[nodiscard]] services::EServiceStatus answer_close(const windows::SAnswerClose& answer) noexcept;
    void close(const std::uint32_t failure = 0u) noexcept;
    void extent(const std::uint32_t width, const std::uint32_t height,
        const std::uint32_t dpi, const bool minimised) noexcept;

    windows::SWindowToken token;
    native::SWindow native;
    windows::SWindowSnapshot snapshot;
    windows::CWindowPublication publication;
    bool destruction_failed{ false };
};

struct SWindowUse
{
    explicit SWindowUse(const platform::threading::SAwakeClock clock) noexcept : budget{ clock } {}
    windows::SWindowViewV1 view;
    services::SDispatchedCommand acquisition;
    services::SServiceBorrow borrow;
    services::SNotificationRegistration registration;
    ::threading::SNotificationEndpoint notification;
    modules::CHostEligibleBudget budget;
    services::EServiceQuiescence quiescence{ services::EServiceQuiescence::none };
    std::int32_t completion{ -1 };
    std::uint64_t closing_awake{ 0u };
    bool ready{ false }, closing{ false }, retirement_started{ false };
    bool diagnosed{ false }, clock_failed{ false };
};

}   //  namespace platform::runtime
