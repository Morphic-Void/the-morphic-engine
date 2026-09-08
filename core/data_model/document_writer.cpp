
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_writer.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "data_model/document_writer.hpp"

#include <charconv>
#include <cstring>
#include <utility>

#include "data_model/baked_document.hpp"
#include "debug/macros.hpp"
#include "memory/memory_policies.hpp"

namespace document_writer
{
namespace writer_util
{

[[nodiscard]] static bool is_container(const EBakedValueType type) noexcept
{
    return (type == EBakedValueType::array) || (type == EBakedValueType::object) || (type == EBakedValueType::recovered_array);
}

[[nodiscard]] static bool is_reserved_data_name(const CStringView& name) noexcept
{
    std::size_t dollars = 0u;
    while ((dollars < name.length()) && (name.string()[dollars] == '$'))
    {
        ++dollars;
    }
    return (dollars != 0u) && ((name.length() - dollars) == 7u) &&
        (std::memcmp(name.string() + dollars, "morphic", 7u) == 0);
}

//  Decode only already-validated baked payloads: strict UTF-8 plus C0 80.
//  No validity policy or normalization belongs here. Physical terminators
//  are excluded by the caller's explicit string length.
[[nodiscard]] static std::uint32_t decode_scalar(const std::uint8_t* const bytes, std::size_t& size) noexcept
{
    const std::uint8_t lead = bytes[0];
    if (lead < 0x80u) { size = 1u; return lead; }
    if (lead < 0xe0u)
    {
        size = 2u;
        return ((lead & 0x1fu) << 6u) | (bytes[1] & 0x3fu);
    }
    if (lead < 0xf0u)
    {
        size = 3u;
        return ((lead & 0x0fu) << 12u) | ((bytes[1] & 0x3fu) << 6u) | (bytes[2] & 0x3fu);
    }
    size = 4u;
    return ((lead & 0x07u) << 18u) | ((bytes[1] & 0x3fu) << 12u) | ((bytes[2] & 0x3fu) << 6u) | (bytes[3] & 0x3fu);
}

class CWriter
{
public:
    //  Bound inputs must outlive this single-use writer.
    CWriter(const CBakedDocument& source, const CDocumentWriteOptions& options) noexcept : m_source(source), m_options(options) {}

    //  Emit once, then transfer complete output or discard partial work.
    [[nodiscard]] CDocumentWriteResult run() noexcept;

private:
    //  Status and byte storage: retain the first failure and bound every extension.
    [[nodiscard]] bool good() const noexcept { return m_report.succeeded(); }
    void fail(const EDocumentWriteStatus status) noexcept { if (good()) m_report.status = status; }
    [[nodiscard]] bool grow(const std::size_t count) noexcept;
    void append(const char* const bytes, const std::size_t count) noexcept;
    template<std::size_t N> void literal(const char (&bytes)[N]) noexcept { append(bytes, (N - 1u)); }
    void character(const char value) noexcept { append(&value, 1u); }

    //  Layout: separators, line endings and indentation at the current nesting depth.
    void newline() noexcept;
    void indentation() noexcept;
    void item(const bool follows_item) noexcept;
    void begin(const char bracket) noexcept;
    void end(const char bracket, const bool nonempty) noexcept;

    //  Scalar spelling: quoting/escaping and the selected numeric output grammar.
    void quoted(const CStringView& value, const bool data_name = false) noexcept;
    void key(const CStringView& value, const bool data_name = false) noexcept;
    void hex_escape(std::uint32_t unit) noexcept;
    void integer(CBakedValueIndex node) noexcept;
    void floating(CBakedValueIndex node) noexcept;

    //  Iterative traversal with implied objects and explicit recovery payloads.
    [[nodiscard]] bool needs_object_wrapper(const CBakedValueIndex node) const noexcept;
    void enter(CBakedValueIndex node) noexcept;
    void leave(CBakedValueIndex node) noexcept;
    void traverse() noexcept;

    //  Bound configuration; run() does not change or take ownership of either input.
    const CBakedDocument& m_source;
    const CDocumentWriteOptions& m_options;

    //  Per-run ownership, reporting and formatting state.
    CByteBuffer m_output;
    CDocumentWriteReport m_report;
    std::size_t m_depth{ 0u };
};

//  Run lifecycle: only a completed result may retain output or occurrence counts.
CDocumentWriteResult CWriter::run() noexcept
{
    m_report.status = EDocumentWriteStatus::success;

    traverse();
    if (good())
    {
        MV_ASSERT(m_depth == 0u);
        if (!m_output.append(1u))
        {
            fail(EDocumentWriteStatus::allocation_failed);
        }
    }

    CDocumentWriteResult result;
    if (good())
    {
        m_report.logical_text_byte_size = m_output.size() - 1u;
        result.output = std::move(m_output);
        result.report = m_report;
    }
    else
    {
        m_output.deallocate();
        result.report.status = m_report.status;
    }
    return result;
}

//  Byte storage: use the buffer's default growth policy, without a sizing pass.
bool CWriter::grow(const std::size_t count) noexcept
{
    if (!good())
    {
        return false;
    }

    //  Leave room within the size limit for the final physical zero.
    if (count > (memory::k_byte_size_ceiling - 1u - m_output.size()))
    {
        fail(EDocumentWriteStatus::output_exceeds_engine_size_limit);
        return false;
    }
    if (!m_output.append(count, false))
    {
        fail(EDocumentWriteStatus::allocation_failed);
        return false;
    }
    return true;
}

void CWriter::append(const char* const bytes, const std::size_t count) noexcept
{
    const std::size_t offset = m_output.size();
    if (grow(count))
    {
        if (count)
        {
            std::memcpy(m_output.data() + offset, bytes, count);
        }
    }
}

//  Layout helpers share the same byte path as scalar and container output.
void CWriter::newline() noexcept
{
    if (m_options.line_ending == EDocumentWriteLineEnding::crlf)
    {
        literal("\r\n");
    }
    else
    {
        literal("\n");
    }
}

void CWriter::indentation() noexcept
{
    if (m_depth && (m_options.indent_width > ((memory::k_byte_size_ceiling - 1u) / m_depth)))
    {
        fail(EDocumentWriteStatus::output_exceeds_engine_size_limit);
        return;
    }
    const std::size_t count = m_depth * m_options.indent_width;
    const std::size_t offset = m_output.size();
    if (grow(count))
    {
        if (count)
        {
            std::memset(m_output.data() + offset, ' ', count);
        }
    }
}

void CWriter::item(const bool follows_item) noexcept
{
    if (follows_item)
    {
        character(',');
    }
    if (m_options.pretty_print)
    {
        newline();
        indentation();
    }
}

void CWriter::begin(const char bracket) noexcept
{
    character(bracket);
    ++m_depth;
}

void CWriter::end(const char bracket, const bool nonempty) noexcept
{
    if (!m_depth)
    {
        fail(EDocumentWriteStatus::internal_error);
        return;
    }
    --m_depth;
    if (nonempty && m_options.pretty_print)
    {
        newline();
        indentation();
    }
    character(bracket);
}

//  Scalar emission assumes the baked representation has already been validated.
void CWriter::hex_escape(const std::uint32_t unit) noexcept
{
    constexpr char digits[] = "0123456789abcdef";
    const char escaped[]{ '\\', 'u',
        digits[(unit >> 12u) & 15u], digits[(unit >> 8u) & 15u],
        digits[(unit >> 4u) & 15u], digits[unit & 15u] };
    append(escaped, sizeof(escaped));
}

void CWriter::quoted(const CStringView& value, const bool data_name) noexcept
{
    if (!value.string())
    {
        fail(EDocumentWriteStatus::source_contract_violation);
        return;
    }
    character('"');
    if (data_name && is_reserved_data_name(value))
    {
        character('$');
        ++m_report.reserved_property_names_escaped;
    }
    for (std::size_t offset = 0u; good() && (offset < value.length());)
    {
        std::size_t size = 0u;
        const std::uint32_t point = decode_scalar((value.string() + offset), size);
        switch (point)
        {
            case ('"'):
            {
                literal("\\\"");
                break;
            }
            case ('\\'):
            {
                literal("\\\\");
                break;
            }
            case ('\b'):
            {
                literal("\\b");
                break;
            }
            case ('\f'):
            {
                literal("\\f");
                break;
            }
            case ('\n'):
            {
                literal("\\n");
                break;
            }
            case ('\r'):
            {
                literal("\\r");
                break;
            }
            case ('\t'):
            {
                literal("\\t");
                break;
            }
            default:
            {
                if (point < 0x20u)
                {
                    hex_escape(point);
                    if (point == 0u)
                    {
                        ++m_report.embedded_nuls_escaped;
                    }
                }
                else if (m_options.escape_non_ascii && (point > 0x7fu))
                {
                    if (point <= 0xffffu)
                    {
                        hex_escape(point);
                    }
                    else
                    {
                        const std::uint32_t supplementary = point - 0x10000u;
                        hex_escape(0xd800u + (supplementary >> 10u));
                        hex_escape(0xdc00u + (supplementary & 0x3ffu));
                    }
                    ++m_report.non_ascii_code_points_escaped;
                }
                else
                {
                    append(reinterpret_cast<const char*>(value.string() + offset), size);
                }
                break;
            }
        }
        offset += size;
    }
    character('"');
}

void CWriter::key(const CStringView& value, const bool data_name) noexcept
{
    quoted(value, data_name);
    character(':');
    if (m_options.pretty_print)
    {
        character(' ');
    }
}

void CWriter::integer(const CBakedValueIndex node) noexcept
{
    CIntegerMetadata metadata;
    if (!m_source.integer_metadata(node, metadata))
    {
        fail(EDocumentWriteStatus::source_contract_violation);
        return;
    }
    std::uint64_t magnitude = 0u;
    bool negative = false;
    const bool signed_value = metadata.domain == EIntegerDomain::signed_value;
    if (signed_value)
    {
        std::int64_t value = 0;
        if (!m_source.signed_integer_value(node, value))
        {
            fail(EDocumentWriteStatus::source_contract_violation);
            return;
        }
        negative = value < 0;
        magnitude = negative ? (std::uint64_t{ 0u } - static_cast<std::uint64_t>(value)) : static_cast<std::uint64_t>(value);
    }
    else if (!m_source.unsigned_integer_value(node, magnitude))
    {
        fail(EDocumentWriteStatus::source_contract_violation);
        return;
    }

    if (negative)
    {
        character('-');
    }
    else if (signed_value)
    {
        if (m_options.mode == EDocumentWriteMode::strict_json)
        {
            ++m_report.explicit_positive_signs_omitted;
        }
        else
        {
            character('+');
        }
    }
    int base = 10;
    if (metadata.notation != EIntegerNotation::decimal)
    {
        if (m_options.mode == EDocumentWriteMode::strict_json)
        {
            ++m_report.non_decimal_integers_normalised;
        }
        else if (metadata.notation == EIntegerNotation::hexadecimal)
        {
            base = 16;
            if (metadata.prefix == EIntegerPrefix::alternate)
            {
                character('#');
            }
            else
            {
                literal("0x");
            }
        }
        else
        {
            base = 2;
            literal("0b");
        }
    }
    char digits[64];
    const auto converted = std::to_chars(digits, digits + sizeof(digits), magnitude, base);
    if (converted.ec != std::errc{})
    {
        fail(EDocumentWriteStatus::internal_error);
        return;
    }
    append(digits, static_cast<std::size_t>(converted.ptr - digits));
}

void CWriter::floating(const CBakedValueIndex node) noexcept
{
    double value = 0.0;
    if (!m_source.floating_point_value(node, value))
    {
        fail(EDocumentWriteStatus::source_contract_violation);
        return;
    }
    if (value == 0.0)
    {
        if ((live_floating_point_bits(value) >> 63u) != 0u)
        {
            literal("-0.0");
        }
        else
        {
            literal("0.0");
        }
        return;
    }
    char digits[64];
    //  The no-precision overload selects shortest round-trip output. Conversion
    //  uses caller storage, is locale independent and reports errors explicitly.
    const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
    if (converted.ec != std::errc{})
    {
        fail(EDocumentWriteStatus::internal_error);
        return;
    }
    bool has_float_marker = false;
    for (const char* digit = digits; digit != converted.ptr; ++digit)
    {
        has_float_marker = has_float_marker || (*digit == '.') || (*digit == 'e');
    }
    append(digits, static_cast<std::size_t>(converted.ptr - digits));
    if (!has_float_marker)
    {
        literal(".0");
    }
}

//  Traversal follows baked links; synthetic recovery containers affect layout only.
void CWriter::enter(const CBakedValueIndex node) noexcept
{
    switch (m_source.value_type(node))
    {
        case (EBakedValueType::null_value):
        {
            literal("null");
            break;
        }
        case (EBakedValueType::boolean):
        {
            bool value = false;
            if (!m_source.boolean_value(node, value))
            {
                fail(EDocumentWriteStatus::source_contract_violation);
                break;
            }
            if (value)
            {
                literal("true");
            }
            else
            {
                literal("false");
            }
            break;
        }
        case (EBakedValueType::integer):
        {
            integer(node);
            break;
        }
        case (EBakedValueType::floating_point):
        {
            floating(node);
            break;
        }
        case (EBakedValueType::string):
        {
            quoted(m_source.string_value(node));
            break;
        }
        case (EBakedValueType::array):
        {
            begin('[');
            break;
        }
        case (EBakedValueType::object):
        {
            begin('{');
            break;
        }
        case (EBakedValueType::recovered_array):
        {
            ++m_report.recovered_arrays_written;
            begin('{');
            item(false);
            key(CStringView{ "$morphic" });
            begin('{');
            item(false);
            key(CStringView{ "v" });
            character('1');
            item(true);
            key(CStringView{ "type" });
            quoted(CStringView{ "recovered-array" });
            item(true);
            key(CStringView{ "values" });
            begin('[');
            break;
        }
        default:
        {
            fail(EDocumentWriteStatus::source_contract_violation);
            break;
        }
    }
}

void CWriter::leave(const CBakedValueIndex node) noexcept
{
    const EBakedValueType type = m_source.value_type(node);
    if (is_container(type))
    {
        end(type == EBakedValueType::object ? '}' : ']', m_source.child_count(node) != 0u);
        if (type == EBakedValueType::recovered_array)
        {
            end('}', true);
            end('}', true);
        }
    }
    if (needs_object_wrapper(node))
    {
        end('}', true);
    }
}

bool CWriter::needs_object_wrapper(const CBakedValueIndex node) const noexcept
{
    return m_source.is_object_entry(node) &&
        (m_source.value_type(m_source.parent(node)) != EBakedValueType::object);
}

void CWriter::traverse() noexcept
{
    const CBakedValueIndex root = m_source.root();
    CBakedValueIndex node = root;
    while (good())
    {
        if (node != root)
        {
            item(m_source.previous_sibling(node).is_valid());
            if (needs_object_wrapper(node))
            {
                begin('{');
                item(false);
            }
            if (m_source.is_object_entry(node))
            {
                key(m_source.name(node), true);
            }
        }
        enter(node);
        if (!good())
        {
            break;
        }
        const CBakedValueIndex child = m_source.first_child(node);
        if (child.is_valid())
        {
            node = child;
            continue;
        }
        leave(node);
        while (good() && (node != root))
        {
            const CBakedValueIndex next = m_source.next_sibling(node);
            if (next.is_valid())
            {
                node = next;
                break;
            }
            node = m_source.parent(node);
            leave(node);
        }
        if (node == root)
        {
            break;
        }
    }
    if (m_options.trailing_line_ending)
    {
        newline();
    }
}

}   //  namespace writer_util

CDocumentWriteResult write(const CBakedDocument& source, const CDocumentWriteOptions& options) noexcept
{
    CDocumentWriteResult result;
    if (!source.is_ready())
    {
        result.report.status = EDocumentWriteStatus::source_not_ready;
        return result;
    }
    if (((options.mode != EDocumentWriteMode::morphic) && (options.mode != EDocumentWriteMode::strict_json)) ||
        ((options.line_ending != EDocumentWriteLineEnding::lf) && (options.line_ending != EDocumentWriteLineEnding::crlf)))
    {
        result.report.status = EDocumentWriteStatus::invalid_options;
        return result;
    }

    writer_util::CWriter writer(source, options);
    return writer.run();
}

}   //  namespace document_writer
