
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
#include <limits>

#include "debug/log_path.hpp"
#include "host/runtime/host.hpp"
#include "host/system/host_context.hpp"
#include "platform/system/process_priority.hpp"

[[nodiscard]] static bool parse_count(const char* const text, std::uint32_t& count, const std::uint32_t minimum) noexcept
{
    if ((text == nullptr) || (*text == '\0'))
    {
        return false;
    }
    std::uint32_t value = 0u;
    for (const char* digit = text; *digit != '\0'; ++digit)
    {
        if ((*digit < '0') || (*digit > '9'))
        {
            return false;
        }
        const std::uint32_t next = static_cast<std::uint32_t>(*digit - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max() - next) / 10u)
        {
            return false;
        }
        value = (value * 10u) + next;
    }
    if (value < minimum)
    {
        return false;
    }
    count = value;
    return true;
}

[[nodiscard]] static bool parse_log_tag(const int argc, char** const argv, const char*& log_tag) noexcept
{
    constexpr char prefix[] = "--log-tag=";
    log_tag = nullptr;
    for (int index = 1; index < argc; ++index)
    {
        const char* const argument = argv[index];
        if ((argument != nullptr) && (std::strncmp(argument, prefix, (sizeof(prefix) - 1u)) == 0))
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
    const char* executive_file = "package:/bin/MorphicExecutive.dll";
    const char* log_directory = "development/logical-roots/logs";
    bool log_directory_option_seen = false;
    bool executive_option_seen = false;
    bool workers_option_seen = false;
    bool batch_option_seen = false;
    std::uint32_t worker_count = host::k_default_worker_count;
    std::uint32_t batch_runner_count = host::k_default_batch_runner_count;
    constexpr char workers_prefix[] = "--host-workers=";
    constexpr char batch_prefix[] = "--batch-runners=";
    constexpr char executive_prefix[] = "--executive=";
    constexpr char log_directory_prefix[] = "--log-directory=";
    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], workers_prefix, sizeof(workers_prefix) - 1u) == 0)
        {
            if (workers_option_seen || !parse_count((argv[index] + sizeof(workers_prefix) - 1u), worker_count, 1u))
            {
                std::fputs("Use one --host-workers=<positive integer> for the total I/O and conditioning worker count.\n", stderr);
                return 2;
            }
            workers_option_seen = true;
        }
        else if (std::strcmp(argv[index], "--host-workers") == 0)
        {
            std::fputs("Use --host-workers=<positive integer>.\n", stderr);
            return 2;
        }
        if (std::strncmp(argv[index], batch_prefix, (sizeof(batch_prefix) - 1u)) == 0)
        {
            if (batch_option_seen || !parse_count((argv[index] + sizeof(batch_prefix) - 1u), batch_runner_count, 0u))
            {
                std::fputs("Use one --batch-runners=<nonnegative integer>.\n", stderr);
                return 2;
            }
            batch_option_seen = true;
        }
        else if (std::strcmp(argv[index], "--batch-runners") == 0)
        {
            std::fputs("Use --batch-runners=<nonnegative integer>.\n", stderr);
            return 2;
        }
        if (std::strncmp(argv[index], log_directory_prefix, (sizeof(log_directory_prefix) - 1u)) == 0)
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
        if (std::strncmp(argv[index], executive_prefix, (sizeof(executive_prefix) - 1u)) == 0)
        {
            executive_file = argv[index] + sizeof(executive_prefix) - 1u;
            if (executive_option_seen || (*executive_file == '\0'))
            {
                std::fputs("Use one --executive=<logical DLL path>.\n", stderr);
                return 2;
            }
            executive_option_seen = true;
        }
    }
    const int host_result = host::host(log_tag, executive_file, log_directory, worker_count, batch_runner_count);
    platform::system::set_current_process_priority(platform::system::EProcessPriority::AboveNormal);

    return host_result;
}
