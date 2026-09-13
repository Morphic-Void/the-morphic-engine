
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_parser.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#pragma once

#ifndef DOCUMENT_PARSER_HPP_INCLUDED
#define DOCUMENT_PARSER_HPP_INCLUDED

#include "data_model/document_findings.hpp"
#include "data_model/document_structure.hpp"
#include "text/text_linter.hpp"

class CLiveDocument;

enum class EDocumentParseStatus : std::uint8_t
{
    unexamined = 0u, success, policy_rejected, invalid_options, failed
};

struct CDocumentParseReport
{
    EDocumentParseStatus status{ EDocumentParseStatus::unexamined };
    //  Shared terminal diagnosis; successful fallback is evidence, not failure.
    CDocumentFailure failure;
    //  Evaluated only after construction completes; rejection is not failure.
    CDocumentPolicyResult policy;
    CTextLocation structure_start;
    CTextLocation failure_point;
    //  Findings compose the examined stages and survive later failure.
    std::uint32_t findings{ 0u };
    //  Distinguish skipped construction from a failed or completed attempt.
    bool parser_examined{ false };
    bool construction_completed{ false };
    //  Populated by ingest; low-level parse receives already linted text.
    bool linter_examined{ false };
    CTextLintReport linter;
    //  Retained even when construction fails. A successful structural report
    //  describes a complete syntax check; failed checks retain partial findings.
    //  Structural success does not claim that construction completed.
    CDocumentStructureReport structure;

    [[nodiscard]] bool succeeded() const noexcept;
};

namespace document_parser
{

//  Consume present, bounded UTF-8 from a successful linter call. Pass an
//  explicit CStringView length excluding the physical terminator, and normalize
//  every source line break to LF during linting. This function performs the structural
//  pass; it does not perform encoding detection or CP1252 conversion.
//  Source stays immutable and alive until return. Destination is replaced only
//  after construction and policy acceptance, and is unchanged on failure or
//  rejection. Encoding provenance is unavailable here; use ingest to apply
//  source-encoding permissions. All owned storage uses the ambient framework
//  allocator. Empty text constructs the implicit root object. The default
//  policy excludes relaxed syntax; k_all_supported opts into every feature.
//
//  Extend duplicate members in encounter order through the public live-document
//  collision operation, using ordinary arrays. Normalize singleton objects in
//  arrays; dollar-prefixed names and former protocol shapes are ordinary data.
//  Explicit containers select the root kind. Otherwise a first name followed
//  by a colon selects an object body; other non-empty input selects an array
//  body, including a single scalar.
[[nodiscard]] CDocumentParseReport parse(const CStringView& source, CLiveDocument& destination,
    const CDocumentParseOptions& options = {}) noexcept;

//  Lint source bytes with uniform LF normalization, then parse. Retain the
//  linter report on every outcome. Evaluate policy against all stage findings
//  after construction. Failure or rejection preserves destination. A linter
//  failure exposes its location as failure_point; structure stays unexamined.
[[nodiscard]] CDocumentParseReport ingest(const CByteConstView& source, CLiveDocument& destination,
    const CDocumentParseOptions& options = {}) noexcept;
//  The string-view overload also admits present zero-length source text.
[[nodiscard]] CDocumentParseReport ingest(const CStringView& source, CLiveDocument& destination,
    const CDocumentParseOptions& options = {}) noexcept;

}   //  namespace document_parser

inline bool CDocumentParseReport::succeeded() const noexcept
{
    return status == EDocumentParseStatus::success;
}

#endif // DOCUMENT_PARSER_HPP_INCLUDED
