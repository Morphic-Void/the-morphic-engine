
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Platform input commands, typed records and immutable borrowed reader views.

#pragma once

#include "input_state.hpp"
#include "window.hpp"
#include "threading/transports/TRingTransport.hpp"
#include "platform/threading/owner_wait.hpp"
#include "assets/asset_repository.hpp"

namespace platform::input
{

inline constexpr services::SServiceContract k_input_contract{ 2u, 4u };
inline constexpr std::uint32_t k_action_capacity = 256u, k_text_capacity = 64u;
enum class EConnection : std::uint32_t { desktop = 1u, controller = 2u };
enum class EClock : std::uint32_t { awake_nanoseconds = 1u, source_nanoseconds = 2u };
enum class ECommand : std::uint16_t
{
    install_configuration = 1u, retire_configuration = 2u,
    acquire_desktop = 32u, acquire_controller = 33u, input_ready = 34u,
    boundary_ready = 35u, select_context = 38u, enable_text = 39u, disable_text = 40u,
    set_capture = 41u, release_capture = 42u, acquire_catalogue = 46u, catalogue_ready = 47u,
    enumerate_device = 48u, assign_device = 49u, device_metadata = 50u
};
struct SConnectionToken { std::uint64_t value{ 0u }; };
struct SInstallationToken { std::uint64_t value{ 0u }; };
struct SInstallConfiguration
{
    CAssetId source_asset;
    std::uint32_t format_version{ 1u }, reserved{};
};
struct SRetireConfiguration { SInstallationToken installation; };
enum class EConfigurationError : std::uint16_t
{
    none = 0u, schema, layout, version, limit, duplicate, reference, value, combination, allocation
};
struct SInstallationResult
{
    SInstallationToken installation;
    EConfigurationError error{ EConfigurationError::none };
    std::uint16_t category{};
    std::uint32_t definition{}, field{}, reserved{};
};
static_assert(sizeof(SInstallationResult) == 24u);
struct SAcquireDesktop
{
    windows::SWindowToken window;
    SInstallationToken installation;
    std::uint32_t profile{ 0u }, context{ 0u };
};
struct SAcquireController
{
    SInstallationToken installation;
    std::uint64_t device{ 0u }; //  Zero selects the first eligible unclaimed device.
    std::uint32_t profile{ 0u }, context{ 0u };
};
struct SInputReady
{
    SConnectionToken connection;
    services::SNotificationRegistration registration;
    services::SServiceRequestId acquisition;
};
struct SBoundaryReady
{
    SConnectionToken connection;
    std::uint64_t action_generation{ 0u }, text_generation{ 0u };
};
struct SSelectContext
{
    SConnectionToken connection;
    std::uint32_t context{ 0u }, reserved{ 0u };
};
struct STextTarget
{
    SConnectionToken connection;
    std::uint64_t transition{ 0u }, target{ 0u }; //  Zero disables text.
};

struct SSetCapture
{
    SConnectionToken connection;
    std::uint64_t transition{}, action_generation{};
};
struct SReleaseCapture
{
    SConnectionToken connection;
    std::uint64_t transition{}, grant{};
};

enum class EActionKind : std::uint16_t
{
    digital = 0u, scalar = 1u, vector = 2u, delta = 3u, pulse = 4u,
    pointer = 5u, button = 6u, wheel = 7u, modifiers = 8u, boundary = 9u
};
enum class ETextKind : std::uint16_t { scalar, boundary };
//  Contract 4 uses these payloads with the unchanged V1 record framing. Unused
//  payload bytes are zero. Cancellation is an inactive/zero state, never a pulse.
inline constexpr std::uint16_t k_action_cancelled = 1u, k_no_source_time = 2u;
struct SDigitalAction { std::uint32_t active{}; };
struct SScalarAction { float value{}; };
struct SVectorAction { float x{}, y{}; };
struct SDeltaAction { float x{}, y{}; };
struct SPulseAction { std::uint32_t binding{}; };
static_assert((sizeof(SDigitalAction) == 4u) && (sizeof(SScalarAction) == 4u) &&
    (sizeof(SVectorAction) == 8u) && (sizeof(SDeltaAction) == 8u) && (sizeof(SPulseAction) == 4u));
//  Modifier bits: left/right Shift, Ctrl, Alt, Meta, then AltGr. Button bits
//  follow the five portable mouse-button controls. Unknown bits are not neutral.
inline constexpr std::uint16_t k_modifier_mask = 0x01ffu;
inline constexpr std::uint8_t k_button_mask = 0x1fu;
struct SDesktopState
{
    std::uint16_t modifiers{}, modifiers_known{};
    std::uint8_t buttons{}, buttons_known{};
    std::uint16_t reserved{};
};
struct SPointerInput { float x{}, y{}; SDesktopState state; };
struct SButtonInput { float x{}, y{}; SDesktopState state; };
struct SWheelInput { float x{}, y{}, horizontal{}, vertical{}; SDesktopState state; };
struct SModifierInput { SDesktopState state; };
static_assert((sizeof(SDesktopState) == 8u) && (sizeof(SWheelInput) == 24u));
static_assert((sizeof(SSetCapture) == 24u) && (sizeof(SReleaseCapture) == 24u));

struct SActionBoundary
{
    std::uint64_t generation{ 0u }, transition{ 0u };
    std::uint32_t context{ 0u }, reasons{ 0u };
};
struct STextBoundary { std::uint64_t generation{ 0u }, transition{ 0u }; };
struct alignas(8) SActionInputV1
{
    std::uint64_t sequence{ 0u }, time_ns{ 0u };
    std::uint32_t subject{ 0u };
    EActionKind kind{ EActionKind::digital };
    std::uint16_t flags{ 0u };
    unsigned char payload[24]{};
};
struct alignas(8) STextInputV1
{
    std::uint64_t sequence{ 0u }, time_ns{ 0u };
    ETextKind kind{ ETextKind::scalar };
    std::uint16_t byte_count{ 0u };
    std::uint32_t reserved{ 0u };
    unsigned char payload[16]{};
};

struct SInputEndpointV4
{
    [[nodiscard]] bool is_valid() const noexcept;
    std::uint32_t size{ sizeof(SInputEndpointV4) }, version{ 4u };
    EConnection kind{ EConnection::desktop };
    EClock clock{ EClock::awake_nanoseconds };
    SConnectionToken connection;
    modules::SModuleInstance provider, consumer;
    windows::SWindowToken window;
    std::uint32_t action_capacity{ 0u }, text_capacity{ 0u };
    ::threading::transports::TRingConsumerEndpoint<SActionInputV1>* actions{ nullptr };
    ::threading::transports::TRingConsumerEndpoint<STextInputV1>* text{ nullptr };
    const CInputPublication* state{ nullptr };
    platform::threading::SWakeEndpoint capacity_wake;
};

inline bool SInputEndpointV4::is_valid() const noexcept
{
    return (size == sizeof(SInputEndpointV4)) && (version == 4u) &&
        ((kind == EConnection::desktop) || (kind == EConnection::controller)) &&
        (((kind == EConnection::desktop) && (clock == EClock::awake_nanoseconds)) ||
            ((kind == EConnection::controller) && (clock == EClock::source_nanoseconds))) &&
        connection.value && provider.value && consumer.value &&
        (action_capacity == k_action_capacity) && actions && actions->is_ready() && state && capacity_wake.is_valid() &&
        ((kind == EConnection::desktop) ? (window.value && text && text->is_ready() && (text_capacity == k_text_capacity)) :
            (!window.value && !text && !text_capacity));
}

static_assert((sizeof(SActionInputV1) == 48u) && (alignof(SActionInputV1) == 8u));
static_assert((sizeof(STextInputV1) == 40u) && (alignof(STextInputV1) == 8u));
static_assert(std::is_trivially_copyable_v<SActionInputV1> && std::is_trivially_copyable_v<STextInputV1>);
static_assert(std::is_standard_layout_v<SInputEndpointV4> && std::is_trivially_copyable_v<SInputEndpointV4>);
static_assert((sizeof(SAcquireDesktop) == 24u) && (sizeof(SAcquireController) == 24u));
static_assert((sizeof(SInputReady) == 24u) && (sizeof(SBoundaryReady) == 24u) && (sizeof(STextTarget) == 24u));

}   //  namespace platform::input
