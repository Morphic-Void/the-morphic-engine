
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_merge.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Shared internal document merging and operation-wide string preparation.

#pragma once

#ifndef SCHEMA_DOCUMENT_MERGE_HPP_INCLUDED
#define SCHEMA_DOCUMENT_MERGE_HPP_INCLUDED

#include "schema/document_query.hpp"

namespace schema::detail
{

enum class EDocumentMergeReason : std::uint8_t
{
    none = 0u,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed
};

struct SDocumentMergeDiagnostic
{
    EDocumentMergeReason reason{ EDocumentMergeReason::none };
    SOccurrence first, second;
};

//  Inputs have already passed their domain admission. Target is private, empty
//  staging. Bulk prefix applies only at depth three in the prepared data grammar.
[[nodiscard]] bool merge_document_section(const CDocumentRead& first, const CDocumentRead& second,
    const CStringView& section, const bool bulk_prefix, CLiveDocument& target,
    SDocumentMergeDiagnostic& diagnostic) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DOCUMENT_MERGE_HPP_INCLUDED
