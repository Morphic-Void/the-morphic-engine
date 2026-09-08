
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_structure.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "data_model/document_structure.hpp"

#include "containers/TPodVector.hpp"

namespace document_structure
{

//  Group the state-machine implementation types separately from the entry point.
namespace structure_util
{

using document_text::CToken;
using document_text::ETokenKind;
using document_text::ESyntaxError;
using document_text::ERelaxation;

enum class EState : std::uint8_t { first, after_comma, colon, value, separator };

struct CFrame
{
    bool object;
    bool implicit;
    EState state;
};

class CCheck
{
public:
    explicit CCheck(const CStringView& source) noexcept : m_scanner(source) {}
    [[nodiscard]] CDocumentStructureReport run() noexcept;

private:
    void advance() noexcept;
    void fail(const EDocumentStructureStatus status, const ESyntaxError error = ESyntaxError::none) noexcept;
    void syntax(const ESyntaxError error) noexcept { fail(EDocumentStructureStatus::syntax_error, error); }
    void push(const bool object, const bool implicit = false) noexcept;
    void value() noexcept;
    void step() noexcept;

    document_text::CScanner m_scanner;
    CToken m_token;
    TPodVector<CFrame> m_frames;
    CDocumentStructureReport m_report;
};

void CCheck::fail(const EDocumentStructureStatus status, const ESyntaxError error) noexcept
{
    if (m_report.succeeded())
    {
        m_report = {};
        m_report.status = status;
        m_report.syntax_error = error;
        m_report.byte_offset = m_token.offset;
    }
}

void CCheck::advance() noexcept
{
    m_token = m_scanner.next();
    if (m_token.kind == ETokenKind::error)
    {
        syntax(m_token.error);
    }
}

void CCheck::push(const bool object, const bool implicit) noexcept
{
    if (m_frames.size() == memory::t_max_elements<CFrame>())
    {
        fail(EDocumentStructureStatus::scratch_size_limit);
        return;
    }
    if (!m_frames.push_back(CFrame{ object, implicit, EState::first }))
    {
        fail(EDocumentStructureStatus::allocation_failed);
        return;
    }
    ++m_report.estimates.value_count;
    if (object)
    {
        ++m_report.estimates.object_count;
    }
    else
    {
        ++m_report.estimates.array_count;
    }
    if (m_frames.size() > m_report.estimates.maximum_depth)
    {
        m_report.estimates.maximum_depth = m_frames.size();
    }
}

void CCheck::value() noexcept
{
    //  Set the parent's next state before push_back can relocate the frames.
    m_frames.last().state = EState::separator;
    switch (m_token.kind)
    {
        case ETokenKind::object_begin:
        case ETokenKind::array_begin:
        {
            push(m_token.kind == ETokenKind::object_begin);
            break;
        }
        case ETokenKind::string:
        {
            m_report.estimates.string_source_byte_size += m_token.size;
            ++m_report.estimates.value_count;
            break;
        }
        case ETokenKind::integer:
        case ETokenKind::floating_point:
        case ETokenKind::true_value:
        case ETokenKind::false_value:
        case ETokenKind::null_value:
        {
            ++m_report.estimates.value_count;
            break;
        }
        default:
        {
            syntax(ESyntaxError::expected_value);
            return;
        }
    }
    if (m_report.succeeded())
    {
        advance();
    }
}

void CCheck::step() noexcept
{
    CFrame& frame = m_frames.last();
    const bool end = m_token.kind == ETokenKind::end;
    const bool closing = (m_token.kind == ETokenKind::object_end) || (m_token.kind == ETokenKind::array_end);
    if (end || closing)
    {
        const bool matches = frame.implicit ? end : (m_token.kind == (frame.object ? ETokenKind::object_end : ETokenKind::array_end));
        if (!matches)
        {
            syntax(end ? ESyntaxError::unexpected_end : ESyntaxError::mismatched_delimiter);
            return;
        }
        if (frame.state == EState::colon)
        {
            syntax(ESyntaxError::expected_colon);
            return;
        }
        if (frame.state == EState::value)
        {
            syntax(ESyntaxError::expected_value);
            return;
        }
        if (frame.state == EState::after_comma)
        {
            m_report.required_relaxations |= static_cast<std::uint32_t>(ERelaxation::trailing_commas);
        }
        (void)m_frames.discard_back();
        if (!end)
        {
            advance();
        }
        return;
    }
    switch (frame.state)
    {
        case EState::first:
        case EState::after_comma:
        {
            if (!frame.object)
            {
                value();
                return;
            }
            if (!document_text::is_name_token(m_token.kind))
            {
                syntax(ESyntaxError::expected_name);
                return;
            }
            if (m_token.kind != ETokenKind::string)
            {
                m_report.required_relaxations |= static_cast<std::uint32_t>(ERelaxation::unquoted_names);
            }
            ++m_report.estimates.named_entry_count;
            m_report.estimates.string_source_byte_size += m_token.size;
            frame.state = EState::colon;
            advance();
            return;
        }
        case EState::colon:
        {
            if (m_token.kind != ETokenKind::colon)
            {
                syntax(ESyntaxError::expected_colon);
                return;
            }
            frame.state = EState::value;
            advance();
            return;
        }
        case EState::value:
        {
            value();
            return;
        }
        case EState::separator:
        {
            if (m_token.kind != ETokenKind::comma)
            {
                syntax(ESyntaxError::expected_separator);
                return;
            }
            frame.state = EState::after_comma;
            advance();
            return;
        }
        default:
        {
            fail(EDocumentStructureStatus::internal_error);
            return;
        }
    }
}

CDocumentStructureReport CCheck::run() noexcept
{
    m_report.status = EDocumentStructureStatus::success;
    advance();
    if (m_report.succeeded())
    {
        const bool implicit = m_token.kind != ETokenKind::object_begin;
        push(true, implicit);
        if (implicit && m_report.succeeded())
        {
            m_report.required_relaxations |= static_cast<std::uint32_t>(ERelaxation::implicit_root_object);
        }
        else if (m_report.succeeded())
        {
            advance();
        }
    }
    while (m_report.succeeded() && !m_frames.is_empty())
    {
        step();
    }
    if (m_report.succeeded() && (m_token.kind != ETokenKind::end))
    {
        syntax(ESyntaxError::trailing_content);
    }
    if (m_report.succeeded())
    {
        m_report.required_relaxations |= m_scanner.required_relaxations();
        m_report.numeric_extensions = m_scanner.numeric_extensions();
    }
    return m_report;
}

}   //  namespace structure_util

CDocumentStructureReport check(const CStringView& source) noexcept
{
    if (source.empty())
    {
        return {};
    }
    structure_util::CCheck checker(source);
    return checker.run();
}

}   //  namespace document_structure
