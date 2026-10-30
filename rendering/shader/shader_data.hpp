
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_data.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Owned shader requests and reports, independent of compiler and Host lifetimes.

#pragma once

#ifndef RENDERING_SHADER_DATA_HPP_INCLUDED
#define RENDERING_SHADER_DATA_HPP_INCLUDED

#include "containers/StringBuffers.hpp"
#include "containers/TUnorderedCollection.hpp"

namespace shader
{

enum class EStage : std::uint8_t
{
    vertex = 0u,
    hull,
    domain,
    geometry,
    pixel,
    compute
};

enum class ETarget : std::uint8_t
{
    vulkan_1_1 = 0u,
    vulkan_1_2,
    vulkan_1_3
};

enum class EOptimisation : std::uint8_t
{
    disabled = 0u,
    level_1,
    level_2,
    level_3
};

enum class EDebug : std::uint8_t
{
    none = 0u,
    source_and_lines,
    source_debug
};

enum class EFeatures : std::uint8_t
{
    conventional_32_bit = 0u
};

enum class EStatus : std::uint8_t
{
    not_executed = 0u,
    success,
    invalid_input,
    compilation_failed,
    allocation_failed,
    limit_exceeded,
    unsupported_feature,
    compiler_unavailable,
    compiler_failed
};

enum class EOutcome : std::uint8_t
{
    all_succeeded = 0u,
    member_failures,
    interrupted
};

struct SOptions
{
    ETarget target{ ETarget::vulkan_1_1 };
    EOptimisation optimisation{ EOptimisation::disabled };
    EDebug debug{ EDebug::source_debug };
    EFeatures features{ EFeatures::conventional_32_bit };
    bool warnings_as_errors{ false };
};

//  Fixed ordered members over existing stable collection storage. No erasure
//  or insertion after initialisation; index is the request/result identity.
template<typename T>
class TMembers
{
public:
    TMembers() noexcept = default;
    TMembers(const TMembers&) = delete;
    TMembers& operator=(const TMembers&) = delete;
    TMembers(TMembers&& source) noexcept;
    TMembers& operator=(TMembers&& source) noexcept;
    [[nodiscard]] bool initialise(const std::uint32_t count) noexcept;
    [[nodiscard]] std::uint32_t count() const noexcept;
    [[nodiscard]] T& operator[](const std::uint32_t index) noexcept;
    [[nodiscard]] const T& operator[](const std::uint32_t index) const noexcept;
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept;

private:
    TUnorderedCollection<T> m_items;
    std::uint32_t m_count{ 0u };
};

struct SDefinition
{
    CSimpleString name, value;
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(name.memory_attribution(), value.memory_attribution());
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        name.unsafe_replace_memory_context_without_accounting(source, target);
        value.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

struct SEntry
{
    CSimpleString source_path, entry_point;
    EStage stage{ EStage::vertex };
    SOptions options;
    TMembers<SDefinition> definitions;
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(definitions.memory_attribution(),
            memory::combine_memory_attribution(source_path.memory_attribution(), entry_point.memory_attribution()));
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        source_path.unsafe_replace_memory_context_without_accounting(source, target);
        entry_point.unsafe_replace_memory_context_without_accounting(source, target);
        definitions.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

struct CCompileRequest
{
    TMembers<CSimpleString> include_roots;
    TMembers<SDefinition> definitions;
    TMembers<SEntry> entries;
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(entries.memory_attribution(),
            memory::combine_memory_attribution(include_roots.memory_attribution(), definitions.memory_attribution()));
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        include_roots.unsafe_replace_memory_context_without_accounting(source, target);
        definitions.unsafe_replace_memory_context_without_accounting(source, target);
        entries.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

//  SPIR-V words are aligned. Requirements are retained separately from later
//  resource reflection: numeric capabilities and declared extension strings.
struct CCompiledShader
{
    //  Initial output is always Vulkan SPIR-V; no API-specific enum escapes.
    EStage stage{ EStage::vertex };
    ETarget target{ ETarget::vulkan_1_1 };
    CByteBuffer bytes;
    CSimpleString requirements;
    [[nodiscard]] CByteConstView view() const noexcept { return bytes.const_view(); }
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(bytes.memory_attribution(), requirements.memory_attribution());
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        bytes.unsafe_replace_memory_context_without_accounting(source, target);
        requirements.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

struct CShaderCompileReport
{
    std::uint32_t member{ 0u };
    EStatus status{ EStatus::not_executed };
    EStage stage{ EStage::vertex };
    SOptions options;
    bool diagnostics_truncated{ false };
    CSimpleString source_path, entry_point, diagnostics, provenance;
    CCompiledShader shader;
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        auto result =
            memory::combine_memory_attribution(source_path.memory_attribution(), entry_point.memory_attribution());
        result = memory::combine_memory_attribution(result, diagnostics.memory_attribution());
        result = memory::combine_memory_attribution(result, provenance.memory_attribution());
        return memory::combine_memory_attribution(result, shader.memory_attribution());
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        source_path.unsafe_replace_memory_context_without_accounting(source, target);
        entry_point.unsafe_replace_memory_context_without_accounting(source, target);
        diagnostics.unsafe_replace_memory_context_without_accounting(source, target);
        provenance.unsafe_replace_memory_context_without_accounting(source, target);
        shader.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

struct SSummary
{
    std::uint32_t succeeded{ 0u }, failed{ 0u }, not_executed{ 0u };
    EOutcome outcome{ EOutcome::all_succeeded };
};

struct CCompileGroup
{
    TMembers<CShaderCompileReport> reports;
    SSummary summary;
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return reports.memory_attribution();
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        reports.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

//==============================================================================
//  TMembers<T> out of class function bodies
//==============================================================================

template<typename T>
inline TMembers<T>::TMembers(TMembers&& source) noexcept : m_items{ std::move(source.m_items) }, m_count{ std::exchange(source.m_count, 0u) }
{
}

template<typename T>
inline TMembers<T>& TMembers<T>::operator=(TMembers&& source) noexcept
{
    if (this != &source)
    {
        m_items = std::move(source.m_items);
        m_count = std::exchange(source.m_count, 0u);
    }
    return *this;
}

template<typename T>
inline bool TMembers<T>::initialise(const std::uint32_t count) noexcept
{
    if ((m_count != 0u) || !m_items.initialise(count))
    {
        return false;
    }
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        if (m_items.emplace() != static_cast<std::int32_t>(index))
        {
            m_items.deallocate();
            m_count = 0u;
            return false;
        }
        ++m_count;
    }
    return true;
}

template<typename T>
inline std::uint32_t TMembers<T>::count() const noexcept { return m_count; }

template<typename T>
inline T& TMembers<T>::operator[](const std::uint32_t index) noexcept
{
    return *m_items.get_object(static_cast<std::int32_t>(index));
}

template<typename T>
inline const T& TMembers<T>::operator[](const std::uint32_t index) const noexcept
{
    return *m_items.get_object(static_cast<std::int32_t>(index));
}

template<typename T>
inline memory::SMemoryAttribution TMembers<T>::memory_attribution() const noexcept
{
    auto result = m_items.memory_attribution();
    for (std::uint32_t index = 0u; index < m_count; ++index)
    {
        result = memory::combine_memory_attribution(result, (*this)[index].memory_attribution());
    }
    return result;
}

template<typename T>
inline void TMembers<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
{
    m_items.unsafe_replace_memory_context_without_accounting(source, target);
    for (std::uint32_t index = 0u; index < m_count; ++index)
    {
        (*this)[index].unsafe_replace_memory_context_without_accounting(source, target);
    }
}

}   //  namespace shader

#endif   //  RENDERING_SHADER_DATA_HPP_INCLUDED
