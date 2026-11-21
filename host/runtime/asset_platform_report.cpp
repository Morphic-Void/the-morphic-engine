
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_platform_report.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Admits, publishes and retires Host-owned Platform snapshots on the Host owner thread.

#include "runtime/asset_service.hpp"
#include "runtime/pumped_client.hpp"
#include "framework/platform/report.hpp"
#include "framework/platform/report_memory.hpp"
#include "framework/threading/messages/message_helpers.hpp"

#include <cstring>

namespace host
{

using namespace platform::reports;

static EStatus failure_status(const SFailure failure) noexcept
{
    switch (failure.reason)
    {
        case EReason::allocation_failed:
        {
            return EStatus::allocation_failed;
        }
        case EReason::closing:
        {
            return EStatus::interrupted;
        }
        case EReason::transport_failed:
        {
            return EStatus::delivery_failed;
        }
        default:
        {
            return EStatus::failed;
        }
    }
}

PlatformReportResult CAssetService::report_state() const noexcept
{
    PlatformReportResult result;
    const bool installed = m_report_provider && !m_report_closing && !m_report_provider->stop_requested &&
        (m_report_provider->phase == EClientPhase::ready);
    result.availability = installed ? ((m_current_report >= 0) ? EAvailability::available : EAvailability::not_ready) :
        EAvailability::unavailable;
    result.update_in_progress = installed && (m_initial_report || m_capture.identity.capture_id);
    result.last_update = installed ? m_report_last : ELastUpdate::none;
    result.failure = installed ? m_report_failure : SFailure{};
    return result;
}

void CAssetService::install_report_provider(SClientRecord& client) noexcept
{
    m_report_provider = &client;
    m_report_last = ELastUpdate::none;
    m_report_failure = {};
    m_initial_report = true;
    m_report_closing = false;
}

void CAssetService::close_report_provider(SClientRecord& client) noexcept
{
    if (m_report_provider != &client) return;
    m_report_closing = true;
    m_initial_report = false;
    m_current_report = -1;
    if (!m_capture.identity.capture_id)
    {
        m_report_provider = nullptr;
    }
}

SFailure CAssetService::start_report(const std::int32_t delivery) noexcept
{
    SClientRecord& producer = *m_report_provider;
    if (!m_next_capture)
    {
        return { EStage::capture, EReason::limit_exceeded };
    }
    const modules::EBudgetStatus started = m_capture.budget.start(producer.host_execution.restriction,
        k_capture_limits.eligible_duration_ns);
    if ((started != modules::EBudgetStatus::running) && (started != modules::EBudgetStatus::restricted))
    {
        return { EStage::capture, EReason::clock_failed };
    }
    const std::int32_t backing = m_report_backings.emplace();
    if (backing < 0)
    {
        return { EStage::publication, EReason::allocation_failed };
    }
    if (!producer.reserve_replies(2u))
    {
        (void)m_report_backings.erase(backing);
        return { EStage::transfer, EReason::limit_exceeded };
    }
    if (!producer.reserve_replies(1u))
    {
        producer.cancel_reservation(2u);
        (void)m_report_backings.erase(backing);
        return { EStage::transfer, EReason::limit_exceeded };
    }
    m_capture.identity = { producer.instance, m_next_capture++ };
    m_capture.delivery = delivery;
    m_capture.backing = backing;
    const PlatformReportCaptureRequest request{ m_capture.identity, m_capture.budget.anchor() };
    if (!producer.reply(threading::make_pod_message(-1, request)))
    {
        if (producer.endpoint->host_to_worker_msgs.posting_poisoned())
        {
            m_capture.delivery = -1;
            m_capture.failure = { EStage::transfer, EReason::transport_failed };
            m_failed = true;
        }
        else retire_capture();
        return { EStage::transfer, EReason::transport_failed };
    }
    return {};
}

bool CAssetService::grant_report(SReportDelivery& delivery, SReportBacking& backing) noexcept
{
    SAcquisition& acquisition = *m_acquisitions.get_object(delivery.acquisition_slot);
    if (!acquisition.backing.acquire(backing.references, EAssetBackingRole::acquisition))
    {
        return false;
    }
    delivery.result.status = EStatus::success;
    delivery.result.acquisition = delivery.token;
    delivery.result.bytes = backing.block.file_bytes();
    return true;
}

void CAssetService::request_report(SClientRecord& client, const std::int32_t slot, const bool update, const bool closed) noexcept
{
    PlatformReportResult result = report_state();
    if (slot < 0)
    {
        result.status = EStatus::invalid_request;
        result.failure = { EStage::validation, EReason::invalid_data };
    }
    else if (closed || client.terminal() || client.stop_requested)
    {
        result.status = EStatus::interrupted;
        result.failure = { EStage::lifecycle, EReason::closing };
    }
    else if (result.availability == EAvailability::unavailable) result.status = EStatus::unavailable;
    else if (update && result.update_in_progress)
    {
        result.status = EStatus::update_in_progress;
        result.failure = {};
    }
    else if (!update && (result.availability == EAvailability::not_ready)) result.status = EStatus::not_ready;
    else result.status = EStatus::success;

    const std::int32_t index = m_report_deliveries.emplace();
    if (index < 0)
    {
        result.status = EStatus::allocation_failed;
        result.failure = { EStage::publication, EReason::allocation_failed };
        if (!client.reply(threading::make_pod_message(slot, result))) m_failed = true;
        return;
    }
    SReportDelivery& delivery = *m_report_deliveries.get_object(index);
    delivery.client = &client;
    delivery.slot = slot;
    delivery.result = result;
    delivery.ready = true;
    if (result.status != EStatus::success) return;

    delivery.acquisition_slot = m_next_acquisition ? m_acquisitions.emplace() : -1;
    SFailure failure{ EStage::publication, EReason::allocation_failed };
    if (delivery.acquisition_slot >= 0)
    {
        delivery.token = CAssetAcquisitionId{ m_next_acquisition++ };
        if (update)
        {
            failure = start_report(index);
            if (failure.reason == EReason::none)
            {
                delivery.ready = false;
                return;
            }
        }
        else if (grant_report(delivery, *m_report_backings.get_object(m_current_report)))
        {
            return;
        }
    }
    delivery.result.status = failure_status(failure);
    delivery.result.failure = failure;
}

bool CAssetService::report_matches(const SClientRecord& client, const SCaptureIdentity identity) const noexcept
{
    return (m_report_provider == &client) && (identity.platform_instance.value == client.instance.value) &&
        identity.capture_id && (identity.capture_id == m_capture.identity.capture_id) && !m_capture.outcome && !m_capture.settled;
}

void CAssetService::report_candidate(SClientRecord& client, threading::CErasedOwnerMsg& message) noexcept
{
    const PlatformReportCaptured* const offered = message.owner().payload<PlatformReportCaptured>();
    if (!offered || !report_matches(client, offered->capture))
    {
        return;
    }
    m_capture.candidate = message.take_owner();
    m_capture.outcome = true;
}

void CAssetService::report_failed(SClientRecord& client, const PlatformReportCaptureFailed& outcome) noexcept
{
    if (!report_matches(client, outcome.capture)) return;
    if (m_capture.failure.reason == EReason::none)
    {
        m_capture.failure = outcome.failure;
        if ((outcome.failure.stage == EStage::none) || (outcome.failure.reason == EReason::none) ||
            (outcome.failure.stage > EStage::lifecycle) || (outcome.failure.reason > EReason::unsupported_backend) ||
            (outcome.failure.domain > EErrorDomain::hresult) ||
            ((outcome.failure.domain == EErrorDomain::none) && outcome.failure.native_error))
        {
            m_capture.failure = { EStage::validation, EReason::invalid_data };
        }
    }
    m_capture.outcome = true;
}

static bool sample(std::uint64_t& now) noexcept
{
    return platform::threading::query_awake_nanoseconds(now).code == platform::threading::EWaitError::none;
}

static SFailure import_report(const PlatformReportCaptured& offer, schema::CBakedDataBlock& destination,
    std::uint64_t& copy_ns, std::uint64_t& adoption_ns, std::uint64_t& validation_ns, std::uint64_t& peak_bytes) noexcept
{
    const CByteConstView bytes = offer.storage.buffer_bytes();
    if (!bytes.data() || !bytes.size() || (reinterpret_cast<std::uintptr_t>(bytes.data()) & 127u))
    {
        return { EStage::validation, EReason::invalid_data };
    }
    const std::uint64_t incoming = offer.storage.memory_attribution().allocation_size;
    if ((bytes.size() > k_capture_limits.report_bytes) || (incoming > (k_capture_limits.owned_bytes - bytes.size())))
    {
        return { EStage::transfer, EReason::limit_exceeded };
    }
    CReportMemory scratch{ k_capture_limits.owned_bytes };
    const std::size_t allocated = memory::get_ambient_memory_context()->condition_bytes(128u, bytes.size());
    if (!allocated || !scratch.reserve(incoming) || !scratch.reserve(allocated) || !scratch.reserve(sizeof(offer)))
    {
        return { EStage::transfer, EReason::limit_exceeded };
    }
    std::uint64_t begin{}, copied{}, adopted{}, checked{};
    if (!sample(begin))
    {
        return { EStage::transfer, EReason::clock_failed };
    }
    CByteBuffer copy;
    if (!copy.allocate(bytes.size(), 128u) || !copy.set_size(bytes.size()))
    {
        return { EStage::transfer, EReason::allocation_failed };
    }
    std::memcpy(copy.data(), bytes.data(), bytes.size());
    if (!sample(copied) || (copied < begin))
    {
        return { EStage::transfer, EReason::clock_failed };
    }
    copy_ns = copied - begin;
    CBakedDocumentBlock local;
    schema::SDataBlockDiagnostic error;
    CReportMemoryScope scope{ scratch.context() };
    if (!local.adopt_buffer(std::move(copy)) || !destination.adopt_local(std::move(local), error))
    {
        if (scratch.limited())
        {
            return { EStage::validation, EReason::limit_exceeded };
        }
        if (scratch.allocation_failed() || (error.reason == schema::EDataBlockReason::allocation_failed))
        {
            return { EStage::validation, EReason::allocation_failed };
        }
        return { EStage::validation, EReason::invalid_data };
    }
    if (!sample(adopted) || (adopted < copied))
    {
        return { EStage::validation, EReason::clock_failed };
    }
    adoption_ns = adopted - copied;
    schema::SDataOperationLimits limits;
    limits.owned_bytes = std::min(limits.owned_bytes, (k_capture_limits.owned_bytes - scratch.peak()));
    const SFailure result = validate_report(destination, offer.capture, limits);
    peak_bytes = scratch.peak();
    if (!sample(checked) || (checked < adopted))
    {
        return { EStage::validation, EReason::clock_failed };
    }
    validation_ns = checked - adopted;
    if (scratch.limited())
    {
        return { EStage::validation, EReason::limit_exceeded };
    }
    if (scratch.allocation_failed())
    {
        return { EStage::validation, EReason::allocation_failed };
    }
    return result;
}

void CAssetService::settle_report() noexcept
{
    SClientRecord& producer = *m_report_provider;
    SFailure& failure = m_capture.failure;
    SReportBacking& backing = *m_report_backings.get_object(m_capture.backing);
    SReportDelivery* const delivery = (m_capture.delivery >= 0) ? m_report_deliveries.get_object(m_capture.delivery) : nullptr;
    if ((failure.reason == EReason::none) && !backing.block.loaded_ready())
    {
        const auto* const candidate = m_capture.candidate.payload<PlatformReportCaptured>();
        failure = candidate ? import_report(*candidate, backing.block, m_capture.copy_ns, m_capture.adoption_ns,
            m_capture.validation_ns, m_capture.peak_bytes) : SFailure{ EStage::validation, EReason::invalid_data };
        //  Return through the outer Host loop to observe stop and deadline after
        //  synchronous validation and before changing current state.
        if (failure.reason == EReason::none) return;
    }
    if (delivery && !delivery->result.acquisition && (failure.reason == EReason::none) && !grant_report(*delivery, backing))
    {
        failure = { EStage::publication, EReason::allocation_failed };
    }
    if (failure.reason == EReason::none)
    {
        const modules::EBudgetStatus time = m_capture.budget.observe().status;
        if ((time == modules::EBudgetStatus::retry) || (time == modules::EBudgetStatus::restricted))
        {
            return;
        }
        if (time == modules::EBudgetStatus::expired) failure = { EStage::publication, EReason::deadline_exceeded };
        else if (time == modules::EBudgetStatus::failed) failure = { EStage::publication, EReason::clock_failed };
    }
    if (failure.reason == EReason::none)
    {
        m_current_report = m_capture.backing;
        m_capture.backing = -1;
        m_report_last = ELastUpdate::succeeded;
    }
    else
    {
        if (delivery && delivery->result.acquisition)
        {
            m_acquisitions.get_object(delivery->acquisition_slot)->backing.reset();
        }
        m_report_last = ELastUpdate::failed;
    }
    m_report_failure = failure;
    if (delivery)
    {
        const CByteConstView bytes = delivery->result.bytes;
        delivery->result = report_state();
        delivery->result.status = (failure.reason == EReason::none) ? EStatus::success : failure_status(failure);
        delivery->result.failure = failure;
        if (delivery->result.status == EStatus::success)
        {
            delivery->result.bytes = bytes;
            delivery->result.acquisition = delivery->token;
        }
        delivery->ready = true;
        m_capture.delivery = -1;
    }
    std::uint64_t before{}, after{};
    const bool sampled = sample(before);
    m_capture.candidate.destroy();
    if (sampled && sample(after) && (after >= before))
    {
        const std::uint64_t admission = m_capture.budget.anchor().awake_start_ns;
        MV_LOG(info, "Host report {} stage {} reason {}, copy ns {}, adopt ns {}, validate ns {}, retire incoming ns {}, peak bytes {}",
            m_capture.identity.capture_id, static_cast<unsigned>(failure.stage), static_cast<unsigned>(failure.reason),
            m_capture.copy_ns, m_capture.adoption_ns, m_capture.validation_ns, (after - before), m_capture.peak_bytes);
        if (after >= admission)
        {
            MV_LOG(info, "Host report {} admission to settlement awake ns {}", m_capture.identity.capture_id, (after - admission));
        }
    }
    m_capture.settled = true;
    const EDisposition disposition = (failure.reason == EReason::none) ? EDisposition::published :
        ((failure.reason == EReason::closing) ? EDisposition::interrupted : EDisposition::rejected);
    m_capture.receipt = { m_capture.identity, disposition, failure };
}

void CAssetService::retire_capture() noexcept
{
    if (!m_capture.cancel_sent) m_report_provider->cancel_reservation(1u);
    if (!m_capture.receipt_settled) m_report_provider->cancel_reservation(1u);
    if (m_capture.backing >= 0) (void)m_report_backings.erase(m_capture.backing);
    m_capture = SReportCapture{};
    if (m_report_closing) close_report_provider(*m_report_provider);
}

void CAssetService::deliver_reports() noexcept
{
    for (std::int32_t i = m_report_deliveries.first_live(); i >= 0;)
    {
        const std::int32_t slot = i;
        i = m_report_deliveries.next_live(slot);
        SReportDelivery& d = *m_report_deliveries.get_object(slot);
        SClientRecord& client = *d.client;
        if (client.delivery_protected()) continue;
        if (!d.ready && !client.terminal())
        {
            continue;
        }
        if (!d.ready)
        {
            m_capture.delivery = -1;
            client.cancel_reservation(1u);
        }
        else
        {
            const EReplyPublication result = client.publish_reply(threading::make_pod_message(d.slot, d.result));
            const bool poisoned = client.endpoint->host_to_worker_msgs.posting_poisoned();
            if (poisoned)
            {
                if (result == EReplyPublication::busy) client.cancel_reservation(1u);
                client.request_exit();
                m_failed = true;
            }
            else if (result == EReplyPublication::busy)
            {
                continue;
            }
            if ((d.result.status == EStatus::success) && ((result == EReplyPublication::queued) || poisoned))
            {
                SAcquisition& record = *m_acquisitions.get_object(d.acquisition_slot);
                record.identity = d.token;
                record.client = &client;
                d.acquisition_slot = -1;
            }
        }
        if (d.acquisition_slot >= 0) (void)m_acquisitions.erase(d.acquisition_slot);
        (void)m_report_deliveries.erase(slot);
    }
}

void CAssetService::progress_reports(const bool admission_closed) noexcept
{
    deliver_reports();
    for (std::int32_t i = m_report_backings.first_live(); i >= 0;)
    {
        const std::int32_t slot = i;
        i = m_report_backings.next_live(slot);
        if ((slot != m_current_report) && (slot != m_capture.backing) && !m_report_backings.get_object(slot)->references.records)
        {
            std::uint64_t before{}, after{};
            const bool sampled = sample(before);
            const std::size_t bytes = m_report_backings.get_object(slot)->block.file_bytes().size();
            (void)m_report_backings.erase(slot);
            if (sampled && sample(after) && (after >= before))
            {
                MV_LOG(info, "Retired historical Platform report bytes {}, ns {}", bytes, (after - before));
            }
        }
    }
    if (!m_report_provider) return;
    SClientRecord& producer = *m_report_provider;
    if (admission_closed || producer.stop_requested || producer.terminal()) close_report_provider(producer);
    if (m_initial_report)
    {
        m_initial_report = false;
        m_report_failure = start_report(-1);
        if (m_report_failure.reason != EReason::none) m_report_last = ELastUpdate::failed;
    }
    const std::uint64_t id = m_capture.identity.capture_id;
    if (!id || producer.delivery_protected())
    {
        return;
    }
    const bool drained = producer.execution_quiescent();
    if (m_capture.settled)
    {
        if (!m_capture.receipt_settled)
        {
            const EReplyPublication publication = producer.publish_reply(threading::make_pod_message(-1, m_capture.receipt));
            m_capture.receipt_settled = publication != EReplyPublication::busy;
            m_capture.receipt_published = publication == EReplyPublication::queued;
        }
        if (m_capture.receipt_published || (m_capture.receipt_settled && drained)) retire_capture();
        return;
    }
    const modules::EBudgetStatus time = m_capture.budget.observe().status;
    if (m_report_closing) m_capture.failure = { EStage::lifecycle, EReason::closing };
    else if (time == modules::EBudgetStatus::expired) m_capture.failure = { EStage::capture, EReason::deadline_exceeded };
    else if (time == modules::EBudgetStatus::failed) m_capture.failure = { EStage::capture, EReason::clock_failed };
    if (!m_capture.outcome && (m_capture.failure.reason != EReason::none))
    {
        //  The public operation has failed even if producer cleanup is still outstanding.
        m_report_last = ELastUpdate::failed;
        m_report_failure = m_capture.failure;
        if (m_capture.delivery >= 0)
        {
            SReportDelivery& delivery = *m_report_deliveries.get_object(m_capture.delivery);
            delivery.result = report_state();
            delivery.result.status = failure_status(m_capture.failure);
            delivery.result.failure = m_capture.failure;
            delivery.ready = true;
            m_capture.delivery = -1;
        }
    }
    if (drained)
    {
        m_capture.failure = { EStage::lifecycle, EReason::closing };
        m_capture.outcome = true;
    }
    if (m_capture.outcome)
    {
        if ((time == modules::EBudgetStatus::retry) || (time == modules::EBudgetStatus::restricted))
        {
            return;
        }
        settle_report();
        if (drained)
        {
            producer.cancel_reservation(1u);
            m_capture.receipt_settled = true;
            retire_capture();
        }
    }
    else if (!m_capture.cancel_sent && (m_capture.failure.reason != EReason::none))
    {
        const PlatformReportCaptureCancel cancel{ m_capture.identity, m_capture.failure.reason };
        m_capture.cancel_sent = true;
        if (!producer.reply(threading::make_pod_message(-1, cancel))) m_failed = true;
    }
}

}   //  namespace host
