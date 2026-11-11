
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    log_path.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Collision-safe debug-log path construction.

#include "debug/log_path.hpp"

#include <cstdio>       //  std::snprintf

namespace debug_system
{

[[nodiscard]] static bool is_ascii_letter_or_digit(const char character) noexcept
{
    return
        ((character >= 'a') && (character <= 'z')) ||
        ((character >= 'A') && (character <= 'Z')) ||
        ((character >= '0') && (character <= '9'));
}

bool is_valid_log_tag(const char* const tag) noexcept
{
    if ((tag == nullptr) || !is_ascii_letter_or_digit(tag[0]))
    {
        return false;
    }

    std::size_t length = 1u;
    for (; tag[length] != '\0'; ++length)
    {
        if (length >= k_log_tag_max_length)
        {
            return false;
        }
        const char character = tag[length];
        if (!is_ascii_letter_or_digit(character) && (character != '.') && (character != '_') && (character != '-'))
        {
            return false;
        }
    }
    return length <= k_log_tag_max_length;
}

bool format_process_log_path(
    char* const destination,
    const std::size_t destination_capacity,
    const char* const stem,
    const char* const tag,
    const std::uint64_t process_id,
    const char* const directory) noexcept
{
    if ((destination == nullptr) || (destination_capacity == 0u))
    {
        return false;
    }
    destination[0] = '\0';
    if ((stem == nullptr) || (stem[0] == '\0') || (process_id == 0u) || ((tag != nullptr) && !is_valid_log_tag(tag)))
    {
        return false;
    }

    const bool has_directory = (directory != nullptr) && (directory[0] != '\0');
    const char* const prefix = has_directory ? directory : "";
    const char* const separator = has_directory ? "/" : "";
    const int result = (tag != nullptr)
        ? std::snprintf(destination, destination_capacity, "%s%s%s.%s.p%llu.log", prefix, separator, stem, tag, static_cast<unsigned long long>(process_id))
        : std::snprintf(destination, destination_capacity, "%s%s%s.p%llu.log", prefix, separator, stem, static_cast<unsigned long long>(process_id));
    if ((result < 0) || (static_cast<std::size_t>(result) >= destination_capacity))
    {
        destination[0] = '\0';
        return false;
    }
    return true;
}

}   //  namespace debug_system
