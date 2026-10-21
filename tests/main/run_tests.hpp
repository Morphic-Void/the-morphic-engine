
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    run_tests.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Apr 26

#pragma once

#ifndef RUN_TESTS_HPP_INCLUDED
#define RUN_TESTS_HPP_INCLUDED

enum class ETestRunMode
{
    none = 0,
    core = 1,
    moderate = 2,
    full = 3
};

bool should_print_usage(int argc, char** argv);
void print_usage();
[[nodiscard]] bool parse_log_tag(int argc, char** argv, const char*& log_tag);
[[nodiscard]] bool parse_output_directory(int argc, char** argv, const char*& output_directory);
ETestRunMode parse_test_run_mode(int argc, char** argv);
int run_tests(ETestRunMode mode);

#endif  //  RUN_TESTS_HPP_INCLUDED
