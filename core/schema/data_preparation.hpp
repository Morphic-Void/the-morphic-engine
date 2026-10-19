
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_preparation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Transactional document-only schema boundary operation.

#pragma once

#ifndef SCHEMA_DATA_PREPARATION_HPP_INCLUDED
#define SCHEMA_DATA_PREPARATION_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"

namespace schema
{

//  Source locations borrow inputs; no diagnostic retains temporary result handles.
//  Occupied and source-aliased destinations are supported. Failure preserves them.
enum class EDataPreparationStage : std::uint8_t
{
    none = 0u,
    preconditions,
    source_validation,
    document_preparation,
    result_validation
};

enum class EDataPreparationReason : std::uint8_t
{
    none = 0u,
    invalid_schema,
    invalid_source,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

struct SInstancePreparationDiagnostic
{
    EDataPreparationStage stage{ EDataPreparationStage::none };
    EDataPreparationReason reason{ EDataPreparationReason::none };
    SInstanceDiagnostic detail;
};

struct SBulkDataPreparationDiagnostic
{
    EDataPreparationStage stage{ EDataPreparationStage::none };
    EDataPreparationReason reason{ EDataPreparationReason::none };
    SBulkDataDiagnostic detail;
};

[[nodiscard]] bool prepare_instances_document(
    const CInstanceDocumentQuery& source,
    const CResolvedSchema& source_schema,
    CLiveDocument& destination,
    SInstancePreparationDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_instances_document(
    const CInstanceDocumentQuery& source,
    const CResolvedSchema& source_schema,
    const CByteConstView& payload,
    const bool compare_embedded,
    CLiveDocument& destination,
    SInstancePreparationDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_bulk_data_document(
    const CBulkDataDocumentQuery& source,
    const CResolvedSchema& source_schema,
    CLiveDocument& destination,
    SBulkDataPreparationDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool prepare_bulk_data_document(
    const CBulkDataDocumentQuery& source,
    const CResolvedSchema& source_schema,
    const CByteConstView& payload,
    const bool compare_embedded,
    CLiveDocument& destination,
    SBulkDataPreparationDiagnostic& diagnostic) noexcept;

}   // namespace schema

#endif // SCHEMA_DATA_PREPARATION_HPP_INCLUDED
