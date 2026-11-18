
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Version-1 Platform report layout, limits and named collections.

#pragma once

#include "framework/platform/report_layout.inl"
#include "schema/data_operation.hpp"

namespace platform::reports
{

inline constexpr std::uint32_t k_report_version = 1u;
inline constexpr platform_report_v1::PlatformCaptureLimits k_capture_limits{
    3u, 3u, 3u, 64u, 256u, 256u, 256u, 256u, 8192u, 16384u, 4096u, 32768u, 65536u,
    UINT64_C(64) << 20u, UINT64_C(16) << 20u, UINT64_C(5000000000) };

inline constexpr platform_report_v1::PlatformSchemaLimits schema_limits(const schema::SDataOperationLimits limits) noexcept
{
    return { limits.inspected_source_bytes, limits.owned_bytes, limits.result_bytes, limits.work_credits,
        limits.document_nodes, limits.records, limits.depth };
}

struct SCollectionName { const char* type; const char* name; };
inline constexpr SCollectionName k_collections[] = {
    { "PlatformAdapter", "adapters" }, { "PlatformDisplaySource", "sources" },
    { "PlatformDisplay", "displays" }, { "PlatformDisplayPath", "paths" },
    { "PlatformModeCatalogue", "mode_catalogues" }, { "PlatformDisplayMode", "modes" },
    { "PlatformNativeIdentity", "native_identities" }, { "PlatformByte", "identity_bytes" },
    { "PlatformQueryResult", "queries" }
};

[[nodiscard]] inline CByteConstView schema_source() noexcept
{
    static constexpr char source[] =
#include "framework/platform/report_types.inl"
    ;
    return { reinterpret_cast<const std::uint8_t*>(source), (sizeof(source) - 1u), 1u };
}

}   //  namespace platform::reports
