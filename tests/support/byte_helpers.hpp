
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    byte_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef MORPHIC_TEST_BYTE_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_BYTE_HELPERS_HPP_INCLUDED

#include <cstring>
#include <string>
#include <vector>

#include "containers/ByteBuffers.hpp"

namespace tests
{

//  Compare exact content; empty ranges need no readable storage.
[[nodiscard]] inline bool same_bytes(const CByteConstView actual, const CByteConstView expected) noexcept
{
    return actual.size() == expected.size() &&
        ((actual.size() == 0u) || (std::memcmp(actual.data(), expected.data(), actual.size()) == 0));
}

//  Borrow snapshot storage for this call, without changing its type or lifetime.
//  Check the original size before constructing a view, which can reject an oversized range.
[[nodiscard]] inline bool same_bytes(const CByteConstView actual, const std::string& expected) noexcept
{
    return actual.size() == expected.size() && same_bytes(actual, CByteConstView{
        reinterpret_cast<const std::uint8_t*>(expected.data()), expected.size(), 1u });
}

[[nodiscard]] inline bool same_bytes(const CByteConstView actual, const std::vector<std::uint8_t>& expected) noexcept
{
    return actual.size() == expected.size() && same_bytes(actual, CByteConstView{
        expected.data(), expected.size(), 1u });
}

}   //  namespace tests

#endif  //  MORPHIC_TEST_BYTE_HELPERS_HPP_INCLUDED
