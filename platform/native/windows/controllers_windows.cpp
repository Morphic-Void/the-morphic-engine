
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controllers_windows.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  GameInput v4 discovery and reading history, dispatched only by the Host pump.

#include "runtime/platform_runtime.hpp"

#if MV_PLATFORM_WINDOWS
#include <utility>
#include "platform/windows_include.hpp"
#include <GameInput.h>

namespace platform::native
{

using namespace GameInput::v4;

SGameInput::SDevice::SDevice(IGameInputDevice* const source) noexcept : device{ source }
{
    if (device) device->AddRef();
}

SGameInput::SDevice::SDevice(SDevice&& other) noexcept
{
    *this = std::move(other);
}

SGameInput::SDevice& SGameInput::SDevice::operator=(SDevice&& other) noexcept
{
    if (this != &other)
    {
        if (cursor) cursor->Release();
        if (device) device->Release();
        device = std::exchange(other.device, nullptr);
        cursor = std::exchange(other.cursor, nullptr);
        info = std::exchange(other.info, SControllerInfo{});
        ordinal = std::exchange(other.ordinal, 0u);
    }
    return *this;
}

SGameInput::SDevice::~SDevice() noexcept
{
    if (cursor) cursor->Release();
    if (device) device->Release();
}

SGameInput::~SGameInput() noexcept
{
    //  Callback removal precedes context destruction. Retain the dispatcher
    //  until then: dropping it first resumes GameInput's native workers.
    if (callback) (void)input->UnregisterCallback(callback);
    if (enumeration) (void)input->UnregisterCallback(enumeration);
    for (std::uint32_t index = 0u; index < count; ++index) events[index].device->Release();
    devices.deallocate();
    if (dispatcher) dispatcher->Release();
    if (input) input->Release();
}

static void CALLBACK device_callback(const GameInputCallbackToken token, void* const context,
    const GameInputDeviceEvent event, IGameInputDevice* const device)
{
    SGameInput& state = *static_cast<SGameInput*>(context);
    const bool enumeration = token == state.enumeration;
    if (enumeration && (event != GameInputDeviceEventConnected))
    {
        return;
    }
    if ((event != GameInputDeviceEventConnected) && (event != GameInputDeviceEventDisconnected))
    {
        return;
    }
    if (state.count == SGameInput::k_events)
    {
        state.overflow = true;
        return;
    }
    device->AddRef();
    state.events[state.count++] = { device, (event == GameInputDeviceEventConnected), enumeration };
}

static bool destroy(SControllers& source) noexcept
{
    source.gameinput.reset();
    source.state = nullptr;
    source.work_pending = false;
    return true;
}

static bool initialise(SControllers& source) noexcept
{
    if (source.state || source.gameinput)
    {
        source.error = ERROR_ALREADY_EXISTS;
        return false;
    }
    source.gameinput = TInstance<SGameInput>::create();
    if (!source.gameinput || !source.gameinput->devices.initialise(input::k_device_capacity))
    {
        source.error = ERROR_NOT_ENOUGH_MEMORY;
        (void)destroy(source);
        return false;
    }
    SGameInput* const state = &*source.gameinput;
    source.state = state;
    HRESULT status = GameInputCreate(&state->input);
    if (SUCCEEDED(status)) status = state->input->CreateDispatcher(&state->dispatcher);
    if (SUCCEEDED(status))
    {
        state->input->EnableBackgroundInput(true);
        status = state->input->RegisterDeviceCallback(nullptr, GameInputKindGamepad, state, &device_callback, &state->callback);
    }
    if (FAILED(status))
    {
        source.error = static_cast<std::uint32_t>(status);
        (void)destroy(source);
        return false;
    }
    source.work_pending = true;
    return true;
}

static void events(SControllers& source, SGameInput& state) noexcept
{
    for (std::uint32_t index = 0u; index < state.count; ++index)
    {
        const SGameInput::SEvent event = state.events[index];
        SGameInput::SDevice* found = nullptr;
        for (std::int32_t slot = state.devices.first_live(); slot >= 0; slot = state.devices.next_live(slot))
        {
            SGameInput::SDevice* const device = state.devices.get_slot(slot);
            if (device->device == event.device)
            {
                found = device;
                break;
            }
        }
        if (!event.connected)
        {
            if (found)
            {
                source.runtime->controller_departed(found->info.device);
                (void)state.devices.erase(SGameInput::SDeviceKey{ found->info.device });
            }
        }
        else if (!found && (!event.enumeration || event.device->IsConnected()))
        {
            if (state.devices.occupied_count() >= input::k_device_capacity)
            {
                source.runtime->controller_overflow();
                state.rescan = true;
            }
            else if (state.next_device == UINT64_MAX)
            {
                state.overflow = true;
            }
            else
            {
                SGameInput::SDevice device{ event.device };
                SControllerInfo& info = device.info;
                info.device = state.next_device++;
                info.clock_epoch = state.clock_epoch;
                info.vendor = event.device->GetVendorId();
                info.product = event.device->GetProductId();
                (void)event.device->GetDisplayName(sizeof(info.name), info.name);
                info.name[sizeof(info.name) - 1u] = 0;
                GameInputGamepadInfo capabilities{};
                info.eligible = event.device->GetGamepadInfo(&capabilities) &&
                    ((capabilities.supportedLayout & GameInputGamepadLayoutStandard) == GameInputGamepadLayoutStandard);
                const std::int32_t slot = state.devices.insert(SGameInput::SDeviceKey{ info.device }, std::move(device));
                if (slot < 0)
                {
                    source.error = ERROR_NOT_ENOUGH_MEMORY;
                    state.overflow = true;
                }
                else source.runtime->controller_arrived(state.devices.get_slot(slot)->info);
            }
        }
        event.device->Release();
    }
    state.count = 0u;
}

static void dispatch(SControllers& source, const std::uint64_t budget_ns) noexcept
{
    SGameInput* const state = static_cast<SGameInput*>(source.state);
    if (!state || !budget_ns || state->overflow)
    {
        return;
    }
    std::uint64_t start = 0u;
    if (source.clock.read(start).code != platform::threading::EWaitError::none)
    {
        source.runtime->controller_backend(input::EControllerBackend::failed, UINT32_MAX);
        return;
    }
    const std::uint64_t now_source = state->input->GetCurrentTimestamp();
    if (now_source < state->last_clock)
    {
        if (state->clock_epoch == UINT64_MAX) state->overflow = true;
        else
        {
            ++state->clock_epoch;
            for (std::int32_t slot = state->devices.first_live(); slot >= 0; slot = state->devices.next_live(slot))
            {
                SGameInput::SDevice& device = *state->devices.get_slot(slot);
                if (device.cursor) device.cursor->Release();
                device.cursor = nullptr;
                device.ordinal = 0u;
                device.info.clock_epoch = state->clock_epoch;
                source.runtime->controller_arrived(device.info);
            }
        }
    }
    state->last_clock = now_source;
    for (std::uint32_t work = 0u; work < 64u; ++work)
    {
        std::uint64_t awake = 0u;
        if ((source.clock.read(awake).code != platform::threading::EWaitError::none) || (awake < start))
        {
            state->overflow = true;
            break;
        }
        if ((awake - start) >= budget_ns) break;
        source.work_pending = state->dispatcher->Dispatch(0u);
        events(source, *state);
        if (state->overflow) break;
        if (!source.work_pending)
        {
            if (state->enumeration)
            {
                (void)state->input->UnregisterCallback(state->enumeration);
                state->enumeration = 0u;
            }
            if (!state->ready)
            {
                state->ready = true;
                source.runtime->controller_backend(input::EControllerBackend::ready);
            }
            break;
        }
    }
    if (state->overflow)
    {
        //  Lost discovery means lifetime continuity cannot be proven. Preserve
        //  the callback context for orderly destruction, but revoke all claims.
        source.work_pending = false;
        source.runtime->controller_backend(input::EControllerBackend::failed, (source.error ? source.error : UINT32_MAX));
    }
    if (state->rescan && !state->overflow)
    {
        const bool available = state->devices.occupied_count() < input::k_device_capacity;
        if (available && !state->enumeration)
        {
            //  Keep the authoritative listener registered throughout rescan.
            //  Snapshot enumeration can add missing devices, but cannot retire
            //  identities or hide disconnect/reconnect events on the main feed.
            const HRESULT status = state->input->RegisterDeviceCallback(nullptr, GameInputKindGamepad, state, &device_callback, &state->enumeration);
            state->rescan = false;
            if (FAILED(status)) source.runtime->controller_backend(input::EControllerBackend::failed, static_cast<std::uint32_t>(status));
            else source.work_pending = true;
        }
    }
}

static EControllerRead read(SControllers& source, const std::uint64_t instance, const bool baseline, SControllerReading& result) noexcept
{
    SGameInput* const state = static_cast<SGameInput*>(source.state);
    if (!state || state->overflow)
    {
        return EControllerRead::failed;
    }
    SGameInput::SDevice* const found = state->devices.get_slot(SGameInput::SDeviceKey{ instance });
    if (!found) return EControllerRead::disconnected;
    IGameInputReading* reading = nullptr;
    const HRESULT status = (baseline || !found->cursor) ?
        state->input->GetCurrentReading(GameInputKindGamepad, found->device, &reading) :
        state->input->GetNextReading(found->cursor, GameInputKindGamepad, found->device, &reading);
    if (status == GAMEINPUT_E_READING_NOT_FOUND)
    {
        return EControllerRead::empty;
    }
    if (status == GAMEINPUT_E_REFERENCE_READING_TOO_OLD)
    {
        return EControllerRead::history_lost;
    }
    if (status == GAMEINPUT_E_DEVICE_DISCONNECTED)
    {
        return EControllerRead::disconnected;
    }
    if (FAILED(status) || !reading)
    {
        return EControllerRead::failed;
    }
    GameInputGamepadState value{};
    const std::uint64_t microseconds = reading->GetTimestamp();
    if (!reading->GetGamepadState(&value) || (microseconds > (UINT64_MAX / 1000u)) || (found->ordinal == UINT64_MAX))
    {
        reading->Release();
        return EControllerRead::failed;
    }
    result = {};
    result.ordinal = ++found->ordinal;
    result.time_ns = microseconds * 1000u;
    constexpr GameInputGamepadButtons buttons[]{ GameInputGamepadA, GameInputGamepadB, GameInputGamepadX, GameInputGamepadY,
        GameInputGamepadLeftShoulder, GameInputGamepadRightShoulder, GameInputGamepadLeftThumbstick, GameInputGamepadRightThumbstick,
        GameInputGamepadDPadUp, GameInputGamepadDPadDown, GameInputGamepadDPadLeft, GameInputGamepadDPadRight,
        GameInputGamepadMenu, GameInputGamepadView };
    for (std::uint32_t index = 0u; index < 14u; ++index)
    {
        if (value.buttons & buttons[index]) result.buttons |= 1u << index;
    }
    result.left_x = value.leftThumbstickX;
    result.left_y = value.leftThumbstickY;
    result.right_x = value.rightThumbstickX;
    result.right_y = value.rightThumbstickY;
    result.left_trigger = value.leftTrigger;
    result.right_trigger = value.rightTrigger;
    if (found->cursor) found->cursor->Release();
    found->cursor = reading;
    return EControllerRead::reading;
}

const SControllerAdapter& controller_adapter() noexcept
{
    static const SControllerAdapter adapter{ &initialise, &dispatch, &read, &destroy };
    return adapter;
}

}   //  namespace platform::native
#endif
