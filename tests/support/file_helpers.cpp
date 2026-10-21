
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    file_helpers.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#include "tests/support/file_helpers.hpp"

#include <cstdio>
#include <cstring>

#include "platform/filesystem/file.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/path/native_path.hpp"

namespace tests
{

bool write_test_file(const std::string& path, const void* const bytes, const std::size_t size)
{
    return platform::filesystem::saveFile(path.c_str(), CByteConstView{ static_cast<const std::uint8_t*>(bytes), size });
}

bool write_test_file(const std::string& path, const char* const text)
{
    return write_test_file(path, text, std::strlen(text));
}

bool file_contains(
    const char* const path, const char* const expected) noexcept
{
    if ((path == nullptr) || (expected == nullptr) || (expected[0] == 0))
    {
        return false;
    }

    const platform::path::NativePath native_path =
        platform::path::makeNativePath(path);
    std::FILE* const stream = platform::filesystem::openFile(
        native_path, platform::filesystem::EOpenMode::BinaryRead);
    if (stream == nullptr)
    {
        return false;
    }

    constexpr std::size_t buffer_capacity = 8192u;
    char buffer[buffer_capacity]{};
    const std::size_t expected_size = std::strlen(expected);
    if (expected_size >= buffer_capacity)
    {
        std::fclose(stream);
        return false;
    }

    std::size_t retained = 0u;
    for (;;)
    {
        const std::size_t read = std::fread(
            buffer + retained, 1u,
            buffer_capacity - retained - 1u, stream);
        const std::size_t available = retained + read;
        buffer[available] = 0;
        if (std::strstr(buffer, expected) != nullptr)
        {
            std::fclose(stream);
            return true;
        }
        if (read == 0u)
        {
            std::fclose(stream);
            return false;
        }

        const std::size_t overlap = expected_size - 1u;
        retained = (available < overlap) ? available : overlap;
        std::memmove(buffer, buffer + available - retained, retained);
    }
}

}   //  namespace tests
