
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Shared options and diagnostics for completed instance and bulk data blocks.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"

namespace schema
{

struct SDataBlockLoadOptions
{
    bool compare_embedded{ false };
};

enum class EDataBlockStage : std::uint8_t
{
    none = 0u,
    preconditions,
    format_validation,
    schema_binding,
    specialised_loading,
    description_preparation,
    materialisation,
    document_promotion,
    document_bake,
    assembly
};

enum class EDataBlockReason : std::uint8_t
{
    none = 0u,
    destination_not_empty,
    invalid_input,
    invalid_option,
    invalid_document,
    unsupported_document_version,
    missing_property,
    unknown_property,
    invalid_property_type,
    wrong_content_type,
    unsupported_format_version,
    invalid_alignment,
    size_overflow,
    storage_limit,
    file_length_mismatch,
    nonzero_padding,
    schema_unavailable,
    schema_binding_failed,
    specialised_failure,
    allocation_failed,
    description_preparation_failed,
    document_promotion_failed,
    document_bake_failed,
    assembly_failed
};

enum class EDataBlockLocationKind : std::uint8_t
{
    none = 0u,
    source_live_node,
    source_baked_value,
    file_byte
};

//  Only the discriminated field is meaningful; all others remain defaulted.
//  Handles borrow identity from the caller's source, not temporary output.
//  Diagnostics retain no storage. File offsets start at the complete file base.
struct SDataBlockLocation
{
    EDataBlockLocationKind kind{ EDataBlockLocationKind::none };
    CNodeKey live_node;
    CBakedValueIndex baked_value;
    std::uint64_t byte_offset{};
};

struct SDataBlockDiagnostic
{
    EDataBlockStage stage{ EDataBlockStage::none };
    EDataBlockReason reason{ EDataBlockReason::none };
    SDataBlockLocation location;
};

//  A role failure keeps its precise reason here; any occurrence must obey the
//  same source lifetime rule as block.location. Clear unmapped temporary handles.
struct SInstancesBlockDiagnostic
{
    SDataBlockDiagnostic block;
    SInstanceDiagnostic role;
};

struct SBulkDataBlockDiagnostic
{
    SDataBlockDiagnostic block;
    SBulkDataDiagnostic role;
};

}   // namespace schema

#endif // SCHEMA_DATA_BLOCK_HPP_INCLUDED
