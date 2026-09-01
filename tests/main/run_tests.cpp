
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:    run_tests.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    24 Apr 26

#include "tests/main/run_tests.hpp"
#include "tests/test_suites/AssetRepository_test_suite.hpp"
#include "tests/test_suites/AsyncState_test_suite.hpp"
#include "tests/test_suites/ByteBuffers_test_suite.hpp"
#include "tests/test_suites/CMemoryToken_test_suite.hpp"
#include "tests/test_suites/CMemoryView_test_suite.hpp"
#include "tests/test_suites/DebugService_test_suite.hpp"
#include "tests/test_suites/ErasedPod_test_suite.hpp"
#include "tests/test_suites/ErasedOwner_test_suite.hpp"
#include "tests/test_suites/StringBuffers_test_suite.hpp"
#include "tests/test_suites/SystemTypeIdentity_test_suite.hpp"
#include "tests/test_suites/TInstance_test_suite.hpp"
#include "tests/test_suites/TOrderedCollection_test_suite.hpp"
#include "tests/test_suites/TPodFifo_test_suite.hpp"
#include "tests/test_suites/TPodVector_test_suite.hpp"
#include "tests/test_suites/TUnorderedCollection_test_suite.hpp"
#include "tests/test_suites/TOrderedSlots_test_harness.hpp"
#include "tests/test_suites/TUnorderedSlots_test_harness.hpp"
#include "tests/test_suites/TextLinter_test_suite.hpp"
#include "tests/test_suites/TQueueTransport_test_suite.hpp"
#include "tests/test_suites/TMpmcTransport_test_suite.hpp"
#include "tests/test_suites/TRingTransport_test_suite.hpp"
#include "tests/test_suites/TOwningTransport_test_suite.hpp"
#include "tests/environment/test_environment.hpp"
#include "debug/log_path.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace
{
bool starts_with(const std::string& value, const char* prefix)
{
    const std::size_t prefix_length = std::strlen(prefix);
    return value.size() >= prefix_length && std::memcmp(value.data(), prefix, prefix_length) == 0;
}

bool is_help_argument(const std::string& argument)
{
    return
        argument == "-?" ||
        argument == "/?" ||
        argument == "-h" ||
        argument == "--help" ||
        argument == "-help" ||
        argument == "/help";
}

bool parse_test_mode_value(const std::string& value, ETestRunMode& out_mode)
{
    if (value == "0")
    {
        out_mode = ETestRunMode::none;
        return true;
    }
    if (value == "1")
    {
        out_mode = ETestRunMode::core;
        return true;
    }
    if (value == "2")
    {
        out_mode = ETestRunMode::moderate;
        return true;
    }
    if (value == "3")
    {
        out_mode = ETestRunMode::full;
        return true;
    }
    return false;
}

template<typename F>
int run_isolated_suite(const char* const name, F&& run)
{
    if (!test_environment::is_clean())
    {
        std::cerr << name << ": dirty test environment before suite\n";
        return 1;
    }
    const int result = run();
    if (!test_environment::is_clean())
    {
        std::cerr << name << ": residual service or memory state after suite\n";
        return (result == 0) ? 1 : result;
    }
    return result;
}
}

bool should_print_usage(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (is_help_argument(argument))
        {
            return true;
        }
    }

    return false;
}

void print_usage()
{
    std::cout <<
        "Usage: MorphicTests [options]\n"
        "\n"
        "Options:\n"
        "  -?, /?, -h, --help, -help, /help\n"
        "               Show this usage summary\n"
        "  -t0          Skip all tests\n"
        "  -t1          Run core tests only (no expensive harness work)\n"
        "  -t2          Run moderate tests (current default)\n"
        "  -t3          Run full expensive tests\n"
        "  --tests=<0-3>\n"
        "               Long-form equivalent of -t0..-t3\n"
        "  --log-tag=<value>\n"
        "               Add a 1-48 character disambiguation tag to test logs\n"
        "  --output-directory=<path>\n"
        "               Write test output beneath the supplied directory\n";
}

bool parse_log_tag(const int argc, char** const argv, const char*& log_tag)
{
    constexpr char prefix[] = "--log-tag=";
    log_tag = nullptr;
    for (int index = 1; index < argc; ++index)
    {
        const char* const argument = argv[index];
        if ((argument != nullptr) &&
            (std::strncmp(argument, prefix, sizeof(prefix) - 1u) == 0))
        {
            const char* const candidate = argument + (sizeof(prefix) - 1u);
            if ((log_tag != nullptr) || !debug_system::is_valid_log_tag(candidate))
            {
                return false;
            }
            log_tag = candidate;
        }
        else if ((argument != nullptr) && (std::strcmp(argument, "--log-tag") == 0))
        {
            return false;
        }
    }
    return true;
}

bool parse_output_directory(
    const int argc, char** const argv, const char*& output_directory)
{
    constexpr char prefix[] = "--output-directory=";
    output_directory = nullptr;
    for (int index = 1; index < argc; ++index)
    {
        const char* const argument = argv[index];
        if ((argument != nullptr) &&
            (std::strncmp(argument, prefix, sizeof(prefix) - 1u) == 0))
        {
            const char* const candidate = argument + (sizeof(prefix) - 1u);
            if ((output_directory != nullptr) || (candidate[0] == 0))
            {
                return false;
            }
            output_directory = candidate;
        }
        else if ((argument != nullptr) &&
            (std::strcmp(argument, "--output-directory") == 0))
        {
            return false;
        }
    }
    return true;
}

ETestRunMode parse_test_run_mode(int argc, char** argv)
{
    ETestRunMode mode = ETestRunMode::moderate;

    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];

        if (argument.size() == 3 && starts_with(argument, "-t"))
        {
            ETestRunMode parsed_mode = mode;
            if (parse_test_mode_value(argument.substr(2), parsed_mode))
            {
                mode = parsed_mode;
            }
            continue;
        }

        if (starts_with(argument, "--tests="))
        {
            ETestRunMode parsed_mode = mode;
            if (parse_test_mode_value(argument.substr(std::strlen("--tests=")), parsed_mode))
            {
                mode = parsed_mode;
            }
            continue;
        }
    }

    return mode;
}

int run_tests(ETestRunMode mode)
{
    if (mode == ETestRunMode::none)
    {
        return 0;
    }

    int cumulative_result = 0;

    cumulative_result += run_isolated_suite("AssetRepository", &run_asset_repository_tests);
    cumulative_result += run_isolated_suite("AsyncState", &run_async_state_tests);
    cumulative_result += run_isolated_suite("CMemoryToken", &run_memory_token_tests);
    cumulative_result += run_isolated_suite("CMemoryView", &run_memory_view_tests);
    cumulative_result += run_isolated_suite("ErasedOwner", &run_erased_owner_tests);
    cumulative_result += run_isolated_suite("ErasedPod", &run_erased_pod_tests);
    cumulative_result += run_isolated_suite("TPodVector", &run_pod_vector_tests);
    cumulative_result += run_isolated_suite("ByteBuffers", &run_byte_buffer_tests);
    cumulative_result += run_isolated_suite("TextLinter", &run_text_linter_tests);
    cumulative_result += run_isolated_suite("TInstance", &run_instance_tests);
    cumulative_result += run_isolated_suite("DebugService", &run_debug_service_tests);
    cumulative_result += run_isolated_suite("SystemTypeIdentity", &run_system_type_identity_tests);
    cumulative_result += run_isolated_suite("TPodFifo", &run_pod_fifo_tests);
    cumulative_result += run_isolated_suite("StringBuffers", &run_string_buffer_tests);
    cumulative_result += run_isolated_suite("TOrderedCollection", &run_ordered_collection_tests);
    cumulative_result += run_isolated_suite("TUnorderedCollection", &run_unordered_collection_tests);
    cumulative_result += run_isolated_suite("TOwningTransport", &run_owning_transport_tests);
    cumulative_result += run_isolated_suite("TQueueTransport", &run_queue_transport_tests);
    cumulative_result += run_isolated_suite("TRingTransport", &run_ring_transport_tests);
    cumulative_result += run_isolated_suite("TMpmcTransport", &run_mpmc_transport_tests);

    if (mode >= ETestRunMode::core)
    {
        TOrderedConfig tlex_cfg;
        tlex_cfg.run_exhaustive_delete =
            mode >= ETestRunMode::moderate;
        if (mode >= ETestRunMode::full)
        {
            tlex_cfg.run_exhaustive_insert_delete = true;
            tlex_cfg.max_insert_perms = 0;
            tlex_cfg.max_delete_perms_each_insert = 0;
        }

        cumulative_result += run_isolated_suite(
            "TOrderedSlots", [&tlex_cfg]() { return run_all_tests(tlex_cfg); });
    }

    if (mode >= ETestRunMode::core)
    {
        TUnorderedConfig tun_cfg;

        cumulative_result += run_isolated_suite(
            "TUnorderedSlots", [&tun_cfg]() { return run_all_tests(tun_cfg); });
    }

    return cumulative_result;
}
