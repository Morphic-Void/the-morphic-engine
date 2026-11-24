
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    source_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Input fixture source custody through installation and acknowledged cleanup.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/platform/input.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include <cstring>

namespace input_host
{

//  One named source operation, owned by the module execution object. It never
//  reads the acquired bytes. The pin protects configuration installation until
//  its terminal result, including when stop arrives after transport acceptance.
class CSourceExecution
{
public:
    enum class EMode { install, transfer_cleanup, acquire_cleanup };
    explicit CSourceExecution(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}
    [[nodiscard]] bool start(const void* const data, const std::size_t size, const services::SServiceRoute route,
        const services::SServiceRequestId request, const EMode mode = EMode::install) noexcept
    {
        if (!complete() || m_failed || !data || !size ||
            ((mode == EMode::install) && (!route.value || !request.value)))
        {
            return false;
        }
        CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
        RawAssetTransfer* const transfer = owner.payload<RawAssetTransfer>();
        if (!transfer || !transfer->storage.value.resize(size, 32u))
        {
            return false;
        }
        std::memcpy(transfer->storage.value.data(), data, size);
        m_owner.set_message_type<RawAssetTransfer>();
        m_owner.set_async_slot(k_slot);
        m_owner.set_owner(std::move(owner));
        m_route = route;
        m_request = request;
        m_mode = mode;
        m_installation = {};
        m_stopping = m_sent = m_acknowledged = false;
        return enter(EPhase::transfer);
    }
    void stop() noexcept { m_stopping = true; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool complete() const noexcept
    {
        return !m_unsafe && !m_sent && !m_asset.is_valid() && !m_acquisition && (m_phase == EPhase::idle);
    }
    [[nodiscard]] bool owns(const std::int32_t slot) const noexcept { return slot == k_slot; }
    [[nodiscard]] platform::input::SInstallationToken installation() const noexcept { return m_installation; }

    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_unsafe) return;
        if (!owns(message.query_async_slot()) || !m_sent)
        {
            unsafe();
            return;
        }
        switch (m_phase)
        {
            case EPhase::transfer:
            {
                AssetResult result;
                if (!message.copy_payload_to(result) || (result.status > EAssetStatus::busy) ||
                    ((result.status != EAssetStatus::success) && (result.asset.is_valid() || (result.kind != EAssetKind::none))))
                {
                    unsafe();
                    return;
                }
                m_sent = false;
                if (result.status != EAssetStatus::success)
                {
                    m_failed = true;
                    (void)enter(EPhase::idle);
                    return;
                }
                m_asset = result.asset;
                if (!m_asset.is_valid() || (result.kind != EAssetKind::raw))
                {
                    unsafe();
                    return;
                }
                (void)enter((m_stopping || (m_mode == EMode::transfer_cleanup)) ? EPhase::dispose : EPhase::acquire);
                break;
            }
            case EPhase::acquire:
            {
                AssetAcquireResult result;
                if (!message.copy_payload_to(result) || (result.status > EAssetStatus::busy) ||
                    (result.view != EAssetAcquireView::bytes) ||
                    ((result.status != EAssetStatus::success) && (result.acquisition || result.views.bytes.data() || result.views.bytes.size())))
                {
                    unsafe();
                    return;
                }
                m_sent = false;
                if (result.status != EAssetStatus::success)
                {
                    m_failed = true;
                    (void)enter(EPhase::dispose);
                    return;
                }
                m_acquisition = result.acquisition;
                if (!m_acquisition || !result.views.bytes.data() || !result.views.bytes.size())
                {
                    unsafe();
                    return;
                }
                // No byte view is retained, even while installation is pending.
                (void)enter((m_stopping || (m_mode == EMode::acquire_cleanup)) ? EPhase::release : EPhase::install);
                break;
            }
            case EPhase::install:
            {
                services::ServiceReply result;
                if (!message.copy_payload_to(result) || (result.route.value != m_route.value) ||
                    (result.request.value != m_request.value) ||
                    (result.opcode != static_cast<std::uint16_t>(platform::input::ECommand::install_configuration)) ||
                    (result.status > services::EServiceStatus::delivery_failed))
                {
                    unsafe();
                    return;
                }
                if (result.notice == services::EServiceNotice::acknowledged)
                {
                    if (m_acknowledged || (result.status != services::EServiceStatus::success) || result.payload_size)
                    {
                        unsafe();
                        return;
                    }
                    m_acknowledged = true;
                    return;
                }
                if ((result.notice != services::EServiceNotice::completed) || !m_acknowledged)
                {
                    unsafe();
                    return;
                }
                if (result.status == services::EServiceStatus::success)
                {
                    platform::input::SInstallationResult installed;
                    if (result.payload_size != sizeof(installed))
                    {
                        unsafe();
                        return;
                    }
                    std::memcpy(&installed, result.payload, sizeof(installed));
                    m_installation = installed.installation;
                    if (!m_installation.value || (installed.error != platform::input::EConfigurationError::none) || installed.reserved)
                    {
                        unsafe();
                        return;
                    }
                }
                else
                {
                    // Invalid authored configuration may carry a diagnostic,
                    // but cannot confer an installed configuration identity.
                    platform::input::SInstallationResult diagnostic;
                    if (result.payload_size == sizeof(diagnostic))
                    {
                        std::memcpy(&diagnostic, result.payload, sizeof(diagnostic));
                    }
                    if ((result.payload_size && (result.payload_size != sizeof(diagnostic))) ||
                        diagnostic.installation.value || diagnostic.reserved ||
                        (diagnostic.error > platform::input::EConfigurationError::allocation))
                    {
                        unsafe();
                        return;
                    }
                    m_failed = true;
                }
                m_sent = false;
                (void)enter(EPhase::release);
                break;
            }
            case EPhase::release:
            {
                AssetReleaseResult result;
                if (!message.copy_payload_to(result) || (result.acquisition != m_acquisition) ||
                    (result.status != EAssetStatus::success))
                {
                    unsafe();
                    return;
                }
                m_sent = false;
                m_acquisition = {};
                (void)enter(EPhase::dispose);
                break;
            }
            case EPhase::dispose:
            {
                AssetDisposeResult result;
                if (!message.copy_payload_to(result) || (result.asset != m_asset) || (result.status != EAssetStatus::success))
                {
                    unsafe();
                    return;
                }
                m_sent = false;
                m_asset = {};
                (void)enter(EPhase::idle);
                break;
            }
            case EPhase::idle:
            {
                unsafe();
                break;
            }
        }
    }

    // At most one publication per invocation. Waiting for a result or capacity
    // is not runnable work; the common preamble owns both notification waits.
    void advance(bool& runnable, std::uint64_t& next_wake_ns) noexcept
    {
        runnable = false;
        if (m_unsafe || (m_phase == EPhase::idle))
        {
            return;
        }
        if (m_stopping && !m_sent)
        {
            if (m_phase == EPhase::transfer)
            {
                m_owner = {};
                (void)enter(EPhase::idle);
                return;
            }
            if (m_phase == EPhase::acquire) (void)enter(EPhase::dispose);
            if (m_phase == EPhase::install) (void)enter(EPhase::release);
        }
        std::uint64_t remaining = 0u;
        if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            unsafe();
            return;
        }
        if (remaining < next_wake_ns) next_wake_ns = remaining;
        if (m_sent) return;
        modules::ESubmission result = modules::ESubmission::invalid;
        switch (m_phase)
        {
            case EPhase::transfer:
            {
                result = m_communication.submit({}, std::move(m_owner));
                break;
            }
            case EPhase::acquire:
            {
                result = post(AssetAcquireRequest{ m_asset });
                break;
            }
            case EPhase::install:
            {
                services::ServiceRequest request;
                request.route = m_route;
                request.request = m_request;
                request.opcode = static_cast<std::uint16_t>(platform::input::ECommand::install_configuration);
                const platform::input::SInstallConfiguration install{ m_asset };
                request.payload_size = sizeof(install);
                std::memcpy(request.payload, &install, sizeof(install));
                result = post(request);
                break;
            }
            case EPhase::release:
            {
                result = post(AssetReleaseRequest{ m_acquisition });
                break;
            }
            case EPhase::dispose:
            {
                result = post(AssetDisposeRequest{ m_asset });
                break;
            }
            case EPhase::idle:
            {
                return;
            }
        }
        if ((result == modules::ESubmission::accepted) || (result == modules::ESubmission::published_wake_failed))
        {
            m_sent = true;
        }
        else if (result != modules::ESubmission::busy)
        {
            // An unaccepted initial transfer owns no Host obligation. Failure
            // to publish cleanup cannot prove the accepted resource retired.
            m_failed = true;
            if (m_phase == EPhase::transfer)
            {
                m_owner = {};
                (void)enter(EPhase::idle);
            }
            else unsafe();
        }
    }

private:
    static constexpr std::int32_t k_slot = 2;
    enum class EPhase { idle, transfer, acquire, install, release, dispose };
    bool enter(const EPhase phase) noexcept
    {
        m_phase = phase;
        if ((phase != EPhase::idle) &&
            (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none))
        {
            unsafe();
            return false;
        }
        return true;
    }
    template<class T> modules::ESubmission post(const T& message) const noexcept
    {
        return m_communication.submit({}, threading::make_pod_message(k_slot, message));
    }
    void unsafe() noexcept { m_failed = m_unsafe = true; }
    const modules::CCommunication m_communication;
    threading::CErasedOwnerMsg m_owner;
    platform::threading::CWaitDeadline m_deadline;
    services::SServiceRoute m_route;
    services::SServiceRequestId m_request;
    platform::input::SInstallationToken m_installation;
    CAssetId m_asset;
    CAssetAcquisitionId m_acquisition;
    EMode m_mode{ EMode::install };
    EPhase m_phase{ EPhase::idle };
    bool m_sent{}, m_acknowledged{}, m_stopping{}, m_failed{}, m_unsafe{};
};

}   //  namespace input_host
