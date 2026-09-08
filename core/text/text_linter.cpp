
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    text_linter.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No file I/O or logging.

#include "text/text_linter.hpp"

#include <cstdint>
#include <limits>

#include "external/SuiteUTF/include/unicode_classification.h"
#include "external/SuiteUTF/include/utf_toolkit.h"
#include "memory/memory_policies.hpp"

namespace text_linter
{

using unicode::unicode_t;
using unicode::utf::utf_text;
using unicode::utf::toolkit::IUTFTK;
using unicode::utf::toolkit::UTF_SUB_TYPE;
using unicode::utf::toolkit::cp_errors;

struct CLineState
{
    std::size_t line_1_based = 1u;
    std::size_t column_1_based = 1u;
    std::size_t byte_offset_in_line_0_based = 0u;
    std::size_t line_start_byte_offset_in_file_0_based = 0u;
    std::size_t current_code_points = 0u;
    std::size_t current_bytes = 0u;
    std::size_t current_likely_printable = 0u;
    bool current_is_whitespace_only = true;
};

struct CDecodedUnit
{
    unicode_t value = 0;
    std::uint32_t bytes = 0u;
    cp_errors errors;
};

static constexpr std::uint32_t k_utf8_bom_bytes = 3u;

static constexpr std::uint32_t k_rejected_utf8_forms =
    static_cast<std::uint32_t>(cp_errors::bits::IrregularForm) | static_cast<std::uint32_t>(cp_errors::bits::OverlongUTF8) |
    static_cast<std::uint32_t>(cp_errors::bits::SurrogatePair) | static_cast<std::uint32_t>(cp_errors::bits::ExtendedUTF8) |
    static_cast<std::uint32_t>(cp_errors::bits::HighSurrogate) | static_cast<std::uint32_t>(cp_errors::bits::LowSurrogate);

static constexpr std::uint32_t k_cp1252_common_punctuation =
    (1u << (0x91u - 0x80u)) | (1u << (0x92u - 0x80u)) | (1u << (0x93u - 0x80u)) |
    (1u << (0x94u - 0x80u)) | (1u << (0x96u - 0x80u)) | (1u << (0x97u - 0x80u));

static constexpr std::uint32_t k_cp1252_undefined_bytes =
    (1u << (0x81u - 0x80u)) | (1u << (0x8du - 0x80u)) | (1u << (0x8fu - 0x80u)) |
    (1u << (0x90u - 0x80u)) | (1u << (0x9du - 0x80u));

static constexpr bool has_bit(const std::uint32_t mask, const ETextLineEnding value) noexcept { return (mask & text_line_ending_bit(value)) != 0u; }

static void update_maximums(CTextLineMetrics& metrics, const CLineState& state) noexcept
{
    if (state.current_code_points > metrics.maximum_line_code_points)
    {
        metrics.maximum_line_code_points = state.current_code_points;
    }
    if (state.current_bytes > metrics.maximum_line_bytes)
    {
        metrics.maximum_line_bytes = state.current_bytes;
    }
    if (state.current_likely_printable > metrics.maximum_line_likely_printable_code_points)
    {
        metrics.maximum_line_likely_printable_code_points = state.current_likely_printable;
    }
}

static bool is_likely_printable(const unicode_t value) noexcept
{
    return unicode::isCharacter(value) && !unicode::isCC(value) && !unicode::isBOM(value) && !unicode::isSpecial(value);
}

static bool is_white_space(const unicode_t value) noexcept
{   //  check for breaking and non-breaking white space
    return unicode::isBreakingWhite(value) || (value == 0x00a0u) || (value == 0x2007u) || (value == 0x202fu);
}

static void finish_line(CTextLineMetrics& metrics, const CLineState& state) noexcept
{
    update_maximums(metrics, state);
    if (state.current_code_points == 0u)
    {
        ++metrics.empty_line_count;
    }
    else if (state.current_is_whitespace_only)
    {
        ++metrics.whitespace_only_line_count;
    }
}

static void consume_content(CLineState& state, CTextLineMetrics& metrics, const unicode_t value, const std::size_t source_bytes) noexcept
{
    ++state.current_code_points;
    state.current_bytes += source_bytes;
    ++metrics.total_code_points;
    if (is_likely_printable(value))
    {
        ++state.current_likely_printable;
        ++metrics.total_likely_printable_code_points;
    }
    state.current_is_whitespace_only = state.current_is_whitespace_only && is_white_space(value);
    ++state.column_1_based;
    state.byte_offset_in_line_0_based += source_bytes;
    update_maximums(metrics, state);
}

static void consume_line_break(CLineState& state, CTextLineMetrics& metrics, const std::size_t source_bytes, const std::size_t source_code_points) noexcept
{
    finish_line(metrics, state);
    metrics.total_code_points += source_code_points;
    ++metrics.line_count;
    ++state.line_1_based;
    state.column_1_based = 1u;
    state.line_start_byte_offset_in_file_0_based += state.byte_offset_in_line_0_based + source_bytes;
    state.byte_offset_in_line_0_based = 0u;
    state.current_code_points = state.current_bytes = state.current_likely_printable = 0u;
    state.current_is_whitespace_only = true;
}

static CTextLintLocation current_location(const CLineState& state, const std::size_t file_offset) noexcept
{
    CTextLintLocation location;
    location.line_1_based = state.line_1_based;
    location.code_point_column_1_based = state.column_1_based;
    location.byte_offset_in_line_0_based = state.byte_offset_in_line_0_based;
    location.byte_offset_in_file_0_based = file_offset;
    location.line_start_byte_offset_in_file_0_based = state.line_start_byte_offset_in_file_0_based;
    return location;
}

static void set_first_failure(CTextLintReport& report, const std::uint32_t errors, const CLineState& state, const std::size_t file_offset) noexcept
{
    if (!report.first_failure.present)
    {
        report.first_failure.present = true;
        report.first_failure.suite_utf_cp_errors = errors;
        report.first_failure.location = current_location(state, file_offset);
    }
}

static CDecodedUnit decode_java_utf8(const std::uint8_t* const data, const std::uint32_t length, const std::uint32_t offset) noexcept
{
    CDecodedUnit unit;
    utf_text text{ length, offset, const_cast<std::uint8_t*>(data) };
    unit.errors = IUTFTK::getHandler(UTF_SUB_TYPE::JUTF8st).get(text, unit.value, unit.bytes);
    return unit;
}

static bool is_java_modified_nul(const std::uint8_t* const data, const std::uint32_t length, const std::uint32_t offset) noexcept
{
    return ((length - offset) >= 2u) && (data[offset] == 0xc0u) && (data[offset + 1u] == 0x80u);
}

static CDecodedUnit decode_accepted_java_utf8(const std::uint8_t* const data, const std::uint32_t length, const std::uint32_t offset) noexcept
{
    //  JUTF8st remains the diagnostic decoder. Its strict irregular-form
    //  handling is intentionally overridden only for Java's exact C0 80 NULL.
    CDecodedUnit unit = decode_java_utf8(data, length, offset);
    if (is_java_modified_nul(data, length, offset))
    {
        unit.value = 0;
        unit.bytes = 2u;
        unit.errors = cp_errors::bits::ModifiedUTF8;
    }
    return unit;
}

static bool is_rejected_utf8_form(const cp_errors errors) noexcept
{
    return errors.failed() || errors.any(k_rejected_utf8_forms);
}

static ETextLineEnding identify_line_ending(const CDecodedUnit& current, const CDecodedUnit* const next) noexcept
{
    switch (current.value)
    {
        case (0x000au): return (next != nullptr) && (next->value == 0x000du) ? ETextLineEnding::lfcr : ETextLineEnding::lf;
        case (0x000bu): return ETextLineEnding::vt;
        case (0x000cu): return ETextLineEnding::ff;
        case (0x000du): return (next != nullptr) && (next->value == 0x000au) ? ETextLineEnding::crlf : ETextLineEnding::cr;
        case (0x0085u): return ETextLineEnding::nel;
        case (0x2028u): return ETextLineEnding::ls;
        case (0x2029u): return ETextLineEnding::ps;
        default:        return ETextLineEnding::none;
    }
}

static bool append_bytes(CTextLintResult& result, const std::uint8_t* const bytes, const std::size_t count) noexcept
{
    if (count > (memory::k_byte_size_ceiling - result.output.size()))
    {
        result.report.output_exceeds_engine_size_limit = true;
        return false;
    }
    if (!result.output.append(bytes, count))
    {
        result.report.allocation_failed = true;
        return false;
    }
    return true;
}

static bool append_utf8(CTextLintResult& result, const unicode_t value, std::size_t& bytes_written) noexcept
{
    bytes_written = 0u;
    std::uint8_t encoded[4u]{};
    const std::uint32_t point = static_cast<std::uint32_t>(value);
    if (point <= 0x7fu)
    {
        encoded[0] = static_cast<std::uint8_t>(point);
        bytes_written = 1u;
    }
    else if (point <= 0x7ffu)
    {
        encoded[0] = static_cast<std::uint8_t>(0xc0u | (point >> 6));
        encoded[1] = static_cast<std::uint8_t>(0x80u | (point & 0x3fu));
        bytes_written = 2u;
    }
    else if (point <= 0xffffu)
    {
        encoded[0] = static_cast<std::uint8_t>(0xe0u | (point >> 12));
        encoded[1] = static_cast<std::uint8_t>(0x80u | ((point >> 6) & 0x3fu));
        encoded[2] = static_cast<std::uint8_t>(0x80u | (point & 0x3fu));
        bytes_written = 3u;
    }
    else if (point <= 0x10ffffu)
    {
        encoded[0] = static_cast<std::uint8_t>(0xf0u | (point >> 18));
        encoded[1] = static_cast<std::uint8_t>(0x80u | ((point >> 12) & 0x3fu));
        encoded[2] = static_cast<std::uint8_t>(0x80u | ((point >> 6) & 0x3fu));
        encoded[3] = static_cast<std::uint8_t>(0x80u | (point & 0x3fu));
        bytes_written = 4u;
    }
    return (bytes_written != 0u) && append_bytes(result, encoded, bytes_written);
}

static constexpr bool is_cp1252_c1_bit_set(const std::uint32_t mask, const std::uint8_t value) noexcept
{
    return (value >= 0x80u) && (value <= 0x9fu) && ((mask & (1u << (value - 0x80u))) != 0u);
}

static void discard_failed_output(CTextLintResult& result) noexcept
{
    result.output.deallocate();
    result.report.logical_text_byte_size = 0u;
    result.report.output_is_pure_ascii = true;
    result.report.output_encoding = ETextLintEncoding::none;
}

static bool finish_output(CTextLintResult& result) noexcept
{
    const std::uint8_t terminator = 0u;
    if (!append_bytes(result, &terminator, 1u))
    {
        discard_failed_output(result);
        return false;
    }
    result.report.logical_text_byte_size = result.output.size() - 1u;
    result.report.output_is_pure_ascii = true;
    for (std::size_t index = 0u; index < result.report.logical_text_byte_size; ++index)
    {
        if (result.output.data()[index] > 0x7fu)
        {
            result.report.output_is_pure_ascii = false;
            break;
        }
    }
    result.report.output_encoding = ETextLintEncoding::utf8;
    result.report.success = true;
    return true;
}

static bool reserve_output(CTextLintResult& result, const std::size_t capacity) noexcept
{
    if (capacity > memory::k_byte_size_ceiling)
    {
        result.report.output_exceeds_engine_size_limit = true;
        return false;
    }
    if (!result.output.reserve(capacity))
    {
        result.report.allocation_failed = true;
        return false;
    }
    return true;
}

static void set_cp1252_evidence(CTextLintReport& report, const std::uint8_t* const data, const std::size_t length) noexcept
{
    report.cp1252_positive_evidence = text_lint_evidence_bit(ETextLintEvidence::strict_utf8_failure);
    for (std::size_t index = 0u; index < length; ++index)
    {
        const std::uint8_t value = data[index];
        if (value >= 0x80u)
        {
            if (value <= 0x9fu)
            {
                if (is_cp1252_c1_bit_set(k_cp1252_undefined_bytes, value))
                {
                    report.cp1252_counter_evidence |= text_lint_evidence_bit(ETextLintEvidence::cp1252_undefined_byte);
                }
                else if (is_cp1252_c1_bit_set(k_cp1252_common_punctuation, value))
                {
                    report.cp1252_positive_evidence |= text_lint_evidence_bit(ETextLintEvidence::cp1252_common_punctuation);
                }
                else
                {
                    report.cp1252_positive_evidence |= text_lint_evidence_bit(ETextLintEvidence::cp1252_defined_c1);
                }
            }
            else
            {
                report.cp1252_positive_evidence |= text_lint_evidence_bit(ETextLintEvidence::cp1252_printable);
            }
        }
    }
    if (report.cp1252_counter_evidence != 0u)
    {
        report.cp1252_confidence = ECP1252Confidence::low;
    }
    else if ((report.cp1252_positive_evidence & text_lint_evidence_bit(ETextLintEvidence::cp1252_common_punctuation)) != 0u)
    {
        report.cp1252_confidence = ECP1252Confidence::likely;
    }
    else if ((report.cp1252_positive_evidence & text_lint_evidence_bit(ETextLintEvidence::cp1252_defined_c1)) != 0u)
    {
        report.cp1252_confidence = ECP1252Confidence::moderate;
    }
    else
    {
        report.cp1252_confidence = ECP1252Confidence::low;
    }
}

static bool is_compound_le(const ETextLineEnding le) noexcept
{
    return (text_line_ending_bit(le) & k_compound_line_endings) != 0u;
}

CTextLintResult lint(const CByteConstView& input, const std::uint32_t line_ending_flags) noexcept
{
    CTextLintResult result;
    CTextLintReport& report = result.report;
    report.input_byte_size = input.size();
    report.input_is_empty = input.is_empty();
    if (!input.is_valid())
    {
        report.input_view_invalid = true;
        return result;
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        report.input_too_large_for_suite_utf = true;
        return result;
    }

    const std::uint8_t* const data = input.data();
    std::size_t payload_length = input.size();
    while ((payload_length != 0u) && (data[payload_length - 1u] == 0u))
    {
        --payload_length;
        ++report.stripped_terminal_zero_count;
    }
    report.payload_input_byte_size = payload_length;
    for (std::size_t index = 0u; index < payload_length; ++index)
    {
        if (data[index] == 0u)
        {
            ++report.embedded_nul_count;
        }
        if (data[index] > 0x7fu)
        {
            report.input_is_pure_ascii = false;
        }
    }

    const std::uint32_t source_length = static_cast<std::uint32_t>(payload_length);
    std::uint32_t identified_bom_bytes = 0u;
    if (source_length != 0u)
    {
        (void)unicode::utf::identifyUTF(data, source_length, identified_bom_bytes);
        report.leading_bom_detected = (identified_bom_bytes != 0u);
        report.leading_bom_byte_count = identified_bom_bytes;
    }
    std::uint32_t source_offset = 0u;
    if ((identified_bom_bytes == k_utf8_bom_bytes) && (source_length >= k_utf8_bom_bytes) && (data[0] == 0xefu) && (data[1] == 0xbbu) && (data[2] == 0xbfu))
    {
        source_offset = k_utf8_bom_bytes;
        report.leading_utf8_bom_stripped = true;
    }

    if (payload_length >= memory::k_byte_size_ceiling)
    {
        report.output_exceeds_engine_size_limit = true;
        return result;
    }
    if (!reserve_output(result, payload_length + 1u))
    {
        return result;
    }
    report.input_metrics.line_count = report.output_metrics.line_count = 1u;
    CLineState input_line;
    CLineState output_line;
    input_line.line_start_byte_offset_in_file_0_based = source_offset;

    bool strict_ok = true;
    std::uint32_t offset = source_offset;
    while (offset < source_length)
    {
        const CDecodedUnit current = decode_accepted_java_utf8(data, source_length, offset);
        if ((current.bytes == 0u) || is_rejected_utf8_form(current.errors))
        {
            set_first_failure(report, current.errors.raw(), input_line, offset);
            strict_ok = false;
            break;
        }
        CDecodedUnit next;
        const CDecodedUnit* next_ptr = nullptr;
        if ((offset + current.bytes) < source_length)
        {
            next = decode_accepted_java_utf8(data, source_length, offset + current.bytes);
            //  Do not fail while peeking: consuming the current code point
            //  first preserves the exact input line/column of a later failure.
            if ((next.bytes != 0u) && !is_rejected_utf8_form(next.errors))
            {
                next_ptr = &next;
            }
        }
        const ETextLineEnding ending = identify_line_ending(current, next_ptr);
        const bool compound = is_compound_le(ending);
        const std::uint32_t consumed_bytes = current.bytes + (compound ? next.bytes : 0u);
        if (ending != ETextLineEnding::none)
        {
            report.encountered_line_endings |= text_line_ending_bit(ending);
        }
        if ((ending != ETextLineEnding::none) && has_bit(line_ending_flags, ending))
        {
            const std::uint8_t lf = 0x0au;
            if (!append_bytes(result, &lf, 1u))
            {
                discard_failed_output(result);
                return result;
            }
            report.normalised_line_endings |= text_line_ending_bit(ending);
            consume_line_break(input_line, report.input_metrics, consumed_bytes, compound ? 2u : 1u);
            consume_line_break(output_line, report.output_metrics, 1u, 1u);
        }
        else
        {
            const bool modified_nul = current.errors.any(cp_errors::bits::ModifiedUTF8);
            const std::uint8_t nul = 0u;
            const std::size_t emitted_bytes = modified_nul ? 1u : current.bytes;
            if (!append_bytes(result, (modified_nul ? &nul : (data + offset)), emitted_bytes))
            {
                discard_failed_output(result);
                return result;
            }
            consume_content(input_line, report.input_metrics, current.value, current.bytes);
            consume_content(output_line, report.output_metrics, current.value, emitted_bytes);
            if (compound)
            {
                if (!append_bytes(result, data + offset + current.bytes, next.bytes))
                {
                    discard_failed_output(result);
                    return result;
                }
                consume_content(input_line, report.input_metrics, next.value, next.bytes);
                consume_content(output_line, report.output_metrics, next.value, next.bytes);
            }
        }
        if (current.errors.any(cp_errors::bits::ModifiedUTF8))
        {
            ++report.modified_utf8_nul_count;
        }
        if (compound && next.errors.any(cp_errors::bits::ModifiedUTF8))
        {
            ++report.modified_utf8_nul_count;
        }
        offset += consumed_bytes;
    }

    if (strict_ok)
    {
        finish_line(report.input_metrics, input_line);
        finish_line(report.output_metrics, output_line);
        (void)finish_output(result);
        return result;
    }

    //  A recognized BOM announces another Unicode encoding and forbids CP1252 recovery.
    if (report.leading_bom_detected)
    {
        discard_failed_output(result);
        return result;
    }

    result.output.deallocate();
    report.input_metrics = CTextLineMetrics{ 1u };
    report.output_metrics = CTextLineMetrics{ 1u };
    input_line = CLineState{};
    output_line = CLineState{};
    report.encountered_line_endings = report.normalised_line_endings = 0u;
    report.modified_utf8_nul_count = 0u;
    set_cp1252_evidence(report, data, payload_length);
    if (payload_length >= memory::k_byte_size_ceiling)
    {
        report.output_exceeds_engine_size_limit = true;
        discard_failed_output(result);
        return result;
    }
    if (!reserve_output(result, payload_length + 1u))
    {
        discard_failed_output(result);
        return result;
    }

    const IUTFTK& cp1252 = IUTFTK::getHandler(UTF_SUB_TYPE::CP1252st);
    offset = 0u;
    while (offset < source_length)
    {
        utf_text text{ source_length, offset, const_cast<std::uint8_t*>(data) };
        unicode_t value = 0;
        std::uint32_t decoded_bytes = 0u;
        const cp_errors errors = cp1252.get(text, value, decoded_bytes);
        if (errors.failed() || (decoded_bytes != 1u))
        {
            value = 0xfffdu;
            decoded_bytes = 1u;
            ++report.cp1252_replacement_character_count;
        }

        unicode_t next_value = 0;
        std::uint32_t next_bytes = 0u;
        if ((offset + decoded_bytes) < source_length)
        {
            utf_text next_text{ source_length, offset + decoded_bytes, const_cast<std::uint8_t*>(data) };
            const cp_errors next_errors = cp1252.get(next_text, next_value, next_bytes);
            if (next_errors.failed() || (next_bytes != 1u))
            {
                next_value = 0xfffdu;
                next_bytes = 1u;
            }
        }
        const CDecodedUnit current{ value, decoded_bytes, cp_errors{} };
        const CDecodedUnit next{ next_value, next_bytes, cp_errors{} };
        const ETextLineEnding ending = identify_line_ending(current, (next_bytes != 0u) ? &next : nullptr);
        const bool compound = is_compound_le(ending);
        const std::uint32_t consumed_bytes = decoded_bytes + (compound ? next_bytes : 0u);
        if (ending != ETextLineEnding::none)
        {
            report.encountered_line_endings |= text_line_ending_bit(ending);
        }
        if ((ending != ETextLineEnding::none) && has_bit(line_ending_flags, ending))
        {
            const std::uint8_t lf = 0x0au;
            if (!append_bytes(result, &lf, 1u))
            {
                discard_failed_output(result);
                return result;
            }
            report.normalised_line_endings |= text_line_ending_bit(ending);
            consume_line_break(input_line, report.input_metrics, consumed_bytes, compound ? 2u : 1u);
            consume_line_break(output_line, report.output_metrics, 1u, 1u);
        }
        else
        {
            std::size_t emitted = 0u;
            if (!append_utf8(result, value, emitted))
            {
                discard_failed_output(result);
                return result;
            }
            consume_content(input_line, report.input_metrics, value, decoded_bytes);
            consume_content(output_line, report.output_metrics, value, emitted);
            if (compound)
            {
                if (!append_utf8(result, next_value, emitted))
                {
                    discard_failed_output(result);
                    return result;
                }
                consume_content(input_line, report.input_metrics, next_value, next_bytes);
                consume_content(output_line, report.output_metrics, next_value, emitted);
            }
        }
        offset += consumed_bytes;
    }
    finish_line(report.input_metrics, input_line);
    finish_line(report.output_metrics, output_line);
    if (finish_output(result))
    {
        report.recovered_as_cp1252 = true;
    }
    return result;
}

}   //  namespace text_linter
