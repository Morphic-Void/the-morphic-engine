
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    system_erased_owner_operations.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  SYSTEM owner operations compiled separately into each participating binary.

#pragma once

#include "system/erased_owner_operations.hpp"

namespace erased_owner_operations
{

[[nodiscard]] const SCategoryView& system_operations_view() noexcept;

}   //  namespace erased_owner_operations
