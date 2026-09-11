
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

namespace document_parser
{

//  Keep construction types distinct from the public parsing entry point.
namespace parser_util
{

using document_text::CToken;
using document_text::ETokenKind;

//  These free helpers are private to this translation unit.
static std::size_t reserved_dollars(const CStringView& name) noexcept
{
    std::size_t dollars = 0u;
    while ((dollars < name.length()) && (name.string()[dollars] == '$'))
    {
        ++dollars;
    }
    return ((name.length() - dollars) == 7u) && (std::memcmp(name.string() + dollars, "morphic", 7u) == 0) ? dollars : 0u;
}

//  Spelling has been checked by the scanner. Match version one without
//  imposing a numeric range on unsupported version tokens.
static bool version_one(const CStringView& source, const CToken& token) noexcept
{
    if (token.kind != ETokenKind::integer)
    {
        return false;
    }
    const std::uint8_t* first = source.string() + token.offset;
    const std::uint8_t* const last = first + token.size;
    if (*first == '-')
    {
        return false;
    }
    if (*first == '+')
    {
        ++first;
    }
    if (*first == '#')
    {
        ++first;
    }
    else if (((last - first) >= 2) && (*first == '0') &&
        ((first[1] == 'x') || (first[1] == 'X') || (first[1] == 'b') || (first[1] == 'B')))
    {
        first += 2;
    }
    while ((first < last) && (*first == '0'))
    {
        ++first;
    }
    return ((last - first) == 1) && (*first == '1');
}

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

enum class EFrameRole : std::uint8_t { object = 0u, array, wrapper, metadata, transport };

static constexpr std::uint8_t k_version_field = 1u;
static constexpr std::uint8_t k_type_field = 2u;
static constexpr std::uint8_t k_values_field = 4u;
static constexpr std::uint8_t k_all_control_fields = k_version_field | k_type_field | k_values_field;

struct CFrame
{
    CNodeKey node;
    EFrameRole role;
    CTextLocation entry_location;
    std::uint8_t control_fields{ 0u };
};

class CParser
{
public:
    explicit CParser(const CStringView& source) noexcept : m_source(source), m_scanner(source) {}
    [[nodiscard]] CDocumentParseReport run(CLiveDocument& destination) noexcept;

private:
    void fail(const EDocumentParseStatus status) noexcept;
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
    void metadata_entry() noexcept;
    void begin_recovery(const CTextLocation& entry_location) noexcept;
    void complete() noexcept;
    void entry() noexcept;

    const CStringView& m_source;
    document_text::CScanner m_scanner;
    CToken m_token;
    TPodVector<CFrame> m_frames;
    //  Names must remain stable while a string value is decoded.
    CByteBuffer m_name_scratch;
    CByteBuffer m_value_scratch;
    CLiveDocument m_document;
    CDocumentParseReport m_report;
};

static CDocumentParseReport ingest_linted(const CTextLintResult& linted, CLiveDocument& destination) noexcept
{
    CDocumentParseReport report;
    if (linted.report.success)
    {
        report = parse(CStringView{ linted.output.data(), linted.report.logical_text_byte_size }, destination);
    }
    else
    {
        report.status = EDocumentParseStatus::linter_failure;
        report.failure_point = linted.report.first_failure.location;
    }
    report.linter_examined = true;
    report.linter = linted.report;
    return report;
}

void CParser::fail(const EDocumentParseStatus status) noexcept
{
    if (m_report.succeeded())
    {
        m_report.status = status;
        m_report.structure_start = m_report.failure_point = m_token.location;
    }
}

void CParser::advance() noexcept
{
    m_token = m_scanner.next();
    if (m_token.kind == ETokenKind::error)
    {
        //  The immutable input already passed the same lexical rules.
        fail(EDocumentParseStatus::internal_error);
    }
}

bool CParser::require(const bool success) noexcept
{
    if (!success)
    {
        fail(EDocumentParseStatus::construction_failed);
    }
    return success;
}

bool CParser::push(const CNodeKey node, const EFrameRole role, const CTextLocation& entry_location) noexcept
{
    if (m_frames.size() == memory::t_max_elements<CFrame>())
    {
        fail(EDocumentParseStatus::storage_limit);
        return false;
    }
    if (!m_frames.push_back(CFrame{ node, role, entry_location }))
    {
        fail(EDocumentParseStatus::allocation_failed);
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
        fail(EDocumentParseStatus::storage_limit);
        return false;
    }
    if (!buffer.append(bytes, size))
    {
        fail(EDocumentParseStatus::allocation_failed);
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
    if (!quoted || (std::memchr(bytes + first, '\\', size) == nullptr))
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
            if (document_text::read_escape(m_source, offset, bytes[token.offset], scalar) != document_text::ESyntaxError::none)
            {
                fail(EDocumentParseStatus::internal_error);
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
        fail(EDocumentParseStatus::numeric_out_of_range);
        return {};
    }
    if ((converted.ec != std::errc{}) || (converted.ptr != last))
    {
        fail(EDocumentParseStatus::internal_error);
        return {};
    }
    if (!signed_value)
    {
        metadata.width = live_unsigned_integer_smallest_width(magnitude);
        return m_document.create_unsigned_integer(magnitude, metadata, name);
    }
    const std::uint64_t maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (magnitude > (maximum + (negative ? 1u : 0u)))
    {
        fail(EDocumentParseStatus::numeric_out_of_range);
        return {};
    }
    std::int64_t value = 0;
    if (!negative)
    {
        value = static_cast<std::int64_t>(magnitude);
    }
    else if (magnitude == (maximum + 1u))
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
        fail(EDocumentParseStatus::numeric_out_of_range);
        return {};
    }
    if ((converted.ec != std::errc{}) || (converted.ptr != last))
    {
        fail(EDocumentParseStatus::internal_error);
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
        {
            const CStringView value = text(m_token, m_value_scratch);
            return m_report.succeeded() ? m_document.create_string(value, name) : CNodeKey{};
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
            fail(EDocumentParseStatus::internal_error);
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
    if (m_document.value_type(existing) != ELiveValueType::recovered_array)
    {
        const CNodeKey first = m_document.detach_payload(existing);
        if (!require(first.is_valid()))
        {
            return false;
        }
        const CNodeKey recovery = m_document.create_recovered_array();
        if (!require(recovery.is_valid()) ||
            !require(m_document.append_child(recovery, first).succeeded()) ||
            !require(m_document.attach_payload(existing, recovery).is_valid()))
        {
            return false;
        }
    }
    //  Extract the complete incoming payload, including a recovered array as
    //  one nested competitor. The receiver keeps its original name and place.
    const CNodeKey payload = m_document.detach_payload(node);
    if (!require(payload.is_valid()) ||
        !require(m_document.erase(node)) ||
        !require(m_document.append_child(existing, payload).succeeded()))
    {
        return false;
    }
    ++m_report.interpretations.duplicate_members_recovered;
    return true;
}

void CParser::begin_recovery(const CTextLocation& entry_location) noexcept
{
    const CNodeKey wrapper = m_frames.last().node;
    if (wrapper == m_document.root())
    {
        fail(EDocumentParseStatus::invalid_root_value);
        return;
    }
    if (m_document.child_count(wrapper) != 0u)
    {
        fail(EDocumentParseStatus::malformed_recovery_wrapper);
        return;
    }
    advance(); // colon
    advance(); // metadata object
    if (m_token.kind != ETokenKind::object_begin)
    {
        fail(EDocumentParseStatus::malformed_recovery_wrapper);
        return;
    }
    const CNodeKey recovery = m_document.create_recovered_array();
    if (!require(recovery.is_valid()) ||
        !require(m_document.erase_payload(wrapper)) ||
        !require(m_document.attach_payload(wrapper, recovery).is_valid()))
    {
        return;
    }
    m_frames.last().role = EFrameRole::wrapper;
    //  Metadata and its transport array feed this same recovered value; they
    //  are grammar contexts, not extra live nodes or ordinary object members.
    if (push(wrapper, EFrameRole::metadata, entry_location))
    {
        advance();
    }
}

void CParser::metadata_entry() noexcept
{
    const CStringView name = text(m_token, m_name_scratch);
    if (!m_report.succeeded())
    {
        return;
    }
    std::uint8_t field = 0u;
    if (name == CStringView{ "v" })
    {
        field = k_version_field;
    }
    else if (name == CStringView{ "type" })
    {
        field = k_type_field;
    }
    else if (name == CStringView{ "values" })
    {
        field = k_values_field;
    }
    if ((field == 0u) || ((m_frames.last().control_fields & field) != 0u))
    {
        fail(EDocumentParseStatus::malformed_recovery_wrapper);
        return;
    }
    m_frames.last().control_fields |= field;
    advance(); // colon
    advance(); // field value
    if (field == k_version_field)
    {
        if (m_token.kind != ETokenKind::integer)
        {
            fail(EDocumentParseStatus::malformed_recovery_wrapper);
            return;
        }
        if (!version_one(m_source, m_token))
        {
            fail(EDocumentParseStatus::unsupported_recovery_version);
            return;
        }
    }
    else if (field == k_type_field)
    {
        if (m_token.kind != ETokenKind::string)
        {
            fail(EDocumentParseStatus::malformed_recovery_wrapper);
            return;
        }
        const CStringView type = text(m_token, m_value_scratch);
        if (!m_report.succeeded())
        {
            return;
        }
        if (!(type == CStringView{ "recovered-array" }))
        {
            fail(EDocumentParseStatus::unsupported_recovery_type);
            return;
        }
    }
    else
    {
        if (m_token.kind != ETokenKind::array_begin)
        {
            fail(EDocumentParseStatus::malformed_recovery_wrapper);
            return;
        }
        if (!push(m_frames.last().node, EFrameRole::transport, m_token.location))
        {
            return;
        }
    }
    advance();
}

void CParser::complete() noexcept
{
    const CFrame frame = m_frames.last();
    if ((frame.role == EFrameRole::metadata) && (frame.control_fields != k_all_control_fields))
    {
        fail(EDocumentParseStatus::malformed_recovery_wrapper);
        return;
    }
    (void)m_frames.discard_back();
    if ((frame.role == EFrameRole::metadata) || (frame.role == EFrameRole::transport) || (frame.node == m_document.root()))
    {
        return;
    }
    if (m_frames.is_empty())
    {
        fail(EDocumentParseStatus::internal_error);
        return;
    }
    if (frame.role == EFrameRole::wrapper)
    {
        ++m_report.interpretations.recovered_arrays_decoded;
    }
    CNodeKey node = frame.node;
    const CFrame parent = m_frames.last();
    if ((frame.role == EFrameRole::object) && (parent.role == EFrameRole::array) &&
        !m_document.is_object_entry(node) && (m_document.child_count(node) == 1u))
    {
        const CNodeKey child = m_document.first_child(node);
        if (!require(m_document.detach(child)) || !require(m_document.erase(node)))
        {
            m_report.structure_start = frame.entry_location;
            return;
        }
        node = child;
        ++m_report.interpretations.singleton_objects_unwrapped;
    }
    if (!attach(parent, node))
    {
        m_report.structure_start = frame.entry_location;
    }
}

void CParser::entry() noexcept
{
    const CFrame parent = m_frames.last();
    if (parent.role == EFrameRole::metadata)
    {
        metadata_entry();
        return;
    }
    if (parent.role == EFrameRole::wrapper)
    {
        fail(EDocumentParseStatus::malformed_recovery_wrapper);
        return;
    }
    const CTextLocation entry_location = m_token.location;
    CStringView name;
    if (parent.role == EFrameRole::object)
    {
        name = text(m_token, m_name_scratch);
        if (!m_report.succeeded())
        {
            return;
        }
        if (name.length() == 0u)
        {
            fail(EDocumentParseStatus::empty_property_name);
            return;
        }
        const std::size_t dollars = reserved_dollars(name);
        if (dollars == 1u)
        {
            begin_recovery(entry_location);
            return;
        }
        if (dollars > 1u)
        {
            name = CStringView{ name.string() + 1u, name.length() - 1u };
            ++m_report.interpretations.reserved_names_unescaped;
        }
        advance(); // colon; structure has already established member placement
        if (m_token.kind != ETokenKind::colon)
        {
            fail(EDocumentParseStatus::internal_error);
            return;
        }
        advance();
    }
    if (!m_report.succeeded())
    {
        return;
    }
    const CNodeKey node = create(name);
    if (!m_report.succeeded())
    {
        return;
    }
    if (!node.is_valid())
    {
        //  Live creation can fail for allocation or storage limits; its public
        //  API does not distinguish those causes. Do not invent a precise cause.
        fail(EDocumentParseStatus::construction_failed);
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
        m_report.structure_start = entry_location;
        return;
    }
    advance();
}

CDocumentParseReport CParser::run(CLiveDocument& destination) noexcept
{
    m_report.status = EDocumentParseStatus::success;
    advance();
    const bool implicit = m_token.kind != ETokenKind::object_begin;
    if (!m_document.initialise())
    {
        fail(EDocumentParseStatus::construction_failed);
    }
    if (m_report.succeeded() && push(m_document.root(), EFrameRole::object, m_token.location))
    {
        if (!implicit)
        {
            advance();
        }
        while (m_report.succeeded() && (m_token.kind != ETokenKind::end))
        {
            if (m_frames.is_empty())
            {
                fail(EDocumentParseStatus::internal_error);
                break;
            }
            if ((m_token.kind == ETokenKind::object_end) || (m_token.kind == ETokenKind::array_end))
            {
                complete();
                if (m_report.succeeded())
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
        if (m_report.succeeded() && (m_frames.size() != (implicit ? 1u : 0u)))
        {
            fail(EDocumentParseStatus::internal_error);
        }
    }
    if (m_report.succeeded())
    {
        destination = std::move(m_document);
    }
    else
    {
        m_report.interpretations = {};
    }
    return m_report;
}

}   //  namespace parser_util

CDocumentParseReport parse(const CStringView& source, CLiveDocument& destination) noexcept
{
    CDocumentParseReport report;
    report.structure = document_structure::check(source);
    if (!report.structure.succeeded())
    {
        report.status = (report.structure.status == EDocumentStructureStatus::invalid_input_view) ?
            EDocumentParseStatus::invalid_input_view : EDocumentParseStatus::structural_failure;
        report.structure_start = report.structure.structure_start;
        report.failure_point = report.structure.failure_point;
        return report;
    }
    parser_util::CParser parser(source);
    CDocumentParseReport result = parser.run(destination);
    result.structure = report.structure;
    return result;
}

CDocumentParseReport ingest(const CByteConstView& source, CLiveDocument& destination) noexcept
{
    const CTextLintResult linted = text_linter::lint(source, k_document_text_lint_line_endings);
    return parser_util::ingest_linted(linted, destination);
}

CDocumentParseReport ingest(const CStringView& source, CLiveDocument& destination) noexcept
{
    const CTextLintResult linted = text_linter::lint(source, k_document_text_lint_line_endings);
    return parser_util::ingest_linted(linted, destination);
}

}   //  namespace document_parser
