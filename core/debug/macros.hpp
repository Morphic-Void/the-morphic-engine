
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   macros.hpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:   3 Aug 26
//
//  Public debug reporting macros.

#pragma once

#ifndef DEBUG_MACROS_HPP_INCLUDED
#define DEBUG_MACROS_HPP_INCLUDED

#include "debug/service.hpp"

#if !defined(MV_DEVELOPMENT_BUILD) || !defined(MV_DEBUG_DEVELOPMENT_BUILD)
    #error MV_DEVELOPMENT_BUILD and MV_DEBUG_DEVELOPMENT_BUILD must be defined by solution build policy.
#endif

#if !defined(MV_COMPILED_INFORMATION_LEVEL)
    #error MV_COMPILED_INFORMATION_LEVEL must be defined by solution build policy.
#endif

#if (MV_COMPILED_INFORMATION_LEVEL < 0) || (MV_COMPILED_INFORMATION_LEVEL > 3)
    #error MV_COMPILED_INFORMATION_LEVEL must be between 0 (none) and 3 (trace).
#endif

#define MV_INTERNAL_USAGE_POINT \
    debug_system::SEventUsagePoint{ \
        __FILE__, sizeof(__FILE__) - 1u, static_cast<std::uint32_t>(__LINE__) }

#define MV_INTERNAL_PROCESS_CONDITION(level, condition_expression, breakpoint_default, shutdown_reason, expression_text, ...) \
    do \
    { \
        if (!(condition_expression)) \
        { \
            static debug_system::EBreakpointOverride s_breakpoint_override = debug_system::EBreakpointOverride::inherit; \
            (void)debug_system::process_condition_event<level, debug_system::EEventType::condition>( \
                MV_INTERNAL_USAGE_POINT, s_breakpoint_override, breakpoint_default, shutdown_reason, \
                expression_text, __VA_ARGS__); \
        } \
    } while (0)

#define MV_INTERNAL_PROCESS_EVENT(level, breakpoint_default, shutdown_reason, ...) \
    do \
    { \
        static debug_system::EBreakpointOverride s_breakpoint_override = debug_system::EBreakpointOverride::inherit; \
        (void)debug_system::process_event<level, debug_system::EEventType::event>( \
            MV_INTERNAL_USAGE_POINT, s_breakpoint_override, breakpoint_default, shutdown_reason, \
            __VA_ARGS__); \
    } while (0)

#define MV_INTERNAL_INFORMATION_EVENT(level, ...) \
    do \
    { \
        if (debug_system::informational_event_enabled(level)) \
        { \
            (void)debug_system::submit_event<level, debug_system::EEventType::event>(MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
        } \
    } while (0)

#if MV_DEVELOPMENT_BUILD
    #define MV_ASSERT(condition) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, condition, true, \
            debug_system::EShutdownReason::none, \
            "Assertion failed: " #condition, "")
    #define MV_ASSERT_MSG(condition, ...) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, condition, true, \
            debug_system::EShutdownReason::none, \
            "Assertion failed: " #condition " | ", __VA_ARGS__)
#else
    #define MV_ASSERT(condition) do { } while (0)
    #define MV_ASSERT_MSG(condition, ...) do { } while (0)
#endif

//  Diagnostic-only assertion reporting in every build. The caller has already
//  detected the failure and owns continuation; breakpoint/shutdown policy is not applied.
#define MV_ASSERT_REPORT(...) \
    do \
    { \
        (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

#if MV_DEBUG_DEVELOPMENT_BUILD
    #define MV_DEBUG_ASSERT(condition) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, condition, true, \
            debug_system::EShutdownReason::none, \
            "Debug assertion failed: " #condition, "")
    #define MV_DEBUG_ASSERT_MSG(condition, ...) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, condition, true, \
            debug_system::EShutdownReason::none, \
            "Debug assertion failed: " #condition " | ", __VA_ARGS__)
    #define MV_DEBUG_ONLY(expression) do { (void)(expression); } while (0)
#else
    #define MV_DEBUG_ASSERT(condition) do { } while (0)
    #define MV_DEBUG_ASSERT_MSG(condition, ...) do { } while (0)
    #define MV_DEBUG_ONLY(expression) do { } while (0)
#endif

#define MV_CRITICAL_ASSERT(condition) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::critical, condition, true, \
        debug_system::EShutdownReason::critical_incident, \
        "Critical assertion failed: " #condition, "")

#define MV_CRITICAL_ASSERT_MSG(condition, ...) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::critical, condition, true, \
        debug_system::EShutdownReason::critical_incident, \
        "Critical assertion failed: " #condition " | ", __VA_ARGS__)

#define MV_FATAL_ASSERT(condition) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::fatal, condition, true, \
        debug_system::EShutdownReason::fatal_incident, \
        "Fatal assertion failed: " #condition, "")

#define MV_FATAL_ASSERT_MSG(condition, ...) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::fatal, condition, true, \
        debug_system::EShutdownReason::fatal_incident, \
        "Fatal assertion failed: " #condition " | ", __VA_ARGS__)

#if MV_COMPILED_INFORMATION_LEVEL >= 1
    #define MV_INFO(...) MV_INTERNAL_INFORMATION_EVENT(debug_system::EEventLevel::info, __VA_ARGS__)
#else
    #define MV_INFO(...) do { } while (0)
#endif

#if MV_COMPILED_INFORMATION_LEVEL >= 2
    #define MV_DETAIL(...) MV_INTERNAL_INFORMATION_EVENT(debug_system::EEventLevel::detail, __VA_ARGS__)
#else
    #define MV_DETAIL(...) do { } while (0)
#endif

#if MV_COMPILED_INFORMATION_LEVEL >= 3
    #define MV_TRACE(...) MV_INTERNAL_INFORMATION_EVENT(debug_system::EEventLevel::trace, __VA_ARGS__)
#else
    #define MV_TRACE(...) do { } while (0)
#endif

#define MV_REPORT(...) \
    do \
    { \
        (void)debug_system::report(MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

#define MV_REPORT_IMMEDIATE(...) \
    do \
    { \
        (void)debug_system::report_immediate(MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

#define MV_WARNING(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::warning, false, \
        debug_system::EShutdownReason::none, __VA_ARGS__)

#define MV_ERROR(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::error, true, \
        debug_system::EShutdownReason::none, __VA_ARGS__)

#define MV_CRITICAL_EVENT(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::critical, true, \
        debug_system::EShutdownReason::critical_incident, __VA_ARGS__)

#define MV_FATAL_EVENT(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::fatal, true, \
        debug_system::EShutdownReason::fatal_incident, __VA_ARGS__)

#define MV_PANIC(...) \
    do \
    { \
        debug_system::panic(MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

#endif  //  #ifndef DEBUG_MACROS_HPP_INCLUDED
