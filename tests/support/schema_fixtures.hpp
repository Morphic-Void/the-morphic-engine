
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  Test fixture publication for aggregate authoring documents.

#pragma once

#include "schema/schema.hpp"
#include "schema/baked_data_block.hpp"
#include "tests/support/document_helpers.hpp"

namespace tests
{

template<class TSource, class TRole, class TTypes, class TDiagnostic>
[[nodiscard]] inline bool prepare_and_attach_runtime(const TSource& source, CBakedDocumentBlock& description,
    CByteBuffer& payload, TRole& role, TTypes& types, TDiagnostic& diagnostic) noexcept
{
    CLiveDocument output;
    return source.prepare_output(output, payload, diagnostic) && document_translation::bake(output, description) &&
        role.set_document(description.document()) && role.attach_types(types, payload.const_view(), diagnostic);
}

[[nodiscard]] inline bool bake_schema_document(const std::string& text, schema::CBakedDataBlock& block,
    schema::SSchemaDiagnostic& diagnostic) noexcept
{
    CLiveDocument document;
    schema::CSchema source;
    return parse_document(text, document) && source.try_adopt(std::move(document), diagnostic) && source.bake(block, diagnostic);
}

template<class TTypes>
[[nodiscard]] inline bool bake_schema_sections(const TTypes& types, const schema::CSchemaDocumentQuery& instances,
    const schema::CSchemaDocumentQuery& bulk, schema::CBakedDataBlock& block, schema::SSchemaDiagnostic& diagnostic) noexcept
{
    schema::CSchema combined;
    return combined.import_from(types, diagnostic) &&
        combined.compose_from(combined.document_query(), instances, bulk, diagnostic) && combined.bake(block, diagnostic);
}

//  Keep raw input fixtures available for immutability and source-handle checks.
//  Composition and compatibility are provided solely by CSchema.
[[nodiscard]] inline bool merge_schema_documents(const schema::CSchemaDocumentQuery& first,
    const schema::CSchemaDocumentQuery& second, CLiveDocument& destination, schema::SSchemaDiagnostic& diagnostic) noexcept
{
    schema::CSchema result;
    if (!result.merge_from(first, second, diagnostic)) return false;
    destination = std::move(result.edit());
    return true;
}

}   // namespace tests
