
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   log.cpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Basic log file class

#include "platform/filesystem/log.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/path/native_path.hpp"

namespace platform::filesystem
{

bool Log::open(const char* const utf8_path, const bool append) noexcept
{
    close();
    if (utf8_path != nullptr)
    {
        path::NativePath std_path = path::makeNativePath(utf8_path);
        if (!std_path.is_empty())
        {
            m_stream = openFile(std_path, (append ? EOpenMode::TextAppend : EOpenMode::TextWrite));
        }
    }
    return m_stream != nullptr;
}

int Log::write(const char* const format, ...) noexcept
{
    int ret = -1;
    if ((m_stream != nullptr) && (format != nullptr))
    {
        std::va_list args;
        va_start(args, format);
        ret = std::vfprintf(m_stream, format, args);
        va_end(args);
    }
    return ret;   //  ret < 0 on failure
}

int Log::write_va(const char* const format, std::va_list args) noexcept
{
    int ret = -1;
    if ((m_stream != nullptr) && (format != nullptr))
    {
        ret = std::vfprintf(m_stream, format, args);
    }
    return ret;   //  ret < 0 on failure
}

bool Log::flush() noexcept
{
    return (m_stream != nullptr) && (std::fflush(m_stream) == 0);
}

bool Log::flush_durable() noexcept
{
    return (m_stream != nullptr) ? flushToDisk(m_stream) : false;
}

void Log::close() noexcept
{
    if (m_stream != nullptr)
    {
        flushToDisk(m_stream);
        std::fclose(m_stream);
        m_stream = nullptr;
    }
}

}	//	namespace platform::filesystem
