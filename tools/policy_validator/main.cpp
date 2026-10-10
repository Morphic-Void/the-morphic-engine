//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    main.cpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    19 Aug 26

#include "policy_validator.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

static void print_usage()
{
    std::cout <<
        "Usage: MorphicPolicyValidator [--root path] [--project name] "
        "[--configuration name] [--platform name] [--no-report]\n";
}

static std::string require_value(const int argc, char** const argv, int& index)
{
    if ((index + 1) >= argc)
    {
        throw std::invalid_argument(std::string("missing value for ") + argv[index]);
    }
    ++index;
    return argv[index];
}

int main(const int argc, char** const argv)
{
    try
    {
        morphic::policy::SOptions options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if ((argument == "--help") || (argument == "-h"))
            {
                print_usage();
                return 0;
            }
            if (argument == "--root")
            {
                options.repository_root = require_value(argc, argv, index);
            }
            else if (argument == "--project")
            {
                options.project = require_value(argc, argv, index);
            }
            else if (argument == "--configuration")
            {
                options.configuration = require_value(argc, argv, index);
            }
            else if (argument == "--platform")
            {
                options.platform = require_value(argc, argv, index);
            }
            else if (argument == "--no-report")
            {
                options.write_report = false;
            }
            else
            {
                throw std::invalid_argument("unknown argument: " + argument);
            }
        }

        if (options.repository_root.empty())
        {
            options.repository_root = morphic::policy::discover_repository_root(
                std::filesystem::current_path());
        }
        return morphic::policy::run(options);
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "MorphicPolicyValidator: " << error.what() << '\n';
        print_usage();
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MorphicPolicyValidator: internal failure: " << error.what() << '\n';
        return 3;
    }
}
