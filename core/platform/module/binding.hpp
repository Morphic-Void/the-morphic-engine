
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   binding.hpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Module (DLL/SO/DYLIB) binding.
//
//  CPlatformModule owns a native module handle. It is move-only.
//
//  Note: get_modules_std_ext() returns the extension without any preceding '.'

#pragma once

#ifndef MODULE_BINDING_HPP_INCLUDED
#define MODULE_BINDING_HPP_INCLUDED

#include "platform/path/native_path.hpp"
#include "platform/platform_defines.hpp"
#include "containers/StringBuffers.hpp"

namespace platform::module
{

using FModuleFunction = void(MV_STD_ABI_CALL*)() noexcept;

const char* get_modules_std_ext() noexcept;

//  Append '.' and the native extension to a basename or logical path stem.
//  The destination is unchanged on failure; an existing suffix is not removed.
[[nodiscard]] bool make_module_filename(const char* stem, CSimpleString& destination) noexcept;

//  Configuration-only substitution of ${module_extension}; other text is literal.
//  No allocation. Source and destination must not overlap. Failure leaves the
//  destination unchanged; capacity includes the terminating NUL.
[[nodiscard]] bool expand_module_extension(const char* source, char* destination, std::size_t capacity) noexcept;

class CPlatformModule
{
public:

    //  Default and deleted lifetime
    CPlatformModule() noexcept = default;
    CPlatformModule(const CPlatformModule& other) = delete;
    CPlatformModule& operator=(const CPlatformModule& other) = delete;

    //  Move lifetime and destructor
    CPlatformModule(CPlatformModule&& other) noexcept;
    CPlatformModule& operator=(CPlatformModule&& other) noexcept;
    ~CPlatformModule() noexcept;

    //  Binding
    [[nodiscard]] bool bind(const platform::path::NativePath& path) noexcept;
    [[nodiscard]] bool unbind() noexcept;
    //  Relinquishes handle ownership without unloading the native module.
    [[nodiscard]] void* release_without_unload() noexcept;

    //  Queries
    [[nodiscard]] bool is_bound() const noexcept;
    [[nodiscard]] FModuleFunction find_function(const char* const symbol_name) const noexcept;
    [[nodiscard]] void* find_symbol(const char* const symbol_name) const noexcept;
    [[nodiscard]] void* native_handle() const noexcept;

private:

    void* m_native_handle = nullptr;
};

}   //  namespace platform::module

#endif  //  #ifndef MODULE_BINDING_HPP_INCLUDED
