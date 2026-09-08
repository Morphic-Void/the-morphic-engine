
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_parser.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#pragma once

#ifndef DOCUMENT_PARSER_HPP_INCLUDED
#define DOCUMENT_PARSER_HPP_INCLUDED

#include "data_model/document_structure.hpp"

class CLiveDocument;

enum class EDocumentParseStatus : std::uint8_t
{
    success = 0u, invalid_input_view, structural_failure, numeric_out_of_range,
    empty_property_name, duplicate_object_name, unsupported_morphic_representation,
    allocation_failed, storage_limit, construction_failed, internal_error
};

struct CDocumentParseReport
{
    EDocumentParseStatus status{ EDocumentParseStatus::invalid_input_view };
    //  First failure in the linter's UTF-8 output; zero on success. Construction
    //  errors identify the responsible token, not an original pre-lint offset.
    std::size_t byte_offset{ 0u };
    //  Retained even when construction fails. A successful structural report
    //  describes accepted syntax, including relaxation and numeric-extension
    //  bits; it does not claim that the document was constructed successfully.
    CDocumentStructureReport structure;

    [[nodiscard]] bool succeeded() const noexcept { return status == EDocumentParseStatus::success; }
};

namespace document_parser
{

//  Consume present, bounded UTF-8 from a successful linter call. Pass an
//  explicit CStringView length excluding the physical terminator, and disable
//  newline rewriting during linting. This function performs the structural
//  pass; it does not perform encoding detection or CP1252 conversion.
//  Source stays immutable and alive until return. Destination is replaced only
//  after success and is unchanged on failure. All owned storage uses the
//  ambient framework allocator. Empty text constructs the implicit root object.
//
//  Initial construction slice: duplicate names and reserved dollar+morphic
//  names return explicit errors pending recovery support. Ordinary singleton
//  object wrappers are retained pending the separate normalization slice.
[[nodiscard]] CDocumentParseReport parse(const CStringView& source, CLiveDocument& destination) noexcept;

}   //  namespace document_parser

#endif // DOCUMENT_PARSER_HPP_INCLUDED
