
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    directory.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Sep 26
//
//  Native directory enumeration and running executable location.

#include <cstring>
#include "platform/filesystem/directory.hpp"
#include "platform/path/native_path.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#else
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platform::filesystem
{

#if MV_PLATFORM_WINDOWS
static bool utf8(const wchar_t* const source, const int length, CSimpleString& destination) noexcept
{
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, length, nullptr, 0, nullptr, nullptr);
    TPodVector<char> buffer;
    if ((bytes <= 0) || !buffer.resize(static_cast<std::size_t>(bytes) + 1u))
    {
        return false;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, length, buffer.data(), bytes, nullptr, nullptr) != bytes)
    {
        return false;
    }
    buffer[static_cast<std::size_t>(bytes)] = '\0';
    return destination.set(buffer.data());
}
#endif

EDirectoryQuery query_directory(const char* const path, const FDirectoryVisitor visitor, void* const context) noexcept
{
    if ((path == nullptr) || (visitor == nullptr))
    {
        return EDirectoryQuery::open_failed;
    }
#if MV_PLATFORM_WINDOWS
    platform::path::NativePath native = platform::path::makeNativePath(path);
    if (native.is_empty())
    {
        return EDirectoryQuery::open_failed;
    }
    const DWORD attributes = GetFileAttributesW(native.data());
    if ((attributes == INVALID_FILE_ATTRIBUTES) || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return EDirectoryQuery::open_failed;
    }
    const std::size_t end = native.size() - 1u;
    if (!native.resize(end + 3u))
    {
        return EDirectoryQuery::open_failed;
    }
    native[end] = L'/';
    native[end + 1u] = L'*';
    native[end + 2u] = L'\0';
    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW(native.data(), &data);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return (GetLastError() == ERROR_FILE_NOT_FOUND) ? EDirectoryQuery::complete : EDirectoryQuery::open_failed;
    }
    EDirectoryQuery result = EDirectoryQuery::complete;
    do
    {
        if ((data.cFileName[0] == L'.') && ((data.cFileName[1] == L'\0') ||
            ((data.cFileName[1] == L'.') && (data.cFileName[2] == L'\0'))))
        {
            continue;
        }
        CSimpleString name;
        if (!utf8(data.cFileName, -1, name))
        {
            result = EDirectoryQuery::read_failed;
            break;
        }
        const EDirectoryEntry kind = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? EDirectoryEntry::other :
            ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? EDirectoryEntry::directory : EDirectoryEntry::file);
        if (!visitor(context, name.cstring(), kind))
        {
            result = EDirectoryQuery::visitor_failed;
            break;
        }
    } while (FindNextFileW(handle, &data));
    if ((result == EDirectoryQuery::complete) && (GetLastError() != ERROR_NO_MORE_FILES))
    {
        result = EDirectoryQuery::read_failed;
    }
    if (!FindClose(handle))
    {
        result = EDirectoryQuery::read_failed;
    }
    return result;
#else
    struct stat root{};
    if ((lstat(path, &root) != 0) || !S_ISDIR(root.st_mode))
    {
        return EDirectoryQuery::open_failed;
    }
    DIR* handle = opendir(path);
    if (handle == nullptr)
    {
        return EDirectoryQuery::open_failed;
    }
    EDirectoryQuery result = EDirectoryQuery::complete;
    for (;;)
    {
        errno = 0;
        const dirent* entry = readdir(handle);
        if (entry == nullptr)
        {
            result = (errno == 0) ? EDirectoryQuery::complete : EDirectoryQuery::read_failed;
            break;
        }
        if ((std::strcmp(entry->d_name, ".") == 0) || (std::strcmp(entry->d_name, "..") == 0))
        {
            continue;
        }
        TPodVector<char> full;
        const std::size_t prefix = std::strlen(path);
        const std::size_t suffix = std::strlen(entry->d_name);
        if (!full.resize(prefix + suffix + 2u))
        {
            result = EDirectoryQuery::read_failed;
            break;
        }
        std::memcpy(full.data(), path, prefix);
        full[prefix] = '/';
        std::memcpy(full.data() + prefix + 1u, entry->d_name, suffix + 1u);
        struct stat details{};
        if (lstat(full.data(), &details) != 0)
        {
            result = EDirectoryQuery::read_failed;
            break;
        }
        const EDirectoryEntry kind = S_ISREG(details.st_mode) ? EDirectoryEntry::file :
            (S_ISDIR(details.st_mode) ? EDirectoryEntry::directory : EDirectoryEntry::other);
        if (!visitor(context, entry->d_name, kind))
        {
            result = EDirectoryQuery::visitor_failed;
            break;
        }
    }
    if (closedir(handle) != 0)
    {
        result = EDirectoryQuery::read_failed;
    }
    return result;
#endif
}

bool executable_directory(CSimpleString& destination) noexcept
{
    CSimpleString filename;

    //  Deliberately bounded to this development profile; this query does not
    //  promise extended-path support beyond the existing native-path policy.
    constexpr std::size_t query_storage_bytes = 1024u;
#if MV_PLATFORM_WINDOWS
    wchar_t buffer[query_storage_bytes / sizeof(wchar_t)];
    constexpr DWORD capacity = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const DWORD length = GetModuleFileNameW(nullptr, buffer, capacity);
    if ((length == 0u) || (length >= capacity) || !utf8(buffer, static_cast<int>(length), filename))
    {
        return false;
    }
#elif defined(__linux__)
    char buffer[query_storage_bytes];
    const auto length = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if ((length <= 0) || (static_cast<std::size_t>(length) >= sizeof(buffer)) ||
        !filename.set(buffer, static_cast<std::size_t>(length)))
    {
        return false;
    }
#else
    return false;
#endif
    std::size_t end = filename.length();
    while ((end > 0u) && (filename.cstring()[end - 1u] != '/') && (filename.cstring()[end - 1u] != '\\'))
    {
        --end;
    }
    return (end > 0u) && destination.set(filename.cstring(), end - 1u);
}

}   //  namespace platform::filesystem
