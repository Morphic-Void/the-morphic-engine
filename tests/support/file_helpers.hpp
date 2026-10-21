
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    file_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#pragma once

#ifndef MORPHIC_TEST_FILE_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_FILE_HELPERS_HPP_INCLUDED

#include <cstddef>
#include <string>

namespace tests
{

[[nodiscard]] bool write_test_file(
    const std::string& path, const void* bytes, std::size_t size);
[[nodiscard]] bool write_test_file(
    const std::string& path, const char* text);

[[nodiscard]] bool file_contains(
    const char* path, const char* expected) noexcept;

}   //  namespace tests

#endif  //  MORPHIC_TEST_FILE_HELPERS_HPP_INCLUDED
