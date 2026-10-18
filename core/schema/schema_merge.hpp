
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_merge.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Transactional merging of independently valid type-schema documents.

#pragma once

#ifndef SCHEMA_MERGE_HPP_INCLUDED
#define SCHEMA_MERGE_HPP_INCLUDED

#include "schema/resolved_schema.hpp"

namespace schema
{

enum class ESchemaMergeStage : std::uint8_t
{
    none = 0u,
    first_source_validation,
    second_source_validation,
    compatibility,
    document_merge,
    result_validation
};

enum class ESchemaMergeReason : std::uint8_t
{
    none = 0u,
    invalid_source,
    incompatible_type,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

//  Locations borrow the original inputs. Source-validation detail belongs to
//  the source selected by stage; result-validation detail has no temporary
//  handles or ranges. No diagnostic owns storage. Success clears all fields.
struct SSchemaMergeDiagnostic
{
    ESchemaMergeStage stage{ ESchemaMergeStage::none };
    ESchemaMergeReason reason{ ESchemaMergeReason::none };
    CSchemaHandle first;
    CSchemaHandle second;
    SDiagnostic validation;
};

//  Merge only types, preserving declarations and combining presentation.
//  Destination may be occupied or supplied as either/both sources. Failure
//  preserves all inputs and destination; success replaces destination only.
//  Borrowed inputs must remain unchanged throughout this synchronous call.
[[nodiscard]] bool merge_type_schemas(
    const CSchemaDocumentQuery& first,
    const CSchemaDocumentQuery& second,
    CLiveDocument& destination,
    SSchemaMergeDiagnostic& diagnostic) noexcept;

}   // namespace schema

#endif // SCHEMA_MERGE_HPP_INCLUDED
