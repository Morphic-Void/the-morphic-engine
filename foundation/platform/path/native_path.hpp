
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   native_path.hpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Platform native path container and utils
//
//  Notes:
//  - Requires C++ 17 or greater
//  - No exceptions.

#pragma once

#ifndef NATIVE_PATH_HPP_INCLUDED
#define NATIVE_PATH_HPP_INCLUDED

#include "containers/TPodVector.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
using native_char = wchar_t;
#else
using native_char = char;
#endif

namespace platform::path
{

using NativePath = TPodVector<native_char>;
NativePath makeNativePath(const char* const utf8_path) noexcept;  // UTF-8 -> native (with policy)
NativePath makeTempNativePath(const NativePath& std_path) noexcept; // append ".tmp"

}   //  namespace platform::path

#endif  //  #ifndef NATIVE_PATH_HPP_INCLUDED
