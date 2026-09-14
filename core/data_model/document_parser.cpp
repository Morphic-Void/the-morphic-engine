
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_parser.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "data_model/document_parser.hpp"

#include <charconv>
#include <cstring>
#include <limits>
#include <utility>

#include "data_model/live_document.hpp"
#include "data_model/document_structure.hpp"
#include "data_model/document_text_lex.hpp"

namespace document_parser
{

//  Keep construction types distinct from the public parsing entry point.
namespace parser_util
{

using document_text::CToken;
using document_text::ETokenKind;

//  read_escape has already established that scalar is a Unicode scalar value.
//  Emit ordinary UTF-8 here; live string admission owns modified-NUL storage.
static std::size_t encode_scalar(const std::uint32_t scalar, std::uint8_t* const bytes) noexcept
{
    if (scalar < 0x80u)
    {
        bytes[0] = static_cast<std::uint8_t>(scalar);
        return 1u;
    }
    if (scalar < 0x800u)
    {
        bytes[0] = static_cast<std::uint8_t>(0xc0u | (scalar >> 6u));
        bytes[1] = static_cast<std::uint8_t>(0x80u | (scalar & 0x3fu));
        return 2u;
    }
    if (scalar < 0x10000u)
    {
        bytes[0] = static_cast<std::uint8_t>(0xe0u | (scalar >> 12u));
        bytes[1] = static_cast<std::uint8_t>(0x80u | ((scalar >> 6u) & 0x3fu));
        bytes[2] = static_cast<std::uint8_t>(0x80u | (scalar & 0x3fu));
        return 3u;
    }
    bytes[0] = static_cast<std::uint8_t>(0xf0u | (scalar >> 18u));
    bytes[1] = static_cast<std::uint8_t>(0x80u | ((scalar >> 12u) & 0x3fu));
    bytes[2] = static_cast<std::uint8_t>(0x80u | ((scalar >> 6u) & 0x3fu));
    bytes[3] = static_cast<std::uint8_t>(0x80u | (scalar & 0x3fu));
    return 4u;
}

enum class EFrameRole : std::uint8_t { object = 0u, array };

struct CFrame
{
    CNodeKey node;
    EFrameRole role;
    CTextLocation entry_location;
};

class CParser
{
public:
    explicit CParser(const CStringView& source, CDocumentReport& report) noexcept;
    void run(CLiveDocument& destination, const CDocumentParseOptions& options) noexcept;

private:
    void fail(const EDocumentFailureReason reason) noexcept;
    void advance() noexcept;
    [[nodiscard]] bool push(const CNodeKey node, const EFrameRole role, const CTextLocation& entry_location) noexcept;
    [[nodiscard]] bool require(const bool success) noexcept;
    [[nodiscard]] bool append_text(CByteBuffer& buffer, const std::uint8_t* const bytes, const std::size_t size) noexcept;
    [[nodiscard]] CStringView text(const CToken& token, CByteBuffer& buffer) noexcept;
    [[nodiscard]] CNodeKey integer(const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey floating(const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey create(const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey member(const CNodeKey object, const CStringView& name) const noexcept;
    [[nodiscard]] bool attach(const CFrame& parent, const CNodeKey node) noexcept;
    void complete() noexcept;
    void entry() noexcept;

    const CStringView& m_source;
    document_text::CScanner m_scanner;
    CToken m_token;
    TPodVector<CFrame> m_frames;

    //  Separate scratch buffers keep the decoded name stable while decoding its value.
    CByteBuffer m_name_scratch;
    CByteBuffer m_value_scratch;

    CLiveDocument m_document;
    CDocumentReport& m_report;
};

static EDocumentFailureReason linter_failure_reason(const ETextLintFailure reason) noexcept
{
    switch (reason)
    {
        case ETextLintFailure::invalid_input_view: return EDocumentFailureReason::invalid_input_view;
        case ETextLintFailure::input_limit: return EDocumentFailureReason::input_limit;
        case ETextLintFailure::output_limit: return EDocumentFailureReason::storage_limit;
        case ETextLintFailure::allocation_failed: return EDocumentFailureReason::allocation_failed;
        case ETextLintFailure::utf8_decode: return EDocumentFailureReason::utf8_decode;
        case ETextLintFailure::undefined_cp1252_byte: return EDocumentFailureReason::undefined_cp1252_byte;
        case ETextLintFailure::cp1252_decode: return EDocumentFailureReason::cp1252_decode;
        default: return EDocumentFailureReason::internal_error;
    }
}

CParser::CParser(const CStringView& source, CDocumentReport& report) noexcept : m_source(source), m_scanner(source), m_report(report)
{
}

void CParser::fail(const EDocumentFailureReason reason) noexcept
{
    if (m_report.processing_succeeded())
    {
        m_report.state = EDocumentProcessingState::failure;
        m_report.failure = { EDocumentFailureStage::parser, reason };
        m_report.failure.element_start = m_report.failure.location = m_token.location;
    }
}

void CParser::advance() noexcept
{
    m_token = m_scanner.next();
    if (m_token.kind == ETokenKind::error)
    {
        //  The immutable input already passed the same lexical rules.
        fail(EDocumentFailureReason::internal_error);
    }
}

bool CParser::require(const bool success) noexcept
{
    if (!success)
    {
        fail(EDocumentFailureReason::construction_failed);
    }
    return success;
}

bool CParser::push(const CNodeKey node, const EFrameRole role, const CTextLocation& entry_location) noexcept
{
    if (m_frames.size() == memory::t_max_elements<CFrame>())
    {
        fail(EDocumentFailureReason::storage_limit);
        return false;
    }
    if (!m_frames.push_back(CFrame{ node, role, entry_location }))
    {
        fail(EDocumentFailureReason::allocation_failed);
        return false;
    }
    return true;
}

bool CParser::append_text(CByteBuffer& buffer, const std::uint8_t* const bytes, const std::size_t size) noexcept
{
    if (size == 0u)
    {
        return true;
    }
    if (size > (memory::k_byte_size_ceiling - buffer.size()))
    {
        fail(EDocumentFailureReason::storage_limit);
        return false;
    }
    if (!buffer.append(bytes, size))
    {
        fail(EDocumentFailureReason::allocation_failed);
        return false;
    }
    return true;
}

CStringView CParser::text(const CToken& token, CByteBuffer& buffer) noexcept
{
    const bool quoted = token.kind == ETokenKind::string;
    const std::size_t first = token.offset + (quoted ? 1u : 0u);
    const std::size_t size = token.size - (quoted ? 2u : 0u);
    const std::uint8_t* const bytes = m_source.string();
    if (std::memchr(bytes + first, '\\', size) == nullptr)
    {
        return CStringView{ bytes + first, size };
    }
    (void)buffer.set_size(0u);
    const std::size_t end = first + size;
    std::size_t offset = first;
    while (offset < end)
    {
        const std::size_t start = offset;
        while ((offset < end) && (bytes[offset] != '\\'))
        {
            ++offset;
        }
        if (!append_text(buffer, bytes + start, offset - start))
        {
            return {};
        }
        if (offset < end)
        {
            std::uint32_t scalar = 0u;
            if (document_text::read_escape(m_source, offset, quoted ? bytes[token.offset] : '"', scalar) != EDocumentFailureReason::none)
            {
                fail(EDocumentFailureReason::internal_error);
                return {};
            }
            std::uint8_t encoded[4u];
            const std::size_t count = encode_scalar(scalar, encoded);
            if (!append_text(buffer, encoded, count))
            {
                return {};
            }
        }
    }
    return CStringView{ buffer.data(), buffer.size() };
}

CNodeKey CParser::integer(const CStringView& name) noexcept
{
    constexpr std::uint64_t max_signed_integer = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

    const char* first = reinterpret_cast<const char*>(m_source.string() + m_token.offset);
    const char* const last = first + m_token.size;
    const bool negative = *first == '-';
    const bool signed_value = negative || (*first == '+');
    if (signed_value)
    {
        ++first;
    }
    CIntegerMetadata metadata;
    metadata.domain = signed_value ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value;
    int base = 10;
    if (*first == '#')
    {
        base = 16;
        metadata.prefix = EIntegerPrefix::alternate;
        ++first;
    }
    else if (((last - first) >= 2) && (*first == '0'))
    {
        if ((first[1] == 'x') || (first[1] == 'X'))
        {
            base = 16;
            first += 2;
        }
        else if ((first[1] == 'b') || (first[1] == 'B'))
        {
            base = 2;
            first += 2;
        }
    }
    metadata.notation = (base == 16) ? EIntegerNotation::hexadecimal : ((base == 2) ? EIntegerNotation::binary : EIntegerNotation::decimal);
    std::uint64_t magnitude = 0u;
    const auto converted = std::from_chars(first, last, magnitude, base);
    if (converted.ec == std::errc::result_out_of_range)
    {
        fail(EDocumentFailureReason::numeric_out_of_range);
        return {};
    }
    if ((converted.ec != std::errc{}) || (converted.ptr != last))
    {
        fail(EDocumentFailureReason::internal_error);
        return {};
    }
    if (!signed_value)
    {
        metadata.width = live_unsigned_integer_smallest_width(magnitude);
        return m_document.create_unsigned_integer(magnitude, metadata, name);
    }
    if (magnitude > (max_signed_integer + (negative ? 1u : 0u)))
    {
        fail(EDocumentFailureReason::numeric_out_of_range);
        return {};
    }
    std::int64_t value = 0;
    if (!negative)
    {
        value = static_cast<std::int64_t>(magnitude);
    }
    else if (magnitude == (max_signed_integer + 1u))
    {
        value = std::numeric_limits<std::int64_t>::min();
    }
    else
    {
        value = -static_cast<std::int64_t>(magnitude);
    }
    metadata.width = live_signed_integer_smallest_width(value);
    return m_document.create_signed_integer(value, metadata, name);
}

CNodeKey CParser::floating(const CStringView& name) noexcept
{
    const char* first = reinterpret_cast<const char*>(m_source.string() + m_token.offset);
    const char* const last = first + m_token.size;
    if (*first == '+')
    {
        ++first;
    }
    double value = 0.0;
    const auto converted = std::from_chars(first, last, value);
    if ((converted.ec == std::errc::result_out_of_range) || !live_floating_point_is_finite(value))
    {
        fail(EDocumentFailureReason::numeric_out_of_range);
        return {};
    }
    if ((converted.ec != std::errc{}) || (converted.ptr != last))
    {
        fail(EDocumentFailureReason::internal_error);
        return {};
    }
    return m_document.create_floating_point(value, name);
}

CNodeKey CParser::create(const CStringView& name) noexcept
{
    switch (m_token.kind)
    {
        case ETokenKind::null_value:
        {
            return m_document.create_null(name);
        }
        case ETokenKind::true_value:
        case ETokenKind::false_value:
        {
            return m_document.create_boolean(m_token.kind == ETokenKind::true_value, name);
        }
        case ETokenKind::integer:
        {
            return integer(name);
        }
        case ETokenKind::floating_point:
        {
            return floating(name);
        }
        case ETokenKind::string:
        case ETokenKind::unquoted_string:
        {
            const CStringView value = text(m_token, m_value_scratch);
            const CNodeKey node = m_report.processing_succeeded() ? m_document.create_string(value, name) : CNodeKey{};
            if (node.is_valid() && (m_token.literal_line_break != 0u) && !m_document.set_newline_escaping_suppressed(node, true))
            {
                fail(EDocumentFailureReason::internal_error);
                return {};
            }
            return node;
        }
        case ETokenKind::object_begin:
        {
            return m_document.create_object(name);
        }
        case ETokenKind::array_begin:
        {
            return m_document.create_array(name);
        }
        default:
        {
            fail(EDocumentFailureReason::internal_error);
            return {};
        }
    }
}

CNodeKey CParser::member(const CNodeKey object, const CStringView& name) const noexcept
{
    for (CNodeKey child = m_document.first_child(object); child.is_valid(); child = m_document.next_sibling(child))
    {
        if (m_document.name(child) == name)
        {
            return child;
        }
    }
    return {};
}

bool CParser::attach(const CFrame& parent, const CNodeKey node) noexcept
{
    CNodeKey existing;
    if (parent.role == EFrameRole::object)
    {
        existing = member(parent.node, m_document.name(node));
    }
    if (!existing.is_valid())
    {
        return require(m_document.append_child(parent.node, node).succeeded());
    }
    if (!require(m_document.extend_object_child(parent.node, node).is_valid()))
    {
        return false;
    }
    m_report.findings |= document_finding_bit(EDocumentFinding::name_collision_extension);
    return true;
}

void CParser::complete() noexcept
{
    const CFrame frame = m_frames.last();
    (void)m_frames.discard_back();
    if (frame.node == m_document.root())
    {
        return;
    }
    if (m_frames.is_empty())
    {
        fail(EDocumentFailureReason::internal_error);
        return;
    }
    CNodeKey node = frame.node;
    const CFrame parent = m_frames.last();
    if ((frame.role == EFrameRole::object) && (parent.role == EFrameRole::array) &&
        !m_document.is_object_entry(node) && (m_document.child_count(node) == 1u))
    {
        const CNodeKey child = m_document.first_child(node);
        if (!require(m_document.detach(child)) || !require(m_document.erase(node)))
        {
            m_report.failure.element_start = frame.entry_location;
            return;
        }
        node = child;
        m_report.findings |= document_finding_bit(EDocumentFinding::singleton_normalization);
    }
    if (!attach(parent, node))
    {
        m_report.failure.element_start = frame.entry_location;
    }
}

void CParser::entry() noexcept
{
    const CFrame parent = m_frames.last();
    const CTextLocation entry_location = m_token.location;
    CStringView name;
    if (parent.role == EFrameRole::object)
    {
        name = text(m_token, m_name_scratch);
        if (!m_report.processing_succeeded())
        {
            return;
        }
        advance(); // colon; structure has already established member placement
        if (m_token.kind != ETokenKind::colon)
        {
            fail(EDocumentFailureReason::internal_error);
            return;
        }
        advance();
    }
    if (!m_report.processing_succeeded())
    {
        return;
    }
    const CNodeKey node = create(name);
    if (!m_report.processing_succeeded())
    {
        return;
    }
    if (!node.is_valid())
    {
        //  Live creation can fail for allocation or storage limits; its public
        //  API does not distinguish those causes. Do not invent a precise cause.
        fail(EDocumentFailureReason::construction_failed);
        return;
    }
    if ((m_token.kind == ETokenKind::object_begin) || (m_token.kind == ETokenKind::array_begin))
    {
        const EFrameRole role = (m_token.kind == ETokenKind::object_begin) ? EFrameRole::object : EFrameRole::array;
        if (!push(node, role, entry_location))
        {
            return;
        }
    }
    else if (!attach(parent, node))
    {
        m_report.failure.element_start = entry_location;
        return;
    }
    advance();
}

void CParser::run(CLiveDocument& destination, const CDocumentParseOptions& options) noexcept
{
    m_report.state = EDocumentProcessingState::success;
    advance();
    const document_text::CRootForm root = document_text::select_root(m_token, m_scanner);
    if (!m_document.initialise() || !m_document.set_root_type(root.object ? ELiveValueType::object : ELiveValueType::array))
    {
        fail(EDocumentFailureReason::construction_failed);
    }
    const CTextLocation root_location = root.implicit ? CTextLocation{ true, 1u, 1u } : m_token.location;
    if (m_report.processing_succeeded() && push(m_document.root(), root.object ? EFrameRole::object : EFrameRole::array, root_location))
    {
        if (!root.implicit)
        {
            advance();
        }
        while (m_report.processing_succeeded() && (m_token.kind != ETokenKind::end))
        {
            if (m_frames.is_empty())
            {
                fail(EDocumentFailureReason::internal_error);
                break;
            }
            if ((m_token.kind == ETokenKind::object_end) || (m_token.kind == ETokenKind::array_end))
            {
                complete();
                if (m_report.processing_succeeded())
                {
                    advance();
                }
            }
            else if (m_token.kind == ETokenKind::comma)
            {
                advance();
            }
            else
            {
                entry();
            }
        }
        if (m_report.processing_succeeded() && (m_frames.size() != (root.implicit ? 1u : 0u)))
        {
            fail(EDocumentFailureReason::internal_error);
        }
    }
    if (m_report.processing_succeeded())
    {
        m_report.policy = document_policy::evaluate(m_report.findings, options);
        if (m_report.policy.accepted())
        {
            destination = std::move(m_document);
        }
    }
}

}   //  namespace parser_util

CDocumentReport parse(const CByteConstView& source, CLiveDocument& destination, const CDocumentParseOptions& options, CTextLintReport* const linter_report) noexcept
{
    const CStringView input = (source.size() == 0u) ? CStringView{ "", 0u } : CStringView{ source.data(), source.size() };
    const CTextLintResult linted = text_linter::lint(input, k_document_text_lint_line_endings);
    if (linter_report != nullptr)
    {
        *linter_report = linted.report;
    }
    const std::uint32_t source_findings = document_findings::from_source(linted.report.source_findings);
    if (!linted.report.success)
    {
        CDocumentReport report;
        report.state = EDocumentProcessingState::failure;
        report.findings = source_findings;
        report.failure = { EDocumentFailureStage::linter, parser_util::linter_failure_reason(linted.report.first_failure.reason),
            linted.report.first_failure.location, {} };
        return report;
    }
    const CStringView text{ linted.output.data(), linted.report.logical_text_byte_size };
    CDocumentReport report = document_structure::check(text);
    report.findings |= source_findings;
    if (report.processing_succeeded())
    {
        parser_util::CParser parser(text, report);
        parser.run(destination, options);
    }
    return report;
}

}   //  namespace document_parser
