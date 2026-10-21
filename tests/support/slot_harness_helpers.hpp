
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    slot_harness_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef MORPHIC_TEST_SLOT_HARNESS_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_SLOT_HARNESS_HELPERS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#   define MORPHIC_SLOT_TEST_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#   define MORPHIC_SLOT_TEST_DEBUG_BREAK() __builtin_trap()
#else
#   define MORPHIC_SLOT_TEST_DEBUG_BREAK() std::abort()
#endif

namespace tests::slot_harness
{

struct TestLogger
{
    bool stop_on_fail = true;
    bool debug_break_on_fail =
#if !defined(NDEBUG)
        true;
#else
        false;
#endif
    std::uint32_t failures = 0;

    void fail(const std::string& msg) noexcept
    {
        ++failures;
        std::cerr << "FAIL: " << msg << "\n";
        if (debug_break_on_fail) { MORPHIC_SLOT_TEST_DEBUG_BREAK(); }
        if (stop_on_fail) { std::abort(); }
    }
};

//  Contract violations always terminate, independently of the logger policy.
[[noreturn]] inline void hard_fail_contract(const char* msg) noexcept
{
    std::cerr << "FAIL (contract): " << msg << "\n";
    MORPHIC_SLOT_TEST_DEBUG_BREAK();
    std::abort();
}

inline bool starts_with(const std::string& s, const char* prefix)
{
    const std::size_t n = std::strlen(prefix);
    return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

inline bool parse_i64(const std::string& s, std::int64_t& out_v)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out_v = static_cast<std::int64_t>(v);
    return true;
}

template<typename TValue>
std::string vec_to_string(const std::vector<TValue>& v)
{
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        if (i) oss << ", ";
        oss << v[i];
    }
    oss << "]";
    return oss.str();
}

template<typename TRecord>
void push_history(std::vector<TRecord>& history, const std::uint32_t limit, const TRecord& record)
{
    if (limit == 0) return;
    if (history.size() < limit) { history.push_back(record); return; }
    for (std::size_t i = 1; i < history.size(); ++i) history[i - 1] = history[i];
    history.back() = record;
}

}   //  namespace tests::slot_harness

#undef MORPHIC_SLOT_TEST_DEBUG_BREAK

#endif  //  MORPHIC_TEST_SLOT_HARNESS_HELPERS_HPP_INCLUDED
