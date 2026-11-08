
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_runtime.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Declares the Host-pumped Platform runtime, its native callback interface and
//  the window, borrow and reply state retained until cleanup completes.

#pragma once

#include "runtime/window_record.hpp"
#include "runtime/input_connection.hpp"
#include "runtime/input_configuration.hpp"
#include "runtime/desktop_input.hpp"
#include "assets/schema_messages.hpp"
#include "containers/TInstance.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "module/host_pump.hpp"
#include "threading/messages/CErasedPodMsg.hpp"

namespace platform::runtime
{

class CPlatformRuntime
{
public:
    CPlatformRuntime(const modules::SHostPumpContext& context,
        const native::SWindowAdapter& adapter = native::window_adapter(),
        const platform::threading::SAwakeClock clock = {}) noexcept;
    [[nodiscard]] bool initialise() noexcept;
    void pump(const modules::SPumpInput& input, modules::SPumpResult& result) noexcept;
    void begin_stop(modules::SProgress& progress) noexcept;
    [[nodiscard]] bool stopped() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

    //  Native adapter callbacks only record state, publish and perform bounded
    //  maintenance. Record erasure and native destruction stay in outer pump.
    void native_extent(const std::uint32_t width, const std::uint32_t height, const std::uint32_t dpi, const bool minimised) noexcept;
    void native_close() noexcept;
    void input_focus(const bool focused) noexcept;
    [[nodiscard]] EInputAdmission input_observation(const input::EConnection kind, const SInputObservation& observation) noexcept;
    void input_device(const input::EConnection kind, const std::uint64_t instance) noexcept;
    void input_discontinuity(const input::EConnection kind) noexcept;
    void desktop_observation(const SDesktopTransaction& transaction) noexcept;
    void desktop_quiet(const SInputObservation& baseline, const std::uint64_t epoch) noexcept;
    [[nodiscard]] std::uint64_t desktop_epoch() noexcept { m_desktop.synchronise(); return m_desktop.epoch(); }
    [[nodiscard]] std::uint64_t desktop_text_epoch() const noexcept { return m_desktop.text_epoch(); }
    [[nodiscard]] bool desktop_text_pending() const noexcept { return m_desktop.text_pending(); }
    void desktop_capture_lost() noexcept;
    void native_stop() noexcept { m_host_stop = true; }
    void native_failure(const std::uint32_t error) noexcept;
    void enter_callback() noexcept;
    void leave_callback() noexcept;
    void enter_modal(const bool menu) noexcept;
    void leave_modal(const bool menu) noexcept;
    [[nodiscard]] bool maintain_native() noexcept;
    [[nodiscard]] bool cancelling() const noexcept;

private:
    friend struct SPlatformTestAccess;
    friend struct SPlatformInputTestAccess;
    struct SCompletion
    {
        services::ServiceReply reply;
        bool live{ false }, terminal{ false };
    };
    [[nodiscard]] std::int32_t reserve(const services::ServiceRequest& command, const bool acquisition) noexcept;
    void complete(const std::int32_t slot, const services::EServiceStatus status) noexcept;
    void flush_completions() noexcept;
    [[nodiscard]] bool command(const services::SDispatchedCommand& command) noexcept;
    void acquire(const services::SDispatchedCommand& command, const bool create, const std::int32_t completion) noexcept;
    [[nodiscard]] services::EServiceStatus ready(const services::SDispatchedCommand& command, const windows::SWindowReady& request) noexcept;
    [[nodiscard]] bool message(const ::threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] SWindowUse* find(const services::SServiceBorrow borrow) noexcept;
    [[nodiscard]] bool owns(const SWindowUse& use, const services::SDispatchedCommand& command) const noexcept;
    [[nodiscard]] bool holder(const services::SDispatchedCommand& command, const bool controller) const noexcept;
    void close_use(SWindowUse& use, const services::EServiceStatus status) noexcept;
    void progress_uses() noexcept;
    [[nodiscard]] bool input_command(const services::SDispatchedCommand& command) noexcept;
    void input_message(const ::threading::CErasedPodMsg& message) noexcept;
    void close_input(CInputConnection& connection, const services::EServiceStatus status) noexcept;
    void progress_inputs() noexcept;
    void progress_mappers(const std::uint64_t budget_ns) noexcept;
    void input_eligibility() noexcept;
    void release_native_capture() noexcept;
    [[nodiscard]] bool configuration_command(const services::SDispatchedCommand& command, const std::int32_t completion) noexcept;
    void configuration_message(const ::threading::CErasedPodMsg& message) noexcept;
    void progress_configurations(const std::uint64_t budget_ns) noexcept;
    void cancel_configuration(const services::EServiceStatus status) noexcept;
    [[nodiscard]] bool configuration_used(const input::SInstallationToken token) const noexcept;
    [[nodiscard]] CInputConnection* find_input(const input::SConnectionToken token) noexcept;
    void publish() noexcept;
    void close_window(const std::uint32_t failure = 0u) noexcept;
    void progress_window() noexcept;
    void progress(modules::SProgress& progress) const noexcept;
    [[nodiscard]] modules::SRestrictionScope enter_native(const std::uint32_t reason) noexcept;
    void leave_native(const modules::SRestrictionScope scope) noexcept;

    const modules::SHostPumpContext& m_context;
    const native::SWindowAdapter& m_adapter;
    platform::threading::SAwakeClock m_clock;
    SWindowRecord m_window;
    TUnorderedCollection<SWindowUse> m_uses;
    TInstance<CInputConnection> m_inputs[2];
    CDesktopInput m_desktop;
    bool m_native_capture{}, m_releasing_capture{}, m_desktop_eligible{ true };
    std::uint64_t m_capture_epoch{};
    TUnorderedCollection<SInputInstallation> m_input_installations;
    struct SConfiguration
    {
        TInstance<CInputConfiguration> data;
        input::SInstallationToken token;
        std::int32_t retirement{ -1 };
        bool retiring{ false };
    };
    struct SPreparation
    {
        explicit SPreparation(const platform::threading::SAwakeClock clock = {}) noexcept : budget{ clock } {}
        modules::CHostEligibleBudget budget;
        SchemaPrepareRequest request;
        services::SDispatchedCommand command;
        CAssetAcquisitionId acquisition;
        input::SInstallationToken token;
        std::int32_t configuration{ -1 }, completion{ -1 }, correlation{ -1 };
        bool requested{ false }, waiting{ false }, building{ false }, release_waiting{ false }, cancelled{ false };
    };
    SConfiguration m_configurations[8];
    SPreparation m_preparation;
    std::uint64_t m_next_installation{ 1u };
    std::int32_t m_next_conditioning{ 1 };
    bool m_configuration_runnable{ false };
    SCompletion m_completions[96]{};
    std::uint32_t m_use_count{ 0u }, m_native_depth{ 0u }, m_callback_depth{ 0u };
    modules::SRestrictionScope m_modal[2][8]{};
    std::uint32_t m_modal_depth[2]{};
    std::uint64_t m_next_window{ 1u };
    std::uint64_t m_next_input{ 1u };
    std::uint32_t m_mapper_turn{};
    bool m_input_focus{ true };
    bool m_stopping{ false }, m_failed{ false }, m_maintaining{ false }, m_native_first{ false };
    bool m_host_stop{ false };
};

}   //  namespace platform::runtime
