
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_compiler.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Non-throwing boundary to the exception-enabled DXC adapter.

#pragma once

#ifndef RENDERING_RUNTIME_SHADER_COMPILER_HPP_INCLUDED
#define RENDERING_RUNTIME_SHADER_COMPILER_HPP_INCLUDED

#include "assets/bundle.hpp"
#include "shader/shader_data.hpp"
#include "platform/platform_defines.hpp"

namespace rendering::shader_compiler
{

//  Rendering service policy; callers cannot raise these ceilings through requests.
struct SLimits
{
    std::uint32_t entries{ 256u }, include_roots{ 32u }, definitions{ 128u };
    std::size_t request_bytes{ 4u * 1024u * 1024u };
    std::size_t path_bytes{ 1024u }, name_bytes{ 256u }, definition_bytes{ 4096u };
    std::size_t file_bytes{ 4u * 1024u * 1024u }, source_bytes{ 16u * 1024u * 1024u };
    std::size_t diagnostic_bytes{ 256u * 1024u }, artifact_bytes{ 16u * 1024u * 1024u };
    std::size_t group_bytes{ 256u * 1024u * 1024u };
};

//  Call on rendering before dispatch. Loads the pinned library beside the
//  executable and keeps it resident across rendering-module replacement.
[[nodiscard]] bool initialise_compiler() noexcept;
[[nodiscard]] shader::EStatus validate_request(const shader::CCompileRequest& request, const SLimits& limits) noexcept;
void compile(const shader::CCompileRequest& request, const std::uint32_t member, const bundle::CBundleView& sources,
    const SLimits& limits, shader::CShaderCompileReport& report) noexcept;

class CCompileJob final
{
public:
    CCompileJob(const shader::CCompileRequest& request, const std::uint32_t member, const bundle::CBundleView& sources,
        const SLimits& limits) noexcept
        : m_request{ request }, m_member{ member }, m_sources{ sources }, m_limits{ limits }
    {
    }
    CCompileJob(const CCompileJob&) = delete;
    CCompileJob& operator=(const CCompileJob&) = delete;
    void run() noexcept { compile(m_request, m_member, m_sources, m_limits, report); }
    static void MV_STD_ABI_CALL execute(void* const object) noexcept { static_cast<CCompileJob*>(object)->run(); }
    shader::CShaderCompileReport report;

private:
    const shader::CCompileRequest& m_request;
    const std::uint32_t m_member;
    const bundle::CBundleView m_sources;
    const SLimits m_limits;
};

}   //  namespace rendering::shader_compiler

#endif   //  RENDERING_RUNTIME_SHADER_COMPILER_HPP_INCLUDED
