
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_execution.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Module-owned Rendering execution and asynchronous texture conditioning.

#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"

#include "runtime/rendering_execution.hpp"
#include "runtime/basis_codec.hpp"
#include "runtime/resource_requests.hpp"
#include "runtime/shader_requests.hpp"
#include "runtime/texture_flow_tests.hpp"

#include "containers/TUnorderedCollection.hpp"
#include "debug/macros.hpp"
#include "framework/module/module_binding_context.hpp"
#include "framework/module/TExecutionInstance.hpp"
#include "texture/texture_requests.hpp"

#include <utility>

namespace rendering
{

class CRenderingExecution
{
public:
    CRenderingExecution(const modules::SExecutionContext& context) noexcept;

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context, void** const runtime) noexcept;
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept;
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept;

private:
    static modules::ESubmission post_asset(void* const context, const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool complete() const noexcept;
    void service() noexcept;
    modules::EExecutionStatus dispatch(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept;

    const modules::CCommunication m_communication;
    threading::CBatchClient& m_batch;
    CResourceRequests m_requests;
    CTextureFlowTests m_tests;
    connections::SToken m_closing[16];
    bool m_codec{ false }, m_enabled{ false }, m_stopping{ false }, m_failed{ false }, m_continuation{ false };
};

CRenderingExecution::CRenderingExecution(const modules::SExecutionContext& context) noexcept
    : m_communication{ *context.communication }, m_batch{ *context.batch }
    , m_requests{ m_batch, { this, &post_asset } }, m_tests{ m_communication, module_ids::rendering, m_requests }
{
}

modules::EExecutionStatus MV_STD_ABI_CALL CRenderingExecution::create(const modules::SExecutionContext* const context, void** const runtime) noexcept
{
    if (!context || !runtime || (context->size != sizeof(*context)) || (context->version != 1u) ||
        !context->communication || !context->batch || !modules::is_thread_context_ready(context))
    {
        return modules::EExecutionStatus::invalid_argument;
    }
    *runtime = nullptr;
    *runtime = modules::TExecutionInstance<CRenderingExecution>::create(*context);
    return (*runtime != nullptr) ? modules::EExecutionStatus::success : modules::EExecutionStatus::failed;
}

modules::EExecutionStatus MV_STD_ABI_CALL CRenderingExecution::handle(void* const runtime, const modules::SInvocation* const invocation,
    modules::SExecutionProgress* const progress) noexcept
{
    if (!runtime || !invocation || !progress)
    {
        return modules::EExecutionStatus::invalid_argument;
    }
    return modules::TExecutionInstance<CRenderingExecution>::get(runtime)->dispatch(*invocation, *progress);
}

modules::EExecutionStatus MV_STD_ABI_CALL CRenderingExecution::destroy(void* const runtime) noexcept
{
    CRenderingExecution* const self = modules::TExecutionInstance<CRenderingExecution>::get(runtime);
    if (!self || !self->complete())
    {
        return modules::EExecutionStatus::failed;
    }
    if (self->m_codec) basis_codec::shutdown();
    modules::TExecutionInstance<CRenderingExecution>::destroy(runtime);
    return modules::EExecutionStatus::success;
}

modules::ESubmission CRenderingExecution::post_asset(void* const context, const threading::CErasedPodMsg& message) noexcept
{
    return static_cast<CRenderingExecution*>(context)->m_communication.submit({}, message);
}

bool CRenderingExecution::complete() const noexcept
{
    return m_stopping && !m_requests.pending() && !m_requests.jobs_pending() && !m_tests.pending();
}

void CRenderingExecution::service() noexcept
{
    m_continuation = false;
    if (!m_enabled && !m_stopping && !m_failed)
    {
        modules::SExecutionIntent intent;
        intent.kind = modules::SExecutionIntent::EKind::participate;
        intent.correlation = 0;
        const modules::ESubmission status = m_communication.submit(intent);
        m_enabled = modules::accepted(status);
        if (!m_enabled && (status != modules::ESubmission::busy)) m_failed = true;
    }
    if (m_stopping || m_failed) m_tests.stop();
    threading::SBatchResponse response;
    unsigned batches = 0u;
    while ((batches < 32u) && m_batch.receive(response))
    {
        ++batches;
        if (!m_requests.complete(response)) m_failed = true;
    }
    const bool output_continuation = m_requests.progress();
    m_continuation = (batches == 32u) || output_continuation;
    if (!m_tests.progress()) m_failed = true;
    unsigned results = 0u;
    SResourceResult result;
    std::int32_t correlation = -1;
    while ((results < 32u) && m_requests.receive(result, correlation))
    {
        ++results;
        if (!m_tests.complete(std::move(result), correlation)) m_failed = true;
        result = {};
    }
    m_continuation = m_continuation || (results == 32u);
    m_failed = m_requests.failed() || m_failed;
    if (!m_tests.pending() && !m_requests.pending())
    {
        for (connections::SToken& token : m_closing)
        {
            if (!token.value) continue;
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::connection_quiescent;
            intent.connection = token;
            if (modules::accepted(m_communication.submit(intent))) token = {};
            else m_failed = true;
        }
    }
}

modules::EExecutionStatus CRenderingExecution::dispatch(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
{
    const bool failed_before = m_failed;
    switch (invocation.event)
    {
        case modules::EExecutionEvent::start:
        {
            m_codec = basis_codec::initialise();
            m_failed = !m_codec || !m_requests.initialise();
            if (!shader_compiler::initialise_compiler())
            {
                MV_LOG(info, "Rendering: DXC is unavailable; shader requests will retain failure reports");
            }
            service();
            break;
        }
        case modules::EExecutionEvent::service_async:
        {
            service();
            break;
        }
        case modules::EExecutionEvent::stop:
        {
            m_stopping = true;
            m_tests.stop();
            service();
            break;
        }
        case modules::EExecutionEvent::participation:
        {
            if (invocation.notification.connection_status != connections::EStatus::success) m_failed = true;
            break;
        }
        case modules::EExecutionEvent::connection_offered:
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::consent;
            intent.connection = invocation.notification.connection;
            intent.decision = (!invocation.notification.bulk.is_bulk() && (invocation.notification.peer == system_ids::executive)) ?
                connections::EStatus::success : connections::EStatus::rejected;
            if (!modules::accepted(m_communication.submit(intent))) m_failed = true;
            break;
        }
        case modules::EExecutionEvent::connection_closing:
        {
            bool stored = false;
            for (connections::SToken& token : m_closing)
            {
                if (token.value) continue;
                token = invocation.notification.connection;
                stored = true;
                break;
            }
            if (!stored) m_failed = true;
            service();
            break;
        }
        case modules::EExecutionEvent::message:
        {
            if (!invocation.message)
            {
                m_failed = true;
                break;
            }
            const threading::CErasedPodMsg& message = *invocation.message;
            if (invocation.source.kind == connections::SSource::EKind::direct)
            {
                if ((invocation.source.address != system_ids::executive) || !m_tests.start(invocation.source.connection, message))
                {
                    m_failed = true;
                }
            }
            else if (invocation.source.is_host() && (message.is_payload_a<AssetAcquireResult>() || message.is_payload_a<AssetReleaseResult>()))
            {
                if (!m_requests.complete(message)) m_failed = true;
            }
            else if (invocation.source.is_host() && (message.is_payload_a<AssetResult>() || message.is_payload_a<AssetDisposeResult>()))
            {
                if (!m_tests.complete(message)) m_failed = true;
            }
            else m_failed = true;
            break;
        }
        default:
        {
            break;
        }
    }
    progress = {};
    progress.ready = (m_enabled && !m_failed) ? 1u : 0u;
    progress.direct_input = (m_tests.can_read() && !m_stopping && !m_failed) ? 1u : 0u;
    progress.work_pending = m_continuation ? 1u : 0u;
    progress.work_complete = complete() ? 1u : 0u;
    if (m_failed && !failed_before)
    {
        MV_LOG_IMMEDIATE(error, "Rendering: Execution failed during event {}", static_cast<unsigned>(invocation.event));
    }
    return (m_failed && !failed_before) ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success;
}

modules::SExecutionV1 rendering_execution() noexcept
{
    return { sizeof(modules::SExecutionV1), 1u, 0u,
        &CRenderingExecution::create, &CRenderingExecution::handle, &CRenderingExecution::destroy };
}

}   //  namespace rendering
