
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_schema_process.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Authenticated schema processing, acquired publication and conditioning retirement.

#include "runtime/asset_service.hpp"

namespace host
{

using Status = ESchemaProcessStatus;
using Reason = ESchemaAdmissionReason;

static bool within_service_limits(const schema::SDataOperationLimits& limits, SSchemaProcessFailure& failure) noexcept
{
    constexpr schema::SDataOperationLimits maximum;
    using Limit = schema::EDataOperationLimit;
    struct SCheck { Limit kind; std::uint64_t requested, allowed; };
    const SCheck checks[]{
        { Limit::inspected_source_bytes, limits.inspected_source_bytes, maximum.inspected_source_bytes },
        { Limit::owned_bytes, limits.owned_bytes, maximum.owned_bytes },
        { Limit::result_bytes, limits.result_bytes, maximum.result_bytes },
        { Limit::work_credits, limits.work_credits, maximum.work_credits },
        { Limit::document_nodes, limits.document_nodes, maximum.document_nodes },
        { Limit::records, limits.records, maximum.records },
        { Limit::depth, limits.depth, maximum.depth }
    };
    for (const SCheck& check : checks)
    {
        if (check.requested > check.allowed)
        {
            failure.reason = static_cast<std::uint16_t>(Reason::limits);
            failure.limit = check.kind;
            failure.requested = check.requested;
            failure.allowed = check.allowed;
            return false;
        }
    }
    return true;
}

void CAssetService::validate_process(SProcess& process, const SchemaProcessRequest& request) noexcept
{
    auto& work = process.work;
    work.plan = { request.operation, request.output_kind, request.input_count,
        { request.types_source, request.instances_source, request.data_source } };
    if (!work.plan.valid())
    {
        process.result.failure.reason = static_cast<std::uint16_t>(Reason::assembly_plan);
        process.outcome = Status::invalid_request;
        return;
    }
    if (!within_service_limits(request.limits, process.result.failure))
    {
        process.outcome = Status::invalid_request;
        return;
    }
    work.limits = request.limits;
    work.deadline = process.deadline = request.deadline;
    work.restriction = process.client->host_execution.restriction;
    const auto& anchor = request.deadline;
    if (anchor.generation)
    {
        const auto status = process.budget.resume(process.client->host_execution.restriction, anchor);
        if (status == modules::EBudgetStatus::failed) process.outcome = Status::invalid_request;
        else if (status == modules::EBudgetStatus::expired) process.outcome = Status::expired;
        if (process.outcome != Status::success)
        {
            process.result.failure.reason = static_cast<std::uint16_t>(Reason::deadline);
            return;
        }
    }
    else if (anchor.duration_ns || anchor.eligible_start_ns || anchor.awake_start_ns)
    {
        process.result.failure.reason = static_cast<std::uint16_t>(Reason::deadline);
        process.outcome = Status::invalid_request;
        return;
    }
    for (std::uint32_t i = 0u; i < 3u; ++i)
    {
        const auto& input = request.inputs.slots[i];
        const bool empty = !input.document.is_ready() && !input.document.memory_attribution().allocation_size;
        if (i >= request.input_count)
        {
            if (empty && !input.asset && (input.kind == ESchemaInput::none))
            {
                continue;
            }
        }
        else if (input.kind == ESchemaInput::owned_document)
        {
            if (!input.asset && input.document.is_ready())
            {
                work.sources[i].live = &input.document;
                continue;
            }
        }
        else if ((input.kind == ESchemaInput::retained_document) && empty && input.asset)
        {
            const CAssetRecord* const record = m_assets.resolve(input.asset);
            const auto* const asset = record ? record->payload<SchemaOutputAsset>() : nullptr;
            if (asset && (asset->descriptor->kind == schema::ESchemaOutput::schema_document) &&
                protect_asset(*record, process.pins[i], true))
            {
                work.sources[i].retained = asset->document;
                continue;
            }
            process.outcome = Status::invalid_source;
            process.result.failure.reason = static_cast<std::uint16_t>(Reason::retained_source);
            process.result.failure.input = static_cast<std::int8_t>(i);
            return;
        }
        process.outcome = Status::invalid_request;
        process.result.failure.reason = static_cast<std::uint16_t>(Reason::input);
        process.result.failure.input = static_cast<std::int8_t>(i);
        return;
    }
    process.acquisition = m_next_acquisition ? m_acquisitions.emplace() : -1;
    process.backing = m_schema_outputs.emplace();
    if ((process.acquisition < 0) || (process.backing < 0))
    {
        process.outcome = Status::allocation_failed;
        return;
    }
    SOutputBacking& backing = *m_schema_outputs.get_object(process.backing);
    SAcquisition& acquisition = *m_acquisitions.get_object(process.acquisition);
    acquisition.identity = CAssetAcquisitionId{ m_next_acquisition++ };
    backing.references.open = false;
    CAssetBackingLease lease;
    CErasedOwner owner = CErasedOwner::create<SchemaOutputAsset>();
    auto* const carrier = owner.payload<SchemaOutputAsset>();
    if (carrier && lease.acquire(backing.references, EAssetBackingRole::complete))
    {
        *carrier = { &backing.block, &backing.descriptor };
        backing.asset = m_assets.insert(std::move(owner), std::move(lease));
    }
    if (!backing.asset || !acquisition.backing.acquire(backing.references, EAssetBackingRole::acquisition))
    {
        process.outcome = Status::allocation_failed;
        return;
    }
    work.output = &backing.block;
}

bool CAssetService::post_internal_process(threading::CErasedOwnerMsg& message, SClientRecord& client) noexcept
{
    if (!m_conditioning || !message.is_message_a<SchemaProcessRequest>() || !schema_ingress_available() ||
        !client.reserve_replies(2u))
    {
        return false;
    }
    request_process(message, client, false, m_workers_stopped);
    return true;
}

void CAssetService::request_process(threading::CErasedOwnerMsg& message, SClientRecord& client,
    const bool closed, const bool workers_unavailable) noexcept
{
    std::size_t index{};
    while ((index < 4u) && m_processes[index].client) ++index;
    MV_ASSERT(!m_processes[index].client);
    SProcess& process = m_processes[index];
    m_process_stop[index].store(false, std::memory_order_relaxed);
    process.work.stop = &m_process_stop[index];
    process.client = &client;
    process.slot = message.query_async_slot();
    process.work.envelope = message.take_owner();
    process.work.parent = memory::get_ambient_memory_context();
    process.work.client = client.identity.value;
    const auto* const request = process.work.envelope.payload<SchemaProcessRequest>();
    if (request) process.result.request_id = process.id = request->request_id;
    process.work.request = process.id.value;
    const bool fresh = process.id.value > client.last_schema_request;
    if (fresh) client.last_schema_request = process.id.value;
    if (!request || (request->contract_version != 1u) || (process.slot < 0) || !fresh)
    {
        process.outcome = Status::invalid_request;
        const Reason reason = !request ? Reason::envelope : ((request->contract_version != 1u) ?
            Reason::version : ((process.slot < 0) ? Reason::slot : Reason::request_id));
        process.result.failure.reason = static_cast<std::uint16_t>(reason);
    }
    else
    {
        if (workers_unavailable || m_workers_stopped || !m_conditioning || m_conditioning->pod_delivery_poisoned())
        {
            process.outcome = Status::delivery_failed;
        }
        else if (closed || (index == 4u)) process.outcome = Status::busy;
        else validate_process(process, *request);
    }
    progress_processes();
}

void CAssetService::cancel_process(const SchemaProcessCancel& cancel, SClientRecord& client) noexcept
{
    if ((cancel.contract_version != 1u) || !cancel.request_id.value)
    {
        return;
    }
    for (auto& slot : m_processes)
    {
        if (slot.client && (slot.client == &client) && (slot.id.value == cancel.request_id.value) &&
            (slot.phase != EProcessPhase::accepting) && (slot.phase != EProcessPhase::retiring) &&
            (slot.outcome == Status::success))
        {
            slot.outcome = Status::cancelled;
            slot.work.stop->store(true, std::memory_order_release);
        }
    }
}

bool CAssetService::observe_process(SProcess& process) noexcept
{
    if (process.outcome != Status::success) return true;
    const SClientRecord& client = *process.client;
    if (client.stop_requested || client.terminal() || (client.phase == EClientPhase::stopping) ||
        (client.phase == EClientPhase::retired))
    {
        process.outcome = Status::cancelled;
    }
    else if (process.deadline.generation)
    {
        const auto status = process.budget.observe().status;
        if (status == modules::EBudgetStatus::retry)
        {
            return false;
        }
        if (status == modules::EBudgetStatus::expired) process.outcome = Status::expired;
        else if (status == modules::EBudgetStatus::failed) process.outcome = Status::invalid_request;
    }
    if (process.outcome != Status::success) process.work.stop->store(true, std::memory_order_release);
    return true;
}

void CAssetService::progress_processes() noexcept
{
    for (std::int32_t index = 0; index < 5; ++index)
    {
        if (!m_processes[index].client) continue;
        SProcess& process = m_processes[index];
        const bool observed = observe_process(process);
        SClientRecord& client = *process.client;
        if (process.phase == EProcessPhase::accepting)
        {
            if (client.thread && client.thread->pod_delivery_poisoned())
            {
                process.outcome = Status::delivery_failed;
                client.request_exit();
                m_failed = true;
            }
            if (process.outcome == Status::success)
            {
                if (client.delivery_protected() || !observed)
                {
                    continue;
                }
                const auto published = client.publish_reply(threading::make_pod_message(process.slot,
                    SchemaProcessAccepted{ process.id }));
                if (client.thread && client.thread->pod_delivery_poisoned())
                {
                    if (published == EReplyPublication::busy) client.cancel_reservation(1u);
                    client.request_exit();
                    m_failed = true;
                    process.outcome = Status::delivery_failed;
                }
                else if (published == EReplyPublication::busy) continue;
                else if (published == EReplyPublication::cancelled) process.outcome = Status::cancelled;
                else process.accepted = true;
            }
            else client.cancel_reservation(1u);
            process.phase = EProcessPhase::dispatch;
        }
        if (process.phase == EProcessPhase::dispatch)
        {
            if (process.outcome != Status::success) process.work.stop->store(true, std::memory_order_release);
            if (m_workers_stopped || !m_conditioning)
            {
                process.work.envelope.destroy();
                process.phase = EProcessPhase::publishing;
                if (process.outcome == Status::success) process.outcome = Status::delivery_failed;
                for (auto& pin : process.pins) pin.reset();
            }
            else if (post_conditioning(threading::make_pod_message(index, SchemaProcessWork{ &process.work })))
            {
                process.phase = EProcessPhase::executing;
            }
            else if (m_conditioning->pod_delivery_poisoned())
            {
                // Publication may have preceded the queue fault. Drain proves retirement.
                process.phase = EProcessPhase::executing;
                process.outcome = Status::delivery_failed;
                process.work.stop->store(true, std::memory_order_release);
                m_conditioning->request_exit();
                m_failed = true;
            }
        }
        if (process.phase == EProcessPhase::publishing) publish_process(process);
        if ((process.phase == EProcessPhase::retiring) &&
            ((process.backing < 0) || !m_schema_outputs.get_object(process.backing)))
        {
            m_processes[index] = SProcess{};
        }
    }
}

void CAssetService::publish_process(SProcess& process) noexcept
{
    SClientRecord& client = *process.client;
    if (client.delivery_protected() || !observe_process(process))
    {
        return;
    }
    auto* const backing = (process.backing >= 0) ? m_schema_outputs.get_object(process.backing) : nullptr;
    auto* const acquisition = (process.acquisition >= 0) ? m_acquisitions.get_object(process.acquisition) : nullptr;
    auto result = process.result;
    result.status = process.outcome;
    if (result.status == Status::success)
    {
        backing->descriptor.kind = process.work.plan.output;
        backing->descriptor.bytes = static_cast<const CBakedDocumentBlock&>(backing->block).buffer_bytes();
        result = SchemaProcessResult{ process.id, backing->descriptor.kind,
            { backing->asset, acquisition->identity, &backing->descriptor } };
    }
    if ((result.status != Status::success) && !result.failure.reason)
    {
        result.failure.stage = process.accepted ? ESchemaProcessStage::publication : ESchemaProcessStage::admission;
        const Reason reason = (result.status == Status::cancelled) ? Reason::cancellation :
            ((result.status == Status::expired) ? Reason::elapsed : ((result.status == Status::busy) ? Reason::capacity :
((result.status == Status::allocation_failed) ? Reason::allocation :
            ((result.status == Status::invalid_request) ? Reason::deadline : Reason::transport))));
        result.failure.reason = static_cast<std::uint16_t>(reason);
    }
    const bool already_poisoned = client.thread && client.thread->pod_delivery_poisoned();
    EReplyPublication publication = EReplyPublication::cancelled;
    if (already_poisoned)
    {
        client.cancel_reservation(1u);
        client.request_exit();
        m_failed = true;
    }
    else publication = client.publish_reply(threading::make_pod_message(process.slot, result));
    const bool ambiguous = !already_poisoned && client.thread && client.thread->pod_delivery_poisoned();
    if (ambiguous)
    {
        if (publication == EReplyPublication::busy) client.cancel_reservation(1u);
        client.request_exit();
        m_failed = true;
    }
    const bool exposed = (result.status == Status::success) && (ambiguous || (publication == EReplyPublication::queued));
    if (exposed)
    {
        acquisition->client = &client;
        acquisition->source = backing->asset;
        acquisition->schema_output = true;
        backing->references.open = true;
        backing->ambiguous_client = ambiguous ? &client : nullptr;
        backing->private_output = false;
        process.backing = process.acquisition = -1;
    }
    else
    {
        if (!ambiguous && (publication == EReplyPublication::busy))
        {
            return;
        }
        if (backing && backing->asset)
        {
            (void)m_assets.erase(backing->asset);
            backing->asset = {};
        }
        if (result.status == Status::success)
        {
            result = process.result;
            result.status = already_poisoned ? Status::delivery_failed : Status::cancelled;
            result.failure.stage = ESchemaProcessStage::publication;
            result.failure.reason = static_cast<std::uint16_t>(already_poisoned ? Reason::transport : Reason::cancellation);
        }
        MV_LOG(info, "Host schema client {} request {} terminal status {} stage {} reason {} input {} limit {}",
            client.identity.value, process.id.value, static_cast<unsigned>(result.status),
static_cast<unsigned>(result.failure.stage), static_cast<unsigned>(result.failure.reason), static_cast<int>(result.failure.input),
            static_cast<unsigned>(result.failure.limit));
        if (acquisition) (void)m_acquisitions.erase(process.acquisition);
        if (backing) backing->private_output = false;
    }
    process.phase = EProcessPhase::retiring;
}

void CAssetService::complete_process(const SchemaProcessDone& done, const std::int32_t slot,
    threading::CThreadPackage& worker) noexcept
{
    if (&worker != m_conditioning)
    {
        m_failed = true;
        return;
    }
    if (done.reclaimed)
    {
        auto* const backing = m_schema_outputs.get_object(slot);
        if (!backing || !backing->reclaiming)
        {
            m_failed = true;
            return;
        }
        for (auto& process : m_processes)
        {
            if (process.client && (process.phase == EProcessPhase::retiring) && (process.backing == slot)) process = SProcess{};
        }
        (void)m_schema_outputs.erase(slot);
        return;
    }
    if ((slot < 0) || (slot >= 5) || !m_processes[slot].client || (m_processes[slot].phase != EProcessPhase::executing))
    {
        m_failed = true;
        return;
    }
    SProcess& process = m_processes[slot];
    for (auto& pin : process.pins) pin.reset();
    if (process.outcome == Status::success)
    {
        process.outcome = process.work.status;
        process.result.failure = process.work.failure;
    }
    process.phase = EProcessPhase::publishing;
    progress_processes();
}

void CAssetService::progress_schema_outputs() noexcept
{
    for (std::int32_t i = m_schema_outputs.first_live(); i >= 0;)
    {
        const std::int32_t next = m_schema_outputs.next_live(i);
        SOutputBacking& backing = *m_schema_outputs.get_object(i);
        if (backing.ambiguous_client && backing.ambiguous_client->execution_quiescent())
        {
            backing.ambiguous_client = nullptr;
            if (backing.asset && !disposal_pending(backing.asset))
            {
                (void)m_assets.erase(backing.asset);
                backing.asset = {};
            }
        }
        if (!backing.private_output && !backing.references.records && !backing.references.operations && !backing.reclaiming)
        {
            if (m_workers_stopped)
            {
                for (auto& process : m_processes)
                {
                    if (process.client && (process.phase == EProcessPhase::retiring) && (process.backing == i)) process = SProcess{};
                }
                (void)m_schema_outputs.erase(i);
            }
            else if (post_conditioning(threading::make_pod_message(i, SchemaProcessWork{ nullptr, &backing.block })))
            {
                backing.reclaiming = true;
            }
            else if (m_conditioning && m_conditioning->pod_delivery_poisoned())
            {
                backing.reclaiming = true;
                m_conditioning->request_exit();
                m_failed = true;
            }
        }
        i = next;
    }
}

void CAssetService::fail_processes() noexcept
{
    // Worker terminal states and drained completion queues have already been observed.
    for (auto& process : m_processes)
    {
        if (process.client && (process.phase == EProcessPhase::accepting) && (process.outcome == Status::success))
        {
            process.outcome = Status::delivery_failed;
        }
        if (process.client && (process.phase == EProcessPhase::executing))
        {
            process.outcome = Status::delivery_failed;
            process.phase = EProcessPhase::dispatch;
        }
    }
    for (std::int32_t i = m_schema_outputs.first_live(); i >= 0; i = m_schema_outputs.next_live(i))
    {
        m_schema_outputs.get_object(i)->reclaiming = false;
    }
    progress_processes();
    progress_schema_outputs();
}

void CAssetService::close_schema_outputs() noexcept
{
    for (std::int32_t i = m_schema_outputs.first_live(); i >= 0; i = m_schema_outputs.next_live(i))
    {
        SOutputBacking& backing = *m_schema_outputs.get_object(i);
        if (backing.asset)
        {
            (void)m_assets.erase(backing.asset);
            backing.asset = {};
        }
    }
    progress_schema_outputs();
}

}   // namespace host
