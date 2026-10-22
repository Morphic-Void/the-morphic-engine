
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_requests.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Drain every accepted callback before returning the group's borrowed bundle.

#include "rendering/runtime/shader_requests.hpp"
#include "threading/messages/message_helpers.hpp"

namespace rendering
{

bool CShaderRequests::submit(const threading::CErasedPodMsg& message) noexcept
{
    ShaderCompileWork input;
    if ((message.query_async_slot() < 0) || !message.copy_payload_to(input) || (input.request == nullptr))
    {
        return false;
    }
    auto response = threading::make_owner_message<ShaderCompileResult>(message.query_async_slot());
    auto* const result = response.owner().payload<ShaderCompileResult>();
    if (result == nullptr)
    {
        return m_context.post(std::move(response));
    }
    result->status = shader_compiler::validate_request((*input.request), m_limits);
    if (result->status != shader::EStatus::success)
    {
        return m_context.post(std::move(response));
    }
    const auto count = input.request->entries.count();
    if (((static_cast<std::uint64_t>(count) * sizeof(shader::CShaderCompileReport)) + sizeof(shader::CCompileGroup)) >
        m_limits.group_bytes)
    {
        result->status = shader::EStatus::limit_exceeded;
        return m_context.post(std::move(response));
    }
    if (!result->group.reports.initialise(count))
    {
        result->status = shader::EStatus::allocation_failed;
        return m_context.post(std::move(response));
    }
    const std::int32_t group = m_groups.emplace();
    if (group < 0)
    {
        result->status = shader::EStatus::allocation_failed;
        return m_context.post(std::move(response));
    }
    auto& pending = *m_groups.get_object(group);
    pending.response = std::move(response);
    pending.remaining = count;
    pending.retained_bytes = sizeof(shader::CCompileGroup) + result->group.memory_attribution().allocation_size;
    if (pending.retained_bytes > m_limits.group_bytes)
    {
        result->status = shader::EStatus::limit_exceeded;
        return finish(group);
    }
    for (std::uint32_t member = 0u; member < count; ++member)
    {
        const std::int32_t slot = m_jobs.emplace();
        auto* const job = m_jobs.get_object(slot);
        if ((job == nullptr) || !job->work.emplace((*input.request), member, input.sources, m_limits))
        {
            if (slot >= 0)
            {
                (void)m_jobs.erase(slot);
            }
            shader::CShaderCompileReport report;
            report.status = shader::EStatus::allocation_failed;
            if (!collect(group, member, std::move(report)))
            {
                return false;
            }
            continue;
        }
        job->group = group;
        job->member = member;
        const auto submitted = m_batch.submit((&shader_compiler::CCompileJob::execute), job->work.operator->(),
            (k_correlation_tag | static_cast<std::uint32_t>(slot)));
        if (submitted.status != threading::EBatchSubmission::queued)
        {
            shader::CShaderCompileReport report = std::move(job->work->report);
            if (submitted.status != threading::EBatchSubmission::completed_inline)
            {
                report.status = shader::EStatus::not_executed;
            }
            (void)m_jobs.erase(slot);
            if (!collect(group, member, std::move(report)))
            {
                return false;
            }
        }
    }
    return true;
}

bool CShaderRequests::complete(const threading::SBatchResponse& response) noexcept
{
    const auto slot = static_cast<std::int32_t>(response.correlation & (~k_correlation_tag));
    auto* const job = m_jobs.get_object(slot);
    if (job == nullptr)
    {
        return false;
    }
    const auto group = job->group;
    const auto member = job->member;
    shader::CShaderCompileReport report = std::move(job->work->report);
    if (response.completion != threading::EBatchCompletion::executed)
    {
        report.status = shader::EStatus::not_executed;
    }
    (void)m_jobs.erase(slot);
    return collect(group, member, std::move(report));
}

bool CShaderRequests::collect(
    const std::int32_t group, const std::uint32_t member, shader::CShaderCompileReport&& report) noexcept
{
    auto& pending = *m_groups.get_object(group);
    auto& result = *pending.response.owner().payload<ShaderCompileResult>();
    const auto bytes = report.memory_attribution().allocation_size;
    if (bytes > (m_limits.group_bytes - pending.retained_bytes))
    {
        report = {};
        report.status = shader::EStatus::limit_exceeded;
    }
    else
    {
        pending.retained_bytes += bytes;
    }
    report.member = member;
    auto& summary = result.group.summary;
    if (report.status == shader::EStatus::success)
    {
        ++summary.succeeded;
    }
    else if (report.status == shader::EStatus::not_executed)
    {
        ++summary.not_executed;
    }
    else
    {
        ++summary.failed;
    }
    if ((report.status == shader::EStatus::not_executed) || (report.status == shader::EStatus::compiler_failed) ||
        (report.status == shader::EStatus::compiler_unavailable) ||
        (report.status == shader::EStatus::allocation_failed))
    {
        summary.outcome = shader::EOutcome::interrupted;
    }
    else if ((report.status != shader::EStatus::success) && (summary.outcome == shader::EOutcome::all_succeeded))
    {
        summary.outcome = shader::EOutcome::member_failures;
    }
    result.group.reports[member] = std::move(report);
    return (--pending.remaining != 0u) || finish(group);
}

bool CShaderRequests::finish(const std::int32_t group) noexcept
{
    auto response = std::move(m_groups.get_object(group)->response);
    (void)m_groups.erase(group);
    return m_context.post(std::move(response));
}

}   //  namespace rendering
