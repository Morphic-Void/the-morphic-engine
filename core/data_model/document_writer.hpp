
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_writer.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26
//
//  Iterative text serialization over the checked baked-document interface.

#pragma once

#ifndef DOCUMENT_WRITER_HPP_INCLUDED
#define DOCUMENT_WRITER_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "containers/ByteBuffers.hpp"

class CBakedDocument;

enum class EDocumentWriteMode : std::uint8_t { morphic, strict_json };
enum class EDocumentWriteLineEnding : std::uint8_t { lf, crlf };

struct CDocumentWriteOptions
{
    EDocumentWriteMode mode{ EDocumentWriteMode::morphic };
    bool escape_non_ascii{ false };
    bool pretty_print{ true };
    std::size_t indent_width{ 2u };
    EDocumentWriteLineEnding line_ending{ EDocumentWriteLineEnding::lf };
    bool trailing_line_ending{ true };
};

enum class EDocumentWriteStatus : std::uint8_t
{
    success,
    source_not_ready,
    invalid_options,
    source_contract_violation,
    output_exceeds_engine_size_limit,
    allocation_failed,
    internal_error,
};

struct CDocumentWriteReport
{
    EDocumentWriteStatus status{ EDocumentWriteStatus::internal_error };
    std::size_t logical_text_byte_size{ 0u };

    //  Emitted occurrences, not distinct interned strings. All counts are zero
    //  on failure. Strict mode reports numeric normalization; both modes use
    //  reversible reserved-name escaping and recovered-array wrappers.
    std::size_t non_decimal_integers_normalised{ 0u };
    std::size_t explicit_positive_signs_omitted{ 0u };
    std::size_t non_ascii_code_points_escaped{ 0u };
    std::size_t embedded_nuls_escaped{ 0u };
    std::size_t reserved_property_names_escaped{ 0u };
    std::size_t recovered_arrays_written{ 0u };

    [[nodiscard]] bool succeeded() const noexcept { return status == EDocumentWriteStatus::success; }
};

struct CDocumentWriteResult
{
    //  Success includes one physical terminal zero in output.size(). Logical
    //  size excludes it and includes any requested trailing line ending.
    //  Failure owns no output allocation and reports an explicit status.
    CByteBuffer output;
    CDocumentWriteReport report;
};

namespace document_writer
{

//  Source bytes must stay immutable and alive throughout the call. The checked
//  view owns validation; writing does not revalidate or take source ownership.
//  Uses the ambient framework allocator, with no file I/O or logging.
[[nodiscard]] CDocumentWriteResult write(const CBakedDocument& source, const CDocumentWriteOptions& options = {}) noexcept;

}

#endif // DOCUMENT_WRITER_HPP_INCLUDED
