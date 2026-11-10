
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    GameInput_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    09 Nov 26
//
//  Windows device ownership, acquisition continuity and startup allocation rollback.

#include "tests/support/test_context.hpp"
#include "platform/native/controllers.hpp"
#include "platform/runtime/platform_runtime.hpp"

#if MV_PLATFORM_WINDOWS
#include <utility>
#include "platform/windows_include.hpp"
#include <GameInput.h>
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{
using namespace GameInput::v4;
using platform::native::SGameInput;
using tests::TTestContext;

//  Unused interface methods are inert, fully typed stubs so Win32 and x64 use
//  the real COM calling convention.
template<typename T>
struct TCountedInterface : T
{
    ULONG references{ 1u }, additions{}, releases{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** const result) noexcept override
    {
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { ++additions; return ++references; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { ++releases; return --references; }
};

struct SReading final : TCountedInterface<IGameInputReading>
{
    std::uint64_t timestamp{ 100u };
    GameInputGamepadState value{};
    GameInputKind STDMETHODCALLTYPE GetInputKind() noexcept override { return {}; }
    std::uint64_t STDMETHODCALLTYPE GetTimestamp() noexcept override { return timestamp; }
    void STDMETHODCALLTYPE GetDevice(IGameInputDevice** const) noexcept override { }
    std::uint32_t STDMETHODCALLTYPE GetControllerAxisCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerAxisState(const std::uint32_t, float* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerButtonCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerButtonState(const std::uint32_t, bool* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerSwitchCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerSwitchState(const std::uint32_t, GameInputSwitchPosition* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetKeyCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetKeyState(const std::uint32_t, GameInputKeyState* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetMouseState(GameInputMouseState* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetSensorsState(GameInputSensorsState* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetArcadeStickState(GameInputArcadeStickState* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetFlightStickState(GameInputFlightStickState* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetGamepadState(GameInputGamepadState* const result) noexcept override { *result = value; return true; }
    bool STDMETHODCALLTYPE GetRacingWheelState(GameInputRacingWheelState* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetRawReport(IGameInputRawDeviceReport** const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetSystemButtonState(GameInputSystemButtons* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetTouchContactCount(const std::uint32_t) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetTouchSurfaceState(const std::uint32_t, const std::uint32_t, GameInputTouchContact* const) noexcept override { return {}; }
};

struct SDevice final : TCountedInterface<IGameInputDevice>
{
    std::uint16_t STDMETHODCALLTYPE GetVendorId() noexcept override { return {}; }
    std::uint16_t STDMETHODCALLTYPE GetProductId() noexcept override { return {}; }
    std::uint16_t STDMETHODCALLTYPE GetRevisionNumber() noexcept override { return {}; }
    void STDMETHODCALLTYPE GetUsage(GameInputUsage* const) noexcept override { }
    void STDMETHODCALLTYPE GetHardwareVersion(GameInputVersion* const) noexcept override { }
    void STDMETHODCALLTYPE GetFirmwareVersion(GameInputVersion* const) noexcept override { }
    void STDMETHODCALLTYPE GetDeviceId(APP_LOCAL_DEVICE_ID* const) noexcept override { }
    void STDMETHODCALLTYPE GetDeviceRootId(APP_LOCAL_DEVICE_ID* const) noexcept override { }
    GameInputDeviceFamily STDMETHODCALLTYPE GetDeviceFamily() noexcept override { return {}; }
    GameInputKind STDMETHODCALLTYPE GetSupportedInput() noexcept override { return {}; }
    GameInputRumbleMotors STDMETHODCALLTYPE GetSupportedRumbleMotors() noexcept override { return {}; }
    GameInputSystemButtons STDMETHODCALLTYPE GetSupportedSystemButtons() noexcept override { return {}; }
    GameInputLightingGroups STDMETHODCALLTYPE GetSupportedLightingGroups() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetSupportedLightingGroupSize(const GameInputLightingGroups) noexcept override { return {}; }
    void STDMETHODCALLTYPE GetContainerId(GUID* const) noexcept override { }
    std::uint32_t STDMETHODCALLTYPE GetDisplayName(const std::uint32_t, char* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetPnpPath(const std::uint32_t, char* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetXInputDeviceIndex(std::uint32_t* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetKeyboardInfo(GameInputKeyboardInfo* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetMouseInfo(GameInputMouseInfo* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetSensorsInfo(GameInputSensorsInfo* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerAxisCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerAxisLabels(const std::uint32_t, GameInputLabel* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerButtonCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerButtonLabels(const std::uint32_t, GameInputLabel* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerSwitchCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetControllerSwitchInfo(const std::uint32_t, GameInputControllerSwitchInfo* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetArcadeStickInfo(GameInputArcadeStickInfo* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetFlightStickInfo(GameInputFlightStickInfo* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetGamepadInfo(GameInputGamepadInfo* const) noexcept override { return {}; }
    bool STDMETHODCALLTYPE GetRacingWheelInfo(GameInputRacingWheelInfo* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetForceFeedbackMotorCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetForceFeedbackMotorInfo(const std::uint32_t, GameInputForceFeedbackMotorInfo* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetInputReportCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetInputReportInfo(const std::uint32_t, GameInputRawDeviceReportInfo* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetOutputReportCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetOutputReportInfo(const std::uint32_t, GameInputRawDeviceReportInfo* const) noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetFeatureReportCount() noexcept override { return {}; }
    std::uint32_t STDMETHODCALLTYPE GetFeatureReportInfo(const std::uint32_t, GameInputRawDeviceReportInfo* const) noexcept override { return {}; }
    HRESULT STDMETHODCALLTYPE GetHapticInfo(GameInputHapticInfo* const) noexcept override { return E_NOTIMPL; }
    bool STDMETHODCALLTYPE IsConnected() noexcept override { return {}; }
    HRESULT STDMETHODCALLTYPE CreateForceFeedbackEffect(const std::uint32_t, const GameInputForceFeedbackParams* const, IGameInputForceFeedbackEffect** const) noexcept override { return E_NOTIMPL; }
    bool STDMETHODCALLTYPE IsForceFeedbackMotorPoweredOn(const std::uint32_t) noexcept override { return {}; }
    void STDMETHODCALLTYPE SetForceFeedbackMotorGain(const std::uint32_t, const float) noexcept override { }
    void STDMETHODCALLTYPE SetRumbleState(const GameInputRumbleParams* const) noexcept override { }
    HRESULT STDMETHODCALLTYPE DirectInputEscape(const std::uint32_t, const void* const, const std::uint32_t, void* const, const std::uint32_t, std::uint32_t* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateInputMapper(IGameInputMapper** const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetExtraAxisCount(const GameInputKind, std::uint32_t* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetExtraButtonCount(const GameInputKind, std::uint32_t* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetExtraAxisIndexes(const GameInputKind, const std::uint32_t, std::uint8_t* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetExtraButtonIndexes(const GameInputKind, const std::uint32_t, std::uint8_t* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateRawDeviceReport(const std::uint32_t, const GameInputRawDeviceReportKind, IGameInputRawDeviceReport** const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRawDeviceFeature(const std::uint32_t, IGameInputRawDeviceReport** const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetRawDeviceFeature(IGameInputRawDeviceReport* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SendRawDeviceOutput(IGameInputRawDeviceReport* const) noexcept override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE SetLightingColor(const GameInputColor, const GameInputLightingGroups, const std::uint32_t) noexcept override { }
    void STDMETHODCALLTYPE ApplyLightingColors() noexcept override { }
    void STDMETHODCALLTYPE ApplyLightingSystemDefault() noexcept override { }
    std::uint32_t STDMETHODCALLTYPE GetTouchSurfaceCount() noexcept override { return {}; }
    HRESULT STDMETHODCALLTYPE GetTouchSurfaceInfo(const std::uint32_t, GameInputTouchSurfaceInfo* const) noexcept override { return E_NOTIMPL; }
};

struct SInput final : TCountedInterface<IGameInput>
{
    IGameInputReading* current{}, *next{};
    HRESULT current_status{ S_OK }, next_status{ GAMEINPUT_E_REFERENCE_READING_TOO_OLD };
    std::uint32_t current_calls{}, next_calls{};
    std::uint64_t now{};
    std::uint64_t STDMETHODCALLTYPE GetCurrentTimestamp() noexcept override { return now; }
    HRESULT STDMETHODCALLTYPE GetCurrentReading(const GameInputKind, IGameInputDevice* const,
        IGameInputReading** const reading) noexcept override
    {
        ++current_calls;
        *reading = SUCCEEDED(current_status) ? current : nullptr;
        if (*reading) (*reading)->AddRef();
        return current_status;
    }
    HRESULT STDMETHODCALLTYPE GetNextReading(IGameInputReading* const, const GameInputKind,
        IGameInputDevice* const, IGameInputReading** const reading) noexcept override
    {
        ++next_calls;
        *reading = SUCCEEDED(next_status) ? next : nullptr;
        if (*reading) (*reading)->AddRef();
        return next_status;
    }
    HRESULT STDMETHODCALLTYPE GetPreviousReading(IGameInputReading* const, const GameInputKind,
        IGameInputDevice* const, IGameInputReading** const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterReadingCallback(IGameInputDevice* const, const GameInputKind, void* const,
        const GameInputReadingCallback, GameInputCallbackToken* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterDeviceCallback(IGameInputDevice* const, const GameInputKind, void* const,
        const GameInputDeviceCallback, GameInputCallbackToken* const) noexcept override { return E_NOTIMPL; }
    bool STDMETHODCALLTYPE UnregisterCallback(const GameInputCallbackToken) noexcept override { return false; }
    HRESULT STDMETHODCALLTYPE CreateDispatcher(IGameInputDispatcher** const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindDeviceFromId(const APP_LOCAL_DEVICE_ID* const, IGameInputDevice** const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindDeviceFromPlatformString(const LPCWSTR, IGameInputDevice** const) noexcept override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE EnableBackgroundInput(const bool) noexcept override { }
    HRESULT STDMETHODCALLTYPE CreateAggregateDevice(const GameInputKind, APP_LOCAL_DEVICE_ID* const) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DisableAggregateDevice(const APP_LOCAL_DEVICE_ID* const) noexcept override { return E_NOTIMPL; }
};

struct SDispatcher final : TCountedInterface<IGameInputDispatcher>
{
    bool STDMETHODCALLTYPE Dispatch(const std::uint64_t) noexcept override { return false; }
    HRESULT STDMETHODCALLTYPE OpenWaitHandle(HANDLE* const) noexcept override { return E_NOTIMPL; }
};

platform::threading::WaitError fixed_clock(void* const, std::uint64_t& value) noexcept
{
    value = 0u;
    return {};
}

void device_owner_moves(TTestContext& ctx)
{
    SDevice first, second;
    SReading first_reading, second_reading;
    {
        SGameInput::SDevice source{ &first };
        source.cursor = &first_reading; // Adopt the reading's initial reference.
        source.info.device = 7u;
        source.ordinal = 9u;
        source.fresh_after = 500u;
        source.freshness_pending = true;
        SGameInput::SDevice moved{ std::move(source) };
        TEST_EXPECT(ctx, (!source.device && !source.cursor && !source.info.device && !source.ordinal));
        TEST_EXPECT(ctx, ((moved.device == &first) && (moved.cursor == &first_reading) &&
            (moved.info.device == 7u) && (moved.ordinal == 9u)));
        TEST_EXPECT(ctx, (!source.fresh_after && !source.freshness_pending &&
            (moved.fresh_after == 500u) && moved.freshness_pending));
        TEST_EXPECT(ctx, ((first.references == 2u) && (first.additions == 1u) && !first.releases &&
            (first_reading.references == 1u) && !first_reading.additions && !first_reading.releases));
        SGameInput::SDevice destination{ &second };
        destination.cursor = &second_reading;
        destination = std::move(moved);
        TEST_EXPECT(ctx, (!moved.device && !moved.cursor && (second.references == 1u) &&
            (second.releases == 1u) && !second_reading.references && (second_reading.releases == 1u)));
        TEST_EXPECT(ctx, (!moved.fresh_after && !moved.freshness_pending &&
            (destination.fresh_after == 500u) && destination.freshness_pending));
        SGameInput::SDevice& self = destination;
        destination = std::move(self);
        TEST_EXPECT(ctx, ((destination.device == &first) && (destination.cursor == &first_reading) &&
            (first.references == 2u) && !first.releases && (first_reading.references == 1u)));
    }
    TEST_EXPECT(ctx, ((first.references == 1u) && (first.releases == 1u) &&
        !first_reading.references && (first_reading.releases == 1u)));
    TEST_EXPECT(ctx, ((second.releases == 1u) && (second_reading.releases == 1u)));
}

void device_slot_identity(TTestContext& ctx)
{
    SDevice first, second, third;
    SReading reading;
    {
        TInstance<SGameInput> state = TInstance<SGameInput>::create();
        TEST_EXPECT(ctx, (state && state->devices.initialise(platform::input::k_device_capacity)));
        if (!state || !state->devices.is_ready())
        {
            return;
        }
        SGameInput::SDevice initial{ &first };
        initial.info.device = 10u;
        initial.cursor = &reading;
        initial.ordinal = 27u;
        const std::int32_t old_slot = state->devices.insert(SGameInput::SDeviceKey{ 10u }, std::move(initial));
        TEST_EXPECT(ctx, ((old_slot >= 0) && !initial.device && !initial.cursor));
        SGameInput::SDevice later{ &second };
        later.info.device = 20u;
        TEST_EXPECT(ctx, (state->devices.insert(SGameInput::SDeviceKey{ 20u }, std::move(later)) >= 0));
        SGameInput::SDevice duplicate{ &third };
        TEST_EXPECT(ctx, (state->devices.insert(SGameInput::SDeviceKey{ 20u }, std::move(duplicate)) < 0));
        TEST_EXPECT(ctx, ((duplicate.device == &third) && (third.references == 2u) && !third.releases));
        TEST_EXPECT(ctx, state->devices.erase(SGameInput::SDeviceKey{ 10u }));
        TEST_EXPECT(ctx, !state->devices.erase(SGameInput::SDeviceKey{ 10u }));
        TEST_EXPECT(ctx, (!state->devices.get_slot(SGameInput::SDeviceKey{ 10u }) &&
            (first.references == 1u) && !reading.references && (reading.releases == 1u)));
        //  The same native object and storage slot can return with a fresh key.
        SGameInput::SDevice reconnected{ &first };
        reconnected.info.device = 30u;
        const std::int32_t reused = state->devices.insert(SGameInput::SDeviceKey{ 30u }, std::move(reconnected));
        TEST_EXPECT(ctx, (reused == old_slot));
        const SGameInput::SDevice* const current = state->devices.get_slot(SGameInput::SDeviceKey{ 30u });
        TEST_EXPECT(ctx, (current && (current->info.device == 30u) && !current->cursor && !current->ordinal));
        const std::int32_t oldest = state->devices.first_live();
        const std::int32_t newest = state->devices.next_live(oldest);
        const SGameInput::SDevice* const oldest_device = state->devices.get_slot(oldest);
        const SGameInput::SDevice* const newest_device = state->devices.get_slot(newest);
        TEST_EXPECT(ctx, (oldest_device && newest_device && (oldest_device->info.device == 20u) &&
            (newest_device->info.device == 30u) && (state->devices.next_live(newest) < 0)));
        TEST_EXPECT(ctx, state->devices.check_integrity());
    }
    TEST_EXPECT(ctx, ((first.references == 1u) && (first.releases == 2u) &&
        (second.references == 1u) && (second.releases == 1u) &&
        (third.references == 1u) && (third.releases == 1u)));
}

void queued_device_retirement(TTestContext& ctx)
{
    SDevice device;
    TInstance<SGameInput> state = TInstance<SGameInput>::create();
    TEST_EXPECT(ctx, bool(state));
    if (!state) return;
    device.AddRef();
    state->events[0] = { &device, true, false };
    state->count = 1u;
    state.reset();
    TEST_EXPECT(ctx, ((device.references == 1u) && (device.additions == 1u) && (device.releases == 1u)));
    state.reset();
    TEST_EXPECT(ctx, (device.releases == 1u));
}

void startup_allocation_failures(TTestContext& ctx)
{
    //  The shared object, slot metadata, values and keys are all allocated before
    //  creating GameInput. Each failure must leave no callback context or owner.
    constexpr std::size_t k_startup_allocations = 4u;
    for (std::size_t failure = 0u; failure < k_startup_allocations; ++failure)
    {
        tests::TAllocationFailureFixture allocations;
        allocations.fail_on = failure;
        memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator };
        const tests::TMemoryContextScope scope{ &memory };
        platform::native::SControllers source;
        const platform::native::SControllerAdapter& adapter = platform::native::controller_adapter();
        const bool initialised = adapter.initialise(source);
        TEST_EXPECT(ctx, (!initialised && allocations.failed && !source.state && !source.gameinput && !source.work_pending));
        TEST_EXPECT(ctx, (source.error == ERROR_NOT_ENOUGH_MEMORY));
        TEST_EXPECT(ctx, adapter.destroy(source));
        TEST_EXPECT(ctx, (memory.get_live_allocation_count() == 0u));
    }
}

void expired_history_continuity(TTestContext& ctx)
{
    using platform::native::EControllerRead;
    SInput input;
    SDevice device;
    SReading idle, pressed, recovered;
    {
        SGameInput state;
        input.AddRef();
        state.input = &input;
        TEST_EXPECT(ctx, state.devices.initialise(platform::input::k_device_capacity));
        SGameInput::SDevice record{ &device };
        idle.AddRef();
        record.cursor = &idle;
        record.info.device = 1u;
        record.ordinal = 12u;
        const std::int32_t slot = state.devices.insert(SGameInput::SDeviceKey{ 1u }, std::move(record));
        TEST_EXPECT(ctx, (slot >= 0));
        if (slot < 0) return;
        const SGameInput::SDevice* const stored = state.devices.get_slot(slot);
        platform::native::SControllers source;
        source.state = &state;
        const platform::native::SControllerAdapter& adapter = platform::native::controller_adapter();
        platform::native::SControllerReading result;
        input.current = &idle;
        for (unsigned attempt = 0u; attempt < 8u; ++attempt)
        {
            TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::empty));
        }
        TEST_EXPECT(ctx, ((input.current_calls == 8u) && (input.next_calls == 8u) &&
            (stored->cursor == &idle) && (stored->ordinal == 12u) && (idle.references == 2u) && (idle.releases == 8u)));
        //  Fresh input after idle still walks history and advances exactly once.
        input.next_status = S_OK;
        input.next = &pressed;
        pressed.value.buttons = GameInputGamepadA;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::reading));
        TEST_EXPECT(ctx, ((result.ordinal == 13u) && (result.buttons == 1u) && (result.time_ns == 100000u) &&
            (input.current_calls == 8u) && (stored->cursor == &pressed) && (idle.references == 1u)));
        //  Distinct readings with identical time and state do not prove continuity.
        recovered.value = pressed.value;
        input.next_status = GAMEINPUT_E_REFERENCE_READING_TOO_OLD;
        input.current = &recovered;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::history_lost));
        TEST_EXPECT(ctx, ((stored->cursor == &pressed) && (stored->ordinal == 13u) &&
            (recovered.references == 1u) && (recovered.releases == 1u)));
        TEST_EXPECT(ctx, (adapter.read(source, 1u, true, result) == EControllerRead::reading));
        TEST_EXPECT(ctx, ((stored->cursor == &recovered) && (result.ordinal == 14u) && (pressed.references == 1u)));
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::empty));
        TEST_EXPECT(ctx, ((stored->ordinal == 14u) && (recovered.references == 2u)));
        input.current_status = GAMEINPUT_E_DEVICE_DISCONNECTED;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::disconnected));
        input.current_status = E_FAIL;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::failed));
        input.current_status = GAMEINPUT_E_READING_NOT_FOUND;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::history_lost));
        input.current_status = S_OK;
        input.current = nullptr;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, false, result) == EControllerRead::failed));
    }
    TEST_EXPECT(ctx, ((input.references == 1u) && (device.references == 1u) &&
        (idle.references == 1u) && (pressed.references == 1u) && (recovered.references == 1u)));
}

void focus_freshness(TTestContext& ctx)
{
    using platform::native::EControllerRead;
    SInput input;
    SDispatcher dispatcher;
    SDevice first, second, late;
    SReading cached, fresh;
    {
        //  Only native catalogue callbacks are exercised: no routed borrowers
        //  or Host services are needed by this runtime's publication storage.
        modules::SHostPumpContext context{};
        platform::runtime::CPlatformRuntime runtime{ context };
        SGameInput state;
        input.AddRef();
        dispatcher.AddRef();
        state.input = &input;
        state.dispatcher = &dispatcher;
        TEST_EXPECT(ctx, state.devices.initialise(platform::input::k_device_capacity));
        for (IGameInputDevice* const device : { &first, &second })
        {
            SGameInput::SDevice record{ device };
            record.info.device = (device == &first) ? 1u : 2u;
            cached.AddRef();
            record.cursor = &cached;
            record.ordinal = 7u;
            const SGameInput::SDeviceKey key{ record.info.device };
            TEST_EXPECT(ctx, (state.devices.insert(key, std::move(record)) >= 0));
        }
        SGameInput::SDevice* const one = state.devices.get_slot(SGameInput::SDeviceKey{ 1u });
        SGameInput::SDevice* const two = state.devices.get_slot(SGameInput::SDeviceKey{ 2u });
        if (!one || !two)
        {
            return;
        }
        platform::native::SControllers source;
        source.state = &state;
        source.runtime = &runtime;
        source.clock = { nullptr, &fixed_clock };
        const platform::native::SControllerAdapter& adapter = platform::native::controller_adapter();
        platform::native::SControllerReading result;
        input.current = &cached;
        input.now = 200u;
        adapter.eligibility(source, false);
        adapter.eligibility(source, true);
        TEST_EXPECT(ctx, (one->freshness_pending && two->freshness_pending &&
            (one->fresh_after == 200u) && (two->fresh_after == 200u)));
        for (const std::uint64_t timestamp : { 199u, 200u })
        {
            cached.timestamp = timestamp;
            TEST_EXPECT(ctx, (adapter.read(source, 1u, true, result) == EControllerRead::empty));
        }
        TEST_EXPECT(ctx, ((one->cursor == &cached) && (one->ordinal == 7u) &&
            (cached.references == 3u) && (cached.releases == 2u) && !input.next_calls));
        fresh.timestamp = 201u;
        fresh.value.buttons = GameInputGamepadA;
        fresh.value.leftThumbstickX = 0.8f;
        input.current = &fresh;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, true, result) == EControllerRead::reading));
        TEST_EXPECT(ctx, (!one->freshness_pending && two->freshness_pending &&
            (result.buttons == 1u) && (result.left_x == 0.8f) && (result.ordinal == 8u)));
        input.current = &cached;
        TEST_EXPECT(ctx, (adapter.read(source, 2u, true, result) == EControllerRead::empty));
        TEST_EXPECT(ctx, (two->freshness_pending && (two->ordinal == 7u)));

        //  Another return supersedes both devices' cutoffs. Repeated eligibility
        //  notifications without a transition must not keep moving the cutoff.
        input.now = 300u;
        adapter.eligibility(source, false);
        adapter.eligibility(source, true);
        input.now = 400u;
        adapter.eligibility(source, true);
        TEST_EXPECT(ctx, (one->freshness_pending && two->freshness_pending &&
            (one->fresh_after == 300u) && (two->fresh_after == 300u)));

        late.AddRef();
        state.events[0] = { &late, true, false };
        state.count = 1u;
        state.next_device = 3u;
        adapter.dispatch(source, 1000u);
        const SGameInput::SDevice* const arrived = state.devices.get_slot(SGameInput::SDeviceKey{ 3u });
        TEST_EXPECT(ctx, (arrived && arrived->freshness_pending && (arrived->fresh_after == 300u)));
        input.current = &cached;
        TEST_EXPECT(ctx, (adapter.read(source, 3u, true, result) == EControllerRead::empty));
        fresh.timestamp = 301u;
        input.current = &fresh;
        TEST_EXPECT(ctx, (adapter.read(source, 3u, true, result) == EControllerRead::reading));

        //  A rollback observed at return remains visible even if the clock
        //  advances beyond its former value before the next dispatch.
        input.now = 50u;
        adapter.eligibility(source, false);
        adapter.eligibility(source, true);
        TEST_EXPECT(ctx, state.clock_reset);
        fresh.timestamp = 51u;
        input.current = &fresh;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, true, result) == EControllerRead::empty));
        input.now = 500u;
        adapter.dispatch(source, 1000u);
        TEST_EXPECT(ctx, (!state.clock_reset && (state.clock_epoch == 2u) &&
            (one->info.clock_epoch == 2u) && !one->cursor && !one->ordinal &&
            (one->fresh_after == 500u) && (two->fresh_after == 500u) && (state.resume_time == 500u)));
        fresh.timestamp = 500u;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, true, result) == EControllerRead::empty));
        fresh.timestamp = 501u;
        TEST_EXPECT(ctx, (adapter.read(source, 1u, true, result) == EControllerRead::reading));
        TEST_EXPECT(ctx, ((result.ordinal == 1u) && !one->freshness_pending && two->freshness_pending));
        //  Retirement while the second device still awaits freshness releases
        //  all retained ownership without needing any further physical input.
    }
    TEST_EXPECT(ctx, ((input.references == 1u) && (dispatcher.references == 1u) &&
        (first.references == 1u) && (second.references == 1u) && (late.references == 1u) &&
        (cached.references == 1u) && (fresh.references == 1u)));
}

}   //  namespace
#endif

void run_gameinput_ownership_tests(tests::TTestContext& ctx)
{
#if MV_PLATFORM_WINDOWS
    device_owner_moves(ctx);
    device_slot_identity(ctx);
    queued_device_retirement(ctx);
    startup_allocation_failures(ctx);
    expired_history_continuity(ctx);
    focus_freshness(ctx);
#else
    (void)ctx;
#endif
}
