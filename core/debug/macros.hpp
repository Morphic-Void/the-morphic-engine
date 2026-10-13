
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

//==============================================================================
//  Build-policy requirements
//==============================================================================

#if !defined(MV_DEVELOPMENT_BUILD) || !defined(MV_DEBUG_DEVELOPMENT_BUILD)
    #error MV_DEVELOPMENT_BUILD and MV_DEBUG_DEVELOPMENT_BUILD must be defined by solution build policy.
#endif

#if !defined(MV_COMPILED_INFORMATION_LEVEL)
    #error MV_COMPILED_INFORMATION_LEVEL must be defined by solution build policy.
#endif

#if (MV_COMPILED_INFORMATION_LEVEL < 0) || (MV_COMPILED_INFORMATION_LEVEL > 3)
    #error MV_COMPILED_INFORMATION_LEVEL must be between 0 (none) and 3 (trace).
#endif

//==============================================================================
//  Internal submission and control helpers
//==============================================================================

#define MV_INTERNAL_USAGE_POINT \
    debug_system::SEventUsagePoint{ \
        __FILE__, sizeof(__FILE__) - 1u, static_cast<std::uint32_t>(__LINE__) }

#define MV_INTERNAL_PROCESS_CONDITION(level, immediate, condition_expression, breakpoint_default, shutdown_reason, expression_text, ...) \
    do \
    { \
        if (!(condition_expression)) \
        { \
            static debug_system::EBreakpointOverride s_breakpoint_override = debug_system::EBreakpointOverride::inherit; \
            (void)debug_system::process_condition_event<level, debug_system::EEventType::condition, immediate>( \
                MV_INTERNAL_USAGE_POINT, s_breakpoint_override, breakpoint_default, shutdown_reason, \
                expression_text, __VA_ARGS__); \
        } \
    } while (0)

#define MV_INTERNAL_PROCESS_EVENT(level, immediate, breakpoint_default, shutdown_reason, ...) \
    do \
    { \
        static debug_system::EBreakpointOverride s_breakpoint_override = debug_system::EBreakpointOverride::inherit; \
        (void)debug_system::process_event<level, debug_system::EEventType::event, immediate>( \
            MV_INTERNAL_USAGE_POINT, s_breakpoint_override, breakpoint_default, shutdown_reason, __VA_ARGS__); \
    } while (0)

#define MV_INTERNAL_INFORMATION_EVENT(level, ...) \
    do \
    { \
        if (debug_system::informational_event_enabled(level)) \
        { \
            __VA_ARGS__; \
        } \
    } while (0)

//  Token dispatch removes disabled informational calls, including their arguments.
//  Only the five diagnostic logging levels are accepted by MV_LOG forms.
#if MV_COMPILED_INFORMATION_LEVEL >= 1
    #define MV_INTERNAL_LOG_info(...) MV_INTERNAL_INFORMATION_EVENT(debug_system::EEventLevel::info, __VA_ARGS__)
#else
    #define MV_INTERNAL_LOG_info(...) do { } while (0)
#endif

#if MV_COMPILED_INFORMATION_LEVEL >= 2
    #define MV_INTERNAL_LOG_detail(...) MV_INTERNAL_INFORMATION_EVENT(debug_system::EEventLevel::detail, __VA_ARGS__)
#else
    #define MV_INTERNAL_LOG_detail(...) do { } while (0)
#endif

#if MV_COMPILED_INFORMATION_LEVEL >= 3
    #define MV_INTERNAL_LOG_trace(...) MV_INTERNAL_INFORMATION_EVENT(debug_system::EEventLevel::trace, __VA_ARGS__)
#else
    #define MV_INTERNAL_LOG_trace(...) do { } while (0)
#endif

#define MV_INTERNAL_LOG_warning(...) do { __VA_ARGS__; } while (0)
#define MV_INTERNAL_LOG_error(...) do { __VA_ARGS__; } while (0)

//==============================================================================
//  General diagnostic reporting: level, representation and delivery
//==============================================================================

//  Levels: info, detail, trace, warning, error (fixed tokens at the call site).
//  Structured forms use bounded {} arguments; rich forms use printf formatting.
//  All forms are diagnostic-only. IMMEDIATE attempts direct write/flush before
//  returning; ordinary delivery queues when possible and can fall back to direct.

#define MV_LOG(level, ...) \
    MV_INTERNAL_LOG_##level( \
        (void)debug_system::submit_event<debug_system::EEventLevel::level, debug_system::EEventType::event, false>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__))

#define MV_LOG_IMMEDIATE(level, ...) \
    MV_INTERNAL_LOG_##level( \
        (void)debug_system::submit_event<debug_system::EEventLevel::level, debug_system::EEventType::event, true>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__))

#define MV_LOG_RICH(level, ...) \
    MV_INTERNAL_LOG_##level( \
        (void)debug_system::log_rich<debug_system::EEventLevel::level, false>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__))

#define MV_LOG_RICH_IMMEDIATE(level, ...) \
    MV_INTERNAL_LOG_##level( \
        (void)debug_system::log_rich<debug_system::EEventLevel::level, true>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__))

//==============================================================================
//  Informational conveniences
//==============================================================================

#define MV_INFO(...) MV_LOG(info, __VA_ARGS__)
#define MV_DETAIL(...) MV_LOG(detail, __VA_ARGS__)
#define MV_TRACE(...) MV_LOG(trace, __VA_ARGS__)

//==============================================================================
//  Diagnostic assertion reporting
//==============================================================================

//  Already-detected contract failures, in every build and without filtering,
//  breakpoint processing or shutdown. The caller owns recovery and continuation.

#define MV_ASSERT_REPORT(...) \
    do \
    { \
        (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition, false>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

#define MV_ASSERT_REPORT_IMMEDIATE(...) \
    do \
    { \
        (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition, true>( \
            MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

//==============================================================================
//  Condition assertions: ordinary and immediate delivery
//==============================================================================

//  IMMEDIATE changes delivery only; condition evaluation, build gating and
//  breakpoint/shutdown policy are shared with the corresponding ordinary form.

#if MV_DEVELOPMENT_BUILD
    #define MV_ASSERT(condition) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, false, condition, true, \
            debug_system::EShutdownReason::none, \
            "Assertion failed: " #condition, "")

    #define MV_ASSERT_IMMEDIATE(condition) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, true, condition, true, \
            debug_system::EShutdownReason::none, \
            "Assertion failed: " #condition, "")

    #define MV_ASSERT_MSG(condition, ...) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, false, condition, true, \
            debug_system::EShutdownReason::none, \
            "Assertion failed: " #condition " | ", __VA_ARGS__)

    #define MV_ASSERT_MSG_IMMEDIATE(condition, ...) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, true, condition, true, \
            debug_system::EShutdownReason::none, \
            "Assertion failed: " #condition " | ", __VA_ARGS__)

#else
    #define MV_ASSERT(condition) do { } while (0)
    #define MV_ASSERT_IMMEDIATE(condition) do { } while (0)
    #define MV_ASSERT_MSG(condition, ...) do { } while (0)
    #define MV_ASSERT_MSG_IMMEDIATE(condition, ...) do { } while (0)
#endif

#if MV_DEBUG_DEVELOPMENT_BUILD
    #define MV_DEBUG_ASSERT(condition) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, false, condition, true, \
            debug_system::EShutdownReason::none, \
            "Debug assertion failed: " #condition, "")

    #define MV_DEBUG_ASSERT_IMMEDIATE(condition) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, true, condition, true, \
            debug_system::EShutdownReason::none, \
            "Debug assertion failed: " #condition, "")

    #define MV_DEBUG_ASSERT_MSG(condition, ...) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, false, condition, true, \
            debug_system::EShutdownReason::none, \
            "Debug assertion failed: " #condition " | ", __VA_ARGS__)

    #define MV_DEBUG_ASSERT_MSG_IMMEDIATE(condition, ...) \
        MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::assert, true, condition, true, \
            debug_system::EShutdownReason::none, \
            "Debug assertion failed: " #condition " | ", __VA_ARGS__)

#else
    #define MV_DEBUG_ASSERT(condition) do { } while (0)
    #define MV_DEBUG_ASSERT_IMMEDIATE(condition) do { } while (0)
    #define MV_DEBUG_ASSERT_MSG(condition, ...) do { } while (0)
    #define MV_DEBUG_ASSERT_MSG_IMMEDIATE(condition, ...) do { } while (0)
#endif

#define MV_CRITICAL_ASSERT(condition) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::critical, false, condition, true, \
        debug_system::EShutdownReason::critical_incident, \
        "Critical assertion failed: " #condition, "")

#define MV_CRITICAL_ASSERT_IMMEDIATE(condition) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::critical, true, condition, true, \
        debug_system::EShutdownReason::critical_incident, \
        "Critical assertion failed: " #condition, "")

#define MV_CRITICAL_ASSERT_MSG(condition, ...) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::critical, false, condition, true, \
        debug_system::EShutdownReason::critical_incident, \
        "Critical assertion failed: " #condition " | ", __VA_ARGS__)

#define MV_CRITICAL_ASSERT_MSG_IMMEDIATE(condition, ...) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::critical, true, condition, true, \
        debug_system::EShutdownReason::critical_incident, \
        "Critical assertion failed: " #condition " | ", __VA_ARGS__)

#define MV_FATAL_ASSERT(condition) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::fatal, false, condition, true, \
        debug_system::EShutdownReason::fatal_incident, \
        "Fatal assertion failed: " #condition, "")

#define MV_FATAL_ASSERT_IMMEDIATE(condition) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::fatal, true, condition, true, \
        debug_system::EShutdownReason::fatal_incident, \
        "Fatal assertion failed: " #condition, "")

#define MV_FATAL_ASSERT_MSG(condition, ...) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::fatal, false, condition, true, \
        debug_system::EShutdownReason::fatal_incident, \
        "Fatal assertion failed: " #condition " | ", __VA_ARGS__)

#define MV_FATAL_ASSERT_MSG_IMMEDIATE(condition, ...) \
    MV_INTERNAL_PROCESS_CONDITION(debug_system::EEventLevel::fatal, true, condition, true, \
        debug_system::EShutdownReason::fatal_incident, \
        "Fatal assertion failed: " #condition " | ", __VA_ARGS__)

//==============================================================================
//  Abnormal events with breakpoint and shutdown policy
//==============================================================================

#define MV_WARNING(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::warning, false, false, \
        debug_system::EShutdownReason::none, __VA_ARGS__)

#define MV_WARNING_IMMEDIATE(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::warning, true, false, \
        debug_system::EShutdownReason::none, __VA_ARGS__)

#define MV_ERROR(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::error, false, true, \
        debug_system::EShutdownReason::none, __VA_ARGS__)

#define MV_ERROR_IMMEDIATE(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::error, true, true, \
        debug_system::EShutdownReason::none, __VA_ARGS__)

#define MV_CRITICAL_EVENT(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::critical, false, true, \
        debug_system::EShutdownReason::critical_incident, __VA_ARGS__)

#define MV_CRITICAL_EVENT_IMMEDIATE(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::critical, true, true, \
        debug_system::EShutdownReason::critical_incident, __VA_ARGS__)

#define MV_FATAL_EVENT(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::fatal, false, true, \
        debug_system::EShutdownReason::fatal_incident, __VA_ARGS__)

#define MV_FATAL_EVENT_IMMEDIATE(...) \
    MV_INTERNAL_PROCESS_EVENT(debug_system::EEventLevel::fatal, true, true, \
        debug_system::EShutdownReason::fatal_incident, __VA_ARGS__)

//==============================================================================
//  Emergency reporting and build-only evaluation
//==============================================================================

#define MV_PANIC(...) \
    do \
    { \
        debug_system::panic(MV_INTERNAL_USAGE_POINT, __VA_ARGS__); \
    } while (0)

#if MV_DEBUG_DEVELOPMENT_BUILD
    #define MV_DEBUG_ONLY(expression) do { (void)(expression); } while (0)
#else
    #define MV_DEBUG_ONLY(expression) do { } while (0)
#endif

#endif  //  #ifndef DEBUG_MACROS_HPP_INCLUDED
