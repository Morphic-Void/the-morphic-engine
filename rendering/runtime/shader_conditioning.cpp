
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_conditioning.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  One stable callback owns both stage reports and executes them in sequence.

#include "runtime/shader_conditioning.hpp"
#include "text/utf8_string.hpp"

namespace rendering::shader_conditioning
{

EPreparation CCompileReflectJob::prepare(const shader::CCompileRequest& request, const std::uint32_t member,
    const bundle::CBundleView& sources, const CStringView identity,
    const shader_compiler::SLimits& compile_limits, const shader_reflection::SLimits& reflect_limits) noexcept
{
    if (m_preparation != EPreparation::not_prepared)
    {
        return EPreparation::invalid_state;
    }
    if ((identity.length() > reflect_limits.string_bytes) || (identity.length() > reflect_limits.strings_bytes))
    {
        return m_preparation = EPreparation::limit_exceeded;
    }
    std::size_t measured = 0u;
    if ((member >= request.entries.count()) || !sources.is_ready() || (identity.length() == 0u) ||
        !utf8_string::validate_and_measure(identity.string(), identity.length(), utf8_string::ELiteralNulPolicy::reject, measured) ||
        (std::memchr(identity.string(), 0xc0, identity.length()) != nullptr))
    {
        return m_preparation = EPreparation::invalid_input;
    }
    if (!m_identity.set(identity.string(), identity.length()))
    {
        return m_preparation = EPreparation::allocation_failed;
    }
    if (!m_reflection.prepare())
    {
        m_identity.deallocate();
        return m_preparation = EPreparation::allocation_failed;
    }
    if (!shader_compiler::initialise_compiler())
    {
        m_identity.deallocate();
        return m_preparation = EPreparation::compiler_unavailable;
    }
    m_request = &request;
    m_member = member;
    m_sources = sources;
    m_compile_limits = compile_limits;
    m_reflect_limits = reflect_limits;
    return m_preparation = EPreparation::ready;
}

bool CCompileReflectJob::run() noexcept
{
    if (!ready())
    {
        return false;
    }
    m_started = true;
    shader_compiler::compile(*m_request, m_member, m_sources, m_compile_limits, report.compilation);

    //  A failed compile takes reflect's ordinary blocked path, retaining its
    //  identity/diagnostic contract without parsing or initialising the vendor
    //  reflection module.
    (void)m_reflection.run({ &report.compilation, m_identity.view() }, m_reflect_limits, report.reflection);
    return true;
}

}   //  namespace rendering::shader_conditioning
