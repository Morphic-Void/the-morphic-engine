
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_merge.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Transactional aggregation of prepared instance and bulk data documents.

#include "schema/data_merge.hpp"
#include "schema/data_document_validation.hpp"
#include "schema/document_merge.hpp"
#include "data_model/live_document.hpp"

#include <type_traits>
#include <utility>

namespace schema
{

template<class TAccess, class TDiagnostic>
[[nodiscard]] static bool validate_source(const detail::CDocumentRead& document, const CResolvedSchema& schema,
    const bool bulk, const EDataMergeStage stage, TDiagnostic& diagnostic) noexcept
{
    detail::SDataValidationDiagnostic error;
    if (detail::validate_data_document(document, schema, bulk, error))
    {
        return true;
    }
    diagnostic.stage = stage;
    diagnostic.reason = (error.reason == detail::EDataValidationReason::allocation_failed) ? EDataMergeReason::allocation_failed :
        ((error.reason == detail::EDataValidationReason::storage_limit) ? EDataMergeReason::storage_limit :
            ((stage == EDataMergeStage::result_validation) ? EDataMergeReason::invalid_result : EDataMergeReason::invalid_source));
    diagnostic.validation.reason = detail::data_validation_reason<decltype(diagnostic.validation.reason)>(error.reason);
    if (stage != EDataMergeStage::result_validation)
    {
        diagnostic.validation.occurrence = TAccess::make(error.occurrence);
        (stage == EDataMergeStage::first_source_validation ? diagnostic.first : diagnostic.second) = diagnostic.validation.occurrence;
    }
    return false;
}

template<class TAccess, class TDiagnostic>
[[nodiscard]] static bool merge_data(const detail::CDocumentRead& first, const detail::CDocumentRead& second,
    const CResolvedSchema& schema, const bool bulk, CLiveDocument& destination, TDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!schema.is_ready() || detail::SDocumentQueryAccess::backs(schema, destination))
    {
        diagnostic.stage = EDataMergeStage::preconditions;
        diagnostic.reason = EDataMergeReason::invalid_schema;
        return false;
    }
    CLiveDocument staged;
    {
        if (!validate_source<TAccess>(first, schema, bulk, EDataMergeStage::first_source_validation, diagnostic) ||
            !validate_source<TAccess>(second, schema, bulk, EDataMergeStage::second_source_validation, diagnostic))
        {
            return false;
        }
        detail::SDocumentMergeDiagnostic error;
        if (!detail::merge_document_section(first, second, CStringView{ bulk ? "data" : "instances" }, bulk, staged, error))
        {
            diagnostic.stage = EDataMergeStage::document_merge;
            diagnostic.first = TAccess::make(error.first);
            diagnostic.second = TAccess::make(error.second);
            switch (error.reason)
            {
                case detail::EDocumentMergeReason::conflicting_value:
                {
                    diagnostic.reason = EDataMergeReason::conflicting_value;
                    break;
                }
                case detail::EDocumentMergeReason::allocation_failed:
                {
                    diagnostic.reason = EDataMergeReason::allocation_failed;
                    break;
                }
                case detail::EDocumentMergeReason::storage_limit:
                {
                    diagnostic.reason = EDataMergeReason::storage_limit;
                    break;
                }
                default:
                {
                    diagnostic.reason = EDataMergeReason::construction_failed;
                    break;
                }
            }
            return false;
        }
        if (!validate_source<TAccess>(detail::CDocumentRead{ staged }, schema, bulk, EDataMergeStage::result_validation, diagnostic))
        {
            return false;
        }
    }
    destination = std::move(staged);
    diagnostic = {};
    return true;
}

bool merge_instances(const CInstanceDocumentQuery& first, const CInstanceDocumentQuery& second,
    const CResolvedSchema& schema, CLiveDocument& destination, SInstanceMergeDiagnostic& diagnostic) noexcept
{
    return merge_data<detail::SInstanceHandleAccess>(detail::SDocumentQueryAccess::read(first),
        detail::SDocumentQueryAccess::read(second), schema, false, destination, diagnostic);
}

bool merge_bulk_data(const CBulkDataDocumentQuery& first, const CBulkDataDocumentQuery& second,
    const CResolvedSchema& schema, CLiveDocument& destination, SBulkDataMergeDiagnostic& diagnostic) noexcept
{
    return merge_data<detail::SBulkDataHandleAccess>(detail::SDocumentQueryAccess::read(first),
        detail::SDocumentQueryAccess::read(second), schema, true, destination, diagnostic);
}

}   // namespace schema
