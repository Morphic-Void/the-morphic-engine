
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Module-owned bundle lifecycle operations with explicit reply and custody state.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/filesystem/filesystem_messages.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "framework/threading/CBatchWork.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "platform/filesystem/file.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/host_test_paths.hpp"
#include "tests/module_lifecycle/fixture_io.hpp"
#include "debug/macros.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace module_lifecycle_tests
{

class CBundleExecution
{
    friend struct SBundleExecutionTestAccess;
public:
    explicit CBundleExecution(const modules::SExecutionContext& context) noexcept
        : m_communication{ *context.communication }, m_batch{ context.batch } {}

    bool begin(const char* const selected, const bool rendering_owner = false) noexcept
    {
        m_stop_batch = std::strcmp(selected, "bundle-batch-stop") == 0;
        if ((std::strcmp(selected, "bundle") == 0) || (std::strcmp(selected, "bundle-inline") == 0) || m_stop_batch)
        {
            m_scenario = EScenario::workflow;
            if (!m_batch || ((std::strcmp(selected, "bundle-inline") == 0) && m_batch->has_runners()) ||
                (m_stop_batch && !m_batch->has_runners()))
            {
                return false;
            }
        }
        else if (std::strcmp(selected, "bundle-rendering") == 0)
        {
            m_scenario = rendering_owner ? EScenario::rendering : EScenario::rendering_client;
        }
        else if (std::strcmp(selected, "bundle-late-reference") == 0) m_scenario = EScenario::late_reference;
        else if (std::strcmp(selected, "bundle-replace") == 0) m_scenario = EScenario::replace;
        else if (std::strcmp(selected, "bundle-resume") == 0) m_scenario = EScenario::resume;
        else if (std::strcmp(selected, "bundle-shutdown") == 0) m_scenario = EScenario::shutdown;
        else if (std::strcmp(selected, "bundle-failures") == 0) m_scenario = EScenario::failures;
        else return false;
        m_memory = memory::get_ambient_memory_context();
        if (!m_memory) return false;
        m_allocations = m_memory->get_live_allocation_count();
        m_bytes = m_memory->get_live_allocated_bytes();
        if ((m_scenario == EScenario::failures) &&
            (std::remove("development/logical-roots/dev-source/BundleMissing/Vanished") != 0))
        {
            return false;
        }
        return true;
    }
    void cancel() noexcept
    {
        m_owner = {};
        m_cancelled = true;
        m_item.release.store(true, std::memory_order_release);
        if (!m_batch_active) clear_views();
    }
    bool quiescent() const noexcept { return !m_batch_active; }
    bool complete() const noexcept { return (m_complete || m_cancelled) && quiescent(); }
    bool poll_batch_start() const noexcept
    {
        return m_stop_batch && (m_stage == 24u) && !m_item.started.load(std::memory_order_acquire);
    }
    bool stop_requested() const noexcept { return m_stop_ready; }
    bool timed_poll() const noexcept { return poll_batch_start() || m_poll_handoff; }
    bool runnable() const noexcept { return !complete() && !m_waiting && !m_blocked && !timed_poll(); }
    modules::EExecutionStatus handle_rendering(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        switch (invocation.event)
        {
            case modules::EExecutionEvent::start:
            {
                m_domain_ready = begin("bundle-rendering", true);
                m_domain_failed = !m_domain_ready ||
                    (m_domain_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none);
                break;
            }
            case modules::EExecutionEvent::message:
            {
                if (!m_domain_stopping && (!invocation.source.is_host() || !invocation.message || !receive(*invocation.message)))
                {
                    m_domain_failed = true;
                }
                break;
            }
            case modules::EExecutionEvent::stop:
            {
                if (!m_domain_stopping)
                {
                    m_domain_stopping = true;
                    m_owner = {};
                    m_pending = m_waiting = m_blocked = false;
                    if (!m_domain_failed && m_original.asset && !m_closed)
                    {
                        // Value identity only: cease view access before final
                        // closure. Host retains sub-files and notifications.
                        m_original.views = {};
                        m_file = m_result = {};
                        m_stage = 7u;
                        m_domain_failed = m_domain_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none;
                    }
                    else cancel();
                }
                break;
            }
            case modules::EExecutionEvent::service_async:
            case modules::EExecutionEvent::provider_admission_closed:
            {
                break;
            }
            default:
            {
                m_domain_failed = true;
                break;
            }
        }
        if (m_domain_failed && m_domain_stopping) cancel();
        if (!m_domain_failed) m_domain_failed = !advance();
        progress = {};
        progress.ready = m_domain_ready;
        progress.work_complete = m_domain_stopping && complete();
        progress.work_pending = !m_domain_failed && runnable();
        if (!complete() && (m_domain_stopping || (m_stage < 6u)))
        {
            std::uint64_t remaining = 0u;
            if ((m_domain_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                m_domain_failed = true;
            }
            else progress.next_wake_ns = remaining;
        }
        if (m_domain_failed && !m_domain_reported)
        {
            m_domain_reported = true;
            MV_LOG(error, "Lifecycle rendering bundle: execution failed at stage {}", m_stage);
            return modules::EExecutionStatus::failed;
        }
        return modules::EExecutionStatus::success;
    }
    bool service_batch() noexcept
    {
        if (!m_batch_active) return true;
        threading::SBatchResponse response;
        if (!m_batch->receive(response)) return true;
        m_batch_active = false;
        const bool executed = response.completion == threading::EBatchCompletion::executed;
        const bool valid = (response.correlation == 700u) && !m_batch->outstanding() &&
            ((executed && m_item.valid) || (m_cancelled &&
                (response.completion == threading::EBatchCompletion::discarded_requester_exiting)));
        if (m_cancelled)
        {
            clear_views();
            if (m_stop_batch)
            {
                if ((m_memory->get_live_allocation_count() != m_allocations) || (m_memory->get_live_allocated_bytes() != m_bytes))
                {
                    return false;
                }
                MV_LOG(info, "Bundle fixture: stopped batch borrow retired");
                MV_LOG(info, "Bundle fixture: Executive allocation baseline restored");
            }
        }
        else if (m_waiting && (m_reply == EReply::batch))
        {
            m_waiting = false;
            ++m_stage;
        }
        return valid;
    }
    bool advance() noexcept
    {
        if (m_cancelled || complete() || m_waiting)
        {
            return true;
        }
        m_blocked = false;
        if (!m_pending && !prepare())
        {
            return false;
        }
        if (!m_pending) return true;
        const modules::ESubmission result = m_owner.has_owner() ?
            m_communication.submit({}, std::move(m_owner)) : m_communication.submit({}, m_pod);
        if (result == modules::ESubmission::busy)
        {
            m_blocked = true;
            return true;
        }
        if (!modules::accepted(result)) return false;
        m_pending = false;
        if ((m_scenario == EScenario::late_reference) && (m_stage == 2u))
        {
            m_reference_slot = m_slot;
        }
        if ((m_scenario == EScenario::workflow) && ((m_stage == 12u) || (m_stage == 13u)))
        {
            m_parse_slots[m_stage - 12u] = m_slot;
        }
        if (m_closing) m_closed = true;
        if (m_reply == EReply::none) ++m_stage;
        else m_waiting = true;
        return true;
    }
    bool receive(const threading::CErasedPodMsg& message) noexcept
    {
        const std::int32_t slot = message.query_async_slot();
        for (unsigned index = 0u; index < 2u; ++index)
        {
            if ((m_parse_slots[index] == -1) || (slot != m_parse_slots[index]))
            {
                continue;
            }
            if (m_parse_received[index] || !message.copy_payload_to(m_parsed[index]) ||
                (m_parsed[index].status != EAssetStatus::success) || !m_parsed[index].document_view().is_ready())
            {
                return false;
            }
            m_parse_received[index] = true;
            if (m_waiting && (m_reply == EReply::parsed) && m_parse_received[0] && m_parse_received[1])
            {
                m_waiting = false;
                ++m_stage;
            }
            return true;
        }
        if (slot == -1)
        {
            BundleDisposedNotification notification;
            if (!m_closed || m_notified || !message.copy_payload_to(notification) || (notification.asset != m_original.asset))
            {
                return false;
            }
            m_notified = true;
            if (m_waiting && (m_reply == EReply::notification))
            {
                m_waiting = false;
                ++m_stage;
            }
            return true;
        }
        if ((m_reference_slot != -1) && (slot == m_reference_slot))
        {
            if (m_reference.asset || !message.copy_payload_to(m_reference) ||
                (m_reference.status != EAssetStatus::success) || !m_reference.asset)
            {
                return false;
            }
            return true;
        }
        if (!m_waiting || (slot != m_slot))
        {
            return false;
        }
        switch (m_reply)
        {
            case EReply::asset:
            {
                AssetResult result;
                if (!message.copy_payload_to(result) || (result.status != m_expected))
                {
                    return false;
                }
                if ((m_expected != EAssetStatus::success) && result.asset)
                {
                    return false;
                }
                *m_target = result;
                break;
            }
            case EReply::disposal:
            {
                AssetDisposeResult result;
                if (!message.copy_payload_to(result) || (result.asset != m_disposing) || (result.status != m_disposal_status))
                {
                    return false;
                }
                break;
            }
            case EReply::module:
            {
                modules::ModuleResult result;
                if (!message.copy_payload_to(result) || (result.module != module_ids::rendering) ||
                    (result.status != modules::EModuleStatus::success))
                {
                    return false;
                }
                if (!m_module_acknowledged)
                {
                    m_module_acknowledged = result.notice == modules::EModuleNotice::acknowledged;
                    return m_module_acknowledged;
                }
                if ((result.notice != modules::EModuleNotice::completed) || (result.available != m_loading))
                {
                    return false;
                }
                break;
            }
            case EReply::refresh:
            {
                filesystem::FilesystemRefreshResult result;
                if (!message.copy_payload_to(result) || (result.status != m_refresh_status))
                {
                    return false;
                }
                break;
            }
            default:
            {
                return false;
            }
        }
        m_waiting = false;
        ++m_stage;
        return true;
    }

private:
    enum class EScenario { workflow, rendering, rendering_client, late_reference, replace, resume, shutdown, failures };
    enum class EReply { none, asset, disposal, refresh, notification, parsed, batch, module };
    struct SHandoff { CAssetId original, file; };
    struct SBatchRead
    {
        CByteConstView bytes;
        std::atomic<bool> release{ false };
        std::atomic<bool> started{ false };
        bool wait{ false }, valid{ false };
    };
    static constexpr const char* k_root{ "bundle-test:" };
    static constexpr const char* k_saved{ "dev-workspace:/acceptance/lifecycle-bundle.bdl" };
    modules::CCommunication m_communication;
    threading::CBatchClient* const m_batch;
    threading::CErasedOwnerMsg m_owner;
    threading::CErasedPodMsg m_pod;
    AssetResult m_original, m_file, m_result, m_reference;
    AssetResult m_baked, m_image, m_parsed[2];
    CByteBuffer m_snapshot;
    SBatchRead m_item;
    bool m_batch_active{ false }, m_batch_queued{ false }, m_parse_received[2]{};
    bool m_stop_batch{ false }, m_stop_ready{ false };
    std::int32_t m_parse_slots[2]{ -1, -1 };
    AssetResult* m_target{ nullptr };
    CAssetId m_disposing;
    SHandoff m_handoff;
    memory::CMemoryContext* m_memory{ nullptr };
    std::uint32_t m_allocations{ 0u };
    std::uint64_t m_bytes{ 0u };
    EScenario m_scenario{ EScenario::shutdown };
    EReply m_reply{ EReply::none };
    EAssetStatus m_expected{ EAssetStatus::success };
    EAssetStatus m_disposal_status{ EAssetStatus::success };
    filesystem::EFilesystemStatus m_refresh_status{ filesystem::EFilesystemStatus::invalid_root };
    std::int32_t m_slot{ 2000 }, m_reference_slot{ -1 };
    unsigned m_stage{ 0u };
    bool m_pending{ false }, m_waiting{ false }, m_blocked{ false }, m_complete{ false }, m_cancelled{ false };
    bool m_closing{ false }, m_closed{ false }, m_notified{ false };
    bool m_poll_handoff{ false }, m_loading{ false }, m_module_acknowledged{ false };
    bool m_domain_ready{ false }, m_domain_stopping{ false }, m_domain_failed{ false }, m_domain_reported{ false };
    platform::threading::CWaitDeadline m_domain_deadline;

    void clear_views() noexcept
    {
        m_original = m_file = m_result = m_reference = m_baked = m_image = {};
        m_parsed[0] = m_parsed[1] = {};
        m_item.bytes = {};
        m_snapshot.deallocate();
    }
    static bool same_bytes(const CByteConstView first, const CByteConstView second) noexcept
    {
        return first.is_ready() && second.is_ready() && (first.size() == second.size()) &&
            (std::memcmp(first.data(), second.data(), first.size()) == 0);
    }
    static void MV_STD_ABI_CALL read_bundle(void* const object) noexcept
    {
        SBatchRead& item = *static_cast<SBatchRead*>(object);
        item.started.store(true, std::memory_order_release);
        while (item.wait && !item.release.load(std::memory_order_acquire)) platform::threading::processor_relax();
        item.valid = item.bytes.size() == 128u;
        for (std::size_t index = 0u; index < item.bytes.size(); ++index) item.valid &= item.bytes.data()[index] == 0x5a;
    }
    bool document() noexcept
    {
        static constexpr char text[]{ "{\"answer\":42}" };
        CLiveDocument document;
        BakedAssetTransfer* const request = owner<BakedAssetTransfer>();
        expect(m_result);
        if (!request || !document_parser::parse(CByteConstView{
            reinterpret_cast<const std::uint8_t*>(text), (sizeof(text) - 1u) }, document).accepted() ||
            !document_translation::bake(document, request->storage.value) ||
            !request->storage.file.set("dev-source:/BundleInput/Document.baked"))
        {
            return false;
        }
        request->save = true;
        request->retention = EAssetRetention::discard;
        request->settings.format = EAssetFileFormat::baked;
        return true;
    }
    bool inspect_original() noexcept
    {
        const bundle::CBundleView view = m_original.bundle_view();
        if (!view.is_ready()) return false;
        for (const char* const name : { "empty", "zero", "unsupported" })
        {
            if (view.lookup(CStringView{ name }).status != bundle::ELookupStatus::not_found)
            {
                return false;
            }
        }
        for (const char* const name : { "offset", "content", "size", "MiXeD/Child" })
        {
            if (view.lookup(CStringView{ name }).status != bundle::ELookupStatus::success)
            {
                return false;
            }
        }
        for (const std::size_t size : { 127u, 128u, 129u })
        {
            const char* const name = (size == 127u) ? "Before" : ((size == 128u) ? "Aligned" : "After");
            const bundle::SLookupResult found = view.lookup(CStringView{ name });
            if ((found.status != bundle::ELookupStatus::success) || (found.bytes.bytes() != size) ||
                ((reinterpret_cast<std::uintptr_t>(found.bytes.data()) & 127u) != 0u))
            {
                return false;
            }
            const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(found.bytes.data());
            for (std::size_t index = 0u; index < size; ++index)
            {
                if (bytes[index] != 0x5a) return false;
            }
            if (bytes[size] != 0u) return false;
        }
        const CByteConstView bytes = m_original.byte_view();
        if (!m_snapshot.resize(bytes.size())) return false;
        std::memcpy(m_snapshot.data(), bytes.data(), bytes.size());
        return true;
    }
    bool wait_notification() noexcept
    {
        m_reply = EReply::notification;
        if (m_notified) ++m_stage;
        else m_waiting = true;
        return true;
    }
    bool module(const bool load) noexcept
    {
        modules::ModuleRequest* const request = owner<modules::ModuleRequest>();
        if (!request) return false;
        m_loading = load;
        m_module_acknowledged = false;
        m_reply = EReply::module;
        request->module = module_ids::rendering;
        request->action = load ? modules::EModuleAction::load : modules::EModuleAction::unload;
        return !load || request->file.set(test_environment::module_filename("package:/bin/MorphicLifecycleRenderingDisposal").c_str());
    }
    bool rendering() noexcept
    {
        switch (m_stage)
        {
            case 0u:
            {
                return bake(m_original);
            }
            case 1u:
            {
                return save(m_original.asset, k_saved);
            }
            case 2u:
            {
                return close(true);
            }
            case 3u:
            {
                m_original = {};
                m_closed = m_notified = false;
                return bind(k_saved, EAssetStatus::success, &m_original);
            }
            case 4u:
            {
                return load(m_file, "bundle-test:/Aligned");
            }
            case 5u:
            {
                MV_LOG(info, "Bundle fixture: Rendering owns loaded bundle");
                return handoff("test-output:/bundle-rendering-handoff.bin");
            }
            case 6u:
            {
                m_waiting = true; // Stop owns the next transition, not a reply.
                m_reply = EReply::none;
                return true;
            }
            case 7u:
            {
                return close(false);
            }
            default:
            {
                return finish();
            }
        }
    }
    bool rendering_client() noexcept
    {
        switch (m_stage)
        {
            case 0u:
            {
                return module(true);
            }
            case 1u:
            {
                // Preserve the test-only value handoff, with one bounded file
                // observation per timed invocation while Rendering writes it.
                m_poll_handoff = !platform::filesystem::loadFileInto(
                    "development/logical-roots/test-output/bundle-rendering-handoff.bin",
                    CByteView{ reinterpret_cast<std::uint8_t*>(&m_handoff), sizeof(m_handoff), alignof(SHandoff) });
                if (m_poll_handoff) return true;
                if (!m_handoff.original || !m_handoff.file)
                {
                    return false;
                }
                ++m_stage;
                return true;
            }
            case 2u:
            {
                return dispose(m_handoff.original, EAssetStatus::invalid_request);
            }
            case 3u:
            {
                return reference(m_handoff.original, EAssetStatus::success);
            }
            case 4u:
            {
                return m_result.bundle_view().is_ready() && dispose(m_result.asset);
            }
            case 5u:
            {
                return load(m_file, "bundle-test:/Aligned");
            }
            case 6u:
            {
                return (m_file.asset == m_handoff.file) && module(false);
            }
            case 7u:
            {
                return reference(m_handoff.original, EAssetStatus::invalid_asset);
            }
            case 8u:
            {
                return bake(m_result, EAssetStatus::busy);
            }
            case 9u:
            {
                return save(m_file.asset, "test-output:/bundle-rendering-survivor.bin");
            }
            case 10u:
            {
                return dispose(m_file.asset);
            }
            case 11u:
            {
                return bake(m_original);
            }
            case 12u:
            {
                return close(true);
            }
            default:
            {
                MV_LOG(info, "Bundle fixture: Rendering ownership and post-join sub-file release passed");
                return finish();
            }
        }
    }
    bool workflow() noexcept
    {
        switch (m_stage)
        {
            case 0u:
            {
                return document();
            }
            case 1u:
            {
                return bake(m_original);
            }
            case 2u:
            {
                return inspect_original() && reference(m_original.asset, EAssetStatus::success);
            }
            case 3u:
            {
                return (m_result.asset != m_original.asset) && same_bytes(m_result.byte_view(), m_original.byte_view()) && dispose(m_result.asset);
            }
            case 4u:
            {
                return load(m_result, "dev-source:/BundleInput/Shader.hlsl");
            }
            case 5u:
            {
                const bundle::SLookupResult found = m_original.bundle_view().lookup(CStringView{ "shader.hlsl" });
                return same_bytes(m_result.byte_view(), CByteConstView{
                    static_cast<const std::uint8_t*>(found.bytes.data()), found.bytes.bytes() }) && dispose(m_result.asset);
            }
            case 6u:
            {
                return load(m_file, "bundle-test:/Aligned");
            }
            case 7u:
            {
                return refresh(filesystem::EFilesystemStatus::success);
            }
            case 8u:
            {
                return load(m_result, "BUNDLE-TEST:/ALIGNED");
            }
            case 9u:
            {
                return (m_result.asset == m_file.asset) && load(m_result, "bundle-test:/Aligned",
                    EAssetFileFormat::raw, EAssetStatus::invalid_request, 256u);
            }
            case 10u:
            {
                return load(m_baked, "bundle-test:/Document.baked", EAssetFileFormat::baked);
            }
            case 11u:
            {
                return m_baked.document_view().is_ready() &&
                    (m_baked.byte_view().data() == m_original.bundle_view().lookup(CStringView{ "document.baked" }).bytes.data()) &&
                    load(m_image, "bundle-test:/Image.tga", EAssetFileFormat::tga);
            }
            case 12u:
            case 13u:
            {
                if (!m_image.image_view().is_ready() || !load(m_result, "bundle-test:/Data.json", EAssetFileFormat::json))
                {
                    return false;
                }
                m_reply = EReply::none; // Two fixed operations may complete in either order.
                return true;
            }
            case 14u:
            {
                m_reply = EReply::parsed;
                if (m_parse_received[0] && m_parse_received[1]) ++m_stage;
                else m_waiting = true;
                return true;
            }
            case 15u:
            {
                return save(m_original.asset, k_saved);
            }
            case 16u:
            {
                return dispose(m_baked.asset);
            }
            case 17u:
            {
                if (!same_bytes(m_original.byte_view(), m_snapshot.const_view()))
                {
                    return false;
                }
                m_item.bytes = m_file.byte_view();
                m_item.wait = m_batch->has_runners();
                const threading::SBatchSubmission result = m_batch->submit(&read_bundle, &m_item, 700u);
                m_batch_active = m_batch_queued = result.status == threading::EBatchSubmission::queued;
                if (!m_batch_active && (result.status != threading::EBatchSubmission::completed_inline))
                {
                    return false;
                }
                ++m_stage;
                return true;
            }
            case 18u:
            {
                return close(false);
            }
            case 19u:
            {
                return reference(m_original.asset, EAssetStatus::invalid_asset);
            }
            case 20u:
            {
                return refresh();
            }
            case 21u:
            {
                return bake(m_result, EAssetStatus::busy);
            }
            case 22u:
            {
                return save(m_original.asset, k_saved, EAssetStatus::invalid_asset);
            }
            case 23u:
            {
                return load(m_result, "bundle-test:/Aligned", EAssetFileFormat::raw, EAssetStatus::read_failed);
            }
            case 24u:
            {
                if (m_stop_batch)
                {
                    if (poll_batch_start()) return true;
                    m_stop_ready = true;
                    m_waiting = true;
                    MV_LOG(info, "Bundle fixture: requesting stop with active batch borrow after closure");
                    return true;
                }
                m_item.release.store(true, std::memory_order_release);
                m_reply = EReply::batch;
                if (m_batch_active) m_waiting = true;
                else
                {
                    if (!m_item.valid) return false;
                    ++m_stage;
                }
                return true;
            }
            case 25u:
            {
                return save(m_file.asset, "test-output:/bundle-subfile.bin");
            }
            case 26u:
            {
                return dispose(m_file.asset);
            }
            case 27u:
            {
                return wait_notification();
            }
            case 28u:
            {
                if (!m_item.valid || !m_parsed[0].document_view().check_integrity() || !m_image.image_view().is_ready())
                {
                    return false;
                }
                MV_LOG_RICH(info, "Bundle fixture: batch borrow %s survived closure", m_batch_queued ? "queued" : "inline");
                ++m_stage;
                return true;
            }
            case 29u:
            {
                return dispose(m_parsed[0].asset);
            }
            case 30u:
            {
                if (m_parsed[1].asset != m_parsed[0].asset)
                {
                    return dispose(m_parsed[1].asset);
                }
                ++m_stage;
                return true;
            }
            case 31u:
            {
                return dispose(m_image.asset);
            }
            case 32u:
            {
                return bind(k_saved, EAssetStatus::success, &m_original);
            }
            case 33u:
            case 35u:
            {
                m_closed = m_notified = false;
                return same_bytes(m_original.byte_view(), m_snapshot.const_view()) && close(true);
            }
            case 34u:
            {
                return bake(m_original);
            }
            default:
            {
                MV_LOG(info, "Bundle fixture: persistence, refresh, cache and root reuse passed");
                return finish();
            }
        }
    }
    void expect(AssetResult& target, const EAssetStatus status = EAssetStatus::success) noexcept
    {
        m_target = &target;
        m_expected = status;
        m_reply = EReply::asset;
    }
    template<typename T> T* owner() noexcept
    {
        m_owner = threading::make_owner_message<T>(++m_slot);
        m_pending = true;
        m_closing = false;
        return m_owner.owner().payload<T>();
    }
    template<typename T> void pod(const T& value, const EReply reply) noexcept
    {
        m_pod = threading::make_pod_message(++m_slot, value);
        m_pending = true;
        m_reply = reply;
        m_closing = false;
    }
    bool bake(AssetResult& target, const EAssetStatus status = EAssetStatus::success,
        const char* const directory = "BundleInput") noexcept
    {
        BundleBakeRequest* const request = owner<BundleBakeRequest>();
        expect(target, status);
        return request && request->paths.source_root.set("dev-source:") && request->paths.subdirectory.set(directory) &&
            request->paths.root.set(k_root);
    }
    bool load(AssetResult& target, const char* const file, const EAssetFileFormat format = EAssetFileFormat::raw,
        const EAssetStatus status = EAssetStatus::success, const std::size_t alignment = 16u) noexcept
    {
        AssetLoadRequest* const request = owner<AssetLoadRequest>();
        expect(target, status);
        if (!request || !request->file.set(file))
        {
            return false;
        }
        request->format = format;
        request->alignment = alignment;
        return true;
    }
    bool bind(const char* const file, const EAssetStatus status, AssetResult* const target = nullptr) noexcept
    {
        BundleLoadRequest* const request = owner<BundleLoadRequest>();
        expect(target ? *target : m_result, status);
        return request && request->paths.file.set(file) && request->paths.root.set(k_root);
    }
    bool save(const CAssetId asset, const char* const path, const EAssetStatus status = EAssetStatus::success) noexcept
    {
        AssetSaveRequest* const request = owner<AssetSaveRequest>();
        expect(m_result, status);
        if (!request || !request->file.set(path))
        {
            return false;
        }
        request->source = asset;
        request->settings.format = EAssetFileFormat::raw;
        return true;
    }
    bool handoff(const char* const path = "test-output:/bundle-handoff.bin") noexcept
    {
        RawAssetTransfer* const request = owner<RawAssetTransfer>();
        expect(m_result);
        if (!request || !request->storage.file.set(path) || !request->storage.value.resize(sizeof(SHandoff)))
        {
            return false;
        }
        const SHandoff value{ m_original.asset, m_file.asset };
        std::memcpy(request->storage.value.data(), &value, sizeof(value));
        request->save = true;
        request->retention = EAssetRetention::discard;
        request->settings.format = EAssetFileFormat::raw;
        return true;
    }
    bool dispose(const CAssetId asset, const EAssetStatus status = EAssetStatus::success) noexcept
    {
        m_disposing = asset;
        m_disposal_status = status;
        pod(AssetDisposeRequest{ asset }, EReply::disposal);
        return true;
    }
    bool close(const bool wait) noexcept
    {
        pod(AssetDisposeRequest{ m_original.asset }, wait ? EReply::notification : EReply::none);
        m_closing = true;
        return true;
    }
    bool reference(const CAssetId asset, const EAssetStatus expected) noexcept
    {
        pod(BundleReferenceRequest{ asset }, EReply::asset);
        expect(m_result, expected);
        return true;
    }
    bool refresh(const filesystem::EFilesystemStatus status = filesystem::EFilesystemStatus::invalid_root) noexcept
    {
        filesystem::FilesystemRefreshRequest* const request = owner<filesystem::FilesystemRefreshRequest>();
        m_reply = EReply::refresh;
        m_refresh_status = status;
        return request && request->root.set(k_root);
    }
    bool finish() noexcept
    {
        if (!quiescent()) return false;
        clear_views(); // No module-local borrowed view survives domain completion.
        if ((m_memory->get_live_allocation_count() != m_allocations) || (m_memory->get_live_allocated_bytes() != m_bytes))
        {
            return false;
        }
        m_complete = true;
        if (m_scenario == EScenario::rendering) MV_LOG(info, "Bundle fixture: Rendering allocation baseline restored");
        else MV_LOG(info, "Bundle fixture: Executive allocation baseline restored");
        return true;
    }
    bool prepare() noexcept
    {
        if (m_scenario == EScenario::rendering)
        {
            return rendering();
        }
        if (m_scenario == EScenario::rendering_client)
        {
            return rendering_client();
        }
        if (m_scenario == EScenario::workflow)
        {
            return workflow();
        }
        if (m_scenario == EScenario::failures)
        {
            switch (m_stage)
            {
                case 0u:
                {
                    return bind("dev-source:/BundleInput/Data.json", EAssetStatus::conditioning_failed);
                }
                case 1u:
                {
                    return bind("dev-source:/missing.bdl", EAssetStatus::read_failed);
                }
                case 2u:
                {
                    return bake(m_result, EAssetStatus::invalid_request, "BundleInput/Empty");
                }
                case 3u:
                {
                    return bake(m_result, EAssetStatus::read_failed, "BundleMissing");
                }
                case 4u:
                {
                    return bake(m_original);
                }
                case 5u:
                {
                    return close(true);
                }
                default:
                {
                    return finish();
                }
            }
        }
        if (m_scenario == EScenario::resume)
        {
            switch (m_stage)
            {
                case 0u:
                {
                    CSimpleString native;
                    return host_test_paths::native_output("bundle-handoff.bin", native) && load(m_result, native.cstring());
                }
                case 1u:
                {
                    if (m_result.byte_view().size() != sizeof(m_handoff))
                    {
                        return false;
                    }
                    std::memcpy(&m_handoff, m_result.byte_view().data(), sizeof(m_handoff));
                    return m_handoff.original && m_handoff.file && dispose(m_result.asset);
                }
                case 2u:
                {
                    return refresh();
                }
                case 3u:
                {
                    return reference(m_handoff.original, EAssetStatus::invalid_asset);
                }
                case 4u:
                {
                    return bake(m_result, EAssetStatus::busy);
                }
                case 5u:
                {
                    return save(m_handoff.file, "test-output:/bundle-replacement.bin");
                }
                case 6u:
                {
                    return dispose(m_handoff.file);
                }
                case 7u:
                {
                    return bake(m_original);
                }
                case 8u:
                {
                    return close(true);
                }
                default:
                {
                    MV_LOG(info, "Bundle fixture: replacement released old storage without inheriting its notification");
                    return finish();
                }
            }
        }
        if (m_stage == 0u) return bake(m_original);
        if (m_stage == 1u)
        {
            return m_original.bundle_view().is_ready() && load(m_file, "bundle-test:/Aligned");
        }
        if (m_scenario == EScenario::shutdown)
        {
            return finish();
        }
        if (m_scenario == EScenario::replace)
        {
            switch (m_stage)
            {
                case 2u:
                {
                    return handoff();
                }
                case 3u:
                {
                    return close(false);
                }
                case 4u:
                {
                    return reference(m_original.asset, EAssetStatus::invalid_asset);
                }
                default:
                {
                    return select_case("bundle-resume") && finish();
                }
            }
        }
        switch (m_stage)
        {
            case 2u:
            {
                pod(BundleReferenceRequest{ m_original.asset }, EReply::none);
                return true;
            }
            case 3u:
            {
                return close(false);
            }
            case 4u:
            {
                return reference(m_original.asset, EAssetStatus::invalid_asset);
            }
            case 5u:
            {
                // The later refusal proves closure before the accepted complete
                // view is inspected, including a result delivered before closure.
                return m_reference.bundle_view().is_ready() && dispose(m_file.asset);
            }
            case 6u:
            {
                return dispose(m_reference.asset);
            }
            case 7u:
            {
                m_reply = EReply::notification;
                if (m_notified) ++m_stage;
                else m_waiting = true;
                return true;
            }
            default:
            {
                return finish();
            }
        }
    }
};

}   // namespace module_lifecycle_tests
