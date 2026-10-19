
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_composition.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Transactional document-only schema boundary operation.

#pragma once

#ifndef SCHEMA_DOCUMENT_COMPOSITION_HPP_INCLUDED
#define SCHEMA_DOCUMENT_COMPOSITION_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"

namespace schema
{

//  Source locations borrow inputs; no diagnostic retains temporary result handles.
//  Occupied and source-aliased destinations are supported. Failure preserves them.
enum class EDocumentCompositionStage : std::uint8_t
{
    none = 0u,
    types_validation,
    instances_validation,
    bulk_data_validation,
    document_composition
};

enum class EDocumentCompositionReason : std::uint8_t
{
    none = 0u,
    invalid_source,
    allocation_failed,
    storage_limit,
    construction_failed
};

struct SDocumentCompositionDiagnostic
{
    EDocumentCompositionStage stage{ EDocumentCompositionStage::none };
    EDocumentCompositionReason reason{ EDocumentCompositionReason::none };
    SDiagnostic types;
    SInstanceDiagnostic instances;
    SBulkDataDiagnostic bulk_data;
};

[[nodiscard]] bool compose_document(
    const CSchemaDocumentQuery& types,
    const CInstanceDocumentQuery& instances,
    CLiveDocument& destination,
    SDocumentCompositionDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool compose_document(
    const CSchemaDocumentQuery& types,
    const CBulkDataDocumentQuery& bulk_data,
    CLiveDocument& destination,
    SDocumentCompositionDiagnostic& diagnostic) noexcept;

[[nodiscard]] bool compose_document(
    const CSchemaDocumentQuery& types,
    const CInstanceDocumentQuery& instances,
    const CBulkDataDocumentQuery& bulk_data,
    CLiveDocument& destination,
    SDocumentCompositionDiagnostic& diagnostic) noexcept;

}   // namespace schema

#endif // SCHEMA_DOCUMENT_COMPOSITION_HPP_INCLUDED
