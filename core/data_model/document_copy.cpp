
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_copy.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Stabilise names before document storage can move.

#include "data_model/document_copy.hpp"

#include <cstring>

namespace document_translation
{

//==============================================================================
//  Name stabilisation
//==============================================================================

[[nodiscard]] bool stabilise_document_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept
{
    if (source.empty())
    {
        return false;
    }
    if (source.length() == 0u)
    {
        copied = CStringView{ "" };
        return true;
    }
    if (!storage.allocate(source.length(), 1u) || !storage.set_size(source.length()))
    {
        return false;
    }
    std::memcpy(storage.data(), source.string(), source.length());
    copied = CStringView{ storage.data(), source.length() };
    return true;
}

}   // namespace document_translation
