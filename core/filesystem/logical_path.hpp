
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    logical_path.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Shared byte-oriented logical path rules; ASCII folding only.

#pragma once

#ifndef LOGICAL_PATH_HPP_INCLUDED
#define LOGICAL_PATH_HPP_INCLUDED

#include "containers/StringBuffers.hpp"

namespace logical_path
{

[[nodiscard]] inline char fold_ascii(const char value) noexcept
{
    return ((value >= 'A') && (value <= 'Z')) ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] inline bool is_folded(const CStringView name) noexcept
{
    if (name.string() == nullptr) { return false; }
    for (std::size_t i = 0u; i < name.length(); ++i)
    {
        if (fold_ascii(name.cstring()[i]) != name.cstring()[i]) { return false; }
    }
    return true;
}

[[nodiscard]] inline bool case_equal(const CStringView a, const CStringView b) noexcept
{
    if ((a.string() == nullptr) || (b.string() == nullptr) || (a.length() != b.length())) { return false; }
    for (std::size_t i = 0u; i < a.length(); ++i)
    {
        if (fold_ascii(a.cstring()[i]) != fold_ascii(b.cstring()[i])) { return false; }
    }
    return true;
}

//  Reserved Host mount; never a configured directory or virtual bundle root.
[[nodiscard]] inline bool is_native_mount(const CStringView root) noexcept
{
    return case_equal(root, CStringView{ "native:" });
}

[[nodiscard]] inline bool valid_component(const CStringView name) noexcept
{
    if ((name.length() == 0u) || (name == CStringView{ "." }) || (name == CStringView{ ".." })) { return false; }
    for (std::size_t i = 0u; i < name.length(); ++i)
    {
        const char value = name.cstring()[i];
        if ((value == '\0') || (value == '/') || (value == '\\') ||
            (value == ':') || (value == '*') || (value == '?')) { return false; }
    }
    return true;
}

[[nodiscard]] inline bool valid_relative_file(const CStringView path) noexcept
{
    if (path.length() == 0u) { return false; }
    std::size_t start = 0u;
    for (std::size_t i = 0u; i <= path.length(); ++i)
    {
        if ((i == path.length()) || (path.cstring()[i] == '/'))
        {
            if (!valid_component(CStringView{ path.cstring() + start, i - start })) { return false; }
            start = i + 1u;
        }
    }
    return true;
}

}   // namespace logical_path

#endif // LOGICAL_PATH_HPP_INCLUDED
