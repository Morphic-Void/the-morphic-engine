
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

#include <cstdio>

#include "host/entry/bootstrap_config.hpp"
#include "host/runtime/host.hpp"
#include "host/system/host_context.hpp"
#include "platform/system/process_priority.hpp"

int main(const int argc, char** const argv)
{
    if ((argc != 2) || (argv[1] == nullptr) || (*argv[1] == '\0'))
    {
        std::fputs("Usage: MorphicEngine <bootstrap-configuration-file>\n", stderr);
        return 2;
    }
    host::SBootstrapConfig config;
    host::SBootstrapError error;
    if (!host::load_bootstrap_config(argv[1], config, error))
    {
        if (error.line == 0u)
        {
            std::fprintf(stderr, "Invalid bootstrap configuration '%s': %s.\n", argv[1], error.message);
        }
        else
        {
            std::fprintf(stderr, "Invalid bootstrap configuration '%s' at line %u: %s.\n", argv[1], error.line, error.message);
        }
        return 2;
    }
    if (!host::host_context_install())
    {
        return 1;
    }
    const char* const log_tag = (*config.log_tag != '\0') ? config.log_tag : nullptr;
    const int host_result = host::host(log_tag, config.executive, config.log_directory, config.worker_count, config.batch_runner_count);
    platform::system::set_current_process_priority(platform::system::EProcessPriority::AboveNormal);
    return host_result;
}
