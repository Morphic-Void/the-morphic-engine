
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    operation_meter.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Private synchronous scope hooks for shared schema helpers in bounded operations.

#pragma once

#include <cstdint>
#include <algorithm>

class CLiveDocument;

namespace schema::detail
{

//  No owner is retained. CDataOperationScope installs/restores the calling
//  operation on this thread, including nested legacy helpers. Without a bounded
//  caller these checks are no-ops, preserving existing reference-free APIs.
[[nodiscard]] bool data_work(const std::uint64_t credits = 1u) noexcept;
[[nodiscard]] bool data_bytes(const std::uint64_t bytes) noexcept;
[[nodiscard]] bool data_nodes(const std::uint64_t nodes) noexcept;
[[nodiscard]] bool data_depth(const std::uint32_t depth) noexcept;
[[nodiscard]] bool data_output(const std::uint64_t bytes) noexcept;
[[nodiscard]] bool data_healthy() noexcept;
[[nodiscard]] bool data_active() noexcept;
[[nodiscard]] bool admit_data_document(const CLiveDocument& document) noexcept;
[[nodiscard]] bool prepare_data_document_bake(const CLiveDocument& document) noexcept;

//  Stop before the next comparison when the operation is exhausted. A standard
//  sort cannot abort its comparator without violating strict weak ordering.
template<class T, class FCompare>
[[nodiscard]] bool data_sort(T* const begin, T* const end, const FCompare& compare) noexcept
{
    if (!data_active())
    {
        std::sort(begin, end, compare);
        return true;
    }
    const std::size_t count = static_cast<std::size_t>(end - begin);
    const auto sift = [&](std::size_t root, const std::size_t size) noexcept
    {
        while (root < (size >> 1u))
        {
            std::size_t child = (root << 1u) + 1u;
            if ((child + 1u) < size)
            {
                if (!data_work()) return false;
                const bool right = compare(begin[child], begin[child + 1u]);
                if (!data_healthy()) return false;
                if (right) ++child;
            }
            if (!data_work()) return false;
            const bool move = compare(begin[root], begin[child]);
            if (!data_healthy()) return false;
            if (!move) break;
            std::swap(begin[root], begin[child]);
            root = child;
        }
        return true;
    };
    for (std::size_t root = count >> 1u; root; --root)
    {
        if (!sift((root - 1u), count)) return false;
    }
    for (std::size_t size = count; size > 1u; --size)
    {
        if (!data_work()) return false;
        std::swap(begin[0], begin[size - 1u]);
        if (!sift(0u, (size - 1u))) return false;
    }
    return true;
}

}   // namespace schema::detail
