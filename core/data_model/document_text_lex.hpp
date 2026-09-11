
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
#include "text/text_diagnostics.hpp"

namespace document_text
{

enum class ERelaxation : std::uint32_t
{
    comments = 1u << 0,
    single_quotes = 1u << 1,
    unquoted_names = 1u << 2,
    trailing_commas = 1u << 3,
    unescaped_controls = 1u << 4,
    implicit_root_object = 1u << 5
};

enum class ENumericExtension : std::uint32_t
{
    explicit_plus = 1u << 0,
    hexadecimal = 1u << 1,
    binary = 1u << 2
};

enum class ETokenKind : std::uint8_t
{
    end = 0u, error, object_begin, object_end, array_begin, array_end, colon, comma,
    string, identifier, integer, floating_point, true_value, false_value, null_value
};

enum class ESyntaxError : std::uint8_t
{
    none = 0u, unexpected_character, unterminated_comment, unterminated_string,
    invalid_escape, invalid_surrogate_pair, invalid_number,
    expected_name, expected_colon, expected_value, expected_separator,
    mismatched_delimiter, unexpected_end, trailing_content
};

struct CToken
{
    ETokenKind kind{ ETokenKind::end };
    ESyntaxError error{ ESyntaxError::none };
    std::size_t offset{ 0u };
    std::size_t size{ 0u };
    CTextLocation location;
    CTextLocation failure_point;
};

//  On success, offset advances past one escape (including both UTF-16 units
//  of a surrogate pair), and scalar contains its Unicode value, including NUL.
//  On failure, offset identifies the offending byte or bounded end of input.
//  quote is the enclosing quote delimiter; offset initially addresses '\\'.
[[nodiscard]] ESyntaxError read_escape(const CStringView& source, std::size_t& offset, const std::uint8_t quote, std::uint32_t& scalar) noexcept;

[[nodiscard]] bool is_name_token(const ETokenKind kind) noexcept;

class CScanner
{
public:
    //  Source must be present, immutable and alive. Exclude the linter's
    //  physical terminator; literal NUL within a quoted span is content.
    explicit CScanner(const CStringView& source) noexcept : m_source(source) {}
    [[nodiscard]] CToken next() noexcept;
    [[nodiscard]] std::uint32_t required_relaxations() const noexcept { return m_relaxations; }
    [[nodiscard]] std::uint32_t numeric_extensions() const noexcept { return m_numeric_extensions; }

private:
    [[nodiscard]] CToken scan_next() noexcept;
    void locate(const std::size_t offset) noexcept;
    [[nodiscard]] CToken failure(const ESyntaxError error) const noexcept;
    [[nodiscard]] CToken quoted() noexcept;
    [[nodiscard]] CToken number() noexcept;
    [[nodiscard]] CToken identifier() noexcept;
    [[nodiscard]] bool boundary() const noexcept;

    CStringView m_source;
    std::size_t m_offset{ 0u };
    std::size_t m_element_offset{ 0u };
    std::size_t m_location_offset{ 0u };
    CTextLocation m_location{ true, 1u, 1u };
    std::uint32_t m_relaxations{ 0u };
    std::uint32_t m_numeric_extensions{ 0u };
};

}   //  namespace document_text

#endif // DOCUMENT_TEXT_LEX_HPP_INCLUDED
