
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
#include "text/text_linter.hpp"

class CLiveDocument;

enum class EDocumentParseStatus : std::uint8_t
{
    success = 0u, invalid_input_view, linter_failure, structural_failure, numeric_out_of_range,
    empty_property_name, malformed_recovery_wrapper, unsupported_recovery_version,
    unsupported_recovery_type, invalid_root_value,
    allocation_failed, storage_limit, construction_failed, internal_error
};

struct CDocumentParseInterpretations
{
    std::size_t recovered_arrays_decoded{ 0u };
    std::size_t reserved_names_unescaped{ 0u };
    std::size_t duplicate_members_recovered{ 0u };
    std::size_t singleton_objects_unwrapped{ 0u };
};

struct CDocumentParseReport
{
    EDocumentParseStatus status{ EDocumentParseStatus::invalid_input_view };
    CTextLocation structure_start;
    CTextLocation failure_point;
    //  Populated by ingest; low-level parse receives already linted text.
    bool linter_examined{ false };
    CTextLintReport linter;
    //  Retained even when construction fails. A successful structural report
    //  describes accepted syntax, including relaxation and numeric-extension
    //  bits; it does not claim that the document was constructed successfully.
    CDocumentStructureReport structure;
    //  Successful semantic interpretations, counted by source occurrence.
    //  Cleared on failure; structural syntax observations remain available.
    CDocumentParseInterpretations interpretations;

    [[nodiscard]] bool succeeded() const noexcept { return status == EDocumentParseStatus::success; }
};

namespace document_parser
{

//  Consume present, bounded UTF-8 from a successful linter call. Pass an
//  explicit CStringView length excluding the physical terminator, and normalize
//  every source line break to LF during linting. This function performs the structural
//  pass; it does not perform encoding detection or CP1252 conversion.
//  Source stays immutable and alive until return. Destination is replaced only
//  after success and is unchanged on failure. All owned storage uses the
//  ambient framework allocator. Empty text constructs the implicit root object.
//
//  Decode version-1 recovery wrappers and escaped reserved data names, recover
//  duplicate members in order, and normalize ordinary singleton objects in
//  ordinary arrays. Protocol metadata is validated separately from user data.
//  The root remains an object; a recovery wrapper cannot replace it.
[[nodiscard]] CDocumentParseReport parse(const CStringView& source, CLiveDocument& destination) noexcept;

//  Lint source bytes with uniform LF normalization, then parse. Retain the
//  linter report on every outcome. Failure preserves destination and exposes
//  the linter location directly as failure_point; structure stays unexamined.
[[nodiscard]] CDocumentParseReport ingest(const CByteConstView& source, CLiveDocument& destination) noexcept;
//  The string-view overload also admits present zero-length source text.
[[nodiscard]] CDocumentParseReport ingest(const CStringView& source, CLiveDocument& destination) noexcept;

}   //  namespace document_parser

#endif // DOCUMENT_PARSER_HPP_INCLUDED
