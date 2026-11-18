
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_operation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Shared finite limits and durable diagnostics for reference-aware operations.

#pragma once

#include "schema/data_block.hpp"

#include <cstdint>

namespace schema
{

struct SDataOperationLimits
{
    std::uint64_t inspected_source_bytes{ UINT64_C(64) << 20u };
    std::uint64_t owned_bytes{ UINT64_C(64) << 20u };
    std::uint64_t result_bytes{ UINT64_C(16) << 20u };
    std::uint64_t work_credits{ UINT64_C(1) << 22u };
    std::uint32_t document_nodes{ 262144u };
    std::uint32_t records{ 262144u };
    std::uint32_t depth{ 256u };
};

enum class EDataOperationStage : std::uint8_t
{
    none, binding, planning, construction, editing, baking, validation, publication
};

enum class EDataOperationReason : std::uint8_t
{
    none, invalid_input, destination_not_empty, allocation_failed, limit_exceeded,
    reference_context_required, invalid_reference, type_mismatch, cycle,
    naming_conflict, stale_handle, unrepresentable_value
};

enum class EDataOperationLimit : std::uint8_t
{
    none, inspected_source_bytes, owned_bytes, result_bytes, document_nodes,
    records, work_credits, depth
};

//  No borrowed handles or strings: survives rollback and destruction of staging.
struct SDataOperationDiagnostic
{
    EDataOperationStage stage{};
    EDataOperationReason reason{};
    EDataOperationLimit limit{};
    std::uint64_t requested{}, allowed{};
    std::uint32_t source{}, record{};
    char entry[96]{}, path[192]{};
    SDataBlockDiagnostic block;
};

}   // namespace schema
