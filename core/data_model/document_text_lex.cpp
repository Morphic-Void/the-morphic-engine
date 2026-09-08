
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

static bool name_start(const std::uint8_t ch) noexcept
{
    return ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) || (ch == '_') || (ch == '$');
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
        case '"':
        case '\\':
        case '/':
        {
            scalar = ch;
            break;
        }
        case '\'':
        {
            if (quote != '\'')
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
    return (kind == ETokenKind::string) || (kind == ETokenKind::identifier) ||
        (kind == ETokenKind::true_value) || (kind == ETokenKind::false_value) || (kind == ETokenKind::null_value);
}

CToken CScanner::failure(const ESyntaxError error) const noexcept
{
    return { ETokenKind::error, error, m_offset, 0u };
}

bool CScanner::boundary() const noexcept
{
    if (m_offset == m_source.length())
    {
        return true;
    }
    const std::uint8_t ch = m_source.string()[m_offset];
    if (lex_util::whitespace(ch) || (ch == ',') || (ch == ':') || (ch == ']') || (ch == '}'))
    {
        return true;
    }
    return (ch == '/') && ((m_source.length() - m_offset) >= 2u) &&
        ((m_source.string()[m_offset + 1u] == '/') || (m_source.string()[m_offset + 1u] == '*'));
}

CToken CScanner::quoted() noexcept
{
    const std::size_t start = m_offset;
    const std::uint8_t quote = m_source.string()[m_offset++];
    if (quote == '\'')
    {
        m_relaxations |= static_cast<std::uint32_t>(ERelaxation::single_quotes);
    }
    while (m_offset < m_source.length())
    {
        const std::uint8_t ch = m_source.string()[m_offset];
        if (ch == quote)
        {
            ++m_offset;
            return { ETokenKind::string, ESyntaxError::none, start, m_offset - start };
        }
        if (ch == '\\')
        {
            std::uint32_t scalar = 0u;
            const ESyntaxError error = read_escape(m_source, m_offset, quote, scalar);
            if (error != ESyntaxError::none)
            {
                return failure(error);
            }
        }
        else
        {
            if (ch < 0x20u)
            {
                m_relaxations |= static_cast<std::uint32_t>(ERelaxation::unescaped_controls);
            }
            ++m_offset;
        }
    }
    return failure(ESyntaxError::unterminated_string);
}

CToken CScanner::number() noexcept
{
    const std::size_t start = m_offset;
    std::uint8_t ch = m_source.string()[m_offset];
    if ((ch == '+') || (ch == '-'))
    {
        if (ch == '+')
        {
            m_numeric_extensions |= static_cast<std::uint32_t>(ENumericExtension::explicit_plus);
        }
        ++m_offset;
        if (m_offset == m_source.length())
        {
            return failure(ESyntaxError::invalid_number);
        }
        ch = m_source.string()[m_offset];
    }
    unsigned base = 10u;
    if (ch == '#')
    {
        base = 16u;
        ++m_offset;
    }
    else if ((ch == '0') && ((m_source.length() - m_offset) >= 2u))
    {
        const std::uint8_t prefix = m_source.string()[m_offset + 1u];
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
            m_offset += 2u;
        }
    }
    ETokenKind kind = ETokenKind::integer;
    if (base != 10u)
    {
        m_numeric_extensions |= static_cast<std::uint32_t>((base == 16u) ? ENumericExtension::hexadecimal : ENumericExtension::binary);
        const std::size_t digits = m_offset;
        while (m_offset < m_source.length())
        {
            const int value = lex_util::hex_digit(m_source.string()[m_offset]);
            if ((value < 0) || (static_cast<unsigned>(value) >= base))
            {
                break;
            }
            ++m_offset;
        }
        if (m_offset == digits)
        {
            return failure(ESyntaxError::invalid_number);
        }
    }
    else
    {
        if (!lex_util::digit(ch))
        {
            return failure(ESyntaxError::invalid_number);
        }
        ++m_offset;
        if (ch != '0')
        {
            while ((m_offset < m_source.length()) && lex_util::digit(m_source.string()[m_offset]))
            {
                ++m_offset;
            }
        }
        if ((m_offset < m_source.length()) && (m_source.string()[m_offset] == '.'))
        {
            kind = ETokenKind::floating_point;
            ++m_offset;
            const std::size_t digits = m_offset;
            while ((m_offset < m_source.length()) && lex_util::digit(m_source.string()[m_offset]))
            {
                ++m_offset;
            }
            if (m_offset == digits)
            {
                return failure(ESyntaxError::invalid_number);
            }
        }
        if ((m_offset < m_source.length()) && ((m_source.string()[m_offset] == 'e') || (m_source.string()[m_offset] == 'E')))
        {
            kind = ETokenKind::floating_point;
            ++m_offset;
            if ((m_offset < m_source.length()) && ((m_source.string()[m_offset] == '+') || (m_source.string()[m_offset] == '-')))
            {
                ++m_offset;
            }
            const std::size_t digits = m_offset;
            while ((m_offset < m_source.length()) && lex_util::digit(m_source.string()[m_offset]))
            {
                ++m_offset;
            }
            if (m_offset == digits)
            {
                return failure(ESyntaxError::invalid_number);
            }
        }
    }
    if (!boundary())
    {
        return failure(ESyntaxError::invalid_number);
    }
    return { kind, ESyntaxError::none, start, m_offset - start };
}

CToken CScanner::identifier() noexcept
{
    const std::size_t start = m_offset++;
    while ((m_offset < m_source.length()) && (lex_util::name_start(m_source.string()[m_offset]) || lex_util::digit(m_source.string()[m_offset])))
    {
        ++m_offset;
    }
    if (!boundary())
    {
        return failure(ESyntaxError::unexpected_character);
    }
    const std::size_t size = m_offset - start;
    ETokenKind kind = ETokenKind::identifier;
    if ((size == 4u) && (std::memcmp(m_source.string() + start, "true", 4u) == 0))
    {
        kind = ETokenKind::true_value;
    }
    if ((size == 5u) && (std::memcmp(m_source.string() + start, "false", 5u) == 0))
    {
        kind = ETokenKind::false_value;
    }
    if ((size == 4u) && (std::memcmp(m_source.string() + start, "null", 4u) == 0))
    {
        kind = ETokenKind::null_value;
    }
    return { kind, ESyntaxError::none, start, size };
}

CToken CScanner::next() noexcept
{
    while (m_offset < m_source.length())
    {
        const std::uint8_t ch = m_source.string()[m_offset];
        if (lex_util::whitespace(ch))
        {
            ++m_offset;
            continue;
        }
        if ((ch != '/') || ((m_source.length() - m_offset) < 2u))
        {
            break;
        }
        const std::uint8_t next = m_source.string()[m_offset + 1u];
        if ((next != '/') && (next != '*'))
        {
            break;
        }
        m_relaxations |= static_cast<std::uint32_t>(ERelaxation::comments);
        m_offset += 2u;
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
    if (m_offset == m_source.length())
    {
        return { ETokenKind::end, ESyntaxError::none, m_offset, 0u };
    }
    const std::uint8_t ch = m_source.string()[m_offset];
    if ((ch == '"') || (ch == '\''))
    {
        return quoted();
    }
    if (lex_util::digit(ch) || (ch == '+') || (ch == '-') || (ch == '#'))
    {
        return number();
    }
    if (lex_util::name_start(ch))
    {
        return identifier();
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
            return failure(ESyntaxError::unexpected_character);
        }
    }
    return { kind, ESyntaxError::none, m_offset++, 1u };
}

}   //  namespace document_text
