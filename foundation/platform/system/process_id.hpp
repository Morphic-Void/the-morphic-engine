
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    process_id.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Native process identifier query.

#pragma once

#ifndef PROCESS_ID_HPP_INCLUDED
#define PROCESS_ID_HPP_INCLUDED

#include <cstdint>      //  std::uint64_t

namespace platform::system
{

class CPlatformProcessId
{
public:
    constexpr CPlatformProcessId() noexcept = default;
    explicit constexpr CPlatformProcessId(const std::uint64_t value) noexcept : m_value(value) {}

    constexpr bool is_valid() const noexcept { return m_value != 0u; }
    constexpr std::uint64_t value() const noexcept { return m_value; }

    constexpr bool operator==(const CPlatformProcessId rhs) const noexcept { return m_value == rhs.m_value; }
    constexpr bool operator!=(const CPlatformProcessId rhs) const noexcept { return m_value != rhs.m_value; }

private:
    std::uint64_t m_value{ 0u };
};

//  Returns the native OS identifier for the calling process. Native process
//  identifiers may be reused after the corresponding process exits.
CPlatformProcessId query_current_process_id() noexcept;

}   //  namespace platform::system

#endif  //  #ifndef PROCESS_ID_HPP_INCLUDED
