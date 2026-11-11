
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   native_path.cpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Platform native path container and utils
//
//  Notes:
//  - Requires C++ 17 or greater
//  - No exceptions.

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

#include "platform/path/native_path.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
    #include <cwchar>
    #include "platform/windows_include.hpp"
#endif

namespace platform::path
{

NativePath makeNativePath(const char* const utf8_path) noexcept
{
    NativePath std_path;
    if ((utf8_path != nullptr) && (utf8_path[0] != 0))
    {   //  input path is not nullptr or an empty string
        std::size_t path_length = std::strlen(utf8_path);
        std::size_t name_index = 0;
        bool has_wildcard = false;
        TPodVector<char> cpath;
        if (cpath.allocate(path_length + 1u))
        {
            (void)cpath.set_size(path_length + 1u);
            for (std::size_t byte_index = 0; byte_index <= path_length; ++byte_index)
            {
                char byte = utf8_path[byte_index];
                if (byte == '\\')
                {
                    byte = '/';
                }
                if (byte == '/')
                {
                    name_index = byte_index + 1;
                }
                else if ((byte == '?') || (byte == '*'))
                {
                    has_wildcard = true;
                }
                cpath[byte_index] = byte;
            }
            std::size_t name_length = path_length - name_index;
            bool path_is_valid = !(has_wildcard || (name_length == 0) || (cpath[path_length - 1] == '/'));
            if (path_is_valid)
            {
                const char name_byte0 = cpath[name_index];
                const char name_byte1 = cpath[name_index + 1];
                if ((name_byte0 == '.') && ((name_length == 1) || ((name_length == 2) && (name_byte1 == '.'))))
                {   //  directory navigation only
                    path_is_valid = false;
                }
#if MV_PLATFORM_WINDOWS
                else if (path_length >= 2)
                {
                    const char path_byte0 = cpath[0];
                    const char path_byte1 = cpath[1];
                    if ((path_byte0 == '/') && (path_byte1 == '/'))
                    {   //  reject Windows UNC and extended-length
                        path_is_valid = false;
                    }
                    else if ((path_length == 2) && (path_byte1 == ':') && std::isalpha(static_cast<unsigned char>(path_byte0)))
                    {   //  reject drive only
                        path_is_valid = false;
                    }
                }
#endif
            }
#if MV_PLATFORM_WINDOWS
            if (path_is_valid)
            {
                const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cpath.data(), -1, nullptr, 0);
                if (wlen > 0)
                {   //  success, the returned wlen is the wchar count INCLUDING the null terminator
                    NativePath wpath;
                    if (wpath.allocate(static_cast<std::size_t>(wlen)))
                    {
                        (void)wpath.set_size(static_cast<std::size_t>(wlen));
                        const int wchk = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cpath.data(), -1, wpath.data(), wlen);
                        if (wchk == wlen)
                        {   //  success, the returned size is the wchar count INCLUDING the null terminator
                            std_path = std::move(wpath);
                        }
                    }
                }
            }
#else
            if (path_is_valid)
            {
                std_path = std::move(cpath);
            }
#endif
        }
    }
    return std_path;
}

NativePath makeTempNativePath(const NativePath& std_path) noexcept
{
    NativePath tmp_path;
    if (!std_path.is_empty())
    {
#if MV_PLATFORM_WINDOWS
        std::size_t tmp_length = std::wcslen(std_path.data()) + 5u;
#else
        std::size_t tmp_length = std::strlen(std_path.data()) + 5u;
#endif
        if (tmp_path.allocate(tmp_length))
        {
            (void)tmp_path.set_size(tmp_length);
#if MV_PLATFORM_WINDOWS
            if (_snwprintf_s(tmp_path.data(), tmp_length, (tmp_length - 1), L"%s.tmp", std_path.data()) < 0)
#else
            if (std::snprintf(tmp_path.data(), tmp_length, "%s.tmp", std_path.data()) < 0)
#endif
            {
                tmp_path.deallocate();
            }
        }
    }
    return tmp_path;
}

}   //  namespace platform::path
