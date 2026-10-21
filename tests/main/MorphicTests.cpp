
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    MorphicTests.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#include <iostream>

#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/main/run_tests.hpp"

int main(const int argc, char** const argv)
{
    if (should_print_usage(argc, argv))
    {
        print_usage();
        return 0;
    }
    const char* log_tag = nullptr;
    if (!parse_log_tag(argc, argv, log_tag))
    {
        std::cerr <<
            "Invalid --log-tag. Use --log-tag=<value> with 1-48 ASCII letters, digits, '.', '_' or '-'.\n";
        return 2;
    }
    const char* output_directory = nullptr;
    if (!parse_output_directory(argc, argv, output_directory))
    {
        std::cerr <<
            "Invalid --output-directory. Use --output-directory=<path>.\n";
        return 2;
    }
    if (!test_environment::initialise_paths(
        (argc > 0) ? argv[0] : nullptr, log_tag, output_directory))
    {
        std::cerr << "MorphicTests could not initialise repository paths and log output.\n";
        return 1;
    }
    std::cout << "MorphicTests logs: " << test_environment::log_path_pattern() << '\n';
    if (!test_environment::install())
    {
        std::cerr << "MorphicTests could not install its executable environment.\n";
        return 1;
    }

    const int result = run_tests(parse_test_run_mode(argc, argv));
    if (!test_environment::is_clean())
    {
        std::cerr << "MorphicTests finished with residual service or memory state.\n";
        return (result == 0) ? 1 : result;
    }
    return result;
}
