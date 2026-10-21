//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    transport_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef MORPHIC_TEST_TRANSPORT_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_TRANSPORT_HELPERS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tests
{

inline std::string make_repeated_string(const char c, const std::uint32_t count)
{
    return std::string(static_cast<std::size_t>(count), c);
}

template<typename TTransport>
bool post_string(TTransport& transport, const std::string_view text) noexcept
{
    return transport.post(text.data(), static_cast<std::uint32_t>(text.size()));
}

template<typename TTransport>
bool read_exact_string(TTransport& transport, std::string& out, const std::uint32_t count)
{
    out.resize(static_cast<std::size_t>(count));
    if (!transport.read(out.data(), count))
    {
        out.clear();
        return false;
    }
    return true;
}

}   //  namespace tests

#endif  //  MORPHIC_TEST_TRANSPORT_HELPERS_HPP_INCLUDED
