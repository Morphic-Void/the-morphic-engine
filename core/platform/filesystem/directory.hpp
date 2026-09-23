
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    directory.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Sep 26
//
//  Basic directory queries for the directory-backed filesystem image.

#pragma once

#ifndef PLATFORM_FILESYSTEM_DIRECTORY_HPP_INCLUDED
#define PLATFORM_FILESYSTEM_DIRECTORY_HPP_INCLUDED

#include "containers/StringBuffers.hpp"

namespace platform::filesystem
{

enum class EDirectoryEntry : std::uint8_t { file = 0, directory, other };
enum class EDirectoryQuery : std::uint8_t { complete = 0, open_failed, read_failed, visitor_failed };

//  Enumerates one directory, excluding . and ... The name is borrowed only for
//  the callback. Symlinks/reparse points are other; callers must not recurse into
//  them. Empty directories succeed. A false visitor return stops enumeration.
using FDirectoryVisitor = bool (*)(void* const, const char* const, const EDirectoryEntry) noexcept;
[[nodiscard]] EDirectoryQuery query_directory(const char* const path, const FDirectoryVisitor visitor, void* const context) noexcept;

//  Directory of the running process image, not argv[0] or the working directory.
//  Queries the full executable filename into 1 KiB of typed stack storage.
//  Truncation fails without changing destination; there is no growth fallback.
[[nodiscard]] bool executable_directory(CSimpleString& destination) noexcept;

}   //  namespace platform::filesystem

#endif  //  PLATFORM_FILESYSTEM_DIRECTORY_HPP_INCLUDED
