
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   log.hpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Basic log file class

#pragma once

#ifndef LOG_HPP_INCLUDED
#define LOG_HPP_INCLUDED

#include <cstdarg>
#include <cstdio>

namespace platform::filesystem
{

class Log
{
public:
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    Log() noexcept = default;
    explicit Log(const char* const utf8_path) noexcept { (void)open(utf8_path); }
    Log(Log&& other) noexcept : m_stream(other.m_stream) { other.m_stream = nullptr; }

    ~Log() noexcept { close(); }

    Log& operator=(Log&& other) noexcept;

    bool opened() const noexcept { return m_stream != nullptr; }
    bool closed() const noexcept { return m_stream == nullptr; }

    bool open(const char* const utf8_path, const bool append = false) noexcept;

    //  write text to the log, return is < 0 on failure
    int  write(const char* const format, ...) noexcept;
    int  write_va(const char* const format, std::va_list args) noexcept;

    //  on-demand: flush stdio only.
    bool flush() noexcept;

    //  on-demand: flush stdio + request OS flush (best-effort durability).
    bool flush_durable() noexcept;

    void close() noexcept;

private:
    std::FILE* m_stream = nullptr;
};

//==============================================================================
//  Log out of class function bodies
//==============================================================================

inline Log& Log::operator=(Log&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_stream = other.m_stream;
        other.m_stream = nullptr;
    }
    return *this;
}

}   //  namespace platform::filesystem

#endif  //  #ifndef LOG_HPP_INCLUDED
