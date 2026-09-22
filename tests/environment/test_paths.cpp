//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#include "tests/environment/test_paths.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>

#include "debug/log_path.hpp"
#include "debug/service.hpp"
#include "platform/system/process_id.hpp"

namespace test_environment
{
namespace
{
std::filesystem::path s_repository_root;
std::filesystem::path s_binary_directory;
std::filesystem::path s_log_directory;
std::filesystem::path s_output_directory;
std::string s_repository_root_text;
std::string s_log_tag;
std::string s_log_path_pattern;
platform::system::CPlatformProcessId s_process_id;

bool is_repository_root(const std::filesystem::path& candidate)
{
    std::error_code error;
    return std::filesystem::is_regular_file(candidate / "development" / "root-manifest.json", error);
}

std::filesystem::path find_repository_root(std::filesystem::path candidate)
{
    std::error_code error;
    candidate = std::filesystem::absolute(candidate, error);
    if (error)
    {
        return {};
    }

    for (;;)
    {
        if (is_repository_root(candidate))
        {
            return candidate.lexically_normal();
        }
        const std::filesystem::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate)
        {
            return {};
        }
        candidate = parent;
    }
}
}

bool initialise_paths(
    const char* const executable_argument,
    const char* const log_tag,
    const char* const output_directory)
{
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    if (error)
    {
        return false;
    }

    s_repository_root.clear();
    s_binary_directory.clear();
    s_log_directory.clear();
    s_output_directory.clear();
    s_repository_root_text.clear();
    s_log_tag.clear();
    s_log_path_pattern.clear();
    s_process_id = platform::system::CPlatformProcessId();

    if ((log_tag != nullptr) && !debug_system::is_valid_log_tag(log_tag))
    {
        return false;
    }

    if ((executable_argument != nullptr) && (executable_argument[0] != 0))
    {
        std::filesystem::path executable(executable_argument);
        executable = std::filesystem::absolute(executable, error);
        if (!error)
        {
            s_binary_directory = executable.parent_path().lexically_normal();
            s_repository_root = find_repository_root(s_binary_directory);
        }
    }

    if (s_repository_root.empty())
    {
        s_repository_root = find_repository_root(current);
    }
    if (s_binary_directory.empty())
    {
        s_binary_directory = current.lexically_normal();
    }
    if (s_repository_root.empty())
    {
        return false;
    }

    s_process_id = platform::system::query_current_process_id();
    if (!s_process_id.is_valid())
    {
        return false;
    }
    if (log_tag != nullptr)
    {
        s_log_tag = log_tag;
    }
    std::filesystem::path output_path;
    if ((output_directory != nullptr) && (output_directory[0] != 0))
    {
        output_path = std::filesystem::absolute(
            std::filesystem::path(output_directory), error);
        if (error)
        {
            return false;
        }
        s_log_directory = output_path.lexically_normal() / "logs";
    }
    else
    {
        output_path = s_repository_root / "development" / "logical-roots" / "test-output";
        s_log_directory = s_repository_root / "development" / "logical-roots" / "test-logs";
    }
    s_output_directory = output_path.lexically_normal();
    std::filesystem::create_directories(s_output_directory, error);
    if (error)
    {
        return false;
    }
    std::filesystem::create_directories(s_log_directory, error);
    if (error)
    {
        return false;
    }

    s_repository_root_text = s_repository_root.string();
    s_log_path_pattern = test_log_path("*");
    if (s_log_path_pattern.empty())
    {
        return false;
    }
    std::filesystem::current_path(s_repository_root, error);
    if (error)
    {
        return false;
    }
    return true;
}

const std::string& repository_root() noexcept
{
    return s_repository_root_text;
}

std::string repository_path(const char* const relative_path)
{
    if ((relative_path == nullptr) || s_repository_root.empty())
    {
        return {};
    }
    return (s_repository_root / std::filesystem::path(relative_path)).lexically_normal().string();
}

std::string binary_path(const char* const filename)
{
    if ((filename == nullptr) || s_binary_directory.empty())
    {
        return {};
    }
    return (s_binary_directory / std::filesystem::path(filename)).lexically_normal().string();
}

std::string test_log_path(const char* const stem)
{
    if ((stem == nullptr) || (stem[0] == '\0') || s_log_directory.empty() || !s_process_id.is_valid())
    {
        return {};
    }

    const std::string absolute_stem =
        (s_log_directory / std::filesystem::path(stem)).lexically_normal().string();
    std::array<char, debug_system::k_log_path_capacity> path{};
    const char* const tag = s_log_tag.empty() ? nullptr : s_log_tag.c_str();
    if (!debug_system::format_process_log_path(
        path.data(), path.size(), absolute_stem.c_str(), tag, s_process_id.value()))
    {
        return {};
    }
    return path.data();
}

const std::string& log_path_pattern() noexcept
{
    return s_log_path_pattern;
}

std::string test_output_path(const char* const filename)
{
    if ((filename == nullptr) || (filename[0] == '\0') || s_output_directory.empty() || !s_process_id.is_valid())
    {
        return {};
    }
    const std::filesystem::path name(filename);
    const std::string stem = (s_output_directory / name.stem()).string();
    std::array<char, debug_system::k_log_path_capacity> path{};
    const char* const tag = s_log_tag.empty() ? nullptr : s_log_tag.c_str();
    if (!debug_system::format_process_log_path(
        path.data(), path.size(), stem.c_str(), tag, s_process_id.value()))
    {
        return {};
    }
    return std::filesystem::path(path.data()).replace_extension(name.extension()).string();
}

}   //  namespace test_environment
