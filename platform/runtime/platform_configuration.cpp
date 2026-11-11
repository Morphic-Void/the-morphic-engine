
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_configuration.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Nominated Host conditioning followed by bounded Platform installation/retirement.

#include "runtime/platform_runtime.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include <algorithm>
#include <cstring>

namespace platform::runtime
{

using services::EServiceStatus;

bool CPlatformRuntime::configuration_used(const input::SInstallationToken token) const noexcept
{
    for (const TInstance<CInputConnection>& connection : m_inputs)
    {
        if (connection && (connection->installation.value == token.value))
        {
            return true;
        }
    }
    return false;
}

bool CPlatformRuntime::configuration_command(const services::SDispatchedCommand& command,
    const std::int32_t completion) noexcept
{
    if (!m_context.services->post_pod || !m_context.services->commit_resource)
    {
        complete(completion, EServiceStatus::unavailable);
        return true;
    }
    if (command.command.opcode == static_cast<std::uint16_t>(input::ECommand::retire_configuration))
    {
        input::SRetireConfiguration request;
        if (command.command.payload_size != sizeof(request))
        {
            complete(completion, EServiceStatus::invalid_request);
            return true;
        }
        std::memcpy(&request, command.command.payload, sizeof(request));
        for (SConfiguration& configuration : m_configurations)
        {
            if (!request.installation.value || (configuration.token.value != request.installation.value))
            {
                continue;
            }
            if (configuration_used(request.installation) || (configuration.retirement >= 0))
            {
                complete(completion, EServiceStatus::busy);
            }
            else configuration.retirement = completion;
            return true;
        }
        complete(completion, EServiceStatus::stale);
        return true;
    }
    input::SInstallConfiguration request;
    if (command.command.payload_size != sizeof(request))
    {
        complete(completion, EServiceStatus::invalid_request);
        return true;
    }
    std::memcpy(&request, command.command.payload, sizeof(request));
    if (!request.source_asset || request.reserved || (request.format_version != 1u))
    {
        complete(completion, EServiceStatus::invalid_request);
        return true;
    }
    if ((m_preparation.configuration >= 0) || (m_next_conditioning == INT32_MAX) || !m_next_installation)
    {
        complete(completion, EServiceStatus::busy);
        return true;
    }
    std::int32_t vacant = -1;
    for (std::int32_t index = 0; index < 8; ++index)
    {
        if (!m_configurations[index].data)
        {
            vacant = index;
            break;
        }
    }
    if (vacant < 0)
    {
        complete(completion, EServiceStatus::busy);
        return true;
    }
    if (!m_configurations[vacant].data.emplace())
    {
        complete(completion, EServiceStatus::allocation_failed);
        return true;
    }
    m_preparation = SPreparation{ m_clock };
    m_preparation.configuration = vacant;
    m_preparation.completion = completion;
    m_preparation.command = command;
    m_preparation.request.source = request.source_asset;
    m_preparation.correlation = m_next_conditioning++;
    const modules::EBudgetStatus budget = m_preparation.budget.start(m_context.execution->restriction, 5000000000u);
    if ((budget != modules::EBudgetStatus::running) && (budget != modules::EBudgetStatus::restricted))
    {
        cancel_configuration(EServiceStatus::native_failure);
    }
    m_configuration_runnable = true;
    return true;
}

void CPlatformRuntime::cancel_configuration(const EServiceStatus status) noexcept
{
    if ((m_preparation.configuration < 0) || m_preparation.cancelled)
    {
        return;
    }
    m_preparation.cancelled = true;
    m_configurations[m_preparation.configuration].data->detach();
    if (m_preparation.completion >= 0)
    {
        services::ServiceReply& reply = m_completions[m_preparation.completion].reply;
        if (reply.payload_size == sizeof(input::SInstallationResult))
        {
            input::SInstallationResult result;
            std::memcpy(&result, reply.payload, sizeof(result));
            result.installation = {};
            std::memcpy(reply.payload, &result, sizeof(result));
        }
    }
    complete(m_preparation.completion, status);
    m_configuration_runnable = true;
}

void CPlatformRuntime::configuration_message(const ::threading::CErasedPodMsg& message) noexcept
{
    services::ServiceRequestCancelled cancelled;
    if (message.copy_payload_to(cancelled))
    {
        if ((m_preparation.configuration >= 0) &&
            (m_preparation.command.command.route.value == cancelled.route.value) &&
            (m_preparation.command.command.request.value == cancelled.request.value))
        {
            cancel_configuration(EServiceStatus::cancelled);
        }
        for (SConfiguration& configuration : m_configurations)
        {
            if (configuration.retirement < 0) continue;
            const services::ServiceReply& reply = m_completions[configuration.retirement].reply;
            if ((reply.route.value == cancelled.route.value) && (reply.request.value == cancelled.request.value))
            {
                complete(configuration.retirement, EServiceStatus::cancelled);
                configuration.retirement = -1;
            }
        }
    }
    if ((m_preparation.configuration < 0) || (message.query_async_slot() != m_preparation.correlation))
    {
        return;
    }
    SchemaPrepareResult prepared;
    AssetReleaseResult released;
    if (m_preparation.waiting && message.copy_payload_to(prepared))
    {
        m_preparation.waiting = false;
        if (prepared.status == EAssetStatus::busy)
        {
            m_preparation.requested = false;
            return;
        }
        if (prepared.status != EAssetStatus::success)
        {
            if (m_preparation.completion >= 0)
            {
                input::SInstallationResult diagnostic;
                diagnostic.error = input::EConfigurationError::schema;
                diagnostic.definition = prepared.diagnostic.line;
                diagnostic.field = prepared.diagnostic.column;
                services::ServiceReply& reply = m_completions[m_preparation.completion].reply;
                reply.payload_size = sizeof(diagnostic);
                std::memcpy(reply.payload, &diagnostic, sizeof(diagnostic));
            }
            cancel_configuration((prepared.status == EAssetStatus::allocation_failed) ?
                EServiceStatus::allocation_failed : EServiceStatus::invalid_request);
            return;
        }
        m_preparation.acquisition = prepared.acquisition;
        if (!prepared.acquisition || !prepared.descriptor)
        {
            cancel_configuration(EServiceStatus::protocol_failure);
            return;
        }
        if (!m_preparation.cancelled)
        {
            if (!m_configurations[m_preparation.configuration].data->begin(prepared.descriptor))
            {
                const input::EConfigurationError error = m_configurations[m_preparation.configuration].data->diagnostic().error;
                cancel_configuration((error == input::EConfigurationError::allocation) ?
                    EServiceStatus::allocation_failed : EServiceStatus::invalid_request);
            }
            else m_preparation.building = true;
        }
    }
    else if (m_preparation.release_waiting && message.copy_payload_to(released))
    {
        if ((released.acquisition != m_preparation.acquisition) || (released.status != EAssetStatus::success))
        {
            native_failure(UINT32_MAX);
            return;
        }
        m_preparation.acquisition = {};
        m_preparation.release_waiting = false;
    }
}

void CPlatformRuntime::progress_configurations(const std::uint64_t budget_ns) noexcept
{
    m_configuration_runnable = false;
    if (m_stopping) cancel_configuration(EServiceStatus::closing);
    std::uint64_t start{};
    if (m_clock.read(start).code != platform::threading::EWaitError::none)
    {
        cancel_configuration(EServiceStatus::native_failure);
        native_failure(UINT32_MAX);
        return;
    }
    const std::uint64_t limit = std::min(budget_ns, std::uint64_t{ 500000u });
    const auto remaining = [&]() noexcept
    {
        std::uint64_t now{};
        if ((m_clock.read(now).code != platform::threading::EWaitError::none) || (now < start))
        {
            cancel_configuration(EServiceStatus::native_failure);
            native_failure(UINT32_MAX);
            return false;
        }
        return limit && ((now - start) < limit);
    };
    if (m_preparation.configuration >= 0)
    {
        SPreparation& preparation = m_preparation;
        SConfiguration& configuration = m_configurations[preparation.configuration];
        const modules::EBudgetStatus budget = preparation.budget.observe().status;
        if (budget == modules::EBudgetStatus::expired) cancel_configuration(EServiceStatus::timed_out);
        if (budget == modules::EBudgetStatus::failed) cancel_configuration(EServiceStatus::native_failure);
        if (!preparation.cancelled && !preparation.requested && (budget == modules::EBudgetStatus::running))
        {
            const ::threading::CErasedPodMsg request = ::threading::make_pod_message(preparation.correlation, preparation.request);
            const EServiceStatus status = m_context.services->post_pod(m_context.service_context, &request);
            if (status == EServiceStatus::success) preparation.requested = preparation.waiting = true;
            else if (status != EServiceStatus::busy) cancel_configuration(status);
        }
        if (!preparation.cancelled && preparation.building && (budget == modules::EBudgetStatus::running))
        {
            //  Charge conservative check credits before entering each resumable
            //  stage. Page growth/copies are bounded well below 32 KiB per turn.
            std::uint32_t work = 256u;
            std::uint32_t bytes = 32768u;
            while (remaining())
            {
                const std::uint32_t cost = configuration.data->step_cost();
                const std::uint32_t copied = configuration.data->step_bytes();
                if ((cost > work) || (copied > bytes))
                {
                    break;
                }
                work -= cost;
                bytes -= copied;
                const EConfigurationBuild result = configuration.data->step();
                if (result == EConfigurationBuild::failed)
                {
                    const input::SInstallationResult diagnostic = configuration.data->diagnostic();
                    if (preparation.completion >= 0)
                    {
                        services::ServiceReply& reply = m_completions[preparation.completion].reply;
                        reply.payload_size = sizeof(diagnostic);
                        std::memcpy(reply.payload, &diagnostic, sizeof(diagnostic));
                    }
                    cancel_configuration((diagnostic.error == input::EConfigurationError::allocation) ?
                        EServiceStatus::allocation_failed : EServiceStatus::invalid_request);
                    break;
                }
                if (result == EConfigurationBuild::ready)
                {
                    configuration.data->detach();
                    preparation.building = false;
                    break;
                }
            }
            if (preparation.building && !preparation.cancelled) m_configuration_runnable = true;
        }
        if (preparation.acquisition && !preparation.release_waiting && (preparation.cancelled || !preparation.building))
        {
            const ::threading::CErasedPodMsg request = ::threading::make_pod_message(preparation.correlation,
                AssetReleaseRequest{ preparation.acquisition });
            const EServiceStatus status = m_context.services->post_pod(m_context.service_context, &request);
            if (status == EServiceStatus::success) preparation.release_waiting = true;
            else if (status != EServiceStatus::busy) native_failure(UINT32_MAX);
        }
        if (preparation.cancelled)
        {
            if (!preparation.waiting && !preparation.acquisition && (preparation.completion < 0))
            {
                m_configuration_runnable = true;
                if (remaining() && configuration.data->clear_step())
                {
                    configuration.data.reset();
                    preparation = SPreparation{ m_clock };
                }
            }
        }
        else if (configuration.data->ready() && !preparation.acquisition && !preparation.release_waiting &&
            (budget == modules::EBudgetStatus::running))
        {
            if (!preparation.token.value)
            {
                preparation.token.value = m_next_installation;
                m_next_installation = (m_next_installation == UINT64_MAX) ? 0u : (m_next_installation + 1u);
            }
            services::ServiceReply& reply = m_completions[preparation.completion].reply;
            input::SInstallationResult result;
            result.installation = preparation.token;
            reply.status = EServiceStatus::success;
            reply.payload_size = sizeof(result);
            std::memcpy(reply.payload, &result, sizeof(result));
            const EServiceStatus status = m_context.services->commit_resource(m_context.service_context, &reply);
            if (status == EServiceStatus::success)
            {
                configuration.token = preparation.token;
                configuration.data->publish(configuration.token);
                m_completions[preparation.completion] = {};
                preparation = SPreparation{ m_clock };
            }
            else if (status != EServiceStatus::busy) cancel_configuration(status);
        }
    }
    for (SConfiguration& configuration : m_configurations)
    {
        if (!configuration.data) continue;
        if (m_stopping && configuration.token.value && !configuration_used(configuration.token))
        {
            configuration.token = {};
            configuration.retiring = true;
            if (configuration.retirement >= 0) complete(configuration.retirement, EServiceStatus::closing);
            configuration.retirement = -1;
        }
        if (configuration.retirement >= 0)
        {
            const std::int32_t completion = configuration.retirement;
            if (configuration_used(configuration.token))
            {
                complete(completion, EServiceStatus::busy);
                configuration.retirement = -1;
            }
            else
            {
                services::ServiceReply& reply = m_completions[completion].reply;
                reply.status = EServiceStatus::success;
                const EServiceStatus status = m_context.services->commit_resource(m_context.service_context, &reply);
                if (status == EServiceStatus::success)
                {
                    configuration.token = {};
                    configuration.retiring = true;
                    m_completions[completion] = {};
                    configuration.retirement = -1;
                }
                else if (status != EServiceStatus::busy)
                {
                    complete(completion, status);
                    configuration.retirement = -1;
                }
            }
        }
        if (configuration.retiring)
        {
            m_configuration_runnable = true;
            if (remaining() && configuration.data->clear_step())
            {
                configuration.data.reset();
                configuration.retiring = false;
            }
        }
    }
}

}   // namespace platform::runtime
