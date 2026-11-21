
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_capture.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Runs bounded native steps between ordinary pumps, freezes one outcome and waits for Host retirement.

#include "runtime/report_capture.hpp"
#include "framework/services/provider_client.hpp"
#include "framework/threading/messages/message_helpers.hpp"

#include <algorithm>

namespace platform::runtime
{

using namespace reports;
using namespace platform_report_v1;

void CReportCapture::fail(const SFailure failure) noexcept
{
    m_failure = failure;
    m_envelope = {};
    m_session.reset();
    m_phase = EPhase::post;
}

void CReportCapture::message(const ::threading::CErasedPodMsg& message) noexcept
{
    PlatformReportCaptureRequest request;
    PlatformReportCaptureCancel cancel;
    PlatformReportCaptureReceipt receipt;
    if (message.copy_payload_to(request))
    {
        if (active() || (request.capture.platform_instance.value != m_context.instance.value) ||
            (request.capture.capture_id <= m_last_id))
        {
            return;
        }
        m_identity = request.capture;
        m_last_id = m_identity.capture_id;
        m_attempts = m_calls = m_raw_modes = 0u;
        m_begin = m_max_call = m_schema_ns = m_max_pump = 0u;
        m_failure = {};
        m_phase = EPhase::start;
        if ((request.deadline.duration_ns != k_capture_limits.eligible_duration_ns) ||
            (m_budget.resume(m_context.execution->restriction, request.deadline) == modules::EBudgetStatus::failed))
        {
            fail({ EStage::capture, EReason::clock_failed });
        }
    }
    else if (message.copy_payload_to(cancel) && runnable() && (cancel.capture.platform_instance.value == m_context.instance.value) &&
        (cancel.capture.capture_id == m_identity.capture_id))
    {
        fail({ EStage::lifecycle, cancel.reason });
    }
    else if (message.copy_payload_to(receipt) && (m_phase == EPhase::receipt) &&
        (receipt.capture.platform_instance.value == m_context.instance.value) && (receipt.capture.capture_id == m_identity.capture_id))
    {
        MV_LOG(info, "Platform report {} receipt {}, calls {}, max native ns {}, schema ns {}, max Platform pump ns {}, peak staged bytes {}",
            m_last_id, static_cast<unsigned>(receipt.disposition), m_calls, m_max_call, m_schema_ns, m_max_pump, m_memory ? m_memory->peak() : 0u);
        m_memory.reset();
        m_identity = {};
        m_phase = EPhase::idle;
    }
}

bool CReportCapture::eligible(const bool stopping) noexcept
{
    if (stopping)
    {
        fail({ EStage::lifecycle, EReason::closing });
        return false;
    }
    const modules::EBudgetStatus time = m_budget.observe().status;
    if (time == modules::EBudgetStatus::expired) fail({ EStage::capture, EReason::deadline_exceeded });
    if (time == modules::EBudgetStatus::failed) fail({ EStage::capture, EReason::clock_failed });
    return time == modules::EBudgetStatus::running;
}

void CReportCapture::build() noexcept
{
    CBakedDocumentBlock frozen;
    schema::SSchemaDiagnostic diagnostic;
    SReportBakeTiming timing{ m_clock };
    std::uint64_t begin{}, end{};
    if (m_clock.read(begin).code != platform::threading::EWaitError::none)
    {
        fail({ EStage::capture, EReason::clock_failed });
        return;
    }
    {
        CReportMemoryScope scope{ m_memory->context() };
        //  Create the block owner inside the capture allocation domain as well.
        CBakedDocumentBlock candidate;
        if (!m_session->candidate.bake(candidate, {}, diagnostic, &timing))
        {
            if (timing.clock_failed)
            {
                fail({ EStage::capture, EReason::clock_failed });
                return;
            }
            MV_LOG(info, "Platform report {} bake rejected, stage {}, reason {}, instance {}, collection {}, operation {}",
                m_last_id, static_cast<unsigned>(diagnostic.stage), static_cast<unsigned>(diagnostic.reason),
                static_cast<unsigned>(diagnostic.instances.reason), static_cast<unsigned>(diagnostic.bulk_data.reason),
                static_cast<unsigned>(diagnostic.operation.reason));
            fail({ EStage::capture, (m_memory->limited() || (diagnostic.operation.reason == schema::EDataOperationReason::limit_exceeded)) ?
                EReason::limit_exceeded : ((m_memory->allocation_failed() || (diagnostic.operation.reason == schema::EDataOperationReason::allocation_failed)) ?
                    EReason::allocation_failed : EReason::invalid_data) });
            return;
        }
        frozen = std::move(candidate);
    }
    m_session.reset();
    if ((m_clock.read(end).code != platform::threading::EWaitError::none) || (end < begin))
    {
        fail({ EStage::capture, EReason::clock_failed });
        return;
    }
    if ((timing.prepared < begin) || (timing.authored < timing.prepared) || (timing.baked < timing.authored) || (end < timing.baked))
    {
        fail({ EStage::capture, EReason::clock_failed });
        return;
    }
    m_schema_ns = end - begin;
    MV_LOG(info, "Platform report {} schema ns, declarations {}, logical records {}, bake {}, retire staging {}",
        m_last_id, (timing.prepared - begin), (timing.authored - timing.prepared), (timing.baked - timing.authored), (end - timing.baked));
    const CByteConstView bytes = frozen.buffer_bytes().const_view();
    const memory::CMemoryContext* const parent = memory::get_ambient_memory_context();
    const std::size_t copy_size = parent->condition_bytes(bytes.align(), bytes.size());
    const std::size_t envelope_size = parent->condition_bytes(alignof(PlatformReportCaptured), sizeof(PlatformReportCaptured));
    if ((bytes.size() > k_capture_limits.report_bytes) || !copy_size || !envelope_size ||
        !m_memory->reserve(copy_size) || !m_memory->reserve(envelope_size))
    {
        fail({ EStage::transfer, EReason::limit_exceeded });
        return;
    }
    //  The private counting allocator cannot escape to Host. Freeze the exact
    //  range in ordinary module storage compatible with the owning transport.
    m_envelope = ::threading::make_owner_message<PlatformReportCaptured>(-1);
    auto* const offer = m_envelope.owner().payload<PlatformReportCaptured>();
    CByteBuffer transfer;
    if (!offer || !transfer.construct_and_copy_from(bytes) || !offer->storage.adopt_buffer(std::move(transfer)))
    {
        fail({ EStage::transfer, EReason::allocation_failed });
        return;
    }
    offer->capture = m_identity;
    m_phase = EPhase::post;
}

void CReportCapture::observe_pump(const std::uint64_t start) noexcept
{
    if (!active()) return;
    std::uint64_t end{};
    if ((m_clock.read(end).code == platform::threading::EWaitError::none) && (end >= start))
    {
        m_max_pump = std::max(m_max_pump, (end - start));
    }
}

void CReportCapture::progress(const bool stopping, const std::uint64_t share_ns) noexcept
{
    if (!runnable()) return;
    if ((m_failure.reason == EReason::none) && (!eligible(stopping) || !share_ns))
    {
        return;
    }
    if (m_phase == EPhase::start)
    {
        if (!m_memory) m_memory = TInstance<CReportMemory>::create(k_capture_limits.owned_bytes - sizeof(CReportMemory));
        if (!m_memory)
        {
            fail({ EStage::capture, EReason::allocation_failed });
            return;
        }
        CReportMemoryScope scope{ m_memory->context() };
        m_session = TInstance<native::SReportSession>::create();
        if (!m_session)
        {
            fail({ EStage::capture, EReason::allocation_failed });
            return;
        }
        m_session->clock = m_clock;
        m_session->native_calls = m_calls;
        m_session->raw_modes = m_raw_modes;
        m_session->budget = &m_budget;
        m_session->candidate.header.limits = k_capture_limits;
        m_session->candidate.header.schema_limits = schema_limits({});
        ++m_attempts;
        m_phase = EPhase::native;
        return;
    }
    if (m_phase == EPhase::native)
    {
        std::uint64_t before{}, after{};
        if (m_clock.read(before).code != platform::threading::EWaitError::none)
        {
            fail({ EStage::capture, EReason::clock_failed });
            return;
        }
        if (!m_calls) m_begin = before;
        m_session->slice_end_ns = before + std::min(share_ns, UINT64_C(500000));
        native::EReportStep step;
        {
            CReportMemoryScope scope{ m_memory->context() };
            step = m_adapter.step(*m_session);
        }
        m_calls = m_session->native_calls;
        m_raw_modes = m_session->raw_modes;
        m_max_call = std::max(m_max_call, m_session->maximum_call_ns);
        if (!eligible(stopping)) return;
        if ((m_clock.read(after).code != platform::threading::EWaitError::none) || (after < before))
        {
            fail({ EStage::capture, EReason::clock_failed });
            return;
        }
        if (m_memory->limited()) fail({ EStage::capture, EReason::limit_exceeded });
        else if (step == native::EReportStep::failed) fail(m_session->failure);
        else if (step == native::EReportStep::topology_changed)
        {
            m_session.reset();
            if (m_attempts == k_capture_limits.capture_attempts) fail({ EStage::capture, EReason::retry_exhausted });
            else m_phase = EPhase::start;
        }
        else if (step == native::EReportStep::finished)
        {
            PlatformReportHeader& header = m_session->candidate.header;
            header.kind = PlatformReportKind::platform_report;
            header.schema_version = k_report_version;
            header.platform = PlatformBackend::windows;
            header.query_profile = PlatformQueryProfile::windows_desktop_discovery_v1;
            header.clock = PlatformClock::system_awake_ns;
            header.visibility_scope = PlatformVisibilityScope::calling_session;
            header.platform_instance = m_identity.platform_instance.value;
            header.capture_id = m_identity.capture_id;
            header.capture_begin_ns = m_begin;
            header.capture_attempts = m_attempts;
            header.native_calls = m_calls;
            m_phase = EPhase::schema;
        }
        return;
    }
    if (m_phase == EPhase::schema)
    {
        build();
        if (m_failure.reason == EReason::none) (void)eligible(stopping);
        return;
    }
    services::EServiceStatus status;
    if (m_failure.reason != EReason::none)
    {
        const auto message = ::threading::make_pod_message(-1, PlatformReportCaptureFailed{ m_identity, m_failure });
        status = services::submission_status(m_context.endpoint->post(message));
    }
    else
    {
        status = services::submission_status(m_context.endpoint->post(std::move(m_envelope)));
        if ((status == services::EServiceStatus::invalid_request) && m_envelope.has_owner())
        {
            fail({ EStage::transfer, EReason::transport_failed });
            return;
        }
    }
    if (status == services::EServiceStatus::busy) return;
    //  Ambiguous delivery retains bookkeeping. Only a matching receipt or
    //  exceptional Host lifetime retention can settle a potentially exposed offer.
    m_phase = EPhase::receipt;
}

}   //  namespace platform::runtime
