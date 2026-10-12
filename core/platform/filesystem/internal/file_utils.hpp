
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   file_utils.hpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Platform native file handling utilities
//
//  Notes:
//  - Requires C++ 17 or greater
//  - No exceptions.

#pragma once

#ifndef FILE_UTILS_HPP_INCLUDED
#define FILE_UTILS_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t
#include <cstdio>       //  std::FILE
#include "platform/path/native_path.hpp"

namespace platform::filesystem
{

enum class EOpenMode : std::uint8_t { BinaryRead = 0u, BinaryWrite, BinaryAppend, TextWrite, TextAppend };

std::FILE* openFile(const path::NativePath& file_path, const EOpenMode mode = EOpenMode::BinaryRead) noexcept;
void removeFile(const path::NativePath& file_path) noexcept;
bool renameFile(const path::NativePath& src_path, const path::NativePath& dst_path) noexcept;
bool flushToDisk(std::FILE* const handle) noexcept;

//  Reports the unpadded size, including successful empty files. Resets size on failure.
//  On success the stream is positioned at the beginning, not its original position.
bool queryFileSize(std::FILE* const handle, std::size_t& size) noexcept;

//  Convenience query: empty files and errors both return zero, even with padding.
std::size_t getFileSize(std::FILE* const handle, const std::size_t pad = 0) noexcept;

//  Reads a non-empty range at the current position. Short/error reads may partially
//  modify the destination. Does not seek, allocate, pad or close the stream.
bool readFileContent(std::FILE* const handle, std::uint8_t* const data, const std::size_t size) noexcept;

}   //  namespace platform::filesystem

#endif  //  #ifndef FILE_UTILS_HPP_INCLUDED
