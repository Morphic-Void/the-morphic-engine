
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_extension.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Host configuration substitution for the native module extension.

#pragma once

#include <cstddef>

namespace host::config
{

//  Configuration-only substitution of ${module_extension}; other text is literal.
//  No allocation. Source and destination must not overlap. Failure leaves the
//  destination unchanged; capacity includes the terminating NUL.
[[nodiscard]] bool expand_module_extension(
    const char* const source, char* const destination, const std::size_t capacity) noexcept;

}   //  namespace host::config
