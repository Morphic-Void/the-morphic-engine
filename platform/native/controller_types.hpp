
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controller_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Portable controller descriptions and complete acquisition observations.

#pragma once

#include "framework/platform/controllers.hpp"

namespace platform::native
{

struct SControllerInfo
{
    std::uint64_t device{}, clock_domain{ 1u }, clock_epoch{ 1u };
    std::uint16_t vendor{}, product{};
    bool eligible{ true };
    char name[64]{};
};
struct SControllerReading
{
    std::uint64_t ordinal{}, time_ns{};
    std::uint32_t buttons{};
    float left_x{}, left_y{}, right_x{}, right_y{}, left_trigger{}, right_trigger{};
    bool time_valid{ true };
};
enum class EControllerRead { empty, reading, history_lost, disconnected, failed };

}   //  namespace platform::native
