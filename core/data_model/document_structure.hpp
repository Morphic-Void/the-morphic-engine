
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
    unexamined = 0u, success, failed
};

struct CDocumentStructureEstimates
{
    //  Syntactic occurrences before collision recovery or wrapper decoding.
    //  Counts include the explicit or inferred root container. They are hints for
    //  later construction, not live-node counts or allocation guarantees.
    std::size_t value_count{ 0u };
    std::size_t object_count{ 0u };
    std::size_t array_count{ 0u };
    std::size_t named_entry_count{ 0u };
    //  Sum of raw name/string token bytes, including quotes and escapes.
    std::size_t string_source_byte_size{ 0u };
    //  Root counts as depth one, even when its delimiters are implicit.
    std::size_t maximum_depth{ 0u };
};

struct CDocumentStructureReport
{
    EDocumentStructureStatus status{ EDocumentStructureStatus::unexamined };
    CDocumentFailure failure;
    CTextLocation structure_start;
    CTextLocation failure_point;
    //  Retain established observations on failure. Only success denotes a
    //  complete scan; absent findings in a partial scan do not prove absence.
    std::uint32_t findings{ 0u };

    [[nodiscard]] bool succeeded() const noexcept;
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
//  Optional capacity estimates are separate from diagnostics. They are reset
//  on entry and published only after a successful structural check.
[[nodiscard]] CDocumentStructureReport check(const CStringView& source, CDocumentStructureEstimates* estimates = nullptr) noexcept;

}   //  namespace document_structure

inline bool CDocumentStructureReport::succeeded() const noexcept
{
    return status == EDocumentStructureStatus::success;
}

#endif // DOCUMENT_STRUCTURE_HPP_INCLUDED
