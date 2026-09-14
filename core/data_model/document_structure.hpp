
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_structure.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#pragma once

#ifndef DOCUMENT_STRUCTURE_HPP_INCLUDED
#define DOCUMENT_STRUCTURE_HPP_INCLUDED

#include "containers/StringBuffers.hpp"
#include "data_model/document_findings.hpp"

struct CDocumentStructureEstimates
{
    //  Syntactic occurrences before collision extension or singleton normalisation.
    //  Counts include the explicit or inferred root container. They are hints for
    //  later construction, not live-node counts or allocation guarantees.
    std::size_t value_count{ 0u };
    std::size_t object_count{ 0u };
    std::size_t array_count{ 0u };
    std::size_t named_entry_count{ 0u };
    std::size_t string_source_byte_size{ 0u };    //  Raw name/string token bytes, including quotes and escapes.
    std::size_t maximum_depth{ 0u };              //  Root counts as depth one, including an implicit root.
};

namespace document_structure
{

//  Check bounded UTF-8 already produced by a successful linter call. Exclude
//  its physical terminal zero and normalise all source line breaks to LF.
//  Source must stay immutable and alive. An absent string is invalid input;
//  a present zero-length string denotes an implicit empty object. Always supply
//  an explicit length to preserve embedded NULs. No document construction,
//  numeric conversion, name interning or collision interpretation occurs.
//  Processing success guarantees syntax only; policy remains unexamined.
//  Uses ambient framework allocation for an iterative O(depth) frame vector.
//  Optional capacity estimates are separate from diagnostics. They are reset
//  on entry and published only after a successful structural check.
[[nodiscard]] CDocumentReport check(const CStringView& source, CDocumentStructureEstimates* const estimates = nullptr) noexcept;

}   //  namespace document_structure

#endif // DOCUMENT_STRUCTURE_HPP_INCLUDED
