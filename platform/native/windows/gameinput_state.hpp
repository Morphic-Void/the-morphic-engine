
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    gameinput_state.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Stable GameInput callback state and movable, uniquely owned device records.

#pragma once

#include "native/controller_types.hpp"
#include "containers/TOrderedSlots.hpp"

#if MV_PLATFORM_WINDOWS
namespace GameInput::v4
{
struct IGameInput;
struct IGameInputDispatcher;
struct IGameInputDevice;
struct IGameInputReading;
}

namespace platform::native
{

struct SGameInput
{
    struct SDeviceKey
    {
        std::uint64_t value{};
        [[nodiscard]] std::int32_t relationship(const SDeviceKey& other) const noexcept
        {
            return (value < other.value) ? -1 : ((value > other.value) ? 1 : 0);
        }
    };
    struct SDevice
    {
        SDevice() noexcept = default;
        explicit SDevice(GameInput::v4::IGameInputDevice* const source) noexcept;
        SDevice(const SDevice&) = delete;
        SDevice& operator=(const SDevice&) = delete;
        SDevice(SDevice&& other) noexcept;
        SDevice& operator=(SDevice&& other) noexcept;
        ~SDevice() noexcept;

        GameInput::v4::IGameInputDevice* device{};
        GameInput::v4::IGameInputReading* cursor{};
        SControllerInfo info;
        std::uint64_t ordinal{};
        std::uint64_t fresh_after{}; //  Native microseconds in info.clock_epoch.
        bool freshness_pending{};
    };
    struct SEvent
    {
        GameInput::v4::IGameInputDevice* device{};
        bool connected{}, enumeration{};
    };
    SGameInput() noexcept = default;
    SGameInput(const SGameInput&) = delete;
    SGameInput& operator=(const SGameInput&) = delete;
    SGameInput(SGameInput&&) = delete;
    SGameInput& operator=(SGameInput&&) = delete;
    ~SGameInput() noexcept;

    static constexpr std::uint32_t k_events = 64u;
    GameInput::v4::IGameInput* input{};
    GameInput::v4::IGameInputDispatcher* dispatcher{};
    std::uint64_t callback{}, enumeration{};
    TOrderedSlots<SDevice, SDeviceKey> devices;
    SEvent events[k_events];
    std::uint32_t count{};
    std::uint64_t next_device{ 1u }, clock_epoch{ 1u }, last_clock{};
    std::uint64_t resume_time{};
    bool eligible{ true }, resumed{}, clock_reset{};
    bool overflow{}, rescan{}, ready{};
};

}   //  namespace platform::native
#endif
