
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture_types.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Shared value and explicitly borrowed payloads for the execution fixtures.

#pragma once

#include "framework/services/provider_messages.hpp"
#include <atomic>

namespace pump_tests
{

struct SModalData
{
    std::atomic<unsigned> ready{ 0u }, entered{ 0u }, leave{ 0u }, passes{ 0u }, pressure{ 0u };
};
struct SModalDescriptor { SModalData* state{ nullptr }; };
struct SNotificationData { std::atomic<unsigned> value{ 0u }; };
struct SNotificationReady { services::SServiceBorrow borrow; services::SNotificationRegistration registration; };
struct SSequenceProbe
{
    std::atomic<std::uint64_t>* counter{ nullptr };
    std::uint64_t sequence{ 0u };
};

}   // namespace pump_tests
