
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    vendor_allocator.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Aligned metadata retains the exact allocation size through realloc/free.

#include "vendor_allocator.hpp"
#include <cstring>
#include <limits>

namespace audio
{
struct alignas(std::max_align_t) SAllocationHeader { std::size_t bytes; };

void* vendor_allocate(const std::size_t bytes, void* const context) noexcept
{
    if ((context == nullptr) || (bytes == 0u) ||
        (bytes > std::numeric_limits<std::size_t>::max() - sizeof(SAllocationHeader) - alignof(SAllocationHeader))) return nullptr;
    auto& memory = *static_cast<memory::CMemoryContext*>(context);
    if (memory.condition_bytes(memory.condition_alignment(alignof(SAllocationHeader)), sizeof(SAllocationHeader) + bytes) == 0u) return nullptr;
    auto* header = static_cast<SAllocationHeader*>(memory.allocate(alignof(SAllocationHeader), sizeof(SAllocationHeader) + bytes));
    if (header == nullptr) return nullptr;
    header->bytes = bytes;
    return header + 1;
}

void vendor_free(void* const block, void* const context) noexcept
{
    if (block == nullptr) return;
    auto* header = static_cast<SAllocationHeader*>(block) - 1;
    static_cast<memory::CMemoryContext*>(context)->deallocate(alignof(SAllocationHeader), sizeof(SAllocationHeader) + header->bytes, header);
}

void* vendor_reallocate(void* const block, const std::size_t bytes, void* const context) noexcept
{
    if (bytes == 0u)
    {
        vendor_free(block, context);
        return nullptr;
    }
    void* const replacement = vendor_allocate(bytes, context);
    if (replacement == nullptr) return nullptr;
    if (block != nullptr)
    {
        const std::size_t old_bytes = (static_cast<SAllocationHeader*>(block) - 1)->bytes;
        std::memcpy(replacement, block, (old_bytes < bytes) ? old_bytes : bytes);
        vendor_free(block, context);
    }
    return replacement;
}
}   //  namespace audio
