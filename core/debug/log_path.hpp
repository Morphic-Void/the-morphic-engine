
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    log_path.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Collision-safe debug-log path construction.

#pragma once

#ifndef DEBUG_LOG_PATH_HPP_INCLUDED
#define DEBUG_LOG_PATH_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint64_t

namespace debug_system
{

constexpr std::size_t k_log_tag_max_length = 48u;

//  A null tag means that no caller-supplied disambiguation was requested.
//  Non-null tags must contain only ASCII letters, digits, '.', '_', or '-',
//  begin with a letter or digit, and fit k_log_tag_max_length.
[[nodiscard]] bool is_valid_log_tag(const char* tag) noexcept;

//  Produces <stem>.<tag>.p<process-id>.log, or <stem>.p<process-id>.log
//  when tag is null. An optional directory prefixes the filename.
//  The destination is cleared on failure.
[[nodiscard]] bool format_process_log_path(
    char* destination,
    std::size_t destination_capacity,
    const char* stem,
    const char* tag,
    std::uint64_t process_id,
    const char* directory = nullptr) noexcept;

}   //  namespace debug_system

#endif  //  #ifndef DEBUG_LOG_PATH_HPP_INCLUDED
