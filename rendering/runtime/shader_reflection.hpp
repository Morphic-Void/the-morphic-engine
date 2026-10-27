
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_reflection.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Renderer-local reflection of one engine-compiled artifact.

#pragma once

#ifndef RENDERING_RUNTIME_SHADER_REFLECTION_HPP_INCLUDED
#define RENDERING_RUNTIME_SHADER_REFLECTION_HPP_INCLUDED

#include "shader/shader_data.hpp"
#include "containers/ByteBuffers.hpp"
#include "schema/schema.hpp"
#include "platform/platform_defines.hpp"

namespace rendering::shader_reflection
{

namespace detail
{
class CModule;
}

enum class EStatus : std::uint8_t
{
    not_executed = 0u,
    success,
    blocked_by_compilation,
    invalid_input,
    limit_exceeded,
    allocation_failed,
    unsupported_feature,
    unrepresentable_layout,
    reflection_failed,
    validation_failed
};

enum class EReason : std::uint8_t
{
    none = 0u,
    invalid_request,
    compilation_failed,
    malformed_artifact,
    context_mismatch,
    artifact_limit,
    id_limit,
    instruction_limit,
    dependency_limit,
    nesting_limit,
    member_limit,
    string_limit,
    node_limit,
    declaration_limit,
    retained_limit,
    working_limit,
    allocation_size_limit,
    allocation,
    specialization_shape,
    specialization_execution,
    unsupported_specialization,
    unsupported_resource,
    unsupported_interface,
    unsupported_execution,
    unsupported_type,
    library_failure,
    schema_failure,
    layout_conflict,
};

struct SDiagnostic
{
    EReason reason{ EReason::none };
    bool has_word_offset{ false };
    bool has_result_id{ false };
    bool has_member{ false };
    std::uint32_t word_offset{ 0u };
    std::uint32_t result_id{ 0u };
    std::uint32_t member{ 0u };
    //  Scalar copies only; private staging handles never escape.
    std::uint32_t schema_stage{ 0u };
    std::uint32_t schema_reason{ 0u };
    std::uint32_t types_stage{ 0u };
    std::uint32_t types_reason{ 0u };
    std::int32_t library_result{ 0 };
};

//  Service policy, not device limits. Source/debug blobs use the artifact and
//  allocation budgets; string_bytes applies only to retained names/metadata.
struct SLimits
{
    std::size_t artifact_bytes{ 16u * 1024u * 1024u };
    std::uint32_t id_bound{ 262144u };
    std::uint32_t instructions{ 1048576u };
    std::uint32_t resources{ 1024u };
    std::uint32_t interface_values{ 4096u };
    std::uint32_t declarations{ 4096u };
    std::uint32_t members{ 32768u };
    std::uint32_t aggregate_members{ 1024u };
    //  Dependency traversal has an implementation ceiling of 32 levels; service
    //  policy may lower it. Authoring-tree depth is checked again by Core.
    std::uint32_t nesting{ 32u };
    std::uint32_t array_rank{ 8u };
    std::uint32_t document_nodes{ 262144u };
    std::size_t string_bytes{ 4096u };
    std::size_t strings_bytes{ 4u * 1024u * 1024u };
    std::size_t diagnostic_bytes{ 64u * 1024u };
    std::size_t retained_bytes{ 16u * 1024u * 1024u };
    //  Preflight estimate, not a hard peak-allocation guarantee. The existing
    //  engine 2 GiB per-allocation ceiling remains independently enforced.
    std::size_t estimated_working_bytes{ 128u * 1024u * 1024u };
};

struct SRequest
{
    const shader::CShaderCompileReport* compilation{ nullptr };
    CStringView component_identity;
};

struct SStorageStatistics
{
    std::uint64_t estimated_working_bytes{ 0u };
    std::uint64_t retained_bytes{ 0u };
    //  Reserved allowance and cumulative bump usage, including alignment and
    //  temporaries. These remain available after the operation releases storage.
    std::uint64_t reflection_arena_capacity{ 0u };
    std::uint64_t reflection_arena_consumed{ 0u };
};

class CResult final
{

public:
    CResult() noexcept = default;
    ~CResult() noexcept = default;
    CResult(const CResult&) = delete;
    CResult& operator=(const CResult&) = delete;
    CResult(CResult&& source) noexcept;
    CResult& operator=(CResult&& source) noexcept;
    void clear() noexcept;

    [[nodiscard]] EStatus status() const noexcept
    {
        return m_status;
    }

    [[nodiscard]] const SDiagnostic& diagnostic() const noexcept
    {
        return m_diagnostic;
    }

    [[nodiscard]] const SStorageStatistics& storage_statistics() const noexcept
    {
        return m_statistics;
    }
    //  Also true when diagnostic text is omitted by allocation/retention failure.
    [[nodiscard]] bool diagnostics_truncated() const noexcept
    {
        return m_truncated;
    }

    [[nodiscard]] CStringView diagnostics() const noexcept;
    [[nodiscard]] CStringView component_identity() const noexcept;
    [[nodiscard]] CStringView root_name() const noexcept;
    //  Immutable borrow, independent of the compile report after completion.
    //  Expires on clear/destruction, including replacement by move assignment.
    [[nodiscard]] const schema::CSchema* schema() const noexcept;

private:
    friend class CReflection;
    CSimpleString m_identity;
    CSimpleString m_diagnostics;
    schema::CSchema m_schema;
    EStatus m_status{ EStatus::not_executed };
    SDiagnostic m_diagnostic;
    SStorageStatistics m_statistics;
    bool m_truncated{ false };
};

//  Requires an empty destination; invalid invocation preserves an occupied one.
//  Input report/identity must remain stable until the call finishes. Only
//  engine-produced artifacts are supported; this is not a SPIR-V validator.
//  Retained allocations use the ordinary ambient engine context, which must
//  outlive the result. Destroy results before unloading their rendering module.
[[nodiscard]] EStatus reflect(const SRequest& request, const SLimits& limits, CResult& result) noexcept;

//  One reflection instance per rendering-owned job. Prepare on the requester
//  before dispatch; only the executing callback may access it until its terminal
//  response is consumed. Keep the instance at a stable address through that drain.
//  The vendor module and operation arena belong to this instance, while the
//  callback's installed memory context supplies their ordinary allocations.
//  Vendor access and arena reclamation finish before publication; results do not
//  borrow this instance. Its allocator context and rendering code must outlive it.
class CReflection final
{

public:
    CReflection() noexcept = default;
    ~CReflection() noexcept;
    CReflection(const CReflection&) = delete;
    CReflection& operator=(const CReflection&) = delete;
    CReflection(CReflection&&) = delete;
    CReflection& operator=(CReflection&&) = delete;

    //  Acquires and constructs the private owner, without parsing an artifact.
    //  Failure leaves the instance unprepared; repeated successful preparation
    //  is rejected. No stage result is changed by preparation.
    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool ready() const noexcept;
    //  One-shot. Unprepared/used instances preserve the supplied result.
    [[nodiscard]] EStatus run(const SRequest& request, const SLimits& limits, CResult& result) noexcept;

private:
    friend EStatus reflect(const SRequest&, const SLimits&, CResult&) noexcept;
    friend struct SReflectionTestAccess;
    [[nodiscard]] static EStatus reflect_module(const SRequest& request, const SLimits& limits,
        CResult& result, detail::CModule& module) noexcept;
    //  Opaque, aligned engine-owned storage keeps vendor types out of this header.
    //  The module is constructed once during prepare and destroyed before storage.
    CByteBuffer m_module_storage;
    bool m_started{ false };
};

class CReflectJob final
{

public:
    CReflectJob(const SRequest& request, const SLimits& limits = {}) noexcept
        : m_request{ request }, m_limits{ limits }
    {
    }

    CReflectJob(const CReflectJob&) = delete;
    CReflectJob& operator=(const CReflectJob&) = delete;
    CReflectJob(CReflectJob&&) = delete;
    CReflectJob& operator=(CReflectJob&&) = delete;

    [[nodiscard]] bool prepare() noexcept
    {
        return m_reflection.prepare();
    }

    void run() noexcept
    {
        (void)m_reflection.run(m_request, m_limits, result);
    }

    static void MV_STD_ABI_CALL execute(void* const object) noexcept
    {
        static_cast<CReflectJob*>(object)->run();
    }

    CResult result;

private:
    friend struct SReflectionTestAccess;
    const SRequest m_request;
    const SLimits m_limits;
    CReflection m_reflection;
};

}   //  namespace rendering::shader_reflection

#endif   //  RENDERING_RUNTIME_SHADER_REFLECTION_HPP_INCLUDED
