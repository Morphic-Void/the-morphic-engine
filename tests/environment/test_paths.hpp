
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    test_paths.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#pragma once

#ifndef MORPHIC_TEST_PATHS_HPP_INCLUDED
#define MORPHIC_TEST_PATHS_HPP_INCLUDED

#include <string>
#include "platform/module/binding.hpp"

namespace test_environment
{

//  Independent expected filename construction for module and filesystem tests.
[[nodiscard]] inline std::string module_filename(const char* stem, const bool uppercase_extension = false)
{
    std::string extension = platform::module::get_modules_std_ext();
    if (uppercase_extension)
    {
        for (char& character : extension)
        {
            if ((character >= 'a') && (character <= 'z')) { character -= 'a' - 'A'; }
        }
    }
    return std::string(stem) + "." + extension;
}

[[nodiscard]] bool initialise_paths(
    const char* executable_argument,
    const char* log_tag = nullptr,
    const char* output_directory = nullptr);
[[nodiscard]] const std::string& repository_root() noexcept;
[[nodiscard]] std::string repository_path(const char* relative_path);
[[nodiscard]] std::string binary_path(const char* filename);
[[nodiscard]] std::string test_log_path(const char* stem);
//  Produces a process/tag-qualified output filename, preserving its extension.
[[nodiscard]] std::string test_output_path(const char* filename);
[[nodiscard]] const std::string& log_path_pattern() noexcept;

}   //  namespace test_environment

#endif  //  MORPHIC_TEST_PATHS_HPP_INCLUDED
