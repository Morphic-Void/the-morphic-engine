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

namespace tests
{

[[nodiscard]] bool file_contains(
    const char* path, const char* expected) noexcept;

}   //  namespace tests

#endif  //  MORPHIC_TEST_FILE_HELPERS_HPP_INCLUDED
