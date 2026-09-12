
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_text_lex.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "data_model/document_text_lex.hpp"

#include <cstring>

namespace document_text
{

//  Group byte-level lexical implementation helpers under a distinct name.
namespace lex_util
{

//  Static linkage keeps these free functions local to this translation unit.
static bool whitespace(const std::uint8_t ch) noexcept
{
    return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
}

static bool digit(const std::uint8_t ch) noexcept { return (ch >= '0') && (ch <= '9'); }

static bool line_break(const std::uint32_t scalar) noexcept
{
    return (scalar == '\n') || (scalar == '\r') || (scalar == '\v') || (scalar == '\f') ||
        (scalar == 0x85u) || (scalar == 0x2028u) || (scalar == 0x2029u);
}

static int hex_digit(const std::uint8_t ch) noexcept
{
    if (digit(ch))
    {
        return ch - '0';
    }
    if ((ch >= 'a') && (ch <= 'f'))
    {
        return ch - 'a' + 10;
    }
    if ((ch >= 'A') && (ch <= 'F'))
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool unquoted_boundary(const std::uint8_t ch) noexcept
{
    return whitespace(ch) || (ch == '{') || (ch == '}') || (ch == '[') ||
        (ch == ']') || (ch == ':') || (ch == ',') || (ch == '"');
}

static bool read_unit(const CStringView& source, std::size_t& offset, std::uint32_t& unit) noexcept
{
    unit = 0u;
    for (unsigned i = 0u; i < 4u; ++i)
    {
        if (offset == source.length())
        {
            return false;
        }
        const int value = hex_digit(source.string()[offset]);
        if (value < 0)
        {
            return false;
        }
        unit = (unit << 4u) | static_cast<std::uint32_t>(value);
        ++offset;
    }
    return true;
}

//  Classify a complete non-empty source candidate without decoding escapes.
//  Failed numeric interpretation contributes no numeric feature permissions.
static ETokenKind classify_number(const CStringView& source, std::uint32_t& value_findings) noexcept
{
    std::size_t offset = 0u;
    std::uint32_t findings = 0u;
    std::uint8_t ch = source.string()[offset];
    if ((ch == '+') || (ch == '-'))
    {
        if (ch == '+')
        {
            findings |= document_finding_bit(EDocumentFinding::explicit_plus);
        }
        ++offset;
        if (offset == source.length())
        {
            return ETokenKind::unquoted_string;
        }
        ch = source.string()[offset];
    }
    unsigned base = 10u;
    if (ch == '#')
    {
        base = 16u;
        findings |= document_finding_bit(EDocumentFinding::alternate_hexadecimal_prefix);
        ++offset;
    }
    else if ((ch == '0') && ((source.length() - offset) >= 2u))
    {
        const std::uint8_t prefix = source.string()[offset + 1u];
        if ((prefix == 'x') || (prefix == 'X'))
        {
            base = 16u;
        }
        if ((prefix == 'b') || (prefix == 'B'))
        {
            base = 2u;
        }
        if (base != 10u)
        {
            offset += 2u;
        }
    }
    ETokenKind kind = ETokenKind::integer;
    if (base != 10u)
    {
        findings |= document_finding_bit((base == 16u) ? EDocumentFinding::hexadecimal : EDocumentFinding::binary);
        const std::size_t digits = offset;
        while (offset < source.length())
        {
            const int value = hex_digit(source.string()[offset]);
            if ((value < 0) || (static_cast<unsigned>(value) >= base))
            {
                break;
            }
            ++offset;
        }
        if (offset == digits)
        {
            return ETokenKind::unquoted_string;
        }
    }
    else
    {
        if (!digit(ch))
        {
            return ETokenKind::unquoted_string;
        }
        ++offset;
        if (ch != '0')
        {
            while ((offset < source.length()) && digit(source.string()[offset]))
            {
                ++offset;
            }
        }
        if ((offset < source.length()) && (source.string()[offset] == '.'))
        {
            kind = ETokenKind::floating_point;
            ++offset;
            const std::size_t digits = offset;
            while ((offset < source.length()) && digit(source.string()[offset]))
            {
                ++offset;
            }
            if (offset == digits)
            {
                return ETokenKind::unquoted_string;
            }
        }
        if ((offset < source.length()) && ((source.string()[offset] == 'e') || (source.string()[offset] == 'E')))
        {
            kind = ETokenKind::floating_point;
            ++offset;
            if ((offset < source.length()) && ((source.string()[offset] == '+') || (source.string()[offset] == '-')))
            {
                ++offset;
            }
            const std::size_t digits = offset;
            while ((offset < source.length()) && digit(source.string()[offset]))
            {
                ++offset;
            }
            if (offset == digits)
            {
                return ETokenKind::unquoted_string;
            }
        }
    }
    if (offset != source.length())
    {
        return ETokenKind::unquoted_string;
    }
    //  Numeric spelling is established only after the complete token is valid.
    value_findings = findings;
    return kind;
}

}   //  namespace lex_util

ESyntaxError read_escape(const CStringView& source, std::size_t& offset, const std::uint8_t quote, std::uint32_t& scalar) noexcept
{
    if ((offset >= source.length()) || (source.string()[offset] != '\\'))
    {
        return ESyntaxError::invalid_escape;
    }
    ++offset;
    if (offset == source.length())
    {
        return ESyntaxError::invalid_escape;
    }
    const std::uint8_t ch = source.string()[offset];
    switch (ch)
    {
        case '\\':
        case '/':
        {
            scalar = ch;
            break;
        }
        case '"':
        case '\'':
        {
            if (quote != ch)
            {
                return ESyntaxError::invalid_escape;
            }
            scalar = ch;
            break;
        }
        case 'b':
        {
            scalar = '\b';
            break;
        }
        case 'f':
        {
            scalar = '\f';
            break;
        }
        case 'n':
        {
            scalar = '\n';
            break;
        }
        case 'r':
        {
            scalar = '\r';
            break;
        }
        case 't':
        {
            scalar = '\t';
            break;
        }
        case 'u':
        {
            ++offset;
            if (!lex_util::read_unit(source, offset, scalar))
            {
                return ESyntaxError::invalid_escape;
            }
            if ((scalar >= 0xdc00u) && (scalar <= 0xdfffu))
            {
                return ESyntaxError::invalid_surrogate_pair;
            }
            if ((scalar >= 0xd800u) && (scalar <= 0xdbffu))
            {
                if (((source.length() - offset) < 2u) || (source.string()[offset] != '\\') || (source.string()[offset + 1u] != 'u'))
                {
                    return ESyntaxError::invalid_surrogate_pair;
                }
                offset += 2u;
                std::uint32_t low = 0u;
                if (!lex_util::read_unit(source, offset, low) || (low < 0xdc00u) || (low > 0xdfffu))
                {
                    return ESyntaxError::invalid_surrogate_pair;
                }
                scalar = 0x10000u + ((scalar - 0xd800u) << 10u) + (low - 0xdc00u);
            }
            return ESyntaxError::none;
        }
        default:
        {
            return ESyntaxError::invalid_escape;
        }
    }
    ++offset;
    return ESyntaxError::none;
}

bool is_name_token(const ETokenKind kind) noexcept
{
    return (kind == ETokenKind::string) || (kind == ETokenKind::unquoted_string) ||
        (kind == ETokenKind::integer) || (kind == ETokenKind::floating_point) ||
        (kind == ETokenKind::true_value) || (kind == ETokenKind::false_value) || (kind == ETokenKind::null_value);
}

CRootForm select_root(const CToken& first, CScanner scanner) noexcept
{
    if (first.kind == ETokenKind::end)
    {
        return { true, true };
    }
    if ((first.kind == ETokenKind::object_begin) || (first.kind == ETokenKind::array_begin))
    {
        return { first.kind == ETokenKind::object_begin, false };
    }
    return { is_name_token(first.kind) && (scanner.next().kind == ETokenKind::colon), true };
}

CToken CScanner::failure(const ESyntaxError error) const noexcept
{
    return { ETokenKind::error, error, m_offset, 0u };
}

void CScanner::record_line_break(const std::size_t offset) noexcept
{
    if (m_line_break_offset == m_source.length())
    {
        m_line_break_offset = offset;
    }
}

CToken CScanner::quoted() noexcept
{
    const std::size_t start = m_offset;
    std::uint8_t literal_line_break = 0u;
    const std::uint8_t quote = m_source.string()[m_offset++];
    if (quote == '\'')
    {
        m_findings |= document_finding_bit(EDocumentFinding::single_quotes);
    }
    while (m_offset < m_source.length())
    {
        const std::uint8_t ch = m_source.string()[m_offset];
        if (ch == quote)
        {
            ++m_offset;
            CToken token{ ETokenKind::string, ESyntaxError::none, start, m_offset - start };
            token.literal_line_break = literal_line_break;
            return token;
        }
        if (ch == '\\')
        {
            const std::size_t escape_start = m_offset;
            std::uint32_t scalar = 0u;
            const ESyntaxError error = read_escape(m_source, m_offset, quote, scalar);
            if (error != ESyntaxError::none)
            {
                return failure(error);
            }
            if (scalar == 0u)
            {
                m_findings |= document_finding_bit(EDocumentFinding::logical_nul);
            }
            if (lex_util::line_break(scalar))
            {
                record_line_break(escape_start);
            }
        }
        else
        {
            //  All literal source line breaks have been normalized to LF.
            if (ch == '\n')
            {
                record_line_break(m_offset);
                literal_line_break = 1u;
            }
            if (ch < 0x20u)
            {
                const EDocumentFinding finding = (ch == 0u) ? EDocumentFinding::logical_nul :
                    ((ch == '\n') || (ch == '\r')) ? EDocumentFinding::raw_quoted_line_breaks : EDocumentFinding::raw_quoted_controls;
                m_findings |= document_finding_bit(finding);
            }
            ++m_offset;
        }
    }
    return failure(ESyntaxError::unterminated_string);
}

CToken CScanner::unquoted() noexcept
{
    const std::size_t start = m_offset;
    while (m_offset < m_source.length())
    {
        const std::uint8_t ch = m_source.string()[m_offset];
        if (ch == '\\')
        {
            const std::size_t escape_start = m_offset;
            std::uint32_t scalar = 0u;
            const ESyntaxError error = read_escape(m_source, m_offset, '"', scalar);
            if (error != ESyntaxError::none)
            {
                return failure(error);
            }
            if (scalar == 0u)
            {
                m_findings |= document_finding_bit(EDocumentFinding::logical_nul);
            }
            if (lex_util::line_break(scalar))
            {
                record_line_break(escape_start);
            }
        }
        else
        {
            if (lex_util::unquoted_boundary(ch))
            {
                break;
            }
            if (ch == 0u)
            {
                m_findings |= document_finding_bit(EDocumentFinding::logical_nul);
            }
            ++m_offset;
        }
    }
    const std::size_t size = m_offset - start;
    const CStringView candidate{ m_source.string() + start, size };
    ETokenKind kind;
    std::uint32_t value_findings = 0u;
    if ((size == 4u) && (std::memcmp(candidate.string(), "true", 4u) == 0))
    {
        kind = ETokenKind::true_value;
    }
    else if ((size == 5u) && (std::memcmp(candidate.string(), "false", 5u) == 0))
    {
        kind = ETokenKind::false_value;
    }
    else if ((size == 4u) && (std::memcmp(candidate.string(), "null", 4u) == 0))
    {
        kind = ETokenKind::null_value;
    }
    else
    {
        kind = lex_util::classify_number(candidate, value_findings);
        if (kind == ETokenKind::unquoted_string)
        {
            value_findings = document_finding_bit(EDocumentFinding::unquoted_strings);
        }
    }
    return { kind, ESyntaxError::none, start, size, value_findings };
}

CToken CScanner::next() noexcept
{
    m_line_break_offset = m_source.length();
    CToken token = scan_next();
    locate(m_element_offset);
    token.location = m_location;
    if (m_line_break_offset != m_source.length())
    {
        locate(m_line_break_offset);
        token.first_line_break = m_location;
    }
    if (token.kind == ETokenKind::error)
    {
        locate(token.offset);
        token.failure_point = m_location;
    }
    return token;
}

void CScanner::locate(const std::size_t offset) noexcept
{
    //  Each byte is visited at most once for coordinates across all tokens.
    //  Input is linted UTF-8; continuation bytes never advance the column.
    while (m_location_offset < offset)
    {
        const std::uint8_t ch = m_source.string()[m_location_offset++];
        if (ch == '\n')
        {
            ++m_location.line_1_based;
            m_location.code_point_column_1_based = 1u;
        }
        else if ((ch & 0xc0u) != 0x80u)
        {
            ++m_location.code_point_column_1_based;
        }
    }
}

CToken CScanner::scan_next() noexcept
{
    while (m_offset < m_source.length())
    {
        const std::uint8_t ch = m_source.string()[m_offset];
        if (lex_util::whitespace(ch))
        {
            ++m_offset;
            continue;
        }
        const bool semicolon_comment = ch == ';';
        if (!semicolon_comment && ((ch != '/') || ((m_source.length() - m_offset) < 2u)))
        {
            break;
        }
        const std::uint8_t next = semicolon_comment ? '/' : m_source.string()[m_offset + 1u];
        if ((next != '/') && (next != '*'))
        {
            break;
        }
        m_element_offset = m_offset;
        m_findings |= document_finding_bit(EDocumentFinding::comments);
        m_offset += semicolon_comment ? 1u : 2u;
        if (next == '/')
        {
            while ((m_offset < m_source.length()) && (m_source.string()[m_offset] != '\r') && (m_source.string()[m_offset] != '\n'))
            {
                ++m_offset;
            }
        }
        else
        {
            while (((m_source.length() - m_offset) >= 2u) &&
                !((m_source.string()[m_offset] == '*') && (m_source.string()[m_offset + 1u] == '/')))
            {
                ++m_offset;
            }
            if ((m_source.length() - m_offset) < 2u)
            {
                m_offset = m_source.length();
                return failure(ESyntaxError::unterminated_comment);
            }
            m_offset += 2u;
        }
    }
    m_element_offset = m_offset;
    if (m_offset == m_source.length())
    {
        return { ETokenKind::end, ESyntaxError::none, m_offset, 0u };
    }
    const std::uint8_t ch = m_source.string()[m_offset];
    if ((ch == '"') || (ch == '\''))
    {
        return quoted();
    }
    ETokenKind kind;
    switch (ch)
    {
        case '{':
        {
            kind = ETokenKind::object_begin;
            break;
        }
        case '}':
        {
            kind = ETokenKind::object_end;
            break;
        }
        case '[':
        {
            kind = ETokenKind::array_begin;
            break;
        }
        case ']':
        {
            kind = ETokenKind::array_end;
            break;
        }
        case ':':
        {
            kind = ETokenKind::colon;
            break;
        }
        case ',':
        {
            kind = ETokenKind::comma;
            break;
        }
        default:
        {
            return unquoted();
        }
    }
    return { kind, ESyntaxError::none, m_offset++, 1u };
}

}   //  namespace document_text
