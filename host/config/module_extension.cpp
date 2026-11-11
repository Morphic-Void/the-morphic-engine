
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_extension.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Host configuration substitution without changing native filename construction.

#include "host/config/module_extension.hpp"

#include <cstring>

#include "platform/module/binding.hpp"

namespace host::config
{

bool expand_module_extension(
    const char* const source, char* const destination, const std::size_t capacity) noexcept
{
    if ((source == nullptr) || (destination == nullptr) || (capacity == 0u))
    {
        return false;
    }
    static constexpr char token[] = "${module_extension}";
    const char* const extension = platform::module::get_modules_std_ext();
    const std::size_t suffix = std::strlen(extension);
    std::size_t length = 0u;
    for (const char* cursor = source; *cursor != '\0';)
    {
        const bool replace = std::strncmp(cursor, token, (sizeof(token) - 1u)) == 0;
        const std::size_t count = replace ? suffix : 1u;
        if (count >= (capacity - length)) return false;
        length += count;
        cursor += replace ? (sizeof(token) - 1u) : 1u;
    }
    char* output = destination;
    for (const char* cursor = source; *cursor != '\0';)
    {
        if (std::strncmp(cursor, token, (sizeof(token) - 1u)) == 0)
        {
            std::memcpy(output, extension, suffix);
            output += suffix;
            cursor += sizeof(token) - 1u;
        }
        else
        {
            *output++ = *cursor++;
        }
    }
    *output = '\0';
    return true;
}

}   //  namespace host::config
