
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bootstrap_config.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Bounded Host startup settings loaded before engine services exist.

#pragma once

#ifndef HOST_BOOTSTRAP_CONFIG_HPP_INCLUDED
#define HOST_BOOTSTRAP_CONFIG_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "debug/log_path.hpp"

namespace host
{

inline constexpr std::size_t k_bootstrap_file_max_bytes = 4096u;
inline constexpr std::size_t k_bootstrap_value_capacity = 512u;

struct SBootstrapConfig
{
    char executive[k_bootstrap_value_capacity]{};
    char log_directory[k_bootstrap_value_capacity]{};
    char log_tag[debug_system::k_log_tag_max_length + 1u]{};
    std::uint32_t worker_count{ 0u };
    std::uint32_t batch_runner_count{ 0u };
};

struct SBootstrapError
{
    std::uint32_t line{ 0u };
    const char* message{ nullptr };
};

[[nodiscard]] bool load_bootstrap_config(const char* path, SBootstrapConfig& config, SBootstrapError& error) noexcept;

}   //  namespace host

#endif  //  HOST_BOOTSTRAP_CONFIG_HPP_INCLUDED
