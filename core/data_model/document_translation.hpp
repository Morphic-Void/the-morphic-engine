
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_translation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Explicit translation between live and baked document representations.

#pragma once

#ifndef DOCUMENT_TRANSLATION_HPP_INCLUDED
#define DOCUMENT_TRANSLATION_HPP_INCLUDED

class CBakedDocument;
class CBakedDocumentBlock;
class CLiveDocument;

namespace document_translation
{

[[nodiscard]] bool bake(const CLiveDocument& source, CBakedDocumentBlock& destination) noexcept;
[[nodiscard]] bool promote(const CBakedDocument& source, CLiveDocument& destination) noexcept;

}

#endif // DOCUMENT_TRANSLATION_HPP_INCLUDED
