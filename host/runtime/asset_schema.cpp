
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_schema.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Temporary schema result admission, acquired publication and worker retirement.

#include "runtime/asset_service.hpp"
#include "framework/threading/messages/message_helpers.hpp"

namespace host
{

void CAssetService::request_schema(const SchemaPrepareRequest& request, const std::int32_t slot,
    SClientRecord& client) noexcept
{
    SchemaPrepareResult failure;
    std::uint32_t count = 0u;
    for (std::int32_t index = m_schemas.first_live(); index >= 0; index = m_schemas.next_live(index)) ++count;
    CAssetRecord* const source = disposal_pending(request.source) ? nullptr : m_assets.resolve(request.source);
    if (slot < 0) failure.status = EAssetStatus::invalid_request;
    else if (!source) failure.status = EAssetStatus::invalid_asset;
    else if (m_workers_stopped || (m_conditioning && m_conditioning->pod_delivery_poisoned())) failure.status = EAssetStatus::delivery_failed;
    else if ((count >= 4u) || !m_conditioning || !m_next_acquisition) failure.status = EAssetStatus::busy;
    else
    {
        const std::int32_t index = m_schemas.emplace();
        if (index >= 0)
        {
            SSchemaOperation& operation = *m_schemas.get_object(index);
            AssetResult view;
            describe_views(*source, view);
            operation.work.source = view.byte_view();
            if ((view.kind == EAssetKind::raw) && operation.work.source.data() && protect_asset(*source, operation.backing, false))
            {
                operation.source = request.source;
                operation.work.request = request;
                operation.client = &client;
                operation.client_slot = slot;
                operation.acquisition = CAssetAcquisitionId{ m_next_acquisition++ };
                if (post_conditioning(threading::make_pod_message(index, SchemaConditionWork{ &operation.work, false })))
                {
                    return;
                }
                if (m_conditioning->pod_delivery_poisoned())
                {
                    //  A queue growth failure can follow publication. Keep every
                    //  worker-borrowed pointer until completion or terminal drain.
                    m_failed = true;
                    m_conditioning->request_exit();
                    return;
                }
                failure.status = EAssetStatus::busy;
            }
            else failure.status = EAssetStatus::invalid_asset;
            (void)m_schemas.erase(index);
        }
        else failure.status = EAssetStatus::allocation_failed;
    }
    if (!client.reply(threading::make_pod_message(slot, failure))) m_failed = true;
}

void CAssetService::complete_schema(const SchemaConditionResult& result, const std::int32_t slot,
    threading::CThreadPackage& worker) noexcept
{
    SSchemaOperation* const operation = m_schemas.get_object(slot);
    if (!operation || (&worker != m_conditioning) ||
        (operation->phase != (result.retire ? ESchemaPhase::reclaiming : ESchemaPhase::preparing)))
    {
        m_failed = true;
        return;
    }
    if (result.retire)
    {
        (void)m_schemas.erase(slot);
        return;
    }
    operation->work.source = {};
    operation->backing.reset();
    operation->source = {};
    operation->phase = ESchemaPhase::publishing;
    progress_schemas();
}

void CAssetService::progress_schemas() noexcept
{
    std::int32_t next = m_schemas.first_live();
    while (next >= 0)
    {
        const std::int32_t index = next;
        next = m_schemas.next_live(index);
        SSchemaOperation& operation = *m_schemas.get_object(index);
        if (operation.phase == ESchemaPhase::publishing)
        {
            //  Outer Host progress can wait for a protected invocation to end.
            //  Never turn staged/discarded delivery into an untracked pointer.
            if (operation.client->delivery_protected())
            {
                continue;
            }
            SchemaPrepareResult result;
            result.diagnostic = operation.work.diagnostic;
            result.status = (result.diagnostic.status == schema::EPreparationStatus::success) ?
                EAssetStatus::success : ((result.diagnostic.status == schema::EPreparationStatus::allocation_failed) ?
                    EAssetStatus::allocation_failed : EAssetStatus::conditioning_failed);
            if (result.status == EAssetStatus::success)
            {
                result.acquisition = operation.acquisition;
                result.descriptor = operation.work.prepared.descriptor();
            }
            const EReplyPublication publication = operation.client->publish_reply(
                threading::make_pod_message(operation.client_slot, result));
            const bool poisoned = operation.client->thread && operation.client->thread->pod_delivery_poisoned();
            if (poisoned)
            {
                //  False is not proof of non-escape for a poisoned TQueue. Treat
                //  the descriptor as acquired until explicit release or proven
                //  recipient/batch quiescence, and never publish it twice.
                if (publication == EReplyPublication::busy) operation.client->cancel_reservation(1u);
                operation.client->request_exit();
                m_failed = true;
            }
            else if (publication == EReplyPublication::busy) continue;
            if (((publication == EReplyPublication::queued) || poisoned) && (result.status == EAssetStatus::success))
            {
                operation.phase = ESchemaPhase::acquired;
            }
            else
            {
                operation.client = nullptr;
                operation.phase = ESchemaPhase::retiring;
            }
        }
        if (operation.phase == ESchemaPhase::retiring)
        {
            if (m_workers_stopped)
            {
                //  Abnormal shutdown only: workers published terminal state and
                //  their outputs were drained; this result has no readers. The
                //  Host allocator remains alive for exceptional reclamation.
                operation.work.prepared.clear();
                (void)m_schemas.erase(index);
            }
            else if (post_conditioning(threading::make_pod_message(index, SchemaConditionWork{ &operation.work, true })))
            {
                operation.phase = ESchemaPhase::reclaiming;
            }
            else if (m_conditioning && m_conditioning->pod_delivery_poisoned())
            {
                operation.phase = ESchemaPhase::reclaiming;
                m_conditioning->request_exit();
                m_failed = true;
            }
        }
    }
}

bool CAssetService::release_schema(const CAssetAcquisitionId acquisition, SClientRecord& client) noexcept
{
    for (std::int32_t index = m_schemas.first_live(); index >= 0; index = m_schemas.next_live(index))
    {
        SSchemaOperation& operation = *m_schemas.get_object(index);
        if ((operation.phase == ESchemaPhase::acquired) && (operation.acquisition == acquisition) &&
            (operation.client == &client))
        {
            operation.acquisition = {};
            operation.client = nullptr;
            operation.phase = ESchemaPhase::retiring;
            return true;
        }
    }
    return false;
}

}   // namespace host
