
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_conditioning.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Renderer-local compile/reflect composition, without a Host publication API.

#pragma once

#ifndef RENDERING_RUNTIME_SHADER_CONDITIONING_HPP_INCLUDED
#define RENDERING_RUNTIME_SHADER_CONDITIONING_HPP_INCLUDED

#include "rendering/runtime/shader_compiler.hpp"
#include "rendering/runtime/shader_reflection.hpp"

namespace rendering::shader_conditioning
{

enum class EPreparation : std::uint8_t
{
    not_prepared = 0u,
    ready,
    invalid_input,
    limit_exceeded,
    allocation_failed,
    compiler_unavailable,
    invalid_state
};

class CCompileReflectReport final
{

public:
    CCompileReflectReport() noexcept = default;
    ~CCompileReflectReport() noexcept = default;
    CCompileReflectReport(const CCompileReflectReport&) = delete;
    CCompileReflectReport& operator=(const CCompileReflectReport&) = delete;
    CCompileReflectReport(CCompileReflectReport&& source) noexcept
    {
        *this = std::move(source);
    }

    CCompileReflectReport& operator=(CCompileReflectReport&& source) noexcept
    {
        if (this != &source)
        {
            compilation = std::move(source.compilation);
            reflection = std::move(source.reflection);
            source.compilation = {};
        }
        return *this;
    }

    shader::CShaderCompileReport compilation;
    shader_reflection::CResult reflection;
};

//  Prepare once on the rendering/requester thread, before dispatch. Preparation
//  constructs the private reflection instance and initialises the DXC factory
//  there, never in a worker callback. Only ready jobs
//  may be submitted. Setup failure leaves both stage results not_executed.
//
//  The identity and policies are copied; request and complete bundle backing are
//  borrowed and immutable until all accepted callbacks have drained. Job storage
//  must stay stable through terminal-response consumption. A rejected or discarded
//  unexecuted job retains not_executed results; the owner keeps the batch outcome
//  separately. Exit belongs to the requester route, not an individual group.
//
//  Reports may move out after drain and outlive inputs/jobs. Every allocator
//  context captured by their storage AND this code's rendering module must remain
//  alive until those retained reports/schemas are destroyed, not merely drained.
class CCompileReflectJob final
{

public:
    CCompileReflectJob() noexcept = default;
    ~CCompileReflectJob() noexcept = default;
    CCompileReflectJob(const CCompileReflectJob&) = delete;
    CCompileReflectJob& operator=(const CCompileReflectJob&) = delete;
    CCompileReflectJob(CCompileReflectJob&&) = delete;
    CCompileReflectJob& operator=(CCompileReflectJob&&) = delete;

    [[nodiscard]] EPreparation prepare(const shader::CCompileRequest& request, const std::uint32_t member,
        const bundle::CBundleView& sources, const CStringView identity,
        const shader_compiler::SLimits& compile_limits = {}, const shader_reflection::SLimits& reflect_limits = {}) noexcept;
    [[nodiscard]] EPreparation preparation() const noexcept
    {
        return m_preparation;
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return (m_preparation == EPreparation::ready) && !m_started;
    }

    //  Synchronous composition on this thread. Neither stage is rescheduled.
    //  One-shot even after report transfer; false preserves an unready/used job.
    [[nodiscard]] bool run() noexcept;
    static void MV_STD_ABI_CALL execute(void* const object) noexcept
    {
        (void)static_cast<CCompileReflectJob*>(object)->run();
    }

    CCompileReflectReport report;

private:
    const shader::CCompileRequest* m_request{ nullptr };
    bundle::CBundleView m_sources;
    CSimpleString m_identity;
    shader_compiler::SLimits m_compile_limits;
    shader_reflection::SLimits m_reflect_limits;
    shader_reflection::CReflection m_reflection;
    std::uint32_t m_member{ 0u };
    EPreparation m_preparation{ EPreparation::not_prepared };
    bool m_started{ false };
};

}   //  namespace rendering::shader_conditioning

#endif   //  RENDERING_RUNTIME_SHADER_CONDITIONING_HPP_INCLUDED
