
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_structure.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#pragma once

#ifndef DOCUMENT_STRUCTURE_HPP_INCLUDED
#define DOCUMENT_STRUCTURE_HPP_INCLUDED

#include "data_model/document_text_lex.hpp"

enum class EDocumentStructureStatus : std::uint8_t
{
    unexamined = 0u, success, invalid_input_view, syntax_error, allocation_failed, scratch_size_limit, internal_error
};

struct CDocumentStructureEstimates
{
    //  Syntactic occurrences before collision recovery or wrapper decoding.
    //  Counts include the explicit or implicit root object. They are hints for
    //  later construction, not live-node counts or allocation guarantees.
    std::size_t value_count{ 0u };
    std::size_t object_count{ 0u };
    std::size_t array_count{ 0u };
    std::size_t named_entry_count{ 0u };
    //  Sum of raw name/string token bytes, including quotes and escapes.
    std::size_t string_source_byte_size{ 0u };
    //  Root counts as depth one, even when its braces are implicit.
    std::size_t maximum_depth{ 0u };
};

struct CDocumentStructureReport
{
    EDocumentStructureStatus status{ EDocumentStructureStatus::unexamined };
    document_text::ESyntaxError syntax_error{ document_text::ESyntaxError::none };
    CTextLocation structure_start;
    CTextLocation failure_point;
    //  Published only on success; failure leaves flags and estimates zero.
    std::uint32_t required_relaxations{ 0u };
    std::uint32_t numeric_extensions{ 0u };
    CDocumentStructureEstimates estimates;

    [[nodiscard]] bool succeeded() const noexcept { return status == EDocumentStructureStatus::success; }
};

namespace document_structure
{

//  Check bounded UTF-8 already produced by a successful linter call. Exclude
//  its physical terminal zero and normalize all source line breaks to LF.
//  Source must stay immutable and alive. An absent string is invalid input;
//  a present zero-length string denotes an implicit empty object. Always supply
//  an explicit length to preserve embedded NULs. No document construction,
//  numeric conversion, name interning or collision/protocol interpretation
//  occurs. Success guarantees syntax only.
//  Uses ambient framework allocation for an iterative O(depth) frame vector.
[[nodiscard]] CDocumentStructureReport check(const CStringView& source) noexcept;

}   //  namespace document_structure

#endif // DOCUMENT_STRUCTURE_HPP_INCLUDED
