
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_execution.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  The service owns all device operations; idle loading never opens a device.

#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "audio/runtime/audio_execution.hpp"
#include "miniaudio_adapter.hpp"
#include "framework/module/module_binding_context.hpp"
#include <new>

namespace audio
{

CAudioExecution::CAudioExecution(const modules::SExecutionContext& context, IAudioOutput& output, const SRunClock clock) noexcept
    : m_communication{ *context.communication }, m_run{ output, clock }
{
}

bool CAudioExecution::complete() const noexcept
{
    return m_stopping && !m_run.active() && !m_pending && !m_rejected;
}

static modules::RunTestsResult report(const modules::ETestRunStatus status) noexcept
{
    modules::RunTestsResult result{ module_ids::audio, status };
    if (status == modules::ETestRunStatus::success)
    {
        result.passed = 1u;
    }
    else if ((status == modules::ETestRunStatus::unavailable) || (status == modules::ETestRunStatus::cancelled))
    {
        result.skipped = 1u;
    }
    else if (status == modules::ETestRunStatus::failed)
    {
        result.failed = 1u;
    }
    return result;
}

bool CAudioExecution::reply(const connections::SToken token, const threading::CErasedPodMsg& message) noexcept
{
    const modules::ESubmission result = m_communication.submit({ modules::SDestination::EKind::connection, {}, token }, message);
    if (result == modules::ESubmission::busy) return false;
    if ((result == modules::ESubmission::invalid) || (result == modules::ESubmission::transport_failed) ||
        (result == modules::ESubmission::published_wake_failed)) m_failed = true;
    //  A closed conversation explicitly cancels its outstanding data reply.
    return true;
}

void CAudioExecution::service() noexcept
{
    if (!m_enabled && !m_stopping)
    {
        modules::SExecutionIntent intent;
        intent.kind = modules::SExecutionIntent::EKind::participate;
        intent.correlation = 0;
        const modules::ESubmission result = m_communication.submit(intent);
        m_enabled = modules::accepted(result);
        if (!m_enabled && (result != modules::ESubmission::busy)) m_failed = true;
        if (m_enabled) MV_LOG(info, "Audio: Running (device closed)");
    }
    if (m_stopping || m_failed || (m_pending &&
        !m_communication.query({ modules::SDestination::EKind::connection, {}, m_conversation }).active))
    {
        m_run.cancel();
        m_pending = false;
    }
    else m_run.check();
    if (m_pending && !m_run.active())
    {
        const modules::ETestRunStatus status = m_run.result();
        if (!m_reported) MV_LOG(info, "Audio: Test complete after cleanup (status {})", static_cast<unsigned>(status));
        m_reported = true;
        if (reply(m_conversation, threading::make_pod_message(m_correlation, report(status)))) m_pending = false;
    }
    if (m_rejected && reply(m_rejected_connection, m_rejected_reply)) m_rejected = false;
}

void CAudioExecution::receive(const threading::CErasedPodMsg& message, const connections::SSource& source) noexcept
{
    modules::RunTestsRequest request;
    if ((source.kind != connections::SSource::EKind::direct) || (source.address != system_ids::executive) ||
        !message.copy_payload_to(request))
    {
        m_failed = true;
        return;
    }
    const std::int32_t slot = message.query_async_slot();
    modules::ETestRunStatus rejection = modules::ETestRunStatus::invalid_request;
    if ((request.module == module_ids::audio) && (slot >= 0))
    {
        if (m_stopping || m_failed) rejection = modules::ETestRunStatus::cancelled;
        else if (m_pending) rejection = modules::ETestRunStatus::busy;
        else
        {
            m_correlation = slot;
            m_conversation = source.connection;
            m_pending = true;
            m_reported = false;
            m_run.begin();
            return;
        }
    }
    if (m_rejected)
    {
        m_failed = true;
        return;
    }
    m_rejected_connection = source.connection;
    m_rejected_reply = threading::make_pod_message(slot, report(rejection));
    m_rejected = true;
}

modules::EExecutionStatus CAudioExecution::handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
{
    const bool failed_before = m_failed;
    switch (invocation.event)
    {
        case modules::EExecutionEvent::start:
        case modules::EExecutionEvent::service_async:
        {
            service();
            break;
        }
        case modules::EExecutionEvent::stop:
        {
            m_stopping = true;
            m_run.cancel();
            m_pending = m_rejected = false;
            MV_LOG(info, "Audio: Exited");
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
            const connections::SToken token = invocation.notification.connection;
            if (m_pending && (m_conversation.value == token.value))
            {
                m_run.cancel();
                m_pending = false;
            }
            if (m_rejected && (m_rejected_connection.value == token.value)) m_rejected = false;
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::connection_quiescent;
            intent.connection = token;
            if (!modules::accepted(m_communication.submit(intent))) m_failed = true;
            break;
        }
        case modules::EExecutionEvent::message:
        {
            if (!invocation.message) m_failed = true;
            else receive(*invocation.message, invocation.source);
            break;
        }
        default:
        {
            break;
        }
    }
    progress = {};
    progress.ready = m_enabled ? 1u : 0u;
    progress.direct_input = (!m_rejected && !m_stopping && !m_failed) ? 1u : 0u;
    progress.work_complete = complete() ? 1u : 0u;
    //  A refused direct reply waits for its capacity notification. Device work
    //  keeps the run's existing nearest deadline, including continuous ingress.
    if (!m_run.remaining_wait(progress.next_wake_ns)) progress.next_wake_ns = 0u;
    return (m_failed && !failed_before) ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success;
}

class CNativeAudioExecution
{
public:
    CNativeAudioExecution(memory::CMemoryToken&& allocation, const modules::SExecutionContext& context, memory::CMemoryContext& memory) noexcept;

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context, void** const runtime) noexcept;
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept;
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept;

private:
    memory::CMemoryToken m_allocation;
    CMiniaudioOutput m_output;
    CAudioExecution m_execution;
};

CNativeAudioExecution::CNativeAudioExecution(memory::CMemoryToken&& allocation, const modules::SExecutionContext& context, memory::CMemoryContext& memory) noexcept
    : m_allocation{ std::move(allocation) }, m_output{ memory }, m_execution{ context, m_output }
{
}

modules::EExecutionStatus MV_STD_ABI_CALL CNativeAudioExecution::create(const modules::SExecutionContext* const context, void** const runtime) noexcept
{
    if (!context || !runtime || (context->size != sizeof(*context)) || (context->version != 1u) ||
        !context->communication || !modules::is_thread_context_ready(context))
    {
        return modules::EExecutionStatus::invalid_argument;
    }
    *runtime = nullptr;
    memory::CMemoryContext* const memory = memory::get_ambient_memory_context();
    if (!memory)
    {
        return modules::EExecutionStatus::failed;
    }
    memory::CMemoryToken allocation{ 1u, alignof(CNativeAudioExecution), memory };
    if (!allocation.allocate(sizeof(CNativeAudioExecution)))
    {
        return modules::EExecutionStatus::failed;
    }
    void* const storage = allocation.data();
    *runtime = ::new (storage) CNativeAudioExecution{ std::move(allocation), *context, *memory };
    return modules::EExecutionStatus::success;
}

modules::EExecutionStatus MV_STD_ABI_CALL CNativeAudioExecution::handle(void* const runtime, const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
{
    if (!runtime || !invocation || !progress)
    {
        return modules::EExecutionStatus::invalid_argument;
    }
    return static_cast<CNativeAudioExecution*>(runtime)->m_execution.handle(*invocation, *progress);
}

modules::EExecutionStatus MV_STD_ABI_CALL CNativeAudioExecution::destroy(void* const runtime) noexcept
{
    CNativeAudioExecution* const self = static_cast<CNativeAudioExecution*>(runtime);
    if (!self || !self->m_execution.complete())
    {
        return modules::EExecutionStatus::failed;
    }
    memory::CMemoryToken allocation{ std::move(self->m_allocation) };
    self->~CNativeAudioExecution();
    return modules::EExecutionStatus::success;
}

modules::SExecutionV1 audio_execution() noexcept
{
    return { sizeof(modules::SExecutionV1), 1u, 0u, &CNativeAudioExecution::create, &CNativeAudioExecution::handle, &CNativeAudioExecution::destroy };
}

}   //  namespace audio
