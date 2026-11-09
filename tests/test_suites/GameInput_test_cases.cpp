
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    GameInput_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    09 Nov 26
//
//  Windows device-owner moves, keyed slot reuse and startup allocation rollback.

#include "tests/support/test_context.hpp"
#include "platform/native/controllers.hpp"

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

//  Only reference ownership is exercised. The remaining interface methods are
//  inert, fully typed stubs so Win32 and x64 use the real COM calling convention.
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
    GameInputKind STDMETHODCALLTYPE GetInputKind() noexcept override { return {}; }
    std::uint64_t STDMETHODCALLTYPE GetTimestamp() noexcept override { return {}; }
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
    bool STDMETHODCALLTYPE GetGamepadState(GameInputGamepadState* const) noexcept override { return {}; }
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

void device_owner_moves(TTestContext& ctx)
{
    SDevice first, second;
    SReading first_reading, second_reading;
    {
        SGameInput::SDevice source{ &first };
        source.cursor = &first_reading; // Adopt the reading's initial reference.
        source.info.device = 7u;
        source.ordinal = 9u;
        SGameInput::SDevice moved{ std::move(source) };
        TEST_EXPECT(ctx, (!source.device && !source.cursor && !source.info.device && !source.ordinal));
        TEST_EXPECT(ctx, ((moved.device == &first) && (moved.cursor == &first_reading) &&
            (moved.info.device == 7u) && (moved.ordinal == 9u)));
        TEST_EXPECT(ctx, ((first.references == 2u) && (first.additions == 1u) && !first.releases &&
            (first_reading.references == 1u) && !first_reading.additions && !first_reading.releases));
        SGameInput::SDevice destination{ &second };
        destination.cursor = &second_reading;
        destination = std::move(moved);
        TEST_EXPECT(ctx, (!moved.device && !moved.cursor && (second.references == 1u) &&
            (second.releases == 1u) && !second_reading.references && (second_reading.releases == 1u)));
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

}   //  namespace
#endif

void run_gameinput_ownership_tests(tests::TTestContext& ctx)
{
#if MV_PLATFORM_WINDOWS
    device_owner_moves(ctx);
    device_slot_identity(ctx);
    queued_device_retirement(ctx);
    startup_allocation_failures(ctx);
#else
    (void)ctx;
#endif
}
