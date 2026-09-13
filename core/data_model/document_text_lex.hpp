
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_text_lex.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26
//
//  Feature-local, allocation-free lexical operations shared by text checking
//  and document parsing. Input is bounded UTF-8 already produced by the linter.

#pragma once

#ifndef DOCUMENT_TEXT_LEX_HPP_INCLUDED
#define DOCUMENT_TEXT_LEX_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "containers/StringBuffers.hpp"
#include "data_model/document_findings.hpp"

namespace document_text
{

enum class ETokenKind : std::uint8_t
{
    end = 0u, error, object_begin, object_end, array_begin, array_end, colon, comma,
    string, unquoted_string, integer, floating_point, true_value, false_value, null_value
};

struct CToken
{
    ETokenKind kind{ ETokenKind::end };
    EDocumentFailureReason error{ EDocumentFailureReason::none };
    std::size_t offset{ 0u };
    std::size_t size{ 0u };
    std::uint32_t value_findings{ 0u };        //  Apply only in value position; numeric-looking names remain strings.
    CTextLocation location;
    CTextLocation failure_point;
    CTextLocation first_line_break;           //  Decoded content evidence; an escaped break points to its backslash.
    std::uint8_t literal_line_break{ 0u };    //  Per-token source spelling, independent of the aggregate findings.
};

//  On success, offset advances past one escape (including both UTF-16 units
//  of a surrogate pair), and scalar contains its Unicode value, including NUL.
//  On failure, offset identifies the offending byte or bounded end of input.
//  quote is the enclosing delimiter, or double quote for unquoted JSON escapes;
//  offset initially addresses '\\'.
[[nodiscard]] EDocumentFailureReason read_escape(const CStringView& source, std::size_t& offset, const std::uint8_t quote, std::uint32_t& scalar) noexcept;

[[nodiscard]] bool is_name_token(const ETokenKind kind) noexcept;

class CScanner
{
public:
    //  Source must be present, immutable and alive. Exclude the linter's
    //  physical terminator; literal NUL within a quoted span is content.
    explicit CScanner(const CStringView& source) noexcept : m_source(source) {}
    [[nodiscard]] CToken next() noexcept;
    //  Context-independent observations; token value_findings need value context.
    [[nodiscard]] std::uint32_t findings() const noexcept;

private:
    [[nodiscard]] CToken scan_next() noexcept;
    void locate(const std::size_t offset) noexcept;
    [[nodiscard]] CToken failure(const EDocumentFailureReason error) const noexcept;
    void record_line_break(const std::size_t offset) noexcept;
    [[nodiscard]] CToken quoted() noexcept;
    [[nodiscard]] CToken unquoted() noexcept;

    CStringView m_source;
    std::size_t m_offset{ 0u };
    std::size_t m_element_offset{ 0u };
    std::size_t m_line_break_offset{ 0u };
    std::size_t m_location_offset{ 0u };
    CTextLocation m_location{ true, 1u, 1u };
    std::uint32_t m_findings{ 0u };
};

struct CRootForm
{
    bool object;
    bool implicit;
};

//  The scanner is positioned just after first. Probe a copy so root selection
//  does not consume input or publish findings from lookahead.
[[nodiscard]] CRootForm select_root(const CToken& first, CScanner scanner) noexcept;

inline std::uint32_t CScanner::findings() const noexcept
{
    return m_findings;
}

}   //  namespace document_text

#endif // DOCUMENT_TEXT_LEX_HPP_INCLUDED
