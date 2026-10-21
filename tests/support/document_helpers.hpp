//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef MORPHIC_TEST_DOCUMENT_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_DOCUMENT_HELPERS_HPP_INCLUDED

#include <string>

#include "data_model/document_translation.hpp"
#include "schema/document_query.hpp"

namespace tests
{

[[nodiscard]] bool parse_document(const std::string& text, CLiveDocument& document);

//  Compare exact baked bytes, including representation details.
[[nodiscard]] bool same_baked_document(const CLiveDocument& document, const CBakedDocumentBlock& expected);

//  Independent ordered comparison of values, ignoring integer spelling.
[[nodiscard]] bool equal_document_values(
    const schema::detail::CDocumentRead& a, schema::detail::SOccurrence left,
    const schema::detail::CDocumentRead& b, schema::detail::SOccurrence right);

}   //  namespace tests

#endif  //  MORPHIC_TEST_DOCUMENT_HELPERS_HPP_INCLUDED
