
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bootstrap_config.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Reads and validates a small bootstrap file without engine services.

#include <cstdio>
#include <cstring>
#include <limits>

#include "host/entry/bootstrap_config.hpp"
#include "host/runtime/batch_pool.hpp"
#include "host/runtime/worker_policy.hpp"

namespace host
{

namespace bootstrap_config_detail
{

[[nodiscard]] static bool fail(SBootstrapError& error, const std::uint32_t line, const char* const message) noexcept
{
    error.line = line;
    error.message = message;
    return false;
}

[[nodiscard]] static bool copy_value(char* const destination, const std::size_t capacity, const char* const value) noexcept
{
    const std::size_t length = std::strlen(value);
    if ((length == 0u) || (length >= capacity) || (value[0] == ' ') || (value[length - 1u] == ' '))
    {
        return false;
    }
    for (std::size_t index = 0u; index < length; ++index)
    {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        if ((byte < 0x20u) || (byte == 0x7fu))
        {
            return false;
        }
    }
    std::memcpy(destination, value, length + 1u);
    return true;
}

[[nodiscard]] static bool parse_count(const char* const text, std::uint32_t& count, const std::uint32_t minimum) noexcept
{
    if (*text == '\0')
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

struct SSeen
{
    bool executive{ false };
    bool log_directory{ false };
    bool log_tag{ false };
    bool workers{ false };
    bool batch{ false };
};

[[nodiscard]] static bool parse_line(char* const line, const std::uint32_t number, SBootstrapConfig& config, SSeen& seen, SBootstrapError& error) noexcept
{
    if ((*line == '\0') || (*line == '#'))
    {
        return true;
    }
    char* const equals = std::strchr(line, '=');
    if ((equals == nullptr) || (equals == line))
    {
        return fail(error, number, "expected key=value");
    }
    *equals = '\0';
    const char* const value = equals + 1;
    if (std::strcmp(line, "executive") == 0)
    {
        if (seen.executive || !copy_value(config.executive, sizeof(config.executive), value))
        {
            return fail(error, number, "invalid or duplicate executive");
        }
        seen.executive = true;
    }
    else if (std::strcmp(line, "log-directory") == 0)
    {
        if (seen.log_directory || !copy_value(config.log_directory, sizeof(config.log_directory), value))
        {
            return fail(error, number, "invalid or duplicate log-directory");
        }
        seen.log_directory = true;
    }
    else if (std::strcmp(line, "log-tag") == 0)
    {
        if (seen.log_tag || !debug_system::is_valid_log_tag(value) ||
            !copy_value(config.log_tag, sizeof(config.log_tag), value))
        {
            return fail(error, number, "invalid or duplicate log-tag");
        }
        seen.log_tag = true;
    }
    else if (std::strcmp(line, "host-workers") == 0)
    {
        if (seen.workers || !parse_count(value, config.worker_count, 1u))
        {
            return fail(error, number, "invalid or duplicate host-workers");
        }
        seen.workers = true;
    }
    else if (std::strcmp(line, "batch-runners") == 0)
    {
        if (seen.batch || !parse_count(value, config.batch_runner_count, 0u))
        {
            return fail(error, number, "invalid or duplicate batch-runners");
        }
        seen.batch = true;
    }
    else
    {
        return fail(error, number, "unknown setting");
    }
    return true;
}

}   //  namespace bootstrap_config_detail

bool load_bootstrap_config(const char* const path, SBootstrapConfig& config, SBootstrapError& error) noexcept
{
    error = {};
    if ((path == nullptr) || (*path == '\0'))
    {
        return bootstrap_config_detail::fail(error, 0u, "missing configuration path");
    }
    std::FILE* file = nullptr;
#if defined(_WIN32)
    (void)::fopen_s(&file, path, "rb");
#else
    file = std::fopen(path, "rb");
#endif
    if (file == nullptr)
    {
        return bootstrap_config_detail::fail(error, 0u, "cannot open file");
    }
    char buffer[k_bootstrap_file_max_bytes + 1u]{};
    const std::size_t size = std::fread(buffer, 1u, sizeof(buffer), file);
    const bool read_failed = (std::ferror(file) != 0);
    const bool close_failed = (std::fclose(file) != 0);
    if (read_failed || close_failed)
    {
        return bootstrap_config_detail::fail(error, 0u, "cannot read file");
    }
    if (size > k_bootstrap_file_max_bytes)
    {
        return bootstrap_config_detail::fail(error, 0u, "file exceeds 4096 bytes");
    }
    SBootstrapConfig parsed{};
    constexpr char default_log_directory[] = "development/logical-roots/logs";
    static_assert(sizeof(default_log_directory) <= sizeof(parsed.log_directory));
    std::memcpy(parsed.log_directory, default_log_directory, sizeof(default_log_directory));
    parsed.worker_count = k_default_worker_count;
    parsed.batch_runner_count = k_default_batch_runner_count;

    std::size_t cursor = 0u;
    if ((size >= 3u) && (static_cast<unsigned char>(buffer[0]) == 0xefu) &&
        (static_cast<unsigned char>(buffer[1]) == 0xbbu) &&
        (static_cast<unsigned char>(buffer[2]) == 0xbfu))
    {
        cursor = 3u;
    }
    bootstrap_config_detail::SSeen seen;
    std::uint32_t line_number = 1u;
    while (cursor < size)
    {
        const std::size_t start = cursor;
        while ((cursor < size) && (buffer[cursor] != '\n'))
        {
            if (buffer[cursor] == '\0')
            {
                return bootstrap_config_detail::fail(error, line_number, "NUL byte in file");
            }
            ++cursor;
        }
        std::size_t end = cursor;
        if (cursor < size) ++cursor;
        if ((end > start) && (buffer[end - 1u] == '\r')) --end;
        buffer[end] = '\0';
        if (!bootstrap_config_detail::parse_line(buffer + start, line_number, parsed, seen, error))
        {
            return false;
        }
        ++line_number;
    }
    if (!seen.executive)
    {
        return bootstrap_config_detail::fail(error, 0u, "executive setting is required");
    }
    config = parsed;
    return true;
}

}   //  namespace host
