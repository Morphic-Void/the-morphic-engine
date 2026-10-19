
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_composition.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Validated section copying with transactional publication.

#include "schema/document_composition.hpp"
#include "schema/document_construction.hpp"
#include "schema/data_document_validation.hpp"
#include "data_model/document_copy.hpp"

#include <utility>

namespace schema
{

using detail::CDocumentRead;
using detail::SOccurrence;
using document_translation::detail::CDocumentConstruction;

template<class TAccess, class TDiagnostic>
[[nodiscard]] static bool validate_composition_data(const CDocumentRead& source, const CResolvedSchema& schema,
    const bool bulk, TDiagnostic& domain_detail, SDocumentCompositionDiagnostic& diagnostic) noexcept
{
    diagnostic.stage = bulk ? EDocumentCompositionStage::bulk_data_validation : EDocumentCompositionStage::instances_validation;
    detail::SDataValidationDiagnostic error;
    if (detail::validate_data_document(source, schema, bulk, error))
    {
        return true;
    }
    diagnostic.reason = (error.reason == detail::EDataValidationReason::allocation_failed) ? EDocumentCompositionReason::allocation_failed :
        ((error.reason == detail::EDataValidationReason::storage_limit) ? EDocumentCompositionReason::storage_limit : EDocumentCompositionReason::invalid_source);
    domain_detail.reason = detail::data_validation_reason<decltype(domain_detail.reason)>(error.reason);
    domain_detail.occurrence = TAccess::make(error.occurrence);
    return false;
}

[[nodiscard]] static bool composition_failure(const detail::SDocumentBuildDiagnostic& error,
    const unsigned domain, SDocumentCompositionDiagnostic& diagnostic) noexcept
{
    diagnostic.stage = EDocumentCompositionStage::document_composition;
    diagnostic.reason = (error.reason == detail::EDocumentBuildReason::allocation_failed) ? EDocumentCompositionReason::allocation_failed :
        ((error.reason == detail::EDocumentBuildReason::storage_limit) ? EDocumentCompositionReason::storage_limit : EDocumentCompositionReason::construction_failed);
    if (domain == 0u) diagnostic.types.occurrence = detail::SSchemaHandleAccess::make(error.occurrence);
    else if (domain == 1u) diagnostic.instances.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
    else diagnostic.bulk_data.occurrence = detail::SBulkDataHandleAccess::make(error.occurrence);
    return false;
}

[[nodiscard]] static bool compose(const CSchemaDocumentQuery& types, const CInstanceDocumentQuery* const instances,
    const CBulkDataDocumentQuery* const bulk, CLiveDocument& destination, SDocumentCompositionDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CLiveDocument staged;
    {
        diagnostic.stage = EDocumentCompositionStage::types_validation;
        const CDocumentRead& source = detail::SDocumentQueryAccess::read(types);
        const auto invalid_types = [&](const EReason reason, const SOccurrence occurrence) noexcept
        {
            diagnostic.reason = EDocumentCompositionReason::invalid_source;
            diagnostic.types.reason = reason;
            diagnostic.types.occurrence = detail::SSchemaHandleAccess::make(occurrence);
            return false;
        };
        if (!source.is_ready() || (source.value_kind(source.root()) != EDocumentValueKind::object))
        {
            return invalid_types(EReason::invalid_input, source.root());
        }
        for (SOccurrence field = source.first_child(source.root()); field; field = source.next_sibling(field))
        {
            if (!(source.name(field) == CStringView{ "types" }))
            {
                return invalid_types(EReason::unknown_property, field);
            }
        }
        const SOccurrence definitions = source.object_child(source.root(), CStringView{ "types" });
        if (!definitions)
        {
            return invalid_types(EReason::missing_property, source.root());
        }
        CResolvedSchema schema;
        if (!schema.resolve(types, diagnostic.types))
        {
            diagnostic.reason = (diagnostic.types.reason == EReason::allocation_failed) ? EDocumentCompositionReason::allocation_failed :
                ((diagnostic.types.reason == EReason::storage_limit) ? EDocumentCompositionReason::storage_limit : EDocumentCompositionReason::invalid_source);
            return false;
        }
        if (instances && !validate_composition_data<detail::SInstanceHandleAccess>(detail::SDocumentQueryAccess::read(*instances),
            schema, false, diagnostic.instances, diagnostic))
        {
            return false;
        }
        if (bulk && !validate_composition_data<detail::SBulkDataHandleAccess>(detail::SDocumentQueryAccess::read(*bulk),
            schema, true, diagnostic.bulk_data, diagnostic))
        {
            return false;
        }
        const CDocumentRead* const sources[]{ &source, (instances ? &detail::SDocumentQueryAccess::read(*instances) : nullptr),
            (bulk ? &detail::SDocumentQueryAccess::read(*bulk) : nullptr) };
        const CStringView names[]{ CStringView{ "types" }, CStringView{ "instances" }, CStringView{ "data" } };
        {
            detail::CDocumentStrings strings;
            std::uint64_t structural_nodes = 2u;
            for (unsigned i = 0u; i < 3u; ++i)
            {
                if (!sources[i])
                {
                    continue;
                }
                const SOccurrence section = sources[i]->object_child(sources[i]->root(), names[i]);
                if (!section) structural_nodes += 2u;
                if (!strings.name(names[i]) || !strings.collect(*sources[i], section, 0u))
                {
                    return composition_failure(strings.diagnostic(), i, diagnostic);
                }
            }
            if (!strings.initialise(staged, structural_nodes) || !strings.preload(staged))
            {
                return composition_failure(strings.diagnostic(), 0u, diagnostic);
            }
        }
        for (unsigned i = 0u; i < 3u; ++i)
        {
            if (!sources[i])
            {
                continue;
            }
            const SOccurrence section = sources[i]->object_child(sources[i]->root(), names[i]);
            detail::SDocumentBuildDiagnostic error;
            const CNodeKey copied = section ?
                detail::copy_document_subtree(*sources[i], section, staged, 0u, error) :
                staged.create_object(names[i]);
            if (!copied || !CDocumentConstruction::append(staged, staged.root(), copied))
            {
                return composition_failure(error, i, diagnostic);
            }
        }
    }
    destination = std::move(staged);
    diagnostic = {};
    return true;
}

bool compose_document(const CSchemaDocumentQuery& types, const CInstanceDocumentQuery& instances,
    CLiveDocument& destination, SDocumentCompositionDiagnostic& diagnostic) noexcept
{
    return compose(types, &instances, nullptr, destination, diagnostic);
}

bool compose_document(const CSchemaDocumentQuery& types, const CBulkDataDocumentQuery& bulk_data,
    CLiveDocument& destination, SDocumentCompositionDiagnostic& diagnostic) noexcept
{
    return compose(types, nullptr, &bulk_data, destination, diagnostic);
}

bool compose_document(const CSchemaDocumentQuery& types, const CInstanceDocumentQuery& instances,
    const CBulkDataDocumentQuery& bulk_data, CLiveDocument& destination, SDocumentCompositionDiagnostic& diagnostic) noexcept
{
    return compose(types, &instances, &bulk_data, destination, diagnostic);
}

}   // namespace schema
