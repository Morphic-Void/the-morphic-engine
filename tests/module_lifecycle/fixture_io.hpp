
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture_io.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Native file/environment access shared by lifecycle fixture component tests.

#pragma once

#include "foundation/platform/platform_defines.hpp"
#include <cstdio>
#include <cstdlib>

namespace module_lifecycle_tests
{

[[nodiscard]] inline std::FILE* open_disposal_handoff(const char* const mode) noexcept
{
    constexpr char path[]{ "development/logical-roots/test-output/lifecycle-render-disposal.ids" };
#if MV_PLATFORM_WINDOWS
    std::FILE* file = nullptr;
    return (fopen_s(&file, path, mode) == 0) ? file : nullptr;
#else
    return std::fopen(path, mode);
#endif
}

[[nodiscard]] inline bool select_case(const char* const value) noexcept
{
#if MV_PLATFORM_WINDOWS
    return _putenv_s("MORPHIC_LIFECYCLE_CASE", value) == 0;
#else
    return setenv("MORPHIC_LIFECYCLE_CASE", value, 1) == 0;
#endif
}

}   //  namespace module_lifecycle_tests
