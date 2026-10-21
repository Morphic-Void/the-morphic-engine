
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   binding.cpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Module (DLL/SO/DYLIB) binding.

#include "platform/module/binding.hpp"
#include "platform/platform_defines.hpp"
#include <cstring>
#include <limits>
#include <utility>

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#else
#include <dlfcn.h>
#endif

namespace platform::module
{

const char* get_modules_std_ext() noexcept
{
#if MV_PLATFORM_WINDOWS
    static const char* k_ext = "dll";
#elif MV_PLATFORM_MAC_OS
    static const char* k_ext = "dylib";
#else
    static const char* k_ext = "so";
#endif

    return k_ext;
}

bool make_module_filename(const char* const stem, CSimpleString& destination) noexcept
{
    if ((stem == nullptr) || (*stem == '\0')) { return false; }
    const char* const extension = get_modules_std_ext();
    const std::size_t length = std::strlen(stem);
    const std::size_t suffix = std::strlen(extension);
    if (length > (std::numeric_limits<std::size_t>::max() - suffix - 2u)) { return false; }
    TPodVector<char> filename;
    if (!filename.resize(length + suffix + 2u)) { return false; }
    std::memcpy(filename.data(), stem, length);
    filename[length] = '.';
    std::memcpy(filename.data() + length + 1u, extension, suffix + 1u);
    CSimpleString result;
    if (!result.set(filename.data())) { return false; }
    destination = std::move(result);
    return true;
}

bool expand_module_extension(const char* const source, char* const destination, const std::size_t capacity) noexcept
{
    if ((source == nullptr) || (destination == nullptr) || (capacity == 0u)) { return false; }
    static constexpr char token[] = "${module_extension}";
    const char* const extension = get_modules_std_ext();
    const std::size_t suffix = std::strlen(extension);
    std::size_t length = 0u;
    for (const char* cursor = source; *cursor != '\0';)
    {
        const bool replace = std::strncmp(cursor, token, sizeof(token) - 1u) == 0;
        const std::size_t count = replace ? suffix : 1u;
        if (count >= (capacity - length)) { return false; }
        length += count;
        cursor += replace ? sizeof(token) - 1u : 1u;
    }
    char* output = destination;
    for (const char* cursor = source; *cursor != '\0';)
    {
        if (std::strncmp(cursor, token, sizeof(token) - 1u) == 0)
        {
            std::memcpy(output, extension, suffix);
            output += suffix;
            cursor += sizeof(token) - 1u;
        }
        else { *output++ = *cursor++; }
    }
    *output = '\0';
    return true;
}

CPlatformModule::~CPlatformModule() noexcept
{
    (void)unbind();
}

CPlatformModule::CPlatformModule(CPlatformModule&& other) noexcept : m_native_handle(other.m_native_handle)
{
    other.m_native_handle = nullptr;
}

CPlatformModule& CPlatformModule::operator=(CPlatformModule&& other) noexcept
{
    if (this != &other)
    {
        (void)unbind();

        m_native_handle = other.m_native_handle;
        other.m_native_handle = nullptr;
    }
    return *this;
}

bool CPlatformModule::bind(const platform::path::NativePath& path) noexcept
{
    bool success = false;

    if ((m_native_handle == nullptr) && !path.is_empty())
    {
#if MV_PLATFORM_WINDOWS
        HMODULE handle = LoadLibraryW(path.data());

        if (handle != nullptr)
        {
            m_native_handle = reinterpret_cast<void*>(handle);
            success = true;
        }
#else
        void* handle = dlopen(path.data(), RTLD_NOW | RTLD_LOCAL);

        if (handle != nullptr)
        {
            m_native_handle = handle;
            success = true;
        }
#endif
    }

    return success;
}

bool CPlatformModule::unbind() noexcept
{
    bool success = true;

    if (m_native_handle != nullptr)
    {
#if MV_PLATFORM_WINDOWS
        HMODULE handle = reinterpret_cast<HMODULE>(m_native_handle);
        success = (FreeLibrary(handle) != 0);
#else
        success = (dlclose(m_native_handle) == 0);
#endif

        if (success)
        {
            m_native_handle = nullptr;
        }
    }

    return success;
}

void* CPlatformModule::release_without_unload() noexcept
{
    void* const native_handle = m_native_handle;
    m_native_handle = nullptr;
    return native_handle;
}

bool CPlatformModule::is_bound() const noexcept
{
    return m_native_handle != nullptr;
}

void* CPlatformModule::find_symbol(const char* const symbol_name) const noexcept
{
    void* symbol = nullptr;

    if ((m_native_handle != nullptr) && (symbol_name != nullptr) && (symbol_name[0] != 0))
    {
#if MV_PLATFORM_WINDOWS
        HMODULE handle = reinterpret_cast<HMODULE>(m_native_handle);
        FARPROC proc = GetProcAddress(handle, symbol_name);

        if (proc != nullptr)
        {
            symbol = reinterpret_cast<void*>(proc);
        }
#else
        symbol = dlsym(m_native_handle, symbol_name);
#endif
    }

    return symbol;
}

FModuleFunction CPlatformModule::find_function(const char* const symbol_name) const noexcept
{
    FModuleFunction symbol = nullptr;

    if ((m_native_handle != nullptr) && (symbol_name != nullptr) && (symbol_name[0] != 0))
    {
#if MV_PLATFORM_WINDOWS
        HMODULE handle = reinterpret_cast<HMODULE>(m_native_handle);
        const FARPROC proc = GetProcAddress(handle, symbol_name);

        if (proc != nullptr)
        {
            symbol = reinterpret_cast<FModuleFunction>(proc);
        }
#else
        symbol = reinterpret_cast<FModuleFunction>(dlsym(m_native_handle, symbol_name));
#endif
    }

    return symbol;
}

void* CPlatformModule::native_handle() const noexcept
{
    return m_native_handle;
}

}   //  namespace platform::module
