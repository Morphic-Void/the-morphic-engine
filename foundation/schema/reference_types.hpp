
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    reference_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Fixed-width schema reference storage; dereferencing requires a block context.

#pragma once

#include <cstdint>

namespace schema
{

struct STextReference
{
    std::int32_t string_value_id{};
};

struct SSpanReference
{
    std::int32_t collection_id{};
    std::int32_t first{};
    std::uint32_t count{};
};

static_assert((sizeof(STextReference) == 4u) && (alignof(STextReference) == 4u));
static_assert((sizeof(SSpanReference) == 12u) && (alignof(SSpanReference) == 4u));

}   // namespace schema
