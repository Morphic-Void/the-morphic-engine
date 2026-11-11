
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   file_utils.cpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Platform native file handling utilities
//
//  Notes:
//  - Requires C++ 17 or greater
//  - No exceptions.

#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
    #include "platform/windows_include.hpp"
    #include <io.h>
    #include <fcntl.h>
#else   //  default, assumes _POSIX_VERSION or __APPLE__ or__linux__ or similar
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace platform::filesystem
{

std::FILE* openFile(const path::NativePath& file_path, const EOpenMode mode) noexcept
{
    std::FILE* handle = nullptr;
    if (!file_path.is_empty())
    {
        const std::uint8_t mode_index = static_cast<std::uint8_t>(mode);
        if (mode_index < 5u)
        {
#if MV_PLATFORM_WINDOWS
            static const wchar_t* const modes[] = {L"rb", L"wb", L"ab", L"wt", L"at"};
            if (_wfopen_s(&handle, file_path.data(), modes[static_cast<std::uint8_t>(mode)]) != 0)
            {   //  file open failed, ensure that the handle is nullptr
                handle = nullptr;
            }
#else
            static const char* const modes[] = { "rb", "wb", "ab", "wt", "at" };
            handle = std::fopen(file_path.data(), modes[static_cast<std::uint8_t>(mode)]);
#endif
        }
    }
    return handle;
}

void removeFile(const path::NativePath& file_path) noexcept
{
    if (!file_path.is_empty())
    {
#if MV_PLATFORM_WINDOWS
        _wremove(file_path.data());
#else
        std::remove(file_path.data());
#endif
    }
}

bool renameFile(const path::NativePath& src_path, const path::NativePath& dst_path) noexcept
{
    bool success = false;
    if (!src_path.is_empty() && !dst_path.is_empty())
    {
#if MV_PLATFORM_WINDOWS
        success = MoveFileExW(src_path.data(), dst_path.data(), (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) != 0;
#else
        success = std::rename(src_path.data(), dst_path.data()) == 0;
#endif
    }
    return success;
}

// Best-effort "durable flush".
// Returns true only if both fflush + OS flush succeed.
bool flushToDisk(std::FILE* const handle) noexcept
{
    if ((handle != nullptr) && (std::fflush(handle) == 0))
    {
#if MV_PLATFORM_WINDOWS
        const int fd = _fileno(handle);
        if (fd >= 0)
        {   //  _get_osfhandle returns an intptr_t; INVALID_HANDLE_VALUE on failure.
            const intptr_t osfh = _get_osfhandle(fd);
            if (osfh != static_cast<intptr_t>(-1))
            {   //  not an invalid handle
                if (FlushFileBuffers(reinterpret_cast<HANDLE>(osfh)) != 0)
                {   //  FlushFileBuffers flushed the file's buffers to disk.
                    return true;
                }
            }
        }
#else
        const int fd = fileno(handle);
        if ((fd >= 0) && (fsync(fd) == 0))
        {   //  fsync flushed file data and metadata to disk.
            return true;
        }
#endif
    }
    return false;
}

bool queryFileSize(std::FILE* const handle, std::size_t& size) noexcept
{
    size = 0;
    if (handle != nullptr)
    {
#if MV_PLATFORM_WINDOWS
        if (_fseeki64(handle, 0, SEEK_END) == 0)
#elif defined(_POSIX_VERSION) || defined(__APPLE__) || defined(__linux__)
        if (fseeko(handle, 0, SEEK_END) == 0)
#else
        if (fseek(handle, 0, SEEK_END) == 0)
#endif
        {
#if MV_PLATFORM_WINDOWS
            const __int64 tell = _ftelli64(handle);
            if (_fseeki64(handle, 0, SEEK_SET) == 0)
#elif defined(_POSIX_VERSION) || defined(__APPLE__) || defined(__linux__)
            const off_t tell = ftello(handle);
            if (fseeko(handle, 0, SEEK_SET) == 0)
#else
            const long tell = ftell(handle);
            if (fseek(handle, 0, SEEK_SET) == 0)
#endif
            {
                if (tell >= 0)
                {
                    static const std::uint64_t max64 = static_cast<std::uint64_t>(~static_cast<std::size_t>(0));
                    std::uint64_t len64 = static_cast<std::uint64_t>(tell);
                    if (len64 <= max64)
                    {
                        size = static_cast<std::size_t>(len64);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

std::size_t getFileSize(std::FILE* const handle, const std::size_t pad) noexcept
{
    std::size_t size = 0;
    if (queryFileSize(handle, size) && (size != 0u) && (pad <= (~static_cast<std::size_t>(0) - size)))
    {
        return size + pad;
    }
    return 0u;
}

bool readFileContent(std::FILE* const handle, std::uint8_t* const data, const std::size_t size) noexcept
{
    return (handle != nullptr) && (data != nullptr) && (size != 0u) &&
        (std::fread(data, 1, size, handle) == size) && (std::ferror(handle) == 0);
}

}   //  namespace platform::filesystem
