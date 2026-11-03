
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_identity.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Exact identity of one installed module instance.

#pragma once

#include <cstdint>

namespace modules
{

struct SModuleInstance { std::uint64_t value{ 0u }; };

}   //  namespace modules
