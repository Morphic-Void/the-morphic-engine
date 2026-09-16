
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   file.hpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Basic load and save utility functions (file <=> memory blob)
//
//  Multi-threaded usage assumes that multiple threads will not be saving
//  files with the same name.

#pragma once

#ifndef FILE_HPP_INCLUDED
#define FILE_HPP_INCLUDED

#include <cstddef>
#include "containers/ByteBuffers.hpp"

namespace platform::filesystem
{

//  utf8_path is expected to be UTF-8 encoded.

//  Empty files are treated as failure.
//  On failure, the buffer is empty.
//  On success, the buffer size includes pad bytes, which are zero-filled.
//  Alignment uses memory conditioning with a 16-byte floor; above-cap requests fail.
//  Capacity is rounded to effective alignment and the entire unused tail is zeroed.
CByteBuffer loadFile(const char* const utf8_path, const std::size_t pad = 0, const std::size_t alignment = 16u) noexcept;

//  Will not write empty files.
//  Semi-atomic file writing.
//  On failure, best effort is made clean up partial writes.
bool saveFile(const char* const utf8_path, const CByteConstView& view) noexcept;

}   //  namespace platform::filesystem

#endif  //  #ifndef FILE_HPP_INCLUDED
