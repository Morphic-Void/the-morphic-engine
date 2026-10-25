
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Aggregate authoring admission and caller-requested whole-document validation.

#include "schema/schema.hpp"
#include "schema/data_document_validation.hpp"
#include "schema/document_construction.hpp"
#include "schema/document_merge.hpp"
#include "schema/types_admission.hpp"
#include "data_model/document_copy.hpp"
#include "data_model/document_translation.hpp"

#include <utility>

namespace schema
{

using detail::CDocumentRead;
using detail::SOccurrence;
using document_translation::detail::CDocumentConstruction;

bool CSchema::admit(const CDocumentRead& source, SSchemaDiagnostic& diagnostic, detail::STypesCatalogue* catalogue) noexcept
{
    diagnostic = {};
    const auto fail = [&](const ESchemaReason reason, const SOccurrence value) noexcept
    {
        diagnostic.stage = ESchemaStage::structure;
        diagnostic.reason = reason;
        diagnostic.first = CSchemaHandle{ value };
        return false;
    };
    if (!source.is_ready() || (source.value_kind(source.root()) != EDocumentValueKind::object))
    {
        return fail(ESchemaReason::invalid_source, source.root());
    }
    //  The same bound applies to opaque values and descriptors, so admitted
    //  authoring documents can always pass through the bounded copy/merge walk.
    SOccurrence item = source.root();
    unsigned depth = 0u;
    while (item)
    {
        if (depth >= document_limits::k_max_recursion_depth)
        {
            return fail(ESchemaReason::storage_limit, item);
        }
        const SOccurrence child = source.first_child(item);
        if (child)
        {
            item = child;
            ++depth;
            continue;
        }
        while (item != source.root() && !source.next_sibling(item))
        {
            item = source.parent(item);
            --depth;
        }
        item = (item == source.root()) ? SOccurrence{} : source.next_sibling(item);
    }
    for (SOccurrence section = source.first_child(source.root()); section; section = source.next_sibling(section))
    {
        const CStringView name = source.name(section);
        if (!(name == CStringView{ "types" }) && !(name == CStringView{ "instances" }) && !(name == CStringView{ "data" }))
        {
            return fail(ESchemaReason::unknown_property, section);
        }
        if (source.value_kind(section) != EDocumentValueKind::object)
        {
            return fail(ESchemaReason::invalid_source, section);
        }
        if (name == CStringView{ "types" })
        {
            detail::STypesAdmissionDiagnostic error;
            detail::STypesCatalogue local;
            if (!detail::admit_types_section(source, section, error, catalogue ? *catalogue : local))
            {
                return fail((error.reason == EReason::allocation_failed) ? ESchemaReason::allocation_failed :
                    ((error.reason == EReason::storage_limit) ? ESchemaReason::storage_limit : ESchemaReason::invalid_types), error.first);
            }
        }
        else
        {
            detail::SDataValidationDiagnostic error;
            if (!detail::admit_data_section(source, section, name == CStringView{ "data" }, error))
            {
                return fail((error.reason == detail::EDataValidationReason::unknown_property) ? ESchemaReason::unknown_property :
                    ((error.reason == detail::EDataValidationReason::allocation_failed) ? ESchemaReason::allocation_failed :
                    ((error.reason == detail::EDataValidationReason::storage_limit) ? ESchemaReason::storage_limit : ESchemaReason::invalid_source)), error.occurrence);
            }
        }
    }
    return true;
}

bool CSchema::merge_from(const CSchemaDocumentQuery& first, const CSchemaDocumentQuery& second,
    SSchemaDiagnostic& diagnostic) noexcept
{
    detail::STypesCatalogue first_types, second_types;
    if (!admit(first.m_read, diagnostic, &first_types)) return false;
    if (!admit(second.m_read, diagnostic, &second_types))
    {
        diagnostic.second = diagnostic.first;
        diagnostic.first = {};
        return false;
    }
    detail::STypesAdmissionDiagnostic types;
    if (!detail::compatible_type_sections(first.m_read, first_types, second.m_read, second_types, types))
    {
        diagnostic.stage = ESchemaStage::merging;
        diagnostic.reason = (types.reason == EReason::allocation_failed) ? ESchemaReason::allocation_failed :
            (types.reason == EReason::storage_limit ? ESchemaReason::storage_limit :
                (types.unestablished ? ESchemaReason::unestablished_compatibility : ESchemaReason::incompatible_definition));
        diagnostic.first = CSchemaHandle{ types.first };
        diagnostic.second = CSchemaHandle{ types.second };
        return false;
    }
    CLiveDocument staged;
    detail::SDocumentMergeDiagnostic error;
    if (!detail::merge_document_sections(first.m_read, second.m_read, staged, error))
    {
        diagnostic.stage = ESchemaStage::merging;
        diagnostic.reason = detail::document_merge_reason<ESchemaReason>(error.reason);
        diagnostic.first = CSchemaHandle{ error.first };
        diagnostic.second = CSchemaHandle{ error.second };
        return false;
    }
    m_document = std::move(staged);
    return true;
}

bool CSchema::initialise(const std::size_t capacity) noexcept
{
    return !m_document.is_ready() && m_document.initialise(capacity);
}

bool CSchema::try_adopt(CLiveDocument&& document, SSchemaDiagnostic& diagnostic) noexcept
{
    if (!admit(CDocumentRead{ document }, diagnostic))
    {
        return false;
    }
    if (&document != &m_document)
    {
        m_document = std::move(document);
    }
    return true;
}

bool CSchema::copy_from(const CSchemaDocumentQuery& source, SSchemaDiagnostic& diagnostic) noexcept
{
    if (!admit(source.m_read, diagnostic))
    {
        return false;
    }
    CLiveDocument staged;
    detail::SDocumentSection sections[3]{};
    std::size_t count{};
    for (SOccurrence section = source.m_read.first_child(source.m_read.root()); section; section = source.m_read.next_sibling(section))
    {
        sections[count++] = { &source.m_read, section };
    }
    detail::SDocumentBuildDiagnostic error;
    if (!detail::copy_document_sections(sections, count, staged, error))
    {
        diagnostic.stage = ESchemaStage::copying;
        diagnostic.reason = (error.reason == detail::EDocumentBuildReason::storage_limit) ? ESchemaReason::storage_limit :
            ((error.stage == detail::EDocumentBuildStage::preparation || error.reason == detail::EDocumentBuildReason::allocation_failed) ?
                ESchemaReason::allocation_failed : ESchemaReason::construction_failed);
        diagnostic.first = CSchemaHandle{ error.occurrence };
        return false;
    }
    m_document = std::move(staged);
    return true;
}

bool CSchema::save(CBakedDocumentBlock& destination, SSchemaDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (destination.buffer_capacity())
    {
        diagnostic.stage = ESchemaStage::saving;
        diagnostic.reason = ESchemaReason::invalid_source;
        return false;
    }
    if (!admit(CDocumentRead{ m_document }, diagnostic))
    {
        return false;
    }
    CBakedDocumentBlock staged;
    if (!document_translation::bake(m_document, staged))
    {
        diagnostic.stage = ESchemaStage::saving;
        diagnostic.reason = ESchemaReason::construction_failed;
        return false;
    }
    destination = std::move(staged);
    return true;
}

}   // namespace schema
