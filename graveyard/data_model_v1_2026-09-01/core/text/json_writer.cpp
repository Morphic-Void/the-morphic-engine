
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    json_writer.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Aug 26

#include "text/json_writer.hpp"

#include <charconv>
#include <cstring>
#include <utility>

#include "data_model/baked_document.hpp"
#include "debug/macros.hpp"
#include "memory/memory_policies.hpp"

namespace json_writer
{
namespace writer_util
{

[[nodiscard]] static bool is_container(const EJsonNodeType type) noexcept
{
    return (type == EJsonNodeType::array) || (type == EJsonNodeType::object) ||  (type == EJsonNodeType::recovered_duplicate_array);
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
    //  Bound inputs must outlive the writer; output and progress belong to each run.
    CWriter(const CBakedDocument& source, const CJsonWriteOptions& options) noexcept : m_source(source), m_options(options) {}

    //  Start fresh, emit once, then transfer complete output or discard partial work.
    [[nodiscard]] CJsonWriteResult run() noexcept;

private:
    //  Status and byte storage: retain the first failure and bound every extension.
    [[nodiscard]] bool good() const noexcept { return m_report.succeeded(); }
    void fail(const EJsonWriteStatus status) noexcept { if (good()) m_report.status = status; }
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
    void quoted(const CStringView& value) noexcept;
    void key(const CStringView& value) noexcept;
    void hex_escape(std::uint32_t unit) noexcept;
    void integer(CBakedNodeIndex node) noexcept;
    void floating(CBakedNodeIndex node) noexcept;

    //  Iterative traversal, including automatic recovery wrappers and the envelope.
    void enter(CBakedNodeIndex node) noexcept;
    void leave(CBakedNodeIndex node) noexcept;
    void traverse() noexcept;

    //  Bound configuration; run() does not change or take ownership of either input.
    const CBakedDocument& m_source;
    const CJsonWriteOptions& m_options;

    //  Per-run ownership, reporting and formatting state.
    CByteBuffer m_output;
    CJsonWriteReport m_report;
    std::size_t m_depth{ 0u };
};

//  Run lifecycle: only a completed result may retain output or occurrence counts.
CJsonWriteResult CWriter::run() noexcept
{
    m_output.deallocate();
    m_report = {};
    m_report.status = EJsonWriteStatus::success;
    m_depth = 0u;

    traverse();
    if (good())
    {
        MV_ASSERT(m_depth == 0u);
        MV_ASSERT(m_output.size() == m_report.logical_text_byte_size);
        if (!m_output.append(1u))
        {
            fail(EJsonWriteStatus::allocation_failed);
        }
    }

    CJsonWriteResult result;
    if (good())
    {
        MV_ASSERT(m_output.size() == (m_report.logical_text_byte_size + 1u));
        MV_ASSERT((m_output.size() > m_report.logical_text_byte_size) && (m_output.data()[m_report.logical_text_byte_size] == 0u));
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
    MV_ASSERT(m_output.size() == m_report.logical_text_byte_size);
    MV_ASSERT(m_report.logical_text_byte_size <= (memory::k_byte_size_ceiling - 1u));
    if (count > (memory::k_byte_size_ceiling - 1u - m_report.logical_text_byte_size))
    {
        fail(EJsonWriteStatus::output_exceeds_engine_size_limit);
        return false;
    }
    if (!m_output.append(count, false))
    {
        fail(EJsonWriteStatus::allocation_failed);
        return false;
    }
    MV_ASSERT(m_output.size() == (m_report.logical_text_byte_size + count));
    return true;
}

void CWriter::append(const char* const bytes, const std::size_t count) noexcept
{
    if (grow(count))
    {
        if (count)
        {
            std::memcpy(m_output.data() + m_report.logical_text_byte_size, bytes, count);
        }
        m_report.logical_text_byte_size += count;
    }
}

//  Layout helpers share the same byte path as scalar and container output.
void CWriter::newline() noexcept
{
    if (m_options.line_ending == EJsonWriteLineEnding::crlf)
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
        fail(EJsonWriteStatus::output_exceeds_engine_size_limit);
        return;
    }
    const std::size_t count = m_depth * m_options.indent_width;
    if (grow(count))
    {
        if (count)
        {
            std::memset(m_output.data() + m_report.logical_text_byte_size, ' ', count);
        }
        m_report.logical_text_byte_size += count;
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
        fail(EJsonWriteStatus::internal_error);
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

void CWriter::quoted(const CStringView& value) noexcept
{
    if (!value.string())
    {
        fail(EJsonWriteStatus::source_contract_violation);
        return;
    }
    character('"');
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

void CWriter::key(const CStringView& value) noexcept
{
    quoted(value);
    character(':');
    if (m_options.pretty_print)
    {
        character(' ');
    }
}

void CWriter::integer(const CBakedNodeIndex node) noexcept
{
    CJsonIntegerMetadata metadata;
    if (!m_source.integer_metadata(node, metadata))
    {
        fail(EJsonWriteStatus::source_contract_violation);
        return;
    }
    std::uint64_t magnitude = 0u;
    bool negative = false;
    const bool signed_value = metadata.sign == EJsonIntegerSign::signed_value;
    if (signed_value)
    {
        std::int64_t value = 0;
        if (!m_source.integer_value(node, value))
        {
            fail(EJsonWriteStatus::source_contract_violation);
            return;
        }
        negative = value < 0;
        magnitude = negative ? (std::uint64_t{ 0u } - static_cast<std::uint64_t>(value)) : static_cast<std::uint64_t>(value);
    }
    else if (!m_source.unsigned_integer_value(node, magnitude))
    {
        fail(EJsonWriteStatus::source_contract_violation);
        return;
    }

    if (negative)
    {
        character('-');
    }
    else if (signed_value)
    {
        if (m_options.strict_json)
        {
            ++m_report.explicit_positive_signs_omitted;
        }
        else
        {
            character('+');
        }
    }
    int base = 10;
    if (metadata.notation != EJsonIntegerNotation::decimal)
    {
        if (m_options.strict_json)
        {
            ++m_report.non_decimal_integers_normalised;
        }
        else if (metadata.notation == EJsonIntegerNotation::hexadecimal)
        {
            base = 16;
            if (metadata.prefix == EJsonIntegerPrefix::alternate)
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
        fail(EJsonWriteStatus::internal_error);
        return;
    }
    append(digits, static_cast<std::size_t>(converted.ptr - digits));
}

void CWriter::floating(const CBakedNodeIndex node) noexcept
{
    double value = 0.0;
    if (!m_source.floating_point_value(node, value))
    {
        fail(EJsonWriteStatus::source_contract_violation);
        return;
    }
    if (value == 0.0)
    {
        if ((json_floating_point_bits(value) >> 63u) != 0u)
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
    const auto converted = std::to_chars(digits, digits + sizeof(digits), value, std::chars_format::general);
    if (converted.ec != std::errc{})
    {
        fail(EJsonWriteStatus::internal_error);
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
void CWriter::enter(const CBakedNodeIndex node) noexcept
{
    switch (m_source.node_type(node))
    {
        case (EJsonNodeType::null_value):
        {
            literal("null");
            break;
        }
        case (EJsonNodeType::boolean):
        {
            bool value = false;
            if (!m_source.boolean_value(node, value))
            {
                fail(EJsonWriteStatus::source_contract_violation);
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
        case (EJsonNodeType::integer):
        {
            integer(node);
            break;
        }
        case (EJsonNodeType::floating_point):
        {
            floating(node);
            break;
        }
        case (EJsonNodeType::string):
        {
            quoted(m_source.string_value(node));
            break;
        }
        case (EJsonNodeType::array):
        {
            begin('[');
            break;
        }
        case (EJsonNodeType::object):
        {
            begin('{');
            break;
        }
        case (EJsonNodeType::recovered_duplicate_array):
        {
            ++m_report.recovery_nodes_written;
            begin('{'); item(false); key(CStringView{ "$morphic.recovery" });
            begin('{'); item(false); key(CStringView{ "kind" }); quoted(CStringView{ "duplicate-member-array" });
            item(true); key(CStringView{ "values" }); begin('[');
            break;
        }
        default:
        {
            fail(EJsonWriteStatus::source_contract_violation);
            break;
        }
    }
}

void CWriter::leave(const CBakedNodeIndex node) noexcept
{
    const EJsonNodeType type = m_source.node_type(node);
    if (is_container(type))
    {
        end(type == EJsonNodeType::object ? '}' : ']', m_source.child_count(node) != 0u);
        if (type == EJsonNodeType::recovered_duplicate_array)
        {
            end('}', true);
            end('}', true);
        }
    }
}

void CWriter::traverse() noexcept
{
    const bool recovery = m_source.contains_recovered_duplicate_arrays();
    if (recovery)
    {
        m_report.diagnostic_envelope_written = true;
        begin('{'); item(false); key(CStringView{ "$morphic.recovery" });
        begin('{'); item(false); key(CStringView{ "format" }); quoted(CStringView{ "diagnostic-document" });
        item(true); key(CStringView{ "version" }); character('1');
        item(true); key(CStringView{ "document" });
    }

    const CBakedNodeIndex root = m_source.root();
    CBakedNodeIndex node = root;
    std::uint32_t visited = 0u;
    while (good())
    {
        if (++visited > m_source.node_count())
        {
            fail(EJsonWriteStatus::source_contract_violation);
            break;
        }
        if (node != root)
        {
            item(m_source.previous_sibling(node).is_valid());
            if (m_source.node_type(m_source.parent(node)) == EJsonNodeType::object)
            {
                key(m_source.property_name(m_source.name_in_parent(node)));
            }
        }
        enter(node);
        if (!good())
        {
            break;
        }
        const CBakedNodeIndex child = m_source.first_child(node);
        if (child.is_valid())
        {
            node = child;
            continue;
        }
        leave(node);
        while (good() && (node != root))
        {
            const CBakedNodeIndex next = m_source.next_sibling(node);
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
    if (good() && (visited != m_source.node_count()))
    {
        fail(EJsonWriteStatus::source_contract_violation);
    }
    if (recovery)
    {
        end('}', true);
        end('}', true);
    }
    if (m_options.trailing_line_ending)
    {
        newline();
    }
}

}   //  namespace writer_util

CJsonWriteResult write(const CBakedDocument& source, const CJsonWriteOptions& options) noexcept
{
    CJsonWriteResult result;
    if (!source.is_ready())
    {
        result.report.status = EJsonWriteStatus::source_not_ready;
        return result;
    }
    if (!source.root().is_valid())
    {
        result.report.status = EJsonWriteStatus::source_contract_violation;
        return result;
    }
    if ((options.line_ending != EJsonWriteLineEnding::lf) && (options.line_ending != EJsonWriteLineEnding::crlf))
    {
        return result;
    }

    writer_util::CWriter writer(source, options);
    return writer.run();
}

}   //  namespace json_writer
