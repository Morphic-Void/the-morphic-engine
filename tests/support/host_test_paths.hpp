
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_test_paths.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    20 Oct 26

#pragma once

#include <filesystem>
#include <string>
#include "containers/StringBuffers.hpp"

namespace host_test_paths
{

//  Host acceptance harnesses run with their staged runtime as the current directory.
//  Keep this separate from the ordinary runner's process/tag-qualified output paths.
[[nodiscard]] static bool native_output(const char* filename, CSimpleString& destination)
{
    std::error_code error;
    const auto path = std::filesystem::absolute(
        std::filesystem::path("development/logical-roots/test-output") / filename, error);
    if (error) return false;
    const auto utf8 = path.generic_u8string();
    const std::string request = "native:" + std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
    return destination.set(request.c_str());
}

}   //  namespace host_test_paths
