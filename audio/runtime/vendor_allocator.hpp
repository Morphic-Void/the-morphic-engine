
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    vendor_allocator.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Explicit-context vendor allocation bridge; no ambient thread context.

#pragma once
#include <cstddef>
#include "memory/memory_context.hpp"

namespace audio
{
void* vendor_allocate(std::size_t bytes, void* context) noexcept;
void* vendor_reallocate(void* block, std::size_t bytes, void* context) noexcept;
void vendor_free(void* block, void* context) noexcept;
}   //  namespace audio
