
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

enum class EState : std::uint8_t { first = 0u, after_comma, colon, value, separator };

struct CFrame
{
    bool object;
    bool implicit;
    EState state;
    CTextLocation start;
    CTextLocation member_start;
};

class CCheck
{
public:
    explicit CCheck(const CStringView& source) noexcept : m_scanner(source) {}
    [[nodiscard]] CDocumentStructureReport run(CDocumentStructureEstimates* const estimates) noexcept;

private:
    void advance() noexcept;
    void fail(const EDocumentFailureReason reason) noexcept;
    void push(const bool object, const bool implicit = false) noexcept;
    void value() noexcept;
    void step() noexcept;

    document_text::CScanner m_scanner;
    CToken m_token;
    TPodVector<CFrame> m_frames;
    CDocumentStructureReport m_report;
    CDocumentStructureEstimates m_estimates;
    std::size_t m_root_value_count{ 0u };
};

void CCheck::fail(const EDocumentFailureReason reason) noexcept
{
    if (m_report.succeeded())
    {
        m_report.status = EDocumentStructureStatus::failed;
        m_report.failure = { EDocumentFailureStage::structure, reason };
        m_report.failure_point = m_token.location;
        m_report.structure_start = m_token.location;
        if (m_token.kind == ETokenKind::error)
        {
            m_report.failure_point = m_token.failure_point;
        }
        else if (!m_frames.is_empty())
        {
            const CFrame& frame = m_frames.last();
            m_report.structure_start = frame.start;
            if ((frame.state == EState::colon) || (frame.state == EState::value))
            {
                m_report.structure_start = frame.member_start;
            }
        }
    }
}

void CCheck::advance() noexcept
{
    m_token = m_scanner.next();
    if (m_token.kind == ETokenKind::error)
    {
        fail(m_token.error);
    }
}

void CCheck::push(const bool object, const bool implicit) noexcept
{
    if (m_frames.size() == memory::t_max_elements<CFrame>())
    {
        fail(EDocumentFailureReason::storage_limit);
        return;
    }
    const CTextLocation start = implicit ? CTextLocation{ true, 1u, 1u } : m_token.location;
    if (!m_frames.push_back(CFrame{ object, implicit, EState::first, start, {} }))
    {
        fail(EDocumentFailureReason::allocation_failed);
        return;
    }
    ++m_estimates.value_count;
    if (object)
    {
        ++m_estimates.object_count;
    }
    else
    {
        ++m_estimates.array_count;
    }
    if (m_frames.size() > m_estimates.maximum_depth)
    {
        m_estimates.maximum_depth = m_frames.size();
    }
}

void CCheck::value() noexcept
{
    const bool implicit_root_value = (m_frames.size() == 1u) && m_frames.last().implicit && !m_frames.last().object;
    //  Set the parent's next state before push_back can relocate the frames.
    const EState previous = m_frames.last().state;
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
        case ETokenKind::unquoted_string:
        {
            m_estimates.string_source_byte_size += m_token.size;
            ++m_estimates.value_count;
            break;
        }
        case ETokenKind::integer:
        case ETokenKind::floating_point:
        case ETokenKind::true_value:
        case ETokenKind::false_value:
        case ETokenKind::null_value:
        {
            ++m_estimates.value_count;
            break;
        }
        default:
        {
            m_frames.last().state = previous;
            fail(EDocumentFailureReason::missing_value);
            return;
        }
    }
    if (implicit_root_value && (++m_root_value_count > 1u))
    {
        m_report.findings |= document_finding_bit(EDocumentFinding::implicit_body);
    }
    m_report.findings |= m_token.value_findings;
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
            fail(end ? EDocumentFailureReason::unexpected_end : EDocumentFailureReason::mismatched_delimiter);
            return;
        }
        if (frame.state == EState::colon)
        {
            fail(EDocumentFailureReason::missing_colon);
            return;
        }
        if (frame.state == EState::value)
        {
            fail(EDocumentFailureReason::missing_value);
            return;
        }
        if (frame.state == EState::after_comma)
        {
            m_report.findings |= document_finding_bit(EDocumentFinding::trailing_commas);
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
                fail(EDocumentFailureReason::missing_name);
                return;
            }
            if (m_token.kind != ETokenKind::string)
            {
                m_report.findings |= document_finding_bit(EDocumentFinding::unquoted_names);
            }
            if ((m_token.kind == ETokenKind::string) && (m_token.size == 2u))
            {
                m_report.findings |= document_finding_bit(EDocumentFinding::empty_member_name);
            }
            if (m_token.first_line_break.available)
            {
                fail(EDocumentFailureReason::newline_in_name);
                m_report.structure_start = m_token.location;
                m_report.failure_point = m_token.first_line_break;
                return;
            }
            ++m_estimates.named_entry_count;
            frame.member_start = m_token.location;
            m_estimates.string_source_byte_size += m_token.size;
            frame.state = EState::colon;
            advance();
            return;
        }
        case EState::colon:
        {
            if (m_token.kind != ETokenKind::colon)
            {
                fail(EDocumentFailureReason::missing_colon);
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
                fail(EDocumentFailureReason::missing_separator);
                return;
            }
            frame.state = EState::after_comma;
            advance();
            return;
        }
        default:
        {
            fail(EDocumentFailureReason::internal_error);
            return;
        }
    }
}

CDocumentStructureReport CCheck::run(CDocumentStructureEstimates* const estimates) noexcept
{
    m_report.status = EDocumentStructureStatus::success;
    advance();
    if (m_report.succeeded())
    {
        const document_text::CRootForm root = document_text::select_root(m_token, m_scanner);
        push(root.object, root.implicit);
        if (root.implicit && m_report.succeeded())
        {
            if (root.object && (m_token.kind != ETokenKind::end))
            {
                m_report.findings |= document_finding_bit(EDocumentFinding::implicit_body);
            }
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
        fail(EDocumentFailureReason::trailing_content);
    }
    m_report.findings |= m_scanner.findings();
    if (m_report.succeeded() && (estimates != nullptr))
    {
        *estimates = m_estimates;
    }
    return m_report;
}

}   //  namespace structure_util

CDocumentStructureReport check(const CStringView& source, CDocumentStructureEstimates* const estimates) noexcept
{
    if (estimates != nullptr)
    {
        *estimates = {};
    }
    if (source.empty())
    {
        CDocumentStructureReport report;
        report.status = EDocumentStructureStatus::failed;
        report.failure = { EDocumentFailureStage::structure, EDocumentFailureReason::invalid_input_view };
        return report;
    }
    structure_util::CCheck checker(source);
    return checker.run(estimates);
}

}   //  namespace document_structure
