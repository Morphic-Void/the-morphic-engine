
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_defines.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Platform selection and capability defines for low-level platform
//  implementation files.
//
//  This header classifies the target platform and derives coarse
//  platform capabilities.
//
//  This header does NOT declare platform APIs or provide feature
//  wrappers and these should not be added to this file.
//
//  Supported targets
//  -----------------
//  Primary:
//  - Windows.
//  - Linux desktop.
//
//  Secondary:
//  - Android API 32 or later.
//
//  Tertiary:
//  - macOS 14.4 or later.
//
//  Unsupported Apple targets
//  -------------------------
//  iOS, iPadOS, tvOS, watchOS, visionOS, simulators, and older macOS
//  deployment targets are rejected until deliberately enabled.
//
//  Platform defines
//  ----------------
//  Exactly one MV_PLATFORM_* target define is set to 1.
//
//  MV_PLATFORM_WINDOWS
//  MV_PLATFORM_LINUX
//  MV_PLATFORM_ANDROID
//  MV_PLATFORM_MAC_OS
//
//  Capability defines
//  ------------------
//  MV_PLATFORM_HAS_PTHREADS
//      Target has pthread support available to platform implementation code.
//
//  MV_PLATFORM_HAS_NATIVE_WAIT_WORD
//      Target has a native 32-bit wait-by-address / wake-by-address primitive
//      suitable for platform::threading wait-word support.
//
//      Timed calls additionally require their deadline clock/backend at runtime;
//      Windows timed support requires QueryUnbiasedInterruptTimePrecise (10+).
//      This capability does not authorise an indefinite fallback for timed calls.
//
//  MV_PLATFORM_NATIVE_WAIT_WORD_EXPERIMENTAL
//      Target is routed through native wait-word support, but the platform
//      implementation has not yet been validated on the target devices.

#pragma once

#ifndef PLATFORM_DEFINES_HPP_INCLUDED
#define PLATFORM_DEFINES_HPP_INCLUDED

//==============================================================================
//  Platform defaults
//==============================================================================

#define MV_PLATFORM_WINDOWS 0
#define MV_PLATFORM_LINUX   0
#define MV_PLATFORM_ANDROID 0
#define MV_PLATFORM_MAC_OS  0

#define MV_PLATFORM_HAS_PTHREADS                  0
#define MV_PLATFORM_HAS_NATIVE_WAIT_WORD          0
#define MV_PLATFORM_NATIVE_WAIT_WORD_EXPERIMENTAL 0

//==============================================================================
//  Platform classification
//==============================================================================

#if defined(_WIN32) || defined(_WIN64)

#undef  MV_PLATFORM_WINDOWS
#define MV_PLATFORM_WINDOWS 1

#elif defined(__ANDROID__)

#undef  MV_PLATFORM_ANDROID
#define MV_PLATFORM_ANDROID 1

#elif defined(__linux__)

#undef  MV_PLATFORM_LINUX
#define MV_PLATFORM_LINUX 1

#elif defined(__APPLE__) && defined(__MACH__)

#undef  MV_PLATFORM_MAC_OS
#define MV_PLATFORM_MAC_OS 1

#else

#error "Unsupported platform."

#endif

//==============================================================================
//  Supported platform validation
//==============================================================================

#if MV_PLATFORM_ANDROID

#if !defined(__ANDROID_API__) || (__ANDROID_API__ < 32)
#error "Unsupported Android target. Requires Android API 32 or later."
#endif

#endif  //  #if MV_PLATFORM_ANDROID

#if MV_PLATFORM_MAC_OS

#include <Availability.h>
#include <TargetConditionals.h>

#if !defined(TARGET_OS_OSX) || !TARGET_OS_OSX
#error "Unsupported Apple target. Only macOS 14.4 or later is currently supported."
#endif

#if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED) || (__MAC_OS_X_VERSION_MIN_REQUIRED < 140400)
#error "Unsupported macOS target. Requires macOS 14.4 or later."
#endif

#endif  //  #if MV_PLATFORM_MAC_OS

//==============================================================================
//  Platform capabilities
//==============================================================================

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID || MV_PLATFORM_MAC_OS
#undef  MV_PLATFORM_HAS_PTHREADS
#define MV_PLATFORM_HAS_PTHREADS 1
#endif

#if MV_PLATFORM_WINDOWS || MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID || MV_PLATFORM_MAC_OS
#undef  MV_PLATFORM_HAS_NATIVE_WAIT_WORD
#define MV_PLATFORM_HAS_NATIVE_WAIT_WORD 1
#endif

#if MV_PLATFORM_ANDROID
#undef  MV_PLATFORM_NATIVE_WAIT_WORD_EXPERIMENTAL
#define MV_PLATFORM_NATIVE_WAIT_WORD_EXPERIMENTAL 1
#endif

//==============================================================================
//  Platform ABI
//==============================================================================

#if defined(_MSC_VER) && defined(_M_IX86)
#define MV_STD_ABI_CALL __stdcall
#else
#define MV_STD_ABI_CALL
#endif

#if MV_PLATFORM_WINDOWS && defined(_MSC_VER) && defined(_M_IX86)
#define MV_MODULE_EXPORT extern "C"
#elif MV_PLATFORM_WINDOWS
#define MV_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define MV_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#endif  //  #ifndef PLATFORM_DEFINES_HPP_INCLUDED
