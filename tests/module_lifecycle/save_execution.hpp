
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    save_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Fixed save/disposal operations and final Rendering ingress for lifecycle tests.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/module_lifecycle/fixture_io.hpp"
#include "debug/macros.hpp"
#include <cstdio>

namespace module_lifecycle_tests
{

class CSaveExecution
{
public:
    explicit CSaveExecution(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}

    void start(const CAssetId asset, const bool dispose) noexcept
    {
        m_asset = asset;
        m_dispose = dispose;
    }
    void cancel() noexcept { m_owner = {}; }
    bool runnable() const noexcept { return pending() && !m_blocked; }
    bool complete() const noexcept
    {
        return m_completed && (m_received == k_save_count) &&
            (!m_dispose || (m_disposed && m_duplicate && m_rejected));
    }
    bool advance() noexcept
    {
        m_blocked = false;
        // Keep publication bounded. Each accepted owner leaves this object;
        // busy keeps that exact owner and the original operation deadline.
        for (unsigned turn = 0u; (turn < 4u) && pending(); ++turn)
        {
            const bool saving = m_submitted < k_save_count;
            const bool control = !saving && !m_control;
            const bool disposal = !saving && !control && (m_disposals < 2u);
            modules::ESubmission result;
            if (disposal)
            {
                result = m_communication.submit({}, threading::make_pod_message(
                    (30 + static_cast<std::int32_t>(m_disposals)), AssetDisposeRequest{ m_asset }));
            }
            else
            {
                if (!m_owner.has_owner())
                {
                    if (control)
                    {
                        m_owner = threading::make_owner_message<modules::ModuleRequest>(40);
                        modules::ModuleRequest* const request = m_owner.owner().payload<modules::ModuleRequest>();
                        if (!request) return false;
                        request->module = module_ids::rendering;
                        request->action = m_dispose ? modules::EModuleAction::load : modules::EModuleAction::unload;
                        if (m_dispose && !request->file.set(test_environment::module_filename("package:/bin/MorphicRendering").c_str()))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        const std::int32_t slot = saving ? (200 + static_cast<std::int32_t>(m_submitted)) : 300;
                        m_owner = threading::make_owner_message<AssetSaveRequest>(slot);
                        AssetSaveRequest* const request = m_owner.owner().payload<AssetSaveRequest>();
                        if (!request || !request->file.set("test-output:/lifecycle-disposal.bin"))
                        {
                            return false;
                        }
                        request->source = m_asset;
                        request->settings.format = EAssetFileFormat::raw;
                    }
                }
                result = m_communication.submit({}, std::move(m_owner));
            }
            if (result == modules::ESubmission::busy)
            {
                m_blocked = true;
                break;
            }
            if (!modules::accepted(result)) return false;
            if (saving) ++m_submitted;
            else if (control) m_control = true;
            else if (disposal) ++m_disposals;
            else m_reject_sent = true;
        }
        return true;
    }
    bool receive(const threading::CErasedPodMsg& message) noexcept
    {
        const std::int32_t slot = message.query_async_slot();
        if (slot == 40)
        {
            modules::ModuleResult result;
            if (!m_control || m_completed || !message.copy_payload_to(result) || (result.module != module_ids::rendering))
            {
                return false;
            }
            if (result.notice == modules::EModuleNotice::acknowledged)
            {
                if (m_acknowledged || (result.status != modules::EModuleStatus::success))
                {
                    return false;
                }
                m_acknowledged = true;
                return true;
            }
            if (!m_acknowledged || (result.notice != modules::EModuleNotice::completed) ||
                (result.status != (m_dispose ? modules::EModuleStatus::already_loaded : modules::EModuleStatus::success)) ||
                (result.available != m_dispose) || (!m_dispose && (m_received != k_save_count)))
            {
                return false;
            }
            m_completed = true;
            return true;
        }
        if ((slot == 30) || (slot == 31))
        {
            AssetDisposeResult result;
            const bool first = slot == 30;
            if (!m_dispose || (m_disposals < (first ? 1u : 2u)) || !message.copy_payload_to(result) ||
                (result.asset != m_asset) || (result.status != (first ? EAssetStatus::success : EAssetStatus::invalid_asset)))
            {
                return false;
            }
            if (first)
            {
                if (m_disposed || (m_received != k_save_count))
                {
                    return false;
                }
                m_disposed = true;
            }
            else
            {
                if (m_duplicate) return false;
                m_duplicate = true;
            }
            return true;
        }
        AssetResult result;
        if (!message.copy_payload_to(result)) return false;
        if (slot == 300)
        {
            if (!m_reject_sent || m_rejected || result.asset || (result.status != EAssetStatus::invalid_asset))
            {
                return false;
            }
            m_rejected = true;
            return true;
        }
        if ((slot < 200) || (slot >= (200 + static_cast<std::int32_t>(m_submitted))))
        {
            return false;
        }
        const unsigned index = static_cast<unsigned>(slot - 200);
        if (m_seen[index] || m_disposed || (result.asset != m_asset) || (result.status != EAssetStatus::success))
        {
            return false;
        }
        m_seen[index] = true;
        ++m_received;
        return true;
    }

private:
    static constexpr unsigned k_save_count{ 32u };
    modules::CCommunication m_communication;
    threading::CErasedOwnerMsg m_owner;
    CAssetId m_asset;
    unsigned m_submitted{ 0u }, m_received{ 0u }, m_disposals{ 0u };
    bool m_seen[k_save_count]{};
    bool m_dispose{ false }, m_control{ false }, m_acknowledged{ false }, m_completed{ false };
    bool m_disposed{ false }, m_duplicate{ false }, m_reject_sent{ false }, m_rejected{ false }, m_blocked{ false };

    bool pending() const noexcept
    {
        return (m_submitted < k_save_count) || !m_control ||
            (m_dispose && m_acknowledged && (m_disposals < 2u)) || (m_duplicate && !m_reject_sent);
    }
};

class CFinalDisposalExecution
{
public:
    explicit CFinalDisposalExecution(const modules::SExecutionContext& context) noexcept
        : m_communication{ *context.communication } {}

    bool configure() noexcept
    {
        std::FILE* const file = open_disposal_handoff("rb");
        if (!file)
        {
            return false;
        }
        const bool read = std::fread(m_assets, sizeof(m_assets), 1u, file) == 1u;
        const bool closed = std::fclose(file) == 0;
        return read && closed && m_assets[0] && m_assets[1] && (m_assets[0] != m_assets[1]);
    }
    void configure(const CAssetId first, const CAssetId second) noexcept
    {
        m_assets[0] = first;
        m_assets[1] = second;
    }
    bool complete() const noexcept { return m_stopping && ((m_submitted == 2u) || m_failed); }
    modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        if (invocation.event == modules::EExecutionEvent::start)
        {
            m_failed |= !m_assets[0] || !m_assets[1];
        }
        else if (invocation.event == modules::EExecutionEvent::stop) m_stopping = true;
        else if (invocation.event == modules::EExecutionEvent::message)
        {
            // Results may arrive between the two posts. They are not required
            // for this deliberate final-ingress case: no byte view is held.
            AssetDisposeResult result;
            const std::int32_t slot = invocation.message ? invocation.message->query_async_slot() : -1;
            if (!invocation.source.is_host() || (slot < 600) ||
                (slot >= (600 + static_cast<std::int32_t>(m_submitted))) ||
                !invocation.message->copy_payload_to(result) || (result.status != EAssetStatus::success))
            {
                m_failed = true;
            }
            else
            {
                const unsigned index = static_cast<unsigned>(slot - 600);
                m_failed |= m_seen[index] || (result.asset != m_assets[index]);
                m_seen[index] = true;
            }
        }
        else if ((invocation.event != modules::EExecutionEvent::service_async) &&
            (invocation.event != modules::EExecutionEvent::provider_admission_closed))
        {
            m_failed = true;
        }
        if (m_stopping && !m_failed)
        {
            while (m_submitted < 2u)
            {
                const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(
                    (600 + static_cast<std::int32_t>(m_submitted)), AssetDisposeRequest{ m_assets[m_submitted] }));
                if (result == modules::ESubmission::busy) break;
                if (!modules::accepted(result))
                {
                    m_failed = true;
                    break;
                }
                ++m_submitted;
            }
            if ((m_submitted == 2u) && !m_logged)
            {
                m_logged = true;
                MV_LOG(info, "Lifecycle rendering: final disposals posted");
            }
        }
        progress = {};
        progress.ready = !m_failed;
        progress.work_complete = complete();
        if (m_failed && !m_reported)
        {
            m_reported = true;
            return modules::EExecutionStatus::failed;
        }
        return modules::EExecutionStatus::success;
    }

private:
    modules::CCommunication m_communication;
    CAssetId m_assets[2];
    bool m_seen[2]{};
    unsigned m_submitted{ 0u };
    bool m_stopping{ false }, m_failed{ false }, m_reported{ false }, m_logged{ false };
};

}   // namespace module_lifecycle_tests
