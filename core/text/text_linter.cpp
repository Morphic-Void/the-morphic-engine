
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
#include <utility>

#include "external/SuiteUTF/include/unicode_classification.h"
#include "external/SuiteUTF/include/utf_toolkit.h"
#include "memory/memory_policies.hpp"

namespace text_linter
{

//  Internal implementation names; pure helpers retain file-local linkage.
namespace lint_util
{

using unicode::unicode_t;
using unicode::utf::utf_text;
using unicode::utf::toolkit::IUTFTK;
using unicode::utf::toolkit::UTF_SUB_TYPE;
using unicode::utf::toolkit::cp_errors;

struct CLineState
{
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

struct CSourceCursor
{
    const std::uint8_t* data = nullptr;
    std::uint32_t length = 0u;
    std::uint32_t offset = 0u;

    [[nodiscard]] CDecodedUnit decode(const bool cp1252) const noexcept;
};

struct CAttempt
{
    CLineState input_line;
    CLineState output_line;
    CTextLocation cursor{ true, 1u, 1u };
};

class CLinter
{
public:
    //  One operation per instance; run transfers the owned result to its caller.
    explicit CLinter(const std::uint32_t line_flags) noexcept : m_line_flags(line_flags) {}
    [[nodiscard]] CTextLintResult run(const std::uint8_t* const data, const std::size_t size) noexcept;

private:
    [[nodiscard]] bool append_bytes(const std::uint8_t* const bytes, const std::size_t count) noexcept;
    [[nodiscard]] bool append_utf8(const unicode_t value, std::size_t& bytes_written) noexcept;
    void discard_failed_output() noexcept;
    [[nodiscard]] bool finish_output() noexcept;
    [[nodiscard]] bool reserve_output(const std::size_t capacity) noexcept;
    void set_cp1252_evidence() noexcept;
    void set_failure(const ETextLintFailure reason, const CTextLocation& location = {}, const std::uint32_t errors = 0u) noexcept;
    void resource_failure() noexcept;
    void observe_utf8(const CDecodedUnit& unit) noexcept;
    [[nodiscard]] bool emit_content(const CDecodedUnit& unit) noexcept;
    [[nodiscard]] bool decode_attempt(const bool cp1252) noexcept;

    CTextLintResult m_result;
    CSourceCursor m_source;
    CAttempt m_attempt;
    const std::uint32_t m_line_flags;
};

static constexpr std::uint32_t k_utf8_bom_bytes = 3u;

static constexpr std::uint32_t k_rejected_utf8_forms =
    static_cast<std::uint32_t>(cp_errors::bits::IrregularForm) |
    static_cast<std::uint32_t>(cp_errors::bits::OverlongUTF8) | static_cast<std::uint32_t>(cp_errors::bits::ExtendedUTF8) |
    static_cast<std::uint32_t>(cp_errors::bits::LowSurrogate) | static_cast<std::uint32_t>(cp_errors::bits::HighSurrogate);

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
    update_maximums(metrics, state);
}

static void consume_line_break(CLineState& state, CTextLineMetrics& metrics, const std::size_t source_code_points) noexcept
{
    finish_line(metrics, state);
    metrics.total_code_points += source_code_points;
    ++metrics.line_count;
    state.current_code_points = state.current_bytes = state.current_likely_printable = 0u;
    state.current_is_whitespace_only = true;
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

static constexpr bool is_cp1252_c1_bit_set(const std::uint32_t mask, const std::uint8_t value) noexcept
{
    return (value >= 0x80u) && (value <= 0x9fu) && ((mask & (1u << (value - 0x80u))) != 0u);
}

static bool is_compound_le(const ETextLineEnding le) noexcept
{
    return (text_line_ending_bit(le) & k_compound_line_endings) != 0u;
}

static void advance_cursor(CTextLocation& cursor, const unicode_t value) noexcept
{
    if (value == 0x0au)
    {
        ++cursor.line_1_based;
        cursor.code_point_column_1_based = 1u;
    }
    else
    {
        ++cursor.code_point_column_1_based;
    }
}

static bool rejected(const CDecodedUnit& unit, const bool cp1252) noexcept
{
    if (cp1252)
    {
        return unit.errors.failed() || (unit.bytes != 1u);
    }
    return (unit.bytes == 0u) || is_rejected_utf8_form(unit.errors);
}

CDecodedUnit CSourceCursor::decode(const bool cp1252) const noexcept
{
    CDecodedUnit unit;
    utf_text text{ length, offset, const_cast<std::uint8_t*>(data) };
    const UTF_SUB_TYPE type = cp1252 ? UTF_SUB_TYPE::CP1252st : UTF_SUB_TYPE::JCESU8st;
    unit.errors = IUTFTK::getHandler(type).get(text, unit.value, unit.bytes);
    //  JCESU8st remains the diagnostic decoder. Its strict irregular-form
    //  handling is intentionally overridden only for Java's exact C0 80 NULL.
    if (!cp1252 && ((length - offset) >= 2u) && (data[offset] == 0xc0u) && (data[offset + 1u] == 0x80u))
    {
        unit.value = 0;
        unit.bytes = 2u;
        unit.errors = cp_errors::bits::ModifiedUTF8;
    }
    return unit;
}

bool CLinter::append_bytes(const std::uint8_t* const bytes, const std::size_t count) noexcept
{
    if (count > (memory::k_byte_size_ceiling - m_result.output.size()))
    {
        m_result.report.output_exceeds_engine_size_limit = true;
        return false;
    }
    if (!m_result.output.append(bytes, count))
    {
        m_result.report.allocation_failed = true;
        return false;
    }
    return true;
}

bool CLinter::append_utf8(const unicode_t value, std::size_t& bytes_written) noexcept
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
    return (bytes_written != 0u) && append_bytes(encoded, bytes_written);
}

void CLinter::discard_failed_output() noexcept
{
    m_result.output.deallocate();
    m_result.report.logical_text_byte_size = 0u;
    m_result.report.output_is_pure_ascii = true;
    m_result.report.output_encoding = ETextLintEncoding::none;
}

bool CLinter::finish_output() noexcept
{
    const std::uint8_t terminator = 0u;
    if (!append_bytes(&terminator, 1u))
    {
        discard_failed_output();
        return false;
    }
    m_result.report.logical_text_byte_size = m_result.output.size() - 1u;
    m_result.report.output_is_pure_ascii = true;
    for (std::size_t index = 0u; index < m_result.report.logical_text_byte_size; ++index)
    {
        if (m_result.output.data()[index] > 0x7fu)
        {
            m_result.report.output_is_pure_ascii = false;
            break;
        }
    }
    m_result.report.output_encoding = ETextLintEncoding::utf8;
    m_result.report.success = true;
    return true;
}

bool CLinter::reserve_output(const std::size_t capacity) noexcept
{
    if (capacity > memory::k_byte_size_ceiling)
    {
        m_result.report.output_exceeds_engine_size_limit = true;
        return false;
    }
    if (!m_result.output.reserve(capacity))
    {
        m_result.report.allocation_failed = true;
        return false;
    }
    return true;
}

void CLinter::set_cp1252_evidence() noexcept
{
    CTextLintReport& report = m_result.report;
    report.cp1252_positive_evidence = text_lint_evidence_bit(ETextLintEvidence::strict_utf8_failure);
    for (std::size_t index = 0u; index < m_source.length; ++index)
    {
        const std::uint8_t value = m_source.data[index];
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

void CLinter::set_failure(const ETextLintFailure reason, const CTextLocation& location, const std::uint32_t errors) noexcept
{
    CTextLintFailure& failure = m_result.report.first_failure;
    failure.present = true;
    failure.before_output = m_result.report.output_metrics.total_code_points == 0u;
    failure.reason = reason;
    failure.location = location;
    failure.suite_utf_cp_errors = errors;
    discard_failed_output();
}

void CLinter::resource_failure() noexcept
{
    const ETextLintFailure reason = m_result.report.allocation_failed ?
        ETextLintFailure::allocation_failed : ETextLintFailure::output_limit;
    set_failure(reason, m_attempt.cursor);
}

void CLinter::observe_utf8(const CDecodedUnit& unit) noexcept
{
    CTextLintReport& report = m_result.report;
    if (unit.errors.any(cp_errors::bits::ModifiedUTF8))
    {
        report.source_findings |= text_source_finding_bit(ETextSourceFinding::modified_nul);
    }
    else if (unit.errors.any(cp_errors::bits::SurrogatePair))
    {
        report.source_findings |= text_source_finding_bit(ETextSourceFinding::cesu8_pair);
    }
    else if (unit.value > 0x7fu)
    {
        report.source_findings |= text_source_finding_bit(ETextSourceFinding::non_ascii_utf8);
    }
}

bool CLinter::emit_content(const CDecodedUnit& unit) noexcept
{
    std::size_t emitted = 0u;
    if (!append_utf8(unit.value, emitted))
    {
        resource_failure();
        return false;
    }
    consume_content(m_attempt.input_line, m_result.report.input_metrics, unit.value, unit.bytes);
    consume_content(m_attempt.output_line, m_result.report.output_metrics, unit.value, emitted);
    advance_cursor(m_attempt.cursor, unit.value);
    return true;
}

bool CLinter::decode_attempt(const bool cp1252) noexcept
{
    CTextLintReport& report = m_result.report;
    report.input_metrics.line_count = report.output_metrics.line_count = 1u;
    if (!reserve_output(static_cast<std::size_t>(m_source.length) + 1u))
    {
        resource_failure();
        return false;
    }
    while (m_source.offset < m_source.length)
    {
        const CDecodedUnit current = m_source.decode(cp1252);
        const bool undefined = cp1252 && is_cp1252_c1_bit_set(k_cp1252_undefined_bytes, m_source.data[m_source.offset]);
        if (undefined || rejected(current, cp1252))
        {
            ETextLintFailure reason = ETextLintFailure::utf8_decode;
            if (cp1252)
            {
                reason = ETextLintFailure::cp1252_decode;
                if (undefined)
                {
                    reason = ETextLintFailure::undefined_cp1252_byte;
                    report.source_findings |= text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte);
                }
            }
            else
            {
                report.utf8_attempt_errors = current.errors.raw();
                report.source_findings |= text_source_finding_bit(ETextSourceFinding::utf8_attempt_failed);
            }
            set_failure(reason, m_attempt.cursor, current.errors.raw());
            return false;
        }
        if (!cp1252)
        {
            observe_utf8(current);
        }
        CDecodedUnit next;
        const CDecodedUnit* next_ptr = nullptr;
        if (((current.value == 0x0au) || (current.value == 0x0du)) && ((m_source.offset + current.bytes) < m_source.length))
        {
            CSourceCursor next_source = m_source;
            next_source.offset += current.bytes;
            next = next_source.decode(cp1252);
            //  Peeking never publishes findings or fails before current emission.
            if (!rejected(next, cp1252))
            {
                next_ptr = &next;
            }
        }
        const ETextLineEnding ending = identify_line_ending(current, next_ptr);
        const bool compound = is_compound_le(ending);
        const std::uint32_t consumed = current.bytes + (compound ? next.bytes : 0u);
        report.encountered_line_endings |= text_line_ending_bit(ending);
        if ((ending != ETextLineEnding::none) && has_bit(m_line_flags, ending))
        {
            const std::uint8_t lf = 0x0au;
            if (!append_bytes(&lf, 1u))
            {
                resource_failure();
                return false;
            }
            report.normalised_line_endings |= text_line_ending_bit(ending);
            consume_line_break(m_attempt.input_line, report.input_metrics, compound ? 2u : 1u);
            consume_line_break(m_attempt.output_line, report.output_metrics, 1u);
            advance_cursor(m_attempt.cursor, lf);
        }
        else if (!emit_content(current) || (compound && !emit_content(next)))
        {
            return false;
        }
        if (!cp1252 && current.errors.any(cp_errors::bits::ModifiedUTF8))
        {
            ++report.modified_utf8_nul_count;
        }
        if (!cp1252 && current.errors.any(cp_errors::bits::SurrogatePair))
        {
            ++report.cesu8_pair_count;
        }
        m_source.offset += consumed;
    }
    if (!finish_output())
    {
        resource_failure();
        return false;
    }
    return true;
}

CTextLintResult CLinter::run(const std::uint8_t* const data, const std::size_t size) noexcept
{
    CTextLintReport& report = m_result.report;
    report.input_byte_size = size;
    report.input_is_empty = size == 0u;
    if (data == nullptr)
    {
        report.input_view_invalid = true;
        set_failure(ETextLintFailure::invalid_input_view);
        return std::move(m_result);
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        report.input_too_large_for_suite_utf = true;
        set_failure(ETextLintFailure::input_limit);
        return std::move(m_result);
    }
    std::size_t payload_length = size;
    while ((payload_length != 0u) && (data[payload_length - 1u] == 0u))
    {
        --payload_length;
        ++report.stripped_terminal_zero_count;
    }
    if (report.stripped_terminal_zero_count != 0u)
    {
        report.source_findings |= text_source_finding_bit(ETextSourceFinding::stripped_terminal_zeros);
    }
    report.payload_input_byte_size = payload_length;
    for (std::size_t index = 0u; index < payload_length; ++index)
    {
        if (data[index] == 0u)
        {
            ++report.embedded_nul_count;
            report.source_findings |= text_source_finding_bit(ETextSourceFinding::literal_nul);
        }
        if (data[index] > 0x7fu)
        {
            report.input_is_pure_ascii = false;
        }
    }
    m_source = CSourceCursor{ data, static_cast<std::uint32_t>(payload_length), 0u };
    std::uint32_t bom_bytes = 0u;
    if (m_source.length != 0u)
    {
        (void)unicode::utf::identifyUTF(data, m_source.length, bom_bytes);
        report.leading_bom_detected = bom_bytes != 0u;
        report.leading_bom_byte_count = bom_bytes;
        if (report.leading_bom_detected)
        {
            report.source_findings |= text_source_finding_bit(ETextSourceFinding::leading_bom);
        }
    }
    if ((bom_bytes == k_utf8_bom_bytes) && (m_source.length >= k_utf8_bom_bytes) &&
        (data[0] == 0xefu) && (data[1] == 0xbbu) && (data[2] == 0xbfu))
    {
        m_source.offset = k_utf8_bom_bytes;
        report.leading_utf8_bom_stripped = true;
        report.source_findings |= text_source_finding_bit(ETextSourceFinding::stripped_utf8_bom);
    }
    if (payload_length >= memory::k_byte_size_ceiling)
    {
        report.output_exceeds_engine_size_limit = true;
        set_failure(ETextLintFailure::output_limit);
        return std::move(m_result);
    }
    const std::uint32_t source_observations = report.source_findings;
    bool success = decode_attempt(false);
    if (!success && (report.first_failure.reason == ETextLintFailure::utf8_decode) && !report.leading_bom_detected)
    {
        //  Publish only the adopted interpretation. Keep raw-byte observations
        //  and failed-attempt evidence, never its columns or source features.
        report.first_failure = {};
        report.input_metrics = {};
        report.output_metrics = {};
        report.encountered_line_endings = report.normalised_line_endings = 0u;
        report.modified_utf8_nul_count = report.cesu8_pair_count = 0u;
        report.source_findings = source_observations |
            text_source_finding_bit(ETextSourceFinding::utf8_attempt_failed) |
            text_source_finding_bit(ETextSourceFinding::cp1252);
        set_cp1252_evidence();
        m_attempt = {};
        m_source.offset = 0u;
        success = decode_attempt(true);
        report.recovered_as_cp1252 = success;
    }
    finish_line(report.input_metrics, m_attempt.input_line);
    finish_line(report.output_metrics, m_attempt.output_line);
    return std::move(m_result);
}

}   //  namespace lint_util

CTextLintResult lint(const CByteConstView& input, const std::uint32_t line_ending_flags) noexcept
{
    lint_util::CLinter linter(line_ending_flags);
    return linter.run(input.data(), input.size());
}

CTextLintResult lint(const CStringView& input, const std::uint32_t line_ending_flags) noexcept
{
    lint_util::CLinter linter(line_ending_flags);
    return linter.run(input.string(), input.length());
}

}   //  namespace text_linter
