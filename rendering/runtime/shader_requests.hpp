
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_requests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Rendering-owned compilation groups and stable batch callback storage.

#pragma once

#ifndef RENDERING_RUNTIME_SHADER_REQUESTS_HPP_INCLUDED
#define RENDERING_RUNTIME_SHADER_REQUESTS_HPP_INCLUDED

#include "rendering/runtime/shader_compiler.hpp"
#include "shader/shader_messages.hpp"
#include "threading/CThreadPackage.hpp"

namespace rendering
{

class CShaderRequests final
{
public:
    static constexpr std::uint32_t k_correlation_tag = 0x80000000u;
    CShaderRequests(
        threading::CThreadContext& context, threading::CBatchClient& batch,
        const shader_compiler::SLimits& limits = {}) noexcept
        : m_context{ context }, m_batch{ batch }, m_limits{ limits }
    {
    }
    [[nodiscard]] bool initialise() noexcept { return m_groups.initialise() && m_jobs.initialise(); }
    [[nodiscard]] bool pending() const noexcept { return !m_groups.is_empty(); }
    [[nodiscard]] bool submit(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool complete(const threading::SBatchResponse& response) noexcept;

private:
    struct SGroup
    {
        threading::CErasedOwnerMsg response;
        std::uint32_t remaining{ 0u };
        std::uint64_t retained_bytes{ 0u };
    };
    struct SJob
    {
        TInstance<shader_compiler::CCompileJob> work;
        std::int32_t group{ -1 };
        std::uint32_t member{ 0u };
    };
    [[nodiscard]] bool collect(
        const std::int32_t group, const std::uint32_t member, shader::CShaderCompileReport&& report) noexcept;
    [[nodiscard]] bool finish(const std::int32_t group) noexcept;
    threading::CThreadContext& m_context;
    threading::CBatchClient& m_batch;
    const shader_compiler::SLimits m_limits;
    TUnorderedCollection<SGroup> m_groups;
    TUnorderedCollection<SJob> m_jobs;
};

}   //  namespace rendering

#endif   //  RENDERING_RUNTIME_SHADER_REQUESTS_HPP_INCLUDED
