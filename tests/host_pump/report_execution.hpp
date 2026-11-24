
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Common-context report acquisition, historical views and requester departure.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/platform/report_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "tests/environment/test_paths.hpp"
#include "debug/macros.hpp"
#include <string>

namespace pump_tests
{

class CReportExecution
{
public:
    enum class EMode : unsigned { snapshot, cancel, shutdown, departure, injected, live };
    CReportExecution(const modules::CCommunication& communication, const bool replacement,
        const char* const output_prefix = "") noexcept
        : m_communication{ communication }, m_replacement{ replacement }, m_output_prefix{ output_prefix } {}
    bool start(const EMode mode) noexcept
    {
        if (m_active) return false;
        m_active = true;
        m_mode = mode;
        next((mode == EMode::snapshot) ? EPhase::before_load :
            (((m_replacement && (mode == EMode::departure)) || (mode == EMode::live)) ? EPhase::initial : EPhase::load));
        return true;
    }
    bool active() const noexcept { return m_active; }
    bool failed() const noexcept { return m_failed; }
    bool live_ready() const noexcept { return m_live_done && !m_failed; }
    bool live_update_pending() const noexcept { return (m_mode == EMode::live) && (m_phase == EPhase::refresh) && m_report_waiting; }
    bool done() const noexcept { return m_active && m_stopping && domain_complete(); }
    bool domain_complete() const noexcept
    {
        if (!m_active) return true;
        if (m_unsafe || (!m_stopping && !m_live_done))
        {
            return false;
        }
        for (const platform::reports::PlatformReportResult& result : m_views)
        {
            if (result.acquisition) return false;
        }
        return m_operation != EOperation::release;
    }
    void stop() noexcept
    {
        m_stopping = true;
        for (platform::reports::PlatformReportResult& result : m_views) result.bytes = {};
        // An unread report result owns no module-local view. Host final ingress,
        // reply cancellation and acquisition reclamation protect its backing.
        // Keep a release already in flight until its exact acknowledgement.
        if (m_operation != EOperation::release) reset_operation();
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        using namespace platform::reports;
        if (m_unsafe) return;
        const std::int32_t slot = message.query_async_slot();
        if ((m_mode == EMode::injected) && (slot >= 267) && (slot <= 269))
        {
            receive_overlap(message);
            return;
        }
        if ((slot == 264) || (slot == 267))
        {
            bool& waiting = (slot == 264) ? m_report_waiting : m_departure_waiting;
            PlatformReportResult result;
            if (!waiting || !message.copy_payload_to(result) || !valid_report(result))
            {
                unsafe();
                return;
            }
            waiting = false;
            // Once stopped, never acquire a local reader from late output.
            // Its accepted Host acquisition is reclaimed at endpoint quiescence;
            // reopening local cleanup after work_complete would violate sealing.
            if (m_stopping) return;
            const unsigned index = (slot == 264) ? 0u : 4u;
            if (m_views[index].acquisition)
            {
                unsafe();
                return;
            }
            m_views[index] = result;
            if (slot == 264) m_report_done = true;
            return;
        }
        if (slot == 265)
        {
            AssetReleaseResult result;
            if ((m_operation != EOperation::release) || !m_sent || !message.copy_payload_to(result) ||
                (result.acquisition != m_views[m_release_index].acquisition))
            {
                unsafe();
                return;
            }
            reset_operation();
            if (result.status != EAssetStatus::success)
            {
                unsafe();
                return;
            }
            m_views[m_release_index] = {};
            if (!m_stopping)
            {
                if (m_poll_release) retry_poll();
                else next(m_after_release);
            }
            return;
        }
        if (slot == 10)
        {
            modules::ModuleResult result;
            if (!message.copy_payload_to(result))
            {
                unsafe();
                return;
            }
            if (m_stopping || m_self_sent)
            {
                return;
            }
            if ((m_operation != EOperation::module) || !m_sent || (result.module != module_ids::platform))
            {
                fail();
                return;
            }
            if (!m_acknowledged)
            {
                m_acknowledged = (result.notice == modules::EModuleNotice::acknowledged) &&
                    (result.status == modules::EModuleStatus::success);
                if (!m_acknowledged) fail();
                return;
            }
            reset_operation();
            if ((result.notice != modules::EModuleNotice::completed) || (result.status != modules::EModuleStatus::success))
            {
                fail();
                return;
            }
            next(m_after_module);
            return;
        }
        unsafe();
    }
    void advance(bool& runnable, std::uint64_t& next_wake) noexcept
    {
        if (!m_active || m_unsafe)
        {
            return;
        }
        if (m_timing && !m_stopping)
        {
            std::uint64_t remaining = 0u;
            if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                fail();
            }
            else if (remaining < next_wake) next_wake = remaining;
        }
        if (m_stopping)
        {
            if (m_operation == EOperation::none)
            {
                for (unsigned index = 0u; index < 5u; ++index)
                {
                    if (m_views[index].acquisition)
                    {
                        release(index, EPhase::waiting);
                        break;
                    }
                }
            }
            publish(runnable);
            return;
        }
        if (m_operation != EOperation::none)
        {
            publish(runnable);
            return;
        }
        if (m_views[4].acquisition)
        {
            release(4u, m_phase);
            publish(runnable);
            return;
        }
        if (m_poll_pending)
        {
            std::uint64_t remaining = 0u;
            if (m_poll_due.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                fail();
                return;
            }
            if (remaining)
            {
                if (remaining < next_wake) next_wake = remaining;
                return;
            }
            m_poll_pending = false;
        }
        if (m_report_done)
        {
            m_report_done = false;
            consume_report();
            runnable = !m_failed;
            return;
        }
        if (m_report_waiting) return;
        if (!m_timing && (m_phase != EPhase::waiting))
        {
            m_timing = true;
            if (m_deadline.set_after_milliseconds(8000u).code != platform::threading::EWaitError::none)
            {
                fail();
                return;
            }
            if (next_wake > UINT64_C(8000000000)) next_wake = UINT64_C(8000000000);
        }
        switch (m_phase)
        {
            case EPhase::load:
            case EPhase::reload:
            {
                module(modules::EModuleAction::load, (m_phase == EPhase::load) ? EPhase::initial : EPhase::current);
                break;
            }
            case EPhase::unload:
            case EPhase::final_unload:
            {
                module(modules::EModuleAction::unload, (m_phase == EPhase::unload) ? EPhase::unavailable : EPhase::self_stop);
                break;
            }
            case EPhase::before_load:
            case EPhase::initial:
            case EPhase::unavailable:
            case EPhase::current:
            case EPhase::refresh:
            case EPhase::failed_update:
            case EPhase::retained:
            {
                m_pod = ((m_phase == EPhase::refresh) || (m_phase == EPhase::failed_update)) ?
                    threading::make_pod_message(264, platform::reports::PlatformReportUpdateRequest{}) :
                    threading::make_pod_message(264, platform::reports::PlatformReportRequest{});
                m_operation = EOperation::report;
                break;
            }
            case EPhase::foreign:
            {
                using namespace platform::reports;
                const PlatformReportCaptureFailed foreign{ { { 1u }, 2u }, { EStage::capture, EReason::clock_failed } };
                m_pod = threading::make_pod_message(-1, foreign);
                m_operation = EOperation::foreign;
                break;
            }
            case EPhase::overlap:
            {
                // Three named requests share one bounded publication pass so
                // the capture's controlled pending interval is actually tested.
                while (m_overlap_posted < 3u)
                {
                    const std::int32_t slot = 267 + static_cast<std::int32_t>(m_overlap_posted);
                    const threading::CErasedPodMsg request = (m_overlap_posted == 2u) ?
                        threading::make_pod_message(slot, platform::reports::PlatformReportRequest{}) :
                        threading::make_pod_message(slot, platform::reports::PlatformReportUpdateRequest{});
                    const modules::ESubmission result = m_communication.submit({}, request);
                    if (result == modules::ESubmission::busy)
                    {
                        return;
                    }
                    if (!modules::accepted(result))
                    {
                        fail();
                        return;
                    }
                    ++m_overlap_posted;
                }
                if (m_overlap_seen == 7u) release(0u, EPhase::unload);
                else return;
                break;
            }
            case EPhase::release_all:
            {
                for (unsigned index = 0u; index < 5u; ++index)
                {
                    if (m_views[index].acquisition)
                    {
                        release(index, EPhase::release_all);
                        break;
                    }
                }
                if (m_operation == EOperation::none)
                {
                    m_live_done = m_mode == EMode::live;
                    next(m_live_done ? EPhase::waiting : EPhase::final_unload);
                    runnable = !m_live_done;
                }
                break;
            }
            case EPhase::departure_update:
            {
                m_pod = threading::make_pod_message(267, platform::reports::PlatformReportUpdateRequest{});
                m_operation = EOperation::departure;
                break;
            }
            case EPhase::self_stop:
            {
                module(((m_mode == EMode::departure) && !m_replacement) ? modules::EModuleAction::replace :
                    modules::EModuleAction::unload, EPhase::waiting, true);
                break;
            }
            case EPhase::waiting:
            {
                break;
            }
        }
        publish(runnable); // One named submission, apart from the three-request overlap pass.
    }

private:
    friend struct SReportExecutionTestAccess;
    enum class EPhase : unsigned
    {
        before_load, load, initial, refresh, unload, unavailable, reload, current,
        release_all, final_unload, departure_update, self_stop, waiting, foreign, failed_update, retained, overlap
    };
    enum class EOperation : unsigned { none, module, report, release, departure, self, foreign };
    const modules::CCommunication& m_communication;
    const bool m_replacement;
    const char* const m_output_prefix;
    EMode m_mode{ EMode::snapshot };
    EPhase m_phase{ EPhase::before_load }, m_after_module{ EPhase::waiting }, m_after_release{ EPhase::waiting };
    EOperation m_operation{ EOperation::none };
    threading::CErasedOwnerMsg m_owner;
    threading::CErasedPodMsg m_pod;
    platform::reports::PlatformReportResult m_views[5]; // Current, cold, refresh, reload, late departure.
    unsigned m_release_index{};
    unsigned m_failure_index{}, m_overlap_posted{}, m_overlap_seen{};
    std::uint8_t m_signature{};
    bool m_active{}, m_stopping{}, m_failed{}, m_unsafe{}, m_sent{}, m_acknowledged{}, m_self_sent{};
    bool m_live_done{};
    bool m_report_waiting{}, m_report_done{}, m_departure_waiting{}, m_poll_pending{}, m_poll_release{}, m_timing{};
    platform::threading::CWaitDeadline m_deadline, m_poll_due;
    void reset_operation() noexcept
    {
        m_owner = {};
        m_operation = EOperation::none;
        m_sent = m_acknowledged = false;
    }
    void next(const EPhase phase) noexcept
    {
        reset_operation();
        m_phase = phase;
        m_timing = false;
    }
    void fail() noexcept { m_failed = true; stop(); }
    void unsafe() noexcept { m_unsafe = true; fail(); }
    static bool valid_report(const platform::reports::PlatformReportResult& result) noexcept
    {
        using namespace platform::reports;
        return (result.status <= EStatus::delivery_failed) && (result.availability <= EAvailability::available) &&
            (result.last_update <= ELastUpdate::failed) && ((result.status == EStatus::success) ?
                (result.acquisition && result.bytes.data() && result.bytes.size()) : (!result.acquisition && !result.bytes.size()));
    }
    void receive_overlap(const threading::CErasedPodMsg& message) noexcept
    {
        using namespace platform::reports;
        const unsigned index = static_cast<unsigned>(message.query_async_slot() - 267);
        const unsigned bit = 1u << index;
        PlatformReportResult result;
        if ((index >= m_overlap_posted) || (m_overlap_seen & bit) || !message.copy_payload_to(result) || !valid_report(result))
        {
            unsafe();
            return;
        }
        m_overlap_seen |= bit;
        if (m_stopping) return; // Host cancels late, unread acquisition custody.
        if (index == 1u)
        {
            if ((result.status != EStatus::update_in_progress) || !result.update_in_progress) fail();
            return;
        }
        if (result.status != EStatus::success)
        {
            fail();
            return;
        }
        const unsigned held = (index == 0u) ? 2u : 0u;
        if (m_views[held].acquisition || (result.acquisition == m_views[1].acquisition) ||
            (result.acquisition == m_views[(held == 0u) ? 2u : 0u].acquisition))
        {
            unsafe();
            return;
        }
        m_views[held] = result; // Preserve even an unexpected view for cleanup.
        if ((index == 0u) ? (result.bytes.data() == m_views[1].bytes.data()) :
            ((result.bytes.data() != m_views[1].bytes.data()) || !result.update_in_progress)) fail();
    }
    void retry_poll() noexcept
    {
        m_poll_pending = true;
        if (m_poll_due.set_after_milliseconds(1u).code != platform::threading::EWaitError::none) fail();
    }
    void release(const unsigned index, const EPhase after, const bool poll = false) noexcept
    {
        m_release_index = index;
        m_after_release = after;
        m_poll_release = poll;
        m_views[index].bytes = {}; // No byte access after release publication.
        m_pod = threading::make_pod_message(265, AssetReleaseRequest{ m_views[index].acquisition });
        m_operation = EOperation::release;
        m_sent = false;
    }
    void module(const modules::EModuleAction action, const EPhase after, const bool self = false) noexcept
    {
        m_owner = threading::make_owner_message<modules::ModuleRequest>(10);
        modules::ModuleRequest* const request = m_owner.owner().payload<modules::ModuleRequest>();
        if (!request)
        {
            fail();
            return;
        }
        request->module = self ? module_ids::executive : module_ids::platform;
        request->action = action;
        if (action != modules::EModuleAction::unload)
        {
            const std::string file = test_environment::module_filename(self ? "package:/bin/MorphicPumpReplacement" :
                ((m_mode == EMode::snapshot) ? "package:/bin/MorphicPlatform" : "package:/bin/MorphicInputPlatform"));
            if (!request->file.set(file.c_str()))
            {
                fail();
                return;
            }
        }
        m_after_module = after;
        m_operation = self ? EOperation::self : EOperation::module;
    }
    void publish(bool& runnable) noexcept
    {
        if ((m_operation == EOperation::none) || m_sent || m_unsafe)
        {
            return;
        }
        const modules::ESubmission result = m_owner.has_owner() ?
            m_communication.submit({}, std::move(m_owner)) : m_communication.submit({}, m_pod);
        if (result == modules::ESubmission::busy) return;
        if (!modules::accepted(result))
        {
            // A refused release still leaves proven acquisition custody locally.
            if (m_operation == EOperation::release) unsafe();
            else fail();
            return;
        }
        m_sent = true;
        if (m_operation == EOperation::report)
        {
            m_report_waiting = true;
            reset_operation();
        }
        else if (m_operation == EOperation::departure)
        {
            m_departure_waiting = true;
            next(EPhase::self_stop);
            runnable = true;
        }
        else if (m_operation == EOperation::self)
        {
            m_self_sent = true;
            next(EPhase::waiting);
        }
        else if (m_operation == EOperation::foreign)
        {
            next(EPhase::failed_update);
            runnable = true;
        }
    }
    bool save(const char* const name, const CByteConstView bytes) const
    {
        const std::string path = std::string(m_output_prefix) + name + ".bsd";
        return platform::filesystem::saveFile(path.c_str(), bytes);
    }
    void consume_report() noexcept
    {
        using namespace platform::reports;
        PlatformReportResult& result = m_views[0];
        if ((m_phase == EPhase::before_load) || (m_phase == EPhase::unavailable))
        {
            if ((result.status != EStatus::unavailable) || result.update_in_progress)
            {
                fail();
                return;
            }
            next((m_phase == EPhase::before_load) ? EPhase::load :
                ((m_mode == EMode::cancel) ? EPhase::self_stop : EPhase::reload));
            return;
        }
        if ((m_phase == EPhase::initial) && ((m_mode == EMode::cancel) || (m_mode == EMode::shutdown)))
        {
            if ((result.status != EStatus::not_ready) || !result.update_in_progress || (result.last_update != ELastUpdate::none))
            {
                fail();
                return;
            }
            next((m_mode == EMode::cancel) ? EPhase::unload : EPhase::self_stop);
            return;
        }
        if (((m_phase == EPhase::initial) || (m_phase == EPhase::current) || (m_phase == EPhase::retained)) && result.update_in_progress)
        {
            if (result.acquisition) release(0u, m_phase, true);
            else retry_poll();
            return;
        }
        if (m_mode == EMode::injected)
        {
            if (m_phase == EPhase::failed_update)
            {
                if ((result.status != EStatus::failed) || (result.availability != EAvailability::available) ||
                    (result.last_update != ELastUpdate::failed) ||
                    (result.failure.stage != ((m_failure_index == 0u) ? EStage::capture : EStage::validation)))
                {
                    fail();
                    return;
                }
                next(EPhase::retained);
                return;
            }
            if (result.status != EStatus::success)
            {
                fail();
                return;
            }
            if (m_phase == EPhase::retained)
            {
                if ((result.bytes.data() != m_views[1].bytes.data()) || (result.acquisition == m_views[1].acquisition))
                {
                    unsafe();
                    return;
                }
                ++m_failure_index;
                release(0u, (m_failure_index == 1u) ? EPhase::foreign : EPhase::overlap);
                return;
            }
            if (m_phase == EPhase::initial)
            {
                m_signature = result.bytes.data()[0];
                m_views[1] = result;
                result = {};
                next(EPhase::foreign);
                return;
            }
            if ((m_phase != EPhase::current) || !m_views[1].bytes.data() ||
                (m_views[1].bytes.data()[0] != m_signature) || (result.bytes.data() == m_views[1].bytes.data()) ||
                (result.acquisition == m_views[1].acquisition) || (result.acquisition == m_views[2].acquisition))
            {
                unsafe();
                return;
            }
            m_views[3] = result;
            result = {};
            next(EPhase::release_all);
            return;
        }
        if (m_mode == EMode::departure)
        {
            if ((result.status != EStatus::success) || (result.last_update != ELastUpdate::succeeded))
            {
                fail();
                return;
            }
            if (m_replacement) MV_LOG(info, "Replacement Executive acquired current after requester departure");
            release(0u, m_replacement ? EPhase::final_unload : EPhase::departure_update);
            return;
        }
        if (m_mode == EMode::live)
        {
            if ((result.status != EStatus::success) ||
                ((m_phase == EPhase::refresh) && !save("window-refresh", result.bytes)))
            {
                fail();
                return;
            }
            const unsigned held = (m_phase == EPhase::initial) ? 1u : 2u;
            if (result.acquisition == m_views[1].acquisition)
            {
                unsafe();
                return;
            }
            m_views[held] = result;
            result = {};
            next((m_phase == EPhase::initial) ? EPhase::refresh : EPhase::release_all);
            return;
        }
#if MV_PLATFORM_WINDOWS
        if (result.status != EStatus::success)
        {
            fail();
            return;
        }
        const unsigned index = (m_phase == EPhase::initial) ? 1u : ((m_phase == EPhase::refresh) ? 2u : 3u);
        for (unsigned held = 1u; held < index; ++held)
        {
            if (m_views[held].acquisition == result.acquisition)
            {
                unsafe();
                return;
            }
        }
        if (!save((index == 1u) ? "cold" : ((index == 2u) ? "refresh" : "reload"), result.bytes) ||
            ((index == 3u) && (!save("historical", m_views[1].bytes) || !save("historical-refresh", m_views[2].bytes))))
        {
            fail();
            return;
        }
        m_views[index] = result;
        result = {};
#else
        if ((result.failure.reason != EReason::unsupported_backend) ||
            ((m_phase == EPhase::refresh) ? (result.status != EStatus::failed) : (result.status != EStatus::not_ready)))
        {
            fail();
            return;
        }
#endif
        next((m_phase == EPhase::initial) ? EPhase::refresh :
            ((m_phase == EPhase::refresh) ? EPhase::unload : EPhase::release_all));
    }
};

} // namespace pump_tests
