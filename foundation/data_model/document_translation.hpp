
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_translation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Explicit translation between live and baked document representations.

//  Public whole-document and root-member translation entry points. Subtree
//  copying is available separately from document_copy.hpp.
//  Include the live/baked document headers separately to define their owners.
//  Include string_value_remap.hpp when retaining a bake's string index mapping.

#pragma once

#ifndef DOCUMENT_TRANSLATION_HPP_INCLUDED
#define DOCUMENT_TRANSLATION_HPP_INCLUDED

class CBakedDocument;
class CBakedDocumentBlock;
class CLiveDocument;
class CStringView;

namespace document_translation
{

class CStringValueRemap;

//==============================================================================
//  Whole-document translation
//==============================================================================

[[nodiscard]] bool bake(const CLiveDocument& source, CBakedDocumentBlock& destination) noexcept;
//  Publish document and map together; failure preserves both caller outputs.
[[nodiscard]] bool bake(const CLiveDocument& source, CBakedDocumentBlock& destination, CStringValueRemap& strings) noexcept;
[[nodiscard]] bool promote(const CBakedDocument& source, CLiveDocument& destination) noexcept;

//==============================================================================
//  Selected root-member translation
//==============================================================================

//  Copy an object root with only one named direct member, if present.
[[nodiscard]] bool bake_root_member(const CLiveDocument& source, const CStringView& member_name, CBakedDocumentBlock& destination) noexcept;
[[nodiscard]] bool promote_root_member(const CBakedDocument& source, const CStringView& member_name, CLiveDocument& destination) noexcept;

}   // namespace document_translation

#endif // DOCUMENT_TRANSLATION_HPP_INCLUDED
