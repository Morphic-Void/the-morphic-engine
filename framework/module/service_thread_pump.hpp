
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_thread_pump.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Minimal module lifecycle coordinator for an optional requested service thread.

#pragma once

#include "framework/module/host_pump.hpp"
#include "framework/module/module_binding_context.hpp"

namespace modules
{

template<threading::FEndpointThreadEntry Entry = nullptr, const thread_ids::id_type* Thread = nullptr, bool Direct = true>
class TServiceThreadPump
{
public:
    [[nodiscard]] static SHostPumpV1 capability() noexcept
    {
        return { sizeof(SHostPumpV1), 1u, 0u, &activate, &pump, &begin_stop, &deactivate };
    }

private:
    static TServiceThreadPump& state() noexcept
    {
        static TServiceThreadPump value;
        return value;
    }
    static ECallStatus MV_STD_ABI_CALL activate(const SHostPumpContext* const context,
        void** const runtime, SProgress* const progress) noexcept
    {
        if (!context || !runtime || !progress || (context->size != sizeof(*context)) || (context->version != 1u) ||
            !context->endpoint || !is_thread_context_ready(context) || state().m_context)
        {
            return ECallStatus::invalid_argument;
        }
        state().m_context = context->endpoint;
        *runtime = &state();
        *progress = {};
        progress->work_pending = 1u;
        if constexpr (Entry == nullptr) progress->phase = EPumpPhase::ready;
        return ECallStatus::success;
    }
    static ECallStatus MV_STD_ABI_CALL pump(void* const runtime, const SPumpInput* const input,
        SPumpResult* const result) noexcept
    {
        if ((runtime != &state()) || !input || !result || (input->size != sizeof(*input)) || (input->version != 1u))
        {
            return ECallStatus::invalid_argument;
        }
        TServiceThreadPump& self = state();
        *result = {};
        threading::CEndpointContext& endpoint = *self.m_context;
        self.m_stopping = self.m_stopping || endpoint.module_stopping();
        if constexpr (Entry != nullptr)
        {
            if (!self.m_requested && !self.m_stopping)
            {
                const threading::CreateThreadRequest request{ Entry, nullptr, *Thread,
                    platform::threading::EThreadPriority::Normal, Direct ? 1u : 0u };
                const threading::SEndpointPost posted = endpoint.post(threading::make_pod_message(0, request));
                if (!posted && (posted.status != threading::EEndpointPost::busy))
                {
                    return ECallStatus::failed;
                }
                self.m_requested = posted;
            }
        }
        for (std::uint32_t count = 0u; count < 64u; ++count)
        {
            threading::CErasedPodMsg message;
            if (!endpoint.read(message)) break;
            threading::CreateThreadResult created;
            threading::EndpointLifecycle lifecycle;
            threading::EndpointReady ready;
            if (message.copy_payload_to(created))
            {
                self.m_created = created.status == services::EServiceStatus::success;
                if (!self.m_created)
                {
                    if (!self.m_stopping && !endpoint.module_stopping())
                    {
                        return ECallStatus::failed;
                    }
                    self.m_retired = true;
                    continue;
                }
                self.m_thread = created.endpoint;
            }
            else if (message.copy_payload_to(ready)) self.m_ready_child = ready.endpoint;
            else if (message.copy_payload_to(lifecycle) && (lifecycle.requester.value == endpoint.identity().value) &&
                (lifecycle.correlation == 0))
            {
                if (lifecycle.event == threading::EEndpointLifecycle::created)
                {
                    self.m_thread = lifecycle.endpoint;
                    if (lifecycle.status != services::EServiceStatus::success) self.m_retired = true;
                }
                if (lifecycle.event == threading::EEndpointLifecycle::execution_ended)
                {
                    self.m_ended = self.m_stopping = true;
                    endpoint.request_module_stop();
                }
                if (lifecycle.event == threading::EEndpointLifecycle::retired) self.m_retired = true;
            }
            else if (message.is_payload_a<threading::ProviderAdmissionClosed>()) self.m_closed = true;
        }
        self.m_stopping = self.m_stopping || endpoint.module_stopping();
        self.m_ready = self.m_ready || (self.m_created && !self.m_stopping && !self.m_retired &&
            self.m_thread.value && (self.m_ready_child.value == self.m_thread.value));
        if (self.m_stopping && self.m_created && !self.m_ended && !self.m_retired && !self.m_stop_sent)
        {
            const threading::SEndpointPost posted = endpoint.post(threading::make_pod_message(2,
                threading::StopThreadRequest{ self.m_thread }));
            if (!posted && (posted.status != threading::EEndpointPost::busy))
            {
                return ECallStatus::failed;
            }
            self.m_stop_sent = posted;
        }
        if (self.m_stopping && (!self.m_requested || self.m_retired))
        {
            if (!self.m_closing)
            {
                self.m_closing = endpoint.post(threading::make_pod_message(1, threading::CloseProviderAdmission{}));
            }
            if (self.m_closed) (void)endpoint.seal();
        }
        result->progress.phase = endpoint.sealed() ? EPumpPhase::stopped :
            (self.m_stopping ? EPumpPhase::stopping : ((self.m_ready || (Entry == nullptr)) ? EPumpPhase::ready : EPumpPhase::starting));
        result->progress.work_pending = ((!self.m_requested && !self.m_stopping && (Entry != nullptr)) ||
            (self.m_stopping && self.m_created && !self.m_ended && !self.m_retired && !self.m_stop_sent) ||
            (self.m_stopping && self.m_closed && !endpoint.sealed())) ? 1u : 0u;
        return ECallStatus::success;
    }
    static ECallStatus MV_STD_ABI_CALL begin_stop(void* const runtime, const EStopReason, SProgress* const progress) noexcept
    {
        if ((runtime != &state()) || !progress)
        {
            return ECallStatus::invalid_argument;
        }
        state().m_stopping = true;
        *progress = {};
        progress->phase = EPumpPhase::stopping;
        progress->work_pending = 1u;
        return ECallStatus::success;
    }
    static ECallStatus MV_STD_ABI_CALL deactivate(void* const runtime) noexcept
    {
        if ((runtime != &state()) || !state().m_context || !state().m_context->sealed())
        {
            return ECallStatus::failed;
        }
        state() = {};
        return ECallStatus::success;
    }

#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SServiceThreadPumpTestAccess;
#endif
    threading::CEndpointContext* m_context{ nullptr };
    services::SClientToken m_thread, m_ready_child;
    bool m_requested{ false }, m_created{ false }, m_ready{ false }, m_stopping{ false };
    bool m_ended{ false }, m_retired{ false }, m_stop_sent{ false }, m_closing{ false }, m_closed{ false };
};

}   //  namespace modules
