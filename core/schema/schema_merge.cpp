
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_merge.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Original-document merging with private construction and final resolution.

#include "schema/schema_merge.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/document_merge.hpp"
#include "data_model/live_document.hpp"

#include <utility>

namespace schema
{

[[nodiscard]] static ESchemaMergeReason resolution_reason(const EReason reason, const ESchemaMergeReason fallback) noexcept
{
    switch (reason)
    {
        case EReason::allocation_failed:
        {
            return ESchemaMergeReason::allocation_failed;
        }
        case EReason::storage_limit:
        {
            return ESchemaMergeReason::storage_limit;
        }
        default:
        {
            return fallback;
        }
    }
}

[[nodiscard]] static bool validate_source(const CSchemaDocumentQuery& source, CResolvedSchema& resolved,
    const bool first, SSchemaMergeDiagnostic& diagnostic) noexcept
{
    if (resolved.resolve(source, diagnostic.validation))
    {
        return true;
    }
    diagnostic.stage = first ? ESchemaMergeStage::first_source_validation : ESchemaMergeStage::second_source_validation;
    diagnostic.reason = resolution_reason(diagnostic.validation.reason, ESchemaMergeReason::invalid_source);
    (first ? diagnostic.first : diagnostic.second) = diagnostic.validation.occurrence;
    return false;
}

[[nodiscard]] static bool compatible(const CSchemaDocumentQuery& first, const CSchemaHandle types,
    const CResolvedSchema& a, const CResolvedSchema& b, SSchemaMergeDiagnostic& diagnostic) noexcept
{
    CDeclarationCompatibility comparison{ a, b, EDeclarationMatch::compatibility };
    for (CSchemaHandle group = first.first_child(types); group; group = first.next_sibling(group))
    {
        for (CSchemaHandle item = first.first_child(group); item; item = first.next_sibling(item))
        {
            const CSchemaIndex other = b.find_declaration(first.name(item));
            if (!other)
            {
                continue;
            }
            const ETypeMatchResult result = comparison.compare(a.map_occurrence(item), other);
            if (result != ETypeMatchResult::match)
            {
                SDeclaration type;
                (void)b.declaration(other, type);
                diagnostic.stage = ESchemaMergeStage::compatibility;
                diagnostic.reason = (result == ETypeMatchResult::allocation_failed) ? ESchemaMergeReason::allocation_failed :
                    ((result == ETypeMatchResult::mismatch) ? ESchemaMergeReason::incompatible_type : ESchemaMergeReason::invalid_source);
                diagnostic.first = item;
                diagnostic.second = type.source;
                return false;
            }
        }
    }
    return true;
}

bool merge_type_schemas(const CSchemaDocumentQuery& first, const CSchemaDocumentQuery& second,
    CLiveDocument& destination, SSchemaMergeDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CLiveDocument staged;
    {
        CResolvedSchema a, b, result;
        if (!validate_source(first, a, true, diagnostic) || !validate_source(second, b, false, diagnostic))
        {
            return false;
        }
        const CSchemaHandle left = first.object_child(first.root(), CStringView{ "types" });
        if (!compatible(first, left, a, b, diagnostic))
        {
            return false;
        }
        diagnostic.stage = ESchemaMergeStage::document_merge;
        detail::SDocumentMergeDiagnostic error;
        if (!detail::merge_document_section(detail::SDocumentQueryAccess::read(first),
            detail::SDocumentQueryAccess::read(second), CStringView{ "types" }, false, staged, error))
        {
            diagnostic.reason = detail::document_merge_reason<ESchemaMergeReason>(error.reason);
            diagnostic.first = detail::SSchemaHandleAccess::make(error.first);
            diagnostic.second = detail::SSchemaHandleAccess::make(error.second);
            return false;
        }
        if (!result.resolve(staged, diagnostic.validation))
        {
            diagnostic.stage = ESchemaMergeStage::result_validation;
            diagnostic.reason = resolution_reason(diagnostic.validation.reason, ESchemaMergeReason::invalid_result);
            const EReason reason = diagnostic.validation.reason;
            const EStage stage = diagnostic.validation.stage;
            diagnostic.validation = {};
            diagnostic.validation.reason = reason;
            diagnostic.validation.stage = stage;
            return false;
        }
    }
    destination = std::move(staged);
    diagnostic = {};
    return true;
}

}   // namespace schema
