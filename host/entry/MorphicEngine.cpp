
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:    MorphicEngine.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    24 Apr 26
//
//  The main() function.
//  This is the entry point for the host thread.
//  Program execution begins and ends here.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "debug/log_path.hpp"
#include "host/runtime/host.hpp"
#include "host/system/host_context.hpp"
#include "platform/system/process_priority.hpp"
#include "platform/threading/hw_thread_count.hpp"

[[nodiscard]] static bool parse_log_tag(const int argc, char** const argv, const char*& log_tag) noexcept
{
    constexpr char prefix[] = "--log-tag=";
    log_tag = nullptr;
    for (int index = 1; index < argc; ++index)
    {
        const char* const argument = argv[index];
        if ((argument != nullptr) && (std::strncmp(argument, prefix, sizeof(prefix) - 1u) == 0))
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

int main(const int argc, char** const argv)
{
    const char* log_tag = nullptr;
    if (!parse_log_tag(argc, argv, log_tag))
    {
        std::fputs("Invalid --log-tag. Use --log-tag=<value> with 1-48 ASCII letters, digits, '.', '_' or '-'.\n", stderr);
        return 2;
    }
    if (!host::host_context_install())
    {
        return 1;
    }
    const char* executive_file = "MorphicExecutive.dll";
    const char* log_directory = "development/logical-roots/logs";
    bool log_directory_option_seen = false;
    bool executive_option_seen = false;
    constexpr char executive_prefix[] = "--executive=";
    constexpr char log_directory_prefix[] = "--log-directory=";
    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], log_directory_prefix, sizeof(log_directory_prefix) - 1u) == 0)
        {
            log_directory = argv[index] + sizeof(log_directory_prefix) - 1u;
            if (log_directory_option_seen || (*log_directory == '\0'))
            {
                std::fputs("Use one --log-directory=<existing directory>.\n", stderr);
                return 2;
            }
            log_directory_option_seen = true;
        }
        else if (std::strcmp(argv[index], "--log-directory") == 0)
        {
            std::fputs("Use --log-directory=<existing directory>.\n", stderr);
            return 2;
        }
        if (std::strncmp(argv[index], executive_prefix, sizeof(executive_prefix) - 1u) == 0)
        {
            executive_file = argv[index] + sizeof(executive_prefix) - 1u;
            if (executive_option_seen || (*executive_file == '\0'))
            {
                std::fputs("Use one --executive=<DLL path>.\n", stderr);
                return 2;
            }
            executive_option_seen = true;
        }
    }
    const int host_result = host::host(log_tag, executive_file, log_directory);
    platform::system::set_current_process_priority(platform::system::EProcessPriority::AboveNormal);
    const std::uint32_t hw_threads_supported = platform::threading::query_hardware_thread_count();
    (void)hw_threads_supported;

    return host_result;
}
