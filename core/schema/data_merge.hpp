
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_merge.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Transactional aggregation of prepared instance and bulk data documents.

#pragma once

#ifndef SCHEMA_DATA_MERGE_HPP_INCLUDED
#define SCHEMA_DATA_MERGE_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"

namespace schema
{

//  Locations borrow prepared inputs. Result-validation locations are cleared;
//  success clears the whole diagnostic. No output owns or binds a schema.
enum class EDataMergeStage : std::uint8_t
{
    none = 0u,
    preconditions,
    first_source_validation,
    second_source_validation,
    document_merge,
    result_validation
};

enum class EDataMergeReason : std::uint8_t
{
    none = 0u,
    invalid_schema,
    invalid_source,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

struct SInstanceMergeDiagnostic
{
    EDataMergeStage stage{ EDataMergeStage::none };
    EDataMergeReason reason{ EDataMergeReason::none };
    CInstanceHandle first;
    CInstanceHandle second;
    SInstanceDiagnostic validation;
};

struct SBulkDataMergeDiagnostic
{
    EDataMergeStage stage{ EDataMergeStage::none };
    EDataMergeReason reason{ EDataMergeReason::none };
    CBulkDataHandle first;
    CBulkDataHandle second;
    SBulkDataDiagnostic validation;
};

[[nodiscard]] bool merge_instances(
    const CInstanceDocumentQuery& first,
    const CInstanceDocumentQuery& second,
    const CResolvedSchema& schema,
    CLiveDocument& destination,
    SInstanceMergeDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool merge_bulk_data(
    const CBulkDataDocumentQuery& first,
    const CBulkDataDocumentQuery& second,
    const CResolvedSchema& schema,
    CLiveDocument& destination,
    SBulkDataMergeDiagnostic& diagnostic) noexcept;

}   // namespace schema

#endif // SCHEMA_DATA_MERGE_HPP_INCLUDED
