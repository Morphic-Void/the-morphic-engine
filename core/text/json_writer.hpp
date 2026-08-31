
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    json_writer.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Aug 26
//
//  Deterministic, no-exception serialization of a validated baked document.
//  No file I/O, logging, live-document adapter, or text-linter prepass.

#pragma once

#ifndef JSON_WRITER_HPP_INCLUDED
#define JSON_WRITER_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "containers/ByteBuffers.hpp"

class CBakedDocument;

enum class EJsonWriteLineEnding : std::uint8_t
{
    lf,
    crlf,
};

struct CJsonWriteOptions
{
    bool strict_json{ false };
    bool escape_non_ascii{ false };
    bool pretty_print{ true };
    std::size_t indent_width{ 2u };
    EJsonWriteLineEnding line_ending{ EJsonWriteLineEnding::lf };
    bool trailing_line_ending{ true };
};

enum class EJsonWriteStatus : std::uint8_t
{
    success,
    source_not_ready,
    source_contract_violation,
    output_exceeds_engine_size_limit,
    allocation_failed,
    internal_error,
};

struct CJsonWriteReport
{
    EJsonWriteStatus status{ EJsonWriteStatus::internal_error };
    std::size_t logical_text_byte_size{ 0u };

    //  Counts describe emitted occurrences (not distinct interned strings).
    //  Every counter is zero on failure, including allocation failure.
    std::size_t non_decimal_integers_normalised{ 0u };
    std::size_t explicit_positive_signs_omitted{ 0u };
    std::size_t non_ascii_code_points_escaped{ 0u };
    std::size_t embedded_nuls_escaped{ 0u };
    std::size_t recovery_nodes_written{ 0u };
    bool diagnostic_envelope_written{ false };

    [[nodiscard]] bool succeeded() const noexcept { return status == EJsonWriteStatus::success; }
};

struct CJsonWriteResult
{
    //  On success size() includes one terminal zero; logical_text_byte_size
    //  excludes it, but includes any requested trailing line ending.
    //  On failure output is empty and owns no allocation.
    CByteBuffer output;
    CJsonWriteReport report;
};

namespace json_writer
{

//  source must retain its validated, immutable bytes throughout this call.
//  Construction/reset is the validation boundary; this does not repeat CRC,
//  structure, floating-point, or UTF-8 validation. A dangling or externally
//  mutated view violates the caller contract and is not safely diagnosable.
//  Recovery state automatically selects diagnostic output; neither option
//  suppresses it or repairs the source. Output uses the ambient allocator.
[[nodiscard]] CJsonWriteResult write(const CBakedDocument& source, const CJsonWriteOptions& options = {}) noexcept;

} // namespace json_writer

#endif // JSON_WRITER_HPP_INCLUDED
