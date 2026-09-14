
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_parser.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26
//
//  Parse source bytes into a live document through linting, structural checking,
//  private construction and final caller-policy evaluation. No file I/O or logging
//  occurs; all owned storage uses the ambient framework allocator.
//
//  Input and lifetime:
//  A zero-length byte view, including a default view, represents empty text and
//  produces an empty object. Internally CStringView preserves present empty text
//  separately from unavailable text. Source bytes must remain immutable and alive
//  until return; they may be backed by the destination's current storage.
//
//  Processing and publication:
//  The linter adopts the source encoding and normalises every line break to LF.
//  Parsing uses its bounded UTF-8 output without the physical terminal zero.
//  The default policy excludes relaxed syntax; k_all_supported admits every
//  supported feature. Policy is evaluated only after construction succeeds.
//  Destination is replaced only when the report is accepted(), and is otherwise
//  unchanged. Processing success alone does not imply policy acceptance.
//
//  Reporting:
//  state distinguishes failure (-1), unprocessed (0) and success (1). findings
//  accumulates observations from all examined stages, including on failure.
//  failure groups one terminal stage, reason, detection location and optional
//  element start. Policy remains unexamined on processing failure; rejection or
//  invalid options leave processing successful and carry no terminal diagnosis.
//  If supplied, linter_report receives the full linter report on every outcome.
//
//  Locations:
//  Coordinates are 1-based lines and code-point columns in the linter's UTF-8
//  output, not byte offsets into the original input. A linter failure identifies
//  its output cursor; element_start is unavailable. Structural and construction
//  failures identify detection and the affected element's start where available.
//  An unavailable location has available == false; success carries no locations.
//  The stage must accompany the reason and location when interpreting diagnostics.
//
//  Grammar, findings and policy details:
//  See docs/data_model/document_text_format.md and
//  docs/data_model/document_parsing.md.

#pragma once

#ifndef DOCUMENT_PARSER_HPP_INCLUDED
#define DOCUMENT_PARSER_HPP_INCLUDED

#include "containers/ByteBuffers.hpp"
#include "data_model/document_findings.hpp"
#include "text/text_linter.hpp"

class CLiveDocument;

namespace document_parser
{

[[nodiscard]] CDocumentReport parse(const CByteConstView& source, CLiveDocument& destination, const CDocumentParseOptions& options = {}, CTextLintReport* const linter_report = nullptr) noexcept;

}   //  namespace document_parser

#endif // DOCUMENT_PARSER_HPP_INCLUDED
